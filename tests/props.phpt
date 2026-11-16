--TEST--
Tensor properties: read-only, computed when read, no storage per view
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_VERSION_ID < 80400) die('skip PHP 8.4+: property hooks (PHP 8.3 is covered by props_php83.phpt)'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor, TensorAggregate, Shape};
$b = new Buffer(96);
$v = $b->view(DType::Int16, [3, 4], [8, 2], 6);
var_dump($v->dtype, $v->shape instanceof Shape, $v->shape->toArray(), $v->ndim, $v->strides, $v->offset, $v->buffer === $b);
var_dump($v instanceof TensorAggregate, $v instanceof Tensor);

echo "sub-views\n";
$r = $v[1];
var_dump($r->shape->toArray(), $r->strides, $r->offset, $r->ndim, $r->dtype);
$p = $v[2][1];   // a leaf is a plain number
var_dump($p);

echo "computed on read\n";
var_dump($v->shape === $v->shape, $v->shape == $v->shape, $v->shape == $r->shape, $v->strides === $v->strides);

echo "read-only\n";
foreach (['dtype', 'shape', 'ndim', 'strides', 'offset', 'buffer'] as $prop) {
    try { $v->$prop = 1; echo "writable $prop\n"; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
try { unset($v->ndim); echo "unset ok\n"; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { $v->shape[] = 1; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { $v->strides[0] = 99; echo "array copy modified: ", json_encode($v->strides), "\n"; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { $v->nope = 1; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
var_dump(@$v->nope);

echo "isset, property_exists, enumeration\n";
var_dump(isset($v->ndim), isset($v->nope), property_exists($v, 'shape'), property_exists(Tensor::class, 'offset'));
var_dump(array_keys(get_object_vars($v)));
echo json_encode($v), "\n";   // JsonSerializable: the data (see json.phpt)
var_dump(array_keys((array)$v) === array_keys(get_object_vars($v)), array_key_exists('value', get_object_vars($v)));

echo "reflection\n";
foreach (['dtype', 'shape', 'ndim', 'strides', 'offset', 'buffer', 'value'] as $prop) {
    $rp = new ReflectionProperty(Tensor::class, $prop);
    printf("%-8s %s virtual=%d public=%d hooks=%s\n", $prop, $rp->getType(), $rp->isVirtual(), $rp->isPublic(), json_encode(array_keys(array_filter(['get' => $rp->getHook(PropertyHookType::Get), 'set' => $rp->getHook(PropertyHookType::Set)]))));
}
foreach ((new ReflectionClass(TensorAggregate::class))->getMethods() as $rm) {
    printf("TensorAggregate::%s(): %s abstract=%d\n", $rm->getName(), $rm->getReturnType(), $rm->isAbstract());
}
var_dump((new ReflectionClass(TensorAggregate::class))->isInterface(), (new ReflectionClass(Tensor::class))->isAbstract());
var_dump($v->getTensor() === $v, $v->getDType() === $v->dtype, $v->getShape() == $v->shape);

echo "creating sub-views allocates nothing\n";
for ($i = 0; $i < 100; $i++) { $s = $v[$i % 3]; $t = $v[1][2]; }   // warm up the pool
unset($s, $t);
gc_collect_cycles();
$before = memory_get_usage();
for ($i = 0; $i < 100000; $i++) { $s = $v[$i % 3]; $t = $v[($i + 1) % 3][$i % 4]; }
unset($s, $t);
$after = memory_get_usage();
var_dump($after - $before);
echo "end\n";
--EXPECT--
enum(Buffers\DType::Int16)
bool(true)
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(4)
}
int(2)
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(2)
}
int(6)
bool(true)
bool(true)
bool(true)
sub-views
array(1) {
  [0]=>
  int(4)
}
array(1) {
  [0]=>
  int(2)
}
int(22)
int(1)
enum(Buffers\DType::Int16)
int(0)
computed on read
bool(false)
bool(true)
bool(false)
bool(true)
read-only
Error: Property Buffers\Tensor::$dtype is read-only
Error: Property Buffers\Tensor::$shape is read-only
Error: Property Buffers\Tensor::$ndim is read-only
Error: Property Buffers\Tensor::$strides is read-only
Error: Property Buffers\Tensor::$offset is read-only
Error: Property Buffers\Tensor::$buffer is read-only
Error: Cannot unset hooked property Buffers\Tensor::$ndim
Error: Cannot modify a Shape: it is immutable
Error: Indirect modification of Buffers\Tensor::$strides is not allowed
Error: Cannot create dynamic property Buffers\Tensor::$nope
NULL
isset, property_exists, enumeration
bool(true)
bool(false)
bool(true)
bool(true)
array(6) {
  [0]=>
  string(5) "dtype"
  [1]=>
  string(5) "shape"
  [2]=>
  string(4) "ndim"
  [3]=>
  string(7) "strides"
  [4]=>
  string(6) "offset"
  [5]=>
  string(6) "buffer"
}
[[0,0,0,0],[0,0,0,0],[0,0,0,0]]
bool(true)
bool(false)
reflection
dtype    Buffers\DType virtual=1 public=1 hooks=["get"]
shape    Buffers\Shape virtual=1 public=1 hooks=["get"]
ndim     int virtual=1 public=1 hooks=["get"]
strides  array virtual=1 public=1 hooks=["get"]
offset   int virtual=1 public=1 hooks=["get"]
buffer   Buffers\Buffer virtual=1 public=1 hooks=["get"]
value    mixed virtual=1 public=1 hooks=["get","set"]
TensorAggregate::getTensor(): Buffers\Tensor abstract=1
TensorAggregate::getDType(): ?Buffers\DType abstract=1
TensorAggregate::getShape(): Buffers\Shape abstract=1
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
creating sub-views allocates nothing
int(0)
end
