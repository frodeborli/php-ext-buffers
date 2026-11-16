--TEST--
PHP 8.3: the computed properties are served by internal __get/__set/__isset/__unset (no property hooks before 8.4)
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_VERSION_ID >= 80400) die('skip PHP 8.3 only: 8.4+ uses property hooks (props.phpt)'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, DataView, CpuDevice, Shape};
function attempt(callable $f): void {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
$b = new Buffer(64);
$v = $b->view(DType::Int32, [3, 4], null, 8);
echo "Tensor\n";
var_dump($v->dtype, $v->ndim, $v->shape->toArray(), $v->strides, $v->offset, $v->buffer === $b);
var_dump(isset($v->dtype), isset($v->value), isset($v->nope), $v->value ?? 'no value', $v->nope ?? 'missing');
attempt(fn() => $v->value);
attempt(function () use ($v) { $v->dtype = DType::Int8; });
attempt(function () use ($v) { $v->nope = 1; });
attempt(function () use ($v) { unset($v->ndim); });
unset($v->nope);
echo "0-d value, read and write\n";
$s = $b->view(DType::Int32, [], null, 12);
$s->value = 9;
var_dump($s->value, $v[0][1], isset($s->value));
attempt(function () use ($s) { $s->value = 'x'; });
echo "undefined property\n";
var_dump($v->nope);
echo "Buffer, Device, Shape, DataView\n";
var_dump($b->byteLength, $b->device === CpuDevice::get(), $b->device->name, $b->device->id, $b->device->hostAccessible);
$sh = new Shape([2, 3]);
var_dump($sh->concrete, $sh->size);
$d = new DataView($b, 4, 16);
var_dump($d->byteOffset, $d->byteLength, $d->buffer === $b, $d->uint16 === $d->uint16, $d->uint16->dtype);
$d->uint16[1] = 0xCCDD;
var_dump(dechex($d->uint16[1]), $d->uint8[1]);
attempt(function () use ($d) { $d->byteLength = 1; });
echo "not declared on PHP 8.3\n";
var_dump(property_exists($v, 'dtype'), get_object_vars($b), method_exists($v, '__get'));
echo "creating sub-views allocates nothing\n";
for ($i = 0; $i < 100; $i++) { $x = $v[$i % 3]; $y = $v[1][2]; }
unset($x, $y);
gc_collect_cycles();
$before = memory_get_usage();
for ($i = 0; $i < 100000; $i++) { $x = $v[$i % 3]; $y = $v[($i + 1) % 3][$i % 4]; $n = $x->ndim; }
unset($x, $y);
var_dump(memory_get_usage() - $before);
echo "end\n";
--EXPECTF--
Tensor
enum(Buffers\DType::Int32)
int(2)
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
int(8)
bool(true)
bool(true)
bool(false)
bool(false)
string(8) "no value"
string(7) "missing"
Error: value is only available on 0-d views; index the view instead
Error: Property Buffers\Tensor::$dtype is read-only
Error: Cannot create dynamic property Buffers\Tensor::$nope
Error: Cannot unset property Buffers\Tensor::$ndim
0-d value, read and write
int(9)
int(9)
bool(true)
TypeError: Cannot write string to a Tensor of dtype Int32
undefined property

Warning: Undefined property: Buffers\Tensor::$nope in %s on line %d
NULL
Buffer, Device, Shape, DataView
int(64)
bool(true)
string(3) "cpu"
int(0)
bool(true)
bool(true)
int(6)
int(4)
int(16)
bool(true)
bool(true)
enum(Buffers\DType::UInt16)
string(4) "ccdd"
int(221)
Error: Property Buffers\DataView::$byteLength is read-only
not declared on PHP 8.3
bool(false)
array(0) {
}
bool(false)
creating sub-views allocates nothing
int(0)
end
