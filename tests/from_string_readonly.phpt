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
?>
--EXPECT--
bool(false)
[1,2]
Error
bool(true)
