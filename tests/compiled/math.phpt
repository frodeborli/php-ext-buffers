--TEST--
Buffers\Compiled: math functions give PHP's own results (NaN payloads aside); Float32 arguments give Float32
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras'); ?>
--FILE--
<?php
use Buffers\{Compiled, Tensor, DType};
$fns = ['sin','cos','tan','asin','acos','atan','sinh','cosh','tanh','asinh','acosh','atanh','exp','expm1','log','log10','log1p','deg2rad','rad2deg'];
$xs = [0.0, 0.5, -0.75, 1.0, 2.5, 100.0, 1e-300, -0.0, INF, NAN, 3];
$bits = fn($v) => is_float($v) ? (is_nan($v) ? 'nan' : bin2hex(pack('e', $v))) : var_export($v, true);
$mism = 0; $n = 0;
foreach ($fns as $f) {
    $k = new Compiled(eval("return fn(\$x) => $f(\$x);"));
    foreach ($xs as $x) { $n++; $a = $bits($f($x)); $b = $bits($k($x)); if ($a !== $b) { $mism++; echo "$f($x): php $a compiled $b\n"; } }
}
$two = ['atan2', 'hypot', 'fdiv', 'log', 'pow'];
foreach ($two as $f) {
    $k = new Compiled(eval("return fn(\$x, \$y) => $f(\$x, \$y);"));
    foreach ([[1.5, 2.0], [-1.0, 0.5], [8.0, 2.0], [100.0, 10.0], [5.0, 1.0], [2.0, 3.0], [0.0, 0.0], [7.5, 2.5]] as [$x, $y]) {
        $n++;
        try { $a = $bits($f($x, $y)); } catch (Throwable $e) { $a = get_class($e); }
        try { $b = $bits($k($x, $y)); } catch (Throwable $e) { $b = get_class($e); }
        if ($a !== $b) { $mism++; echo "$f($x, $y): php $a compiled $b\n"; }
    }
}
echo "pi: ", $bits(pi()) === $bits((new Compiled(fn() => pi()))()) ? "same" : "differs", "\n";
try { (new Compiled(fn($x) => log($x, -1.0)))(2.0); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
$t = Tensor::fromArray([0.5, 1.5], DType::Float32);
echo "Float32: ", json_encode((new Compiled(fn() => [sin($t[0]), atan2($t[0], $t[1])]))()), "\n";   // PHP's doubles, rounded once to Float32
echo "$n cases, mismatches $mism\n";
--EXPECT--
pi: same
ValueError: log(): Argument #2 ($base) must be greater than 0
Float32: [0.4794255495071411,0.32175055146217346]
249 cases, mismatches 0
