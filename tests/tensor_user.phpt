--TEST--
TensorInterface cannot be implemented in PHP code
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--XLEAK--
The fatal error aborts class linking; PHP's bailout leaves the compiled script, the error message and the half-linked class to process exit (Zend, not ext/buffers).
--FILE--
<?php
class Mine implements Buffers\TensorInterface {
    public ?Buffers\DType $dtype { get => null; }
    public Buffers\Shape $shape { get => new Buffers\Shape([]); }
    public int $ndim { get => 0; }
}
echo "not reached\n";
--EXPECTF--
Fatal error: Class Mine cannot implement interface Buffers\TensorInterface: only internal classes can implement it in %s on line %d
