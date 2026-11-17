--TEST--
Buffers\Compiled: arguments, loops, functions, constants, casts, tensors read and written, captures, errors
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras');
if (PHP_INT_SIZE < 8) die('skip 64-bit PHP: the expected output has 64-bit ints'); ?>
--FILE--
<?php
use Buffers\{Compiled, DType, Tensor};

function show(string $name, callable $f): void {
    try { $r = $f(); echo "$name: ", $r instanceof Tensor ? json_encode($r->toArray()) . ' ' . $r->getDType()->name : var_export($r, true), "\n"; }
    catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}
const LIMIT = 3;
class K { const SCALE = 2.5; }

echo "-- scalars and control flow\n";
show('arguments', fn() => (new Compiled(fn(int $a, float $b, bool $c) => $c ? $a * $b : -1.0))(4, 0.5, true));
show('untyped argument takes the call\'s type', fn() => [(new Compiled(fn($a) => $a + 1))(1), (new Compiled(fn($a) => $a + 1))(1.5)]);
show('the same object, other types: another specialization', function () { $c = new Compiled(fn($a) => $a * 2); return [$c(3), $c(3.25), $c(4)]; });
show('for, while, do, break, continue', fn() => (new Compiled(function (int $n) {
    $s = 0;
    for ($i = 0; $i < $n; $i++) { if ($i % 3 == 0) continue; if ($i > 20) break; $s += $i; }
    $j = 0; while ($j < 5) $j += 2;
    $k = 10; do { $k--; } while ($k > 7);
    return [$s, $j, $k];
}))(100));
show('nested loops, a variable whose type widens in the loop', fn() => (new Compiled(function () {
    $acc = 0;
    for ($i = 0; $i < 4; $i++) for ($j = 0; $j < 4; $j++) $acc = $acc + $i * 0.5 + $j;
    return $acc;
}))());
show('integers wrap', fn() => (new Compiled(fn(int $a) => $a + 1))(PHP_INT_MAX));
show('/ gives a float', fn() => (new Compiled(fn(int $a, int $b) => $a / $b))(6, 3));
show('functions', fn() => (new Compiled(fn(float $x) => [abs(-$x), floor($x), ceil($x), round($x), sqrt(16.0), min($x, 2, 3), max(1, $x), intdiv(7, -2), fmod(7.5, 2), is_nan($x), is_finite($x / 0.0 + 1), is_infinite(-INF)]))(2.5));
show('constants', fn() => (new Compiled(fn() => [LIMIT * 2, K::SCALE, PHP_INT_SIZE, M_PI > 3]))());
show('casts', fn() => (new Compiled(fn(float $f) => [(int) $f, (int) -$f, (float) 3, (bool) $f, (int) NAN]))(2.75));
show('returns nothing', fn() => (new Compiled(function () { $a = 1; }))());

echo "-- tensors\n";
$t = Tensor::fromArray([[1, 2, 3], [4, 5, 6]], DType::Int16);
show('read, shape, ndim, count', fn() => (new Compiled(fn() => [$t[1][2], $t->shape[0], $t->shape[1], $t->ndim, count($t), count($t[0])]))());
show('sum of a sub-view in a variable', fn() => (new Compiled(function () use ($t) { $row = $t[1]; $s = 0; for ($i = 0; $i < count($row); $i++) $s += $row[$i]; return $s; }))());
show('elements keep their dtype: Int16 wraps', fn() => (new Compiled(fn() => $t[1][2] * 10000))());
show('(int) gives PHP\'s int', fn() => (new Compiled(fn() => (int) $t[1][2] * 10000))());
show('write, compound assignment, increment', function () use ($t) {
    (new Compiled(function () use ($t) { $t[0][0] = 9; $t[0][1] += 100; $t[0][2]++; $t[1][0] *= -1; }))();
    return $t;
});
show('a write converts as convert(): 70000 into Int16 wraps, a float truncates', function () use ($t) {
    (new Compiled(function () use ($t) { $t[1][1] = 70000; $t[1][2] = 7.9; }))();
    return $t;
});
show('tensor parameter, writes visible to later reads', function () {
    $v = Tensor::fromArray([1.0, 0.0, 0.0, 0.0], DType::Float32);
    (new Compiled(function (Tensor $v) { for ($i = 1; $i < count($v); $i++) $v[$i] = $v[$i - 1] * 2 + 0.5; }))($v);
    return $v;
});
show('Float16 rounds after every operation', function () {
    $h = Tensor::fromArray([1.0, 0.0], DType::Float16);
    (new Compiled(function () use ($h) { $x = $h[0]; for ($i = 0; $i < 4; $i++) $x = $x + 0.0001; $h[1] = $x; }))();
    return $h;
});
show('a literal array, indexed by a variable', fn() => (new Compiled(function () { $a = [1, 2.5, 3]; $s = 0; for ($i = 0; $i < count($a); $i++) $s += $a[$i]; return $s; }))());
$stops = [[0.0, 10], [0.5, 20]];
show('a captured array is read as Tensor::fromArray()', fn() => (new Compiled(fn() => [$stops[1][0], $stops[0][1], count($stops)]))());
show('captured values are read at every call', function () {
    $k = 2; $c = new Compiled(fn() => $k * 10);
    $k = 3; $d = new Compiled(fn() => $k * 10);
    return [$c(), $d()];
});
show('an index out of range', fn() => (new Compiled(fn() => $t[2][0]))());
show('a negative index', fn() => (new Compiled(fn() => $t[0][-1]))());
show('writing a read-only broadcast view', function () {
    $b = Tensor::fromArray([1], DType::Int8)->broadcastTo([3]);
    return (new Compiled(function () use ($b) { $b[0] = 2; }))();
});
show('writes before an error stay', function () {
    $v = Tensor::fromArray([0, 0, 0], DType::Int32);
    try { (new Compiled(function () use ($v) { for ($i = 0; $i < 5; $i++) $v[$i] = $i + 1; }))(); } catch (ValueError $e) { echo "  (", $e->getMessage(), ")\n"; }
    return $v;
});

echo "-- errors of operations, where PHP raises them\n";
show('% by zero', fn() => (new Compiled(fn(int $a, int $b) => $a % $b))(7, 0));
show('intdiv by zero', fn() => (new Compiled(fn(int $a, int $b) => intdiv($a, $b)))(7, 0));
show('shift by a negative count', fn() => (new Compiled(fn(int $a, int $b) => $a << $b))(1, -1));
show('negative integer exponent', fn() => (new Compiled(fn(int $a, int $b) => $a ** $b))(2, -1));
show('an int that does not fit the other operand\'s dtype', fn() => (new Compiled(fn(int $k) => $t[0][0] + $k))(100000));
show('not reached: no error', fn() => (new Compiled(fn(int $a, int $b) => $b != 0 ? $a % $b : -1))(7, 0));

echo "-- null, strings, union types\n";
$k = function (?string $mask, int $n) { $s = 0; for ($i = 0; $i < $n; $i++) { $m = $mask === null ? 255 : \ord($mask[$i]); $s += $m; } return [$s, is_null($mask), $mask === null ? -1 : strlen($mask)]; };
show('a string\'s bytes, and null: as PHP', fn() => [(new Compiled($k))("\x01\x02\xff", 3), (new Compiled($k))(null, 3), $k("\x01\x02\xff", 3), $k(null, 3)]);
show('array|Tensor parameter', fn() => (new Compiled(function (array|Tensor $p) { $s = 0; for ($i = 0; $i < count($p); $i++) $s += $p[$i]; return $s; }))(Tensor::fromArray([4, 5], DType::Int8)));
show('writing into a captured PHP array', function () { $a = [1, 2]; return (new Compiled(function () use ($a) { $a[0] = 5; }))(); });
show('writing into a string', function () { $s = 'ab'; return (new Compiled(function () use ($s) { $s[0] = 'c'; }))(); });
--EXPECT--
-- scalars and control flow
arguments: 2.0
untyped argument takes the call's type: array (
  0 => 2,
  1 => 2.5,
)
the same object, other types: another specialization: array (
  0 => 6,
  1 => 6.5,
  2 => 8,
)
for, while, do, break, continue: array (
  0 => 147,
  1 => 6,
  2 => 7,
)
nested loops, a variable whose type widens in the loop: 36.0
integers wrap: -9223372036854775807-1
/ gives a float: 2.0
functions: array (
  0 => 2.5,
  1 => 2.0,
  2 => 3.0,
  3 => 3.0,
  4 => 4.0,
  5 => 2.0,
  6 => 2.5,
  7 => -3,
  8 => 1.5,
  9 => false,
  10 => false,
  11 => true,
)
constants: array (
  0 => 6,
  1 => 2.5,
  2 => 8,
  3 => true,
)
casts: array (
  0 => 2,
  1 => -2,
  2 => 3.0,
  3 => true,
  4 => 0,
)
returns nothing: NULL
-- tensors
read, shape, ndim, count: array (
  0 => 6,
  1 => 2,
  2 => 3,
  3 => 2,
  4 => 2,
  5 => 3,
)
sum of a sub-view in a variable: 15
elements keep their dtype: Int16 wraps: -5536
(int) gives PHP's int: 60000
write, compound assignment, increment: [[9,102,4],[-4,5,6]] Int16
a write converts as convert(): 70000 into Int16 wraps, a float truncates: [[9,102,4],[-4,4464,7]] Int16
tensor parameter, writes visible to later reads: [1,2.5,5.5,11.5] Float32
Float16 rounds after every operation: [1,1] Float16
a literal array, indexed by a variable: 6.5
a captured array is read as Tensor::fromArray(): array (
  0 => 0.5,
  1 => 10.0,
  2 => 2,
)
captured values are read at every call: array (
  0 => 20,
  1 => 30,
)
an index out of range: ValueError: Index 2 is out of range for an axis of length 2
a negative index: ValueError: Index -1 is out of range for an axis of length 3
writing a read-only broadcast view: Error: Cannot write to a read-only Tensor
  (Index 3 is out of range for an axis of length 3)
writes before an error stay: [1,2,3] Int32
-- errors of operations, where PHP raises them
% by zero: DivisionByZeroError: Modulo by zero
intdiv by zero: DivisionByZeroError: Division by zero
shift by a negative count: ArithmeticError: Bit shift by negative number
negative integer exponent: ValueError: Negative exponent in an integer power; convert() to a float dtype first
an int that does not fit the other operand's dtype: ValueError: Operand 100000 does not fit the dtype Int16 of the other operand
not reached: no error: -1
-- null, strings, union types
a string's bytes, and null: as PHP: array (
  0 => 
  array (
    0 => 258,
    1 => false,
    2 => 3,
  ),
  1 => 
  array (
    0 => 765,
    1 => true,
    2 => -1,
  ),
  2 => 
  array (
    0 => 258,
    1 => false,
    2 => 3,
  ),
  3 => 
  array (
    0 => 765,
    1 => true,
    2 => -1,
  ),
)
array|Tensor parameter: 9
writing into a captured PHP array: TypeError: Buffers\Compiled: writing into a PHP array: it is read as a copy; use a Buffers\Tensor (line 90)
writing into a string: ValueError: Buffers\Compiled: a constant of type string is not supported (line 91)
