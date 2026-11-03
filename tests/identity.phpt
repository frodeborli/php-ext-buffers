--TEST--
Objects are equal only to themselves
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$b = new ArrayBuffer(8);
$a = $b->view(DType::UInt8);
$c = $b->view(DType::UInt8);
$d = $a;
var_dump($a == $c, $a === $c, $a == $d, $a === $d, $a != $c, $a < $c, $a > $c, $a <=> $d);
$e = new ArrayBuffer(8);
var_dump($b == $e, $b == $b, $b->view(DType::Int8) == $b->view(DType::Int8));
var_dump($a == null, $a == []);
var_dump(in_array($c, [$a]), in_array($a, [$a]), array_search($c, [$a, $c]));
?>
--EXPECT--
bool(false)
bool(false)
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
int(0)
bool(false)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
int(1)
