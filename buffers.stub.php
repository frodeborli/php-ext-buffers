<?php

/** @generate-function-entries */

namespace Buffers;

/**
 * Element types of a buffer. Case order is stable. See the README for the table of formats.
 */
enum DType: string
{
    case Bool = 'bool';
    case Int2 = 'int2';
    case Int4 = 'int4';
    case Int8 = 'int8';
    case Int16 = 'int16';
    case Int32 = 'int32';
    case Int64 = 'int64';
    case UInt2 = 'uint2';
    case UInt4 = 'uint4';
    case UInt8 = 'uint8';
    case UInt16 = 'uint16';
    case UInt32 = 'uint32';
    case UInt64 = 'uint64';
    case Float16 = 'float16';
    case Float32 = 'float32';
    case Float64 = 'float64';
    case BFloat16 = 'bfloat16';
    case Float8E3M4 = 'float8_e3m4';
    case Float8E4M3 = 'float8_e4m3';
    case Float8E4M3B11FNUZ = 'float8_e4m3b11fnuz';
    case Float8E4M3FN = 'float8_e4m3fn';
    case Float8E4M3FNUZ = 'float8_e4m3fnuz';
    case Float8E5M2 = 'float8_e5m2';
    case Float8E5M2FNUZ = 'float8_e5m2fnuz';
    case Float8E8M0FNU = 'float8_e8m0fnu';
    case Float6E2M3FN = 'float6_e2m3fn';
    case Float6E3M2FN = 'float6_e3m2fn';
    case Float4E2M1FN = 'float4_e2m1fn';
    case Complex32 = 'complex32';
    case Complex64 = 'complex64';
    case Complex128 = 'complex128';
    case BComplex32 = 'bcomplex32';

    /** Width of one element in bits: 2, 4, 6, 8 ... 128. */
    public function bits(): int {}
    /** Bytes one element occupies, or null for the packed sub-byte formats (Int2, Int4, UInt2, UInt4, Float6*, Float4*). */
    public function byteSize(): ?int {}
    /** False for the packed sub-byte formats: their elements are not a whole number of bytes. */
    public function isByteAligned(): bool {}
    /** True for Int2, Int4, Int8..Int64 and UInt2..UInt64 (the packed ones included). */
    public function isInteger(): bool {}
    /** True for all Float* cases and BFloat16. */
    public function isFloatingPoint(): bool {}
    /** True for Complex32, Complex64, Complex128 and BComplex32: elements read and write as [re, im]. */
    public function isComplex(): bool {}
}

/**
 * Where a buffer's memory lives, identified by name and id. Implemented by CpuDevice, FileDevice and by the
 * devices of tensors imported through the C API's DLPack functions; other extensions implement it from C
 * (buffers_implement_device()). The facts are read-only properties (declared by the extension at startup: stubs cannot
 * express them):
 *
 * @property-read string $name A human readable name: "cpu", "file", "cuda:0".
 * @property-read int $id The device number: 0 for the CPU and for files.
 * @property-read bool $hostAccessible Whether PHP can read and write the memory of the device in general: true only
 *     for memory the CPU can address. Buffer::isHostAccessible() is authoritative for a particular buffer (a mapped
 *     file is host accessible, its device is not).
 */
interface Device
{
}

/** The host CPU. */
final class CpuDevice implements Device
{
    private function __construct() {}
    /** The CPU, one object per request. */
    public static function get(): CpuDevice {}
}

/** Files: the device of Buffer::fromFile() buffers. Storage; its buffers are host accessible only when mapped. */
final class FileDevice implements Device
{
    private function __construct() {}
    /** The device of file buffers, one object per request. */
    public static function get(): FileDevice {}
}

/**
 * The device of a DLPack tensor that is not on the CPU, as imported through the C API (names such as "cuda:0").
 * Cannot be instantiated.
 * @internal
 */
final class ForeignDevice implements Device
{
}

/**
 * The shape of a tensor: a list of dimensions, each an int >= 0 or a 0-d TensorAggregate (a symbolic
 * dimension, for extensions that build deferred computations; 0-d: its getShape() has no dimensions). Immutable. Negative indices count from
 * the end. The shape of a Tensor is always concrete.
 *
 * @property-read bool $concrete True if every dimension is an int.
 * @property-read int|TensorAggregate $size The number of elements: the product of the dimensions (1 for the empty
 *     shape). Reading it throws a ValueError if the product exceeds PHP_INT_MAX and an Error for a shape with symbolic
 *     dimensions, which has no integer size.
 *
 * @implements \ArrayAccess<int, int|TensorAggregate>
 * @implements \IteratorAggregate<int, int|TensorAggregate>
 */
final class Shape implements \ArrayAccess, \Countable, \IteratorAggregate, \JsonSerializable
{
    /** $dims is a list of non-negative ints and 0-d TensorAggregates. */
    public function __construct(array $dims) {}
    /** @return list<int|TensorAggregate> */
    public function toArray(): array {}
    /** The number of dimensions. */
    public function count(): int {}
    /** Whether $offset (an int, negative counts from the end) is a valid index. */
    public function offsetExists(mixed $offset): bool {}
    /** The dimension at $offset (negative counts from the end); ValueError if out of range, TypeError if not an int. */
    public function offsetGet(mixed $offset): mixed {}
    /** Always throws: a Shape is immutable. */
    public function offsetSet(mixed $offset, mixed $value): void {}
    /** Always throws: a Shape is immutable. */
    public function offsetUnset(mixed $offset): void {}
    /** Iterates the dimensions. */
    public function getIterator(): \Iterator {}
    /** The dimensions as a list, for json_encode(). Throws Error if a dimension is symbolic. */
    public function jsonSerialize(): array {}
}

/**
 * Anything that is a tensor or can produce one: a Tensor (which returns itself), a lazy expression of another extension,
 * or a PHP class (a sensor, a column of a database result, a file format). Arithmetic accepts an operand of a class that
 * implements it and has no arithmetic of its own by calling getTensor().
 */
interface TensorAggregate
{
    /** The tensor; an implementation may compute or load it. */
    public function getTensor(): Tensor;
    /** The dtype of getTensor()'s result without producing it; null if it is not known yet. */
    public function getDType(): ?DType;
    /** The shape of getTensor()'s result without producing it. */
    public function getShape(): Shape;
}

/**
 * Bytes on some device: a pointer, a length, a device, a writable flag, an owner (ext-buffers' own allocation, a C
 * deleter, a PHP object that is kept alive, or a file mapping) and optionally a readiness callback. Views are made
 * over it. Buffers come from the constructor, fromString(), fromFile() and other extensions through the
 * C API (foreign memory, memory owned by a PHP object, DLPack import).
 *
 * @property-read int $byteLength
 * @property-read Device $device
 */
final class Buffer
{
    /** Zero-filled, 64-byte aligned host memory that counts toward memory_limit. */
    public function __construct(int $byteLength) {}
    /** A buffer holding a copy of $data. With $writable = false it is read-only, like a mapped file: backends that bake constants into a plan (onnxruntime) do so for read-only tensors only. */
    public static function fromString(string $data, bool $writable = true): Buffer {}
    /**
     * A buffer on FileDevice over bytes $offset .. $offset + $length of a file ($length null: to the end), read-only unless
     * $writable. A writable buffer must be mapped; it is a shared mapping: writes go to the file and other processes that map
     * it see them at once.
     * With $map true the range is memory-mapped: the buffer is host accessible and pages load on first touch (truncating
     * the file from another process while it is mapped crashes the process with SIGBUS on Linux and macOS). With $map
     * false, or when the range cannot be mapped (larger than the address space of 32-bit PHP, for example), the buffer
     * is storage only: isHostAccessible() is false and Tensor::to(CpuDevice::get()) reads it. The range is checked
     * against the file size when the file is opened.
     */
    public static function fromFile(string $path, int $offset = 0, ?int $length = null, bool $map = true, bool $writable = false): Buffer {}
    /**
     * Creates the file (or truncates it) at $byteLength bytes of zeros and returns a writable, mapped, host-accessible buffer on
     * FileDevice. Writes through tensors go to the file (call flush() to push them to disk).
     */
    public static function createFile(string $path, int $byteLength): Buffer {}
    /**
     * Pushes the changes of a writable file mapping to disk (msync / FlushViewOfFile). Does nothing for other buffers.
     */
    public function flush(): void {}
    /**
     * Writes bytes $offset .. $offset + $length ($length null: to the end) straight from the buffer's memory to a PHP stream,
     * looping on short writes, and returns the number of bytes written. The memory must be host accessible.
     * @param resource $stream
     */
    public function writeTo(mixed $stream, int $offset = 0, ?int $length = null): int {}
    /**
     * Reads from a PHP stream straight into the buffer at $offset, up to $length bytes ($length null: to the end of the buffer),
     * stopping at the end of the stream, and returns the number of bytes read. The memory must be host accessible and writable.
     * @param resource $stream
     */
    public function readFrom(mixed $stream, int $offset = 0, ?int $length = null): int {}
    /** False for read-only file buffers, imported read-only tensors and the like: writes through its tensors throw Error. */
    public function isWritable(): bool {}
    /** A read-only buffer over the same memory (this one if it is read-only already): what backends bake into a plan as a constant. The new buffer keeps this one alive; writes through this one are seen by it. */
    public function readOnly(): Buffer {}
    /** Whether the bytes can be read and written from PHP: authoritative for this buffer (a mapped file is, its device is not). */
    public function isHostAccessible(): bool {}
    /**
     * $shape: null is 1-D over the rest of the buffer, an int is 1-D of that length, an array has 0 to 8
     * dimensions ([] makes a 0-d view of one element). $strides are in elements, each at least 0 (null is
     * C-contiguous; a stride of 0 on an axis longer than 1 makes the view read-only). $offset is in bytes. Packed sub-byte dtypes have extra rules, see the README.
     */
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): Tensor {}
    /** A copy of the bytes. Throws Error unless the buffer is host accessible. */
    public function toString(): string {}
}

/**
 * Single values at any byte offset of a Buffer, like JavaScript's DataView over an ArrayBuffer: get<DType>($offset) and
 * set<DType>($offset, $value) for every whole-byte dtype except Bool and the complex ones, named after the DType cases. $offset is in
 * bytes from the start of the view, bounds-checked against its length (ValueError). Multi-byte values are **little-endian by default**
 * (JavaScript defaults to big-endian); pass $bigEndian = true for big-endian. Integers wrap to the width when set; UInt64 values above
 * PHP_INT_MAX read as negative ints (like unpack()). Floats round as Tensor element writes do. Setting on a read-only buffer throws Error,
 * as does any access to memory that is not host accessible. Int64, UInt64 and UInt32 throw Error on 32-bit PHP.
 *
 * @property-read Buffer $buffer
 * @property-read int $byteOffset where the view starts in the buffer
 * @property-read int $byteLength
 *
 * Byte-indexed accessors: `$u32 = $d->uint32;` then `$u32[$byteOffset]` reads and `$u32[$byteOffset] = $v` writes the little-endian
 * uint32 at that byte offset of the view. Index syntax costs no method call, so the accessors are the fast path for hot loops.
 * Each property returns the same accessor object every time.
 * @property-read DataViewAccessor $int8
 * @property-read DataViewAccessor $uint8
 * @property-read DataViewAccessor $int16
 * @property-read DataViewAccessor $uint16
 * @property-read DataViewAccessor $int32
 * @property-read DataViewAccessor $uint32
 * @property-read DataViewAccessor $int64
 * @property-read DataViewAccessor $uint64
 * @property-read DataViewAccessor $float16
 * @property-read DataViewAccessor $bfloat16
 * @property-read DataViewAccessor $float32
 * @property-read DataViewAccessor $float64
 */
final class DataView
{
    /** A window of $length bytes ($length null: to the end) from $offset of $buffer. ValueError if it does not fit. */
    public function __construct(Buffer $buffer, int $offset = 0, ?int $length = null) {}
    public function getInt8(int $offset): int {}
    public function setInt8(int $offset, int $value): void {}
    public function getUInt8(int $offset): int {}
    public function setUInt8(int $offset, int $value): void {}
    public function getInt16(int $offset, bool $bigEndian = false): int {}
    public function setInt16(int $offset, int $value, bool $bigEndian = false): void {}
    public function getInt32(int $offset, bool $bigEndian = false): int {}
    public function setInt32(int $offset, int $value, bool $bigEndian = false): void {}
    public function getInt64(int $offset, bool $bigEndian = false): int {}
    public function setInt64(int $offset, int $value, bool $bigEndian = false): void {}
    public function getUInt16(int $offset, bool $bigEndian = false): int {}
    public function setUInt16(int $offset, int $value, bool $bigEndian = false): void {}
    public function getUInt32(int $offset, bool $bigEndian = false): int {}
    public function setUInt32(int $offset, int $value, bool $bigEndian = false): void {}
    public function getUInt64(int $offset, bool $bigEndian = false): int {}
    public function setUInt64(int $offset, int $value, bool $bigEndian = false): void {}
    public function getFloat16(int $offset, bool $bigEndian = false): float {}
    public function setFloat16(int $offset, float $value, bool $bigEndian = false): void {}
    public function getBFloat16(int $offset, bool $bigEndian = false): float {}
    public function setBFloat16(int $offset, float $value, bool $bigEndian = false): void {}
    public function getFloat32(int $offset, bool $bigEndian = false): float {}
    public function setFloat32(int $offset, float $value, bool $bigEndian = false): void {}
    public function getFloat64(int $offset, bool $bigEndian = false): float {}
    public function setFloat64(int $offset, float $value, bool $bigEndian = false): void {}
    public function getFloat8E3M4(int $offset): float {}
    public function setFloat8E3M4(int $offset, float $value): void {}
    public function getFloat8E4M3(int $offset): float {}
    public function setFloat8E4M3(int $offset, float $value): void {}
    public function getFloat8E4M3B11FNUZ(int $offset): float {}
    public function setFloat8E4M3B11FNUZ(int $offset, float $value): void {}
    public function getFloat8E4M3FN(int $offset): float {}
    public function setFloat8E4M3FN(int $offset, float $value): void {}
    public function getFloat8E4M3FNUZ(int $offset): float {}
    public function setFloat8E4M3FNUZ(int $offset, float $value): void {}
    public function getFloat8E5M2(int $offset): float {}
    public function setFloat8E5M2(int $offset, float $value): void {}
    public function getFloat8E5M2FNUZ(int $offset): float {}
    public function setFloat8E5M2FNUZ(int $offset, float $value): void {}
    public function getFloat8E8M0FNU(int $offset): float {}
    public function setFloat8E8M0FNU(int $offset, float $value): void {}
    /** A copy of bytes $offset .. $offset + $length of the view, as a string. ValueError unless the range lies inside the view. */
    public function getBytes(int $offset, int $length): string {}
    /** Writes all of $bytes at $offset of the view. ValueError unless they fit: nothing is truncated. */
    public function setBytes(int $offset, string $bytes): void {}
    /** Sets bytes $offset .. $offset + $length of the view to $byte (wrapped to 0..255, like setUInt8()). */
    public function fill(int $offset, int $length, int $byte): void {}
    /** Copies $length bytes from $source to $target, both offsets in the view. The ranges may overlap (memmove). */
    public function copyBytes(int $target, int $source, int $length): void {}
}

/**
 * Values of one type at any byte offset of a DataView, with index syntax: `$a[$byteOffset]`. The index is a byte offset into the
 * DataView, not an element number, so neighbouring indexes overlap: `$d->uint32[1]` is bytes 1..4. Valid indexes are
 * 0 .. byteLength - size; any other int throws ValueError, a non-int TypeError (`isset()` and `??` just report it missing).
 * Multi-byte values are little-endian. Writes convert like Tensor element writes: integers wrap to the width, floats round, and
 * UInt64 values above PHP_INT_MAX read as negative ints. Writing to a read-only buffer throws Error. Obtained from a DataView
 * property ($d->uint32 ...); cannot be instantiated.
 *
 * @property-read DType $dtype
 *
 * @implements \ArrayAccess<int, int|float>
 */
final class DataViewAccessor implements \ArrayAccess
{
    /** Whether $offset is a valid byte offset for a value of this type. */
    public function offsetExists(mixed $offset): bool {}
    /** The value at byte $offset. */
    public function offsetGet(mixed $offset): int|float {}
    /** Writes the value at byte $offset. */
    public function offsetSet(mixed $offset, mixed $value): void {}
    /** Always throws: bytes cannot be removed. */
    public function offsetUnset(mixed $offset): void {}
}

/**
 * An n-dimensional window onto a buffer: $v[$i][$j] chains one int offset per axis; partial chains
 * are sub-views of the same memory. Read-only properties (computed when read; reading them
 * allocates only what the value needs, never anything on the access path):
 *
 * @property-read DType $dtype
 * @property-read Shape $shape
 * @property-read int $ndim
 * @property-read list<int> $strides in elements; 1 for the last axis of a packed dtype
 * @property-read int $offset bytes from the start of the buffer to element 0
 * @property-read Buffer $buffer
 * @property mixed $value the single element of a 0-d view, readable and writable; Error on views with axes
 *
 * @implements \ArrayAccess<int, mixed>
 * @implements \IteratorAggregate<int, mixed>
 */
final class Tensor implements TensorAggregate, \Countable, \ArrayAccess, \IteratorAggregate, \JsonSerializable
{
    /** The most axes a tensor can have. (Registered by the extension at startup.) */
    public const int MAX_NDIM = 8;
    /** Itself. */
    public function getTensor(): Tensor {}
    /** The same as $dtype. */
    public function getDType(): DType {}
    /** The same as $shape. */
    public function getShape(): Shape {}
    /**
     * A view of every $step-th element of $axis from $start: $length of them, or as many as fit (null). $start is between 0 and
     * the length of the axis, $step at least 1. A packed dtype's last axis takes only step 1 and a start on a whole byte.
     */
    public function slice(int $axis, int $start, ?int $length = null, int $step = 1): Tensor {}
    /**
     * A view with $axis1 and $axis2 swapped; by default the last two (a matrix transpose). This is PyTorch's transpose; NumPy's
     * transpose() reverses all axes, which is permute() with the reversed list here.
     */
    public function transpose(int $axis1 = -2, int $axis2 = -1): Tensor {}
    /** A view with the axes in another order: axis i of the result is axis $axes[i] of this tensor. */
    public function permute(array $axes): Tensor {}
    /**
     * A view with another shape of the same number of elements (one dimension may be -1: inferred). ValueError when the
     * strides do not allow a view; reshape a copy() then. A packed dtype keeps its last axis.
     */
    public function reshape(array $shape): Tensor {}
    /**
     * A view without $axis, which must have length 1. The axis is required: removing every axis of length 1 would also
     * remove a batch axis that happens to have length 1.
     */
    public function squeeze(int $axis): Tensor {}
    /** A view with a new axis of length 1 at position $axis (0 to ndim; negative counts from the end). */
    public function unsqueeze(int $axis): Tensor {}
    /**
     * A read-only view with $shape: axes are aligned from the right, and an axis of length 1 (or a missing one) repeats its
     * element with a stride of 0.
     */
    public function broadcastTo(array $shape): Tensor {}
    /**
     * The elements, byte for byte: a new C-contiguous host tensor, or written into $into (a writable tensor of the same dtype
     * and shape), which is returned. Overlapping memory is handled.
     */
    public function copy(?Tensor $into = null): Tensor {}
    /** The length of the first axis. A 0-d view has no axes: Error. */
    public function count(): int {}
    /** Whether $offset is a valid index of the first axis. */
    public function offsetExists(mixed $offset): bool {}
    /** A sub-tensor (a view of the same memory) or, on the last axis, the element. */
    public function offsetGet(mixed $offset): mixed {}
    /** Writes one element: every axis must be indexed. Error on read-only buffers. */
    public function offsetSet(mixed $offset, mixed $value): void {}
    /** Always throws: elements cannot be removed. */
    public function offsetUnset(mixed $offset): void {}
    /** Iterates the first axis (sub-tensors, or elements of a 1-D tensor). */
    public function getIterator(): \Iterator {}
    /**
     * This view if its buffer is already on $device; otherwise a C-contiguous copy (packed sub-byte dtypes: rows padded to
     * whole bytes) in a new Buffer on $device. The core copies host-accessible buffers to CpuDevice and reads unmapped file
     * buffers; any other transfer needs an extension that registered it (buffers_register_transfer()), else Error.
     */
    public function to(Device $device): Tensor {}
    /**
     * The elements in another dtype (by the conversion rules of the README, "Conversion") and, with $axes, in another order of the axes (a permutation: axis i of the result is axis $axes[i] of this view), as a new
     * C-contiguous host tensor. One streamed pass: nothing but the result is allocated, so a mapped checkpoint can be converted (and transposed) without a copy of it in between. Packed dtypes: Error.
     */
    public function convert(DType $dtype, ?array $axes = null): Tensor {}
    /**
     * The whole contents as nested arrays (a 0-d view: the scalar; complex elements: [re, im]), for json_encode().
     * json_encode() fails for NaN and INF, as for any PHP value. Throws Error for memory that is not host accessible.
     */
    public function jsonSerialize(): mixed {}
    /**
     * A tensor from nested arrays; the shape is inferred from the first path down the nesting and every other path must match it
     * (ValueError naming the path, e.g. "[2][1]", for a ragged array). Without $dtype: int gives Int64 (Int32 on 32-bit PHP), float
     * Float64, bool Bool, a mix of int and float Float64; a mix with bool needs a $dtype; an empty array gives Float64.
     * With a $dtype: values must fit it (ValueError for an integer dtype, no wrapping; integral floats are accepted for integer
     * dtypes; Bool takes only bool). Packed dtypes work; complex dtypes are not supported. An int, float or bool gives a 0-d tensor.
     * An empty array has a shape ending in 0 (`[]` is [0], `[[], []]` is [2, 0]). The tensor is made in CPU memory and then moved with to($device).
     */
    public static function fromArray(array|int|float|bool $data, ?DType $dtype = null, ?Device $device = null): Tensor {}
    /** The same as jsonSerialize(): nested arrays of int, float or bool (complex elements as [re, im]); a 0-d view gives the scalar. */
    public function toArray(): mixed {}
#if defined(HAVE_BUFFERS_GD)
    /**
     * A UInt8 tensor of shape [height, width, 4] (R, G, B, A) from a GD image (true color or palette). GD's alpha a (0 opaque ... 127
     * transparent) becomes (127 - a) * 255 / 127 rounded to nearest: 0 -> 255, 127 -> 0. Needs ext/gd, built against the same gd.h version as
     * this extension (Error naming both versions otherwise).
     * With $into (a writable UInt8 tensor of shape [height, width, 4], contiguous or strided, in host memory) nothing is allocated: the pixels are
     * written there and $into is returned. ValueError, naming the expected shape, for another dtype or shape.
     */
    public static function fromGdImage(\GdImage $image, ?Tensor $into = null): Tensor {}
    /**
     * A true color GdImage from a UInt8 tensor of shape [height, width, 3] (RGB), [height, width, 4] (RGBA) or [height, width]
     * (grey). Alpha A (255 opaque) becomes 127 - round(A * 127 / 255), the inverse of fromGdImage(); an RGBA image has
     * imagesavealpha() on. Any other dtype or shape, or an empty axis, is an Error. Needs ext/gd.
     */
    public function toGdImage(): \GdImage {}
#endif
}
