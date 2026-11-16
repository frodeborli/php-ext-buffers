--TEST--
Tensor::each() and assign(): what is rejected before evaluation, and errors of elements
--SKIPIF--
<?php if (!extension_loaded('buffers') || !method_exists(Buffers\Tensor::class, 'each')) die('skip built without the extras');
if (PHP_INT_SIZE < 8) die('skip 64-bit PHP: the expected output names Int64 (PHP\'s int on 64-bit PHP)'); ?>
--FILE--
<?php
use Buffers\{DType, Tensor};

function attempt(string $name, callable $f): void {
    try { $r = $f(); echo "$name: ", json_encode($r->toArray()), ' ', $r->getDType()->name, "\n"; }
    catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}

$a = Tensor::fromArray([1.0, 2.0, 3.0], DType::Float32);
$i = Tensor::fromArray([10, 20, 30, 40], DType::Int32);

echo "-- outside the subset: thrown before any element is computed\n";
attempt('unsupported function', fn() => $a->each(fn($v) => mt_rand(0, 10) * $v));
attempt('string', fn() => $a->each(fn($v) => $v . 'x'));
$s = 'abc';
attempt('captured string', fn() => $a->each(fn($v) => $s ? $v : 0));
$o = new stdClass;
attempt('captured object', fn() => $a->each(fn($v) => $o ? $v : 0));
$x = 1;
attempt('by-reference capture', fn() => $a->each(function ($v) use (&$x) { return $v + $x; }));
attempt('assignment', fn() => $a->each(function ($v) { $w = $v * 2; return $w; }));
attempt('loop', fn() => $a->each(function ($v) { for ($k = 0; $k < 3; $k++) $v += 1; return $v; }));
attempt('generator', fn() => $a->each(function ($v) { yield $v; }));
attempt('variadic', fn() => $a->each(fn(...$v) => 1));
attempt('by-reference parameter', fn() => $a->each(fn(&$v) => 1));
attempt('array literal indexed with a constant', fn() => $a->each(fn($v) => [$v, 1][0] + 1));
attempt('named arguments', fn() => $a->each(fn($v) => max(num: $v, value: 2)));
attempt('too few parameters', fn() => $a->each(fn($v) => $v, $a));
attempt('too many coordinates', fn() => $a->each(fn($v, $p, $q) => $v));
attempt('internal callable', fn() => $a->each(Closure::fromCallable('abs')));
attempt('not a tensor input', fn() => $a->each(fn($v, $w) => $v, 'x'));

echo "-- dtype rules, as for the operators\n";
attempt('Bool arithmetic', fn() => Tensor::fromArray([true], DType::Bool)->each(fn($v) => $v + 1));
attempt('Bool with numbers in ?:', fn() => $a->each(fn($v) => $v > 1 ? $v : false));
attempt('bitwise on floats', fn() => $a->each(fn($v) => $v & 1));
attempt('int does not fit', fn() => Tensor::fromArray([1], DType::UInt8)->each(fn($v) => $v + 300));
attempt('Float16 with BFloat16', fn() => Tensor::fromArray([1.0], DType::Float16)->each(fn($v, $w) => $v + $w, Tensor::fromArray([1.0], DType::BFloat16)));
attempt('storage dtype input', fn() => Tensor::fromArray([1.0], DType::Float8E4M3FN)->each(fn($v) => $v));
attempt('int parameter for floats', fn() => $a->each(fn(int $v) => $v));
attempt('array parameter for a scalar', fn() => $a->each(fn($v, array $w) => $v, 1));
attempt('array index out of range', fn() => Tensor::fromArray([[1, 2]], DType::Int8)->each(fn(array $p) => $p[2]));
attempt('array index not constant', fn() => Tensor::fromArray([[1, 2]], DType::Int8)->each(fn(array $p, $y) => $p[$y]));
attempt('float tensor index', fn() => $a->each(fn($v) => $a[$v]));

echo "-- errors of elements: raised for the first element that is selected\n";
attempt('modulo by zero', fn() => $i->each(fn($v) => 100 % ($v - 20)));
attempt('not selected', fn() => $i->each(fn($v) => $v != 20 ? 100 % ($v - 20) : -1));
attempt('intdiv by zero', fn() => $i->each(fn($v) => intdiv(1, $v - 30)));
attempt('negative shift', fn() => $i->each(fn($v) => 1 << ($v - 25)));
attempt('negative exponent', fn() => $i->each(fn($v) => 2 ** ($v - 25)));
attempt('float division by zero is IEEE', fn() => $a->each(fn($v) => [$v / 0 == INF, -$v / 0 == -INF, is_nan($v * 0 / 0)]));
attempt('index out of range', fn() => $i->each(fn($v, $k) => $i[$k + 1]));
attempt('guarded index', fn() => $i->each(fn($v, $k) => $k < 3 ? $i[$k + 1] : 0));
attempt('negative index', fn() => $i->each(fn($v, $k) => $i[$k - 1]));
attempt('first in row-major order', fn() => Tensor::fromArray([[1, 0], [0, 1]], DType::Int8)->each(fn($v, $y, $x) => $x == 1 ? 10 % $v : 5 << ($v - 1)));

echo "-- assign()\n";
$t = Tensor::fromArray([1, 2, 3], DType::Int32);
attempt('result shape differs', fn() => $t->assign(fn($v, $w) => $v + $w, Tensor::fromArray([[1], [2]], DType::Int32)));
attempt('read-only', fn() => Tensor::fromArray([1, 2], DType::Int8)->broadcastTo([2, 2])->assign(fn($v) => 0));
attempt('an error leaves the tensor unchanged', fn() => $t->assign(fn($v) => 6 % ($v - 3)));
echo json_encode($t->toArray()), "\n";
attempt('packed destination', fn() => Tensor::fromArray([1, 2], DType::Int4)->assign(fn($v) => 1));
--EXPECT--
-- outside the subset: thrown before any element is computed
unsupported function: ValueError: Buffers\Tensor::each(): the function mt_rand() is not supported in a tensor expression (line 13)
string: ValueError: Buffers\Tensor::each(): the operation CONCAT is not supported in a tensor expression (line 14)
captured string: ValueError: Buffers\Tensor::each(): the captured variable $s of type string is not supported in a tensor expression (line 16)
captured object: ValueError: Buffers\Tensor::each(): the captured variable $o of type stdClass is not supported in a tensor expression (line 18)
by-reference capture: ValueError: Buffers\Tensor::each(): the by-reference capture of $x is not supported in a tensor expression (line 20)
assignment: ValueError: Buffers\Tensor::each(): the operation ASSIGN is not supported in a tensor expression (line 21)
loop: ValueError: Buffers\Tensor::each(): the operation ASSIGN is not supported in a tensor expression (line 22)
generator: ValueError: Buffers\Tensor::each(): a generator is not supported in a tensor expression (line 23)
variadic: ValueError: Buffers\Tensor::each(): a variadic parameter is not supported in a tensor expression (line 24)
by-reference parameter: ValueError: Buffers\Tensor::each(): the by-reference parameter $v is not supported in a tensor expression (line 25)
array literal indexed with a constant: [2,3,4] Float32
named arguments: ValueError: Buffers\Tensor::each(): this argument (named arguments are not supported) is not supported in a tensor expression (line 27)
too few parameters: ArgumentCountError: Buffers\Tensor::each(): the closure must take a parameter for each of the 2 inputs, it takes 1
too many coordinates: ArgumentCountError: Buffers\Tensor::each(): the closure takes 3 parameters, but there are 1 inputs and 1 axes to iterate over
internal callable: ValueError: Buffers\Tensor::each(): Argument #1 ($fn) must be a closure written in PHP
not a tensor input: TypeError: Buffers\Tensor::each(): Argument #2 must be of type Buffers\TensorAggregate|int|float|bool, string given
-- dtype rules, as for the operators
Bool arithmetic: TypeError: Buffers\Tensor::each(): unsupported operand dtypes for +: Bool and int (line 34)
Bool with numbers in ?:: TypeError: Buffers\Tensor::each(): Bool and Float32 have no common dtype (line 35)
bitwise on floats: TypeError: Buffers\Tensor::each(): unsupported operand dtypes for &: Float32 and int (line 36)
int does not fit: ValueError: Buffers\Tensor::each(): operand 300 does not fit the dtype UInt8 of the other operand (line 37)
Float16 with BFloat16: TypeError: Buffers\Tensor::each(): unsupported operand dtypes for +: Float16 and BFloat16 (line 38)
storage dtype input: TypeError: Buffers\Tensor::each(): input 1 has dtype Float8E4M3FN, which has no arithmetic; convert() it first
int parameter for floats: TypeError: Buffers\Tensor::each(): parameter $v does not accept elements of dtype Float32 (line 40)
array parameter for a scalar: TypeError: Buffers\Tensor::each(): parameter $w is an array, so input 2 must have at least one axis
array index out of range: ValueError: Buffers\Tensor::each(): index 2 is out of range for an array parameter of length 2 (line 42)
array index not constant: ValueError: Buffers\Tensor::each(): indexing an array parameter with anything but an integer literal is not supported in a tensor expression (line 43)
float tensor index: TypeError: Buffers\Tensor::each(): a tensor index must be an integer, not Float32 (line 44)
-- errors of elements: raised for the first element that is selected
modulo by zero: DivisionByZeroError: Modulo by zero
not selected: [0,-1,0,0] Int32
intdiv by zero: DivisionByZeroError: Division by zero
negative shift: ArithmeticError: Bit shift by negative number
negative exponent: ValueError: Negative exponent in an integer power; convert() to a float dtype first
float division by zero is IEEE: [[true,true,true],[true,true,true],[true,true,true]] Bool
index out of range: ValueError: Buffers\Tensor::each(): an index of a captured tensor is out of range (at element [3])
guarded index: [20,30,40,0] Int32
negative index: ValueError: Buffers\Tensor::each(): an index of a captured tensor is out of range (at element [0])
first in row-major order: DivisionByZeroError: Modulo by zero
-- assign()
result shape differs: ValueError: Buffers\Tensor::assign(): the result does not have the shape of the tensor it is assigned to
read-only: Error: Buffers\Tensor::assign(): the tensor is read-only
an error leaves the tensor unchanged: DivisionByZeroError: Modulo by zero
[1,2,3]
packed destination: TypeError: Buffers\Tensor::assign(): input 1 has dtype Int4, which has no arithmetic; convert() it first
