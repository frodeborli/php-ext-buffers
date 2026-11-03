--TEST--
C API: readiness hook - pending buffers are made ready before data is touched
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, BufferView};
function calls() { return bat_counts()['ready_calls']; }

$b = bat_make_pending(8, 7);
$v = $b->view(DType::UInt8);
echo "views and properties do not wait: ", calls(), "\n";
var_dump($v->shape->toArray(), $v->strides, $v->offset, count($v), isset($v[1]), isset($v[9]), bat_info($v)['ndim']);
echo calls(), " calls\n";
var_dump($v[3]);
echo calls(), " call after the first read\n";
var_dump($v[4], $v[5]);
echo calls(), " calls (ready only once)\n";

echo "every kind of access\n";
$kinds = [
    'read'      => fn($v) => $v[1],
    'write'     => function ($v) { $v[1] = 5; return $v[1]; },
    'foreach'   => function ($v) { $n = 0; foreach ($v as $x) $n += $x; return $n; },
    'empty'     => fn($v) => empty($v[0]),
    'coalesce'  => fn($v) => $v[0] ?? 'null',
    'toDLPack'  => fn($v) => get_class($v->toDLPack()),
    'roundtrip' => fn($v) => bat_roundtrip($v)->shape->toArray(),
    'bat_export'=> fn($v) => bat_export($v)['ndim'],
    'peek'      => null,
];
foreach ($kinds as $name => $f) {
    $before = calls();
    $b = bat_make_pending(4, 9);
    $v = $b->view(DType::UInt8);
    if ($name === 'peek') $r = bat_peek($b, 0);
    else $r = $f($v);
    printf("%-10s calls=%d result=%s\n", $name, calls() - $before, json_encode($r));
}

echo "2-D: sub-views do not wait, leaves do\n";
$b = bat_make_pending(12, 3);
$m = $b->view(DType::UInt8, [3, 4]);
$before = calls();
$row = $m[1];
echo calls() - $before, " calls after making a sub-view\n";
var_dump($row[2]);
echo calls() - $before, " call after reading a leaf\n";

echo "a failing computation leaves the buffer pending\n";
$b = bat_make_pending(4, 8, true);
$v = $b->view(DType::UInt8);
$before = calls();
for ($i = 0; $i < 3; $i++) {
    try { echo "read: ", $v[0], "\n"; } catch (Exception $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
echo calls() - $before, " calls\n";

echo "pending again\n";
bat_set_pending($b, true);
$before = calls();
$v[0] = 1; $x = $v[1];
echo calls() - $before, " call, then ", $v[0], $v[1], "\n";
bat_set_pending($b, false);
bat_set_pending($b, true);
$before = calls();
$v[0];
echo calls() - $before, " call\n";

echo "the callback can only be set while the caller holds the only reference\n";
var_dump(bat_set_ready_late(new Buffers\ArrayBuffer(4)));
$b = new Buffers\ArrayBuffer(4);
var_dump(bat_set_ready_late($b));   // the argument is a second reference
echo "an ArrayBuffer can wait too\n";
$a = bat_make_pending_arraybuffer(5, 66);
$av = $a->view(DType::UInt8);
$before = calls();
var_dump(bin2hex($a->toString()), calls() - $before, bin2hex($a->toString()), calls() - $before, $av[0]);
$a = bat_make_pending_arraybuffer(5, 67);
$before = calls();
$av = $a->view(DType::UInt8);
var_dump($av[4], calls() - $before);
unset($b, $v, $m, $row, $a, $av, $e, $f, $r);
$c = bat_counts();
echo $c['made'] === $c['deleted'] ? "deleters ran exactly once\n" : "DELETER COUNT WRONG\n";
echo $c['ctx_freed'] === $c['pending_made'] ? "contexts freed once each\n" : "CONTEXT COUNT WRONG\n";
echo "end\n";
--EXPECT--
views and properties do not wait: 0
array(1) {
  [0]=>
  int(8)
}
array(1) {
  [0]=>
  int(1)
}
int(0)
int(8)
bool(true)
bool(false)
int(1)
0 calls
int(7)
1 call after the first read
int(7)
int(7)
1 calls (ready only once)
every kind of access
read       calls=1 result=9
write      calls=1 result=5
foreach    calls=1 result=36
empty      calls=1 result=false
coalesce   calls=1 result=9
toDLPack   calls=1 result="Buffers\\DLPackCapsule"
roundtrip  calls=1 result=[4]
bat_export calls=1 result=1
peek       calls=1 result=9
2-D: sub-views do not wait, leaves do
0 calls after making a sub-view
int(3)
1 call after reading a leaf
a failing computation leaves the buffer pending
read: Exception: the computation failed
read: 8
read: 8
2 calls
pending again
1 call, then 18
1 call
the callback can only be set while the caller holds the only reference
bool(true)
bool(false)
an ArrayBuffer can wait too
string(10) "4242424242"
int(1)
string(10) "4242424242"
int(1)
int(66)
int(67)
int(1)
deleters ran exactly once
contexts freed once each
end
