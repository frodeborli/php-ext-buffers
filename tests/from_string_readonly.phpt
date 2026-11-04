--TEST--
Buffer::fromString() with $writable = false is read-only
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType};
$b = Buffer::fromString(pack('f*', 1, 2), false);
var_dump($b->isWritable());
$t = $b->view(DType::Float32, [2]);
echo json_encode($t->jsonSerialize()), "\n";
try { $t[0] = 5.0; } catch (Throwable $e) { echo get_class($e), "\n"; }
var_dump(Buffer::fromString("ab")->isWritable());
$w = Buffer::fromString(pack('f*', 1, 2));
$r = $w->readOnly();
var_dump($r->isWritable(), $w->isWritable(), $r->readOnly() === $r);
$w->view(DType::Float32, [2])[0] = 9.0;   // seen through the alias
echo json_encode($r->view(DType::Float32, [2])->jsonSerialize()), "\n";
unset($w);
echo json_encode($r->view(DType::Float32, [2])->jsonSerialize()), "\n";   // the alias keeps the memory alive
?>
--EXPECT--
bool(false)
[1,2]
Error
bool(true)
bool(false)
bool(true)
bool(true)
[9,2]
[9,2]
