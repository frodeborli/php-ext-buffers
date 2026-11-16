--TEST--
Tensor::each(): every operator on every pair of dtypes equals the eager operator, bit for bit
--SKIPIF--
<?php if (!extension_loaded('buffers') || !method_exists(Buffers\Tensor::class, 'each')) die('skip built without the extras'); ?>
--FILE--
<?php
// each(fn($a, $b) => $a OP $b, $b) must equal $a OP $b exactly: dtype, shape, every bit of every element (NaN payloads
// aside), or the same exception class; for every pair of arithmetic dtypes and PHP scalars and every operator
use Buffers\Tensor; use Buffers\DType;
mt_srand(7);
$dts = [DType::Bool, DType::Int8, DType::Int16, DType::Int32, DType::Int64, DType::UInt8, DType::UInt16, DType::UInt32, DType::UInt64,
        DType::Float16, DType::BFloat16, DType::Float32, DType::Float64];
if (PHP_INT_SIZE == 4) {   // 32-bit PHP has no Int64, UInt64 or UInt32 tensors
    $dts = array_values(array_filter($dts, fn($d) => !in_array($d, [DType::Int64, DType::UInt64, DType::UInt32], true)));
}
$ops = [
 '+' => [fn($a,$b)=>$a+$b, fn($a,$b)=>$a+$b], '-' => [fn($a,$b)=>$a-$b, fn($a,$b)=>$a-$b], '*' => [fn($a,$b)=>$a*$b, fn($a,$b)=>$a*$b],
 '/' => [fn($a,$b)=>$a/$b, fn($a,$b)=>$a/$b], '%' => [fn($a,$b)=>$a%$b, fn($a,$b)=>$a%$b], '**' => [fn($a,$b)=>$a**$b, fn($a,$b)=>$a**$b],
 '<<' => [fn($a,$b)=>$a<<$b, fn($a,$b)=>$a<<$b], '>>' => [fn($a,$b)=>$a>>$b, fn($a,$b)=>$a>>$b],
 '&' => [fn($a,$b)=>$a&$b, fn($a,$b)=>$a&$b], '|' => [fn($a,$b)=>$a|$b, fn($a,$b)=>$a|$b], '^' => [fn($a,$b)=>$a^$b, fn($a,$b)=>$a^$b],
];
function rnd(DType $d, int $n): Tensor {
    $v = [];
    for ($i = 0; $i < $n; $i++) {
        $r = mt_rand(0, 9);
        $v[] = match (true) {
            $d === DType::Bool => (bool)mt_rand(0, 1),
            $d->isFloatingPoint() => [0.0, -0.0, 1.5, -2.25, NAN, INF, -INF, 3.0, mt_rand() / 7919.0 - 1e5, 1e-3][$r],
            default => [0, 1, -1, 2, 7, 63, 64, mt_rand(-200, 200), PHP_INT_MAX, PHP_INT_MIN][$r],
        };
    }
    try { return Tensor::fromArray($v, $d); } catch (Throwable) {
        return (Tensor::fromArray(array_map(fn($x) => is_int($x) ? $x & 0x7f : $x, $v), PHP_INT_SIZE == 4 ? DType::Int32 : DType::Int64))->convert($d);
    }
}
function bytes(Tensor $t): string { return implode(',', array_map(fn($x) => is_float($x) ? (is_nan($x) ? 'nan' : bin2hex(pack('e', $x))) : var_export($x, true), flatten($t->toArray()))); }
function flatten($a) { if (!is_array($a)) return [$a]; $r = []; foreach ($a as $x) foreach (flatten($x) as $y) $r[] = $y; return $r; }
function outcome(callable $f): string {
    try { $t = $f(); return $t->getDType()->name . ' ' . json_encode($t->getShape()->toArray()) . ' ' . bytes($t); }
    catch (Throwable $e) { return get_class($e); }
}
$checked = $mism = 0;
foreach ($dts as $da) foreach (array_merge($dts, ['int', 'float', 'bool']) as $db) {
    $a = rnd($da, 6);
    $b = is_string($db) ? match ($db) { 'int' => mt_rand(-3, 70), 'float' => 2.5, 'bool' => true } : rnd($db, 6);
    foreach ($ops as $sym => [$eager, $each]) {
        $e = outcome(fn() => $eager($a, $b));
        $g = outcome(fn() => $a->each($each, $b));
        $checked++;
        if ($e !== $g) { $mism++; if ($mism <= 15) echo "MISMATCH {$da->name} $sym ", is_string($db) ? $db : $db->name, "\n  eager: $e\n  each:  $g\n"; }
    }
}
echo $checked > 1000 ? "more than 1000 cases" : "only $checked cases", ", mismatches $mism\n";
--EXPECT--
more than 1000 cases, mismatches 0
