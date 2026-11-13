--TEST--
C API and DLPack with views at any byte offset: the pointer is handed over as it is; unaligned imports
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer};
$b = new Buffer(64);
$raw = $b->view(DType::UInt8);
for ($i = 0; $i < 64; $i++) $raw[$i] = ($i * 29 + 3) & 0xff;
$s = $b->toString();

echo "buffers_tensor_get_info(): data = base + offset, at any offset\n";
foreach ([[DType::Float32, 'f', 4], [DType::Float64, 'd', 8], [DType::Int16, 's', 2], [DType::Int64, 'q', 8]] as [$dt, $fmt, $size]) {
    foreach ([1, 3, 5] as $off) {
        $v = $b->view($dt, 3, null, $off);
        $i = bat_info($v);
        $t = bat_tensor_bytes($v);
        printf("%-7s @%d info: offset=%d base+offset=%d writable=%d; aligned=%d elements=%d\n", $dt->name, $off, $i['byte_offset'],
            $i['data_is_base_plus_offset'], $t['writable'], $t['data_aligned'], $t['elements'] === substr($s, $off, 3 * $size));
    }
}
$v = $b->view(DType::Float32, 4);
var_dump(bat_tensor_bytes($v)['data_aligned']);   // a buffer the extension allocated, offset 0

echo "strided\n";
$m = $b->view(DType::Float32, [2, 3], [6, 2], 1);
$t = bat_tensor_bytes($m);
$want = '';
for ($r = 0; $r < 2; $r++) for ($c = 0; $c < 3; $c++) $want .= substr($s, 1 + $r * 24 + $c * 8, 4);
var_dump($t['data_aligned'], $t['strides'], $t['elements'] === $want);

echo "DLPack export: as it is\n";
$e = bat_export_bytes($m);
var_dump($e['data_aligned'], $e['read_only'], $e['byte_offset'], $e['strides'], $e['elements'] === $want);
$e = bat_export_bytes($b->view(DType::Float64, [], null, 3));
var_dump($e['data_aligned'], $e['byte_offset'], $e['elements'] === substr($s, 3, 8));
// a round trip is the same memory
$w = bat_roundtrip($m);
var_dump($w->offset, bat_tensor_bytes($w)['elements'] === $want);   // the bytes: random patterns can be NaN
$w[1][2] = 2.5; var_dump($m[1][2]);

echo "DLPack import of unaligned host data\n";
// bat_import(code, bits, lanes, shape, strides, flags, device, byteOffset): memory holds the byte pattern i & 0xff
foreach ([1, 3, 5] as $off) {
    $f = bat_import(2, 32, 1, [3], [1], 0, 1, $off);   // kDLFloat 32
    $bytes = ''; for ($i = $off; $i < $off + 12; $i++) $bytes .= chr($i & 0xff);
    $ok = $f->toArray() === array_values(unpack('f3', $bytes));
    $f[2] = -1.25;
    printf("float32 @%d: values=%d write=%d offset=%d aligned=%d\n", $off, $ok, $f[2] === -1.25, $f->offset, bat_tensor_bytes($f)['data_aligned']);
}
$q = bat_import(0, 64, 1, [2, 2], [4, 1], 0, 1, 1);       // kDLInt 64, strided, byte offset 1
$bytes = ''; for ($i = 1; $i < 1 + 64; $i++) $bytes .= chr($i & 0xff);
var_dump($q[1][1] === unpack('q', $bytes, 5 * 8)[1], $q[0][1] === unpack('q', $bytes, 8)[1]);

echo "DataView: device memory and readiness\n";
try { (new Buffers\DataView(bat_device_buffer(8)))->getUInt8(0); } catch (Error $e) { echo $e->getMessage(), "\n"; }
$p = bat_make_pending(8, 7);
$dv = new Buffers\DataView($p, 1);
var_dump($dv->getUInt16(2));   // made ready first: bytes of 7
echo "end\n";
--EXPECT--
buffers_tensor_get_info(): data = base + offset, at any offset
Float32 @1 info: offset=1 base+offset=1 writable=1; aligned=0 elements=1
Float32 @3 info: offset=3 base+offset=1 writable=1; aligned=0 elements=1
Float32 @5 info: offset=5 base+offset=1 writable=1; aligned=0 elements=1
Float64 @1 info: offset=1 base+offset=1 writable=1; aligned=0 elements=1
Float64 @3 info: offset=3 base+offset=1 writable=1; aligned=0 elements=1
Float64 @5 info: offset=5 base+offset=1 writable=1; aligned=0 elements=1
Int16   @1 info: offset=1 base+offset=1 writable=1; aligned=0 elements=1
Int16   @3 info: offset=3 base+offset=1 writable=1; aligned=0 elements=1
Int16   @5 info: offset=5 base+offset=1 writable=1; aligned=0 elements=1
Int64   @1 info: offset=1 base+offset=1 writable=1; aligned=0 elements=1
Int64   @3 info: offset=3 base+offset=1 writable=1; aligned=0 elements=1
Int64   @5 info: offset=5 base+offset=1 writable=1; aligned=0 elements=1
bool(true)
strided
bool(false)
array(2) {
  [0]=>
  int(6)
  [1]=>
  int(2)
}
bool(true)
DLPack export: as it is
bool(false)
bool(false)
int(1)
array(2) {
  [0]=>
  int(6)
  [1]=>
  int(2)
}
bool(true)
bool(false)
int(3)
bool(true)
int(0)
bool(true)
float(2.5)
DLPack import of unaligned host data
float32 @1: values=1 write=1 offset=0 aligned=0
float32 @3: values=1 write=1 offset=0 aligned=0
float32 @5: values=1 write=1 offset=0 aligned=0
bool(true)
bool(true)
DataView: device memory and readiness
Cannot access the bytes of a buffer on device cuda:5: its memory is not host accessible
int(1799)
end
