<?php
/*
 * Element-wise arithmetic: ns per element for tensor operators against the same work in a PHP loop over arrays.
 * usage: php -d extension=buffers.so [-d opcache.enable_cli=1 -d opcache.jit=tracing ...] benchmarks/arith.php [n]
 */
use Buffers\{DType, Tensor};

$n = (int)($argv[1] ?? 1000000);
$cpu = trim((string)@shell_exec("sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -1")) ?: php_uname('m');
$jit = function_exists('opcache_get_status') && (opcache_get_status(false)['jit']['on'] ?? false) ? 'JIT' : 'no JIT';
printf("CPU: %s; PHP %s %s; n = %d\n", $cpu, PHP_VERSION, $jit, $n);

function best(callable $f, int $reps = 7): float {
    $best = INF;
    for ($r = 0; $r < $reps; $r++) {
        $t = hrtime(true);
        $f();
        $best = min($best, hrtime(true) - $t);
    }
    return $best;
}

$xs = []; $ys = []; $is = []; $js = [];
mt_srand(1);
for ($k = 0; $k < $n; $k++) { $xs[] = mt_rand() / 1e6; $ys[] = mt_rand() / 1e6 + 1; $is[] = mt_rand(-1000, 1000); $js[] = mt_rand(1, 1000); }

$cases = [];
foreach ([DType::Float64, DType::Float32, DType::Float16, DType::BFloat16] as $dt) {
    $a = Tensor::fromArray($xs, $dt); $b = Tensor::fromArray($ys, $dt);
    $cases["{$dt->name} a + b"] = fn() => $a + $b;
    $cases["{$dt->name} a * 2.5"] = fn() => $a * 2.5;
    $cases["{$dt->name} a / b"] = fn() => $a / $b;
}
foreach ([DType::Int32, DType::Int64, DType::UInt8] as $dt) {
    $src = $dt === DType::UInt8 ? array_map(fn($v) => $v & 255, $is) : $is;
    $a = Tensor::fromArray($src, $dt); $b = Tensor::fromArray($dt === DType::UInt8 ? array_map(fn($v) => $v & 255, $js) : $js, $dt);
    $cases["{$dt->name} a + b"] = fn() => $a + $b;
    $cases["{$dt->name} a * 3"] = fn() => $a * 3;
}
$f32 = Tensor::fromArray($xs, DType::Float32);
$cases["Float32 a += 1.5 (in place)"] = function () use (&$f32) { $f32 += 1.5; };
$m = Tensor::fromArray(array_slice($xs, 0, intdiv($n, 1000) * 1000), DType::Float32);
$m2 = $m->buffer->view(DType::Float32, [intdiv($n, 1000), 1000]);
$row = Tensor::fromArray(array_slice($ys, 0, 1000), DType::Float32);
$cases["Float32 [n/1000, 1000] + [1000] (broadcast)"] = fn() => $m2 + $row;

$php = [
    'PHP loop: floats a + b' => function () use ($xs, $ys, $n) { $r = []; for ($k = 0; $k < $n; $k++) $r[] = $xs[$k] + $ys[$k]; return $r; },
    'PHP loop: floats a * 2.5' => function () use ($xs, $n) { $r = []; for ($k = 0; $k < $n; $k++) $r[] = $xs[$k] * 2.5; return $r; },
    'PHP loop: ints a + b' => function () use ($is, $js, $n) { $r = []; for ($k = 0; $k < $n; $k++) $r[] = $is[$k] + $js[$k]; return $r; },
];
foreach ($cases + $php as $name => $f) {
    printf("%-44s %8.3f ns/element\n", $name, best($f) / $n);
}
