# Tensor arithmetic: specification

`Buffers\Tensor` implements `+ - * / % ** << >> & | ^ ~` (and the compound assignments `+=` … and unary `-` and `+`)
element-wise and eagerly: an operation computes its result at once and returns a new `Tensor`, or throws. The result
of an operation is the same on every machine and with every backend.

## Operands

Each operand is one of:

1. a `Buffers\Tensor`;
2. a PHP `int`, `float` or `bool` (a *scalar*);
3. an object whose class has an operator handler of its own (an extension class with arithmetic, such as a lazy
   expression): that handler receives the operation, with the operands in their written order, on either side. PHP
   calls only the left operand's handler when it has one, so a `Tensor` on the left calls the right operand's handler;
4. any other object implementing `Buffers\TensorAggregate`: its `getTensor()` is called, and the result is used as in 1.

Anything else (strings, arrays, `null`, other objects) is PHP's `TypeError` ("Unsupported operand types").

Unary `-$x` is `$x * -1` and `+$x` is `$x * 1` (PHP compiles them so).

## Shapes

Shapes broadcast as in the [Python Array API standard](https://data-apis.org/array-api/latest/API_specification/broadcasting.html):
dimensions are aligned from the right, and a dimension of 1 (or a missing one) stretches to the other operand's. A scalar
is a 0-d operand. Any other mismatch is a `ValueError` naming both shapes. The result is C-contiguous with the broadcast
shape, on the operands' device; host memory counts toward `memory_limit`.

## Result dtype

`buffers_dtype_promote()` (C API) computes it; `tests/promotion.tsv` lists every operand pair and operator. The rules are
the Array API standard's [type promotion](https://data-apis.org/array-api/latest/API_specification/type_promotion.html)
with these additions:

* Two integer dtypes of the same signedness: the wider. Signed with unsigned: the narrowest signed dtype that holds both
  (`UInt8 + Int8` is `Int16`); `UInt64` with a signed dtype has no common dtype.
* Integer with float: the float dtype (`Int32 + Float32` is `Float32`).
* Two float dtypes: the wider (`Float16 + Float32` is `Float32`); `Float16` with `BFloat16` has no common dtype.
* Scalars are *weak*: an `int` or `float` takes the tensor's dtype (`$uint8 + 1` is `UInt8`, `$float16 * 0.5` is
  `Float16`); a `float` with an integer tensor gives `Float64`; a `bool` combines only with `Bool`.
* `/` always gives a float dtype: with integer operands, `Float64`.
* `Bool` takes only `& | ^` with `Bool`, and `~`. Bitwise operators and shifts take integer dtypes only.
* The storage formats (`Float8*`, `Float6*`, `Float4*`, `Int2`, `Int4`, `UInt2`, `UInt4`) and the complex dtypes have no
  arithmetic.

No common dtype, or an operator a dtype does not take, is a `TypeError` that names the dtypes.

## Integers

Two's complement; results wrap to the width of the result dtype. As in PHP: `%` takes the sign of the dividend, and `%`
by zero throws `DivisionByZeroError`; a shift by a negative count throws `ArithmeticError`; a shift by the width or more
gives 0 (`>>` of a negative value gives -1). `**` with a negative exponent throws `ValueError`. A scalar `int` that does
not fit the result's integer dtype is a `ValueError` (so `-$uint8` throws).

## Floats

IEEE 754 binary arithmetic, round to nearest even, results bit-identical on every machine:

* `+ - * /` are correctly rounded, and `%` is `fmod` (exact; the sign of the dividend). `Float16` and `BFloat16` are
  computed in `Float32` and rounded once to the result dtype, which gives the correctly rounded result.
* An integer operand is first rounded once to the float result dtype.
* `**` is within 4 ULP of the correctly rounded result for `Float32` and 2 ULP for `Float64` (the documented maxima of
  CUDA's `powf` and `pow`); `Float16` and `BFloat16` are computed in `Float32` and rounded once.
* Division by zero gives `INF` or `NAN`, as IEEE 754 defines.
* Each element is one IEEE operation: implementations do not contract `a * b + c` into a fused multiply-add, and are not
  built with `-ffast-math`.

## Compound assignment

`$a op= $b` is `$a = $a op $b`: other variables and views that refer to the old tensor keep its values, and an operation
that throws leaves `$a` unchanged. When nothing can observe the difference, the result is written into `$a`'s own
memory: `$a` holds the only reference to its tensor, the tensor the only reference to its `Buffer`, the buffer is
writable host memory that is not exported through DLPack or mapped from a file, and the result has `$a`'s dtype and
shape.

## Comparisons

`==`, `!=`, `<`, `>`, `<=>` are not element-wise: a tensor is equal only to itself.

## Backends

An extension registers a backend with `buffers_register_backend()`: a capability check and an implementation. For each
operation ext-buffers validates the operands, computes the result dtype and shape, asks the backends (newest first)
whether they can compute it for these operands, and otherwise uses its own C implementation for host memory. Operands on
a device that no backend can compute on, or on different devices, are an `Error` naming the devices. Every backend gives
the results this specification defines.
