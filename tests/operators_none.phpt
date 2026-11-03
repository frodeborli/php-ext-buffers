--TEST--
Arithmetic and bitwise operators on a BufferView are a TypeError when no extension handles them
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (function_exists('bat_counts')) die('skip the API consumer registers an operator handler'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType};
$v = (new Buffer(8))->view(DType::Int16, 4);
$w = (new Buffer(8))->view(DType::Int16, 4);
$ops = [
    '+'  => fn() => $v + 1,
    '1+' => fn() => 1 + $v,
    '-'  => fn() => $v - $w,
    '*'  => fn() => $v * 2.5,
    '/'  => fn() => $v / 2,
    '%'  => fn() => $v % 2,
    '**' => fn() => $v ** 2,
    '<<' => fn() => $v << 1,
    '>>' => fn() => $v >> 1,
    '|'  => fn() => $v | 1,
    '&'  => fn() => $v & 1,
    '^'  => fn() => $v ^ 1,
    '~'  => fn() => ~$v,
    '-x' => fn() => -$v,
    '.'  => fn() => $v . 'x',
    '+=' => function () use ($v) { $v += 1; },
    '++' => function () use ($v) { $v++; },
];
foreach ($ops as $name => $f) {
    try { $f(); echo "$name: no error\n"; } catch (Throwable $e) { printf("%-3s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
echo "comparisons are identity\n";
var_dump($v == $v, $v == $w, $v === $w, $v != $w, $v <=> $v, (bool)$v, $v == null);
echo "end\n";
--EXPECT--
+   TypeError: Unsupported operand types: Buffers\BufferView + int
1+  TypeError: Unsupported operand types: int + Buffers\BufferView
-   TypeError: Unsupported operand types: Buffers\BufferView - Buffers\BufferView
*   TypeError: Unsupported operand types: Buffers\BufferView * float
/   TypeError: Unsupported operand types: Buffers\BufferView / int
%   TypeError: Unsupported operand types: Buffers\BufferView % int
**  TypeError: Unsupported operand types: Buffers\BufferView ** int
<<  TypeError: Unsupported operand types: Buffers\BufferView << int
>>  TypeError: Unsupported operand types: Buffers\BufferView >> int
|   TypeError: Unsupported operand types: Buffers\BufferView | int
&   TypeError: Unsupported operand types: Buffers\BufferView & int
^   TypeError: Unsupported operand types: Buffers\BufferView ^ int
~   TypeError: Cannot perform bitwise not on Buffers\BufferView
-x  TypeError: Unsupported operand types: Buffers\BufferView * int
.   Error: Object of class Buffers\BufferView could not be converted to string
+=  TypeError: Unsupported operand types: Buffers\BufferView + int
++  TypeError: Cannot increment Buffers\BufferView
comparisons are identity
bool(true)
bool(false)
bool(false)
bool(true)
int(0)
bool(true)
bool(false)
end
