--TEST--
Views of the same buffer in different dtypes, writes through any alias
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, ArrayBuffer};
$b = new ArrayBuffer(16);
$bytes = $b->view(DType::UInt8);
$words = $b->view(DType::UInt32, 4);
$f = $b->view(DType::Float32, 4);
$f[0] = 1.0; $f[1] = -2.0;
var_dump(dechex($words[0]), dechex($words[1]), $bytes[3], $bytes[7]);
$words[2] = 0x7fc00000; var_dump(is_nan($f[2]));
$bytes[12] = 0xff; var_dump($words[3]);
$mixed = $b->view(DType::Int16, [2, 4]);
$mixed[1][3] = -1; var_dump($bytes[14], $bytes[15], $words[3]);
--EXPECT--
string(8) "3f800000"
string(8) "c0000000"
int(63)
int(192)
bool(true)
int(255)
int(255)
int(255)
int(4294902015)
