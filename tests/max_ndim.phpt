--TEST--
Tensor::MAX_NDIM: the most axes a tensor can have
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
var_dump(Tensor::MAX_NDIM);
$b = new Buffer(1);
var_dump($b->view(DType::UInt8, array_fill(0, Tensor::MAX_NDIM, 1))->ndim);
try { $b->view(DType::UInt8, array_fill(0, Tensor::MAX_NDIM + 1, 1)); } catch (ValueError $e) { echo get_class($e), "\n"; }
?>
--EXPECT--
int(8)
int(8)
ValueError
