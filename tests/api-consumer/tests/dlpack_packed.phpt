--TEST--
DLPack through the C API: packed formats (unpadded), 0-d tensors
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, BufferView};
const PADDED = 4;
echo "packed formats round trip\n";
foreach ([[0, 2], [0, 4], [1, 2], [1, 4], [15, 6], [16, 6], [17, 4]] as [$code, $bits]) {
    $v = bat_import($code, $bits, 1, [2, 8], [8, 1]);
    $x = bat_roundtrip($v);
    $e = bat_export($x);
    echo "$code/$bits => ", $v->dtype->name, " ", $v->buffer->byteLength(), " bytes strides ", json_encode($v->strides), " round trip ", $x->dtype->name, " ", json_encode($x->shape->toArray()), " flags ", $e['flags'], " bits ", $e['bits'], "\n";
}
echo "rejections\n";
foreach ([[0, 4, [4], [1], PADDED], [1, 2, [4], [1], PADDED], [15, 6, [4], [1], PADDED], [0, 3, [4], [1], 0], [0, 4, [2, 3], [3, 1], 0], [1, 2, [4], [2], 0], [15, 6, [2, 3], [3, 1], 0]] as $i => [$code, $bits, $shape, $strides, $flags]) {
    try { bat_import($code, $bits, 1, $shape, $strides, $flags); echo "case $i accepted\n"; }
    catch (ValueError $e) { echo "case $i: ", $e->getMessage(), "\n"; }
}
echo "0-d\n";
$b = new Buffer(16);
$s = $b->view(DType::Int32, [], null, 4);
$i = bat_roundtrip($s);
var_dump($i->ndim, $i->shape->toArray(), $i->strides, $i->offset, $i->dtype, $i->buffer->byteLength());
print_r(bat_export($s));
$p = bat_import(2, 32, 1, []);
var_dump($p->ndim, $p->dtype, $p->buffer->byteLength());
$p2 = bat_import(0, 32, 1, [], null, 0, 1, 0, 1);   // DLPack 1.1: NULL strides, still a scalar
var_dump($p2->ndim, $p2->dtype);
$f4 = bat_import(17, 4, 1, []);
var_dump($f4->ndim, $f4->dtype, $f4->buffer->byteLength());
unset($p, $p2, $f4, $i, $s, $v, $x, $e, $b);
$c = bat_counts(); echo $c['tensors_made'] === $c['tensors_deleted'] ? "deleters ran exactly once\n" : "DELETER COUNT WRONG\n";
echo "end\n";
--EXPECT--
packed formats round trip
0/2 => Int2 4 bytes strides [8,1] round trip Int2 [2,8] flags 0 bits 2
0/4 => Int4 8 bytes strides [8,1] round trip Int4 [2,8] flags 0 bits 4
1/2 => UInt2 4 bytes strides [8,1] round trip UInt2 [2,8] flags 0 bits 2
1/4 => UInt4 8 bytes strides [8,1] round trip UInt4 [2,8] flags 0 bits 4
15/6 => Float6E2M3FN 12 bytes strides [8,1] round trip Float6E2M3FN [2,8] flags 0 bits 6
16/6 => Float6E3M2FN 12 bytes strides [8,1] round trip Float6E3M2FN [2,8] flags 0 bits 6
17/4 => Float4E2M1FN 8 bytes strides [8,1] round trip Float4E2M1FN [2,8] flags 0 bits 4
rejections
case 0: Padded sub-byte tensors are not supported; the tensor must be packed (DLPack's default: little-bit-endian, no padding)
case 1: Padded sub-byte tensors are not supported; the tensor must be packed (DLPack's default: little-bit-endian, no padding)
case 2: Padded sub-byte tensors are not supported; the tensor must be packed (DLPack's default: little-bit-endian, no padding)
case 3: Unsupported DLPack dtype (code 0, 3 bits)
case 4: Axis 0 of a packed sub-byte tensor has a stride of 12 bits, which is not a whole number of bytes
case 5: The last axis of a packed sub-byte tensor must have stride 1
case 6: Axis 0 of a packed sub-byte tensor has a stride of 18 bits, which is not a whole number of bytes
0-d
int(0)
array(0) {
}
array(0) {
}
int(0)
enum(Buffers\DType::Int32)
int(4)
Array
(
    [data_is_buffer_base] => 1
    [device_type] => 1
    [device_id] => 0
    [ndim] => 0
    [code] => 0
    [bits] => 32
    [byte_offset] => 4
    [flags] => 0
    [shape] => Array
        (
        )

    [strides] => Array
        (
        )

)
int(0)
enum(Buffers\DType::Float32)
int(4)
int(0)
enum(Buffers\DType::Int32)
int(0)
enum(Buffers\DType::Float4E2M1FN)
int(1)
deleters ran exactly once
end
