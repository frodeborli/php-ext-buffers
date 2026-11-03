--TEST--
ArrayBuffer allocations are PHP-managed memory (counted by memory_get_usage and memory_limit)
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$before = memory_get_usage();
$b = new ArrayBuffer(4 * 1024 * 1024);
$during = memory_get_usage();
var_dump($during - $before >= 4 * 1024 * 1024, $during - $before < 4 * 1024 * 1024 + 4096);
unset($b);
var_dump(memory_get_usage() - $before < 4096);
// views add a few hundred bytes, not a copy of the data
$b = new ArrayBuffer(4 * 1024 * 1024);
$before = memory_get_usage();
$v = $b->view(DType::Float64, [512, 1024]);
var_dump(memory_get_usage() - $before < 1024);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
