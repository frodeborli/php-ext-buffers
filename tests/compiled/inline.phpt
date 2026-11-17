--TEST--
Buffers\Compiled: functions and static methods written in PHP, $this's properties and constants; what is rejected
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras');
if (PHP_INT_SIZE < 8) die('skip 64-bit PHP: the expected output has 64-bit ints'); ?>
--FILE--
<?php
use Buffers\{Compiled, DType, Tensor};

function show(string $name, callable $f): void {
    try { $r = $f(); echo "$name: ", json_encode($r instanceof Tensor ? $r->toArray() : $r), "\n"; }
    catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}

function clamp01(float $v): float { return $v < 0.0 ? 0.0 : ($v > 1.0 ? 1.0 : $v); }
function scaled(float $v, float $k = 10.0): float { return $v * $k; }
function fact(int $n): int { return $n <= 1 ? 1 : $n * fact($n - 1); }
function swap(array $p): array { [$a, $b] = $p; return [$b, $a]; }
function total(Tensor $t): float { $s = 0.0; for ($i = 0; $i < count($t); $i++) $s += $t[$i]; return $s; }

class Base {
    public static function twice(float $v): float { return 2 * $v; }
    protected static function plusOne(int $v): int { return $v + 1; }
}

final class Pixel extends Base {
    private static function byte(float $v): int { return $v <= 0.0 ? 0 : ($v >= 1.0 ? 255 : (int) ($v * 255 + 0.5)); }
    public static function pack(float $r, float $g, float $b = 0.5): int { return (self::byte($r) << 16) | (self::byte($g) << 8) | self::byte($b); }
    public static function run(float $v): int { return (new Compiled(static fn($v) => self::pack($v, 1 - $v)))($v); }
    public static function late(float $v): int { return (new Compiled(static fn($v) => static::byte($v)))($v); }
    public static function inherited(int $v): int { return (new Compiled(static fn($v) => self::plusOne($v)))($v); }
    public function method(float $v) { return (new Compiled(fn($v) => $this->other($v)))($v); }
}

class Open {
    public static function f(float $v): float { return $v; }
    public static function run(float $v) { return (new Compiled(static fn($v) => static::f($v)))($v); }
}

final class Image {
    private const OPAQUE = 127;
    public int $width = 3;
    protected float $gain = 0.5;
    private Tensor $px;
    public $hooked;
    public function __construct() { $this->px = Tensor::fromArray([10, 20, 30], DType::Int32); }
    public function sum(): int { return (new Compiled(function () { $s = 0; for ($i = 0; $i < $this->width; $i++) $s += $this->px[$i]; return $s; }))(); }
    public function scale(): Tensor { (new Compiled(function () { for ($i = 0; $i < $this->width; $i++) $this->px[$i] = (int) ($this->px[$i] * $this->gain); }))(); return $this->px; }
    public function constant(): int { return (new Compiled(fn() => self::OPAQUE - 1))(); }
    public function assignProperty() { return (new Compiled(function () { $this->width = 4; }))(); }
    public function untyped() { $this->hooked = 'x'; return (new Compiled(fn() => $this->hooked))(); }
}

echo "-- functions and static methods written in PHP are compiled into their callers\n";
show('function', fn() => (new Compiled(fn($v) => clamp01($v * 2)))(0.75));
show('default argument', fn() => (new Compiled(fn($v) => scaled($v)))(0.25));
show('int argument for a float parameter', fn() => (new Compiled(fn($v) => clamp01($v)))(1));
show('static method, private helper', fn() => [Pixel::run(0.25), Pixel::pack(0.25, 0.75)]);
show('inherited protected method', fn() => Pixel::inherited(1));
show('static:: of a final class', fn() => Pixel::late(0.5));
show('array argument and list destructuring', fn() => (new Compiled(fn($a, $b) => swap([$a, $b])))(1, 2));
show('tensor argument', fn() => (new Compiled(fn(Tensor $t) => total($t)))(Tensor::fromArray([1.5, 2.5], DType::Float32)));

echo "-- \$this: properties and constants\n";
$img = new Image;
show('int and tensor properties', fn() => $img->sum());
show('protected float property, writes into a tensor property', fn() => $img->scale());
show('class constant', fn() => $img->constant());

echo "-- rejected\n";
show('recursion', fn() => (new Compiled(fn($v) => fact($v)))(5));
show('float for an int parameter', fn() => (new Compiled(fn($v) => fact($v)))(1.5));
show('missing argument', fn() => (new Compiled(fn($v) => clamp01()))(1.0));
show('instance method', fn() => (new Pixel)->method(0.5));
show('static:: that a subclass could override', fn() => Open::run(0.5));
show('assigning a property', fn() => $img->assignProperty());
show('returning a string property', fn() => $img->untyped());
show('an internal function', fn() => (new Compiled(fn($v) => mt_rand(0, 1) + $v))(1));
show('a string', fn() => (new Compiled(fn() => 'text'))());
show('by-reference capture', function () { $n = 1; return (new Compiled(function () use (&$n) { return $n; }))(); });
show('a generator', fn() => (new Compiled(function () { yield 1; }))());
show('try/catch', fn() => (new Compiled(function () { try { return 1; } catch (Exception $e) { return 2; } }))());
show('Bool and a number', fn() => (new Compiled(fn(bool $b, int $i) => $b + $i))(true, 1));
show('too few arguments', fn() => (new Compiled(fn(int $a, int $b) => $a + $b))(1));
show('a variable with types that cannot meet', fn() => (new Compiled(function (int $n) { $v = 1; if ($n > 0) $v = true; return $v; }))(1));
show('a tensor of a dtype without arithmetic', fn() => (new Compiled(fn(Tensor $t) => $t[0]))(Tensor::fromArray([1.0], DType::Float8E4M3FN)));
--EXPECT--
-- functions and static methods written in PHP are compiled into their callers
function: 1
default argument: 2.5
int argument for a float parameter: 1
static method, private helper: [4243328,4243328]
inherited protected method: 2
static:: of a final class: 128
array argument and list destructuring: [2,1]
tensor argument: 4
-- $this: properties and constants
int and tensor properties: 60
protected float property, writes into a tensor property: [5,10,15]
class constant: 126
-- rejected
recursion: ValueError: Buffers\Compiled: the recursive call of fact() is not supported (line 11)
float for an int parameter: TypeError: Buffers\Compiled: parameter $n does not accept float (line 11)
missing argument: ValueError: Buffers\Compiled: a call of clamp01() without its argument $v is not supported (line 9)
instance method: ValueError: Buffers\Compiled: $this as a value and instance methods is not supported (line 26)
static:: that a subclass could override: ValueError: Buffers\Compiled: static::f(), which a subclass could override (use self::) is not supported (line 31)
assigning a property: ValueError: Buffers\Compiled: writing to a property is not supported (line 44)
returning a string property: ValueError: Buffers\Compiled: returning a string is not supported (line 0)
an internal function: ValueError: Buffers\Compiled: the function mt_rand() is not supported (line 72)
a string: ValueError: Buffers\Compiled: a constant of type string is not supported (line 73)
by-reference capture: ValueError: Buffers\Compiled: the by-reference capture of $n of this type (only arrays and tensors) is not supported (line 74)
a generator: ValueError: Buffers\Compiled: a generator is not supported (line 75)
try/catch: ValueError: Buffers\Compiled: try/catch is not supported (line 76)
Bool and a number: TypeError: Buffers\Compiled: Unsupported operand types for +: bool and int (line 77)
too few arguments: ArgumentCountError: Too few arguments to function {closure}(), 1 passed and at least 2 expected
a variable with types that cannot meet: TypeError: Buffers\Compiled: the variable $v has values of types that cannot be combined where paths meet (line 79)
a tensor of a dtype without arithmetic: TypeError: Buffers\Compiled: this argument has dtype Float8E4M3FN, which has no arithmetic (line 80)
