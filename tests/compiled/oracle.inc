<?php
// A compiled loop computing $a[$i] OP $b[$i] (or OP a PHP scalar) into a tensor of the eager result's dtype must equal
// $a OP $b exactly: every bit of every element (NaN payloads aside), or the same exception class; for every pair of
// arithmetic dtypes and PHP scalars and every operator
use Buffers\{Compiled, Tensor, DType};
mt_srand(7);
$dts = [DType::Bool, DType::Int8, DType::Int16, DType::Int32, DType::Int64, DType::UInt8, DType::UInt16, DType::UInt32, DType::UInt64,
        DType::Float16, DType::BFloat16, DType::Float32, DType::Float64];
if (PHP_INT_SIZE == 4) {   // 32-bit PHP has no Int64, UInt64 or UInt32 tensors
    $dts = array_values(array_filter($dts, fn($d) => !in_array($d, [DType::Int64, DType::UInt64, DType::UInt32], true)));
}
$ops = ['+', '-', '*', '/', '%', '**', '<<', '>>', '&', '|', '^'];
$eager = $tensorOp = $scalarOp = [];
foreach ($ops as $op) {
    $eager[$op] = eval("return fn(\$a, \$b) => \$a $op \$b;");
    $tensorOp[$op] = eval("return fn(\$a, \$b, \$out) => function () use (\$a, \$b, \$out) { for (\$i = 0; \$i < count(\$a); \$i++) \$out[\$i] = \$a[\$i] $op \$b[\$i]; };");
    $scalarOp[$op] = eval("return fn(\$a, \$b, \$out) => function () use (\$a, \$b, \$out) { for (\$i = 0; \$i < count(\$a); \$i++) \$out[\$i] = \$a[\$i] $op \$b; };");
}
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
function zeros(DType $d): Tensor { return Tensor::fromArray(array_fill(0, 6, $d === DType::Bool ? false : 0), $d); }
function bytes(Tensor $t): string { return implode(',', array_map(fn($x) => is_float($x) ? (is_nan($x) ? 'nan' : bin2hex(pack('e', $x))) : var_export($x, true), $t->toArray())); }
$checked = $mism = 0;
foreach ($dts as $da) foreach (array_merge($dts, ['int', 'float', 'bool']) as $db) {
    $a = rnd($da, 6);
    $b = is_string($db) ? match ($db) { 'int' => mt_rand(-3, 70), 'float' => 2.5, 'bool' => true } : rnd($db, 6);
    foreach ($ops as $op) {
        try { $r = $eager[$op]($a, $b); $e = $r->getDType()->name . ' ' . bytes($r); $out = zeros($r->getDType()); }
        catch (Throwable $ex) { $e = get_class($ex); $out = zeros(DType::Float64); }
        try { (new Compiled((is_string($db) ? $scalarOp : $tensorOp)[$op]($a, $b, $out)))(); $g = $out->getDType()->name . ' ' . bytes($out); }
        catch (Throwable $ex) { $g = get_class($ex); }
        $checked++;
        if ($e !== $g) { $mism++; if ($mism <= 15) echo "MISMATCH {$da->name} $op ", is_string($db) ? $db : $db->name, "\n  eager:    $e\n  compiled: $g\n"; }
    }
}
echo $checked > 1000 ? "more than 1000 cases" : "only $checked cases", ", mismatches $mism\n";
