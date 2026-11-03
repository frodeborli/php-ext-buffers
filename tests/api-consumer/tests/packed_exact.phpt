--TEST--
C API: packed views over exactly-sized memory (no byte beyond the end is touched), exported without the padded flag
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, Tensor};
// bat_make_buffer allocates exactly n bytes: valgrind reports any read or write past the end
foreach ([[DType::Float6E2M3FN, 4, 5], [DType::Float6E3M2FN, 3, 4], [DType::Float6E2M3FN, 6, 8], [DType::Float4E2M1FN, 3, 6], [DType::Float4E2M1FN, 1, 1], [DType::Float6E2M3FN, 1, 1]] as [$t, $len, $n]) {
    $b = bat_make_buffer($len);
    $v = $b->view($t, $n);
    $sum = 0;
    for ($i = 0; $i < $n; $i++) { $v[$i] = 1.0; $sum += $v[$i]; }
    for ($i = 0; $i < $n; $i++) { $sum += $v[$i]; $v[$i] = 0.5; }
    foreach ($v as $x) $sum += $x;
    printf("%-14s %d bytes %d elements sum=%s\n", $t->name, $len, $n, $sum);
}
$b = bat_make_buffer(6);
$v = $b->view(DType::Float6E2M3FN, [2, 4]);
print_r(bat_info($v));
print_r(bat_export($v));
print_r(bat_export($v[1]));
$i = bat_roundtrip($v);
var_dump($i->dtype, $i->shape->toArray(), $i->strides, $i->offset, $i->buffer->byteLength);
$i[1][3] = 1.5; var_dump($v[1][3]);
$f4 = bat_make_buffer(8)->view(DType::Float4E2M1FN, [2, 6], [8, 1], 0);
print_r(bat_export($f4)['strides']);
echo "end\n";
--EXPECT--
Float6E2M3FN   4 bytes 5 elements sum=12.5
Float6E3M2FN   3 bytes 4 elements sum=10
Float6E2M3FN   6 bytes 8 elements sum=20
Float4E2M1FN   3 bytes 6 elements sum=15
Float4E2M1FN   1 bytes 1 elements sum=2.5
Float6E2M3FN   1 bytes 1 elements sum=2.5
Array
(
    [dtype] => Float6E2M3FN
    [bits] => 6
    [size] => 0
    [ndim] => 2
    [shape] => Array
        (
            [0] => 2
            [1] => 4
        )

    [strides] => Array
        (
            [0] => 4
            [1] => 1
        )

    [byte_offset] => 0
    [data_is_base_plus_offset] => 1
    [writable] => 1
    [buffer_length] => 6
    [host_accessible] => 1
    [device_type] => 1
    [device_id] => 0
    [is_view] => 1
)
Array
(
    [data_is_buffer_base] => 1
    [device_type] => 1
    [device_id] => 0
    [ndim] => 2
    [code] => 15
    [bits] => 6
    [byte_offset] => 0
    [flags] => 0
    [shape] => Array
        (
            [0] => 2
            [1] => 4
        )

    [strides] => Array
        (
            [0] => 4
            [1] => 1
        )

)
Array
(
    [data_is_buffer_base] => 1
    [device_type] => 1
    [device_id] => 0
    [ndim] => 1
    [code] => 15
    [bits] => 6
    [byte_offset] => 3
    [flags] => 0
    [shape] => Array
        (
            [0] => 4
        )

    [strides] => Array
        (
            [0] => 1
        )

)
enum(Buffers\DType::Float6E2M3FN)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(4)
}
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(1)
}
int(0)
int(6)
float(1.5)
Array
(
    [0] => 8
    [1] => 1
)
end
