--TEST--
DLPack import of foreign tensors; the deleter runs exactly once
--SKIPIF--
<?php if (!extension_loaded('buffers') || !function_exists('buffers_test_make')) die('skip needs a build with --enable-buffers-testing'); if (PHP_INT_SIZE !== 8) die('skip 64-bit PHP only: imports Int64, UInt64 and UInt32 tensors (tests/bits32.phpt covers 32-bit)'); ?>
--FILE--
<?php
use Buffers\{DType, BufferView, BufferInterface, DLPackBuffer, DLPackCapsule, Tensor};
function counts() { $c = buffers_test_counts(); return "{$c['made']}/{$c['deleted']}"; }
const INT = 0, UINT = 1, FLOAT = 2, BFLOAT = 4, COMPLEX = 5, BOOL = 6, BCOMPLEX = 18;
const READ_ONLY = 1, PADDED = 4;

echo "contiguous uint8 2x3: "; 
$v = BufferView::fromDLPack(buffers_test_make(UINT, 8, 1, [2, 3], [3, 1]));
var_dump($v->dtype, $v->shape->toArray(), $v->strides, $v->offset, $v[1][2], $v->buffer instanceof DLPackBuffer, $v->buffer instanceof BufferInterface);
var_dump($v->buffer->isWritable(), $v->buffer->byteLength());
$v[1][2] = 77; var_dump($v[1][2]);
echo counts(), "\n"; unset($v); echo counts(), "\n";

echo "byte offset and strides\n";
$v = BufferView::fromDLPack(buffers_test_make(INT, 16, 1, [2, 2], [1, 2], 0, 1, 2));
var_dump($v->offset, $v->strides, dechex($v[0][0]), dechex($v[0][1]), dechex($v[1][0]), $v->buffer->byteLength());
unset($v);

echo "dtype mapping\n";
foreach ([[INT,8],[INT,16],[INT,32],[INT,64],[UINT,8],[UINT,64],[FLOAT,16],[BFLOAT,16],[FLOAT,32],[FLOAT,64],[COMPLEX,32],[COMPLEX,64],[COMPLEX,128],[BCOMPLEX,32],[BOOL,8],
          [7,8],[8,8],[9,8],[10,8],[11,8],[12,8],[13,8],[14,8]] as [$code, $bits]) {
    $v = BufferView::fromDLPack(buffers_test_make($code, $bits, 1, [1], [1]));
    echo "$code/$bits => ", $v->dtype->name, "\n";
}
foreach ([[15,6],[16,6],[17,4]] as [$code, $bits]) {
    $v = BufferView::fromDLPack(buffers_test_make($code, $bits, 1, [8], [1]));
    echo "$code/$bits packed => ", $v->dtype->name, " ", $v->buffer->byteLength(), " bytes, strides ", json_encode($v->strides), "\n";
}
unset($v);

echo "read-only\n";
$v = BufferView::fromDLPack(buffers_test_make(UINT, 8, 1, [4], [1], READ_ONLY));
var_dump($v->buffer->isWritable(), $v[3]);
try { $v[0] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }
$s = $v->buffer->view(DType::UInt8, 2);
try { $s[0] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }
$m = BufferView::fromDLPack(buffers_test_make(UINT, 8, 1, [2, 2], [2, 1], READ_ONLY));
try { $m[1][1] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }
unset($v, $s, $m);

echo "empty tensor\n";
$v = BufferView::fromDLPack(buffers_test_make(FLOAT, 32, 1, [0, 3], [3, 1]));
var_dump($v->shape->toArray(), count($v), $v->buffer->byteLength());
unset($v);

echo "legacy NULL strides (DLPack < 1.2) mean compact row-major\n";
$v = BufferView::fromDLPack(buffers_test_make(UINT, 8, 1, [2, 3], null, 0, 1, 0, 1));
var_dump($v->strides, $v[1][0]);
unset($v);

echo "rejections\n";
$cases = [
  'device type 0'     => [UINT, 8, 1, [2], [1], 0, 0],
  'lanes'             => [FLOAT, 32, 4, [2], [1]],
  'opaque handle'     => [3, 64, 1, [2], [1]],
  'int 12 bits'       => [INT, 12, 1, [2], [1]],
  'float 128 bits'    => [FLOAT, 128, 1, [2], [1]],
  'unknown code'      => [99, 8, 1, [2], [1]],
  'padded fp4'        => [17, 4, 1, [4], [1], PADDED],
  'padded fp6'        => [15, 6, 1, [4], [1], PADDED],
  'packed fp6 stride' => [15, 6, 1, [2, 3], [3, 1]],
  'packed fp4 last'   => [17, 4, 1, [4], [2]],
  'packed fp4 odd row'=> [17, 4, 1, [2, 3], [3, 1]],
  'NULL strides 1.3'  => [UINT, 8, 1, [2, 3], null],
  '9 dims'            => [UINT, 8, 1, [1,1,1,1,1,1,1,1,1], [1,1,1,1,1,1,1,1,1]],
  'negative dim'      => [UINT, 8, 1, [-1], [1]],
  'zero stride'       => [UINT, 8, 1, [2, 2], [0, 1]],
  'negative stride'   => [UINT, 8, 1, [2, 2], [-2, 1]],
  'misaligned offset' => [INT, 16, 1, [2], [1], 0, 1, 1],
  'misaligned f64'    => [FLOAT, 64, 1, [2], [1], 0, 1, 4],
  'major version 2'   => [UINT, 8, 1, [2], [1], 0, 1, 0, 3, 2],
  'major version 0'   => [UINT, 8, 1, [2], [1], 0, 1, 0, 3, 0],
];
foreach ($cases as $name => $args) {
    $before = buffers_test_counts();
    $cap = buffers_test_make(...$args);
    try { BufferView::fromDLPack($cap); echo "$name: accepted\n"; }
    catch (Throwable $e) { printf("%-18s %s: %s\n", $name, get_class($e), $e->getMessage()); }
    $mid = buffers_test_counts();
    unset($cap, $e);
    $after = buffers_test_counts();
    // a rejected capsule is not consumed: its deleter runs when the capsule is dropped
    if ($after['deleted'] - $before['deleted'] !== 1 || $mid['deleted'] !== $before['deleted']) echo "  DELETER COUNT WRONG\n";
}

echo "consumed capsules\n";
$cap = buffers_test_make(UINT, 8, 1, [4], [1]);
$v = BufferView::fromDLPack($cap);
try { BufferView::fromDLPack($cap); } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
unset($cap);
$c = buffers_test_counts(); echo $c['made'] - $c['deleted'], " live\n";   // only $v's tensor is alive
unset($v, $e);
$c = buffers_test_counts(); echo $c['made'] - $c['deleted'], " live\n";
$cap = buffers_test_make(UINT, 8, 1, [4], [1]); unset($cap);
$c = buffers_test_counts(); echo $c['made'] - $c['deleted'], " live\n";
foreach ([fn() => new DLPackCapsule(), fn() => new DLPackBuffer(), fn() => clone buffers_test_make(UINT, 8, 1, [1], [1]), fn() => serialize(buffers_test_make(UINT, 8, 1, [1], [1]))] as $f) {
    try { $f(); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
unset($f, $e);
$c = buffers_test_counts(); echo $c['made'] - $c['deleted'], " live at end\n";
--EXPECT--
contiguous uint8 2x3: enum(Buffers\DType::UInt8)
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
int(5)
bool(true)
bool(true)
bool(true)
int(6)
int(77)
1/0
1/1
byte offset and strides
int(0)
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(2)
}
string(3) "302"
string(3) "706"
string(3) "504"
int(8)
dtype mapping
0/8 => Int8
0/16 => Int16
0/32 => Int32
0/64 => Int64
1/8 => UInt8
1/64 => UInt64
2/16 => Float16
4/16 => BFloat16
2/32 => Float32
2/64 => Float64
5/32 => Complex32
5/64 => Complex64
5/128 => Complex128
18/32 => BComplex32
6/8 => Bool
7/8 => Float8E3M4
8/8 => Float8E4M3
9/8 => Float8E4M3B11FNUZ
10/8 => Float8E4M3FN
11/8 => Float8E4M3FNUZ
12/8 => Float8E5M2
13/8 => Float8E5M2FNUZ
14/8 => Float8E8M0FNU
15/6 packed => Float6E2M3FN 6 bytes, strides [1]
16/6 packed => Float6E3M2FN 6 bytes, strides [1]
17/4 packed => Float4E2M1FN 4 bytes, strides [1]
read-only
bool(false)
int(3)
Cannot write to a read-only BufferView
Cannot write to a read-only BufferView
Cannot write to a read-only BufferView
empty tensor
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(3)
}
int(0)
int(0)
legacy NULL strides (DLPack < 1.2) mean compact row-major
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(1)
}
int(3)
rejections
device type 0      ValueError: Invalid DLPack device type 0
lanes              ValueError: Vector dtypes are not supported (4 lanes)
opaque handle      ValueError: Unsupported DLPack dtype (code 3, 64 bits)
int 12 bits        ValueError: Unsupported DLPack dtype (code 0, 12 bits)
float 128 bits     ValueError: Unsupported DLPack dtype (code 2, 128 bits)
unknown code       ValueError: Unsupported DLPack dtype (code 99, 8 bits)
padded fp4         ValueError: Padded sub-byte tensors are not supported; the tensor must be packed (DLPack's default: little-bit-endian, no padding)
padded fp6         ValueError: Padded sub-byte tensors are not supported; the tensor must be packed (DLPack's default: little-bit-endian, no padding)
packed fp6 stride  ValueError: Axis 0 of a packed sub-byte tensor has a stride of 18 bits, which is not a whole number of bytes
packed fp4 last    ValueError: The last axis of a packed sub-byte tensor must have stride 1
packed fp4 odd row ValueError: Axis 0 of a packed sub-byte tensor has a stride of 12 bits, which is not a whole number of bytes
NULL strides 1.3   ValueError: Tensors without strides are illegal since DLPack 1.2
9 dims             ValueError: Unsupported number of dimensions (9); expected 0 to 8
negative dim       ValueError: Negative dimension in shape
zero stride        ValueError: Zero and negative strides are not supported
negative stride    ValueError: Zero and negative strides are not supported
misaligned offset  ValueError: Tensor data is not aligned to the element size (2 bytes)
misaligned f64     ValueError: Tensor data is not aligned to the element size (8 bytes)
major version 2    ValueError: Unsupported DLPack major version 2 (expected 1)
major version 0    ValueError: Unsupported DLPack major version 0 (expected 1)
consumed capsules
Error: The DLPackCapsule has already been consumed
1 live
0 live
0 live
Error: DLPackCapsule cannot be instantiated; use BufferView::toDLPack()
Error: DLPackBuffer cannot be instantiated; use BufferView::fromDLPack()
Error: Trying to clone an uncloneable object of class Buffers\DLPackCapsule
Exception: Serialization of 'Buffers\DLPackCapsule' is not allowed
0 live at end
