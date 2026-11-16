--TEST--
convert(): float to integer as PHP's (int); integers rounded to Float32 once
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_INT_SIZE < 8) die('skip 64-bit only'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
$values = [1e20, -1e20, NAN, INF, -INF, 300.7, -2.5, 1.5e19, -0.0];
$f = Tensor::fromArray($values, DType::Float64);
$php = array_map(fn($x) => @(int)$x, $values);
var_dump(iterator_to_array($f->convert(DType::Int64)) === $php);
$w = (new Buffer(8 * count($values)))->view(DType::Int64, count($values));
foreach ($values as $i => $x) $w[$i] = $x;
var_dump(iterator_to_array($w) === $php);
echo json_encode($f->convert(DType::Int8)), ' ', json_encode($f->convert(DType::UInt8)), ' ', json_encode($f->convert(DType::Bool)), "\n";
// 2^62 + 2^38 + 1 rounds to 2^62 + 2^39 in Float32; through a double it would round twice, to 2^62
$i = Tensor::fromArray([(1 << 62) + (1 << 38) + 1, -((1 << 62) + (1 << 38) + 1)], DType::Int64);
printf("%.0f %.0f\n", $i->convert(DType::Float32)[0], $i->convert(DType::Float32)[1]);
?>
--EXPECT--
bool(true)
bool(true)
[0,0,0,0,0,44,-2,0,0] [0,0,0,0,0,44,254,0,0] [true,true,true,true,true,true,true,true,false]
4611686568183201792 -4611686568183201792
