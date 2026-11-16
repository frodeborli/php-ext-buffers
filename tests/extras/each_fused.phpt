--TEST--
Tensor::each(): random fused expressions equal the same expression evaluated with the operators
--SKIPIF--
<?php if (!extension_loaded('buffers') || !method_exists(Buffers\Tensor::class, 'each')) die('skip built without the extras'); ?>
--FILE--
<?php
// Random expression trees: $a->each(fn($a, $b, $c) => E, $b, $c) must equal E evaluated with the operators, broadcast
// to the shape each() iterates over (all three inputs), bit for bit; the fused evaluation reuses chunk registers
use Buffers\Tensor; use Buffers\DType;
error_reporting(E_ALL & ~E_DEPRECATED);   // PHP warns about float % on the constant parts, in both evaluations
mt_srand(3);
$N = 150;
function flatten($a) { if (!is_array($a)) return [$a]; $r = []; foreach ($a as $x) foreach (flatten($x) as $y) $r[] = $y; return $r; }
function outcome(callable $f): string {
    try { $t = $f(); return $t->getDType()->name . ' ' . json_encode($t->getShape()->toArray()) . ' ' . implode(',', array_map(fn($x) => is_float($x) ? (is_nan($x) ? 'nan' : bin2hex(pack('e', $x))) : var_export($x, true), flatten($t->toArray()))); }
    catch (Throwable $e) { return get_class($e); }
}
function gen(int $d, bool $int): string {
    if ($d <= 0 || mt_rand(0, 3) == 0) {
        $r = mt_rand(0, 5);
        return match ($r) { 0 => '$a', 1 => '$b', 2 => '$c', 3 => (string)mt_rand(-5, 9), 4 => $int ? '3' : '0.75', default => '$a' };
    }
    $ops = $int ? ['+', '-', '*', '&', '|', '^', '%', '<<', '>>'] : ['+', '-', '*', '/', '**', '%'];
    $op = $ops[mt_rand(0, count($ops) - 1)];
    if ($op === '<<' || $op === '>>') return '(' . gen($d - 1, $int) . " $op " . mt_rand(0, 5) . ')';
    return '(' . gen($d - 1, $int) . " $op " . gen($d - 1, $int) . ')';
}
$ftypes = [DType::Float32, DType::Float64, DType::Float16, DType::BFloat16];
$itypes = PHP_INT_SIZE == 4   // 32-bit PHP has no Int64 or UInt32 tensors
    ? [DType::Int8, DType::Int16, DType::Int32, DType::UInt8, DType::UInt16]
    : [DType::Int8, DType::Int16, DType::Int32, DType::Int64, DType::UInt8, DType::UInt16, DType::UInt32];
$mism = 0;
for ($t = 0; $t < $N; $t++) {
    $int = mt_rand(0, 1) == 1;
    $types = $int ? $itypes : $ftypes;
    $mk = function () use ($int, $types) {
        $d = $types[mt_rand(0, count($types) - 1)];
        $shape = [[1100], [3, 1100], [1, 1100], [3, 1]][mt_rand(0, 3)];
        $n = array_product($shape);
        $v = [];
        for ($i = 0; $i < $n; $i++) $v[] = $int ? mt_rand(-20, 100) & ($d->bits() < 16 ? 0x3f : 0x3fff) : mt_rand(-3000, 3000) / 64;
        return Tensor::fromArray($v, $d)->reshape($shape);
    };
    $a = $mk(); $b = $mk(); $c = $mk();
    $e = gen(mt_rand(1, 6), $int);
    $f = eval("return fn(\$a, \$b, \$c) => $e;");
    if (!str_contains($e, '$')) continue;   // only constants: PHP computes it, there is no tensor to compare
    $shape = [];
    foreach ([$a, $b, $c] as $t) { $sh = $t->getShape()->toArray(); $sh = array_pad($sh, -2, 1); foreach ($sh as $i => $d) $shape[$i] = max($shape[$i] ?? 1, $d); }
    $eager = outcome(function () use ($a, $b, $c, $e, $shape) { $r = eval("return $e;"); return $r->broadcastTo($shape); });
    $each = outcome(fn() => $a->each($f, $b, $c));
    if (str_starts_with($eager, 'ValueError') || str_starts_with($eager, 'TypeError')) { if ($eager !== $each) { $mism++; if ($mism < 6) echo "MISMATCH(err) $e\n  $eager\n  ", substr($each, 0, 200), "\n"; } continue; }
    if ($eager !== $each) { $mism++; if ($mism < 6) echo "MISMATCH $e [{$a->getDType()->name} {$b->getDType()->name} {$c->getDType()->name}]\n  ", substr($eager, 0, 200), "\n  ", substr($each, 0, 200), "\n"; }
}
echo "expressions $N, mismatches $mism\n";
--EXPECT--
expressions 150, mismatches 0
