--TEST--
32-bit PHP: a file larger than the address space is a storage-only buffer
--SKIPIF--
<?php
if (!extension_loaded('buffers')) die('skip buffers extension not loaded');
if (PHP_INT_SIZE !== 4) die('skip 32-bit PHP only');
if (PHP_OS_FAMILY === 'Windows' || !function_exists('exec')) die('skip needs truncate(1)');
?>
--FILE--
<?php
use Buffers\{Buffer, DType, CpuDevice, FileDevice};
$path = tempnam(sys_get_temp_dir(), 'buf');
exec('truncate -s 5G ' . escapeshellarg($path), $o, $rc);
if ($rc) { echo "no sparse file support\n"; unlink($path); return; }
echo "5 GB sparse file, whole: more than the address space, so not mapped\n";
$b = Buffer::fromFile($path);
var_dump($b->isHostAccessible(), $b->device() === FileDevice::get(), $b->isWritable());
try { $b->byteLength(); } catch (Error $e) { echo $e->getMessage(), "\n"; }
try { $b->toString(); } catch (Error $e) { echo get_class($e), "\n"; }
$b = Buffer::fromFile($path, 100);
var_dump($b->isHostAccessible());
echo "a range that fits is mapped\n";
$m = Buffer::fromFile($path, 1 << 20, 1 << 20);
var_dump($m->isHostAccessible(), $m->byteLength(), $m->view(DType::UInt8, 4, null, 100)->to(CpuDevice::get())->jsonSerialize());
echo "an unmapped file buffer is read by to()\n";
$u = Buffer::fromFile($path, 0, null, false);
var_dump($u->isHostAccessible());
$v = $u->view(DType::UInt8, 8, null, 1 << 20);
var_dump($v->to(CpuDevice::get())->jsonSerialize());
unlink($path);
echo "end\n";
--EXPECT--
5 GB sparse file, whole: more than the address space, so not mapped
bool(false)
bool(true)
bool(false)
The byte length does not fit a PHP int on this platform
Error
bool(false)
a range that fits is mapped
bool(true)
int(1048576)
array(4) {
  [0]=>
  int(0)
  [1]=>
  int(0)
  [2]=>
  int(0)
  [3]=>
  int(0)
}
an unmapped file buffer is read by to()
bool(false)
array(8) {
  [0]=>
  int(0)
  [1]=>
  int(0)
  [2]=>
  int(0)
  [3]=>
  int(0)
  [4]=>
  int(0)
  [5]=>
  int(0)
  [6]=>
  int(0)
  [7]=>
  int(0)
}
end