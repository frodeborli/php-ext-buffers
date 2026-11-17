--TEST--
Buffers\Compiled: every operator on every pair of dtypes equals the eager operator, bit for bit
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras'); ?>
--FILE--
<?php
include __DIR__ . '/oracle.inc';
--EXPECT--
more than 1000 cases, mismatches 0
