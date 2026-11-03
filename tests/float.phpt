--TEST--
Float16, BFloat16, Float32, Float64 element codecs
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (pack('S', 1) !== "\x01\x00") die('skip prints the bytes of elements: little-endian only'); if (PHP_INT_SIZE !== 8) die('skip 64-bit PHP only: 64-bit integer inputs'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer, Tensor};
$b = new Buffer(64);
foreach (['Float16', 'BFloat16', 'Float32', 'Float64'] as $name) {
    $v = $b->view(constant("Buffers\\DType::$name"), 4);
    echo $name, "\n";
    foreach ([0.0, -0.0, 1.0, -1.5, 0.1, 3.14159265358979, 65504.0, 65519.0, 65520.0, 1e-8, 6e-8, 1e38, 3.5e38, 1e300, INF, -INF, NAN, unpack('d', pack('H*', '000000000000f8ff'))[1], 5, -7, PHP_INT_MAX] as $x) {
        $v[1] = $x;
        $r = $v[1];
        printf("  %-24s => %s\n", var_export($x, true), is_nan($r) ? 'NAN' : (($r == 0 && fdiv(1, $r) < 0) ? '-0' : var_export($r, true)));
    }
}
echo "raw bytes\n";
$v = $b->view(DType::Float16, 2); $v[0] = 1.0; $v[1] = -2.0;
echo bin2hex(substr($b->toString(), 0, 4)), "\n";
$v = $b->view(DType::BFloat16, 2); $v[0] = 1.0; $v[1] = -2.0;
echo bin2hex(substr($b->toString(), 0, 4)), "\n";
$v = $b->view(DType::Float32, 1); $v[0] = 1.0;
echo bin2hex(substr($b->toString(), 0, 4)), "\n";
try { $v[0] = "1.5"; } catch (TypeError $e) { echo $e->getMessage(), "\n"; }
try { $v[0] = true; } catch (TypeError $e) { echo $e->getMessage(), "\n"; }
--EXPECT--
Float16
  0.0                      => 0.0
  -0.0                     => -0
  1.0                      => 1.0
  -1.5                     => -1.5
  0.1                      => 0.0999755859375
  3.14159265358979         => 3.140625
  65504.0                  => 65504.0
  65519.0                  => 65504.0
  65520.0                  => INF
  1.0E-8                   => 0.0
  6.0E-8                   => 5.960464477539063E-8
  1.0E+38                  => INF
  3.5E+38                  => INF
  1.0E+300                 => INF
  INF                      => INF
  -INF                     => -INF
  NAN                      => NAN
  NAN                      => NAN
  5                        => 5.0
  -7                       => -7.0
  9223372036854775807      => INF
BFloat16
  0.0                      => 0.0
  -0.0                     => -0
  1.0                      => 1.0
  -1.5                     => -1.5
  0.1                      => 0.10009765625
  3.14159265358979         => 3.140625
  65504.0                  => 65536.0
  65519.0                  => 65536.0
  65520.0                  => 65536.0
  1.0E-8                   => 1.0011717677116394E-8
  6.0E-8                   => 6.007030606269836E-8
  1.0E+38                  => 9.969209968386869E+37
  3.5E+38                  => INF
  1.0E+300                 => INF
  INF                      => INF
  -INF                     => -INF
  NAN                      => NAN
  NAN                      => NAN
  5                        => 5.0
  -7                       => -7.0
  9223372036854775807      => 9.223372036854776E+18
Float32
  0.0                      => 0.0
  -0.0                     => -0
  1.0                      => 1.0
  -1.5                     => -1.5
  0.1                      => 0.10000000149011612
  3.14159265358979         => 3.1415927410125732
  65504.0                  => 65504.0
  65519.0                  => 65519.0
  65520.0                  => 65520.0
  1.0E-8                   => 9.99999993922529E-9
  6.0E-8                   => 5.99999978589949E-8
  1.0E+38                  => 9.999999680285692E+37
  3.5E+38                  => INF
  1.0E+300                 => INF
  INF                      => INF
  -INF                     => -INF
  NAN                      => NAN
  NAN                      => NAN
  5                        => 5.0
  -7                       => -7.0
  9223372036854775807      => 9.223372036854776E+18
Float64
  0.0                      => 0.0
  -0.0                     => -0
  1.0                      => 1.0
  -1.5                     => -1.5
  0.1                      => 0.1
  3.14159265358979         => 3.14159265358979
  65504.0                  => 65504.0
  65519.0                  => 65519.0
  65520.0                  => 65520.0
  1.0E-8                   => 1.0E-8
  6.0E-8                   => 6.0E-8
  1.0E+38                  => 1.0E+38
  3.5E+38                  => 3.5E+38
  1.0E+300                 => 1.0E+300
  INF                      => INF
  -INF                     => -INF
  NAN                      => NAN
  NAN                      => NAN
  5                        => 5.0
  -7                       => -7.0
  9223372036854775807      => 9.223372036854776E+18
raw bytes
003c00c0
803f00c0
0000803f
Cannot write string to a Tensor of dtype Float32
Cannot write bool to a Tensor of dtype Float32
