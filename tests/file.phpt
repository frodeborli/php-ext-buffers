--TEST--
Buffer::fromFile(): mapped and unmapped file buffers, Tensor::to()
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_OS_FAMILY === 'Windows') die('skip POSIX paths'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor, CpuDevice, FileDevice};

/* a file: 4 header bytes, 20 bytes of padding up to offset 24, then float32 [1.5, -2.25, 3, 4.5], bfloat16 [1, 2, 3], int4 [-8..7], at page-unfriendly offsets */
$mem = new Buffer(8192 + 100);
$f32 = $mem->view(DType::Float32, 4, null, 24);
foreach ([1.5, -2.25, 3.0, 4.5] as $i => $x) $f32[$i] = $x;
$bf = $mem->view(DType::BFloat16, 3, null, 40);
foreach ([1.0, 2.0, 3.0] as $i => $x) $bf[$i] = $x;
$i4 = $mem->view(DType::Int4, 16, null, 46);
for ($i = 0; $i < 16; $i++) $i4[$i] = $i - 8;
$far = $mem->view(DType::UInt8, 4, null, 4096 + 10);   // after a page boundary
for ($i = 0; $i < 4; $i++) $far[$i] = 200 + $i;
$data = "HDR!" . substr($mem->toString(), 4);
$path = tempnam(sys_get_temp_dir(), 'buf');
file_put_contents($path, $data);

echo "mapped\n";
$b = Buffer::fromFile($path);
var_dump($b->byteLength === strlen($data), $b->isWritable(), $b->isHostAccessible(), $b->device === FileDevice::get(), $b->toString() === $data);
$v = $b->view(DType::Float32, [2, 2], null, 24);
var_dump($v[0][0], $v[0][1], $v[1][0], $v[1][1], $v->offset, $v->buffer === $b);
$w = $b->view(DType::BFloat16, 3, null, 40);
var_dump(iterator_to_array($w));
$n = $b->view(DType::Int4, 16, null, 46);
var_dump(iterator_to_array($n));
var_dump($b->view(DType::UInt8, 4, null, 4096 + 10)->jsonSerialize());
var_dump($b->view(DType::UInt8, 4)->jsonSerialize() === array_map('ord', str_split("HDR!")));
echo "read-only\n";
foreach ([fn() => $v[0][0] = 1.0, fn() => $w[0] = 1.0, fn() => $b->view(DType::UInt8, [], null, 0)->value = 1] as $f) {
    try { $f(); echo "no error\n"; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
echo "offset and length\n";
foreach ([[24, 16], [5000, null], [4106, 4], [0, 0], [strlen($data), null], [strlen($data), 0]] as [$off, $len]) {
    $c = Buffer::fromFile($path, $off, $len);
    printf("offset %d length %s: %d bytes, host=%d%s\n", $off, var_export($len, true), $c->byteLength, $c->isHostAccessible(), $c->byteLength ? " first=" . bin2hex($c->toString()[0]) : '');
}
$c = Buffer::fromFile($path, 24, 16);
var_dump(iterator_to_array($c->view(DType::Float32, 4)));
foreach ([[strlen($data) + 1, null], [0, strlen($data) + 1], [10, strlen($data) - 9], [-1, null], [0, -1]] as [$off, $len]) {
    try { Buffer::fromFile($path, $off, $len); echo "no error\n"; } catch (Throwable $e) { printf("%s: %s\n", get_class($e), str_replace(strlen($data) . ' bytes', 'N bytes', $e->getMessage())); }
}
foreach ([$path . '.missing', sys_get_temp_dir()] as $bad) {
    try { Buffer::fromFile($bad); } catch (Throwable $e) { echo get_class($e), ': ', str_replace([$bad, sys_get_temp_dir()], 'PATH', $e->getMessage()), "\n"; }
}

echo "to(): a mapped file is host memory\n";
$cpu = CpuDevice::get();
$h = $v->to($cpu);
var_dump($h === $v, $h->buffer->device === $cpu, $h->buffer->isWritable(), $h->buffer->isHostAccessible(), $h->shape->toArray(), $h->strides, $h->offset, iterator_to_array($h[1]));
var_dump($h->to($cpu) === $h, $v->to(FileDevice::get()) === $v);
$h[0][0] = 9.0; var_dump($v[0][0]);   // a copy
try { $h->to(FileDevice::get()); } catch (Error $e) { echo $e->getMessage(), "\n"; }

echo "storage only (not mapped)\n";
$u = Buffer::fromFile($path, 0, null, false);
var_dump($u->isHostAccessible(), $u->isWritable(), $u->device === FileDevice::get(), $u->byteLength === strlen($data), $u->device->hostAccessible);
$uv = $u->view(DType::Float32, [2, 2], null, 24);
foreach ([fn() => $u->toString(), fn() => $uv[0][0], fn() => $uv[0][0] = 1.0, fn() => iterator_to_array($uv[1]), fn() => json_encode($uv), fn() => $b->view(DType::UInt8, [], null, 0)->to($cpu)->value] as $f) {
    try { $r = $f(); echo "no error: ", json_encode($r), "\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
$r = $uv->to($cpu);
var_dump($r->buffer->device === $cpu, $r->buffer->isWritable(), $r->shape->toArray(), $r[0][0], $r[0][1], $r[1][0], $r[1][1]);
var_dump($uv->to(FileDevice::get()) === $uv);
echo "strided, offset and packed views are copied C-contiguously\n";
$s = $u->view(DType::Float32, [2, 2], [1, 1], 24)->to($cpu);          // overlapping rows
var_dump($s->strides, iterator_to_array($s[0]), iterator_to_array($s[1]));
$s = $u->view(DType::UInt8, [2, 3], [4, 1], 25)->to($cpu);
var_dump($s->strides, $s->buffer->byteLength, $s[1][2] === ord($data[25 + 4 + 2]));
$s = $u->view(DType::Int4, [3, 3], [4, 1], 46)->to($cpu);           // packed: rows padded to whole bytes
var_dump($s->strides, $s->buffer->byteLength, iterator_to_array($s[0]), iterator_to_array($s[1]), iterator_to_array($s[2]));
$s = $b->view(DType::Int4, [3, 3], [4, 1], 46)->to($cpu);
var_dump(json_encode($s));
$s = $u->view(DType::BFloat16, [], null, 40)->to($cpu);
var_dump($s->ndim, $s->strides, $s->value, $s->buffer->byteLength);
$s = $u->view(DType::Float32, [0, 3], null, 24)->to($cpu);
var_dump($s->shape->toArray(), $s->buffer->byteLength);
$s = $u->view(DType::Float32, 4, null, 24)->to($cpu);
var_dump(iterator_to_array($s));
$far = $u->view(DType::UInt8, 4, null, 4096 + 10)->to($cpu);
var_dump(iterator_to_array($far));

echo "an empty file\n";
$e = tempnam(sys_get_temp_dir(), 'buf');
foreach ([true, false] as $map) {
    $z = Buffer::fromFile($e, 0, null, $map);
    var_dump($z->byteLength, $z->isHostAccessible(), count($z->view(DType::UInt8)), $z->view(DType::Float32, [0, 2])->shape->toArray());
}
echo "mapped files stay readable after unlink and while the Buffer lives\n";
$m = Buffer::fromFile($path, 24, 16); $mv = $m->view(DType::Float32, 4);
unlink($path); unlink($e);
var_dump($mv[2], $m->toString() === substr($data, 24, 16));
$l = Buffer::fromFile(__FILE__, 0, 5);
var_dump($l->toString());
echo "end\n";
--EXPECT--
mapped
bool(true)
bool(false)
bool(true)
bool(true)
bool(true)
float(1.5)
float(-2.25)
float(3)
float(4.5)
int(24)
bool(true)
array(3) {
  [0]=>
  float(1)
  [1]=>
  float(2)
  [2]=>
  float(3)
}
array(16) {
  [0]=>
  int(-8)
  [1]=>
  int(-7)
  [2]=>
  int(-6)
  [3]=>
  int(-5)
  [4]=>
  int(-4)
  [5]=>
  int(-3)
  [6]=>
  int(-2)
  [7]=>
  int(-1)
  [8]=>
  int(0)
  [9]=>
  int(1)
  [10]=>
  int(2)
  [11]=>
  int(3)
  [12]=>
  int(4)
  [13]=>
  int(5)
  [14]=>
  int(6)
  [15]=>
  int(7)
}
array(4) {
  [0]=>
  int(200)
  [1]=>
  int(201)
  [2]=>
  int(202)
  [3]=>
  int(203)
}
bool(true)
read-only
Error: Cannot write to a read-only Tensor
Error: Cannot write to a read-only Tensor
Error: Cannot write to a read-only Tensor
offset and length
offset 24 length 16: 16 bytes, host=1 first=00
offset 5000 length NULL: 3292 bytes, host=1 first=00
offset 4106 length 4: 4 bytes, host=1 first=c8
offset 0 length 0: 0 bytes, host=1
offset 8292 length NULL: 0 bytes, host=1
offset 8292 length 0: 0 bytes, host=1
array(4) {
  [0]=>
  float(1.5)
  [1]=>
  float(-2.25)
  [2]=>
  float(3)
  [3]=>
  float(4.5)
}
ValueError: Buffers\Buffer::fromFile(): Argument #2 ($offset) is beyond the end of the file (N bytes)
ValueError: Buffers\Buffer::fromFile(): Argument #3 ($length) extends beyond the end of the file (N bytes, from offset 0)
ValueError: Buffers\Buffer::fromFile(): Argument #3 ($length) extends beyond the end of the file (N bytes, from offset 10)
ValueError: Buffers\Buffer::fromFile(): Argument #2 ($offset) must be greater than or equal to 0
ValueError: Buffers\Buffer::fromFile(): Argument #3 ($length) must be greater than or equal to 0
Error: Cannot open 'PATH': No such file or directory
Error: 'PATH' is not a regular file
to(): a mapped file is host memory
bool(false)
bool(true)
bool(true)
bool(true)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(2)
}
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(1)
}
int(0)
array(2) {
  [0]=>
  float(3)
  [1]=>
  float(4.5)
}
bool(true)
bool(true)
float(1.5)
no backend for device 'file'
storage only (not mapped)
bool(false)
bool(false)
bool(true)
bool(true)
bool(false)
Error: Cannot read the bytes of a buffer on device file: its memory is not host accessible; transfer to the host first with Tensor::to()
Error: Cannot access the elements of a tensor on device file: its memory is not host accessible; transfer to the host first with to()
Error: Cannot write to a read-only Tensor
Error: Cannot access the elements of a tensor on device file: its memory is not host accessible; transfer to the host first with to()
Error: Cannot access the elements of a tensor on device file: its memory is not host accessible; transfer to the host first with to()
no error: 72
bool(true)
bool(true)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(2)
}
float(1.5)
float(-2.25)
float(3)
float(4.5)
bool(true)
strided, offset and packed views are copied C-contiguously
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(1)
}
array(2) {
  [0]=>
  float(1.5)
  [1]=>
  float(-2.25)
}
array(2) {
  [0]=>
  float(-2.25)
  [1]=>
  float(3)
}
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(1)
}
int(6)
bool(true)
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(1)
}
int(6)
array(3) {
  [0]=>
  int(-8)
  [1]=>
  int(-7)
  [2]=>
  int(-6)
}
array(3) {
  [0]=>
  int(-4)
  [1]=>
  int(-3)
  [2]=>
  int(-2)
}
array(3) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(2)
}
string(31) "[[-8,-7,-6],[-4,-3,-2],[0,1,2]]"
int(0)
array(0) {
}
float(1)
int(2)
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(3)
}
int(0)
array(4) {
  [0]=>
  float(1.5)
  [1]=>
  float(-2.25)
  [2]=>
  float(3)
  [3]=>
  float(4.5)
}
array(4) {
  [0]=>
  int(200)
  [1]=>
  int(201)
  [2]=>
  int(202)
  [3]=>
  int(203)
}
an empty file
int(0)
bool(true)
int(0)
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(2)
}
int(0)
bool(false)
int(0)
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(2)
}
mapped files stay readable after unlink and while the Buffer lives
float(3)
bool(true)
string(5) "<?php"
end
