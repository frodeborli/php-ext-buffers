--TEST--
Tensor::fromArray() and toArray(): shape and dtype inference, round trips, 0-d and empty
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Tensor, DType, CpuDevice};

function show(Tensor $t): void {
    echo $t->dtype->name, ' ', json_encode($t->shape->toArray()), ' ', json_encode($t->toArray()), "\n";
}

// dtype inference
show(Tensor::fromArray([1, 2, 3]));
show(Tensor::fromArray([1.5, 2.5]));
show(Tensor::fromArray([true, false, true]));
show(Tensor::fromArray([1, 2.5, 3]));
show(Tensor::fromArray([[1, 2, 3], [4, 5, 6]]));
show(Tensor::fromArray([[[1], [2]], [[3], [4]], [[5], [6]]]));
var_dump(Tensor::fromArray([PHP_INT_MAX, PHP_INT_MIN])->toArray() === [PHP_INT_MAX, PHP_INT_MIN]);

// an explicit dtype
show(Tensor::fromArray([1, 2, 3], DType::Float32));
show(Tensor::fromArray([[1, 2], [3, 4]], DType::UInt8));
show(Tensor::fromArray([1.0, -2.0], DType::Int16));   // integral floats are accepted for integer dtypes
show(Tensor::fromArray([0.5, 1.5, 1000], DType::Float16));
show(Tensor::fromArray([true, false], DType::Bool));
show(Tensor::fromArray([[0.1, 0.2]], DType::Float64));
var_dump(Tensor::fromArray([0.1], DType::Float32)->toArray()[0] === unpack('f', pack('f', 0.1))[1]);

// bool, int and float come back as bool, int and float
foreach ([DType::Int8, DType::UInt16, DType::Int32, DType::UInt32, DType::Int64, DType::Float32, DType::Float64, DType::BFloat16, DType::Float8E4M3FN] as $dt) {
    $a = Tensor::fromArray([[0, 1], [2, 3]], $dt)->toArray();
    echo $dt->name, ' ', json_encode($a), ' ', gettype($a[1][1]), "\n";
}
var_dump(Tensor::fromArray([true])->toArray()[0]);

// memory layout: C-contiguous and writable, own buffer
$t = Tensor::fromArray([[1, 2, 3], [4, 5, 6]]);
echo json_encode($t->strides), ' ', $t->offset, ' ', $t->buffer->byteLength, ' ', var_export($t->buffer->isWritable(), true), "\n";
$t[1][2] = 60;
echo json_encode($t->toArray()), "\n";
echo bin2hex(Tensor::fromArray([1, 258], DType::UInt16)->buffer->toString()) === (PHP_INT_SIZE ? bin2hex(pack('v*', 1, 258)) : '') ? "bytes ok\n" : "bytes differ\n";

// 0-d: a scalar
$s = Tensor::fromArray(5);
echo $s->ndim, ' ', $s->dtype->name, ' ', json_encode($s->shape->toArray()), ' ', var_export($s->toArray(), true), ' ', $s->value, "\n";
var_dump(Tensor::fromArray(2.5)->toArray(), Tensor::fromArray(true)->toArray(), Tensor::fromArray(300, DType::UInt16)->toArray());
$z = Tensor::fromArray(7, DType::Float32);
echo $z->dtype->name, ' ', $z->toArray(), "\n";
$v = (new Buffers\Buffer(8))->view(DType::Int64, []);
$v->value = 9;
var_dump($v->toArray());
var_dump(Tensor::fromArray($v->toArray(), DType::Int64)->toArray());

// empty arrays: a shape that ends in 0, Float64 unless a dtype is given
show(Tensor::fromArray([]));
show(Tensor::fromArray([], DType::UInt8));
show(Tensor::fromArray([[], []]));
show(Tensor::fromArray([[]], DType::Int32));
show(Tensor::fromArray([[[]]]));
$e = Tensor::fromArray([]);
echo count($e), ' ', $e->buffer->byteLength, "\n";
show($e->buffer->view(DType::Int8, [0, 3]));

// other keys: the values are used in order; references are followed
$x = 3;
show(Tensor::fromArray(['a' => 1, 'b' => [2][0], 5 => &$x]));
$k = [4, 5];
show(Tensor::fromArray([[1, 2], &$k]));
$p = [10 => [1, 2], 20 => [3, 4]];
next($p);
show(Tensor::fromArray($p));   // the internal pointer of the array does not matter

// round trip through toArray() and fromArray()
$r = Tensor::fromArray([[[1, 2], [3, 4]], [[5, 6], [7, 8]]], DType::Int8);
var_dump($r->toArray() === Tensor::fromArray($r->toArray(), DType::Int8)->toArray());
// views: toArray() of a sub-view and of a strided view
show($r[1]);
show($r->buffer->view(DType::Int8, [2, 2], [4, 2]));
echo json_encode($r), "\n";   // jsonSerialize() is the same

// the device argument: CPU memory
$c = Tensor::fromArray([1, 2], null, CpuDevice::get());
echo $c->buffer->device->name, ' ', json_encode($c->toArray()), "\n";
$c = Tensor::fromArray([1, 2], DType::Int32, null);
echo $c->dtype->name, "\n";
?>
--EXPECT--
Int64 [3] [1,2,3]
Float64 [2] [1.5,2.5]
Bool [3] [true,false,true]
Float64 [3] [1,2.5,3]
Int64 [2,3] [[1,2,3],[4,5,6]]
Int64 [3,2,1] [[[1],[2]],[[3],[4]],[[5],[6]]]
bool(true)
Float32 [3] [1,2,3]
UInt8 [2,2] [[1,2],[3,4]]
Int16 [2] [1,-2]
Float16 [3] [0.5,1.5,1000]
Bool [2] [true,false]
Float64 [1,2] [[0.1,0.2]]
bool(true)
Int8 [[0,1],[2,3]] integer
UInt16 [[0,1],[2,3]] integer
Int32 [[0,1],[2,3]] integer
UInt32 [[0,1],[2,3]] integer
Int64 [[0,1],[2,3]] integer
Float32 [[0,1],[2,3]] double
Float64 [[0,1],[2,3]] double
BFloat16 [[0,1],[2,3]] double
Float8E4M3FN [[0,1],[2,3]] double
bool(true)
[3,1] 0 48 true
[[1,2,3],[4,5,60]]
bytes ok
0 Int64 [] 5 5
float(2.5)
bool(true)
int(300)
Float32 7
int(9)
int(9)
Float64 [0] []
UInt8 [0] []
Float64 [2,0] [[],[]]
Int32 [1,0] [[]]
Float64 [1,1,0] [[[]]]
0 0
Int8 [0,3] []
Int64 [3] [1,2,3]
Int64 [2,2] [[1,2],[4,5]]
Int64 [2,2] [[1,2],[3,4]]
bool(true)
Int8 [2,2] [[5,6],[7,8]]
Int8 [2,2] [[1,3],[5,7]]
[[[1,2],[3,4]],[[5,6],[7,8]]]
cpu [1,2]
Int32
