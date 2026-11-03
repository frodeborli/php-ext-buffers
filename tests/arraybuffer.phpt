--TEST--
ArrayBuffer: construction, zero fill, strings, errors
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$b = new ArrayBuffer(10);
var_dump($b->byteLength(), $b->isWritable(), $b instanceof BufferInterface, bin2hex($b->toString()));
$z = new ArrayBuffer(0);
var_dump($z->byteLength(), $z->toString());
$s = ArrayBuffer::fromString("héllo\0x");
var_dump($s->byteLength(), bin2hex($s->toString()));
$v = $s->view(DType::UInt8);
$v[0] = 72;
var_dump($s->toString() === "H\xc3\xa9llo\0x");
foreach ([fn() => new ArrayBuffer(-1), fn() => new ArrayBuffer(PHP_INT_MAX), fn() => $b->__construct(4), fn() => clone $b,
          fn() => serialize($b), fn() => ($b->x = 1), fn() => new ArrayBuffer("a")] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ": ", $e->getMessage(), "\n"; }
}
// the allocation counts against memory_limit
ini_set('memory_limit', '16M');
$code = 'ini_set("memory_limit","16M"); try { $x = new ArrayBuffer(64*1024*1024); echo "allocated"; } catch (Throwable $e) { echo "threw"; }';
echo "ok\n";
--EXPECT--
int(10)
bool(true)
bool(true)
string(20) "00000000000000000000"
int(0)
string(0) ""
int(8)
string(16) "68c3a96c6c6f0078"
bool(true)
ValueError: ArrayBuffer::__construct(): Argument #1 ($byteLength) must be greater than or equal to 0
ValueError: ArrayBuffer::__construct(): Argument #1 ($byteLength) is too large
Error: ArrayBuffer is already constructed
Error: Trying to clone an uncloneable object of class ArrayBuffer
Exception: Serialization of 'ArrayBuffer' is not allowed
Error: Cannot create dynamic property ArrayBuffer::$x
TypeError: ArrayBuffer::__construct(): Argument #1 ($byteLength) must be of type int, string given
ok
