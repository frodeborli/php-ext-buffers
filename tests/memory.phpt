--TEST--
ArrayBuffer allocations are PHP-managed memory (counted by memory_get_usage and memory_limit)
--SKIPIF--
<?php
if (!extension_loaded('buffers')) die('skip buffers extension not loaded');
if (getenv('USE_ZEND_ALLOC') === '0') die('skip memory_get_usage() is not tracked without the Zend allocator');
?>
--FILE--
<?php
use Buffers\{DType, ArrayBuffer};
$before = memory_get_usage();
$b = new ArrayBuffer(4 * 1024 * 1024);
$during = memory_get_usage();
var_dump($during - $before >= 4 * 1024 * 1024, $during - $before < 8 * 1024 * 1024);
unset($b);
var_dump(memory_get_usage() - $before < 65536);
// views add a few hundred bytes, not a copy of the data
$b = new ArrayBuffer(4 * 1024 * 1024);
$before = memory_get_usage();
$v = $b->view(DType::Float64, [512, 1024]);
var_dump(memory_get_usage() - $before < 65536);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
