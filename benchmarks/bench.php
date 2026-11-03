<?php
/* usage: php [-d extension=buffers.so ...] benchmarks/bench.php [section]    sections: access, dtype, props, memory
 * Prints one TSV line per measurement: name <TAB> ns per element access. run.sh runs it with and without the JIT. */
use Buffers\{DType, Buffer};

$section = $argv[1] ?? 'all';
$w = 640; $h = 480; $px = $w * $h; $reps = 12;

$jit = false;
if (function_exists('opcache_get_status') && ($st = opcache_get_status(false)) && !empty($st['jit']['on'])) {
    $jit = true;
    $free0 = $st['jit']['buffer_free'];
}
fprintf(STDERR, "mode: %s\n", $jit ? 'JIT (tracing)' : 'interpreter');

require __DIR__ . '/loops.php';

function measure_inner(string $name, callable $run, int $accesses, int $reps): void {   // $run returns the ns it spent in its loops
    $run(); $run();
    $best = INF;
    for ($r = 0; $r < $reps; $r++) $best = min($best, $run());
    printf("%s\t%.2f\n", $name, $best / $accesses);
}

function measure(string $name, callable $run, int $accesses, int $reps): void {
    $run();                                  // warm up: lets the JIT compile before the timed runs
    $run();
    $best = INF;
    for ($r = 0; $r < $reps; $r++) {
        $t = hrtime(true); $run(); $best = min($best, hrtime(true) - $t);
    }
    printf("%s\t%.2f\n", $name, $best / $accesses);
}

if ($section === 'all' || $section === 'access') {
    $buf = new Buffer($px * 4);
    $flat = $buf->view(DType::UInt8);                       // 1-D idiom
    $v3 = $buf->view(DType::UInt8, [$h, $w, 4]);
    $v2 = $buf->view(DType::UInt8, [$h, $w * 4]);
    for ($i = 0; $i < $px * 4; $i++) $flat[$i] = $i & 255;
    $arr1 = array_fill(0, $px * 4, 7);
    $arrN = array_fill(0, $h, array_fill(0, $w, [1, 2, 3, 7]));
    $spl = SplFixedArray::fromArray($arr1);

    $m = function (string $name, callable $f) use ($px, $reps) { measure($name, $f, $px, $reps); };
    $m('read  view 1-D idiom',            fn() => read_view_1d($flat, $w, $h));
    $m('read  view 1-D, row hoisted',     fn() => read_view_1d_hoisted($flat, $w, $h));
    $m('read  view [y][x][c]',            fn() => read_view_3d($v3, $w, $h));
    $m('read  view [y][x][c], row var',   fn() => read_view_3d_rowhoisted($v3, $w, $h));
    $m('read  view 2-D [y][x*4+c]',       fn() => read_view_2d($v2, $w, $h));
    $m('read  array flat',                fn() => read_array_1d($arr1, $w, $h));
    $m('read  array flat, row hoisted',   fn() => read_array_1d_hoisted($arr1, $w, $h));
    $m('read  array nested [y][x][c]',    fn() => read_array_nested($arrN, $w, $h));
    $m('read  SplFixedArray flat',        fn() => read_spl_1d($spl, $w, $h));
    $m('write view 1-D idiom',            fn() => write_view_1d($flat, $w, $h));
    $m('write view 1-D, row hoisted',     fn() => write_view_1d_hoisted($flat, $w, $h));
    $m('write view [y][x][c]',            fn() => write_view_3d($v3, $w, $h));
    $m('write view [y][x][c], row var',   fn() => write_view_3d_rowhoisted($v3, $w, $h));
    $m('write view 2-D [y][x*4+c]',       fn() => write_view_2d($v2, $w, $h));
    $mi = function (string $name, callable $f) use ($px, $reps) { measure_inner($name, $f, $px * 4, 5); };
    $mi('write array flat',               fn() => write_array_1d($w, $h, 4));
    $mi('write array flat, row hoisted',  fn() => write_array_1d_hoisted($w, $h, 4));
    $mi('write array nested [y][x][c]',   fn() => write_array_nested($w, $h, 4));
    $m('write SplFixedArray flat',        fn() => write_spl_1d($spl, $w, $h));
    $m('+=    view 1-D idiom',            fn() => add_view_1d($flat, $w, $h));
    $m('+=    view [y][x][c]',            fn() => add_view_3d($v3, $w, $h));
    $mi('+=    array flat',               fn() => add_array_1d($w, $h, 4));
    $mi('+=    array nested [y][x][c]',   fn() => add_array_nested($w, $h, 4));
}

if ($section === 'all' || $section === 'dtype') {
    $n = 1 << 20;
    foreach (DType::cases() as $c) {
        $buf = new Buffer($n * 16);            // fresh memory per dtype: stale bytes of another dtype could be float denormals
        $f = $buf->view($c, $n);
        $val = match (true) { $c === DType::Bool => true, $c->isComplex() => [1.5, -2.5], $c->isFloatingPoint() => 1.5, default => 3 };
        measure("dtype write {$c->name}", fn() => dtype_write($f, $n, $val), $n, 5);
        measure("dtype read  {$c->name}", fn() => dtype_read($f, $n), $n, 5);   // reads what the write left
    }
}

if ($section === 'all' || $section === 'props') {
    // properties are computed on read (internal get hooks); creating a sub-view must stay as cheap as it was
    $pv = (new Buffer(1024))->view(DType::UInt8, [16, 64]);
    $n = 1_000_000;
    measure('prop read ndim',    function () use ($pv, $n) { $s = 0; for ($i = 0; $i < $n; $i++) $s += $pv->ndim; return $s; }, $n, 5);
    measure('prop read offset',  function () use ($pv, $n) { $s = 0; for ($i = 0; $i < $n; $i++) $s += $pv->offset; return $s; }, $n, 5);
    measure('prop read dtype',   function () use ($pv, $n) { for ($i = 0; $i < $n; $i++) $x = $pv->dtype; return $x; }, $n, 5);
    measure('prop read shape',   function () use ($pv, $n) { for ($i = 0; $i < $n; $i++) $x = $pv->shape; return $x; }, $n, 5);
    measure('prop read strides', function () use ($pv, $n) { for ($i = 0; $i < $n; $i++) $x = $pv->strides; return $x; }, $n, 5);
    measure('sub-view $v[$i & 15]', function () use ($pv, $n) { for ($i = 0; $i < $n; $i++) $x = $pv[$i & 15]; return $x; }, $n, 5);
}

if ($section === 'all' || $section === 'memory') {
    $n = 1_000_000;
    $report = function (string $what, callable $build) use ($n) {
        gc_collect_cycles(); $before = memory_get_usage();
        $keep = $build();
        printf("memory\t%s\t%.2f\n", $what, (memory_get_usage() - $before) / $n);
        return $keep;
    };
    $a = $report('Buffer + UInt8 view', fn() => (new Buffer($n))->view(DType::UInt8));
    $b = $report('Buffer + Float32 view', fn() => (new Buffer($n * 4))->view(DType::Float32));
    $c = $report('Buffer + Float64 view', fn() => (new Buffer($n * 8))->view(DType::Float64));
    $d = $report('PHP array of ints (packed)', fn() => array_fill(0, $n, 7));
    $e = $report('PHP array of floats (packed)', fn() => array_fill(0, $n, 0.5));
    $f = $report('SplFixedArray', fn() => new SplFixedArray($n));
    $g = $report('PHP string (1 byte/element)', fn() => str_repeat("\0", $n));
}

if ($jit && $section !== "memory") {
    $st = opcache_get_status(false);
    fprintf(STDERR, "JIT buffer: %d of %d bytes used by this run%s\n", $free0 - $st['jit']['buffer_free'], $st['jit']['buffer_size'],
        $free0 - $st['jit']['buffer_free'] > 0 ? ' - verified: code was compiled' : ' - NOT VERIFIED, NOTHING WAS COMPILED');
    exit($free0 - $st['jit']['buffer_free'] > 0 ? 0 : 3);
}
