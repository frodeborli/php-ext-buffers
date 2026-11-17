--TEST--
Buffers\Compiled: random expressions over tensor elements equal the same expressions evaluated with the operators
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras'); ?>
--FILE--
<?php
include __DIR__ . '/fused.inc';
--EXPECT--
expressions 150, mismatches 0
