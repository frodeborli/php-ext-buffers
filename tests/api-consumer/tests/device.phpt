--TEST--
C API: a Device implemented by another extension; pass-through of device memory
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, BufferView, Device, Buffer};
function live() { $c = bat_counts(); return $c['made'] - $c['deleted']; }

$b = bat_device_buffer(64);
$d = $b->device();
var_dump(get_class($d), $d instanceof Device, $d->name(), $d->id(), $d->isHostAccessible(), $b->device() === $d);
$v = $b->view(DType::Float32, [2, 4], null, 16);
print_r(bat_info($v));
foreach ([fn() => $v[0][0], fn() => $v[1][1] = 1.0, fn() => foreach_it($v[0])] as $f) {
    try { $f(); echo "no error\n"; } catch (Error $e) { echo $e->getMessage(), "\n"; }
}
function foreach_it($x) { foreach ($x as $y) {} }
try { bat_peek($b, 0); } catch (ValueError $e) { echo "peek: ", $e->getMessage(), "\n"; }

echo "export: device, ids and the opaque pointer are passed through unchanged\n";
print_r(bat_export($v));
print_r(bat_export($v[1]));
$i = bat_roundtrip($v);
$bi = bat_info($i);
var_dump($bi['device_type'], $bi['device_id'], $bi['byte_offset'], $bi['host_accessible'], $bi['buffer_length'], $i->buffer->device()->name(), get_class($i->buffer->device()));
$j = bat_roundtrip($v);
var_dump(bat_export($j)['data_is_buffer_base'], bat_export($j)['byte_offset']);
echo live(), " live\n";
unset($v, $b, $i, $j, $d, $e, $f);
echo live(), " live\n";
$c = bat_counts();
echo $c['made'] === $c['deleted'] ? "deleters ran exactly once\n" : "DELETER COUNT WRONG\n";
echo "end\n";
--EXPECT--
string(9) "BatDevice"
bool(true)
string(5) "bat:5"
int(5)
bool(false)
bool(true)
Array
(
    [dtype] => Float32
    [bits] => 32
    [size] => 4
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

    [byte_offset] => 16
    [data_is_base_plus_offset] => 1
    [writable] => 1
    [buffer_length] => 64
    [host_accessible] => 
    [device_type] => 2
    [device_id] => 5
    [is_view] => 1
)
Cannot access the elements of a tensor on device cuda:5: its memory is not host accessible; transfer to the host first with to()
Cannot access the elements of a tensor on device cuda:5: its memory is not host accessible; transfer to the host first with to()
Cannot access the elements of a tensor on device cuda:5: its memory is not host accessible; transfer to the host first with to()
peek: cannot peek
export: device, ids and the opaque pointer are passed through unchanged
Array
(
    [data_is_buffer_base] => 1
    [device_type] => 2
    [device_id] => 5
    [ndim] => 2
    [code] => 2
    [bits] => 32
    [byte_offset] => 16
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
    [device_type] => 2
    [device_id] => 5
    [ndim] => 1
    [code] => 2
    [bits] => 32
    [byte_offset] => 32
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
int(2)
int(5)
int(16)
bool(false)
int(48)
string(6) "cuda:5"
string(21) "Buffers\ForeignDevice"
bool(true)
int(16)
1 live
0 live
deleters ran exactly once
end
