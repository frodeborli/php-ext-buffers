--TEST--
C API: transfers - Tensor::to() with a registered backend, and the errors without one
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor, CpuDevice, FileDevice};
function transfers() { return bat_counts()['transfers']; }
$cpu = CpuDevice::get();
$dev = bat_device_buffer(8)->device;        // BatDevice: "bat:5", not host accessible, DLPack type cuda
var_dump(get_class($dev), $dev->name, $dev->id, $dev->hostAccessible);

$b = new Buffer(96);
$f = $b->view(DType::Float32, [3, 4], [8, 2], 4);
for ($i = 0; $i < 3; $i++) for ($j = 0; $j < 4; $j++) $f[$i][$j] = $i * 10 + $j + 0.5;
echo "host to the registered device and back\n";
$d = $f->to($dev);
var_dump($d === $f, $d->buffer->device === $dev, $d->buffer->isHostAccessible(), $d->shape->toArray(), $d->strides, $d->dtype, transfers());
var_dump($d->to($dev) === $d, transfers());
try { $d[0][0]; } catch (Error $e) { echo $e->getMessage(), "\n"; }
$h = $d->to($cpu);
var_dump($h->buffer->device === $cpu, $h->buffer->isHostAccessible(), transfers(), json_encode($h) === json_encode($f), $h->to($cpu) === $h);
echo "a tensor that is already on the host is returned as it is\n";
$h2 = $f->to($cpu);
var_dump($h2 === $f, $h2->strides, $h2->buffer->byteLength);

echo "packed, 0-d and empty tensors\n";
$p = (new Buffer(16))->view(DType::Int4, [3, 3], [4, 1]);
for ($i = 0; $i < 3; $i++) for ($j = 0; $j < 3; $j++) $p[$i][$j] = $i * 3 + $j - 4;
$pd = $p->to($dev);
var_dump($pd->strides, json_encode($pd->to($cpu)));
$s = (new Buffer(8))->view(DType::Int32, [], null, 4); $s->value = 77;
var_dump($s->to($dev)->to($cpu)->value);
var_dump((new Buffer(8))->view(DType::Float32, [0, 2])->to($dev)->shape->toArray());

echo "files\n";
$path = tempnam(sys_get_temp_dir(), 'buf'); file_put_contents($path, "\x01\x02\x03\x04\x05\x06\x07\x08");
foreach ([true, false] as $map) {
    $t = Buffer::fromFile($path, 0, null, $map)->view(DType::UInt8, [2, 2], [4, 1], 1);
    $u = $t->to($dev);
    var_dump($u->buffer->device === $dev, json_encode($u->to($cpu)), transfers());
}
unlink($path);

echo "no backend\n";
$foreign = bat_import(1, 8, 1, [2], [1], 0, 99, 0, 3, 1, 4);
foreach ([fn() => $f->to(FileDevice::get()), fn() => $f->to($foreign->buffer->device), fn() => $foreign->to($cpu), fn() => $foreign->to($dev), fn() => $d->to(FileDevice::get())] as $fn) {
    try { $fn(); echo "no error\n"; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
var_dump($foreign->to($foreign->buffer->device) === $foreign);
try { $f->to(new stdClass); } catch (TypeError $e) { echo "TypeError\n"; }
unset($d, $h, $h2, $pd, $s, $u, $t, $foreign, $e, $fn);
$c = bat_counts();
echo $c['made'] === $c['deleted'] ? "deleters ran exactly once\n" : "DELETER COUNT WRONG\n";
echo "end\n";
--EXPECT--
string(9) "BatDevice"
string(5) "bat:5"
int(5)
bool(false)
host to the registered device and back
bool(false)
bool(true)
bool(false)
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
enum(Buffers\DType::Float32)
int(1)
bool(true)
int(1)
Cannot access the elements of a tensor on device cuda:5: its memory is not host accessible; transfer to the host first with to()
bool(true)
bool(true)
int(2)
bool(true)
bool(true)
a tensor that is already on the host is returned as it is
bool(true)
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(2)
}
int(96)
packed, 0-d and empty tensors
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(1)
}
string(29) "[[-4,-3,-2],[-1,0,1],[2,3,4]]"
int(77)
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(2)
}
files
bool(true)
string(13) "[[2,3],[6,7]]"
int(9)
bool(true)
string(13) "[[2,3],[6,7]]"
int(11)
no backend
Error: no backend for device 'file'
Error: no backend for device 'device99:4'
Error: no backend for device 'device99:4'
Error: no backend for device 'device99:4'
Error: bat transfer: only to BatDevice or the CPU
bool(true)
TypeError
deleters ran exactly once
end
