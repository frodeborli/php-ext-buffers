--TEST--
count, foreach, iterators, ArrayAccess and Countable interfaces
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer, Tensor};
$b = new Buffer(24);
$raw = $b->view(DType::UInt8);
for ($i = 0; $i < 24; $i++) $raw[$i] = $i;
$m = $b->view(DType::UInt8, [2, 3, 4]);
var_dump(count($m), count($m[0]), count($m[1][2]), $m instanceof Countable, $m instanceof ArrayAccess, $m instanceof IteratorAggregate, $m instanceof Traversable);
foreach ($m as $k => $plane) echo $k, ': ', get_class($plane), ' ', json_encode($plane->shape->toArray()), ' ', json_encode(iterator_to_array($plane[1])), "\n";
foreach ($m[1][1] as $k => $x) echo "$k=$x ";
echo "\n";
echo json_encode(iterator_to_array($raw)), "\n";
echo implode(',', array_map(fn($row) => array_sum(iterator_to_array($row)), iterator_to_array($m[1]))), "\n";
// nested foreach and break
foreach ($m as $i => $plane) { foreach ($plane as $j => $row) { foreach ($row as $k => $x) { if ($x > 5) break 3; echo "$i$j$k:$x "; } } }
echo "\n";
// the iterator object and generators
$it = $m->getIterator();
var_dump($it instanceof Iterator, $it->valid(), $it->key(), $it->current()->shape->toArray());
$it->next(); $it->next(); var_dump($it->valid());
$it->rewind(); var_dump($it->key());
function gen($v) { foreach ($v as $x) yield $x[0][0]; }
echo implode(',', iterator_to_array(gen($m))), "\n";
// by reference is refused
try { foreach ($raw as &$r) {} } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
// ArrayAccess methods called directly
var_dump($raw->offsetGet(5), $raw->offsetExists(5), $raw->offsetExists(99));
$raw->offsetSet(5, 50); var_dump($raw[5]);
foreach ([fn() => $raw->offsetGet(99), fn() => $raw->offsetSet(null, 1), fn() => $raw->offsetUnset(1), fn() => $raw->offsetGet('a')] as $f) {
    try { $f(); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
// list() destructuring and spread work through ArrayAccess/Traversable
[$a, $c] = [$raw[0], $raw[2]]; var_dump($a, $c);
var_dump([...$m[0][0]]);
--EXPECT--
int(2)
int(3)
int(4)
bool(true)
bool(true)
bool(true)
bool(true)
0: Buffers\Tensor [3,4] [4,5,6,7]
1: Buffers\Tensor [3,4] [16,17,18,19]
0=16 1=17 2=18 3=19 
[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23]
54,70,86
000:0 001:1 002:2 003:3 010:4 011:5 
bool(true)
bool(true)
int(0)
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(4)
}
bool(false)
int(0)
0,12
Error: An iterator cannot be used with foreach by reference
int(5)
bool(true)
bool(false)
int(50)
ValueError: Index 99 is out of range for an axis of length 24
Error: Cannot append to a Tensor
Error: Cannot unset an element of a Tensor
TypeError: Tensor offset must be of type int, string given
int(0)
int(2)
array(4) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(2)
  [3]=>
  int(3)
}
