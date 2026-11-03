--TEST--
Tensor cannot be implemented in PHP code
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
class Mine implements Buffers\Tensor {
    public ?Buffers\DType $dtype { get => null; }
    public Buffers\Shape $shape { get => new Buffers\Shape([]); }
    public int $ndim { get => 0; }
}
echo "not reached\n";
--EXPECTF--
Fatal error: Class Mine cannot implement interface Buffers\Tensor: only internal classes can implement it in %s on line %d
