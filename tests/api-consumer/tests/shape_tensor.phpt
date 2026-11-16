--TEST--
C API: a TensorInterface implemented in another extension; symbolic dimensions in a Shape
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); if (PHP_VERSION_ID < 80400) die('skip PHP 8.4+: property hooks (PHP 8.3 is covered by virtual_props_php83.phpt)'); ?>
--FILE--
<?php
use Buffers\{TensorInterface, Shape, DType};
$t0 = new BatTensor();
$t2 = new BatTensor(2, 14);
var_dump($t0 instanceof TensorInterface, $t0->ndim, $t0->dtype, $t0->shape->toArray(), $t2->ndim, $t2->dtype, $t2->shape->toArray());
foreach (['dtype', 'shape', 'ndim'] as $p) {
    try { $t0->$p = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }
}
print_r(array_keys(get_object_vars($t2)));
$rp = new ReflectionProperty(BatTensor::class, 'ndim');
var_dump($rp->isVirtual(), (string)$rp->getType());
foreach ([fn() => $t0->nope = 1, fn() => clone $t0, fn() => serialize($t0)] as $f) {
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
int(0)
enum(Buffers\DType::Int64)
array(0) {
}
int(2)
enum(Buffers\DType::Float32)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(2)
}
Property BatTensor::$dtype is read-only
Property BatTensor::$shape is read-only
Property BatTensor::$ndim is read-only
Array
(
    [0] => dtype
    [1] => shape
    [2] => ndim
)
bool(true)
string(3) "int"
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
Error: Cannot compute the size of a shape with symbolic dimensions: no extension that builds deferred computations is loaded
bool(true)
bool(false)
bool(false)
int(3)
int(5)
ValueError: Shape dimension 0 must be a 0-d TensorInterface
ValueError: Shape dimension 0 must be a 0-d TensorInterface
ValueError: Shape dimension 0 must not be negative, -1 given
TypeError: Shape dimension 0 must be of type int|Buffers\TensorInterface, float given
TypeError: Shape dimension 0 must be of type int|Buffers\TensorInterface, string given
TypeError: Shape dimension 0 must be of type int|Buffers\TensorInterface, null given
ValueError: Shape dimension 0 must be a 0-d TensorInterface
end
