<?php
/*
 * Writes tests/promotion.tsv: the result dtype of every operator for every pair of operands, by the rules of
 * docs/proposal-arithmetic.md. A second implementation of those rules, independent of arith.c (which is tested against
 * this table), and the shared conformance table for other implementations (Skein).
 *
 * Columns: a, b, op, result. a and b are DType values ("int8", "bfloat16") or the PHP scalars "php-int", "php-float",
 * "php-bool";
 * op is the PHP operator; result is a DType value or "error". The unary operator ~ has b = "-".
 *
 * usage: php tools/promotion-table.php > tests/promotion.tsv
 */

$dtypes = ['bool', 'int2', 'int4', 'int8', 'int16', 'int32', 'int64', 'uint2', 'uint4', 'uint8', 'uint16', 'uint32',
    'uint64', 'float16', 'float32', 'float64', 'bfloat16', 'float8_e3m4', 'float8_e4m3', 'float8_e4m3b11fnuz',
    'float8_e4m3fn', 'float8_e4m3fnuz', 'float8_e5m2', 'float8_e5m2fnuz', 'float8_e8m0fnu', 'float6_e2m3fn',
    'float6_e3m2fn', 'float4_e2m1fn', 'complex32', 'complex64', 'complex128', 'bcomplex32'];
$scalars = ['php-int', 'php-float', 'php-bool'];
$binary = ['+', '-', '*', '/', '%', '**', '<<', '>>', '&', '|', '^'];

/** [kind, bits] of an arithmetic dtype; null for storage-only and complex dtypes */
function info(string $t): ?array
{
    if ($t === 'bool') return ['bool', 1];
    if (preg_match('/^(u?)int(8|16|32|64)$/', $t, $m)) return [$m[1] === 'u' ? 'uint' : 'int', (int)$m[2]];
    return match ($t) {
        'float16', 'bfloat16' => ['float', 16],
        'float32' => ['float', 32],
        'float64' => ['float', 64],
        default => null,
    };
}

/** the result of `$a $op $b`; $aScalar/$bScalar say whether the operand is a PHP scalar */
function promote(string $a, bool $aScalar, string $b, bool $bScalar, string $op): string
{
    $ka = $aScalar ? match ($a) { 'php-int' => 'int', 'php-float' => 'float', 'php-bool' => 'bool' } : (info($a)[0] ?? null);
    $kb = $bScalar ? match ($b) { 'php-int' => 'int', 'php-float' => 'float', 'php-bool' => 'bool' } : (info($b)[0] ?? null);
    if ($ka === null || $kb === null || ($aScalar && $bScalar)) return 'error';

    $bitwise = in_array($op, ['&', '|', '^'], true);
    $shift = in_array($op, ['<<', '>>'], true);

    if ($ka === 'bool' || $kb === 'bool') {
        if ($ka === 'bool' && $kb === 'bool' && $bitwise) return 'bool';
        return 'error';
    }
    $tensor = $aScalar ? $b : $a;      // the dtype a weak scalar takes
    if ($bitwise || $shift) {
        if ($ka === 'float' || $kb === 'float') return 'error';
        if ($aScalar || $bScalar) return $tensor;
        return promoteInts($a, $b);
    }
    if ($aScalar || $bScalar) {
        $scalarKind = $aScalar ? $ka : $kb;
        $tensorKind = $aScalar ? $kb : $ka;
        $r = $scalarKind === 'float' && $tensorKind !== 'float' ? 'float64' : $tensor;
    } elseif ($ka === 'float' && $kb === 'float') {
        $r = promoteFloats($a, $b);
    } elseif ($ka === 'float') {
        $r = $a;
    } elseif ($kb === 'float') {
        $r = $b;
    } else {
        $r = promoteInts($a, $b);
    }
    if ($r !== 'error' && $op === '/' && info($r)[0] !== 'float') {
        $r = 'float64';
    }
    return $r;
}

function promoteInts(string $a, string $b): string
{
    [$ka, $ba] = info($a);
    [$kb, $bb] = info($b);
    if ($ka === $kb) return ($ka === 'uint' ? 'uint' : 'int') . max($ba, $bb);
    $signedBits = $ka === 'int' ? $ba : $bb;
    $unsignedBits = $ka === 'int' ? $bb : $ba;
    if ($signedBits > $unsignedBits) return 'int' . $signedBits;
    // the smallest signed type that holds every value of both
    foreach ([16, 32, 64] as $bits) {
        if ($bits > $unsignedBits) return 'int' . $bits;
    }
    return 'error';     // UInt64 with a signed type: no such type
}

function promoteFloats(string $a, string $b): string
{
    if ($a === $b) return $a;
    foreach (['float64', 'float32'] as $wide) {
        if ($a === $wide || $b === $wide) return $wide;
    }
    return 'error';     // Float16 and BFloat16: no common type without loss
}

$operands = [];
foreach ($dtypes as $t) $operands[] = [$t, false];
foreach ($scalars as $s) $operands[] = [$s, true];

echo "a\tb\top\tresult\n";
foreach ($operands as [$a, $as]) {
    foreach ($operands as [$b, $bs]) {
        foreach ($binary as $op) {
            echo $a, "\t", $b, "\t", $op, "\t", promote($a, $as, $b, $bs, $op), "\n";
        }
    }
}
foreach ($operands as [$a, $as]) {
    $info = $as ? null : info($a);
    $r = $info !== null && in_array($info[0], ['bool', 'int', 'uint'], true) ? $a : 'error';
    echo $a, "\t-\t~\t", $r, "\n";
}
