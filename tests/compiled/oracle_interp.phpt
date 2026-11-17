--TEST--
Buffers\Compiled: the interpreter (the backend of CPUs without native code) gives the native code's bits: the oracle and random expressions
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras'); ?>
--ENV--
BUFFERS_CC_INTERP=1
--FILE--
<?php
ob_start();
include __DIR__ . '/oracle.inc';
include __DIR__ . '/fused.inc';
echo ob_get_clean();
--EXPECT--
more than 1000 cases, mismatches 0
expressions 150, mismatches 0
