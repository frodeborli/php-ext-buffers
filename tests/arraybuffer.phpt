--TEST--
Buffer: construction, zero fill, strings, errors
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer};
$b = new Buffer(10);
var_dump($b->byteLength, $b->isWritable(), $b instanceof Buffer, bin2hex($b->toString()));
$z = new Buffer(0);
var_dump($z->byteLength, $z->toString());
$s = Buffer::fromString("héllo\0x");
var_dump($s->byteLength, bin2hex($s->toString()));
$v = $s->view(DType::UInt8);
$v[0] = 72;
var_dump($s->toString() === "H\xc3\xa9llo\0x");
foreach ([fn() => new Buffer(-1), fn() => new Buffer(PHP_INT_MAX), fn() => $b->__construct(4), fn() => clone $b,
          fn() => serialize($b), fn() => ($b->x = 1), fn() => new Buffer("a")] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ": ", $e->getMessage(), "\n"; }
}
// the allocation counts against memory_limit
ini_set('memory_limit', '16M');
$code = 'ini_set("memory_limit","16M"); try { $x = new Buffer(64*1024*1024); echo "allocated"; } catch (Throwable $e) { echo "threw"; }';
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
ValueError: Buffers\Buffer::__construct(): Argument #1 ($byteLength) must be greater than or equal to 0
ValueError: Buffers\Buffer::__construct(): Argument #1 ($byteLength) is too large
Error: Buffer is already constructed
Error: Trying to clone an uncloneable object of class Buffers\Buffer
Exception: Serialization of 'Buffers\Buffer' is not allowed
Error: Cannot create dynamic property Buffers\Buffer::$x
TypeError: Buffers\Buffer::__construct(): Argument #1 ($byteLength) must be of type int, string given
ok
