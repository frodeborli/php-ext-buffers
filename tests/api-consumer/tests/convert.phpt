--TEST--
C API: buffers_convert() between dtypes, strides and packed formats
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\DType;
function conv(DType $from, array|string $in, DType $to, int $n, int $ss = 1, int $ds = 1): string {
    if (is_array($in)) $in = pack(...$in);
    return bin2hex(bat_convert($from, $in, $to, $n, $ss, $ds));
}
// integers wrap
echo conv(DType::Int32, ['l*', 300, -1, 128], DType::Int8, 3), "\n";          // 2c ff 80
echo conv(DType::Int8, ['c*', -1, 5], DType::UInt16, 2), "\n";                 // ffff 0500
echo conv(DType::UInt8, ['C*', 255], DType::Int64, 1), "\n";                   // 255
// float -> int truncates toward zero
echo conv(DType::Float64, ['d*', 2.9, -2.9, 1e3], DType::Int16, 3), "\n";
// int -> float formats, and float -> small floats
echo conv(DType::Int32, ['l*', 3, -2], DType::Float16, 2), "\n";               // 0042 00c0
echo conv(DType::Float32, ['g*', 1.0, 3.140625, 1e30], DType::BFloat16, 3), "\n";
echo conv(DType::Float64, ['d*', 0.5, 448.0, 1000.0, NAN], DType::Float8E4M3FN, 4), "\n";
// bool
echo conv(DType::Float64, ['d*', 0.0, -0.0, 0.1, NAN], DType::Bool, 4), "\n";
echo conv(DType::Bool, "\x00\x01", DType::Float32, 2), "\n";
// complex
echo conv(DType::Complex64, ['g*', 1.5, 2.5], DType::Float32, 1), "\n";         // real part
echo conv(DType::Float32, ['g*', 1.5], DType::Complex128, 1), "\n";
echo conv(DType::Complex64, ['g*', 0.0, 2.0], DType::Bool, 1), "\n";
// strides
echo conv(DType::Int16, ['s*', 1, 2, 3, 4, 5, 6], DType::Int32, 3, 2, 1), "\n"; // elements 0, 2, 4
echo conv(DType::Int16, ['s*', 7, 8, 9], DType::Int16, 3, 1, 2), "\n";          // 7 0 8 0 9
// packed
echo conv(DType::Int8, ['c*', -1, 7, -8, 1], DType::Int4, 4), "\n";            // f 7 8 1 -> 0x7f 0x18
echo conv(DType::Int4, "\x7f\x18", DType::Int8, 4), "\n";                       // ff 07 f8 01
echo conv(DType::UInt8, ['C*', 1, 2, 3, 0], DType::UInt2, 4), "\n";             // 1 | 2<<2 | 3<<4 = 0x39
echo conv(DType::Float32, ['g*', 1.0, 6.0, 0.5, -3.0], DType::Float4E2M1FN, 4), "\n";
echo conv(DType::Float4E2M1FN, "\x21\x1a", DType::Float32, 4), "\n";
echo conv(DType::Float32, ['g*', 1.0, 0.25, 7.5, -1.0], DType::Float6E2M3FN, 4), "\n";
echo conv(DType::Float6E2M3FN, hex2bin(conv(DType::Float32, ['g*', 1.0, 0.25, 7.5, -1.0], DType::Float6E2M3FN, 4)), DType::Float32, 4), "\n";
// uint64 beyond the int64 range
echo conv(DType::Float64, ['d*', 1.8446744073709550e19], DType::UInt64, 1), "\n";
echo conv(DType::UInt64, ['P*', -1], DType::Float64, 1), "\n";
?>
--EXPECT--
2cff80
ffff0500
ff00000000000000
0200feffe803
004200c0
803f49404a71
307e7f7f
00000101
000000000000803f
0000c03f
000000000000f83f0000000000000000
01
010000000300000005000000
07000000080000000900
7f18
ff07f801
39
72d1
0000003f0000803f000080bf0000003f
88f0a1
0000803f0000803e0000f040000080bf
00f8ffffffffffff
000000000000f043
