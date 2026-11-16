--TEST--
Tensor::each() and assign(): inputs, broadcasting, coordinates, selection, functions, captures, array parameters
--SKIPIF--
<?php if (!extension_loaded('buffers') || !method_exists(Buffers\Tensor::class, 'each')) die('skip built without the extras');
if (PHP_INT_SIZE < 8) die('skip 64-bit PHP: the expected output names Int64 (PHP\'s int on 64-bit PHP)'); ?>
--FILE--
<?php
use Buffers\{DType, Tensor};

function show(string $name, callable $f): void {
    try { $r = $f(); echo "$name: ", json_encode($r->toArray()), ' ', $r->getDType()->name, ' ', json_encode($r->getShape()->toArray()), "\n"; }
    catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}

$a = Tensor::fromArray([1.0, 2.0, 3.0, 4.0], DType::Float32);
$b = Tensor::fromArray([10, 20, 30, 40], DType::Int32);
$m = Tensor::fromArray([[1, 2, 3], [4, 5, 6]], DType::Int16);
$img = Tensor::fromArray([[[1, 2, 3, 4], [5, 6, 7, 8]], [[9, 10, 11, 12], [13, 14, 15, 16]]], DType::UInt8);

echo "-- one input\n";
show('map', fn() => $a->each(fn($v) => $v * 2 + 1));
show('same as operators', fn() => $a * 2 + 1);
show('weak scalar keeps the dtype', fn() => $b->each(fn($v) => $v + 1));
show('float scalar with ints', fn() => $b->each(fn($v) => $v * 0.5));
show('division gives a float', fn() => $b->each(fn($v) => $v / 4));
show('constant result', fn() => $a->each(fn($v) => 7));

echo "-- several inputs, broadcast\n";
show('two tensors', fn() => $a->each(fn($x, $y) => $x + $y, $b));
show('row and column', fn() => $m->each(fn($x, $y) => $x * $y, Tensor::fromArray([[10], [100]], DType::Int16)));
show('a PHP scalar as an input', fn() => $a->each(fn($x, $k) => $x * $k, 3));
show('unused input still broadcasts', fn() => $a->each(fn($x, $y) => $x, Tensor::fromArray([[0], [0]], DType::Float32)));

echo "-- coordinates\n";
show('row and column index', fn() => $m->each(fn($v, $y, $x) => $y * 10 + $x));
show('leading axes only', fn() => $m->each(fn($v, $y) => $v * $y));
show('gradient', fn() => $m->each(fn($v, $y, $x) => $x / 2));

echo "-- selection\n";
show('ternary', fn() => $b->each(fn($v) => $v > 15 ? $v : 0));
show('short ternary', fn() => Tensor::fromArray([0, 3, 0, 5], DType::Int8)->each(fn($v) => $v ?: -1));
show('and / or / not', fn() => $b->each(fn($v) => ($v > 10 && $v < 40) || !($v != 10)));
show('mask input', fn() => $a->each(fn($v, $keep) => $keep ? $v : -$v, Tensor::fromArray([true, false, true, false], DType::Bool)));
show('if / elseif with returns', fn() => Tensor::fromArray([-2, 0, 3], DType::Int32)->each(function ($v) { if ($v > 0) { return 1; } elseif ($v < 0) { return -1; } return 0; }));

echo "-- functions\n";
show('min max clamp', fn() => $a->each(fn($v) => max(2.0, min($v, 3.0))));
show('abs floor ceil round', fn() => Tensor::fromArray([-1.5, -0.5, 0.5, 2.5], DType::Float64)->each(fn($v) => [abs($v), floor($v), ceil($v), round($v)]));
show('sqrt of ints is Float64', fn() => $b->each(fn($v) => sqrt($v)));
show('intdiv and fmod', fn() => $b->each(fn($v) => [intdiv($v, 7), $v % 7]));
show('casts', fn() => $a->each(fn($v) => [(int)($v * 1.5), (float)$v, (bool)($v - 1) ? 1 : 0]));

echo "-- captures\n";
$k = 0.5; $n = 3; $flag = true;
show('scalars', fn() => $a->each(fn($v) => $flag ? $v * $k + $n : 0));
show('neighbour of a captured tensor', fn() => $m->each(fn($v, $y, $x) => $x > 0 ? $m[$y][$x - 1] : 0));
show('computed index (gather)', fn() => $b->each(fn($v, $i) => $b[3 - $i]));
$lut = Tensor::fromArray([0, 100, 200, 300], DType::Int32);
show('lookup table', fn() => Tensor::fromArray([3, 0, 2, 1], DType::UInt8)->each(fn($v) => $lut[$v]));

echo "-- array parameters: the last axis\n";
show('sum of channels', fn() => $img->each(fn(array $p) => $p[0] + $p[1] + $p[2]));
show('swap channels', fn() => $img->each(fn(array $p) => [$p[2], $p[1], $p[0], $p[3]]));
show('return the array', fn() => $img->each(fn(array $p) => $p));
show('with coordinates', fn() => $img->each(fn(array $p, $y, $x) => [$y, $x]));
show('two array inputs', fn() => $img->each(fn(array $p, array $q) => [$p[0] + $q[0], $p[3] + $q[3]], $img));

echo "-- assign\n";
$t = Tensor::fromArray([1.5, -2.5, 3.0], DType::Float64);
$r = $t->assign(fn($v) => $v * 2);
show('in place', fn() => $t);
var_dump($r === $t);
$u = Tensor::fromArray([[10, 20], [30, 40]], DType::UInt8);
$u->assign(fn($v) => $v + 250);
show('wraps like the operators', fn() => $u);
$f = Tensor::fromArray([1, 2, 3], DType::UInt8);
$f->assign(fn($v) => $v * 1.75);
show('converted to the dtype', fn() => $f);
$row = Tensor::fromArray([[1, 2, 3, 4], [5, 6, 7, 8]], DType::Int32);
$row->slice(0, 1, 1)->assign(fn($v) => -$v);
show('through a view', fn() => $row);
$px = Tensor::fromArray([[1, 2, 3, 4], [5, 6, 7, 8]], DType::UInt8);
$px->assign(fn(array $p) => [$p[2], $p[1], $p[0], $p[3]]);
show('channels in place', fn() => $px);
$blur = Tensor::fromArray([0, 0, 9, 0, 0], DType::Float32);
$blur->assign(fn($v, $i) => ($i > 0 ? $blur[$i - 1] : 0) + $v + ($i < 4 ? $blur[$i + 1] : 0));
show('reads see the tensor before the call', fn() => $blur);
--EXPECT--
-- one input
map: [3,5,7,9] Float32 [4]
same as operators: [3,5,7,9] Float32 [4]
weak scalar keeps the dtype: [11,21,31,41] Int32 [4]
float scalar with ints: [5,10,15,20] Float64 [4]
division gives a float: [2.5,5,7.5,10] Float64 [4]
constant result: [7,7,7,7] Int64 [4]
-- several inputs, broadcast
two tensors: [11,22,33,44] Float32 [4]
row and column: [[10,20,30],[400,500,600]] Int16 [2,3]
a PHP scalar as an input: [3,6,9,12] Float32 [4]
unused input still broadcasts: [[1,2,3,4],[1,2,3,4]] Float32 [2,4]
-- coordinates
row and column index: [[0,1,2],[10,11,12]] Int64 [2,3]
leading axes only: [[0,0,0],[4,5,6]] Int64 [2,3]
gradient: [[0,0.5,1],[0,0.5,1]] Float64 [2,3]
-- selection
ternary: [0,20,30,40] Int32 [4]
short ternary: [-1,3,-1,5] Int8 [4]
and / or / not: [true,true,true,false] Bool [4]
mask input: [1,-2,3,-4] Float32 [4]
if / elseif with returns: [-1,0,1] Int64 [3]
-- functions
min max clamp: [2,2,3,3] Float32 [4]
abs floor ceil round: [[1.5,-2,-1,-2],[0.5,-1,-0,-1],[0.5,0,1,1],[2.5,2,3,3]] Float64 [4,4]
sqrt of ints is Float64: [3.1622776601683795,4.47213595499958,5.477225575051661,6.324555320336759] Float64 [4]
intdiv and fmod: [[1,3],[2,6],[4,2],[5,5]] Int32 [4,2]
casts: [[1,1,0],[3,2,1],[4,3,1],[6,4,1]] Float64 [4,3]
-- captures
scalars: [3.5,4,4.5,5] Float32 [4]
neighbour of a captured tensor: [[0,1,2],[0,4,5]] Int16 [2,3]
computed index (gather): [40,30,20,10] Int32 [4]
lookup table: [300,0,200,100] Int32 [4]
-- array parameters: the last axis
sum of channels: [[6,18],[30,42]] UInt8 [2,2]
swap channels: [[[3,2,1,4],[7,6,5,8]],[[11,10,9,12],[15,14,13,16]]] UInt8 [2,2,4]
return the array: [[[1,2,3,4],[5,6,7,8]],[[9,10,11,12],[13,14,15,16]]] UInt8 [2,2,4]
with coordinates: [[[0,0],[0,1]],[[1,0],[1,1]]] Int64 [2,2,2]
two array inputs: [[[2,8],[10,16]],[[18,24],[26,32]]] UInt8 [2,2,2]
-- assign
in place: [3,-5,6] Float64 [3]
bool(true)
wraps like the operators: [[4,14],[24,34]] UInt8 [2,2]
converted to the dtype: [1,3,5] UInt8 [3]
through a view: [[1,2,3,4],[-5,-6,-7,-8]] Int32 [2,4]
channels in place: [[3,2,1,4],[7,6,5,8]] UInt8 [2,4]
reads see the tensor before the call: [0,9,9,9,0] Float32 [5]
