--TEST--
Devices of imported DLPack tensors: any device, host accessibility, pass-through
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor, Device, CpuDevice, ForeignDevice};
function live() { $c = bat_counts(); return $c['tensors_made'] - $c['tensors_deleted']; }

$cpu = CpuDevice::get();
try { new ForeignDevice; } catch (Error $e) { echo $e->getMessage(), "\n"; }
echo "an imported CPU tensor is on the CpuDevice\n";
$v = bat_import(1, 8, 1, [4], [1]);
var_dump($v->buffer->device === $cpu, $v->buffer->device);
unset($v);

echo "other devices\n";
$types = [2 => 'cuda', 3 => 'cuda_host', 4 => 'opencl', 7 => 'vulkan', 8 => 'metal', 10 => 'rocm', 11 => 'rocm_host', 13 => 'cuda_managed', 15 => 'webgpu', 99 => 'unknown'];
foreach ($types as $t => $n) {
    $v = bat_import(1, 8, 1, [4], [1], 0, $t, 0, 3, 1, 7);
    $d = $v->buffer->device;
    printf("%-3d %-13s %-14s type=%d id=%d host=%d %s same=%d\n", $t, $n, $d->name, bat_export($v)['device_type'], $d->id, $d->hostAccessible,
        get_class($d), $v->buffer->device === $d);
    unset($v);
}
echo live(), " live\n";

echo "memory the host cannot touch\n";
$v = bat_import(2, 32, 1, [2, 3], [3, 1], 0, 2, 0, 3, 1, 1);   // float32 [2, 3] on cuda:1
var_dump($v->dtype, $v->shape->toArray(), $v->strides, $v->offset, count($v), $v->buffer->byteLength, $v->buffer->isWritable());
$row = $v[1];
var_dump($row->shape->toArray());
$cases = [
    'read leaf'     => fn() => $row[0],
    'write leaf'    => function () use ($row) { $row[0] = 1.0; },
    'read 2-D leaf' => fn() => $v[1][2],
    'isset'         => fn() => isset($row[0]),
    'empty'         => fn() => empty($row[0]),
    'coalesce'      => fn() => $row[0] ?? 0,
    'foreach row'   => function () use ($row) { foreach ($row as $x) {} },
    'read method'   => fn() => $row->offsetGet(0),
    'out of range'  => fn() => $row[5],
    'isset range'   => fn() => isset($row[5]),
];
foreach ($cases as $name => $f) {
    try { $r = $f(); echo "$name: ", var_export($r, true), "\n"; } catch (Throwable $e) { printf("%-14s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
foreach ($v as $i => $r) echo "row $i: ", $r->ndim, "-D\n";
echo "host accessible kinds work normally\n";
foreach ([3, 11, 13] as $t) {
    $h = bat_import(1, 8, 1, [4], [1], 0, $t);
    $h[2] = 200; echo $t, ': ', $h[2], ' ', $h[3], "\n";
}
unset($h);

echo "passing through: the export has the same device and the deleter runs once\n";
$before = live();
$w = bat_roundtrip($v);
var_dump(bat_export($w)['device_type'], $w->buffer->device->id, $w->buffer->device->name, $w->shape->toArray(), $w->strides, $w->dtype);
var_dump($w->buffer === $v->buffer);
echo live() - $before, " new tensors\n";
unset($v, $row, $w, $r, $d, $e, $f, $cases);
echo live(), " live\n";
$c = bat_counts(); echo $c['tensors_made'] === $c['tensors_deleted'] ? "deleters ran exactly once\n" : "DELETER COUNT WRONG\n";
echo "end\n";
--EXPECT--
ForeignDevice cannot be instantiated; it comes from the DLPack import of the C API
an imported CPU tensor is on the CpuDevice
bool(true)
object(Buffers\CpuDevice)#1 (0) {
}
other devices
2   cuda          cuda:7         type=2 id=7 host=0 Buffers\ForeignDevice same=1
3   cuda_host     cuda_host:7    type=3 id=7 host=1 Buffers\ForeignDevice same=1
4   opencl        opencl:7       type=4 id=7 host=0 Buffers\ForeignDevice same=1
7   vulkan        vulkan:7       type=7 id=7 host=0 Buffers\ForeignDevice same=1
8   metal         metal:7        type=8 id=7 host=0 Buffers\ForeignDevice same=1
10  rocm          rocm:7         type=10 id=7 host=0 Buffers\ForeignDevice same=1
11  rocm_host     rocm_host:7    type=11 id=7 host=1 Buffers\ForeignDevice same=1
13  cuda_managed  cuda_managed:7 type=13 id=7 host=1 Buffers\ForeignDevice same=1
15  webgpu        webgpu:7       type=15 id=7 host=0 Buffers\ForeignDevice same=1
99  unknown       device99:7     type=99 id=7 host=0 Buffers\ForeignDevice same=1
0 live
memory the host cannot touch
enum(Buffers\DType::Float32)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(3)
}
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(1)
}
int(0)
int(2)
int(24)
bool(true)
array(1) {
  [0]=>
  int(3)
}
read leaf      Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
write leaf     Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
read 2-D leaf  Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
isset: true
empty          Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
coalesce       Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
foreach row    Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
read method    Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
out of range   Error: Cannot access the elements of a tensor on device cuda:1: its memory is not host accessible; transfer to the host first with to()
isset range: false
row 0: 1-D
row 1: 1-D
host accessible kinds work normally
3: 200 3
11: 200 3
13: 200 3
passing through: the export has the same device and the deleter runs once
int(2)
int(1)
string(6) "cuda:1"
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(3)
}
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(1)
}
enum(Buffers\DType::Float32)
bool(false)
0 new tensors
0 live
deleters ran exactly once
end
