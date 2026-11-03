# ext-buffers

The shared representation of n-dimensional typed data for PHP: `Buffers\ArrayBuffer` memory, `Buffers\BufferView`
windows onto it (n-D, strided, every machine-learning dtype including bfloat16, float8, packed float6 and float4,
and complex), `Buffers\Shape`, the `Buffers\Tensor` and `Buffers\Device` interfaces, zero-copy
[DLPack](https://github.com/dmlc/dlpack) exchange, and a versioned C API (`php_buffers_api.h`) with operator and
readiness hooks for other extensions. Written in C, no dependencies, PHP 8.4 - 8.5 (NTS and ZTS) on Linux; macOS and
Windows build files are included but untested (see [Verification](#verification)).

```php
use Buffers\{ArrayBuffer, DType};

$buf   = new ArrayBuffer(640 * 480 * 4);                 // zero-filled, 64-byte aligned, counts toward memory_limit
$rgba  = $buf->view(DType::UInt8, [480, 640, 4]);        // shape [rows, cols, channels]
$rgba[10][20][3] = 255;                                  // chained single-int offsets
echo $rgba[10][20][3];                                   // 255
$row = $rgba[10];                                        // a sub-view of the same memory
$row[20][0] += 7;                                        // writes through to $buf
echo $rgba->shape[-1], ' ', $rgba->ndim, ' ', json_encode($rgba->strides);   // 4 3 [2560,4,1]

$w = $buf->view(DType::Float8E4M3FN, 16);                // 16 one-byte floats
$w[0] = 1.5;
$torch_ready = $rgba->toDLPack();                        // zero-copy export, see "DLPack"
```

## Install

```sh
pie install phasync/ext-buffers          # needs the package to be published; or:
phpize && ./configure --enable-buffers && make && make install     # then extension=buffers
```

`make install` also installs `php_buffers_api.h` and `dlpack.h` under `ext/buffers/` for extensions that build on
this one (see [C API](#c-api)). Add `--enable-buffers-testing` to build the test-only helpers the DLPack tests use
(`buffers_test_make()`, `buffers_test_counts()`); never needed in production.

## Performance

**For hot loops, use a one-dimensional view and compute the index yourself.** A 1-D contiguous view has the
shortest handler in the extension: one bounds check and one load, no sub-view, no stride, no dtype switch. A
chained `$v[$y][$x][$c]` creates a (pooled) sub-view per level and costs about five times as much.

```php
$f = $buffer->view(DType::UInt8);                        // 1-D over the whole buffer
$alpha = $f[$y * $width * 4 + $x * 4 + 3];               // read
$f[$y * $width * 4 + $x * 4 + 3] = 255;                  // write
// hoisting the row base out of the inner loop is a little faster still:
$base = $y * $width * 4 + 3;  for ($x = 0; $x < $width; $x++) { $a = $f[$base + $x * 4]; }
```

Measured on PHP 8.5.12 NTS, x86-64 (Linux), nanoseconds per element access, best of 5-12 runs over a 640x480 RGBA
image. Interpreter = `opcache.jit=off`; JIT = `opcache.jit=tracing`, and a JIT column is only reported when
opcache's JIT buffer shows that code was actually compiled (`opcache.file_update_protection=0` is required: by
default opcache silently refuses to compile files younger than two seconds). Reproduce with `benchmarks/run.sh`.

| access (alpha byte of each pixel)       | interpreter | JIT  | ext-dlpack JIT (same machine) |
|-----------------------------------------|------------:|-----:|------:|
| **read, view 1-D idiom** | 23.6 | 7.9 | 9.4 |
| **read, view 1-D, row base hoisted** | 17.8 | 7.2 | 7.2 |
| read, view `[$y][$x][$c]` (chained) | 67.6 | 48.3 | 49.1 |
| read, view `[$y]` hoisted, `[$x][$c]` | 41.3 | 27.6 | 27.7 |
| read, view 2-D `[$y][$x*4+$c]` | 44.0 | 29.0 | 27.8 |
| read, PHP array, flat | 20.4 | 3.3 | 3.2 |
| read, PHP array, nested | 24.9 | 6.1 | 6.1 |
| read, SplFixedArray, flat | 25.5 | 10.2 | 10.2 |
| **write, view 1-D idiom** | 22.0 | 8.3 | 8.3 |
| **write, view 1-D, row base hoisted** | 16.9 | 8.0 | 8.2 |
| write, view chained | 66.7 | 56.3 | 54.3 |
| write, view `[$y]` hoisted | 39.6 | 29.8 | 29.4 |
| write, PHP array, flat | 22.7 | 5.3 | 5.2 |
| write, PHP array, nested | 43.3 | 29.6 | 32.2 |
| write, SplFixedArray, flat | 25.3 | 11.3 | 11.2 |
| `+=`, view 1-D idiom | 28.8 | 15.7 | 15.8 |
| `+=`, view chained | 71.4 | 62.0 | 61.4 |

Best of three `benchmarks/run.sh` runs of each extension, back to back on the same machine (PHP 8.5.12, JIT buffer verified:
"code was compiled"). The last column is the predecessor, ext-dlpack, built the same way: the 1-D and chained paths did not
regress (differences are within the run-to-run noise of about 5%; the 7.9 ns against 9.4 ns for the 1-D read idiom is the
only visible gap and in the good direction, not something the code changed on purpose).

Other numbers (JIT): a 1-D read costs 7-8 ns and a write 7-11 ns for every integer, float and small-float dtype, including the
packed `Float6*`/`Float4*` formats (read 7.8, write 11.1: a shift and mask, and a read-modify-write); `Complex*` reads cost about 41-47 ns
because they allocate an `[re, im]` array. Reading a property of a view costs 39 ns (`ndim`, `offset`) to 102 ns (`strides`, which builds an
array; `shape` 92 ns builds a `Shape`); making a sub-view with `$v[$i & 15]` costs 23.5 ns in a loop. Memory per element (1M elements):
`UInt8` 1.01 B, `Float32` 4.00 B, `Float64` 8.00 B, against 16.8 B for a PHP array of ints or floats and 16 B for `SplFixedArray`.


What this means, plainly: under the JIT a typed view is about as fast as `SplFixedArray` and roughly 2x slower than
a plain packed PHP array for flat access (the JIT inlines array access; an object access goes through a handler
call), and chained access is 5-8x slower than nested arrays. What you get in exchange: 1 byte per `UInt8` element
instead of 16.8 bytes, real fixed-width types, many dtypes, aliasing views, and DLPack interop.

The properties of a view (`dtype`, `shape`, `strides`, ...) are computed when read, so reading one costs a function
call and allocates only the value returned (a `Shape` object for `shape`, an array for `strides`); they are for
setup code, not for inner loops. Making a sub-view stores nothing for them.

`$view[$x][$y][$z]++` does **not** work: PHP rejects it with "Indirect modification of overloaded element". Use
`$view[$x][$y][$z] += 1`.

## API

Everything lives in the namespace `Buffers\`. `buffers.stub.php` has the full signatures with docblocks.

```php
namespace Buffers;

enum DType: string { case Bool, Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64,
    Float16, Float32, Float64, BFloat16, Float8E3M4, Float8E4M3, Float8E4M3B11FNUZ, Float8E4M3FN, Float8E4M3FNUZ,
    Float8E5M2, Float8E5M2FNUZ, Float8E8M0FNU, Float6E2M3FN, Float6E3M2FN, Float4E2M1FN,
    Complex32, Complex64, Complex128, BComplex32;
    public function bits(): int;             // width of one element: 4, 6, 8 ... 128
    public function byteSize(): ?int;        // bytes per element; null for the packed formats Float6*, Float4*
    public function isByteAligned(): bool;   // false for the packed formats
    public function isInteger(): bool;       // Int*, UInt*
    public function isFloatingPoint(): bool; // all Float*, BFloat16 and the small formats
    public function isComplex(): bool;
    public function dlpack(): array;         // ['code' => int, 'bits' => int, 'lanes' => 1]
}

interface Tensor {                           // only internal classes can implement it (see "Tensor")
    public ?DType $dtype { get; }
    public Shape $shape { get; }
    public int $ndim { get; }
}

final class Shape implements ArrayAccess, Countable, IteratorAggregate {
    public function __construct(array $dims);        // list of ints >= 0 and 0-d Tensors
    public function isConcrete(): bool;              // every dimension is an int
    public function size(): int|Tensor;              // product; Error if not concrete (see "Shape")
    public function toArray(): array;
}

interface Device { name(): string; dlpackType(): int; id(): int; isHostAccessible(): bool; }
final class CpuDevice implements Device { public static function get(): CpuDevice; }

interface BufferInterface {
    public function byteLength(): int;
    public function isWritable(): bool;
    public function device(): Device;
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): BufferView;
}
final class ArrayBuffer implements BufferInterface {
    public function __construct(int $byteLength);              // zero-filled; emalloc'd, so it counts toward memory_limit
    public static function fromString(string $data): ArrayBuffer;
    public function toString(): string;                        // a copy of the bytes
}

final class BufferView implements Tensor, Countable, ArrayAccess, IteratorAggregate {
    // read-only properties, computed when read:
    //   DType $dtype, Shape $shape, int $ndim, list<int> $strides (in elements), int $offset (bytes), BufferInterface $buffer
    public function toDLPack(): DLPackCapsule;
    public static function fromDLPack(DLPackCapsule $capsule): BufferView;
    // count() = length of the first axis; foreach iterates the first axis; $v[i], isset($v[i]), $v[i] = x
}

final class DLPackCapsule {}    // opaque, consumed once
final class DLPackBuffer implements BufferInterface {}   // memory owned by someone else (imported tensors, other extensions)
final class ForeignDevice implements Device {}           // the device of an imported tensor that is not on the CPU
```

### `view()`

* `shape`: `null` = 1-D covering the rest of the buffer (the remaining size must be a whole number of elements);
  an `int` = 1-D of that length; an `array` of 0-8 non-negative ints = n-D. **`[]` makes a 0-d view of one
  element** (see below).
* `strides`: in elements, one per dimension, each at least 1 (negative and zero strides are rejected);
  `null` = C-contiguous. Overlapping strides are allowed.
* `offset`: bytes, a multiple of the element size (any byte offset for the packed formats).
* Everything is validated once, with overflow-safe arithmetic, so element access needs only a bounds check. A view
  keeps its buffer alive. Empty views (a dimension of 0) are valid.
* On 32-bit PHP, `Int64`, `UInt64` and `UInt32` are rejected with a `ValueError` (their values do not fit a PHP
  int); the same for DLPack import. Everything else works.

### Properties

`$v->dtype`, `$v->shape` (a `Shape`), `$v->ndim`, `$v->strides` (a list, in elements), `$v->offset` (bytes from the start of the
buffer to element 0) and `$v->buffer` are read-only: writing or unsetting one throws `Error`. They are *virtual
properties with internal get hooks*, exactly what a PHP 8.4 class would declare as `public int $ndim { get => ... }`:
nothing is stored in the object, so creating a sub-view (`$v[$y]`, served from a pool) allocates no `Shape`, no array
and nothing else. `var_dump()` shows no properties; `get_object_vars()`, `json_encode()` and reflection do.

### Element access

`$v[$x][$y][$z]` takes one integer offset per level. A partial chain is a sub-view sharing the same memory; the leaf
is a scalar.

| dtype                | read gives                                | write accepts                                           |
|----------------------|-------------------------------------------|---------------------------------------------------------|
| `Int*`, `UInt*`      | `int` (`UInt64` as the signed 64-bit pattern) | `int` (wraps by C truncation) or `float` (PHP's `(int)`: NaN/INF become 0; no clamping) |
| `Bool`               | `bool`                                    | `bool` only                                             |
| float dtypes         | `float`                                   | `int` or `float`, rounded to nearest even               |
| `Complex*`           | `[re, im]`                                | a number, or `[re, im]`                                 |

### Error policy

Any non-int offset throws `TypeError`; an out-of-range or negative index throws `ValueError` (`isset()` and `??` just
say no/null); `$v[] = x`, `unset($v[i])`, writing a sub-view, writing to a read-only view, and a value of the wrong
type (`TypeError`) all throw. Views, shapes, devices, buffers and capsules cannot be cloned, serialized,
instantiated by the user or given dynamic properties. Bad `view()` arguments throw `ValueError` naming the
argument and the reason. Elements of memory that is not host accessible (see Devices) throw `Error`.

### 0-d views

`$buf->view(DType::Float32, [])` is a view of exactly one element: `ndim` 0, `shape` `[]`, `strides` `[]`. It has no axes, so
`count()`, `foreach`, `$v[0]`, `isset($v[0])` and friends throw `Error("A 0-d view has no axes")`. It is a
`Tensor`, shares memory with other views of the buffer, and exports and imports through DLPack. (Reading the single
element is for a later version.)

### Shape

`new Shape([2, 3])`; `$shape[-1]` counts from the end; an out-of-range index throws `ValueError`; `offsetSet`/`offsetUnset`
throw `Error` (a Shape is immutable); `==` compares by value. A dimension is an int or a **0-d `Tensor`** (a symbolic
dimension, for extensions that build deferred computations). `size()` returns the product when the shape is
concrete (`ValueError` above `PHP_INT_MAX`); for a non-concrete shape it throws `Error` in this version, since no
extension that could compute a symbolic product exists yet. The shape of a `BufferView` is always concrete.

### Tensor

`Tensor` is the common face of anything with a dtype and a shape. `BufferView` implements it, and so can classes of
other extensions (a deferred tensor, say) through `buffers_implement_tensor()`. It **cannot be implemented in PHP code**: a
user class declaring `implements Tensor` is a fatal error ("only internal classes can implement it"), which keeps the
set of tensors under the control of extensions that can honour the C API.

### Devices

`BufferInterface::device()` says where the memory lives. `ArrayBuffer` is always on `CpuDevice::get()` (dlpackType 1,
id 0, host accessible). DLPack import accepts **any** device: a tensor that is not on the CPU becomes a view over an
opaque `DLPackBuffer` whose device is a `ForeignDevice` reporting the DLPack device type and id; its
`isHostAccessible()` is false except for `kDLCUDAHost` (3), `kDLROCMHost` (11) and `kDLCUDAManaged` (13). Reading,
writing or iterating the elements of memory that is not host accessible throws `Error`; making sub-views, reading the
properties and exporting work. Exporting such a view again passes the device type, id, data pointer and byte offset
through unchanged (the pointer is an opaque handle and is never dereferenced), and the original tensor's deleter runs
exactly once, when the last view of it is gone.

## Dtypes

Follows [ml_dtypes](https://github.com/jax-ml/ml_dtypes) 0.5.4 for every format, byte for byte (see
[Verification](#verification)). Elements are stored in the **host's native byte order**, like NumPy's default, PyTorch,
DLPack and JavaScript typed arrays (there is no byte-order option); no code assumes little-endian.

| `DType`              | value                | bits | bytes | DLPack code/bits | notes |
|----------------------|----------------------|-----:|------:|------------------|-------|
| `Bool`               | `bool`               | 8    | 1     | 6 / 8            | |
| `Int8` .. `Int64`    | `int8` .. `int64`    | 8-64 | 1-8   | 0 / bits         | |
| `UInt8` .. `UInt64`  | `uint8` .. `uint64`  | 8-64 | 1-8   | 1 / bits         | |
| `Float16`            | `float16`            | 16   | 2     | 2 / 16           | IEEE binary16; the NaN payload is carried over, like numpy |
| `BFloat16`           | `bfloat16`           | 16   | 2     | 4 / 16           | |
| `Float32`, `Float64` | `float32`, `float64` | 32, 64 | 4, 8 | 2 / bits        | |
| `Complex32`          | `complex32`          | 32   | 4     | 5 / 32           | 2 x float16 |
| `BComplex32`         | `bcomplex32`         | 32   | 4     | 18 / 32          | 2 x bfloat16 |
| `Complex64`, `Complex128` | `complex64`, `complex128` | 64, 128 | 8, 16 | 5 / bits | 2 x float32, 2 x float64 |
| `Float8E3M4`         | `float8_e3m4`        | 8    | 1     | 7 / 8            | IEEE-like: inf, NaN |
| `Float8E4M3`         | `float8_e4m3`        | 8    | 1     | 8 / 8            | IEEE-like |
| `Float8E4M3B11FNUZ`  | `float8_e4m3b11fnuz` | 8    | 1     | 9 / 8            | bias 11, no inf, no -0, NaN = 0x80 |
| `Float8E4M3FN`       | `float8_e4m3fn`      | 8    | 1     | 10 / 8           | no inf; overflow and INF become NaN; max 448 |
| `Float8E4M3FNUZ`     | `float8_e4m3fnuz`    | 8    | 1     | 11 / 8           | bias 8, no inf, no -0, NaN = 0x80 |
| `Float8E5M2`         | `float8_e5m2`        | 8    | 1     | 12 / 8           | IEEE-like |
| `Float8E5M2FNUZ`     | `float8_e5m2fnuz`    | 8    | 1     | 13 / 8           | bias 16, no inf, no -0, NaN = 0x80 |
| `Float8E8M0FNU`      | `float8_e8m0fnu`     | 8    | 1     | 14 / 8           | exponent only, 2^(code-127), 0xFF = NaN; rounds half up; zero, negatives, INF and NaN encode as NaN |
| `Float6E2M3FN`       | `float6_e2m3fn`      | 6    | null  | 15 / 6           | **packed**; overflow and INF saturate to the largest finite value |
| `Float6E3M2FN`       | `float6_e3m2fn`      | 6    | null  | 16 / 6           | **packed**; saturating |
| `Float4E2M1FN`       | `float4_e2m1fn`      | 4    | null  | 17 / 4           | **packed**; saturating |

Conversions from PHP's double go through float32 first for every format except `Float16` (direct from double),
exactly as ml_dtypes does, and round to nearest even. Writing NaN into the saturating float6/float4 formats stores
ml_dtypes' quirk: a NaN with the sign bit clear becomes negative zero and one with it set becomes positive zero. The
small formats (at most 8 bits) decode through a 256-entry lookup table built at startup from the same format
descriptors the encoder uses; the encoders are integer bit manipulation specialised per format (no floating point
rounding mode is involved).

### Packed sub-byte storage

`Float6E2M3FN`, `Float6E3M2FN` and `Float4E2M1FN` have **one layout only: packed**, as DLPack's default defines it:
element *i* of a row occupies the bits `[i*bits, (i+1)*bits)` of the row's bytes, little-bit-endian (bit 0 of byte 0
is the lowest bit of element 0), independent of the machine's byte order. `byteSize()` is `null` and
`isByteAligned()` is false. (NumPy and ml_dtypes store these formats padded, one element per byte; a DLPack tensor
marked `IS_SUBBYTE_TYPE_PADDED` is rejected on import with a clear message, and exports never set the flag.)

`view()` rules for them, so that every sub-view starts on a byte boundary:

* the last axis has stride 1 (contiguous);
* every other stride times `bits` is a multiple of 8 (fp4: even strides; fp6: multiples of 4) -
  `view(DType::Float6E2M3FN, [2, 3])` is therefore an error (a row of 3 elements is 18 bits): pass explicit strides
  such as `[4, 1]`, or a shape like `[2, 4]`;
* `offset` is in bytes; `shape = null` needs `byteLength * 8` to divide evenly by `bits`.

`$v->strides` reports elements (the last is 1) and `$v->offset` bytes. fp6 elements may straddle a byte boundary:
reads assemble a 16-bit window byte by byte (`(b0 | b1 << 8) >> shift`, never a native 16-bit load) and writes
read-modify-write, so neighbours are untouched and no byte outside the view's extent is ever read or written.

## DLPack

DLPack v1.3 (`dlpack.h` is vendored unmodified, Apache-2.0, see `NOTICE`).

* `toDLPack()` produces a `DLManagedTensorVersioned` on the buffer's device (`kDLCPU` for an `ArrayBuffer`).
  **Strides are always materialised** (NULL strides are illegal since DLPack 1.2), `byte_offset` is the view's
  offset, `DLPACK_FLAG_READ_ONLY` mirrors `isWritable()`. Packed float6/float4 views are exported packed (no
  `IS_SUBBYTE_TYPE_PADDED`), 0-d views with `ndim` 0. The capsule keeps the buffer alive; the tensor's deleter
  releases it. The capsule is opaque, a PHP object holding the C pointer. Export waits for a pending buffer (see
  [Readiness](#readiness)).
* `fromDLPack()` takes a capsule (consuming it once) and wraps the foreign memory in a `DLPackBuffer` whose view is
  the returned `BufferView`; the tensor's deleter runs exactly once, when that buffer is freed. For host-accessible
  tensors the buffer covers exactly the tensor's extent, so `$v->offset` starts at 0; for other devices the opaque
  data pointer and byte offset are kept as they are. Any device, one lane, 0-8 dimensions, strides of at least 1 and
  (for host memory) data aligned to the element size are imported; **padded** float4/float6 are rejected; versions
  below 1.2 may pass NULL strides (compact row-major). Anything else throws `ValueError` with the reason, and a
  rejected capsule is not consumed (its deleter still runs once when freed).
* The C structs are the standard ones; nothing in PHP can see the raw pointer, so exchange with another extension
  or FFI happens through those C structs (and `buffers_dlpack_export()` / `buffers_dlpack_import()`), not through a
  PHP-visible protocol.

## C API

`php_buffers_api.h` (installed to `ext/buffers/`, `BUFFERS_API_VERSION` 1, `buffers_api_version()` at run time) is the
only thing another extension needs; it never sees the layout of buffers or views. The symbols are exported from
`buffers.so` (also in the release binaries, which hide everything else), so load order matters:
declare `ZEND_MOD_REQUIRED("buffers")`. `tests/api-consumer/` is a complete small extension that uses nothing else
and has phpt tests; read it as the example.

| Area | Functions |
|------|-----------|
| classes, dtypes | `buffers_get_class()`, `buffers_dtype_describe()`, `buffers_dtype_to_zval()`, `buffers_dtype_from_object()`, `buffers_cpu_device()` |
| views | `buffers_is_view()`, `buffers_view_get_info()` (data pointer, dtype, ndim, shape, strides in elements, byte offset, buffer object, writable), `buffers_view_create()` (validated exactly like `view()`) |
| buffers | `buffers_is_buffer()`, `buffers_buffer_get_info()` (base, length, writable, host accessible, device), `buffers_buffer_create_foreign()` (memory + deleter + device), `buffers_buffer_create_owned()` (memory kept alive by a PHP object: holds a reference to the owner and releases it on free, no deleter), `buffers_hold()`, `buffers_release()` |
| shapes, tensors | `buffers_shape_create()`, `buffers_shape_dims()`, `buffers_implement_tensor()` (declare the virtual properties of `Tensor` with three get hooks) |
| DLPack | `buffers_dlpack_export()`, `buffers_dlpack_import()` |
| hooks | `buffers_set_operator_handler()`, `buffers_buffer_set_ready_callback()`, `buffers_buffer_set_pending()`, `buffers_buffer_ensure_ready()` |

**Memory owned by a PHP object** (for example RubixML's Tensor-Ext keeps contiguous row-major float64 data in C): create
a buffer with `buffers_buffer_create_owned(data, length, writable, NULL, owner_object)` and a view with
`buffers_view_create(buffer, BUFFERS_DT_FLOAT64, 2, shape, NULL, 0)`; the owner stays alive as long as any view or
buffer does and is released afterwards (`tests/api-consumer/tests/owner.phpt`). Strides are fully supported by views
(pass them to `buffers_view_create()`); only the `Tensor` interface omits them.

### Operator hook

`buffers_set_operator_handler(fn)` registers one handler (from `MINIT`; one per process). `BufferView` has a
`do_operation` handler that passes `+ - * / % ** << >> | & ^ ~` (also as `+=` etc., unary minus is `* -1`) to it when either operand
is a view; the handler returns `SUCCESS` with the result, or `FAILURE` to leave PHP's default `TypeError`
("Unsupported operand types"). With no handler registered behaviour is exactly that `TypeError`. Comparisons never
go to the handler: a view is only equal to itself.

### Readiness

A buffer produced asynchronously or lazily can carry a **readiness callback** and a pending flag
(`buffers_buffer_set_ready_callback()` while the caller holds the only reference, then
`buffers_buffer_set_pending()`). Element reads and writes, iteration, `ArrayBuffer::toString()` and DLPack export
run the callback first while the flag is set; it makes the contents valid and returns `SUCCESS`, or throws and
returns `FAILURE` (the buffer stays pending, the exception reaches the PHP caller). Making sub-views and reading
properties never wait. Buffers without a callback and host-accessible memory use the unchanged fast handlers: the
check is selected when a view is created, not on each access, so the 1-D and chained fast paths cost the same as
without the feature (see Performance).

## Verification

What has been run, on Linux x86-64 (Ubuntu host, Debian 12 and Alpine containers):

* `tests/` phpt suite (32 files; 24 run and 8 skip with the extension alone, 30 run and 2 skip with the API consumer loaded - the skips
  are tests that need the consumer, 32-bit-only or 64-bit-only tests) on PHP 8.4 and 8.5, Debian/glibc and Alpine/musl via Docker
  (`tools/docker-test.sh`), NTS and ZTS images (8.4 and 8.5, Debian and Alpine), plus debug builds of php-src 8.5.13-dev NTS and ZTS
  (assertions on, which also checks the internal hook functions and the API consumer's arginfo); all pass, no compiler warnings with
  `-Wall -Wextra`. 32-bit PHP (`linux/386`: 8.4 Debian, 8.5 Debian, 8.4 Alpine): all pass, with the 64-bit-only tests skipped and
  `tests/bits32.phpt` checking the 32-bit rule. aarch64 (little-endian, under QEMU, 8.4 Debian): all pass.
* The whole suite, both passes, under valgrind with `USE_ZEND_ALLOC=0` (`tools/valgrind.sh`, PHP 8.4 NTS): zero errors, zero leaks, including
  parked pool objects at shutdown, foreign-tensor deleters, owner references, the readiness contexts and exactly-sized buffers read
  at their last packed element.
* `tools/oracle/run.sh decode|encode|doubles`: against ml_dtypes 0.5.4 - every code of every format of at most 16 bits decodes
  identically; every one of the 2^32 float32 inputs encodes identically for `float8_e4m3fn`, `float8_e5m2` and `float4_e2m1fn` (the
  other formats were swept for the predecessor and their code is unchanged); 20 million random doubles, exact midpoints and their
  neighbours per format encode identically.
* `tools/oracle/run-fuzz.sh`: a NumPy differential fuzz (random dtype, bytes, shape, strides, offset; reads through chained offsets and
  writes compared with NumPy `as_strided` over the same bytes; the packed formats checked with NumPy bit operations - bit positions,
  extraction, read-modify-write - against ml_dtypes' decode and encode; 0-d views checked for geometry and a DLPack round trip):
  30000 views, 1.09 million elements, 14348 write passes, no mismatch. `SCRIPT=view_fuzz.py tools/oracle/run-fuzz.sh`: hostile `view()`
  arguments (including `[]`, and the packed rules) against an exact big-integer model: 100000 calls, 12155 accepted, 87845 rejected, no mismatch.
* Not run: macOS, Windows (`config.w32` and the workflows are written but never executed), the GitHub workflows themselves, real
  multi-threaded ZTS, big-endian machines (s390x needs QEMU binfmt, which is not installed here; no code assumes little-endian and
  the tests that print element bytes skip on big-endian, but that is untested).

Commands: `tools/test.sh [8.x]` builds out of tree and runs the suite (twice: with the extension alone, then with the API
consumer loaded); `tools/valgrind.sh [8.x]`; `tools/docker-test.sh [image ...]` (`DOCKER_PLATFORM=linux/386` for 32-bit);
`benchmarks/run.sh` runs the benchmarks; `tools/gen-arginfo.sh` regenerates `buffers_arginfo.h` from `buffers.stub.php`.
`PHP_BIN=/path/to/bin` selects another PHP installation for the local scripts (debug and ZTS builds).

## Not in this version

`get()`/`set()` methods, array/tuple offsets, reading the element of a 0-d view, `fill/copy/cast/reshape/transpose/slice/toArray/fromArray`,
clamped writes, padded sub-byte layouts, negative or zero strides, arithmetic by the extension itself (the operator hook
is for other extensions), a byte-order option.

## Licence

MIT (`LICENSE`); `dlpack.h` is Apache-2.0 (`LICENSE.dlpack`, `NOTICE`).
