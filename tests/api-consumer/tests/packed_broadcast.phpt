--TEST--
C API: buffers_tensor_create() and DLPack import refuse stride 0 on the last axis of a packed sub-byte tensor; other axes broadcast
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType};
function t(string $label, callable $f): void {
    try { $r = $f(); echo "$label: ", json_encode($r->shape), ' ', json_encode($r->strides), ' ', $r->buffer->isWritable() ? 'writable' : 'read-only', "\n"; }
    catch (Throwable $e) { echo "$label: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}
$b = new Buffer(4);
$int4 = array_search(DType::Int4, DType::cases(), true);
t('tensor_create, last stride 0', fn() => bat_view($b, $int4, [2, 4], [2, 0]));
t('tensor_create, stride 0 on axis 0', fn() => bat_view($b, $int4, [3, 4], [0, 1]));
foreach ([[0, 4], [1, 2], [15, 6], [17, 4]] as [$code, $bits]) {
    t("import $code/$bits [8] stride [0]", fn() => bat_import($code, $bits, 1, [8], [0]));
    t("import $code/$bits [2,8] strides [8,0]", fn() => bat_import($code, $bits, 1, [2, 8], [8, 0]));
}
t('import 0/4 [3,8] strides [0,1]', fn() => bat_import(0, 4, 1, [3, 8], [0, 1]));
?>
--EXPECTF--
tensor_create, last stride 0: ValueError: the last axis of a packed Int4 view must have stride 1
tensor_create, stride 0 on axis 0: [3,4] [0,1] writable
import 0/4 [8] stride [0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 0/4 [2,8] strides [8,0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 1/2 [8] stride [0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 1/2 [2,8] strides [8,0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 15/6 [8] stride [0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 15/6 [2,8] strides [8,0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 17/4 [8] stride [0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 17/4 [2,8] strides [8,0]: ValueError: The last axis of a packed sub-byte tensor must have stride 1
import 0/4 [3,8] strides [0,1]: [3,8] [0,1] %s
