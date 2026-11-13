--TEST--
DataView accessors: $d->uint32[$byteOffset], byte-indexed, little-endian, same values as get/set
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
<?php if (PHP_INT_SIZE < 8) die('skip 64-bit only'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer, DataView, DataViewAccessor};

$b = new Buffer(40);
$d = new DataView($b, 3, 30);

// one object per type, the same on every read
var_dump($d->uint32 instanceof DataViewAccessor, $d->uint32 instanceof ArrayAccess, $d->uint32 === $d->uint32, $d->uint32 !== $d->int32);
var_dump($d->uint32->dtype === DType::UInt32, $d->float64->dtype === DType::Float64);

// every type against the get/set methods at every offset that fits, both ways
$cases = [
    'int8' => ['Int8', 1, [-128, 127, -1]], 'uint8' => ['UInt8', 1, [0, 255, 7]],
    'int16' => ['Int16', 2, [-32768, 32767, -2]], 'uint16' => ['UInt16', 2, [0, 65535, 4660]],
    'int32' => ['Int32', 4, [-2147483648, 2147483647, -3]], 'uint32' => ['UInt32', 4, [0, 4294967295, 305419896]],
    'int64' => ['Int64', 8, [PHP_INT_MIN, PHP_INT_MAX, -4]], 'uint64' => ['UInt64', 8, [0, PHP_INT_MAX, -1]],
    'float16' => ['Float16', 2, [1.5, -0.125, 65504.0]], 'bfloat16' => ['BFloat16', 2, [1.5, -0.125, 3.0]],
    'float32' => ['Float32', 4, [1.5, -0.125, 1048576.0]], 'float64' => ['Float64', 8, [M_PI, -1e300, 0.1]],
];
$bad = 0;
foreach ($cases as $prop => [$name, $size, $vals]) {
    $a = $d->$prop;
    for ($off = 0; $off <= 30 - $size; $off++) {
        foreach ($vals as $x) {
            $a[$off] = $x;
            if ($d->{"get$name"}($off) !== $x) { echo "$prop[$off] = $x: get$name gives ", var_export($d->{"get$name"}($off), true), "\n"; $bad++; }
            $d->{"set$name"}($off, $x);
            if ($a[$off] !== $x) { echo "set$name($off, $x): $prop[$off] gives ", var_export($a[$off], true), "\n"; $bad++; }
        }
    }
}
echo "mismatches: $bad\n";

// little-endian, indexes are byte offsets and overlap, relative to the DataView's start
$b2 = Buffer::fromString("\x01\x02\x03\x04\x05\x06\x07\x08\x09");
$d2 = new DataView($b2, 1);
$u32 = $d2->uint32;
printf("%08x %08x %08x\n", $u32[0], $u32[1], $u32[4]);
$u32[1] = 0xAABBCCDD;
echo bin2hex($b2->toString()), "\n";

// writes wrap to the width, floats round like Tensor element writes, a float is truncated for an int type
$u8 = $d->uint8; $i16 = $d->int16;
$u8[0] = 300; $i16[4] = 40000; $u8[1] = -1; $u8[3] = 7.9;
var_dump($u8[0], $i16[4], $u8[1], $u8[3]);
$f32 = $d->float32; $f32[5] = 0.1; $f32[9] = 3;
var_dump($f32[5], $f32[9]);
$d->uint64[0] = -1; var_dump($d->uint64[0]);

// bounds: 0 .. byteLength - size
var_dump(isset($u32[4]), isset($u32[5]), isset($u32[-1]), isset($u32['x']), $u32[5] ?? 'none', $u32->offsetExists(4));
foreach ([fn() => $u32[5], fn() => $u32[-1], fn() => $u32[5] = 1, fn() => $u32['1'], fn() => $u32[] = 1,
          fn() => $u32[0] = 'x', fn() => $u32[0] = null] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
try { unset($u32[0]); } catch (Error $e) { echo $e->getMessage(), "\n"; }

// a DataView shorter than one value: every index is out of range
$short = (new DataView($b, 0, 3))->uint32;
var_dump(isset($short[0]));
try { $short[0]; } catch (ValueError $e) { echo $e->getMessage(), "\n"; }

// empty() looks at the value
$u8[10] = 0; $u8[11] = 5;
var_dump(empty($u8[10]), empty($u8[11]), empty($u8[99]));

// the ArrayAccess methods
$u32->offsetSet(0, 0x01020304); var_dump($u32->offsetGet(0) === 0x01020304);

// read-only buffers
$ro = (new DataView(Buffer::fromString("abcdefgh", false)))->uint16;
printf("%04x\n", $ro[1]);
try { $ro[1] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }

// an accessor keeps the memory alive after the DataView and the Buffer are gone
$keep = (new DataView(Buffer::fromString("\xff\xff\xff\x7f")))->int32;
gc_collect_cycles();
var_dump($keep[0]);

try { new DataViewAccessor(); } catch (Error $e) { echo $e->getMessage(), "\n"; }
try { clone $u32; } catch (Error $e) { echo $e->getMessage(), "\n"; }
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
mismatches: 0
05040302 06050403 09080706
0102ddccbbaa070809
int(44)
int(-25536)
int(255)
int(7)
float(0.10000000149011612)
float(3)
int(-1)
bool(true)
bool(false)
bool(false)
bool(false)
string(4) "none"
bool(true)
ValueError: Byte offset 5 is out of range: a UInt32 must start between 0 and 4
ValueError: Byte offset -1 is out of range: a UInt32 must start between 0 and 4
ValueError: Byte offset 5 is out of range: a UInt32 must start between 0 and 4
TypeError: DataViewAccessor offset must be of type int, string given
Error: Cannot append to a DataViewAccessor
TypeError: Cannot write string to a DataViewAccessor of type UInt32
TypeError: Cannot write null to a DataViewAccessor of type UInt32
Cannot unset bytes of a DataViewAccessor
bool(false)
Byte offset 0 is out of range: the DataView is shorter than one UInt32
bool(true)
bool(false)
bool(true)
bool(true)
6362
Cannot write to a read-only Buffer
int(2147483647)
DataViewAccessor cannot be instantiated; read a DataView property such as $dataView->uint32
Trying to clone an uncloneable object of class Buffers\DataViewAccessor
