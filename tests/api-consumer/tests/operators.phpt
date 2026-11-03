--TEST--
C API: operator hook - arithmetic and bitwise operators go to the registered handler
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{ArrayBuffer, DType};
$v = (new ArrayBuffer(24))->view(DType::Float32, [2, 3]);
$w = (new ArrayBuffer(8))->view(DType::Int16, 4);
$s = (new ArrayBuffer(8))->view(DType::Int16, []);
$ops = [
    '+'   => fn() => $v + 1,
    '1+'  => fn() => 1 + $v,
    '-'   => fn() => $v - $w,
    '*'   => fn() => $v * 2.5,
    '/'   => fn() => 4 / $v,
    '%'   => fn() => $v % 3,
    '<<'  => fn() => $v << 1,
    '>>'  => fn() => $w >> $v,
    '|'   => fn() => $v | 1,
    '&'   => fn() => $v & $w,
    '^'   => fn() => $v ^ 'x',
    '~'   => fn() => ~$v,
    '-x'  => fn() => -$w,
    '0-d' => fn() => $s + $s,
    '**'  => fn() => $v ** 2,
    '.'   => fn() => $v . 'x',
    '+= ' => function () use ($v) { $v += 5; return $v; },
    '++'  => function () use ($v) { $v++; },
    '[]+' => fn() => $v[1] + 1,
];
foreach ($ops as $name => $f) {
    try { $r = $f(); printf("%-4s %s\n", $name, is_string($r) ? $r : get_debug_type($r)); }
    catch (Throwable $e) { printf("%-4s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
echo "the variable now holds the result of +=\n";
$u = $v;
$u += 1;
var_dump($u, $v instanceof Buffers\BufferView);
$u = $w; $u *= $u;
var_dump($u);

echo "comparisons are still identity\n";
$v2 = (new ArrayBuffer(24))->view(DType::Float32, [2, 3]);
var_dump($v == $v, $v == $v2, $v === $v2, $v != $v2, $v <=> $v);
echo "end\n";
--EXPECT--
+    add(view<Float32 2x3>, int(1))
1+   add(int(1), view<Float32 2x3>)
-    sub(view<Float32 2x3>, view<Int16 4>)
*    mul(view<Float32 2x3>, float)
/    div(int(4), view<Float32 2x3>)
%    mod(view<Float32 2x3>, int(3))
<<   shl(view<Float32 2x3>, int(1))
>>   shr(view<Int16 4>, view<Float32 2x3>)
|    or(view<Float32 2x3>, int(1))
&    and(view<Float32 2x3>, view<Int16 4>)
^    xor(view<Float32 2x3>, string)
~    not(view<Float32 2x3>)
-x   mul(view<Int16 4>, int(-1))
0-d  add(view<Int16>, view<Int16>)
**   TypeError: Unsupported operand types: Buffers\BufferView ** int
.    Error: Object of class Buffers\BufferView could not be converted to string
+=   add(view<Float32 2x3>, int(5))
++   null
[]+  add(view<Float32 3>, int(1))
the variable now holds the result of +=
string(30) "add(view<Float32 2x3>, int(1))"
bool(true)
string(33) "mul(view<Int16 4>, view<Int16 4>)"
comparisons are still identity
bool(true)
bool(false)
bool(false)
bool(true)
int(0)
end
