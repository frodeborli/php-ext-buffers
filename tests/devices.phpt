--TEST--
Devices: CpuDevice and FileDevice, identified by name and id
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_VERSION_ID < 80400) die('skip PHP 8.4+: property hooks (PHP 8.3 is covered by props_php83.phpt)'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Device, CpuDevice, FileDevice, ForeignDevice};

$cpu = CpuDevice::get();
var_dump($cpu === CpuDevice::get(), $cpu instanceof Device, $cpu->name, $cpu->id, $cpu->hostAccessible);
$file = FileDevice::get();
var_dump($file === FileDevice::get(), $file instanceof Device, $file->name, $file->id, $file->hostAccessible, $file === $cpu);
var_dump(method_exists($cpu, "name"), property_exists($cpu, "hostAccessible"), property_exists(Device::class, "id"));
echo implode(",", array_map(fn($p) => $p->getName(), (new ReflectionClass(Device::class))->getProperties())), "\n";
$b = new Buffer(8);
var_dump($b->device === $cpu, $b->view(DType::UInt8)->buffer->device === $cpu, $b->isHostAccessible());
foreach ([fn() => new CpuDevice, fn() => new FileDevice, fn() => new ForeignDevice, fn() => clone $cpu, fn() => clone $file, fn() => serialize($cpu), fn() => (function() use ($cpu) { $cpu->x = 1; })()] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
echo "end\n";
--EXPECT--
bool(true)
bool(true)
string(3) "cpu"
int(0)
bool(true)
bool(true)
bool(true)
string(4) "file"
int(0)
bool(false)
bool(false)
bool(false)
bool(true)
bool(true)
name,id,hostAccessible
bool(true)
bool(true)
bool(true)
Error: Call to private Buffers\CpuDevice::__construct() from global scope
Error: Call to private Buffers\FileDevice::__construct() from global scope
Error: ForeignDevice cannot be instantiated; it comes from the DLPack import of the C API
Error: Trying to clone an uncloneable object of class Buffers\CpuDevice
Error: Trying to clone an uncloneable object of class Buffers\FileDevice
Exception: Serialization of 'Buffers\CpuDevice' is not allowed
Error: Cannot create dynamic property Buffers\CpuDevice::$x
end
