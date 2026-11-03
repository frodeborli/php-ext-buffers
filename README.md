# ext-dlpack

Typed, n-dimensional views over raw byte buffers for PHP, with every machine-learning dtype
(bfloat16, float8 in all its variants, float6, float4, complex) and zero-copy
[DLPack](https://github.com/dmlc/dlpack) exchange. Written in C, no dependencies, PHP 8.2 - 8.5
(NTS and ZTS) on Linux; macOS and Windows build files are included but untested (see
[Verification](#verification)).

```php
$buf   = new ArrayBuffer(640 * 480 * 4);                 // zero-filled, 64-byte aligned, counts toward memory_limit
$rgba  = $buf->view(DType::UInt8, [480, 640, 4]);        // shape [rows, cols, channels]
$rgba[10][20][3] = 255;                                  // chained single-int offsets
echo $rgba[10][20][3];                                   // 255
$row = $rgba[10];                                        // a sub-view of the same memory
$row[20][0] += 7;                                        // writes through to $buf

$w = $buf->view(DType::Float8E4M3FN, 16);                // 16 one-byte floats
$w[0] = 1.5;
$torch_ready = $rgba->toDLPack();                        // zero-copy export, see "DLPack"
```

## Install

```sh
pie install phasync/ext-dlpack          # needs the package to be published; or:
phpize && ./configure --enable-dlpack && make && make install     # then extension=dlpack
```

Add `--enable-dlpack-testing` to build the test-only helpers the DLPack tests use
(`dlpack_test_make()`, `dlpack_test_counts()`); never needed in production.

## Performance

**For hot loops, use a one-dimensional view and compute the index yourself.** A 1-D contiguous view
has the shortest handler in the extension: one bounds check and one load, no sub-view, no stride,
no dtype switch. A chained `$v[$y][$x][$c]` creates a (pooled) sub-view per level and costs about
five times as much.

```php
$f = $buffer->view(DType::UInt8);                        // 1-D over the whole buffer
$alpha = $f[$y * $width * 4 + $x * 4 + 3];               // read
$f[$y * $width * 4 + $x * 4 + 3] = 255;                  // write
// hoisting the row base out of the inner loop is a little faster still:
$base = $y * $width * 4 + 3;  for ($x = 0; $x < $width; $x++) { $a = $f[$base + $x * 4]; }
```

Measured on PHP 8.5.12 NTS, x86-64 (Linux), nanoseconds per element access, best of 5-12 runs over
a 640x480 RGBA image. Interpreter = `opcache.jit=off`; JIT = `opcache.jit=tracing`, and a JIT column
is only reported when opcache's JIT buffer shows that code was actually compiled
(`opcache.file_update_protection=0` is required: by default opcache silently refuses to compile files
younger than two seconds). Reproduce with `benchmarks/run.sh`.

| access (alpha byte of each pixel)       | interpreter | JIT  |
|-----------------------------------------|------------:|-----:|
| **read, view 1-D idiom**                | 23.7        | 9.4  |
| **read, view 1-D, row base hoisted**    | 17.8        | 7.9  |
| read, view `[$y][$x][$c]` (chained)     | 66.8        | 49.1 |
| read, view `[$y]` hoisted, `[$x][$c]`   | 41.2        | 28.0 |
| read, view 2-D `[$y][$x*4+$c]`          | 43.4        | 28.0 |
| read, PHP array, flat                   | 21.1        | 4.1  |
| read, PHP array, nested                 | 25.2        | 6.1  |
| read, SplFixedArray, flat               | 25.8        | 10.7 |
| **write, view 1-D idiom**               | 22.2        | 8.3  |
| **write, view 1-D, row base hoisted**   | 17.0        | 8.2  |
| write, view chained                     | 67.8        | 55.4 |
| write, view `[$y]` hoisted              | 39.6        | 29.7 |
| write, PHP array, flat                  | 23.0        | 5.3  |
| write, PHP array, nested                | 42.1        | 27.8 |
| write, SplFixedArray, flat              | 25.8        | 11.2 |
| `+=`, view 1-D idiom                    | 28.4        | 15.8 |
| `+=`, view chained                      | 71.5        | 61.5 |
| `+=`, PHP array flat / nested           | 22.0 / 39.6 | 5.1 / 25.9 |

What this means, plainly: under the JIT a typed view is about as fast as `SplFixedArray` and roughly
2x slower than a plain packed PHP array for flat access (the JIT inlines array access; an object
access goes through a handler call), and chained access is 5-8x slower than nested arrays. What you
get in exchange: 1 byte per `UInt8` element instead of 16.8 bytes, real fixed-width types, many
dtypes, aliasing views, and DLPack interop. With a bare `$s += $f[$i]` loop the 1-D read costs
6.5 ns (1.0 ns of it loop overhead); the table's 9.4 ns includes the index arithmetic. PHP 8.2-8.4
give the same picture within about 10% (`benchmarks/run.sh` per version).

Per dtype, a 1-D read costs 7-10 ns and a write 7-11 ns under the JIT for every integer, float and
small-float dtype (the small floats decode through a table and encode with integer bit
manipulation); `Complex*` reads cost about 41-47 ns and writes 20-23 ns because they allocate an
`[re, im]` array. Memory per element (1M elements): `UInt8` 1.01 B, `Float32` 4.00 B, `Float64` 8.00 B,
against 16.8 B for a PHP array of ints or floats and 16 B for `SplFixedArray`.

`$view[$x][$y][$z]++` does **not** work: PHP rejects it with "Indirect modification of overloaded
element". Use `$view[$x][$y][$z] += 1`.

## API

Global, un-namespaced names.

```php
enum DType: string { case Bool, Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64,
    Float16, BFloat16, Float32, Float64, Complex32, BComplex32, Complex64, Complex128,
    Float8E3M4, Float8E4M3, Float8E4M3B11FNUZ, Float8E4M3FN, Float8E4M3FNUZ, Float8E5M2,
    Float8E5M2FNUZ, Float8E8M0FNU, Float6E2M3FN, Float6E3M2FN, Float4E2M1FN;
    public function bits(): int;             // storage width of one element: 4, 6, 8 ... 128
    public function byteSize(): ?int;        // bytes one element occupies in a buffer (1 for float4/float6)
    public function isByteAligned(): bool;   // always true: sub-byte formats are stored padded
    public function isInteger(): bool;       // Int*, UInt*
    public function isFloatingPoint(): bool; // all Float*, BFloat16 and the small formats
    public function isComplex(): bool;
    public function dlpack(): array;         // ['code' => int, 'bits' => int, 'lanes' => 1]
}

interface BufferInterface {
    public function byteLength(): int;
    public function isWritable(): bool;
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): TypedView;
}

final class ArrayBuffer implements BufferInterface {
    public function __construct(int $byteLength);              // zero-filled; emalloc'd, so it counts toward memory_limit
    public static function fromString(string $data): ArrayBuffer;
    public function toString(): string;                        // a copy of the bytes
    // byteLength(), isWritable() (true), view()
}

final class TypedView implements Countable, ArrayAccess, IteratorAggregate {
    public function dtype(): DType;
    public function shape(): array;          // [int, ...]
    public function strides(): array;        // in ELEMENTS
    public function byteOffset(): int;       // of element 0, from the start of the buffer
    public function buffer(): BufferInterface;
    public function ndim(): int;             // a method, not a property
    public function toDLPack(): DLPackCapsule;
    public static function fromDLPack(DLPackCapsule $capsule): TypedView;
    // count() = length of the first axis; foreach iterates the first axis; $v[i], isset($v[i]), $v[i] = x
}

final class DLPackCapsule {}    // opaque, consumed once
final class DLPackBuffer implements BufferInterface {}   // what fromDLPack() hands back through ->buffer()
```

### `view()`

* `shape`: `null` = 1-D covering the rest of the buffer (the remaining byte count must be a multiple of
  the element size); an `int` = 1-D of that length; an `array` of 1-8 non-negative ints = n-D.
* `strides`: in elements, one per dimension, each at least 1 (negative and zero strides are rejected in v1);
  `null` = C-contiguous. Overlapping strides are allowed.
* `offset`: bytes, a multiple of the element size.
* Everything is validated once, with overflow-safe arithmetic, so element access needs only a bounds check.
  A view keeps its buffer alive. Empty views (a dimension of 0) are valid.

### Element access

`$v[$x][$y][$z]` takes one integer offset per level. A partial chain is a sub-view sharing the same
memory; the leaf is a scalar.

| dtype                | read gives                                | write accepts                                           |
|----------------------|-------------------------------------------|---------------------------------------------------------|
| `Int*`, `UInt*`      | `int` (`UInt64` as the signed 64-bit pattern) | `int` (wraps by C truncation) or `float` (PHP's `(int)`: NaN/INF become 0; no clamping) |
| `Bool`               | `bool`                                    | `bool` only                                             |
| float dtypes         | `float`                                   | `int` or `float`, rounded to nearest even               |
| `Complex*`           | `[re, im]`                                | a number, or `[re, im]`                                 |

Errors: any non-int offset throws `TypeError`; an out-of-range or negative index throws `ValueError`
(`isset()` and `??` just say no/null); `$v[] = x`, `unset($v[i])`, writing a sub-view, writing to a
read-only view, and a value of the wrong type (`TypeError`) all throw. Views cannot be cloned,
serialized, instantiated or given dynamic properties.

## Dtypes

Follows [ml_dtypes](https://github.com/jax-ml/ml_dtypes) 0.5.4 for every format, byte for byte (see
[Verification](#verification)).

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
| `Float6E2M3FN`       | `float6_e2m3fn`      | 6    | 1     | 15 / 6           | padded; overflow and INF saturate to the largest finite value |
| `Float6E3M2FN`       | `float6_e3m2fn`      | 6    | 1     | 16 / 6           | padded; saturating |
| `Float4E2M1FN`       | `float4_e2m1fn`      | 4    | 1     | 17 / 4           | padded; saturating |

Conversions from PHP's double go through float32 first for every format except `Float16` (direct from
double), exactly as ml_dtypes does, and round to nearest even. Writing NaN into the saturating
float6/float4 formats stores ml_dtypes' quirk: a NaN with the sign bit clear becomes negative zero and
one with it set becomes positive zero. The small formats (at most 8 bits) decode through a 256-entry
lookup table built at startup from the same format descriptors the encoder uses; the encoders are
integer bit manipulation specialised per format (no floating point rounding mode is involved).

Sub-byte formats (`Float6*`, `Float4*`) are stored **padded**, one element per byte with the value in
the low bits, as NumPy and ml_dtypes do; bits above the format width are ignored on read.

## DLPack

DLPack v1.3 (`dlpack.h` is vendored unmodified, Apache-2.0, see `NOTICE`).

* `toDLPack()` produces a `DLManagedTensorVersioned` on device `kDLCPU`. **Strides are always
  materialised** (NULL strides are illegal since DLPack 1.2), `byte_offset` is the view's offset,
  `DLPACK_FLAG_READ_ONLY` mirrors `isWritable()`, and padded float6/float4 views set
  `DLPACK_FLAG_BITMASK_IS_SUBBYTE_TYPE_PADDED`. The capsule keeps the buffer alive; the tensor's deleter
  releases it. The capsule is opaque, a PHP object holding the C pointer.
* `fromDLPack()` takes a capsule (consuming it once) and wraps the foreign memory in a `DLPackBuffer`
  whose `view` is the returned `TypedView`; the tensor's deleter runs exactly once, when that buffer is
  freed. The `TypedView::buffer()` of an imported tensor covers exactly the tensor's extent, so its
  `byteOffset()` starts at 0. Only `kDLCPU`, one lane, 1-8 dimensions, strides of at least 1 and
  data aligned to the element size are imported; **packed** float4/float6 are rejected (padded only);
  versions below 1.2 may pass NULL strides (compact row-major). Anything else throws `ValueError`
  with the reason, and a rejected capsule is not consumed (its deleter still runs once when freed).
* The C structs are the standard ones; nothing in PHP can see the raw pointer, so exchange with another
  extension or FFI happens through those C structs, not through a PHP-visible protocol.

## Verification

What has been run, on Linux x86-64 (Ubuntu host, Debian 12 and Alpine containers):

* `tests/` phpt suite (16 tests) on PHP 8.2, 8.3, 8.4 and 8.5, Debian/glibc and Alpine/musl via Docker
  (`tools/docker-test.sh`), plus ZTS images (8.4, 8.5 Debian, 8.4 Alpine), plus debug builds of PHP 8.5
  NTS and ZTS (assertions on); all pass, no compiler warnings with `-Wall -Wextra`.
* The whole suite under valgrind with `USE_ZEND_ALLOC=0` (`tools/valgrind.sh`): zero errors, zero
  leaks, including parked pool objects at shutdown and foreign-tensor deleters; also on the debug
  NTS and ZTS builds.
* `tools/oracle/run.sh decode|encode|doubles`: against ml_dtypes 0.5.4 - every code of every format of at
  most 16 bits decodes identically; every one of the 2^32 float32 inputs encodes identically for each of
  the 13 formats (float16 including NaN payloads); 20 million random doubles, exact midpoints and their
  neighbours per format encode identically.
* `tools/oracle/run-fuzz.sh`: a NumPy differential fuzz (random dtype, bytes, shape, strides, offset;
  reads through chained offsets and writes compared with NumPy `as_strided` over the same bytes) and
  `SCRIPT=view_fuzz.py tools/oracle/run-fuzz.sh`: hostile `view()` arguments against an exact
  big-integer model.
* Not run: macOS, Windows (`config.w32` and the workflows are written but never executed), real
  multi-threaded ZTS, aarch64.

Commands: `tools/test.sh [8.x]` builds out of tree and runs the suite; `benchmarks/run.sh` runs the benchmarks;
`tools/gen-arginfo.sh` regenerates `dlpack_arginfo.h` from `dlpack.stub.php`.

## Not in v1

`get()`/`set()` methods, array/tuple offsets, `fill/copy/cast/reshape/transpose/slice/toArray/fromArray`,
clamped writes, packed sub-byte layouts, negative or zero strides, non-CPU devices, arithmetic.

## Licence

MIT (`LICENSE`); `dlpack.h` is Apache-2.0 (`LICENSE.dlpack`, `NOTICE`).
