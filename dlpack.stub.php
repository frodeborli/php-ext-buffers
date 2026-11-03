<?php

/** @generate-function-entries */

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

    public function bits(): int {}
    public function byteSize(): ?int {}
    public function isByteAligned(): bool {}
    public function isInteger(): bool {}
    public function isFloatingPoint(): bool {}
    public function isComplex(): bool {}
    /** @return array{code: int, bits: int, lanes: int} */
    public function dlpack(): array {}
}

interface BufferInterface
{
    public function byteLength(): int;
    public function isWritable(): bool;
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): TypedView;
}

final class ArrayBuffer implements BufferInterface
{
    public function __construct(int $byteLength) {}
    public function byteLength(): int {}
    public function isWritable(): bool {}
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): TypedView {}
    public static function fromString(string $data): ArrayBuffer {}
    public function toString(): string {}
}

/** @internal Wraps memory owned by a foreign DLPack tensor; created by TypedView::fromDLPack(). */
final class DLPackBuffer implements BufferInterface
{
    public function byteLength(): int {}
    public function isWritable(): bool {}
    public function view(DType $type, int|array|null $shape = null, ?array $strides = null, int $offset = 0): TypedView {}
}

/** An exported DLPack tensor; opaque and consumed once by TypedView::fromDLPack(). */
final class DLPackCapsule
{
}

final class TypedView implements Countable, ArrayAccess, IteratorAggregate
{
    public function dtype(): DType {}
    public function shape(): array {}
    public function strides(): array {}
    public function byteOffset(): int {}
    public function buffer(): BufferInterface {}
    public function ndim(): int {}
    public function count(): int {}
    public function offsetExists(mixed $offset): bool {}
    public function offsetGet(mixed $offset): mixed {}
    public function offsetSet(mixed $offset, mixed $value): void {}
    public function offsetUnset(mixed $offset): void {}
    public function getIterator(): Iterator {}
    public function toDLPack(): DLPackCapsule {}
    public static function fromDLPack(DLPackCapsule $capsule): TypedView {}
}
