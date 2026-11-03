--TEST--
DLPack export and round trip
--SKIPIF--
<?php if (!extension_loaded('buffers') || !function_exists('buffers_test_make')) die('skip needs a build with --enable-buffers-testing'); ?>
--FILE--
<?php
use Buffers\{DType, ArrayBuffer, BufferView, DLPackBuffer, DLPackCapsule};
$b = new ArrayBuffer(96);
$raw = $b->view(DType::UInt8);
for ($i = 0; $i < 96; $i++) $raw[$i] = $i;
$wr = WeakReference::create($b);

$v = $b->view(DType::Int16, [3, 4], [8, 2], 6);   // strided, offset
$cap = $v->toDLPack();
var_dump($cap instanceof DLPackCapsule);
$w = BufferView::fromDLPack($cap);
var_dump($w->dtype, $w->shape->toArray(), $w->strides, $w->offset , $w->buffer instanceof DLPackBuffer);
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
    $buf = new ArrayBuffer(128);
    $sh = $c->byteSize() === null ? [2, 8] : [2, 3]; // packed rows must be whole bytes
    $x = $buf->view($c, $sh);
    $y = BufferView::fromDLPack($x->toDLPack());
    if ($y->dtype !== $c || $y->shape->toArray() !== $sh || $y->strides !== [$sh[1], 1]) echo "MISMATCH ", $c->name, "\n";
}
echo "ok\n";

echo "sub-views export their own window\n";
$buf = new ArrayBuffer(24);
$raw = $buf->view(DType::UInt8); for ($i = 0; $i < 24; $i++) $raw[$i] = $i;
$m = $buf->view(DType::UInt8, [2, 3, 4]);
$s = BufferView::fromDLPack($m[1][2]->toDLPack());
var_dump($s->shape->toArray(), $s->offset, iterator_to_array($s));
$s = BufferView::fromDLPack($buf->view(DType::UInt8, [3], [4], 1)->toDLPack());
var_dump($s->strides, iterator_to_array($s));

echo "empty views\n";
$e = BufferView::fromDLPack($buf->view(DType::Int32, [0, 3])->toDLPack());
var_dump($e->shape->toArray(), $e->strides);

echo "read-only tensors stay read-only through a round trip\n";
$ro = BufferView::fromDLPack(buffers_test_make(1, 8, 1, [4], [1], 1));
$rt = BufferView::fromDLPack($ro->toDLPack());
var_dump($rt->buffer->isWritable());
try { $rt[0] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }

echo "unconsumed capsules release the buffer\n";
$buf = new ArrayBuffer(8); $wr = WeakReference::create($buf);
$cap = $buf->view(DType::UInt8)->toDLPack(); unset($buf);
var_dump($wr->get() !== null); unset($cap); var_dump($wr->get());
echo "end\n";
--EXPECT--
bool(true)
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
Cannot write to a read-only BufferView
unconsumed capsules release the buffer
bool(true)
NULL
end
