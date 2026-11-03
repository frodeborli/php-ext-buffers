--TEST--
view(): argument validation
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, ArrayBuffer};
$b = new ArrayBuffer(24);
$cases = [
  'default 1-D'            => [DType::UInt8],
  'int16 default'          => [DType::Int16],
  'int shape'              => [DType::Int16, 5],
  'shape 0'                => [DType::UInt8, 0],
  'array shape'            => [DType::Int16, [3, 4]],
  'with strides'           => [DType::Int16, [3, 2], [4, 2]],
  'with offset'            => [DType::Int16, [2, 2], null, 8],
  'offset at end'          => [DType::UInt8, 0, null, 24],
  'float64 3 elements'     => [DType::Float64],
  'int32 x 6'              => [DType::Int32, [6]],
  'zero dim in shape'      => [DType::Int16, [0, 5]],
  'zero dim wide strides'  => [DType::Int16, [4, 0], [100, 1]],
  'complex128'             => [DType::Complex128],
  'too big'                => [DType::Int16, 13],
  'too big 2d'             => [DType::UInt8, [5, 5]],
  'strides overreach'      => [DType::Int16, [3, 2], [5, 2]],
  'offset beyond'          => [DType::UInt8, null, null, 25],
  'negative offset'        => [DType::UInt8, null, null, -1],
  'misaligned offset'      => [DType::Int16, null, null, 1],
  'uneven default shape'   => [DType::Int32, null, null, 2],
  'negative dim'           => [DType::UInt8, [-1]],
  'negative int shape'     => [DType::UInt8, -3],
  'empty shape'            => [DType::UInt8, []],
  '9 dims'                 => [DType::UInt8, [1,1,1,1,1,1,1,1,1]],
  '8 dims'                 => [DType::UInt8, [1,1,1,1,1,1,1,1]],
  'string in shape'        => [DType::UInt8, ['3']],
  'float in shape'         => [DType::UInt8, [3.0]],
  'string shape'           => [DType::UInt8, '3'],
  'strides wrong count'    => [DType::UInt8, [2, 2], [2]],
  'zero stride'            => [DType::UInt8, [2, 2], [0, 1]],
  'negative stride'        => [DType::UInt8, [2, 2], [-2, 1]],
  'huge shape'             => [DType::UInt8, [PHP_INT_MAX, 2]],
  'huge stride'            => [DType::Int64, [2], [PHP_INT_MAX]],
  'overflow product'       => [DType::UInt8, [PHP_INT_MAX, PHP_INT_MAX]],
  'huge offset'            => [DType::UInt8, 1, null, PHP_INT_MAX],
  'non-enum type'          => ['uint8'],
];
foreach ($cases as $name => $args) {
    try {
        $v = $b->view(...$args);
        printf("%-22s ok shape=%s strides=%s off=%d dtype=%s\n", $name, json_encode($v->shape->toArray()), json_encode($v->strides), $v->offset, $v->dtype->name);
    } catch (Throwable $e) {
        printf("%-22s %s: %s\n", $name, get_class($e), $e->getMessage());
    }
}
--EXPECT--
default 1-D            ok shape=[24] strides=[1] off=0 dtype=UInt8
int16 default          ok shape=[12] strides=[1] off=0 dtype=Int16
int shape              ok shape=[5] strides=[1] off=0 dtype=Int16
shape 0                ok shape=[0] strides=[1] off=0 dtype=UInt8
array shape            ok shape=[3,4] strides=[4,1] off=0 dtype=Int16
with strides           ok shape=[3,2] strides=[4,2] off=0 dtype=Int16
with offset            ok shape=[2,2] strides=[2,1] off=8 dtype=Int16
offset at end          ok shape=[0] strides=[1] off=24 dtype=UInt8
float64 3 elements     ok shape=[3] strides=[1] off=0 dtype=Float64
int32 x 6              ok shape=[6] strides=[1] off=0 dtype=Int32
zero dim in shape      ok shape=[0,5] strides=[5,1] off=0 dtype=Int16
zero dim wide strides  ok shape=[4,0] strides=[100,1] off=0 dtype=Int16
complex128             ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) is required: the buffer length minus the offset is not a multiple of the element size (16 bytes)
too big                ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (24 bytes)
too big 2d             ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (24 bytes)
strides overreach      ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (24 bytes)
offset beyond          ValueError: Buffers\ArrayBuffer::view(): Argument #4 ($offset) must be between 0 and the byte length of the buffer
negative offset        ValueError: Buffers\ArrayBuffer::view(): Argument #4 ($offset) must be between 0 and the byte length of the buffer
misaligned offset      ValueError: Buffers\ArrayBuffer::view(): Argument #4 ($offset) must be a multiple of the element size (2 bytes)
uneven default shape   ValueError: Buffers\ArrayBuffer::view(): Argument #4 ($offset) must be a multiple of the element size (4 bytes)
negative dim           ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) must contain only non-negative values
negative int shape     ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) must be non-negative
empty shape            ok shape=[] strides=[] off=0 dtype=UInt8
9 dims                 ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) must have at most 8 elements
8 dims                 ok shape=[1,1,1,1,1,1,1,1] strides=[1,1,1,1,1,1,1,1] off=0 dtype=UInt8
string in shape        TypeError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) must contain only integers, string given
float in shape         TypeError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) must contain only integers, float given
string shape           TypeError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) must be of type array|int|null, string given
strides wrong count    ValueError: Buffers\ArrayBuffer::view(): Argument #3 ($strides) must have one entry per dimension (2)
zero stride            ValueError: Buffers\ArrayBuffer::view(): Argument #3 ($strides) must contain only values of at least 1
negative stride        ValueError: Buffers\ArrayBuffer::view(): Argument #3 ($strides) must contain only values of at least 1
huge shape             ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) describes more elements than can be addressed
huge stride            ValueError: Buffers\ArrayBuffer::view(): Argument #3 ($strides) stride is too large
overflow product       ValueError: Buffers\ArrayBuffer::view(): Argument #2 ($shape) describes more elements than can be addressed
huge offset            ValueError: Buffers\ArrayBuffer::view(): Argument #4 ($offset) must be between 0 and the byte length of the buffer
non-enum type          TypeError: Buffers\ArrayBuffer::view(): Argument #1 ($type) must be of type Buffers\DType, string given
