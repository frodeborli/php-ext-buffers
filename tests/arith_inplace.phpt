--TEST--
Arithmetic: $a op= $b writes into $a's memory exactly when nothing can observe it
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--INI--
memory_limit=-1
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
$n = 1 << 20;   // 4 MiB of Float32
/** whether $f wrote into the tensor's own buffer: a new result buffer exists while the old one is alive, so its id differs */
function same(Buffers\Tensor &$t, callable $f): bool { $id = spl_object_id($t->buffer); $f(); return spl_object_id($t->buffer) === $id; }
$a = (new Buffer($n * 4))->view(DType::Float32, $n);
echo "alone: ", (same($a, function () use (&$a) { $a += 1.5; }) ? "in place" : "allocated"), " ", $a[0], "\n";
$b = $a;
echo "copy held: ", (same($a, function () use (&$a) { $a += 1.5; }) ? "in place" : "allocated"), " a=", $a[0], " b=", $b[0], "\n";
unset($b);
$view = $a->buffer->view(DType::Float32, 4);
echo "view held: ", (same($a, function () use (&$a) { $a *= 2; }) ? "in place" : "allocated"), " a=", $a[0], " view=", $view[0], "\n";
unset($view);
echo "dtype changes: ", (same($a, function () use (&$a) { $a += Tensor::fromArray([1.0], DType::Float64); }) ? "in place" : "allocated"), " ", $a->dtype->name, "\n";
$i = (new Buffer($n * 4))->view(DType::Int32, $n);
echo "int +=: ", (same($i, function () use (&$i) { $i += 7; }) ? "in place" : "allocated"), " ", $i[5], "\n";
echo "int %= (may throw): ", (same($i, function () use (&$i) { $i %= 4; }) ? "in place" : "allocated"), " ", $i[5], "\n";
echo "a += a: ", (same($i, function () use (&$i) { $i += $i; }) ? "in place" : "allocated"), " ", $i[5], "\n";
$m = Buffer::fromString(str_repeat("\0", 64), false)->view(DType::Int32, 16);
$m2 = $m; unset($m2);
echo "read-only buffer: "; $m += 1; echo $m->buffer->isWritable() ? "writable" : "read-only?", " ", $m[0], "\n";
--EXPECT--
alone: in place 1.5
copy held: allocated a=3 b=1.5
view held: allocated a=6 view=3
dtype changes: allocated Float64
int +=: in place 7
int %= (may throw): allocated 3
a += a: in place 6
read-only buffer: writable 1
