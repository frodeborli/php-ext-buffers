--TEST--
Strided and offset views, transposes, empty views
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$b = new ArrayBuffer(48);
$raw = $b->view(DType::UInt8);
for ($i = 0; $i < 48; $i++) $raw[$i] = $i;
$m = $b->view(DType::UInt8, [4, 6]);
$t = $b->view(DType::UInt8, [6, 4], [1, 6]);     // transpose of $m
for ($i = 0; $i < 6; $i++) { $row = []; for ($j = 0; $j < 4; $j++) $row[] = $t[$i][$j]; echo implode(' ', $row), "\n"; }
var_dump($t[5][3] === $m[3][5], $t->strides());
$every2 = $b->view(DType::UInt8, 12, [4]);       // every 4th byte
var_dump($every2[3], $every2[11], $every2->strides());
$every2[3] = 99; var_dump($raw[12]);
$sub = $b->view(DType::UInt16, [2, 3], [8, 2], 4);   // offset 4 bytes, row stride 8 elements
var_dump($sub->byteOffset(), $sub->strides(), $sub[1][2] === ($raw[28] | ($raw[29] << 8)));
$s = $sub[1];
var_dump($s->byteOffset(), $s->strides(), $s[2]);
$e = $b->view(DType::UInt8, [0, 5]);
var_dump(count($e), $e->shape(), isset($e[0]), iterator_to_array($e));
try { $e[0]; } catch (ValueError $ex) { echo $ex->getMessage(), "\n"; }
$e2 = $b->view(DType::UInt8, [3, 0]); var_dump($e2[2]->shape(), count($e2[1]));
// column views of a 2D array via a manual offset
$col = $b->view(DType::UInt8, 4, [6], 2);
var_dump(iterator_to_array($col));
// negative-stride style reversal is not available: strides must be >= 1
// 8 dimensions
$big = new ArrayBuffer(256);
$d8 = $big->view(DType::UInt8, [2,2,2,2,2,2,2,2]);
$d8[1][1][1][1][1][1][1][1] = 5;
var_dump($d8->ndim(), $big->toString()[255] === "\5", $d8[1][1][1][1][1][1][1]->ndim(), $d8->strides());
--EXPECT--
0 6 12 18
1 7 13 19
2 8 14 20
3 9 15 21
4 10 16 22
5 11 17 23
bool(true)
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(6)
}
int(12)
int(44)
array(1) {
  [0]=>
  int(4)
}
int(99)
int(4)
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(2)
}
bool(true)
int(20)
array(1) {
  [0]=>
  int(2)
}
int(7452)
int(0)
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(5)
}
bool(false)
array(0) {
}
Index 0 is out of range for an axis of length 0
array(1) {
  [0]=>
  int(0)
}
int(0)
array(4) {
  [0]=>
  int(2)
  [1]=>
  int(8)
  [2]=>
  int(14)
  [3]=>
  int(20)
}
int(8)
bool(true)
int(1)
array(8) {
  [0]=>
  int(128)
  [1]=>
  int(64)
  [2]=>
  int(32)
  [3]=>
  int(16)
  [4]=>
  int(8)
  [5]=>
  int(4)
  [6]=>
  int(2)
  [7]=>
  int(1)
}
