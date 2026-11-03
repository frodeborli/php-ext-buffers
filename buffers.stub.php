<?php

/** @generate-function-entries */

namespace Buffers;

/**
 * Element types of a buffer. Case order is stable. See the README for the table of formats.
 */
enum DType: string
{
    case Bool = 'bool';
    case Int8 = 'int8';
    case Int16 = 'int16';
    case Int32 = 'int32';
    case Int64 = 'int64';
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
    /** Bytes one element occupies, or null for the packed sub-byte formats (Float6*, Float4*). */
    public function byteSize(): ?int {}
    /** False for the packed sub-byte formats: their elements are not a whole number of bytes. */
    public function isByteAligned(): bool {}
    public function isInteger(): bool {}
    /** True for all Float* cases and BFloat16. */
    public function isFloatingPoint(): bool {}
    public function isComplex(): bool {}
    /** @return array{code: int, bits: int, lanes: int} The DLPack type code and width. */
    public function dlpack(): array {}
}

/**
 * Where a buffer's memory lives. Implemented by CpuDevice and by the devices of imported DLPack
 * tensors; other extensions may implement it from C.
 */
interface Device
{
    /** A human readable name such as "cpu" or "cuda:0". */
    public function name(): string;
    /** The DLPack device type (DLDeviceType): 1 for the CPU. */
    public function dlpackType(): int;
    /** The DLPack device id. */
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
    public function dlpackType(): int {}
    public function id(): int {}
    public function isHostAccessible(): bool {}
}

/**
 * The device of a DLPack tensor that is not on the CPU. Created by BufferView::fromDLPack(); cannot
 * be instantiated.
 * @internal
 */
final class ForeignDevice implements Device
{
    public function name(): string {}
    public function dlpackType(): int {}
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
final class Shape implements \ArrayAccess, \Countable, \IteratorAggregate
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

/** Memory that views are made over. */
interface BufferInterface
{
    public function byteLength(): int;
    public function isWritable(): bool;
    public function device(): Device;
    /**
     * $shape: null is 1-D over the rest of the buffer, an int is 1-D of that length, an array has 0 to 8
     * dimensions ([] makes a 0-d view of one element). $strides are in elements, each at least 1 (null is
     * C-contiguous). $offset is in bytes. Packed sub-byte dtypes have extra rules, see the README.
     */
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): BufferView;
}

/** Zero-filled, 64-byte aligned memory that counts toward memory_limit. */
final class ArrayBuffer implements BufferInterface
{
    public function __construct(int $byteLength) {}
    public function byteLength(): int {}
    public function isWritable(): bool {}
    public function device(): Device {}
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): BufferView {}
    public static function fromString(string $data): ArrayBuffer {}
    /** A copy of the bytes. */
    public function toString(): string {}
}

/**
 * Memory owned by someone else: a foreign DLPack tensor, or memory another extension keeps alive.
 * Created by BufferView::fromDLPack() and by other extensions through the C API; cannot be
 * instantiated. May live on a device other than the CPU.
 * @internal
 */
final class DLPackBuffer implements BufferInterface
{
    public function byteLength(): int {}
    public function isWritable(): bool {}
    public function device(): Device {}
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): BufferView {}
}

/** An exported DLPack tensor; opaque and consumed once by BufferView::fromDLPack(). */
final class DLPackCapsule
{
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
 * @property-read BufferInterface $buffer
 *
 * @implements \ArrayAccess<int, mixed>
 * @implements \IteratorAggregate<int, mixed>
 */
final class BufferView implements Tensor, \Countable, \ArrayAccess, \IteratorAggregate
{
    /** The length of the first axis. A 0-d view has no axes: Error. */
    public function count(): int {}
    public function offsetExists(mixed $offset): bool {}
    public function offsetGet(mixed $offset): mixed {}
    public function offsetSet(mixed $offset, mixed $value): void {}
    public function offsetUnset(mixed $offset): void {}
    public function getIterator(): \Iterator {}
    public function toDLPack(): DLPackCapsule {}
    public static function fromDLPack(DLPackCapsule $capsule): BufferView {}
}
