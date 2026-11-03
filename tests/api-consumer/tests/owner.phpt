--TEST--
C API: a buffer over memory owned by a PHP object (the owner stays alive while views exist)
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, BufferView};
function owners() { $c = bat_counts(); return "made " . $c['owners_made'] . " freed " . $c['owners_freed']; }

$m = new BatMatrix(3, 4);
$wr = WeakReference::create($m);
$v = $m->toView();
var_dump($v->dtype, $v->shape->toArray(), $v->strides, $v->offset, $v->buffer->byteLength(), $v->buffer->isWritable(), $v->buffer->device() === Buffers\CpuDevice::get());
var_dump($v[0][0], $v[1][2], $v[2][3], $m->get(1 * 4 + 2) === $v[1][2]);
echo "writes through a read-only view\n";
foreach ([fn() => $v[0][0] = 1.0, fn() => $v[0] = 1.0, fn() => $v->buffer->view(DType::Float64, 12)[0] = 1.0] as $f) {
    try { $f(); echo "no error\n"; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
var_dump($v[0][0]);
unset($f, $e);   // the closures hold $v

echo "the owner lives as long as a view does\n";
unset($m);
var_dump($wr->get() !== null);
echo owners(), "\n";
$row = $v[2];
unset($v);
var_dump($wr->get() !== null, $row[3]);     // a sub-view holds the buffer too
echo owners(), "\n";
unset($row);
var_dump($wr->get());
echo owners(), "\n";

echo "writable views write into the owner\n";
$m = new BatMatrix(2, 2);
$w = $m->toView(true);
$w[1][1] = 42.5;
var_dump($m->get(3), $w->buffer->isWritable());
echo "several views of one owner\n";
$a = $m->toView(); $b = $m->toView();
var_dump($a->buffer === $b->buffer);
unset($m, $w, $a);
echo owners(), "\n";
var_dump($b[0][1]);
unset($b);
echo owners(), "\n";
echo "DLPack keeps it alive too\n";
$m = new BatMatrix(1, 3);
$wr = WeakReference::create($m);
$i = bat_roundtrip($m->toView());
unset($m);
var_dump($wr->get() !== null);
var_dump($i[0][2], $i->buffer->isWritable());
unset($i);
var_dump($wr->get());
echo owners(), "\n";
echo "end\n";
--EXPECT--
enum(Buffers\DType::Float64)
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(4)
}
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(1)
}
int(0)
int(96)
bool(false)
bool(true)
float(0.5)
float(6.5)
float(11.5)
bool(true)
writes through a read-only view
Error: Cannot write to a read-only BufferView
Error: Cannot write to a read-only BufferView
Error: Cannot write to a read-only BufferView
float(0.5)
the owner lives as long as a view does
bool(true)
made 1 freed 0
bool(true)
float(11.5)
made 1 freed 0
NULL
made 1 freed 1
writable views write into the owner
float(42.5)
bool(true)
several views of one owner
bool(false)
made 2 freed 1
float(1.5)
made 2 freed 2
DLPack keeps it alive too
bool(true)
float(2.5)
bool(false)
NULL
made 3 freed 3
end
