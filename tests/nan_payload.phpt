--TEST--
NaN conversions: float16 keeps the payload like numpy, the 8-bit formats canonicalise like ml_dtypes, float6/float4 store zero of the same sign
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer};
$nan = fn(string $hex) => unpack('E', hex2bin($hex))[1];   // big-endian double bits
$b = new Buffer(8);
$raw = $b->view(DType::UInt8);
$show = function (DType $t, string $hex) use ($b, $nan, $raw) {
    for ($i = 0; $i < 8; $i++) $raw[$i] = 0;
    $v = $b->view($t, 1);
    $v[0] = $nan($hex);
    $n = $t->byteSize() ?? 1;   // a packed element starts in the low bits of the first byte
    printf("%-14s %s -> %s\n", $t->name, $hex, bin2hex(substr($b->toString(), 0, $n)));
};
foreach (['7ff8000000000000', 'fff8000000000000', '7ff0000000000001', 'fff0000040000000', '7ff0040000000000', '7fffffffffffffff', 'ffffffffffffffff', '7ff4000000000000'] as $hex) {
    $show(DType::Float16, $hex);
}
foreach ([DType::BFloat16, DType::Float8E4M3, DType::Float8E4M3FN, DType::Float8E5M2, DType::Float8E4M3FNUZ, DType::Float8E8M0FNU, DType::Float6E2M3FN, DType::Float4E2M1FN] as $t) {
    foreach (['7ff8000000000000', 'fff8000000000000', '7ff0000000000001', 'ffffffffffffffff'] as $hex) $show($t, $hex);
}
?>
--EXPECT--
Float16        7ff8000000000000 -> 007e
Float16        fff8000000000000 -> 00fe
Float16        7ff0000000000001 -> 017c
Float16        fff0000040000000 -> 01fc
Float16        7ff0040000000000 -> 017c
Float16        7fffffffffffffff -> ff7f
Float16        ffffffffffffffff -> ffff
Float16        7ff4000000000000 -> 007d
BFloat16       7ff8000000000000 -> c07f
BFloat16       fff8000000000000 -> c0ff
BFloat16       7ff0000000000001 -> c07f
BFloat16       ffffffffffffffff -> c0ff
Float8E4M3     7ff8000000000000 -> 7c
Float8E4M3     fff8000000000000 -> fc
Float8E4M3     7ff0000000000001 -> 7c
Float8E4M3     ffffffffffffffff -> fc
Float8E4M3FN   7ff8000000000000 -> 7f
Float8E4M3FN   fff8000000000000 -> ff
Float8E4M3FN   7ff0000000000001 -> 7f
Float8E4M3FN   ffffffffffffffff -> ff
Float8E5M2     7ff8000000000000 -> 7e
Float8E5M2     fff8000000000000 -> fe
Float8E5M2     7ff0000000000001 -> 7e
Float8E5M2     ffffffffffffffff -> fe
Float8E4M3FNUZ 7ff8000000000000 -> 80
Float8E4M3FNUZ fff8000000000000 -> 80
Float8E4M3FNUZ 7ff0000000000001 -> 80
Float8E4M3FNUZ ffffffffffffffff -> 80
Float8E8M0FNU  7ff8000000000000 -> ff
Float8E8M0FNU  fff8000000000000 -> ff
Float8E8M0FNU  7ff0000000000001 -> ff
Float8E8M0FNU  ffffffffffffffff -> ff
Float6E2M3FN   7ff8000000000000 -> 00
Float6E2M3FN   fff8000000000000 -> 20
Float6E2M3FN   7ff0000000000001 -> 00
Float6E2M3FN   ffffffffffffffff -> 20
Float4E2M1FN   7ff8000000000000 -> 00
Float4E2M1FN   fff8000000000000 -> 08
Float4E2M1FN   7ff0000000000001 -> 00
Float4E2M1FN   ffffffffffffffff -> 08
