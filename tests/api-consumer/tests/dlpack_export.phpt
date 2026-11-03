--TEST--
DLPack export and round trip
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer, Tensor};
$b = new Buffer(96);
$raw = $b->view(DType::UInt8);
for ($i = 0; $i < 96; $i++) $raw[$i] = $i;
$wr = WeakReference::create($b);

$v = $b->view(DType::Int16, [3, 4], [8, 2], 6);   // strided, offset
$w = bat_roundtrip($v);
var_dump($w->dtype, $w->shape->toArray(), $w->strides, $w->offset , $w->buffer instanceof Buffer);
// same memory: writes are visible in both directions
var_dump($w[2][3] === $v[2][3]);
$w[2][3] = 12345; var_dump($v[2][3]);
$v[0][0] = -2; var_dump($w[0][0]);
// the export keeps the original buffer alive
unset($v, $b, $raw);
var_dump($wr->get() !== null, $w[2][3]);
unset($w, $cap);
var_dump($wr->get());

echo "every dtype round trips\n";
foreach (DType::cases() as $c) {
    if (PHP_INT_SIZE === 4 && in_array($c, [DType::Int64, DType::UInt64, DType::UInt32])) continue;   // not viewable on 32-bit PHP
    $buf = new Buffer(128);
    $sh = $c->byteSize() === null ? [2, 8] : [2, 3]; // packed rows must be whole bytes
    $x = $buf->view($c, $sh);
    $y = bat_roundtrip($x);
    if ($y->dtype !== $c || $y->shape->toArray() !== $sh || $y->strides !== [$sh[1], 1]) echo "MISMATCH ", $c->name, "\n";
}
echo "ok\n";

echo "sub-views export their own window\n";
$buf = new Buffer(24);
$raw = $buf->view(DType::UInt8); for ($i = 0; $i < 24; $i++) $raw[$i] = $i;
$m = $buf->view(DType::UInt8, [2, 3, 4]);
$s = bat_roundtrip($m[1][2]);
var_dump($s->shape->toArray(), $s->offset, iterator_to_array($s));
$s = bat_roundtrip($buf->view(DType::UInt8, [3], [4], 1));
var_dump($s->strides, iterator_to_array($s));

echo "empty views\n";
$e = bat_roundtrip($buf->view(DType::Int32, [0, 3]));
var_dump($e->shape->toArray(), $e->strides);

echo "read-only tensors stay read-only through a round trip\n";
$ro = bat_import(1, 8, 1, [4], [1], 1);
$rt = bat_roundtrip($ro);
var_dump($rt->buffer->isWritable());
try { $rt[0] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }

echo "end\n";
--EXPECT--
enum(Buffers\DType::Int16)
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(4)
}
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(2)
}
int(0)
bool(true)
bool(true)
int(12345)
int(-2)
bool(true)
int(12345)
NULL
every dtype round trips
ok
sub-views export their own window
array(1) {
  [0]=>
  int(4)
}
int(0)
array(4) {
  [0]=>
  int(20)
  [1]=>
  int(21)
  [2]=>
  int(22)
  [3]=>
  int(23)
}
array(1) {
  [0]=>
  int(4)
}
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(5)
  [2]=>
  int(9)
}
empty views
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(3)
}
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(1)
}
read-only tensors stay read-only through a round trip
bool(false)
Cannot write to a read-only Tensor
end
