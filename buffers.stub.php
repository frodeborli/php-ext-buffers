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
    public function isInteger(): bool {}
    /** True for all Float* cases and BFloat16. */
    public function isFloatingPoint(): bool {}
    public function isComplex(): bool {}
}

/**
 * Where a buffer's memory lives, identified by name and id. Implemented by CpuDevice, FileDevice and by the
 * devices of tensors imported through the C API's DLPack functions; other extensions implement it from C.
 */
interface Device
{
    /** A human readable name such as "cpu" or "cuda:0". */
    public function name(): string;
    /** The device number: 0 for the CPU and for files. */
    public function id(): int;
    /** Whether element access from PHP is possible: true only for memory the CPU can address. */
    public function isHostAccessible(): bool;
}

/** The host CPU. */
final class CpuDevice implements Device
{
    private function __construct() {}
    public static function get(): CpuDevice {}
    public function name(): string {}
    public function id(): int {}
    public function isHostAccessible(): bool {}
}

/** Files: the device of Buffer::fromFile() buffers. Storage; its buffers are host accessible only when mapped. */
final class FileDevice implements Device
{
    private function __construct() {}
    public static function get(): FileDevice {}
    public function name(): string {}
    public function id(): int {}
    public function isHostAccessible(): bool {}
}

/**
 * The device of a DLPack tensor that is not on the CPU, as imported through the C API (names such as "cuda:0").
 * Cannot be instantiated.
 * @internal
 */
final class ForeignDevice implements Device
{
    public function name(): string {}
    public function id(): int {}
    public function isHostAccessible(): bool {}
}

/**
 * The shape of a tensor: a list of dimensions, each an int >= 0 or a 0-d Tensor (a symbolic
 * dimension, for extensions that build deferred computations). Immutable. Negative indices count from
 * the end. The shape of a BufferView is always concrete.
 *
 * @implements \ArrayAccess<int, int|Tensor>
 * @implements \IteratorAggregate<int, int|Tensor>
 */
final class Shape implements \ArrayAccess, \Countable, \IteratorAggregate, \JsonSerializable
{
    /** $dims is a list of non-negative ints and 0-d Tensors. */
    public function __construct(array $dims) {}
    /** True if every dimension is an int. */
    public function isConcrete(): bool {}
    /**
     * The number of elements: the product of the dimensions (1 for the empty shape). Throws a ValueError
     * if it exceeds PHP_INT_MAX and an Error for a shape that is not concrete: computing a symbolic
     * product needs an extension that builds deferred computations, and none is loaded in this version.
     */
    public function size(): int|Tensor {}
    /** @return list<int|Tensor> */
    public function toArray(): array {}
    public function count(): int {}
    public function offsetExists(mixed $offset): bool {}
    public function offsetGet(mixed $offset): mixed {}
    /** Always throws: a Shape is immutable. */
    public function offsetSet(mixed $offset, mixed $value): void {}
    /** Always throws: a Shape is immutable. */
    public function offsetUnset(mixed $offset): void {}
    public function getIterator(): \Iterator {}
    /** The dimensions as a list, for json_encode(). Throws Error if a dimension is symbolic (not in this version). */
    public function jsonSerialize(): array {}
}

/**
 * Anything with a dtype and a shape. Implemented by BufferView, and by classes of other extensions
 * (for example a deferred tensor). Only internal classes can implement it: the interface cannot be
 * implemented in PHP code. The properties are read-only and computed when they are read.
 *
 * (The properties are declared by the extension at startup: stubs cannot express them.)
 */
interface Tensor
{
}

/**
 * Bytes on some device: a pointer, a length, a device, a writable flag, an owner (ext-buffers' own allocation, a C
 * deleter, a PHP object that is kept alive, or a file mapping) and optionally a readiness callback. Views are made
 * over it. Buffers come from the constructor, fromString(), fromFile() and other extensions through the
 * C API (foreign memory, memory owned by a PHP object, DLPack import).
 */
final class Buffer
{
    /** Zero-filled, 64-byte aligned host memory that counts toward memory_limit. */
    public function __construct(int $byteLength) {}
    public static function fromString(string $data): Buffer {}
    /**
     * A read-only buffer on FileDevice over bytes $offset .. $offset + $length of a file ($length null: to the end).
     * With $map true the range is memory-mapped: the buffer is host accessible and pages load on first touch (truncating
     * the file from another process while it is mapped crashes the process with SIGBUS on Linux and macOS). With $map
     * false, or when the range cannot be mapped (larger than the address space of 32-bit PHP, for example), the buffer
     * is storage only: isHostAccessible() is false and BufferView::to(CpuDevice::get()) reads it. The range is checked
     * against the file size when the file is opened.
     */
    public static function fromFile(string $path, int $offset = 0, ?int $length = null, bool $map = true): Buffer {}
    public function byteLength(): int {}
    public function isWritable(): bool {}
    public function device(): Device {}
    /** Whether the bytes can be read and written from PHP: authoritative for this buffer (a mapped file is, its device is not). */
    public function isHostAccessible(): bool {}
    /**
     * $shape: null is 1-D over the rest of the buffer, an int is 1-D of that length, an array has 0 to 8
     * dimensions ([] makes a 0-d view of one element). $strides are in elements, each at least 1 (null is
     * C-contiguous). $offset is in bytes. Packed sub-byte dtypes have extra rules, see the README.
     */
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): BufferView {}
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
final class BufferView implements Tensor, \Countable, \ArrayAccess, \IteratorAggregate, \JsonSerializable
{
    /** The length of the first axis. A 0-d view has no axes: Error. */
    public function count(): int {}
    public function offsetExists(mixed $offset): bool {}
    public function offsetGet(mixed $offset): mixed {}
    public function offsetSet(mixed $offset, mixed $value): void {}
    public function offsetUnset(mixed $offset): void {}
    public function getIterator(): \Iterator {}
    /**
     * This view if its buffer is already on $device; otherwise a C-contiguous copy (packed sub-byte dtypes: rows padded to
     * whole bytes) in a new Buffer on $device. The core copies host-accessible buffers to CpuDevice and reads unmapped file
     * buffers; any other transfer needs an extension that registered it (buffers_register_transfer()), else Error.
     */
    public function to(Device $device): BufferView {}
    /**
     * The whole contents as nested arrays (a 0-d view: the scalar; complex elements: [re, im]), for json_encode().
     * json_encode() fails for NaN and INF, as for any PHP value. Throws Error for memory that is not host accessible.
     */
    public function jsonSerialize(): mixed {}
}
