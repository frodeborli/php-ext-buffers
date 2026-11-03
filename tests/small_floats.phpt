--TEST--
Small float formats: values, specials and overflow follow ml_dtypes
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer};
$b = new Buffer(16);
$negnan = unpack('d', pack('H*', '000000000000f8ff'))[1];   // NaN with the sign bit set
$inputs = [0.0, -0.0, 1.0, -1.0, 0.1, 1.5, 3.0, 6.0, 7.0, 100.0, 448.0, 464.0, 465.0, 480.0, 1000.0, 57344.0, 61440.0, 1e6, 1e-3, 1e-6, 1e-10, INF, -INF, NAN, $negnan];
foreach (DType::cases() as $c) {
    if ($c->bits() > 8 || !$c->isFloatingPoint()) continue;
    $v = $b->view($c, 1);
    echo $c->name, "\n";
    foreach ($inputs as $x) {
        $v[0] = $x;
        $r = $v[0];
        printf("  %-10s code=%02x %s\n", is_nan($x) ? (fdiv(1, 1) && ord(pack('E', $x)[0]) >> 7 ? '-NAN' : 'NAN') : var_export($x, true), ord($b->toString()[0]) & ((1 << $c->bits()) - 1), is_nan($r) ? 'NAN' : (($r == 0 && fdiv(1, $r) < 0) ? '-0' : var_export($r, true)));
    }
}
echo "decode every code\n";
foreach (DType::cases() as $c) {
    if ($c->bits() > 8 || !$c->isFloatingPoint()) continue;
    $v = $b->view($c, 1); $raw = $b->view(DType::UInt8, 1);
    $seen = [];
    for ($code = 0; $code < (1 << $c->bits()); $code++) {
        $raw[0] = $code; $r = $v[0];
        $seen[] = is_nan($r) ? 'nan' : (string)$r;
    }
    printf("%-20s %d codes, md5 %s\n", $c->name, count($seen), md5(implode(',', $seen)));
}
echo "padding bits above the format width are ignored\n";
$raw = $b->view(DType::UInt8, 1); $f4 = $b->view(DType::Float4E2M1FN, 1);
$raw[0] = 0xF2; var_dump($f4[0]); $raw[0] = 0x02; var_dump($f4[0]);
--EXPECT--
Float8E3M4
  0.0        code=00 0.0
  -0.0       code=80 -0
  1.0        code=30 1.0
  -1.0       code=b0 -1.0
  0.1        code=06 0.09375
  1.5        code=38 1.5
  3.0        code=48 3.0
  6.0        code=58 6.0
  7.0        code=5c 7.0
  100.0      code=70 INF
  448.0      code=70 INF
  464.0      code=70 INF
  465.0      code=70 INF
  480.0      code=70 INF
  1000.0     code=70 INF
  57344.0    code=70 INF
  61440.0    code=70 INF
  1000000.0  code=70 INF
  0.001      code=00 0.0
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=70 INF
  -INF       code=f0 -INF
  NAN        code=78 NAN
  -NAN       code=f8 NAN
Float8E4M3
  0.0        code=00 0.0
  -0.0       code=80 -0
  1.0        code=38 1.0
  -1.0       code=b8 -1.0
  0.1        code=1d 0.1015625
  1.5        code=3c 1.5
  3.0        code=44 3.0
  6.0        code=4c 6.0
  7.0        code=4e 7.0
  100.0      code=6c 96.0
  448.0      code=78 INF
  464.0      code=78 INF
  465.0      code=78 INF
  480.0      code=78 INF
  1000.0     code=78 INF
  57344.0    code=78 INF
  61440.0    code=78 INF
  1000000.0  code=78 INF
  0.001      code=01 0.001953125
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=78 INF
  -INF       code=f8 -INF
  NAN        code=7c NAN
  -NAN       code=fc NAN
Float8E4M3B11FNUZ
  0.0        code=00 0.0
  -0.0       code=00 0.0
  1.0        code=58 1.0
  -1.0       code=d8 -1.0
  0.1        code=3d 0.1015625
  1.5        code=5c 1.5
  3.0        code=64 3.0
  6.0        code=6c 6.0
  7.0        code=6e 7.0
  100.0      code=80 NAN
  448.0      code=80 NAN
  464.0      code=80 NAN
  465.0      code=80 NAN
  480.0      code=80 NAN
  1000.0     code=80 NAN
  57344.0    code=80 NAN
  61440.0    code=80 NAN
  1000000.0  code=80 NAN
  0.001      code=08 0.0009765625
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=80 NAN
  -INF       code=80 NAN
  NAN        code=80 NAN
  -NAN       code=80 NAN
Float8E4M3FN
  0.0        code=00 0.0
  -0.0       code=80 -0
  1.0        code=38 1.0
  -1.0       code=b8 -1.0
  0.1        code=1d 0.1015625
  1.5        code=3c 1.5
  3.0        code=44 3.0
  6.0        code=4c 6.0
  7.0        code=4e 7.0
  100.0      code=6c 96.0
  448.0      code=7e 448.0
  464.0      code=7e 448.0
  465.0      code=7f NAN
  480.0      code=7f NAN
  1000.0     code=7f NAN
  57344.0    code=7f NAN
  61440.0    code=7f NAN
  1000000.0  code=7f NAN
  0.001      code=01 0.001953125
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=7f NAN
  -INF       code=ff NAN
  NAN        code=7f NAN
  -NAN       code=ff NAN
Float8E4M3FNUZ
  0.0        code=00 0.0
  -0.0       code=00 0.0
  1.0        code=40 1.0
  -1.0       code=c0 -1.0
  0.1        code=25 0.1015625
  1.5        code=44 1.5
  3.0        code=4c 3.0
  6.0        code=54 6.0
  7.0        code=56 7.0
  100.0      code=74 96.0
  448.0      code=80 NAN
  464.0      code=80 NAN
  465.0      code=80 NAN
  480.0      code=80 NAN
  1000.0     code=80 NAN
  57344.0    code=80 NAN
  61440.0    code=80 NAN
  1000000.0  code=80 NAN
  0.001      code=01 0.0009765625
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=80 NAN
  -INF       code=80 NAN
  NAN        code=80 NAN
  -NAN       code=80 NAN
Float8E5M2
  0.0        code=00 0.0
  -0.0       code=80 -0
  1.0        code=3c 1.0
  -1.0       code=bc -1.0
  0.1        code=2e 0.09375
  1.5        code=3e 1.5
  3.0        code=42 3.0
  6.0        code=46 6.0
  7.0        code=47 7.0
  100.0      code=56 96.0
  448.0      code=5f 448.0
  464.0      code=5f 448.0
  465.0      code=5f 448.0
  480.0      code=60 512.0
  1000.0     code=64 1024.0
  57344.0    code=7b 57344.0
  61440.0    code=7c INF
  1000000.0  code=7c INF
  0.001      code=14 0.0009765625
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=7c INF
  -INF       code=fc -INF
  NAN        code=7e NAN
  -NAN       code=fe NAN
Float8E5M2FNUZ
  0.0        code=00 0.0
  -0.0       code=00 0.0
  1.0        code=40 1.0
  -1.0       code=c0 -1.0
  0.1        code=32 0.09375
  1.5        code=42 1.5
  3.0        code=46 3.0
  6.0        code=4a 6.0
  7.0        code=4b 7.0
  100.0      code=5a 96.0
  448.0      code=63 448.0
  464.0      code=63 448.0
  465.0      code=63 448.0
  480.0      code=64 512.0
  1000.0     code=68 1024.0
  57344.0    code=7f 57344.0
  61440.0    code=80 NAN
  1000000.0  code=80 NAN
  0.001      code=18 0.0009765625
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=80 NAN
  -INF       code=80 NAN
  NAN        code=80 NAN
  -NAN       code=80 NAN
Float8E8M0FNU
  0.0        code=ff NAN
  -0.0       code=ff NAN
  1.0        code=7f 1.0
  -1.0       code=ff NAN
  0.1        code=7c 0.125
  1.5        code=80 2.0
  3.0        code=81 4.0
  6.0        code=82 8.0
  7.0        code=82 8.0
  100.0      code=86 128.0
  448.0      code=88 512.0
  464.0      code=88 512.0
  465.0      code=88 512.0
  480.0      code=88 512.0
  1000.0     code=89 1024.0
  57344.0    code=8f 65536.0
  61440.0    code=8f 65536.0
  1000000.0  code=93 1048576.0
  0.001      code=75 0.0009765625
  1.0E-6     code=6b 9.5367431640625E-7
  1.0E-10    code=5e 1.1641532182693481E-10
  INF        code=ff NAN
  -INF       code=ff NAN
  NAN        code=ff NAN
  -NAN       code=ff NAN
Float6E2M3FN
  0.0        code=00 0.0
  -0.0       code=20 -0
  1.0        code=08 1.0
  -1.0       code=28 -1.0
  0.1        code=01 0.125
  1.5        code=0c 1.5
  3.0        code=14 3.0
  6.0        code=1c 6.0
  7.0        code=1e 7.0
  100.0      code=1f 7.5
  448.0      code=1f 7.5
  464.0      code=1f 7.5
  465.0      code=1f 7.5
  480.0      code=1f 7.5
  1000.0     code=1f 7.5
  57344.0    code=1f 7.5
  61440.0    code=1f 7.5
  1000000.0  code=1f 7.5
  0.001      code=00 0.0
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=1f 7.5
  -INF       code=3f -7.5
  NAN        code=20 -0
  -NAN       code=00 0.0
Float6E3M2FN
  0.0        code=00 0.0
  -0.0       code=20 -0
  1.0        code=0c 1.0
  -1.0       code=2c -1.0
  0.1        code=02 0.125
  1.5        code=0e 1.5
  3.0        code=12 3.0
  6.0        code=16 6.0
  7.0        code=17 7.0
  100.0      code=1f 28.0
  448.0      code=1f 28.0
  464.0      code=1f 28.0
  465.0      code=1f 28.0
  480.0      code=1f 28.0
  1000.0     code=1f 28.0
  57344.0    code=1f 28.0
  61440.0    code=1f 28.0
  1000000.0  code=1f 28.0
  0.001      code=00 0.0
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=1f 28.0
  -INF       code=3f -28.0
  NAN        code=20 -0
  -NAN       code=00 0.0
Float4E2M1FN
  0.0        code=00 0.0
  -0.0       code=08 -0
  1.0        code=02 1.0
  -1.0       code=0a -1.0
  0.1        code=00 0.0
  1.5        code=03 1.5
  3.0        code=05 3.0
  6.0        code=07 6.0
  7.0        code=07 6.0
  100.0      code=07 6.0
  448.0      code=07 6.0
  464.0      code=07 6.0
  465.0      code=07 6.0
  480.0      code=07 6.0
  1000.0     code=07 6.0
  57344.0    code=07 6.0
  61440.0    code=07 6.0
  1000000.0  code=07 6.0
  0.001      code=00 0.0
  1.0E-6     code=00 0.0
  1.0E-10    code=00 0.0
  INF        code=07 6.0
  -INF       code=0f -6.0
  NAN        code=08 -0
  -NAN       code=00 0.0
decode every code
Float8E3M4           256 codes, md5 29022d0b44a1cb48af09c56301801f87
Float8E4M3           256 codes, md5 b12935c48a7565a05fe17c4378e2bd7f
Float8E4M3B11FNUZ    256 codes, md5 5a714161e3091104d6fe45ef50c5ec40
Float8E4M3FN         256 codes, md5 837f7d09e54bf2b478799856e9d7057a
Float8E4M3FNUZ       256 codes, md5 7221fca6677251da7659734bed732b73
Float8E5M2           256 codes, md5 66556d0318fd26fb99ca9ff4f26d6143
Float8E5M2FNUZ       256 codes, md5 503dee3b50ddde0bf9b7bdef09debe40
Float8E8M0FNU        256 codes, md5 05891d714695a7537e0605b9ec65ba45
Float6E2M3FN         64 codes, md5 49f47cb1aa3f345c9a2291612ec6af7a
Float6E3M2FN         64 codes, md5 53f48786240d5fcfe8b07659b3a952b6
Float4E2M1FN         16 codes, md5 8ef1f46f5292b1a4d3a6599a5e79c813
padding bits above the format width are ignored
float(1)
float(1)
