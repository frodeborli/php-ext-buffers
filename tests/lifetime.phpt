--TEST--
Lifetime: views keep their buffer alive, sub-view pooling, cycles and shutdown
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
function make() { $b = new ArrayBuffer(16); $v = $b->view(DType::UInt8, [4, 4]); $v[3][3] = 9; return $v; }
$v = make();
var_dump($v[3][3], $v->buffer()->byteLength());
$row = $v[3];
unset($v);
gc_collect_cycles();
var_dump($row[3], $row->buffer()->toString()[15] === "\x09");
// views of the same buffer share the buffer object
$b = new ArrayBuffer(8);
$x = $b->view(DType::UInt8); $y = $b->view(DType::Int16, 4);
var_dump($x->buffer() === $b, $y->buffer() === $b);
$y[0] = 0x0201; var_dump($x[0], $x[1]);
// pooled sub-views: heavy churn stays flat in memory and stays correct
$m = $b->view(DType::UInt8, [2, 2, 2]);
$model = [];
for ($i = 0; $i < 8; $i++) { $m[intdiv($i, 4)][intdiv($i, 2) % 2][$i % 2] = $i; $model[intdiv($i, 4)][intdiv($i, 2) % 2][$i % 2] = $i; }
gc_collect_cycles();
$before = memory_get_usage();
$sum = $msum = 0;
for ($n = 0; $n < 200000; $n++) {
    $sum += $m[$n & 1][($n >> 1) & 1][($n >> 2) & 1];
    $msum += $model[$n & 1][($n >> 1) & 1][($n >> 2) & 1];
    $r = $m[1][0];
    $r[1] = ($n & 1) + 4;
    $model[1][0][1] = ($n & 1) + 4;
}
gc_collect_cycles();
var_dump($sum === $msum, memory_get_usage() - $before < 4096);
// hold many live sub-views at once, then drop them: the pool never grows past its cap
$held = [];
for ($n = 0; $n < 500; $n++) $held[] = $m[$n & 1][1];
$ok = true;
foreach ($held as $n => $h) { $ok = $ok && $h->byteOffset() === (($n & 1) * 4 + 2) && count($h) === 2; }
var_dump($ok);
$held = null;
$again = $m[1][1]; var_dump($again->byteOffset(), $again->shape());
// a sub-view is a normal object: references, arrays, closures
$cl = function () use ($again) { return $again[0]; };
$arr = ['k' => $again]; $ref = &$arr['k'];
var_dump($cl() === $again[0], $arr['k'][1] === $again[1]);
// objects alive at shutdown: parked pool entries, a live view and a sub-view in globals
$GLOBALS['keep'] = $m[1];
$GLOBALS['keepbuf'] = (new ArrayBuffer(4))->view(DType::Float32, 1);
echo "end\n";
--EXPECT--
int(9)
int(16)
int(9)
bool(true)
bool(true)
bool(true)
int(1)
int(2)
bool(true)
bool(true)
bool(true)
int(6)
array(1) {
  [0]=>
  int(2)
}
bool(true)
bool(true)
end
