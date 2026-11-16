--TEST--
Tensor::fromArray(): ragged arrays, range checks, type errors
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_INT_SIZE < 8) die('skip 64-bit PHP only: 64-bit dtypes'); ?>
--FILE--
<?php
use Buffers\{Tensor, DType};

function t(string $label, callable $f): void {
    try { $r = $f(); echo $label, ': ok ', json_encode($r->toArray()), "\n"; }
    catch (Throwable $e) { echo $label, ': ', get_class($e), ': ', $e->getMessage(), "\n"; }
}

// ragged: the path of the first offending array or value
t('short row', fn() => Tensor::fromArray([[1, 2], [3]]));
t('long row', fn() => Tensor::fromArray([[1, 2], [3, 4, 5]]));
t('third row', fn() => Tensor::fromArray([[1, 2], [3, 4], [5]]));
t('deep', fn() => Tensor::fromArray([[[1, 2], [3, 4]], [[5, 6], [7]]]));
t('scalar for array', fn() => Tensor::fromArray([[1, 2], 3]));
t('array for scalar', fn() => Tensor::fromArray([[1, 2], [3, [4]]]));
t('array for scalar, explicit dtype', fn() => Tensor::fromArray([[1, 2], [[3], 4]], DType::Int8));
t('empty vs not', fn() => Tensor::fromArray([[], [1]]));
t('not vs empty', fn() => Tensor::fromArray([[1], []]));
t('first is empty', fn() => Tensor::fromArray([[], [[]]]));
t('depth 8 is fine', fn() => Tensor::fromArray([[[[[[[[1]]]]]]]]));
t('depth 9', fn() => Tensor::fromArray([[[[[[[[[1]]]]]]]]]));

// values that are not numbers
t('string', fn() => Tensor::fromArray([1, 'a']));
t('null', fn() => Tensor::fromArray([[1, 2], [3, null]]));
t('object', fn() => Tensor::fromArray([new stdClass]));
t('string scalar', fn() => Tensor::fromArray('x'));
t('mixed bool and int', fn() => Tensor::fromArray([true, 1]));
t('mixed bool and int, dtype', fn() => Tensor::fromArray([true, 1], DType::Int8));
t('bool in Bool', fn() => Tensor::fromArray([1, 0], DType::Bool));
t('bool in Int8', fn() => Tensor::fromArray([true], DType::Int8));

// range checks: errors, never wrapping
t('uint8 256', fn() => Tensor::fromArray([0, 255, 256], DType::UInt8));
t('uint8 -1', fn() => Tensor::fromArray([[0], [-1]], DType::UInt8));
t('int8 128', fn() => Tensor::fromArray([127, 128], DType::Int8));
t('int8 -129', fn() => Tensor::fromArray([-128, -129], DType::Int8));
t('int16 ok', fn() => Tensor::fromArray([-32768, 32767], DType::Int16));
t('int16 32768', fn() => Tensor::fromArray([32768], DType::Int16));
t('uint16 65536', fn() => Tensor::fromArray([65536], DType::UInt16));
t('int32 edge', fn() => Tensor::fromArray([-2147483648, 2147483647], DType::Int32));
t('int32 2^31', fn() => Tensor::fromArray([2147483648], DType::Int32));
t('uint32 edge', fn() => Tensor::fromArray([0, 4294967295], DType::UInt32));
t('uint32 2^32', fn() => Tensor::fromArray([4294967296], DType::UInt32));
t('uint64 max php int', fn() => Tensor::fromArray([PHP_INT_MAX], DType::UInt64));
t('uint64 -1', fn() => Tensor::fromArray([-1], DType::UInt64));
t('int64 edges', fn() => Tensor::fromArray([PHP_INT_MIN, PHP_INT_MAX], DType::Int64));
t('float 1.5 into int', fn() => Tensor::fromArray([1.5], DType::Int32));
t('float NaN into int', fn() => Tensor::fromArray([NAN], DType::Int32));
t('float INF into int', fn() => Tensor::fromArray([INF], DType::Int32));
t('float 1e30 into int', fn() => Tensor::fromArray([1e30], DType::Int64));
t('float 300.0 into uint8', fn() => Tensor::fromArray([300.0], DType::UInt8));
t('float -0.0 into int', fn() => Tensor::fromArray([-0.0], DType::Int8));
t('int2', fn() => Tensor::fromArray([-2, -1, 0, 1], DType::Int2));
t('int2 2', fn() => Tensor::fromArray([2], DType::Int2));
t('int2 -3', fn() => Tensor::fromArray([[0, 0], [0, -3]], DType::Int2));
t('uint2 4', fn() => Tensor::fromArray([0, 3, 4], DType::UInt2));
t('int4 8', fn() => Tensor::fromArray([8], DType::Int4));
t('int4 -9', fn() => Tensor::fromArray([-9], DType::Int4));
t('uint4 16', fn() => Tensor::fromArray([15, 16], DType::UInt4));
t('scalar 256 as uint8', fn() => Tensor::fromArray(256, DType::UInt8));

// floats: out of range rounds like every float write (no error)

echo json_encode(Tensor::fromArray([1e10, -1e10, NAN], DType::Float16)->buffer->toString() === pack('v*', 0x7c00, 0xfc00, 0x7e00)), "\n";

// dtypes that are not supported
t('complex', fn() => Tensor::fromArray([1, 2], DType::Complex64));
t('complex scalar', fn() => Tensor::fromArray(1, DType::BComplex32));
?>
--EXPECT--
short row: ValueError: Tensor::fromArray(): ragged array at [1]: expected 2 elements, found 1
long row: ValueError: Tensor::fromArray(): ragged array at [1]: expected 2 elements, found 3
third row: ValueError: Tensor::fromArray(): ragged array at [2]: expected 2 elements, found 1
deep: ValueError: Tensor::fromArray(): ragged array at [1][1]: expected 2 elements, found 1
scalar for array: ValueError: Tensor::fromArray(): ragged array at [1]: expected an array, found int
array for scalar: ValueError: Tensor::fromArray(): ragged array at [1][1]: expected a scalar, found an array
array for scalar, explicit dtype: ValueError: Tensor::fromArray(): ragged array at [1][0]: expected a scalar, found an array
empty vs not: ValueError: Tensor::fromArray(): ragged array at [1]: expected 0 elements, found 1
not vs empty: ValueError: Tensor::fromArray(): ragged array at [1]: expected 1 elements, found 0
first is empty: ValueError: Tensor::fromArray(): ragged array at [1]: expected 0 elements, found 1
depth 8 is fine: ok [[[[[[[[1]]]]]]]]
depth 9: ValueError: Tensor::fromArray(): the array is nested more than 8 levels deep
string: ValueError: Tensor::fromArray(): unsupported value at [1]: expected an int, float or bool, found string
null: ValueError: Tensor::fromArray(): unsupported value at [1][1]: expected an int, float or bool, found null
object: ValueError: Tensor::fromArray(): unsupported value at [0]: expected an int, float or bool, found stdClass
string scalar: TypeError: Buffers\Tensor::fromArray(): Argument #1 ($data) must be an array, int, float or bool, string given
mixed bool and int: ValueError: Tensor::fromArray(): cannot infer a dtype from a mix of bool and numbers; pass a dtype
mixed bool and int, dtype: ValueError: Tensor::fromArray(): cannot store bool at [0] as Int8: a numeric tensor takes only int and float values
bool in Bool: ValueError: Tensor::fromArray(): cannot store 1 at [0] as Bool: a Bool tensor takes only bool values
bool in Int8: ValueError: Tensor::fromArray(): cannot store bool at [0] as Int8: a numeric tensor takes only int and float values
uint8 256: ValueError: Tensor::fromArray(): cannot store 256 at [2] as UInt8: out of range
uint8 -1: ValueError: Tensor::fromArray(): cannot store -1 at [1][0] as UInt8: out of range
int8 128: ValueError: Tensor::fromArray(): cannot store 128 at [1] as Int8: out of range
int8 -129: ValueError: Tensor::fromArray(): cannot store -129 at [1] as Int8: out of range
int16 ok: ok [-32768,32767]
int16 32768: ValueError: Tensor::fromArray(): cannot store 32768 at [0] as Int16: out of range
uint16 65536: ValueError: Tensor::fromArray(): cannot store 65536 at [0] as UInt16: out of range
int32 edge: ok [-2147483648,2147483647]
int32 2^31: ValueError: Tensor::fromArray(): cannot store 2147483648 at [0] as Int32: out of range
uint32 edge: ok [0,4294967295]
uint32 2^32: ValueError: Tensor::fromArray(): cannot store 4294967296 at [0] as UInt32: out of range
uint64 max php int: ok [9223372036854775807]
uint64 -1: ValueError: Tensor::fromArray(): cannot store -1 at [0] as UInt64: out of range
int64 edges: ok [-9223372036854775808,9223372036854775807]
float 1.5 into int: ValueError: Tensor::fromArray(): cannot store float at [0] as Int32: the float is not an integer in range
float NaN into int: ValueError: Tensor::fromArray(): cannot store float at [0] as Int32: the float is not an integer in range
float INF into int: ValueError: Tensor::fromArray(): cannot store float at [0] as Int32: the float is not an integer in range
float 1e30 into int: ValueError: Tensor::fromArray(): cannot store float at [0] as Int64: the float is not an integer in range
float 300.0 into uint8: ValueError: Tensor::fromArray(): cannot store float at [0] as UInt8: out of range
float -0.0 into int: ok [0]
int2: ok [-2,-1,0,1]
int2 2: ValueError: Tensor::fromArray(): cannot store 2 at [0] as Int2: out of range
int2 -3: ValueError: Tensor::fromArray(): cannot store -3 at [1][1] as Int2: out of range
uint2 4: ValueError: Tensor::fromArray(): cannot store 4 at [2] as UInt2: out of range
int4 8: ValueError: Tensor::fromArray(): cannot store 8 at [0] as Int4: out of range
int4 -9: ValueError: Tensor::fromArray(): cannot store -9 at [0] as Int4: out of range
uint4 16: ValueError: Tensor::fromArray(): cannot store 16 at [1] as UInt4: out of range
scalar 256 as uint8: ValueError: Tensor::fromArray(): cannot store 256 at the top level as UInt8: out of range
true
complex: ValueError: Buffers\Tensor::fromArray(): Argument #2 ($dtype) must not be a complex dtype
complex scalar: ValueError: Buffers\Tensor::fromArray(): Argument #2 ($dtype) must not be a complex dtype
