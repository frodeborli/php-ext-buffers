--TEST--
Shape: validation, negative indices, immutability, iteration, size
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Shape, Buffer, DType, Tensor};
$s = new Shape([2, 3, 4]);
var_dump(count($s), $s[0], $s[2], $s[-1], $s[-3], $s->toArray(), $s->isConcrete(), $s->size());
var_dump(isset($s[0]), isset($s[-3]), isset($s[3]), isset($s[-4]), isset($s['x']), $s->offsetExists(1));
foreach ($s as $i => $d) echo "$i=$d ";
echo "\n";
var_dump(iterator_to_array($s), $s instanceof ArrayAccess, $s instanceof Countable, $s instanceof IteratorAggregate);

echo "empty shape: a scalar\n";
$e = new Shape([]);
var_dump(count($e), $e->toArray(), $e->size(), $e->isConcrete(), iterator_to_array($e));
var_dump(new Shape([0, 5])->size());

echo "errors\n";
$cases = [
    'out of range'      => fn() => $s[3],
    'negative range'    => fn() => $s[-4],
    'string offset'     => fn() => $s['x'],
    'float offset'      => fn() => $s[1.5],
    'null offset'       => fn() => $s[null],
    'append'            => function () use ($s) { $s[] = 1; },
    'assign'            => function () use ($s) { $s[0] = 1; },
    'assign method'     => fn() => $s->offsetSet(0, 1),
    'unset'             => function () use ($s) { unset($s[0]); },
    'unset method'      => fn() => $s->offsetUnset(0),
    'negative dim'      => fn() => new Shape([2, -1]),
    'float dim'         => fn() => new Shape([2.0]),
    'string dim'        => fn() => new Shape(['2']),
    'object dim'        => fn() => new Shape([new stdClass]),
    'null dim'          => fn() => new Shape([null]),
    'nested array'      => fn() => new Shape([[2]]),
    'not a list'        => fn() => new Shape(['a' => 2]),
    'gap'               => fn() => new Shape([1 => 2]),
    'no arguments'      => fn() => new Shape(),
    'int argument'      => fn() => new Shape(3),
    'overflow size'     => fn() => (new Shape([PHP_INT_MAX, PHP_INT_MAX, PHP_INT_MAX]))->size(),
    'clone'             => fn() => clone $s,
    'serialize'         => fn() => serialize($s),
    'dynamic property'  => function () use ($s) { $s->x = 1; },
    'construct twice'   => fn() => $s->__construct([1]),
];
foreach ($cases as $name => $f) {
    try { $f(); echo "$name: no error\n"; } catch (Throwable $e) { printf("%-16s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
var_dump($s->toArray());   // unchanged

echo "equality is by value\n";
var_dump($s == new Shape([2, 3, 4]), $s == new Shape([2, 3]), $s == new Shape([2, 3, 5]), $s === $s, new Shape([]) == new Shape([]));
var_dump($s != new Shape([2, 3, 4]));

echo "views have concrete shapes\n";
$b = new Buffer(48);
$sh = $b->view(DType::Int16, [2, 3, 4])->shape;
var_dump($sh->toArray(), $sh->isConcrete(), $sh->size(), $sh->offsetGet(-2));
var_dump($b->view(DType::Int16, [])->shape->size());

echo "a Shape of references\n";
$a = 5; $d = [&$a, 6];
var_dump((new Shape($d))->toArray());
var_dump($s);
echo "end\n";
--EXPECT--
int(3)
int(2)
int(4)
int(4)
int(2)
array(3) {
  [0]=>
  int(2)
  [1]=>
  int(3)
  [2]=>
  int(4)
}
bool(true)
int(24)
bool(true)
bool(true)
bool(false)
bool(false)
bool(false)
bool(true)
0=2 1=3 2=4 
array(3) {
  [0]=>
  int(2)
  [1]=>
  int(3)
  [2]=>
  int(4)
}
bool(true)
bool(true)
bool(true)
empty shape: a scalar
int(0)
array(0) {
}
int(1)
bool(true)
array(0) {
}
int(0)
errors
out of range     ValueError: Index 3 is out of range for a shape of 3 dimensions
negative range   ValueError: Index -4 is out of range for a shape of 3 dimensions
string offset    TypeError: Shape offset must be of type int, string given
float offset     TypeError: Shape offset must be of type int, float given
null offset      TypeError: Shape offset must be of type int, null given
append           Error: Cannot modify a Shape: it is immutable
assign           Error: Cannot modify a Shape: it is immutable
assign method    Error: Cannot modify a Shape: it is immutable
unset            Error: Cannot modify a Shape: it is immutable
unset method     Error: Cannot modify a Shape: it is immutable
negative dim     ValueError: Shape dimension 1 must not be negative, -1 given
float dim        TypeError: Shape dimension 0 must be of type int|Buffers\Tensor, float given
string dim       TypeError: Shape dimension 0 must be of type int|Buffers\Tensor, string given
object dim       TypeError: Shape dimension 0 must be of type int|Buffers\Tensor, stdClass given
null dim         TypeError: Shape dimension 0 must be of type int|Buffers\Tensor, null given
nested array     TypeError: Shape dimension 0 must be of type int|Buffers\Tensor, array given
not a list       ValueError: Buffers\Shape::__construct(): Argument #1 ($dims) must be a list
gap              ValueError: Buffers\Shape::__construct(): Argument #1 ($dims) must be a list
no arguments     ArgumentCountError: Buffers\Shape::__construct() expects exactly 1 argument, 0 given
int argument     TypeError: Buffers\Shape::__construct(): Argument #1 ($dims) must be of type array, int given
overflow size    ValueError: The size of the shape exceeds PHP_INT_MAX
clone            Error: Trying to clone an uncloneable object of class Buffers\Shape
serialize        Exception: Serialization of 'Buffers\Shape' is not allowed
dynamic property Error: Cannot create dynamic property Buffers\Shape::$x
construct twice  Error: Shape is already constructed
array(3) {
  [0]=>
  int(2)
  [1]=>
  int(3)
  [2]=>
  int(4)
}
equality is by value
bool(true)
bool(false)
bool(false)
bool(true)
bool(true)
bool(false)
views have concrete shapes
array(3) {
  [0]=>
  int(2)
  [1]=>
  int(3)
  [2]=>
  int(4)
}
bool(true)
int(24)
int(3)
int(1)
a Shape of references
array(2) {
  [0]=>
  int(5)
  [1]=>
  int(6)
}
object(Buffers\Shape)#1 (1) {
  ["dims"]=>
  array(3) {
    [0]=>
    int(2)
    [1]=>
    int(3)
    [2]=>
    int(4)
  }
}
end
