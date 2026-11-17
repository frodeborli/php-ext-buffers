--TEST--
Buffers\Compiled: errors are raised where PHP raises them: the same values, or the same exception, as PHP running the code
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras'); ?>
--FILE--
<?php
// Random code with errors under branches and computed tensor indices, run over every element of two tensors of PHP's int:
// compiled, and by PHP itself (small ints: PHP's and Int64's arithmetic agree). The same values, or the same exception.
use Buffers\{Compiled, Tensor, DType};
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
$lutv = [5, -2, 0, 7, 1, 3, -1, 2, 9];
$dt = PHP_INT_SIZE == 4 ? DType::Int32 : DType::Int64;   // PHP's int
$lut = Tensor::fromArray($lutv, $dt);
$mism = 0; $errs = 0;
for ($t = 0; $t < $N; $t++) {
    $va = []; $vb = [];
    for ($i = 0; $i < 12; $i++) { $va[] = mt_rand(-4, 9); $vb[] = mt_rand(-4, 9); }
    $A = Tensor::fromArray($va, $dt)->reshape([3, 4]);
    $B = Tensor::fromArray($vb, $dt)->reshape([3, 4]);
    $out = Tensor::fromArray(array_fill(0, 12, 0), $dt)->reshape([3, 4]);
    $e = gen(mt_rand(1, 5));
    $k = eval("return function () use (\$A, \$B, \$lut, \$out) { for (\$y = 0; \$y < 3; \$y++) for (\$x = 0; \$x < 4; \$x++) { \$a = \$A[\$y][\$x]; \$b = \$B[\$y][\$x]; \$out[\$y][\$x] = $e; } };");
    try { (new Compiled($k))(); $got = json_encode($out->toArray()); } catch (Throwable $ex) { $got = get_class($ex); }
    $p = eval("return function (\$a, \$b, \$y, \$x, \$lut) { return $e; };");
    try {
        $rows = [];
        for ($y = 0; $y < 3; $y++) { $row = []; for ($x = 0; $x < 4; $x++) $row[] = $p($va[$y * 4 + $x], $vb[$y * 4 + $x], $y, $x, $lut); $rows[] = $row; }
        $want = json_encode($rows);
    } catch (Throwable $ex) { $want = get_class($ex); }
    if ($want !== $got) { $mism++; if ($mism <= 5) echo "MISMATCH $e\n  php:      $want\n  compiled: $got\n"; }
    if (!str_starts_with($want, '[')) $errs++;
}
echo "programs $N, of which raise an error $errs, mismatches $mism\n";
--EXPECT--
programs 200, of which raise an error 83, mismatches 0
