<?php
/* Helper of numpy_fuzz.py: for every case builds the view, reads every element through chained
 * offsets, then applies the writes through chained offsets, and prints both as JSON. */
$cases = json_decode(file_get_contents($argv[1]), true);
$out = [];
$bits = fn(float $x) => is_nan($x) ? 'nan' : bin2hex(pack('E', $x));
foreach ($cases as $c) {
    $dt = constant("DType::{$c['dtype']}");
    $buf = ArrayBuffer::fromString(hex2bin($c['hex']));
    $v = $buf->view($dt, $c['shape'], $c['strides'], $c['offset']);
    $enc = fn($x) => is_array($x) ? [$bits($x[0]), $bits($x[1])] : (is_float($x) ? $bits($x) : $x);
    // leaf positions in C order
    $walk = function ($view, $prefix, $depth) use (&$walk, $c, &$cb) {
        $n = count($view);
        for ($i = 0; $i < $n; $i++) {
            if ($depth + 1 < count($c['shape'])) $walk($view[$i], [...$prefix, $i], $depth + 1);
            else $cb($view, $i, [...$prefix, $i]);
        }
    };
    $reads = [];
    // reads by chained offsets from the root, one full chain per element
    $idx = array_fill(0, count($c['shape']), 0);
    $total = array_product($c['shape']);
    for ($k = 0; $k < $total; $k++) {
        $x = $v;
        foreach ($idx as $i) $x = $x[$i];
        $reads[] = $enc($x);
        for ($d = count($idx) - 1; $d >= 0; $d--) { if (++$idx[$d] < $c['shape'][$d]) break; $idx[$d] = 0; }
    }
    $res = ['reads' => $reads];
    if (isset($c['writes'])) {
        $idx = array_fill(0, count($c['shape']), 0);
        foreach ($c['writes'] as $wv) {
            if (is_array($wv)) $wv = array_map(fn($s) => $s === 'nan' ? NAN : ($s === 'inf' ? INF : ($s === '-inf' ? -INF : (is_string($s) ? unpack('E', hex2bin($s))[1] : $s))), $wv);
            elseif (is_string($wv)) $wv = $wv === 'nan' ? NAN : ($wv === 'inf' ? INF : ($wv === '-inf' ? -INF : unpack('E', hex2bin($wv))[1]));
            $x = $v;
            for ($d = 0; $d < count($idx) - 1; $d++) $x = $x[$idx[$d]];
            $x[$idx[count($idx) - 1]] = $wv;
            for ($d = count($idx) - 1; $d >= 0; $d--) { if (++$idx[$d] < $c['shape'][$d]) break; $idx[$d] = 0; }
        }
        $res['after'] = bin2hex($buf->toString());
    }
    $out[] = $res;
}
echo json_encode($out);
