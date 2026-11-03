--TEST--
TypedView: chained offsets, sub-views, aliasing, offset errors
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$b = new ArrayBuffer(24);
$v = $b->view(DType::UInt8, [2, 3, 4]);
for ($i = 0; $i < 24; $i++) $b->view(DType::UInt8)[$i] = $i;
var_dump($v[1][2][3], $v[0][0][0], $v[1][0][0]);
$plane = $v[1];                       // sub-view
var_dump($plane instanceof TypedView, $plane->shape(), $plane->strides(), $plane->byteOffset(), $plane[2][1]);
$row = $v[1][2];
var_dump($row->shape(), $row->byteOffset(), $row[3], count($row));
$row[0] = 200;                        // writes through to the root
var_dump($v[1][2][0], $b->toString()[20] === chr(200));
$v[0][1][2] = 77; var_dump($plane->byteOffset(), $v[0][1][2]);
$v[1][1][1] += 5; var_dump($v[1][1][1]);
$x = 3; var_dump($v[$x - 2][$x - 1][$x]);
// the sub-view survives its parent
unset($v, $plane);
var_dump($row[3]);
// isset / empty / ??
$v = $b->view(DType::UInt8, [2, 3, 4]);
var_dump(isset($v[0]), isset($v[1][2][3]), isset($v[2]), isset($v[-1]), isset($v["1"]), isset($v[1.0]), isset($v[0][0][4]));
var_dump(empty($v[0][0][0]), empty($v[0][0][1]), $v[5][0] ?? 'dflt', $v[1][9] ?? 'dflt', $v[1][2][3] ?? 'dflt');
// errors
foreach ([
    fn() => $v[2], fn() => $v[-1], fn() => $v[0][3], fn() => $v[0][0][4], fn() => $v[PHP_INT_MAX], fn() => $v[PHP_INT_MIN],
    fn() => $v[[0, 1]], fn() => $v["1"], fn() => $v[1.5], fn() => $v[null], fn() => $v[true], fn() => $v[new stdClass],
    function() use ($v) { $v[] = 1; }, function() use ($v) { $v[0] = 1; }, function() use ($v) { $v[0][0] = 1; },
    function() use ($v) { $v[9][0][0] = 1; }, function() use ($v) { $v[0][0][9] = 1; }, function() use ($v) { $v[[0,0,0]] = 1; },
    function() use ($v) { unset($v[0][0][0]); }, function() use ($v) { unset($v[0]); },
    function() use ($v) { $v->foo = 1; }, fn() => clone $v, fn() => serialize($v), fn() => new TypedView(),
] as $i => $f) {
    try { $f(); echo "$i: no error\n"; } catch (Throwable $e) { echo "$i: ", get_class($e), ": ", $e->getMessage(), "\n"; }
}
set_error_handler(function ($no, $msg) { echo "notice: $msg\n"; return true; });
$v[0][0][0]++;
echo "done\n";
--EXPECT--
int(23)
int(0)
int(12)
bool(true)
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(4)
}
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(1)
}
int(12)
int(21)
array(1) {
  [0]=>
  int(4)
}
int(20)
int(23)
int(4)
int(200)
bool(true)
int(12)
int(77)
int(22)
int(23)
int(23)
bool(true)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
bool(false)
string(4) "dflt"
string(4) "dflt"
int(23)
0: ValueError: Index 2 is out of range for an axis of length 2
1: ValueError: Index -1 is out of range for an axis of length 2
2: ValueError: Index 3 is out of range for an axis of length 3
3: ValueError: Index 4 is out of range for an axis of length 4
4: ValueError: Index 9223372036854775807 is out of range for an axis of length 2
5: ValueError: Index -9223372036854775808 is out of range for an axis of length 2
6: TypeError: TypedView offset must be of type int, array given
7: TypeError: TypedView offset must be of type int, string given
8: TypeError: TypedView offset must be of type int, float given
9: TypeError: TypedView offset must be of type int, null given
10: TypeError: TypedView offset must be of type int, bool given
11: TypeError: TypedView offset must be of type int, stdClass given
12: Error: Cannot append to a TypedView
13: Error: Cannot assign to a sub-view; index every axis down to one element
14: Error: Cannot assign to a sub-view; index every axis down to one element
15: ValueError: Index 9 is out of range for an axis of length 2
16: ValueError: Index 9 is out of range for an axis of length 4
17: TypeError: TypedView offset must be of type int, array given
18: Error: Cannot unset an element of a TypedView
19: Error: Cannot unset an element of a TypedView
20: Error: Cannot create dynamic property TypedView::$foo
21: Error: Trying to clone an uncloneable object of class TypedView
22: Exception: Serialization of 'TypedView' is not allowed
23: Error: TypedView cannot be instantiated; use BufferInterface::view()
notice: Indirect modification of overloaded element of TypedView has no effect
done
