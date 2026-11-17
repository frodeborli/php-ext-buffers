<?php
// Random expression trees E of three tensors (PHP scalars in them follow Tensor arithmetic too): a compiled loop storing E($a[$i], $b[$i], $c[$i]) into a tensor of the
// eager result's dtype must equal E evaluated with the operators, bit for bit, or fail with the same exception class
use Buffers\{Compiled, Tensor, DType};
error_reporting(E_ALL & ~E_DEPRECATED);   // PHP warns about float % on the constant parts, in both evaluations
mt_srand(3);
$N = 150;
function bits(Tensor $t): string { return $t->getDType()->name . ' ' . implode(',', array_map(fn($x) => is_float($x) ? (is_nan($x) ? 'nan' : bin2hex(pack('e', $x))) : var_export($x, true), $t->toArray())); }
function gen(int $d, bool $int): string {
    if ($d <= 0 || mt_rand(0, 3) == 0) {
        $r = mt_rand(0, 5);
        return match ($r) { 0 => '$a', 1 => '$b', 2 => '$c', 3 => (string)mt_rand(-5, 9), 4 => $int ? '3' : '0.75', default => '$a' };
    }
    $ops = $int ? ['+', '-', '*', '&', '|', '^', '%', '<<', '>>'] : ['+', '-', '*', '/', '**', '%'];
    $op = $ops[mt_rand(0, count($ops) - 1)];
    if ($op === '<<' || $op === '>>') return '(' . gen($d - 1, $int) . " $op " . mt_rand(0, 5) . ')';
    $l = gen($d - 1, $int); $r = gen($d - 1, $int);
    // % of two PHP floats: PHP's % truncates them to ints, compiled code follows Tensor arithmetic (fmod)
    if ($op === '%' && !$int && !str_contains($l . $r, '$')) $l = '$a';
    return "($l $op $r)";
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
        $v = [];
        for ($i = 0; $i < 300; $i++) $v[] = $int ? mt_rand(-20, 100) & ($d->bits() < 16 ? 0x3f : 0x3fff) : mt_rand(-3000, 3000) / 64;
        return Tensor::fromArray($v, $d);
    };
    $a = $mk(); $b = $mk(); $c = $mk();
    $e = gen(mt_rand(1, 6), $int);
    if (!str_contains($e, '$')) continue;   // only constants: there is no tensor to compare
    try { $r = eval("return $e;"); $want = bits($r); $out = Tensor::fromArray(array_fill(0, 300, 0), $r->getDType()); }
    catch (Throwable $ex) { $want = get_class($ex); $out = Tensor::fromArray(array_fill(0, 300, 0.0), DType::Float64); }
    $body = str_replace(['$a', '$b', '$c'], ['$a[$i]', '$b[$i]', '$c[$i]'], $e);
    $k = eval("return function () use (\$a, \$b, \$c, \$out) { for (\$i = 0; \$i < 300; \$i++) \$out[\$i] = $body; };");
    try { (new Compiled($k))(); $got = bits($out); } catch (Throwable $ex) { $got = get_class($ex); }
    if ($want !== $got) { $mism++; if ($mism < 6) echo "MISMATCH $e\n  ", substr($want, 0, 200), "\n  ", substr($got, 0, 200), "\n"; }
}
echo "expressions $N, mismatches $mism\n";
