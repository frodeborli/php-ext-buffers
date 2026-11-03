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

    /** Width of one element in bits: 4, 6, 8 ... 128. */
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
 * The shape of a tensor: a list of dimensions, each an int >= 0 or a 0-d TensorInterface (a symbolic
 * dimension, for extensions that build deferred computations). Immutable. Negative indices count from
 * the end. The shape of a Tensor is always concrete.
 *
 * @property-read bool $concrete True if every dimension is an int.
 * @property-read int|TensorInterface $size The number of elements: the product of the dimensions (1 for the empty
 *     shape). Reading it throws a ValueError if the product exceeds PHP_INT_MAX and an Error for a shape that is not
 *     concrete: computing a symbolic product needs an extension that builds deferred computations, and none is loaded
 *     in this version.
 *
 * @implements \ArrayAccess<int, int|TensorInterface>
 * @implements \IteratorAggregate<int, int|TensorInterface>
 */
final class Shape implements \ArrayAccess, \Countable, \IteratorAggregate, \JsonSerializable
{
    /** $dims is a list of non-negative ints and 0-d Tensors. */
    public function __construct(array $dims) {}
    /** @return list<int|TensorInterface> */
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
    /** The dimensions as a list, for json_encode(). Throws Error if a dimension is symbolic (not in this version). */
    public function jsonSerialize(): array {}
}

/**
 * Anything with a dtype and a shape. Implemented by Tensor, and by classes of other extensions
 * (for example a deferred tensor). Only internal classes can implement it: the interface cannot be
 * implemented in PHP code. The properties are read-only and computed when they are read.
 *
 * (The properties are declared by the extension at startup: stubs cannot express them.)
 */
interface TensorInterface
{
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
    /** A writable host Buffer holding a copy of $data. */
    public static function fromString(string $data): Buffer {}
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
    /** Whether the bytes can be read and written from PHP: authoritative for this buffer (a mapped file is, its device is not). */
    public function isHostAccessible(): bool {}
    /**
     * $shape: null is 1-D over the rest of the buffer, an int is 1-D of that length, an array has 0 to 8
     * dimensions ([] makes a 0-d view of one element). $strides are in elements, each at least 1 (null is
     * C-contiguous). $offset is in bytes. Packed sub-byte dtypes have extra rules, see the README.
     */
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): Tensor {}
    /** A copy of the bytes. Throws Error unless the buffer is host accessible. */
    public function toString(): string {}
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
final class Tensor implements TensorInterface, \Countable, \ArrayAccess, \IteratorAggregate, \JsonSerializable
{
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
     * The whole contents as nested arrays (a 0-d view: the scalar; complex elements: [re, im]), for json_encode().
     * json_encode() fails for NaN and INF, as for any PHP value. Throws Error for memory that is not host accessible.
     */
    public function jsonSerialize(): mixed {}
}
