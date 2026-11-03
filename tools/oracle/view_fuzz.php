<?php
/* Helper of view_fuzz.py: view() with hostile arguments. Prints, per case, "ok <last corner element>" or the exception class. */
$cases = json_decode(file_get_contents($argv[1]), true, 512, JSON_BIGINT_AS_STRING);
$buf = [];
$out = [];
foreach ($cases as $c) {
    $dt = constant("DType::{$c['dtype']}");
    $b = $buf[$c['len']] ??= ArrayBuffer::fromString(implode('', $c['len'] ? array_map(fn($i) => chr($i % 251), range(0, $c['len'] - 1)) : []));
    $num = fn($x) => is_string($x) ? (int)$x : $x;   // JSON_BIGINT_AS_STRING only for values beyond int64: keep those as floats
    $conv = fn($a) => is_array($a) ? array_map(fn($x) => is_string($x) ? (float)$x : $x, $a) : (is_string($a) ? (float)$a : $a);
    try {
        $v = $b->view($dt, $conv($c['shape']), $conv($c['strides']), $conv($c['offset']));
        $x = $v;
        $shape = $v->shape();
        $res = 'ok';
        if (!in_array(0, $shape, true)) {
            foreach ($shape as $d) $x = $x[$d - 1];
            $res .= ' ' . $x . ' ' . json_encode($v->strides()) . ' ' . $v->byteOffset();
        } else {
            $res .= ' empty ' . json_encode($v->strides()) . ' ' . $v->byteOffset();
        }
        $out[] = $res;
    } catch (Throwable $e) {
        $out[] = get_class($e);
    }
}
echo json_encode($out);
