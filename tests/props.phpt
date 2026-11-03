--TEST--
BufferView properties: read-only, computed when read, no storage per view
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{ArrayBuffer, DType, BufferView, Tensor, Shape, BufferInterface};
$b = new ArrayBuffer(96);
$v = $b->view(DType::Int16, [3, 4], [8, 2], 6);
var_dump($v->dtype, $v->shape instanceof Shape, $v->shape->toArray(), $v->ndim, $v->strides, $v->offset, $v->buffer === $b);
var_dump($v instanceof Tensor, $v instanceof BufferView);

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
try { $x = $v->nope; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }

echo "isset, property_exists, enumeration\n";
var_dump(isset($v->ndim), isset($v->nope), property_exists($v, 'shape'), property_exists(BufferView::class, 'offset'));
var_dump(array_keys(get_object_vars($v)));
$j = json_decode(json_encode($v), true);
var_dump($j['ndim'], $j['strides'], $j['offset']);

echo "reflection\n";
foreach (['dtype', 'shape', 'ndim', 'strides', 'offset', 'buffer'] as $prop) {
    $rp = new ReflectionProperty(BufferView::class, $prop);
    printf("%-8s %s virtual=%d public=%d hooks=%s\n", $prop, $rp->getType(), $rp->isVirtual(), $rp->isPublic(), json_encode(array_keys(array_filter(['get' => $rp->getHook(PropertyHookType::Get), 'set' => $rp->getHook(PropertyHookType::Set)]))));
}
foreach ((new ReflectionClass(Tensor::class))->getProperties() as $rp) {
    printf("Tensor::$%s %s abstract=%d\n", $rp->getName(), $rp->getType(), $rp->isAbstract());
}
var_dump((new ReflectionClass(Tensor::class))->isInterface(), (new ReflectionClass(BufferView::class))->isAbstract());

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
Error: Property Buffers\BufferView::$dtype is read-only
Error: Property Buffers\BufferView::$shape is read-only
Error: Property Buffers\BufferView::$ndim is read-only
Error: Property Buffers\BufferView::$strides is read-only
Error: Property Buffers\BufferView::$offset is read-only
Error: Property Buffers\BufferView::$buffer is read-only
Error: Cannot unset hooked property Buffers\BufferView::$ndim
Error: Cannot modify a Shape: it is immutable
Error: Indirect modification of Buffers\BufferView::$strides is not allowed
Error: Cannot create dynamic property Buffers\BufferView::$nope

Warning: Undefined property: Buffers\BufferView::$nope in /tmp/claude-1000/-home-frode/ef688df8-4de0-41db-a1cf-1d0800d75848/scratchpad/b84/tests/props.php on line 25
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
int(2)
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(2)
}
int(6)
reflection
dtype    Buffers\DType virtual=1 public=1 hooks=["get"]
shape    Buffers\Shape virtual=1 public=1 hooks=["get"]
ndim     int virtual=1 public=1 hooks=["get"]
strides  array virtual=1 public=1 hooks=["get"]
offset   int virtual=1 public=1 hooks=["get"]
buffer   Buffers\BufferInterface virtual=1 public=1 hooks=["get"]
Tensor::$dtype ?Buffers\DType abstract=1
Tensor::$shape Buffers\Shape abstract=1
Tensor::$ndim int abstract=1
bool(true)
bool(false)
creating sub-views allocates nothing
int(0)
end
