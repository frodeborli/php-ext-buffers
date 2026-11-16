--TEST--
Arithmetic: results are exact, bit for bit, against an independent oracle (PHP double and integer arithmetic)
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_INT_SIZE < 8) die('skip 64-bit only'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};

/*
 * Floats: for + - * / the exact result rounded to double and then once to Float32/Float16/BFloat16 is the exact result
 * rounded once (double has more than twice their precision plus two bits), and fmod is exact; so PHP's double
 * arithmetic followed by one rounding to the format is an oracle. The rounding to the format is done by element writes
 * (the conversion tested against ml_dtypes). ** is checked against its bound (4 ULP Float32, 2 ULP Float64).
 * Integers: PHP int arithmetic with explicit two's-complement wrapping.
 */
mt_srand(20261116);
$n = 4000;

function f32bits(float $x): int { return unpack('V', pack('g', $x))[1]; }
function special(): array { return [0.0, -0.0, 1.0, -1.0, INF, -INF, NAN, 1e-45, -1e-45, 1e-40, 3.4e38, -3.4e38, 65504.0, 6e-8, 0.5, 2.0]; }

/** n random values that are exactly representable in $dt (written through a tensor and read back) */
function values(DType $dt, int $n, bool $nonzero = false): array {
    $t = (new Buffer($n * 8))->view($dt, $n);
    $sp = special();
    for ($i = 0; $i < $n; $i++) {
        $r = mt_rand(0, 9);
        if ($r === 0) $v = $sp[mt_rand(0, count($sp) - 1)];
        elseif ($r < 5) $v = (mt_rand() / mt_getrandmax() - 0.5) * 2 ** mt_rand(-30, 30);
        else $v = (mt_rand(-1000, 1000)) / 8;
        if ($nonzero && $v == 0) $v = 1.0;
        $t[$i] = $v;
    }
    return [$t, iterator_to_array($t)];
}

/** the bits of $x rounded to $dt, through an element write */
function bits(DType $dt, float $x): string {
    static $cell = [];
    $cell[$dt->value] ??= (new Buffer(8))->view($dt, 1);
    $cell[$dt->value][0] = $x;
    return $cell[$dt->value]->buffer->toString();
}

function same(DType $dt, float $got, float $want): bool {
    if (is_nan($want)) return is_nan($got);
    return bits($dt, $got) === bits($dt, $want);
}

$ops = ['+' => fn($a, $b) => $a + $b, '-' => fn($a, $b) => $a - $b, '*' => fn($a, $b) => $a * $b,
    '/' => fn($a, $b) => fdiv($a, $b), '%' => fn($a, $b) => fmod($a, $b)];
foreach ([DType::Float32, DType::Float16, DType::BFloat16, DType::Float64] as $dt) {
    [$ta, $a] = values($dt, $n);
    [$tb, $b] = values($dt, $n);
    foreach ($ops as $sym => $f) {
        $r = match ($sym) { '+' => $ta + $tb, '-' => $ta - $tb, '*' => $ta * $tb, '/' => $ta / $tb, '%' => $ta % $tb };
        $bad = count($r) === $n ? 0 : 1000000;
        foreach ($r as $i => $got) {
            $want = $f($a[$i], $b[$i]);
            if (!same($dt, $got, $want) && $bad++ < 3) printf("%s %s: %.17g %s %.17g = %.17g, want %.17g\n", $dt->name, $sym, $a[$i], $sym, $b[$i], $got, $want);
        }
        echo "{$dt->name} $sym: ", $r->dtype->name, $bad ? " $bad wrong" : " exact", "\n";
    }
}

echo "mixed operands are rounded to the result dtype first, once\n";
[$th, $h] = values(DType::Float16, $n);
$ints = Tensor::fromArray(array_map(fn() => mt_rand(-100000, 100000), range(1, $n)), DType::Int32);
$r = $th + $ints;
$bad = 0;
function bits16(float $x): float { $t = (new Buffer(2))->view(DType::Float16, 1); $t[0] = $x; return $t[0]; }
foreach ($r as $i => $got) {
    $want = $h[$i] + bits16((float)$ints[$i]);   // Int32 rounded to Float16 first, then one Float16 addition
    if (!same(DType::Float16, $got, $want) && $bad++ < 3) printf("Float16 + Int32: %.17g + %d = %.17g, want %.17g\n", $h[$i], $ints[$i], $got, $want);
}
echo "Float16 + Int32: ", $r->dtype->name, $bad ? " $bad wrong" : " exact", "\n";
$r = $th * 0.1;
$bad = 0;
foreach ($r as $i => $got) {
    if (!same(DType::Float16, $got, $h[$i] * bits16(0.1)) && $bad++ < 3) printf("Float16 * 0.1: %.17g\n", $got);
}
echo "Float16 * 0.1 (weak scalar, rounded to Float16): ", $r->dtype->name, $bad ? " $bad wrong" : " exact", "\n";
$big = Tensor::fromArray([(1 << 62) + (1 << 54) + 1, -((1 << 62) + (1 << 54) + 1), (1 << 53) + 1, 16777217], DType::Int64);
$r = $big * Tensor::fromArray([1.0], DType::BFloat16);
echo "Int64 to BFloat16, rounded once: ", json_encode(iterator_to_array($r)), "\n";

echo "** within its bound\n";
foreach ([[DType::Float32, 4], [DType::Float64, 2]] as [$dt, $ulps]) {
    $ta = Tensor::fromArray(array_map(fn() => mt_rand(1, 100000) / 1000, range(1, $n)), $dt);
    $tb = Tensor::fromArray(array_map(fn() => mt_rand(-4000, 4000) / 1000, range(1, $n)), $dt);
    $r = $ta ** $tb;
    $worst = 0;
    foreach ($r as $i => $got) {
        $want = $ta[$i] ** $tb[$i];
        $ulp = $dt === DType::Float32 ? abs(f32bits($got) - f32bits($want)) : abs(unpack('P', pack('e', $got))[1] - unpack('P', pack('e', $want))[1]);
        $worst = max($worst, $ulp);
    }
    echo "{$dt->name} **: worst ", $worst <= $ulps ? "within $ulps ULP" : "$worst ULP", "\n";
}

echo "integers wrap\n";
function wrap(int $v, int $bits, bool $signed): int {
    $m = $bits === 64 ? $v : $v & ((1 << $bits) - 1);
    if ($signed && $bits < 64 && $m >= 1 << ($bits - 1)) $m -= 1 << $bits;
    return $m;
}
function mulwrap(int $a, int $b, int $bits, bool $signed): int {
    // (a * b) mod 2^bits without PHP's overflow to float: split b into 16-bit halves
    $lo = $a * ($b & 0xFFFF);
    $hi = ($a * ($b >> 16)) & 0xFFFFFFFF;
    return wrap($lo + ($hi << 16), $bits, $signed);
}
$intOps = [
    '+' => fn($a, $b, $bits, $s) => wrap($a + $b, $bits, $s),
    '-' => fn($a, $b, $bits, $s) => wrap($a - $b, $bits, $s),
    '*' => fn($a, $b, $bits, $s) => mulwrap($a, $b, $bits, $s),
    '%' => fn($a, $b, $bits, $s) => $b === 0 ? null : ($b === -1 ? 0 : $a % $b),
    '&' => fn($a, $b, $bits, $s) => wrap($a & $b, $bits, $s),
    '|' => fn($a, $b, $bits, $s) => wrap($a | $b, $bits, $s),
    '^' => fn($a, $b, $bits, $s) => wrap($a ^ $b, $bits, $s),
    '<<' => fn($a, $b, $bits, $s) => wrap($b >= $bits ? 0 : $a << $b, $bits, $s),
    '>>' => fn($a, $b, $bits, $s) => $s ? $a >> min($b, 63) : ($b >= 64 ? 0 : $a >> $b),
];
foreach ([[DType::Int8, 8, true], [DType::Int16, 16, true], [DType::Int32, 32, true], [DType::UInt8, 8, false], [DType::UInt16, 16, false], [DType::UInt32, 32, false]] as [$dt, $bits, $s]) {
    $lo = $s ? -(1 << ($bits - 1)) : 0;
    $hi = $s ? (1 << ($bits - 1)) - 1 : (1 << $bits) - 1;
    $a = array_map(fn() => mt_rand($lo, $hi), range(1, $n));
    $b = array_map(fn() => mt_rand($lo, $hi) ?: 1, range(1, $n));
    $c = array_map(fn() => mt_rand(0, $bits + 2), range(1, $n));
    $ta = Tensor::fromArray($a, $dt); $tb = Tensor::fromArray($b, $dt); $tc = Tensor::fromArray($c, $dt);
    $line = [];
    foreach ($intOps as $sym => $f) {
        $rhs = in_array($sym, ['<<', '>>'], true) ? $c : $b;
        $r = match ($sym) { '+' => $ta + $tb, '-' => $ta - $tb, '*' => $ta * $tb, '%' => $ta % $tb, '&' => $ta & $tb,
            '|' => $ta | $tb, '^' => $ta ^ $tb, '<<' => $ta << $tc, '>>' => $ta >> $tc };
        $bad = 0;
        foreach ($r as $i => $got) {
            $want = $f($a[$i], $rhs[$i], $bits, $s);
            if ($got !== $want && $bad++ < 2) printf("%s %d %s %d = %d, want %d\n", $dt->name, $a[$i], $sym, $rhs[$i], $got, $want);
        }
        $line[] = $sym . ($bad ? "($bad wrong)" : '');
    }
    $p = Tensor::fromArray(array_map(fn() => mt_rand($lo, $hi), range(1, 200)), $dt) ** Tensor::fromArray(array_map(fn() => mt_rand(0, 9), range(1, 200)), $dt);
    echo "{$dt->name}: ", implode(' ', $line), " ** ", $p->dtype->name, "\n";
}
echo "end\n";
?>
--EXPECT--
Float32 +: Float32 exact
Float32 -: Float32 exact
Float32 *: Float32 exact
Float32 /: Float32 exact
Float32 %: Float32 exact
Float16 +: Float16 exact
Float16 -: Float16 exact
Float16 *: Float16 exact
Float16 /: Float16 exact
Float16 %: Float16 exact
BFloat16 +: BFloat16 exact
BFloat16 -: BFloat16 exact
BFloat16 *: BFloat16 exact
BFloat16 /: BFloat16 exact
BFloat16 %: BFloat16 exact
Float64 +: Float64 exact
Float64 -: Float64 exact
Float64 *: Float64 exact
Float64 /: Float64 exact
Float64 %: Float64 exact
mixed operands are rounded to the result dtype first, once
Float16 + Int32: Float16 exact
Float16 * 0.1 (weak scalar, rounded to Float16): Float16 exact
Int64 to BFloat16, rounded once: [4.647714815446352e+18,-4.647714815446352e+18,9007199254740992,16777216]
** within its bound
Float32 **: worst within 4 ULP
Float64 **: worst within 2 ULP
integers wrap
Int8: + - * % & | ^ << >> ** Int8
Int16: + - * % & | ^ << >> ** Int16
Int32: + - * % & | ^ << >> ** Int32
UInt8: + - * % & | ^ << >> ** UInt8
UInt16: + - * % & | ^ << >> ** UInt16
UInt32: + - * % & | ^ << >> ** UInt32
end
