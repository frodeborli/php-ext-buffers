--TEST--
Tensor::each(): an element raises what calling the closure in PHP raises for it (random closures with errors under branches)
--SKIPIF--
<?php if (!extension_loaded('buffers') || !method_exists(Buffers\Tensor::class, 'each')) die('skip built without the extras');
if (PHP_INT_SIZE < 8) die('skip 64-bit PHP: Int64 inputs'); ?>
--FILE--
<?php
// Closures with element errors under branches, gathers with computed indices: each() against calling the closure in
// PHP for every element in row-major order (small ints: PHP's and Int64's arithmetic agree). Same values, or the same
// exception class.
use Buffers\{Tensor, DType};
error_reporting(E_ALL & ~E_DEPRECATED);
mt_srand(5);
$N = 200;
function gen(int $d): string {
    if ($d <= 0 || mt_rand(0, 3) == 0) {
        return ['$a', '$b', '$y', '$x', (string)mt_rand(-3, 6), '$lut[$a & 7]'][mt_rand(0, 5)];
    }
    switch (mt_rand(0, 9)) {
        case 0: return '(' . gen($d - 1) . ' % ' . gen($d - 1) . ')';
        case 1: return 'intdiv(' . gen($d - 1) . ', ' . gen($d - 1) . ')';
        case 2: return '(' . gen($d - 1) . ' << (' . gen($d - 1) . ' & 7))';
        case 3: return '(' . gen($d - 1) . ' > ' . gen($d - 1) . ' ? ' . gen($d - 1) . ' : ' . gen($d - 1) . ')';
        case 4: return '$lut[' . gen($d - 1) . ']';
        case 5: return '(' . gen($d - 1) . ' != 0 && ' . gen($d - 1) . ' > 1 ? 1 : 0)';
        case 6: return 'max(' . gen($d - 1) . ', ' . gen($d - 1) . ')';
        default: return '(' . gen($d - 1) . ' ' . ['+', '-', '*'][mt_rand(0, 2)] . ' ' . gen($d - 1) . ')';
    }
}
$lut = Tensor::fromArray([5, -2, 0, 7, 1, 3, -1, 2, 9], DType::Int64);
$mism = 0; $errs = 0;
for ($t = 0; $t < $N; $t++) {
    $va = []; $vb = [];
    for ($i = 0; $i < 12; $i++) { $va[] = mt_rand(-4, 9); $vb[] = mt_rand(-4, 9); }
    $a = Tensor::fromArray($va, DType::Int64)->reshape([3, 4]);
    $b = Tensor::fromArray($vb, DType::Int64)->reshape([3, 4]);
    $e = gen(mt_rand(1, 5));
    $f = eval("return fn(\$a, \$b, \$y, \$x) => $e;");
    try { $r = $a->each($f, $b); $got = json_encode(array_map(fn($row) => array_map(fn($v) => is_float($v) ? round($v, 9) : $v, $row), $r->toArray())); }
    catch (Throwable $ex) { $got = get_class($ex); }
    try {
        $rows = [];
        for ($y = 0; $y < 3; $y++) { $row = []; for ($x = 0; $x < 4; $x++) { $v = $f($va[$y * 4 + $x], $vb[$y * 4 + $x], $y, $x); $row[] = is_float($v) ? round($v, 9) : (is_bool($v) ? $v : $v); } $rows[] = $row; }
        $want = json_encode($rows);
    } catch (Throwable $ex) { $want = get_class($ex); }
    if ($want !== $got && !(str_contains($want, '.') || str_contains($got, '.'))) {
        // `/` is not generated; a mismatch other than int/float formatting is a real one
        $mism++;
        if ($mism <= 5) echo "MISMATCH $e\n  php:  $want\n  each: $got\n";
    }
    if (!str_starts_with($want, '[')) $errs++;
}
echo "closures $N, of which raise an error $errs, mismatches $mism\n";
--EXPECT--
closures 200, of which raise an error 83, mismatches 0
