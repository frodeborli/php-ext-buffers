# ext-buffers

The shared representation of n-dimensional typed data for PHP: `Buffers\Buffer` (bytes on some device: allocated memory,
a memory-mapped file, memory of other extensions), `Buffers\Tensor` windows onto it (n-D, strided, every machine-learning
dtype including bfloat16, float8, packed float6, float4 and int2/int4/uint2/uint4, and complex), `Buffers\Shape`,
the `Buffers\TensorAggregate` and `Buffers\Device` interfaces, exact element-wise arithmetic on tensors (`+ - * / % ** << >> & | ^ ~`,
with broadcasting and defined type promotion), and a versioned C API (`php_buffers_api.h`) with readiness and transfer hooks and [DLPack](https://github.com/dmlc/dlpack) import and export for other extensions. Written in C,
no dependencies, PHP 8.3 - 8.5 (NTS and ZTS; 8.3 with the [differences](#php-83) below), with build files for Linux, macOS and Windows
(see [Verification](#verification) for what each has been tested on).

```php
use Buffers\{Buffer, DType};

$buf   = new Buffer(640 * 480 * 4);                      // zero-filled, 64-byte aligned, counts toward memory_limit
$rgba  = $buf->view(DType::UInt8, [480, 640, 4]);        // a Tensor: shape [rows, cols, channels]
$rgba[10][20][3] = 255;                                  // chained single-int offsets
echo $rgba[10][20][3];                                   // 255
$row = $rgba[10];                                        // a sub-view of the same memory
$row[20][0] += 7;                                        // writes through to $buf
echo $rgba->shape[-1], ' ', $rgba->ndim, ' ', json_encode($rgba->strides);   // 4 3 [2560,4,1]

$w = $buf->view(DType::Float8E4M3FN, 16);                // 16 one-byte floats
$w[0] = 1.5;

$file = Buffer::createFile('/tmp/model.bin', 128 + 32);                    // a new file, mapped writable (shared)
$file->view(DType::BFloat16, [4, 4], null, 128)[0][0] = 1.5;
$file->flush();
$weights = Buffer::fromFile('/tmp/model.bin', 128)->view(DType::BFloat16, [4, 4]);   // read-only, mapped: pages load on first touch
echo $weights[0][0];                                     // 1.5
```

## Install

```sh
pie install frodeborli/ext-buffers
```

or from source:

```sh
phpize && ./configure --enable-buffers && make && make install     # then extension=buffers
```

The GD integration (`Tensor::fromGdImage()`, `toGdImage()`) is built when `configure` finds `gd.h` (libgd-dev, the PHP include dir's
`ext/gd/libgd/` for a PHP built with bundled GD, or `--with-buffers-gd=DIR`); `--without-buffers-gd` turns it off. The header must match the libgd
that ext/gd runs with. ext/gd itself is not linked: it is only needed when one of the two methods is called (without it they throw `Error`).

`make install` also installs `php_buffers_api.h` and `dlpack.h` under `ext/buffers/` for extensions that build on
this one (see [C API](#c-api)).

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

Measured on an Intel Xeon E5-2697 v3 (Haswell, 2.6 GHz), PHP 8.5.12 NTS, Linux, nanoseconds per element access, best of 5-12 runs over a 640x480 RGBA
image. Interpreter = `opcache.jit=off`; JIT = `opcache.jit=tracing`, and a JIT column is only reported when
opcache's JIT buffer shows that code was actually compiled (`opcache.file_update_protection=0` is required: by
default opcache silently refuses to compile files younger than two seconds). Reproduce with `benchmarks/run.sh`, which prints the CPU it ran on.
On a current desktop CPU (AMD Ryzen 9 9950X3D) every row is about 2.5-4x faster, for example the 1-D read idiom 9.0 ns interpreted and
2.5 ns under the JIT; the ratios between rows are similar.

| access (alpha byte of each pixel)       | interpreter | JIT  | ext-dlpack JIT (same machine) |
|-----------------------------------------|------------:|-----:|------:|
| **read, view 1-D idiom** | 23.7 | 9.4 | 9.4 |
| **read, view 1-D, row base hoisted** | 17.8 | 7.2 | 8.5 |
| read, view `[$y][$x][$c]` (chained) | 67.4 | 49.4 | 49.1 |
| read, view `[$y]` hoisted, `[$x][$c]` | 41.2 | 28.1 | 27.7 |
| read, view 2-D `[$y][$x*4+$c]` | 43.7 | 27.8 | 27.8 |
| read, PHP array, flat | 20.4 | 3.2 | 3.5 |
| read, PHP array, nested | 24.9 | 6.1 | 6.1 |
| read, SplFixedArray, flat | 25.6 | 10.2 | 10.2 |
| **write, view 1-D idiom** | 22.1 | 8.3 | 8.3 |
| **write, view 1-D, row base hoisted** | 16.9 | 8.0 | 8.2 |
| write, view chained | 67.4 | 56.7 | 54.4 |
| write, view `[$y]` hoisted | 40.3 | 29.7 | 29.4 |
| write, PHP array, flat | 22.8 | 5.2 | 5.5 |
| write, PHP array, nested | 42.3 | 26.4 | 26.5 |
| write, SplFixedArray, flat | 25.3 | 11.3 | 11.2 |
| `+=`, view 1-D idiom | 29.0 | 15.7 | 15.8 |
| `+=`, view chained | 70.7 | 62.5 | 61.5 |

Median of three `benchmarks/run.sh` runs of each extension, back to back on the same Xeon E5-2697 v3 (PHP 8.5.12, JIT buffer verified:
"code was compiled"). The last column is the predecessor, ext-dlpack, built the same way: the 1-D and chained paths did not
regress (every view row is within about 4%, which is the run-to-run noise). The 1-D read idiom is bimodal under the JIT (9.4 ns, and 7.9 ns in
roughly one run in six, for both extensions: a code-layout effect, not a difference between them).

Other numbers (JIT, same Xeon): a 1-D read costs 7-8 ns and a write 7-11 ns for every integer, float and small-float dtype, including the
packed formats (read 7.8, write 11.1: a shift and mask, and a read-modify-write); `Complex*` reads cost about 41-47 ns
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

### Arithmetic speed

Nanoseconds per element for one operator on tensors of 1,000,000 elements (`benchmarks/arith.php`, PHP 8.5.12 NTS without
the JIT, best of 7), against the same work in a PHP loop over arrays:

| operation                                   | AMD Ryzen 9 9950X3D | Intel Xeon E5-2697 v3 |
|---------------------------------------------|--------------------:|----------------------:|
| `Float64` `$a + $b`                         | 0.44 | 3.18 |
| `Float32` `$a + $b`                         | 0.27 | 1.73 |
| `Float32` `$a * 2.5`                        | 0.24 | 1.35 |
| `Float16` `$a + $b`                         | 0.40 | 2.58 |
| `BFloat16` `$a + $b`                        | 0.067 | 0.53 |
| `Int32` `$a + $b`                           | 0.27 | 1.54 |
| `UInt8` `$a + $b`                           | 0.021 | 0.12 |
| `Float32` `[1000, 1000] + [1000]` (broadcast) | 0.25 | 1.67 |
| `Float32` `$a += 1.5` (written in place)    | 0.027 | 0.25 |
| PHP loop over arrays, `$r[] = $x[$k] + $y[$k]` | 11.7 | 63.0 |

A result in new memory costs mostly the operating system providing the pages (results of 2 MiB or more are mapped with
transparent huge pages where Linux allows it); `$a op= $b` writes into `$a`'s memory when it can (see Arithmetic) and
avoids that. Results are bit-identical on both CPUs (AVX-512 and AVX2 builds of the same loops).

## API

Everything lives in the namespace `Buffers\`. `buffers.stub.php` has the full signatures with docblocks.

**Properties and methods:** facts that do not change are read-only *properties*; state that can change, and anything computed on request,
is a *method*. (Enums cannot have properties, so `DType` has methods.)

```php
namespace Buffers;

enum DType: string { case Bool, Int2, Int4, Int8, Int16, Int32, Int64, UInt2, UInt4, UInt8, UInt16, UInt32, UInt64,
    Float16, Float32, Float64, BFloat16, Float8E3M4, Float8E4M3, Float8E4M3B11FNUZ, Float8E4M3FN, Float8E4M3FNUZ,
    Float8E5M2, Float8E5M2FNUZ, Float8E8M0FNU, Float6E2M3FN, Float6E3M2FN, Float4E2M1FN,
    Complex32, Complex64, Complex128, BComplex32;
    public function bits(): int;             // width of one element: 2, 4, 6, 8 ... 128
    public function byteSize(): ?int;        // bytes per element; null for the packed formats Int2, Int4, UInt2, UInt4, Float6*, Float4*
    public function isByteAligned(): bool;   // false for the packed formats
    public function isInteger(): bool;       // Int*, UInt* (including the packed ones)
    public function isFloatingPoint(): bool; // all Float*, BFloat16 and the small formats
    public function isComplex(): bool;
}

interface TensorAggregate {                  // anything that is or can produce a tensor; PHP classes can implement it
    public function getTensor(): Tensor;     // the numbers (an implementation may compute or load them)
    public function getDType(): ?DType;      // without computing anything; null if not known yet
    public function getShape(): Shape;       // without computing anything
}

final class Shape implements ArrayAccess, Countable, IteratorAggregate, JsonSerializable {
    public bool $concrete { get; }                  // every dimension is an int
    public int|TensorAggregate $size { get; }       // product; Error if not concrete (see "Shape")
    public function __construct(array $dims);       // list of ints >= 0 and 0-d TensorAggregates
    public function count(): int;
    public function toArray(): array;
}

interface Device {                                  // identified by name and id
    public string $name { get; }                    // "cpu", "file", "cuda:0"
    public int $id { get; }
    public bool $hostAccessible { get; }            // the device's general capability: memory the CPU can address
}
final class CpuDevice implements Device { public static function get(): CpuDevice; }
final class FileDevice implements Device { public static function get(): FileDevice; }
final class ForeignDevice implements Device {}      // the device of a tensor imported through the C API that is not on the CPU

final class Buffer {
    public int $byteLength { get; }
    public Device $device { get; }
    public function __construct(int $byteLength);   // zero-filled host memory; emalloc'd, so it counts toward memory_limit
    public static function fromString(string $data, bool $writable = true): Buffer;   // $writable = false: read-only (what backends bake into a plan as a constant)
    public static function fromFile(string $path, int $offset = 0, ?int $length = null, bool $map = true, bool $writable = false): Buffer;
    public static function createFile(string $path, int $byteLength): Buffer;   // creates or truncates; writable, mapped
    public function flush(): void;                  // pushes a writable mapping's changes to disk; no-op for other buffers
    public function writeTo($stream, int $offset = 0, ?int $length = null): int;    // memory straight to a stream; bytes written
    public function readFrom($stream, int $offset = 0, ?int $length = null): int;   // stream straight into memory; bytes read
    public function isWritable(): bool;
    public function readOnly(): Buffer;               // a read-only alias of the same memory (what backends bake into a plan as a constant)
    public function isHostAccessible(): bool;       // authoritative for this buffer (a mapped file is, FileDevice is not)
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): Tensor;
    public function toString(): string;             // a copy of the bytes; Error unless host accessible
}

final class Tensor implements TensorAggregate, Countable, ArrayAccess, IteratorAggregate, JsonSerializable {
    // read-only: DType $dtype, Shape $shape, int $ndim, list<int> $strides (in elements), int $offset (bytes), Buffer $buffer
    // read-write, 0-d views only: mixed $value
    // operators: + - * / % ** << >> & | ^ ~ element-wise, see "Arithmetic"
    public const MAX_NDIM = 16;                     // the most axes a tensor can have
    public function getTensor(): Tensor;            // itself
    public function getDType(): DType;
    public function getShape(): Shape;
    public function slice(int $axis, int $start, ?int $length = null, int $step = 1): Tensor;   // views: see "Views"
    public function transpose(int $axis1 = -2, int $axis2 = -1): Tensor;
    public function permute(array $axes): Tensor;
    public function reshape(array $shape): Tensor;
    public function squeeze(int $axis): Tensor;
    public function unsqueeze(int $axis): Tensor;
    public function broadcastTo(array $shape): Tensor;
    public function copy(?Tensor $into = null): Tensor;   // the elements, byte for byte: a new contiguous tensor, or into $into
    public function convert(DType $dtype, ?array $axes = null): Tensor;   // another dtype (see "Conversion") and/or axis order; a new host tensor
    public function to(Device $device): Tensor;     // the same tensor on another device (see "Devices and files")
    public static function fromArray(array|int|float|bool $data, ?DType $dtype = null, ?Device $device = null): Tensor;
    public function toArray(): mixed;               // nested arrays; a 0-d tensor gives the scalar (see "PHP arrays and GD images")
    public static function fromGdImage(\GdImage $image, ?Tensor $into = null): Tensor;   // with ext/gd at build time
    public function toGdImage(): \GdImage;
    // count() = length of the first axis; foreach iterates the first axis; $v[i], isset($v[i]), $v[i] = x
}

final class DataView {                              // single values at any byte offset, see DataView below
    // read-only: Buffer $buffer, int $byteOffset, int $byteLength
    public function __construct(Buffer $buffer, int $offset = 0, ?int $length = null);
    public function getFloat32(int $offset, bool $bigEndian = false): float;   // and Int8 ... Float64, BFloat16, Float16, Float8*
    public function setFloat32(int $offset, float $value, bool $bigEndian = false): void;
    public function getBytes(int $offset, int $length): string;               // byte ranges, see DataView below
    public function setBytes(int $offset, string $bytes): void;
    public function fill(int $offset, int $length, int $byte): void;
    public function copyBytes(int $target, int $source, int $length): void;  // overlap-safe
    // read-only accessors, $d->uint32[$byteOffset]: DataViewAccessor $int8 ... $uint64, $float16, $bfloat16, $float32, $float64
}

final class DataViewAccessor implements ArrayAccess { // values of one type at any byte offset: $a[$byteOffset], isset($a[$byteOffset]), $a[$byteOffset] = x
    // read-only: DType $dtype
}
```

### `view()`

* `shape`: `null` = 1-D covering the rest of the buffer (the remaining size must be a whole number of elements);
  an `int` = 1-D of that length; an `array` of 0-16 non-negative ints = n-D. **`[]` makes a 0-d view of one
  element** (see below).
* `strides`: in elements, one per dimension, each at least 0 (negative strides are rejected). A stride of 0 repeats one element along
  an axis (a broadcast); a view with a stride of 0 on an axis of more than one element is read-only;
  `null` = C-contiguous. Overlapping strides are allowed.
* `offset`: bytes, any offset: a `Float32` at byte 1 can be viewed, read and written like any other (elements are loaded and stored
  with `memcpy`, so alignment never matters to PHP code). Alignment matters only to native code that loads through typed pointers: it
  checks the data pointer itself and may refuse an unaligned view (see [C API](#c-api)). For single values at any byte offset there is
  [`DataView`](#dataview).
* Everything is validated once, with overflow-safe arithmetic, so element access needs only a bounds check. A view
  keeps its buffer alive. Empty views (a dimension of 0) are valid.
* On 32-bit PHP, `Int64`, `UInt64` and `UInt32` are rejected with a `ValueError` (their values do not fit a PHP
  int), and so is DLPack import in the C API. Everything else works.

### Properties

`$v->dtype`, `$v->shape` (a `Shape`), `$v->ndim`, `$v->strides` (a list, in elements), `$v->offset` (bytes from the start of the
buffer to element 0) and `$v->buffer` are read-only: writing or unsetting one throws `Error`. `$v->value` is the one read-write
property: the single element of a 0-d view (see below). They are *virtual
properties with internal get hooks*, exactly what a PHP 8.4 class would declare as `public int $ndim { get => ... }`:
nothing is stored in the object, so creating a sub-view (`$v[$y]`, served from a pool) allocates no `Shape`, no array
and nothing else. Reflection, `get_object_vars()`, `(array)` and `var_export()` see them (`value` only on 0-d views of host memory);
`var_dump()` and `print_r()` show a purpose-built summary (see [Debug output and JSON](#debug-output-and-json)). The same rule gives `Buffer` its `byteLength` and `device` properties, `Shape` its `size` and `concrete`, and every `Device` its `name`, `id` and `hostAccessible`.

#### PHP 8.3

PHP 8.3 has no property hooks, so there the same getters are reached through internal `__get`, `__set`, `__isset` and `__unset` methods
that ext-buffers gives each class with such properties (also classes of other extensions, through `buffers_declare_virtual_props()`;
subclasses inherit them). Reading, writing `$value`, `isset()`, `??`, the read-only errors and the allocation-free sub-views behave as on 8.4.
What differs on 8.3 (support ends when 8.3 does, 2028-01-01):

* The properties are not declared: Reflection, `property_exists()`, `get_object_vars()`, `(array)` and `var_export()` do not see them.
  `var_dump()` and `print_r()` are unchanged. (`TensorAggregate` has methods, not properties, so it is the same on every version.)
* An extension class that PHP code may extend and that declares properties with `buffers_declare_virtual_props()` must, on 8.3, serve
  them from its own `read_property` handler, or a subclass's own `__get` would replace ext-buffers' (see `php_buffers_api.h`). The classes
  of ext-buffers are final.
* `unset($v->ndim)` says "Cannot unset property" (8.4: "Cannot unset hooked property").
* `$v->strides[] = 1` and `$r = &$v->value` give PHP's notice "Indirect modification of overloaded property" and act on a copy (8.4: an Error).

### Element access

`$v[$x][$y][$z]` takes one integer offset per level. A partial chain is a sub-view sharing the same memory; the leaf
is a scalar.

| dtype                | read gives                                | write accepts                                           |
|----------------------|-------------------------------------------|---------------------------------------------------------|
| `Int*`, `UInt*`      | `int` (`UInt64` as the signed 64-bit pattern) | `int` (wraps by C truncation) or `float` (PHP's `(int)`: NaN/INF become 0; no clamping) |
| `Bool`               | `bool`                                    | `bool` only                                             |
| float dtypes         | `float`                                   | `int` or `float`, rounded to nearest even               |
| `Complex*`           | `[re, im]`                                | a number, or `[re, im]`                                 |

### Views

These methods return a new `Tensor` over the same memory (a view): writing through it changes the original, and nothing
is copied. Negative axes count from the end.

```php
$m = Tensor::fromArray(range(0, 11), DType::Float32)->reshape([3, 4]);
$m->slice(1, 1, 2);            // columns 1 and 2: [3, 2]
$m->slice(0, 0, null, 2);      // every other row: [2, 4]
$m->transpose();               // [4, 3]; transpose($a, $b) swaps two axes (PyTorch's meaning, not NumPy's)
$m->permute([1, 0]);           // the axes in any order
$m->reshape([2, -1]);          // [2, 6]; -1 is inferred
$m->unsqueeze(0)->squeeze(0);  // [1, 3, 4], back to [3, 4]
Tensor::fromArray([1, 2, 3, 4], DType::Float32)->broadcastTo([3, 4]);   // read-only, stride 0 on axis 0
$m->transpose()->copy();       // a contiguous copy
```

* `slice($axis, $start, $length = null, $step = 1)`: every `$step`-th element of the axis from `$start` (0 to the axis length),
  `$length` of them or as many as fit. `$step` is at least 1.
* `reshape($shape)`: the same elements in another shape, with at most one `-1`. When the strides do not allow a view (a
  transposed tensor into fewer axes, for example) it is a `ValueError`; `$t->copy()->reshape(...)` copies first. It never
  copies by itself, so whether the result shares memory does not depend on the layout.
* `transpose($axis1 = -2, $axis2 = -1)` swaps two axes, by default the last two: PyTorch's meaning. NumPy's `transpose()`
  reverses all axes; here that is `permute()` with the axes in reverse order.
* `squeeze($axis)` drops that axis, which must have length 1 (the axis is required: dropping every axis of length 1 would also
  drop a batch axis that happens to be 1); `unsqueeze($axis)` adds one at position `$axis` (0 to `ndim`).
* `broadcastTo($shape)`: axes are aligned from the right; an axis of length 1, or a missing one, repeats its element with a
  stride of 0. The result is read-only.
* `copy(?Tensor $into = null)` copies the elements byte for byte (NaN payloads included): into a new C-contiguous tensor in host
  memory, or into `$into`, a writable tensor of the same dtype and shape, which is returned. That is how to write into memory that
  other views share on purpose (`$src->copy($big->slice(1, 1, 2))`); a copy into memory overlapping the source reads all of it first.
* The packed sub-byte dtypes keep their last axis last and contiguous: `slice()` of it takes step 1 and a start on a whole byte,
  `reshape()` keeps its length, and `transpose()`, `permute()`, `unsqueeze()` and `broadcastTo()` cannot move or repeat it
  (`ValueError`).

### Conversion

`$t->convert(DType $dtype, ?array $axes = null)` returns the elements in another dtype as a new C-contiguous tensor in
host memory, in one pass that allocates nothing but the result (a memory-mapped checkpoint converts without an
intermediate copy). With `$axes` (a permutation of `0 .. ndim - 1`) axis `i` of the result is axis `$axes[i]` of `$t`, so
`$m->convert($m->dtype, [1, 0])` is the transpose. Element writes and `buffers_convert()` (C API) follow the same rules:

* **To an integer dtype**: an integer wraps to the width (two's complement); a float is converted as PHP's `(int)` does
  (truncated toward zero; NaN and infinities give 0; values outside the 64-bit range wrap modulo 2^64), then wraps to
  the width; a complex number gives its real part.
* **To `Bool`**: non-zero is `true` (a complex number: either part non-zero).
* **To a float dtype**: rounded to nearest even, once. Overflow gives infinity in the formats that have it; the 8/6/4-bit
  formats without infinity give NaN or saturate to their largest value, each as ml_dtypes does (see [Dtypes](#dtypes)).
  An integer is rounded once to
  `Float32` for every format of at most 32 bits, as NumPy and ml_dtypes do.
* **To a complex dtype**: real values get an imaginary part of 0; both parts round to the component format.

The packed sub-byte dtypes are not supported by `convert()` (`Error`); `view()` and element access read and write them.

### Error policy

Any non-int offset throws `TypeError`; an out-of-range or negative index throws `ValueError` (`isset()` and `??` just
say no/null); `$v[] = x`, `unset($v[i])`, writing a sub-view, writing to a read-only view, and a value of the wrong
type (`TypeError`) all throw. Tensors, shapes, devices and buffers cannot be cloned, serialized,
instantiated by the user or given dynamic properties. Bad `view()` arguments throw `ValueError` naming the
argument and the reason. Elements of memory that is not host accessible (see Devices) throw `Error`.

### 0-d views

`$buf->view(DType::Float32, [])` is a view of exactly one element: `ndim` 0, `shape` `[]`, `strides` `[]`. It has no axes, so
`count()`, `foreach`, `$v[0]`, `isset($v[0])` and friends throw `Error("A 0-d view has no axes")`. It is a
`TensorAggregate`, shares memory with other views of the buffer, and can be exported and imported through the C API.

Its single element is the read-write property **`$v->value`**: `$v->value` reads it, `$v->value = 3` writes it with the normal
conversion rules of the dtype (like `$v[$i] = 3` on a 1-D view). It is a virtual property with get and set hooks, so nothing
is stored per view. On a view with axes (`ndim > 0`) both throw `Error("value is only available on 0-d views; index the view
instead")`. Read-only buffers, memory that is not host accessible and pending buffers behave as they do for element access.

### Arithmetic

`+ - * / % ** << >> & | ^ ~` (and `+=` etc., unary `-`) work element-wise on tensors and PHP `int`/`float`/`bool` scalars,
computed at once into a new C-contiguous tensor on the operands' device (host memory: ext-buffers' own C loops). The rules (full text in
[docs/arithmetic.md](docs/arithmetic.md)) are the [Python Array API standard's](https://data-apis.org/array-api/)
with PHP's integer semantics:

```php
$img = Tensor::fromArray([[0, 128], [255, 64]], DType::UInt8);
$x = $img / 255 - 0.5;               // Float64: `/` always gives a float; [[-0.5, 0.00196...], [0.5, -0.249...]]
$y = Tensor::fromArray([1.5, 2.5], DType::Float16) * $x;   // broadcast [2] x [2, 2]: Float64
$bias = Tensor::fromArray([1, 2], DType::Int8);
$m = Tensor::fromArray([[1, 2], [3, 4]], DType::Int16) + $bias;   // Int16, the bias added to each row
```

* **Shapes** broadcast: aligned from the right, a dimension of 1 stretches; anything else is a `ValueError` naming both shapes.
* **Result dtype**: the narrowest that holds both operands' types (`UInt8 + Int8` is `Int16`); integer with float gives
  the float; PHP scalars take the tensor's dtype (`$uint8 + 1` stays `UInt8`, `$float16 * 0.5` stays `Float16`; a PHP float
  with an integer tensor gives `Float64`); `/` always gives a float. `Float16` with `BFloat16` and `UInt64` with a signed
  type have no common dtype (`TypeError`: `convert()` one of them). `tests/promotion.tsv` lists every case.
* **Integers** wrap on overflow; `%` takes the sign of the dividend and `%` by zero throws `DivisionByZeroError`, as in PHP;
  shifts by a negative count throw `ArithmeticError`, by the width or more give 0 (or -1 for `>>` of a negative value);
  `**` with a negative integer exponent is a `ValueError`. A PHP int that does not fit the tensor's integer dtype is a
  `ValueError` (so `-$uint8` throws).
* **Floats** are IEEE 754: `+ - * /` and `%` (`fmod`) are exact (correctly rounded, the same bits on every machine;
  `Float16` and `BFloat16` computed in `Float32` and rounded once); `**` is within 4 ULP (`Float32`) or 2 ULP (`Float64`).
  Division by zero gives `INF`/`NAN`.
* `Bool` takes only `& | ^ ~` with `Bool`; storage-only formats (8/6/4-bit floats, packed integers) and complex dtypes have no
  arithmetic (`TypeError`: `convert()` first).
* `$a += $b` is `$a = $a + $b`: other variables and views of the old tensor keep their values. A failed operation leaves
  `$a` unchanged. When nothing can observe the difference, the result is written into `$a`'s own memory without
  allocating: `$a` holds the only reference to its tensor and that tensor the only reference to its buffer (no other view,
  `DataView` or extension holds it), the buffer is writable host memory that has not been exported (DLPack) or mapped
  from a file, and the result has `$a`'s dtype and shape. So `$pos += $vel * $dt;` in a simulation loop does not allocate
  for `$pos`.
* Other operands: an object of a class with arithmetic of its own (an extension class such as `Skein\Expression`) gets the
  operation, on either side; any other `TensorAggregate` (a PHP class) is computed through its `getTensor()`; anything
  else (strings, arrays, `null`, other objects) is PHP's `TypeError`.
* Comparisons are not element-wise: a tensor compares like an enum case. `==` is true only for the same object and false
  against anything else (no notice, so `in_array(1, [$t])` is false); `<`, `<=`, `>`, `>=` are false in both orders and
  `<=>` gives 1. A tensor is truthy like every object. `Buffer`, `DataView` and devices compare the same way.
* Tensors on another device (a GPU) are computed by a backend that the extension owning the device registered; without
  one, or for operands on different devices, it is an `Error` naming the devices (no implicit copies).
* Speed: plain C loops the compiler vectorizes. Built with GCC for x86-64 Linux (glibc), each loop exists for the baseline,
  x86-64-v3 (AVX2) and x86-64-v4 (AVX-512), and the dynamic loader picks the one for the CPU; every variant gives the same
  bits. See Performance for measurements.

### DataView

`new DataView(Buffer $buffer, int $offset = 0, ?int $length = null)` reads and writes single values at any byte offset of a buffer, like
JavaScript's `DataView` over an `ArrayBuffer` (`Buffer` itself stays minimal). Read-only properties `$buffer`, `$byteOffset`, `$byteLength`.

```php
$d = new DataView($buf, 16);                 // bytes 16 .. end of $buf
$d->setFloat32(1, 1.5);                      // any offset, relative to the view
$x = $d->getFloat32(1);                      // 1.5
$n = $d->getUInt32(5, bigEndian: true);      // network byte order
```

* `get<T>(int $offset, bool $bigEndian = false)` and `set<T>(int $offset, $value, bool $bigEndian = false)` for `Int16`, `Int32`, `Int64`,
  `UInt16`, `UInt32`, `UInt64`, `Float16`, `BFloat16`, `Float32` and `Float64`; `Int8`, `UInt8` and the one-byte floats (`Float8E3M4` ...
  `Float8E8M0FNU`) take no byte order. The names are the `DType` case names.
* **Little-endian by default** (JavaScript's default is big-endian); `$bigEndian = true` matches `pack()`'s `n`/`N`/`J`/`E`/`G` formats.
* `$offset` is bounds-checked against the view (`ValueError`); setting on a read-only buffer, and any access to memory that is not host
  accessible, throws `Error`. Integers wrap to the width when set; `UInt64` values above `PHP_INT_MAX` read as negative ints, like `unpack()`.
  Floats round as `Tensor` element writes do. On 32-bit PHP, `Int64`, `UInt64` and `UInt32` throw `Error`.


#### Byte ranges: `getBytes`, `setBytes`, `fill`, `copyBytes`

```php
$s = $d->getBytes(4, 16);                    // a copy of bytes 4..19 of the view, as a string
$d->setBytes(4, $s);                         // writes all of $s at byte 4
$d->fill(0, 64, 0);                          // memset: 64 zero bytes from byte 0
$d->copyBytes(100, 96, 32);                  // memmove: 32 bytes from 96 to 100, overlap-safe
```

* Raw bytes: no encoding, no NUL termination, no byte order. One `memcpy` / `memset` / `memmove` each.
* Offsets are relative to the view. A range must lie inside the view or the call throws `ValueError` naming the argument, before
  anything is written: `setBytes` never truncates. Empty ranges are valid, also at `byteLength`.
* `fill`'s `$byte` wraps to 0..255, like `setUInt8`. `copyBytes` takes the target first, like `memmove` and `setBytes`.
* Writes on a read-only buffer throw `Error`, as does any access to memory that is not host accessible.

#### Accessors: `$d->uint32[$byteOffset]`

A method call costs more than index syntax, so for hot loops a `DataView` also hands out one **accessor** per type, read with
index syntax where **the index is a byte offset**:

```php
$d   = new DataView($buf);
$u32 = $d->uint32;                           // a DataViewAccessor; the same object on every read
$x   = $u32[5];                              // the uint32 in bytes 5..8 of the view: $d->getUInt32(5)
$u32[6] = 0xDEADBEEF;                        // bytes 6..9 (overlapping the previous one): $d->setUInt32(6, ...)
```

* Properties `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `float16`, `bfloat16`, `float32`, `float64`;
  the accessor's `$dtype` says which. The one-byte float formats are only available through `get`/`set`.
* **Little-endian.** Big-endian values go through `get`/`set` with `$bigEndian = true`.
* Valid indexes are `0 .. byteLength - size`. Other ints throw `ValueError`, non-ints `TypeError`; `isset()` and `??` just say no/null,
  and `empty()` looks at the value. `$a[] = x` and `unset($a[i])` throw.
* Values convert as in [element access](#element-access): integers wrap, a float written to an integer type is truncated, floats round
  to nearest even. Read-only buffers throw `Error` on write; memory that is not host accessible throws `Error` on access.
* An accessor keeps the buffer alive and caches the address of the view's first byte, like a `Tensor`. Access is a type check, one
  bounds check and a load or store through the engine's dimension handlers: no method call, and the same cost for every type.
  It cannot be instantiated, cloned or serialized.

This is what code that addresses memory by byte needs: binary formats, network protocols, and WebAssembly linear memory, where every
load and store is a typed value at an arbitrary byte address.

### Shape

`new Shape([2, 3])`; `$shape[-1]` counts from the end; an out-of-range index throws `ValueError`; `offsetSet`/`offsetUnset`
throw `Error` (a Shape is immutable); `==` compares by value. A dimension is an int or a **0-d `TensorAggregate`** (a symbolic
dimension, for extensions that build deferred computations; 0-d means its `getShape()` has no dimensions). `$shape->size` is the product when the shape is
concrete (`ValueError` above `PHP_INT_MAX`); a shape with symbolic dimensions has no integer size, and reading it throws
`Error`. The shape of a `Tensor` is always concrete.

### TensorAggregate

`TensorAggregate` is the common face of anything that is a tensor or can produce one: a `Tensor` (which returns itself), a
lazy expression of another extension, or a plain PHP class (a sensor, a column of a database result, a file format):

```php
use Buffers\{DType, Shape, Tensor, TensorAggregate};

final class Thermometer implements TensorAggregate {
    public function getTensor(): Tensor { return Tensor::fromArray($this->readAll(), DType::Float32); }
    public function getDType(): DType { return DType::Float32; }
    public function getShape(): Shape { return new Shape([$this->count]); }
}
$celsius = $thermometer - Tensor::fromArray(273.15, DType::Float32);   // arithmetic calls getTensor()
```

`getDType()` and `getShape()` describe the tensor without producing it (`getDType()` may return `null` when the dtype is not
known yet); `getTensor()` produces it, and may compute or load it. Arithmetic accepts a `TensorAggregate` operand of a
class without arithmetic of its own by calling `getTensor()` when the other operand is a `Tensor` (see Arithmetic). PHP
runs no operator code for a plain object with a number or with another plain object, so `$thermometer - 273.15` is
PHP's `TypeError`; `$thermometer->getTensor() - 273.15` works.

### Devices and files

A device has a `name`, an `id` and a general `hostAccessible` capability (properties). `$buffer->device` says where the buffer
lives; `$buffer->isHostAccessible()` is authoritative for the buffer itself. A `Buffer` made by the constructor or `fromString()` is on
`CpuDevice::get()`. Memory of other devices comes through the C API (DLPack import: any device, as a `ForeignDevice` with the name
`cuda:0` and so on; host-accessible only for the DLPack host kinds CUDA-host, ROCm-host and CUDA-managed). Reading, writing or
iterating the elements of memory that is not host accessible throws `Error`; making sub-views and reading properties work.

**`Buffer::fromFile($path, $offset = 0, $length = null, $map = true, $writable = false)`** gives a buffer on `FileDevice::get()` over a byte range of a
file (read-only unless `$writable`) (the range is checked against the file size when the file is opened; `ValueError` if it does not fit).

* mapped (`$map = true`, the default): the range is memory-mapped (`mmap`; `CreateFileMapping` + `MapViewOfFile` on Windows, written
  but never run). The buffer is host accessible: element access, `toString()` and the C API work on it directly, and pages load on first touch.
  **Truncating a mapped file from another process crashes the PHP process with SIGBUS on Linux and macOS.** Deleting it does not.
* writable (`$writable = true`, which needs mapping): a **shared** mapping (`MAP_SHARED` / `FILE_MAP_WRITE`): writes through tensors go to the file,
  and other processes that map the file see them at once. `flush()` pushes them to disk (`msync` / `FlushViewOfFile`; a no-op for other buffers).
  `Buffer::createFile($path, $byteLength)` creates (or truncates) a file of that size, zero-filled, and returns a writable mapped buffer.
* unmapped (`$map = false`, or when the range cannot be mapped, for example a range larger than the address space of 32-bit PHP): a
  storage-only buffer. `isHostAccessible()` is false; element access and `toString()` throw; `Tensor::to(CpuDevice::get())` reads
  the bytes a view needs (`pread` / `ReadFile`). (On 32-bit PHP `byteLength` of a buffer beyond 2 GB throws: it does not fit an int.)

**Byte I/O.** `$buffer->writeTo($stream, $offset = 0, $length = null)` writes bytes straight from the buffer's memory to a PHP stream
(`php_stream_write`, looping on short writes; a non-blocking stream that stops accepting data ends the loop and the count says how far it got)
and returns the bytes written; `$buffer->readFrom($stream, $offset = 0, $length = null)` reads from a stream straight into the buffer, stopping at
the end of the stream (or when a non-blocking stream has nothing more), and returns the bytes read. Both need host-accessible memory; `readFrom()` also
needs a writable buffer. They are format-agnostic: parsing a header is PHP's job, then `readFrom()` or `fromFile()` with an offset brings the data in.

**`$tensor->to($device)`** returns the tensor itself if its buffer is already on that device; otherwise a C-contiguous copy (packed
formats: rows padded to whole bytes) in a new `Buffer` on the device. The core copies host-accessible buffers (including mapped files) to
`CpuDevice` and reads unmapped file buffers. Any other transfer throws `Error("no backend for device 'cuda:0'")` unless an extension
registered one (`buffers_register_transfer()`): that is how GPU extensions plug in.

### Sharing a buffer between processes

A shared writable mapping is shared memory: `Buffer::createFile('/dev/shm/frames', $bytes)` in one process and
`Buffer::fromFile('/dev/shm/frames', 0, null, true, true)` (or read-only) in others give tensors over the same bytes; a write is visible to the others at
once. `/dev/shm` (or any tmpfs) keeps it in memory. **There is no synchronisation in this extension and no atomics.** In particular:

* coordinate with `flock()` (on a lock file) or APCu; a read-modify-write (`$t[$i] += 1`) from two processes loses updates;
* a packed sub-byte write (int2, int4, float4, float6, ...) rewrites a whole byte: neighbouring elements written by another process race;
* there are no ordering guarantees between processes (no memory barriers beyond what PHP happens to do), and 16-byte `Complex128` elements, or any
  element written in several instructions, can tear when read concurrently;
* one machine only: memory mapped files on network file systems are not coherent.

## Dtypes

Follows [ml_dtypes](https://github.com/jax-ml/ml_dtypes) 0.5.4 for every format, byte for byte, except that NaN written into the
float6/float4 formats (which have no NaN) is stored as zero of the same sign (see
[Verification](#verification)). Elements are stored **little-endian on every machine**, like WebAssembly memory,
safetensors, GGUF and ONNX files, so the same bytes mean the same numbers everywhere (there is no byte-order option;
`DataView` reads and writes either order explicitly). Every supported 64-bit platform is little-endian, where this
costs nothing; on a big-endian machine elements are byte-swapped on access, and DLPack exchange (which uses the host's
order) of multi-byte dtypes is refused.

Every dtype is a format of single elements. Block-scaled formats (MXFP4/6/8, NVFP4, the quantization blocks of GGUF) are
not dtypes: stored as separate planes they are a tensor of elements plus a tensor of scales (for example `Float4E2M1FN`
and `Float8E8M0FNU`); stored interleaved they are `UInt8` bytes that the library knowing the block layout interprets.
DType cases are only ever added, never reordered or removed.

| `DType`              | value                | bits | bytes | DLPack code/bits (C API) | notes |
|----------------------|----------------------|-----:|------:|------------------|-------|
| `Bool`               | `bool`               | 8    | 1     | 6 / 8            | |
| `Int2`, `Int4`       | `int2`, `int4`       | 2, 4 | null  | 0 / bits         | **packed**; sign-extended on read, writes wrap |
| `Int8` .. `Int64`    | `int8` .. `int64`    | 8-64 | 1-8   | 0 / bits         | |
| `UInt2`, `UInt4`     | `uint2`, `uint4`     | 2, 4 | null  | 1 / bits         | **packed**; writes wrap |
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
exactly as ml_dtypes does, and round to nearest even. The saturating float6/float4 formats have no NaN: writing NaN stores
zero of the same sign (`-NaN` gives `-0`). The
small formats (at most 8 bits) decode through a 256-entry lookup table built at startup from the same format
descriptors the encoder uses; the encoders are integer bit manipulation specialised per format (no floating point
rounding mode is involved).

### Packed sub-byte storage

`Int2`, `Int4`, `UInt2`, `UInt4`, `Float6E2M3FN`, `Float6E3M2FN` and `Float4E2M1FN` have **one layout only: packed**, as DLPack's default defines it:
element *i* of a row occupies the bits `[i*bits, (i+1)*bits)` of the row's bytes, little-bit-endian (bit 0 of byte 0
is the lowest bit of element 0), independent of the machine's byte order. `byteSize()` is `null` and
`isByteAligned()` is false. Integer elements read as `int` (sign-extended for `Int2`/`Int4`), and writes wrap by truncation to the width; a float is converted like for any
other integer dtype (PHP's `(int)`, NaN and INF become 0). (NumPy and ml_dtypes store these formats padded, one element per byte; a DLPack tensor
marked `IS_SUBBYTE_TYPE_PADDED` is rejected on import with a clear message, and exports never set the flag.)

`view()` rules for them, so that every sub-view starts on a byte boundary:

* the last axis has stride 1 (contiguous);
* every other stride times `bits` is a multiple of 8 (4-bit types: even strides; 2- and 6-bit types: multiples of 4) -
  `view(DType::Float6E2M3FN, [2, 3])` is therefore an error (a row of 3 elements is 18 bits): pass explicit strides
  such as `[4, 1]`, or a shape like `[2, 4]`;
* `offset` is in bytes; `shape = null` needs `byteLength * 8` to divide evenly by `bits`.

`$v->strides` reports elements (the last is 1) and `$v->offset` bytes. fp6 elements may straddle a byte boundary:
reads assemble a 16-bit window byte by byte (`(b0 | b1 << 8) >> shift`, never a native 16-bit load) and writes
read-modify-write, so neighbours are untouched and no byte outside the view's extent is ever read or written.

## PHP arrays and GD images

`Tensor::fromArray(array|int|float|bool $data, ?DType $dtype = null, ?Device $device = null)` makes a tensor from nested arrays and
`$tensor->toArray()` gives them back (the same data as `jsonSerialize()`; a 0-d tensor gives the scalar).

* The shape comes from the first path down the nesting (at most 16 levels); every other path must match it, or `ValueError` names the
  offending path (`ragged array at [2][1]: expected 2 elements, found 1`). Keys are ignored: values are taken in order. `[]` is shape `[0]`,
  `[[], []]` is `[2, 0]`; a scalar gives a 0-d tensor.
* Without a dtype: int is `Int64` (`Int32` on 32-bit PHP), float `Float64`, bool `Bool`, int mixed with float `Float64`; bool mixed with numbers needs a
  dtype; an empty array is `Float64`.
* With a dtype, integer dtypes (including the packed `Int2`, `Int4`, `UInt2`, `UInt4`) reject out-of-range values with `ValueError`
  instead of wrapping (`UInt64` accepts 0..`PHP_INT_MAX`, the PHP int range); floats that are whole numbers in range are accepted. `Bool` takes only
  bool and numeric dtypes take no bool. Float dtypes round as elsewhere (overflow gives INF). Complex dtypes are not supported.
* The tensor is built in CPU memory and moved with `to($device)` if a device is given.

With ext/gd available at build time: `Tensor::fromGdImage(\GdImage $img, ?Tensor $into = null)` gives a `UInt8` tensor `[height, width, 4]` (R, G, B, A) from a true color
or palette image. GD's alpha `a` (0 opaque ... 127 transparent) becomes `(127 - a) * 255 / 127` rounded to nearest, so 0 gives 255 and 127 gives 0.
`$tensor->toGdImage()` takes `UInt8` `[h, w, 3]`, `[h, w, 4]` or `[h, w]` (grey), strided views included, and makes a true color image; alpha `A`
becomes `127 - round(A * 127 / 255)` (GD has 128 levels: 256 alpha values map to the nearest of them, at most 1 off after a round trip), and
an RGBA image has `imagesavealpha()` on. Other dtypes or shapes, and empty axes, are an `Error`. With `$into` (a writable host-memory `UInt8` tensor `[h, w, 4]`, contiguous or strided) nothing is allocated: the pixels are written there and
`$into` is returned (`ValueError` naming the expected shape otherwise). Both walk the pixels once in C with one 32-bit store or load per pixel
when the pixels are contiguous (`benchmarks/gd.php`). The `gd.h` the extension was built with must be the version ext/gd was built with: both
calls compare it with the `GD_MAJOR_VERSION`, `GD_MINOR_VERSION` and `GD_RELEASE_VERSION` constants of ext/gd and throw an `Error` naming both versions.

## Debug output and JSON

`var_dump()` and `print_r()` of a `Tensor` show `dtype`, `shape` (as an array), `strides`, `offset`, `device` (a name such as `cpu` or
`cuda:0`), `writable` and `data`: the contents as nested arrays when the view has at most 1000 elements and its memory is host accessible,
otherwise a short note (`"(1048576 elements)"`, `"(not host-accessible)"`, `"(pending)"` for a buffer whose readiness hook has not run: dumping
never triggers a computation). A `Shape` shows its `dims`. `var_export()`, `get_object_vars()` and `(array)` give the properties instead. All of
this is built on demand by handlers that no element access goes through.

`Tensor` and `Shape` implement `JsonSerializable` (when the json extension is loaded, which it is in every normal PHP build).
`json_encode($view)` is the **full** data as nested arrays (a 0-d view: the scalar; complex elements: `[re, im]`; `UInt64` as the signed
pattern); `json_encode($shape)` is the list of dimensions, and throws `Error` for a symbolic dimension. json_encode's own rules
apply: **NaN and INF make it fail** ("Inf and NaN cannot be JSON encoded", or the partial output / `JsonException` flags you asked for).
A view whose memory is not host accessible throws `Error`; a pending buffer is made ready first. `DType` encodes as its string value.

## DLPack (C API only)

[DLPack](https://github.com/dmlc/dlpack) v1.3 (`dlpack.h` is vendored unmodified, Apache-2.0, see `NOTICE`) is the boundary to native
libraries, and it is a **C-level** interface: PHP code has no DLPack objects or methods. Another extension imports and exports through
`buffers_dlpack_import()` and `buffers_dlpack_export()` (and looks codes up with `buffers_dtype_describe()` / `buffers_dtype_from_dlpack()`
and `buffers_device_dlpack()`).

* **Export** produces a malloc'd `DLManagedTensorVersioned`; whoever ends up owning it calls its deleter exactly once. **Strides are always
  materialised** (NULL strides are illegal since DLPack 1.2), `byte_offset` is the view's offset, `DLPACK_FLAG_READ_ONLY` mirrors the
  buffer, packed sub-byte dtypes (int2, int4, uint2, uint4, float6, float4) are exported packed (no `IS_SUBBYTE_TYPE_PADDED`), 0-d tensors
  with `ndim` 0. The device is the buffer's: `kDLCPU` for host memory and for a mapped file, a foreign device's own type and id passed through
  unchanged (the pointer is an opaque handle and is never dereferenced). An unmapped file buffer cannot be exported ("transfer to the host
  first with to()"). The export holds the buffer; the deleter releases it. A pending buffer is made ready first (see [Readiness](#readiness)).
  A view at an offset that is not a multiple of the element size is exported as it is: DLPack allows any `byte_offset`, but consumers that load through typed
  pointers may reject it.
* **Import** wraps the foreign memory in a `Buffer` and returns a `Tensor`; the tensor's deleter runs exactly once, when that buffer is
  freed (and, if the import is rejected, the caller still owns the tensor). For host-accessible tensors the buffer covers exactly the
  tensor's extent, so `$v->offset` starts at 0; for other devices the opaque data pointer and byte offset are kept. Any device, one lane,
  0-16 dimensions and strides of at least 0 are imported (a stride of 0 on an axis of more than one element gives a read-only view) (host data need not be aligned: the view is then unaligned); **padded** sub-byte tensors
  are rejected; versions below 1.2 may pass NULL strides (compact row-major). Anything else throws `ValueError` with the reason.
  Devices that are not on the CPU become a `ForeignDevice` (host-accessible only for CUDA-host, ROCm-host and CUDA-managed).
* The DLPack device type of the `Device` class of another extension is declared with `buffers_register_device_type()`.

## C API

`php_buffers_api.h` (installed to `ext/buffers/`, `BUFFERS_API_VERSION` 3, `buffers_api_version()` at run time) is the
only thing another extension needs; it never sees the layout of buffers or tensors. The symbols are exported from
`buffers.so` (also in the release binaries, which hide everything else), so load order matters:
declare `ZEND_MOD_REQUIRED("buffers")`. `tests/api-consumer/` is a complete small extension that uses nothing else
and has phpt tests; read it as the example.

| Area | Functions |
|------|-----------|
| classes, dtypes | `buffers_get_class()`, `buffers_dtype_describe()`, `buffers_dtype_count()` (the dtypes of the loaded extension: check values against it, not against the compiled `BUFFERS_DT_COUNT`), `buffers_dtype_to_zval()`, `buffers_dtype_from_object()`, `buffers_dtype_from_dlpack()`, `buffers_max_ndim()` (no structure of the API has an array of `BUFFERS_MAX_NDIM`: an extension with fixed arrays of its own checks `ndim` against its own limit) |
| devices | `buffers_cpu_device()`, `buffers_file_device()`, `buffers_implement_device()` (declare `$name`, `$id`, `$hostAccessible` with three get hooks), `buffers_register_device_type()`, `buffers_device_dlpack()`, `buffers_register_transfer()`, `buffers_tensor_to()` |
| tensors | `buffers_is_tensor()`, `buffers_tensor_get_info()` (data pointer, dtype, ndim, pointers to the shape and the strides in elements, which belong to the tensor and stay valid while the caller holds it, byte offset, buffer object, writable; views may start at any byte offset and the pointer is handed over as it is: strides are in elements, so the tensor is aligned exactly when `data % size == 0`, which a consumer that needs alignment checks itself), `buffers_tensor_create()` (validated exactly like `view()`) |
| buffers | `buffers_is_buffer()`, `buffers_buffer_get_info()` (base, length, writable, host accessible, device), `buffers_buffer_create_foreign()` (memory + deleter + device), `buffers_buffer_create_owned()` (memory kept alive by a PHP object: holds a reference to the owner and releases it on free, no deleter), `buffers_buffer_file_origin()` (the path and offset of a mapped `Buffer::fromFile()` buffer: refer to the bytes instead of copying them), `buffers_hold()`, `buffers_release()` |
| shapes, tensors | `buffers_shape_create()`, `buffers_shape_dims()`; `buffers_get_class(BUFFERS_CE_TENSOR_AGGREGATE)` for classes that implement `TensorAggregate` (its three methods, like any interface) |
| elements | `buffers_convert()` (n elements between any two dtypes, strided, every format: integers wrap, floats round like `ml_dtypes`; for extensions that compute on elements) |
| properties | `buffers_declare_virtual_props()` (read-only virtual properties with get hooks, for any internal class) |
| DLPack | `buffers_dlpack_export()`, `buffers_dlpack_import()` (see above) |
| arithmetic | `buffers_dtype_promote()` (the result dtype of an operator, for implementations that must agree with ext-buffers, such as lazy graphs), `buffers_register_backend()` (arithmetic on another device, or faster arithmetic on the host, see below) |
| hooks | `buffers_buffer_set_ready_callback()`, `buffers_buffer_set_pending()`, `buffers_buffer_ensure_ready()` |

**Memory owned by a PHP object** (for example RubixML's Tensor-Ext keeps contiguous row-major float64 data in C): create
a buffer with `buffers_buffer_create_owned(data, length, writable, NULL, owner_object)` and a view with
`buffers_tensor_create(buffer, BUFFERS_DT_FLOAT64, 2, shape, NULL, 0)`; the owner stays alive as long as any view or
buffer does and is released afterwards (`tests/api-consumer/tests/owner.phpt`). Strides are fully supported by views
(pass them to `buffers_tensor_create()`); only the `TensorAggregate` interface omits them.

### Operators and other extensions

Tensor arithmetic is ext-buffers' own. An extension with lazy or
accelerated arithmetic gives its own classes a `do_operation` handler. PHP asks only the left operand's handler when it
has one, so a `Tensor` on the left hands the operation to the right operand's handler when that operand's class has one:
`$tensor + $expression` reaches the extension exactly like `$expression + $tensor`
(`tests/api-consumer/tests/operators_foreign.phpt`). `buffers_dtype_promote()` gives the result dtype of every operator,
and `tests/promotion.tsv` is the conformance table, so a lazy implementation can produce the same dtypes.

**Backends.** `buffers_register_backend()` (from `MINIT`, any number) registers a capability check and an implementation:
ext-buffers validates the operands and works out the result dtype and shape, then asks the backends, newest first, whether
they can compute the operation on these operands (their devices, dtypes, layouts), and falls back to its own C loops for
host memory. A backend must produce exactly the results the rules define (`+ - * /` and integer operations bit for bit,
`**` within its bound) and passes the same conformance tests; it may only be faster. Without a capable backend, operands
on another device are an `Error`.

### Readiness

A buffer produced asynchronously or lazily can carry a **readiness callback** and a pending flag
(`buffers_buffer_set_ready_callback()` while the caller holds the only reference, then
`buffers_buffer_set_pending()`). Element reads and writes, iteration, `Buffer::toString()`, `writeTo()`, `readFrom()` and DLPack export
run the callback first while the flag is set; it makes the contents valid and returns `SUCCESS`, or throws and
returns `FAILURE` (the buffer stays pending, the exception reaches the PHP caller). Making sub-views and reading
properties never wait. Buffers without a callback and host-accessible memory use the unchanged fast handlers: the
check is selected when a view is created, not on each access, so the 1-D and chained fast paths cost the same as
without the feature (see Performance).

## Verification

What has been run, on Linux x86-64 (Ubuntu host, Debian 12 and Alpine containers):

* `tests/` phpt suite (71 files), run twice: with the extension alone, and with the API consumer test extension loaded. On PHP 8.4
  and 8.5, Debian/glibc and Alpine/musl, NTS and ZTS (`tools/docker-test.sh`, 8 images): 45 + 64 pass, the rest skip (tests that
  need the consumer, 32-bit-only tests, the PHP 8.3 variants). On PHP 8.3 (Debian and Alpine NTS): 42 + 61 pass; the tests of
  property hooks skip and their 8.3 variants run. No compiler warnings with `-Wall -Wextra`.
* 32-bit PHP (`DOCKER_PLATFORM=linux/386`, Debian, 8.5 and 8.3): 36 + 53 and 33 + 50 pass; the tests that need 64-bit dtypes skip.
* The whole suite, both passes, under valgrind with `USE_ZEND_ALLOC=0` (`tools/valgrind.sh`, PHP 8.5 NTS): 44 + 63 pass, zero errors,
  zero leaks.
* Arithmetic: `arith_exact.phpt` compares every operator bit for bit with an independent oracle (PHP double and integer arithmetic,
  rounded once to the format) for `Float16`, `BFloat16`, `Float32`, `Float64` and 8/16/32-bit integers, on an AMD Ryzen 9 9950X3D
  (the AVX-512 build of the loops), an Intel Xeon E5-2697 v3 (AVX2) and Alpine/musl (the baseline build); `tests/promotion.tsv` lists
  the result dtype of every operand pair and operator, generated by a second implementation of the rules
  (`tools/promotion-table.php`) and checked against `buffers_dtype_promote()`.
* `tools/oracle/run.sh decode|encode|doubles|ints`, against ml_dtypes 0.5.4: every code of every format of at most 16 bits decodes identically;
  **every one of the 2^32 float32 inputs encodes identically for each of the 11 floating point formats of at most 8 bits** (float8_e3m4, e4m3,
  e4m3b11fnuz, e4m3fn, e4m3fnuz, e5m2, e5m2fnuz, e8m0fnu, float6_e2m3fn, float6_e3m2fn, float4_e2m1fn; for the float6/float4 formats NaN is
  checked against ext-buffers' own rule, zero of the same sign, and everything else against ml_dtypes); 20 million random doubles, exact midpoints
  and their neighbours per format (13 formats) encode identically; the packed integers (through the extension) decode every code identically to
  `int2`/`int4`/`uint2`/`uint4` and wrap writes of -1000..1000 like ml_dtypes' `astype`.
* `tools/oracle/run-fuzz.sh`: a NumPy differential fuzz (random dtype, bytes, shape, strides, offset; reads through chained offsets and writes
  compared with NumPy `as_strided` over the same bytes; the packed formats checked with NumPy bit operations against ml_dtypes' decode and encode;
  0-d views checked for geometry): 30000 views (seed 101), 1.26 million elements, 14348 write passes, no mismatch.
  `SCRIPT=view_fuzz.py tools/oracle/run-fuzz.sh`: hostile `view()` arguments (including `[]`, and the packed rules) against an exact big-integer
  model (any byte offset, strides of at least 0): 100000 calls (seed 102), 12183 accepted, 87817 rejected, no mismatch.
* Two processes sharing a `/dev/shm` mapping (`proc_open`), `createFile` + `flush` + reopen, and `writeTo`/`readFrom` over `php://memory`, a file and a
  socket pair (including short writes on a non-blocking socket) are phpt tests.
* Byte order: a build with `-DBUF_FORCE_SWAP` stores elements byte-swapped on a little-endian host, which is what a big-endian
  host does; every test that works with values passes in it (the tests that compare raw bytes with `pack()` fail by design).
* Not tested: macOS, Windows (`config.w32` and the `CreateFileMapping`/`MapViewOfFile`/`ReadFile`/`FlushViewOfFile` code), multi-threaded ZTS
  under load, big-endian machines.

Commands: `tools/test.sh [8.x]` builds out of tree and runs the suite (twice: with the extension alone, then with the API
consumer loaded); `tools/valgrind.sh [8.x]`; `tools/docker-test.sh [image ...]` (`DOCKER_PLATFORM=linux/386` for 32-bit);
`benchmarks/run.sh` runs the benchmarks; `tools/gen-arginfo.sh` regenerates `buffers_arginfo.h` from `buffers.stub.php`.
`PHP_BIN=/path/to/bin` selects another PHP installation for the local scripts (debug and ZTS builds).

## Licence

MIT (`LICENSE`); `dlpack.h` is Apache-2.0 (`LICENSE.dlpack`, `NOTICE`).
