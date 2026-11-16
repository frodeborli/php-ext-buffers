# Proposal: defined Tensor arithmetic, capability-based backends

Status: decided, ready to implement (2026-11-16). Agreed between the ext-buffers session (wasm-1d) and the Skein session
(phasync-experiments, checked against libskein `ea3ed11`). Nothing is implemented. Replaces C API 2's operator hook,
so it becomes C API 3. An earlier draft added deferred execution to `Buffers\Tensor` (pending nodes, write tracking, a
descriptor API for Skein); Frode's alternative replaced it: `Buffers\Tensor` is eager, and laziness lives in Skein's own
types.

## Problem

`buffers_set_operator_handler()` registers one process-wide handler; the first extension to register it decides what
`+ - * / % ** << >> | & ^ ~` mean on a `Buffers\Tensor`, and a second registrant gets `FAILURE`. So `$a + $b` on two
tensors is a `TypeError` with nothing loaded, a lazy `Skein\Expression` with Skein loaded (`fn.c` `ps_operator_handler`),
and would be an eager tensor with some other math extension. The hook defines *semantics*, not only implementation, and
no rules exist for result dtypes, integer overflow, division by zero or rounding. A standard-library type cannot mean
different things depending on which extension was loaded first.

## Decisions (agreed)

1. **ext-buffers defines the semantics**: eager, element-wise, the rules below. `$a + $b` on `Buffers\Tensor` operands
   computes at once and returns a new `Tensor` (or throws), whatever is loaded.
2. **ext-buffers ships a reference implementation** in plain C for host memory, so arithmetic works with no other
   extension loaded.
3. **Backends only accelerate.** A backend registers a capability check and an implementation; it must produce the
   results the rules define (see Exactness).
4. **Laziness lives in other libraries' types, not in `Buffers\Tensor`.** Skein's `Expression` (and a proposed
   `Skein\Tensor`: an immutable `Expression` holding fixed numbers, returned by Skein's loaders; Skein's decision) builds
   graphs with its own `do_operation`. Because of decision 5 laziness is contagious: `$tensor + $expression` and
   `$expression + $tensor` both reach Skein and stay lazy; only expressions made entirely of `Buffers\Tensor` operands
   run eagerly. The cost: `$img / 255 - 0.5` on plain tensors runs on the CPU before reaching a Skein model (correct,
   not fused); `Expression::of($img) / 255 - 0.5` opts into fusion. This keeps PHP value semantics trivially true and
   avoids write tracking on every write path.
5. **Other operands.** `Tensor`'s `do_operation` treats the other operand, in either position and for compound
   assignment, as follows:
   1. a `Buffers\Tensor`, or a PHP `int`/`float`/`bool`: computed eagerly;
   2. an object whose class has its own `do_operation` handler (an extension class with arithmetic, such as
      `Skein\Expression`): `FAILURE` without throwing, so PHP asks that object and laziness stays contagious
      (`$tensor + new Skein\Input('x')` never tries to compute);
   3. an object implementing `TensorAggregate` without a `do_operation` of its own (in practice a plain PHP class: a
      sensor, a database column, a file format; PHP code cannot define operators): `getTensor()` is called and the
      operation computed eagerly, so `$sensor + $tensor` works in either order;
   4. anything else: `FAILURE`, so PHP throws its `TypeError` ("Unsupported operand types").
   Tested with a PHP `TensorAggregate` and a dummy extension class with its own `do_operation`, each on either side and
   in compound assignment. (Refinement proposed by the Skein session.)
6. **Mixed devices fail.** If no backend can run an operation on its operands (a CPU tensor plus a GPU tensor), it is an
   `Error` naming the devices. No implicit copies.

## Semantics

### Operands
`Tensor op Tensor`, `Tensor op int|float|bool` and `int|float|bool op Tensor`, plus unary `~` and `-` (PHP compiles
`-$x` to `$x * -1`; the result is the same value). Anything else returns `FAILURE` (decision 5). Shapes broadcast by
the Array API rules (dimensions are aligned from the right; a dimension of 1 stretches), so a `[64]` bias adds to every
row of a `[32, 64]` matrix. Incompatible shapes are a `ValueError` naming both shapes. The result is C-contiguous, on
the operands' device, in memory that counts toward `memory_limit` when it is host memory.

### Result dtype
One promotion function, exposed through the C API as `buffers_dtype_promote(a, b, op)` and published as a table
(`tests/promotion.tsv`: pairs and operator to result dtype or error). Skein will call the function instead of its own
`sk_dtype_promote`, and both projects test against the table, so Skein's lazy results and ext-buffers' eager results get
identical dtypes. Base: the [Python Array API standard](https://data-apis.org/array-api/latest/API_specification/type_promotion.html),
with the gaps it leaves filled explicitly:

* Same kind: the wider type (`Int8 + Int32` is `Int32`). Signed with unsigned: the smallest signed type that holds both
  (`UInt8 + Int8` is `Int16`); `UInt64` with a signed type is an error (no such type), as in the standard.
* Integer with float: the float type (`Int32 + Float32` is `Float32`). The standard leaves this to implementations;
  this is NumPy's and Skein's choice in practice for tensors of floats.
* `BFloat16` (not in the standard): with `Float32`/`Float64` gives the wider; with `Float16` is an error asking for an
  explicit `convert()` (no common type without losing range or precision). Skein does the same today.
* PHP scalars are *weak* (the standard's rule for Python scalars): `$uint8Tensor + 1` stays `UInt8`, `$float16Tensor * 0.5`
  stays `Float16`. A PHP float with an integer tensor gives `Float64`. A scalar that does not fit the tensor's integer
  type is a `ValueError`, not a silent wrap.
* `/` always gives a float type (integer operands give `Float64`, as PHP's `/` gives a float).
* `Bool`: `& | ^ ~` stay `Bool`. Arithmetic on `Bool` (`+ - * / % ** << >>`) is a `TypeError` that suggests `convert()`,
  as in the Array API standard (decided).
* Storage-only formats (`Float8*`, `Float6*`, `Float4*`, the packed `Int2/Int4/UInt2/UInt4`): not arithmetic dtypes;
  a `TypeError` that suggests `convert()`. `Complex*`: later.

### Gaps filled (agreed with the Skein session)

* `%` on float tensors is C `fmod`: the sign of the dividend, like integer `%`, and exact.
* Shifts and bitwise operators: integer dtypes only (`& | ^` also `Bool` with `Bool`), operands promoted together as in
  the Array API.
* A PHP int that does not fit an unsigned tensor's dtype is a `ValueError`, so `-$uint8Tensor` (`$t * -1`) throws, as
  in NumPy 2.

### Integer operations
Two's complement with wrap-around on overflow (as element writes already wrap). Division and modulo by zero throw
`DivisionByZeroError`, as PHP does, at the operation (execution is eager). `%` follows PHP (the sign of the dividend), not Python. `<<`/`>>` by a negative
count throw `ArithmeticError` (as PHP); a count at or above the width gives 0 (`>>` of a negative signed value gives -1).
`**` with integer operands: a negative exponent is a `ValueError`; overflow wraps.

### Floats and exactness
IEEE 754, round to nearest even. `+ - * /` are exactly defined, so every backend must give bit-identical results;
there are no reductions in this operator set, so results do not depend on evaluation order. `**` with non-integer
exponents is not correctly rounded in libm or on GPUs: the spec states a bound in ULP, the loosest documented `pow`
error among the targeted platforms: **4 ULP for `Float32`, 2 ULP for `Float64`** (CUDA's documented maximum for `powf`
and `pow`, Programming Guide, mathematical functions appendix; CUDA has no half-precision `pow`); `Float16` and
`BFloat16` are computed in `Float32` within that bound and rounded once. Any method within the bound
is allowed (FMA, polynomial approximations, vendor libraries), and the conformance tests check the bound, not bits.
Contraction is ruled out only where it would change exactly-defined results: each element-wise operator is one IEEE
operation, so FMA cannot speed it up, and allowing the compiler to fuse would make results depend on the CPU (an AVX2
variant would round differently from the SSE2 one). Fused methods added later (`addScaled()`) are defined as a true
fused multiply-add, one rounding, so they are both fast and exact. Results in `Float16`/`BFloat16` are computed in `Float32` and rounded once.

### Compound assignment
Decided: `$a += $b` means `$a = $a + $b`, as for PHP's own `BCMath\Number` and `GMP` (and Julia, JAX, MATLAB; NumPy
and PyTorch write in place instead). `$b = $a; $a += 1;` leaves `$b` unchanged, and so are views of `$a`'s memory.

Performance: the result is written into `$a`'s own memory, without allocating, whenever no one can observe the
difference: the tensor object is referenced only by `$a`, its `Buffer` only by that tensor (no other views, no
`DataView`, no copy held by another extension), the buffer is writable host memory without the externally-shared
cases (DLPack export, shared file mapping), and the result dtype and shape equal `$a`'s. That covers the hot loop of a
simulation, `$pos += $vel * $dt;` every frame: no allocation for `$pos`. The temporary `$vel * $dt` is still allocated;
temporaries of the same size are recycled from a small pool, and a fused multiply-add (`$pos->addScaled($vel, $dt)`
style) is a later method, as are explicit in-place methods for memory that other views share on purpose (a framebuffer
also exposed through a `DataView`): `$a->add($b, into: $a)`.

### Comparisons
`== != < >` stay as today (a tensor equals only itself). Element-wise comparison returning a `Bool` tensor is a
separate later proposal.

## Backends (C API 3)

```c
typedef struct {
    const char *name;                                         /* "cuda", "simd" ... for errors and debugging */
    /* may this backend compute `op` for these operands? no side effects, no exceptions */
    bool (*can)(uint8_t op, const buffers_operand *a, const buffers_operand *b, int result_dtype);
    /* compute it into `result` (allocated by ext-buffers on the operands' device); FAILURE after throwing */
    zend_result (*run)(uint8_t op, const buffers_operand *a, const buffers_operand *b, zend_object *result);
} buffers_backend;

BUFFERS_API zend_result buffers_register_backend(const buffers_backend *b);   /* from MINIT; any number */
BUFFERS_API int buffers_dtype_promote(int a, int b, uint8_t op);             /* a dtype, or -1 with no common type */

```

`buffers_operand` describes a tensor (device, dtype, shape, strides, data pointer) or a scalar. ext-buffers checks the
operands, works out the result dtype and shape, then asks the backends in registration order, newest first (so a later,
more specific backend wins), and falls back to the reference implementation for host memory. `buffers_set_operator_handler()`
is removed.

## Performance

The operators here are element-wise: one pass over memory, a few instructions per element, bound by memory bandwidth on
large tensors. BLAS does not help them (BLAS is linear algebra: matrix products, dot products). They get fast from plain
C loops written so the compiler vectorizes them (contiguous fast paths, `-O3`), with per-CPU variants chosen at load time
(GCC/Clang `target_clones`: SSE2 baseline, AVX2, AVX-512; NEON on aarch64). No dependency is added; the results stay
bit-identical, because element-wise IEEE operations give the same bits vectorized or not. That holds only if the compiler neither
contracts nor reassociates: GCC's default in GNU C mode is `-ffp-contract=fast`, which may fuse `a * b + c` into one FMA
instruction (a different rounding) wherever a kernel computes more than one operation per element. So the evaluator is
built with `-ffp-contract=off`, never with `-ffast-math` or `-Ofast` (every `target_clones` variant included), and a test
compares each vectorized variant with the scalar path bit for bit on inputs where an FMA would round differently.

Matrix products (`matmul()`, `dot()`) are where BLAS matters, and they are a separate later proposal: a reduction's result
depends on summation order, so they need a stated tolerance rather than bit-exactness, and a BLAS library (OpenBLAS,
BLIS, MKL, Accelerate) fits as an optional backend through `buffers_register_backend()` (an optional build-time link, or a
separate extension) behind a portable C fallback, so ext-buffers itself keeps no dependency.

## Kept in ext-buffers (Skein depends on them)

* **Symbolic `Shape` dimensions**: `Skein\Expression::$shape` is a `Buffers\Shape` whose unknown dimensions are 0-d
  Skein expressions (`buffers_shape_create()` with object dimensions). Kept as is.
* **Readiness callbacks**: Skein calls `buffers_buffer_ensure_ready()` for device results that are still being computed.
  "A buffer still being filled" is a property of buffers in general. The reference implementation and backends call it
  before reading an operand.

## Migration of Skein

Drop `buffers_set_operator_handler()` (`skein.c:205`). `Tensor + Tensor` becomes an eager `Buffers\Tensor`; rewrite that
part of `operators.phpt` (the `Expression + Tensor` and `Tensor + Expression` parts keep working through Skein's own
handler and decision 5); update README and docs. Replace `sk_dtype_promote` with `buffers_dtype_promote()` and test
against `promotion.tsv`. Optionally (Frode's decision on the Skein side): `Skein\Tensor` as described in decision 4.
Skein ports when the change lands in an ext-buffers tag.

## Also decided separately

Tensor elements become little-endian on every machine (today: host byte order). No effect on x86-64 and aarch64;
Skein's formats (GGUF, safetensors) are little-endian. Separate change.

## Interface: `TensorAggregate` (decided)

Frode decided to adopt Skein's `TensorAggregate` in ext-buffers, replacing `Buffers\TensorInterface`: methods
`getTensor()`, `getDType()`, `getShape()`, implementable in plain PHP; `Buffers\Tensor` implements it (`getTensor()`
returns itself) and Skein's `Expression` implements it. A separate change from the arithmetic (C API affected: the
`buffers_implement_tensor_interface()` property hooks become ordinary methods). Being methods, they also need no PHP 8.3
fallback.

## Decisions taken

None left for the arithmetic itself. Decided by Frode on 2026-11-16: `Bool` arithmetic is an error; integer `%` and
division by zero follow PHP, floats IEEE; `**` gets a ULP bound with any method allowed within it; `$a += $b` rebinds
(in place when unobservable); `TensorAggregate` replaces `TensorInterface`.

## Scope, order, dependencies, risks

Work in ext-buffers, in dependency order:

1. **Semantics and promotion** (done): `buffers_dtype_promote()` in `arith.c`, `tests/promotion.tsv` generated by
   `tools/promotion-table.php` (a second implementation of the rules), the conformance test with hand-written spot
   checks. Skein can adopt the table independently of the steps below.
2. **Reference evaluator**: element-wise kernels for the arithmetic dtypes (integers, `Float16`, `BFloat16`, `Float32`,
   `Float64`) and operators, broadcasting, strided operands, the error rules, decision 5's tests.
   `buffers_set_operator_handler()` goes: C API 3. Skein's port depends on this step.
3. **Backends**: `buffers_register_backend()` with capability checks, and the conformance tests every backend must pass.
   Depends on 2.
4. **Little-endian Tensor elements**: independent of the above.

Risks:

* **Exactness across backends** rests on the conformance tests: the `**` bound and `Float16`/`BFloat16` rounding (compute
  in `Float32`, round once) must be checked bit for bit (or to the bound) for every backend that registers.
* **Kernel count**: operators x arithmetic dtypes x broadcasting/strided layouts. Mitigation: one generic strided loop per
  operator family, with contiguous fast paths only where benchmarks (on a named CPU) show they matter.
* **Hot path unchanged**: element access gains nothing from this change and is re-benchmarked to confirm it.
