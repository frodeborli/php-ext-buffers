--TEST--
C API: a TensorAggregate implemented in another extension; symbolic dimensions in a Shape
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{TensorAggregate, Shape, DType, Tensor};
$t0 = new BatTensor();
$t2 = new BatTensor(2, 14);
var_dump($t0 instanceof TensorAggregate, $t0->getDType(), $t0->getShape()->toArray(), $t2->getDType(), $t2->getShape()->toArray());
foreach ([fn() => $t0->getTensor(), fn() => Tensor::fromArray([1]) + $t0, fn() => $t0 * Tensor::fromArray([1]), fn() => $t0->nope = 1, fn() => clone $t0, fn() => serialize($t0)] as $f) {
    try { $f(); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}

echo "symbolic dimensions\n";
$s = new Shape([2, $t0, 4]);
var_dump(count($s), $s[0], $s[1] === $t0, $s[-1], $s->concrete);
var_dump($s->toArray()[1] === $t0, iterator_to_array($s)[1] === $t0);
try { $s->size; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
var_dump($s == new Shape([2, $t0, 4]), $s == new Shape([2, new BatTensor(), 4]));
var_dump(bat_shape([3, $t0])->concrete, count(bat_shape_dims(bat_shape([1, 2, $t0]))), bat_shape_dims(bat_shape([5]))[0]);
foreach ([[$t2], [new BatTensor(1)], [-1], [1.5], ['x'], [null]] as $dims) {
    try { bat_shape($dims); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
try { new Shape([$t2]); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
echo "end\n";
--EXPECT--
bool(true)
enum(Buffers\DType::Int64)
array(0) {
}
enum(Buffers\DType::Float32)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(2)
}
Error: BatTensor has no numbers
Error: BatTensor has no numbers
Error: BatTensor has no numbers
Error: Cannot create dynamic property BatTensor::$nope
Error: Trying to clone an uncloneable object of class BatTensor
Exception: Serialization of 'BatTensor' is not allowed
symbolic dimensions
int(3)
int(2)
bool(true)
int(4)
bool(false)
bool(true)
bool(true)
Error: A shape with symbolic dimensions has no integer size
bool(true)
bool(false)
bool(false)
int(3)
int(5)
ValueError: Shape dimension 0 must be a 0-d TensorAggregate
ValueError: Shape dimension 0 must be a 0-d TensorAggregate
ValueError: Shape dimension 0 must not be negative, -1 given
TypeError: Shape dimension 0 must be of type int|Buffers\TensorAggregate, float given
TypeError: Shape dimension 0 must be of type int|Buffers\TensorAggregate, string given
TypeError: Shape dimension 0 must be of type int|Buffers\TensorAggregate, null given
ValueError: Shape dimension 0 must be a 0-d TensorAggregate
end
