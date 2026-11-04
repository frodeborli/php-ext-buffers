--TEST--
Tensor::convert(): dtype conversion and axis permutation in one pass
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType};
$t = Buffer::fromString(pack('f*', 1, 2, 3, 4, 5, 6))->view(DType::Float32, [2, 3]);
$d = $t->convert(DType::Float64);
echo $d->dtype->name, ' ', json_encode($d->jsonSerialize()), "\n";
$p = $t->convert(DType::Float32, [1, 0]);
echo json_encode($p->shape), ' ', json_encode($p->jsonSerialize()), "\n";
$h = $t->convert(DType::BFloat16, [1, 0]);
echo $h->dtype->name, ' ', json_encode($h->jsonSerialize()), "\n";
$i = Buffer::fromString(pack('l*', -1, 300, 7))->view(DType::Int32, [3])->convert(DType::UInt8);
echo json_encode($i->jsonSerialize()), "\n";
// a strided view (every other element) and a 3-d permutation
$v = Buffer::fromString(pack('f*', 0, 1, 2, 3, 4, 5, 6, 7))->view(DType::Float32, [4], [2]);
echo json_encode($v->convert(DType::Float32)->jsonSerialize()), "\n";
$c = Buffer::fromString(pack('f*', ...range(0, 23)))->view(DType::Float32, [2, 3, 4]);
echo json_encode($c->convert(DType::Float32, [2, 0, 1])->jsonSerialize()), "\n";
var_dump($t->convert(DType::Float32)->buffer->isWritable());
foreach ([fn() => $t->convert(DType::Float32, [0]), fn() => $t->convert(DType::Float32, [0, 0]), fn() => $t->convert(DType::UInt4), fn() => $t->convert(DType::Float32, [0, 2])] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), "\n"; }
}
?>
--EXPECT--
Float64 [[1,2,3],[4,5,6]]
[3,2] [[1,4],[2,5],[3,6]]
BFloat16 [[1,4],[2,5],[3,6]]
[255,44,7]
[0,2,4,6]
[[[0,4,8],[12,16,20]],[[1,5,9],[13,17,21]],[[2,6,10],[14,18,22]],[[3,7,11],[15,19,23]]]
bool(true)
ValueError
ValueError
Error
ValueError
