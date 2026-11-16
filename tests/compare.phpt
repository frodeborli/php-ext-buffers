--TEST--
Comparisons: Tensor, Buffer, DataView and devices compare like enum cases (identity, otherwise uncomparable, no notice)
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, CpuDevice, DataView, DType, Tensor};
$t = Tensor::fromArray([1]); $u = Tensor::fromArray([1]);
var_dump($t == $t, $t == $u, $t != $u, $t == 1, 1 == $t, $t == 1.0, $t == '1', $t == null, $t == [], $t == true, $t == false);
var_dump($t < 5, $t > 5, $t < $u, $t > $u, $t <= $t, $t <=> $u, $u <=> $t);
var_dump(in_array(1, [$t]), in_array($t, [$u]), in_array($t, [$u, $t]), array_search($t, [$u, $t]), (bool)$t, ($t ?: 'falsy') === $t);
$b = new Buffer(8); $d = new DataView($b);
var_dump($b == $b, $b == new Buffer(8), $b == 8, $d == $d, $d == new DataView($b), $d->uint8 == $d->uint8, $d->uint8 == 0, CpuDevice::get() == CpuDevice::get(), CpuDevice::get() == 'cpu');
echo "no notices\n";
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
int(1)
int(1)
bool(false)
bool(false)
bool(true)
int(1)
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
no notices
