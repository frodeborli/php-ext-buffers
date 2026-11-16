--TEST--
C API: buffers_tensor_info points at the tensor's own shape and strides (no array of BUFFERS_MAX_NDIM in the ABI); buffers_max_ndim()
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
var_dump(bat_max_ndim() === Tensor::MAX_NDIM);
$b = new Buffer(4 * 2 * 3 * 4);
$t = $b->view(DType::Float32, [2, 3, 4]);
foreach ([$t, $t->transpose(0, 2), $t->slice(1, 1, 2, 1), $t[1], $t->broadcastTo([5, 2, 3, 4]), $b->view(DType::Int4, [2, 6], [16, 1]), $b->view(DType::UInt8, [])] as $v) {
    $i = bat_info($v);
    echo json_encode($i['shape']), ' ', json_encode($i['strides']), ' ', json_encode($v->strides), ' ', bat_info_twice($v) ? 'same pointers' : 'DIFFERENT', "\n";
}
// sub-views come from a pool: a reused object reports its own strides, not those of the tensor it was before
for ($k = 0; $k < 3; $k++) {
    $x = $b->view(DType::Float32, [2, 3, 4])->transpose(0, 2)[1];
    echo json_encode(bat_info($x)['strides']), ' ';
    unset($x);
    $y = $b->view(DType::UInt16, [6, 8])[2];
    echo json_encode(bat_info($y)['strides']), "\n";
    unset($y);
}
?>
--EXPECT--
bool(true)
[2,3,4] [12,4,1] [12,4,1] same pointers
[4,3,2] [1,4,12] [1,4,12] same pointers
[2,2,4] [12,4,1] [12,4,1] same pointers
[3,4] [4,1] [4,1] same pointers
[5,2,3,4] [0,12,4,1] [0,12,4,1] same pointers
[2,6] [16,1] [16,1] same pointers
[] [] [] same pointers
[4,12] [1]
[4,12] [1]
[4,12] [1]
