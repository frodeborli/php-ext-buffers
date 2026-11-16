--TEST--
PHP 8.3: TensorInterface cannot be implemented in PHP code
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_VERSION_ID >= 80400) die('skip PHP 8.3 only (tensor_user.phpt)'); ?>
--XLEAK--
The fatal error aborts class linking; PHP's bailout leaves the compiled script, the error message and the half-linked class to process exit (Zend, not ext/buffers).
--FILE--
<?php
class Mine implements Buffers\TensorInterface {
}
echo "not reached\n";
--EXPECTF--

Fatal error: Class Mine cannot implement interface Buffers\TensorInterface: only internal classes can implement it in %s on line %d
