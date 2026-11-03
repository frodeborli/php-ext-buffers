--TEST--
C API: a buffer over foreign memory with a counting deleter; view info; view creation
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, BufferView, BufferInterface, DLPackBuffer};
function live() { $c = bat_counts(); return $c['made'] - $c['deleted']; }

$b = bat_make_buffer(24);
var_dump($b instanceof DLPackBuffer, $b instanceof BufferInterface, $b->byteLength(), $b->isWritable(), $b->device() === Buffers\CpuDevice::get(), bat_is($b), bat_is($b->view(DType::UInt8)), bat_is(1));
$v = $b->view(DType::UInt8);
var_dump($v[0], $v[23]);
$v[3] = 99; var_dump(bat_peek($b, 3));
echo live(), " live\n";

echo "info\n";
$w = $b->view(DType::Int16, [2, 3], [4, 1], 4);
print_r(bat_info($w));
print_r(bat_info($w[1]));
print_r(bat_info($b->view(DType::Float4E2M1FN, [2, 4], null, 3)));
print_r(bat_info($b->view(DType::Int32, [])));

echo "views made through the C API\n";
$cases = [
    'ok'              => [$b, 4, [2, 3]],            // DType index 4 is Int16
    'strided offset'  => [$b, 4, [2, 3], [6, 2], 4],
    'bad dtype'       => [$b, 99, [1]],
    'beyond'          => [$b, 4, [13]],
    'misaligned'      => [$b, 4, [1], null, 1],
    'neg dim'         => [$b, 4, [-1]],
    'zero stride'     => [$b, 4, [2], [0]],
    'packed stride'   => [$b, 26, [2, 3]],           // Float6E3M2FN: rows of 3 are not whole bytes
    'not a buffer'    => [new stdClass, 4, [1]],
];
foreach ($cases as $name => $args) {
    try { $x = bat_view(...$args); printf("%-15s ok %s %s\n", $name, json_encode($x->shape->toArray()), json_encode($x->strides)); }
    catch (Throwable $e) { printf("%-15s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
var_dump(bat_view($b, 4, [2, 3])->dtype);

echo "read-only memory\n";
$ro = bat_make_buffer(8, false);
$r = $ro->view(DType::UInt8);
var_dump($ro->isWritable(), $r[1]);
try { $r[0] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }
try { $ro->view(DType::UInt8, [2, 4])[1][1] = 1; } catch (Error $e) { echo $e->getMessage(), "\n"; }

echo "lifetime: the deleter runs once, when the last view and buffer are gone\n";
$base = live();
$x = bat_make_buffer(8);
$y = $x->view(DType::UInt8, 4);
$z = $y->toDLPack();
unset($x);
echo live() - $base, " live\n";
unset($y);
echo live() - $base, " live (capsule holds the buffer)\n";
$q = BufferView::fromDLPack($z);
unset($z);
echo live() - $base, " live (imported view holds it)\n";
unset($q, $e, $cases, $w, $v, $b, $x, $r, $ro);
echo live(), " live at the end\n";
$c = bat_counts();
echo $c['made'] === $c['deleted'] ? "deleters ran exactly once\n" : "DELETER COUNT WRONG " . json_encode($c) . "\n";

echo "dtypes\n";
for ($i = 0; $i < 32; $i++) {
    $d = bat_dtype_info($i);
    $c = Buffers\DType::cases()[$i];
    if ($d['name'] !== $c->name || $d['value'] !== $c->value || $d['bits'] !== $c->bits() || $d['size'] !== ($c->byteSize() ?? 0) || $d['roundtrip'] !== $i || $d['dlpack_code'] !== $c->dlpack()['code']) echo "MISMATCH $i\n";
}
var_dump(bat_dtype_info(32), bat_dtype_info(-1));
echo "end\n";
--EXPECT--
bool(true)
bool(true)
int(24)
bool(true)
bool(true)
array(2) {
  [0]=>
  bool(false)
  [1]=>
  bool(true)
}
array(2) {
  [0]=>
  bool(true)
  [1]=>
  bool(false)
}
array(2) {
  [0]=>
  bool(false)
  [1]=>
  bool(false)
}
int(0)
int(23)
int(99)
1 live
info
Array
(
    [dtype] => Int16
    [bits] => 16
    [size] => 2
    [ndim] => 2
    [shape] => Array
        (
            [0] => 2
            [1] => 3
        )

    [strides] => Array
        (
            [0] => 4
            [1] => 1
        )

    [byte_offset] => 4
    [data_is_base_plus_offset] => 1
    [writable] => 1
    [buffer_length] => 24
    [host_accessible] => 1
    [device_type] => 1
    [device_id] => 0
    [is_view] => 1
)
Array
(
    [dtype] => Int16
    [bits] => 16
    [size] => 2
    [ndim] => 1
    [shape] => Array
        (
            [0] => 3
        )

    [strides] => Array
        (
            [0] => 1
        )

    [byte_offset] => 12
    [data_is_base_plus_offset] => 1
    [writable] => 1
    [buffer_length] => 24
    [host_accessible] => 1
    [device_type] => 1
    [device_id] => 0
    [is_view] => 1
)
Array
(
    [dtype] => Float4E2M1FN
    [bits] => 4
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

    [byte_offset] => 3
    [data_is_base_plus_offset] => 1
    [writable] => 1
    [buffer_length] => 24
    [host_accessible] => 1
    [device_type] => 1
    [device_id] => 0
    [is_view] => 1
)
Array
(
    [dtype] => Int32
    [bits] => 32
    [size] => 4
    [ndim] => 0
    [shape] => Array
        (
        )

    [strides] => Array
        (
        )

    [byte_offset] => 0
    [data_is_base_plus_offset] => 1
    [writable] => 1
    [buffer_length] => 24
    [host_accessible] => 1
    [device_type] => 1
    [device_id] => 0
    [is_view] => 1
)
views made through the C API
ok              ok [2,3] [3,1]
strided offset  ValueError: with the given strides and offset extends beyond the end of the buffer (24 bytes)
bad dtype       ValueError: Invalid dtype 99
beyond          ValueError: with the given strides and offset extends beyond the end of the buffer (24 bytes)
misaligned      ValueError: must be a multiple of the element size (2 bytes)
neg dim         ValueError: must contain only non-negative values
zero stride     ValueError: must contain only values of at least 1
packed stride   ValueError: axis 0 of a packed Float6E3M2FN view has a stride of 3 elements = 18 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 4 elements
not a buffer    TypeError: Expected an ArrayBuffer or DLPackBuffer
enum(Buffers\DType::Int16)
read-only memory
bool(false)
int(1)
Cannot write to a read-only BufferView
Cannot write to a read-only BufferView
lifetime: the deleter runs once, when the last view and buffer are gone
1 live
1 live (capsule holds the buffer)
1 live (imported view holds it)
0 live at the end
deleters ran exactly once
dtypes
NULL
NULL
end
