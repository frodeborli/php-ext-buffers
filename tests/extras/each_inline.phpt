--TEST--
Tensor::each(): functions and static methods written in PHP are inlined, with PHP's parameter conversions
--SKIPIF--
<?php if (!extension_loaded('buffers') || !method_exists(Buffers\Tensor::class, 'each')) die('skip built without the extras');
if (PHP_INT_SIZE < 8) die('skip 64-bit PHP: the expected output names Int64 (PHP\'s int on 64-bit PHP)'); ?>
--FILE--
<?php
use Buffers\{DType, Tensor};

function show(string $name, callable $f): void {
    try { $r = $f(); echo "$name: ", json_encode($r->toArray()), ' ', $r->getDType()->name, "\n"; }
    catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}

function clamp01(float $v): float { return $v < 0.0 ? 0.0 : ($v > 1.0 ? 1.0 : $v); }
function scaled(float $v, float $k = 10.0): float { return $v * $k; }
function fact(int $n): int { return $n <= 1 ? 1 : $n * fact($n - 1); }
function swap(array $p): array { [$a, $b] = $p; return [$b, $a]; }
function sumOf(array $p): float { return $p[0] + $p[1]; }

class Base {
    public static function twice(float $v): float { return 2 * $v; }
    protected static function plusOne(int $v): int { return $v + 1; }
}

final class Pixel extends Base {
    private static function byte(float $v): int { return $v <= 0.0 ? 0 : ($v >= 1.0 ? 255 : (int) ($v * 255 + 0.5)); }
    public static function pack(float $r, float $g, float $b = 0.5): int { return (self::byte($r) << 16) | (self::byte($g) << 8) | self::byte($b); }
    public static function run(Tensor $t): Tensor { return $t->each(static fn($v) => self::pack($v, 1 - $v)); }
    public static function late(Tensor $t): Tensor { return $t->each(static fn($v) => static::byte($v)); }
    public static function inherited(Tensor $t): Tensor { return $t->each(static fn($v) => self::plusOne($v)); }
    public function method(Tensor $t): Tensor { return $t->each(fn($v) => $this->other($v)); }
}

class Open {
    public static function f(float $v): float { return $v; }
    public static function run(Tensor $t): Tensor { return $t->each(static fn($v) => static::f($v)); }
}

$f = Tensor::fromArray([-0.5, 0.25, 0.5, 2.0], DType::Float64);
$i = Tensor::fromArray([1, 2, 3], DType::Int32);
$p = Tensor::fromArray([[1.0, 2.0], [3.0, 4.0]], DType::Float32);

echo "-- functions written in PHP are inlined\n";
show('function', fn() => $f->each(fn($v) => clamp01($v * 2)));
show('default argument', fn() => $f->each(fn($v) => scaled($v)));
show('int argument for a float parameter', fn() => $i->each(fn($v) => clamp01($v)));
show('static method, private helper', fn() => Pixel::run($f));
show('the same values called directly', fn() => Tensor::fromArray(array_map(fn($v) => Pixel::pack($v, 1 - $v), [-0.5, 0.25, 0.5, 2.0]), DType::Int64));
show('inherited protected method', fn() => Pixel::inherited($i));
show('static:: of a final class', fn() => Pixel::late($f));
show('array argument and list destructuring', fn() => $p->each(fn(array $q) => swap($q)));
show('array parameter in a function', fn() => $p->each(fn(array $q) => sumOf($q)));
show('?: between an array parameter and a literal', fn() => $p->each(fn(array $q) => $q[0] > 2 ? [0.0, 0.0] : $q));

echo "-- rejected\n";
show('recursion', fn() => $i->each(fn($v) => fact($v)));
show('float for an int parameter', fn() => $f->each(fn($v) => fact($v)));
show('missing argument', fn() => $f->each(fn($v) => clamp01()));
show('instance method', fn() => (new Pixel)->method($f));
show('static:: that a subclass could override', fn() => Open::run($f));
--EXPECT--
-- functions written in PHP are inlined
function: [0,0.5,1,1] Float64
default argument: [-5,2.5,5,20] Float64
int argument for a float parameter: [1,1,1] Float64
static method, private helper: [65408,4243328,8421504,16711808] Int64
the same values called directly: [65408,4243328,8421504,16711808] Int64
inherited protected method: [2,3,4] Int32
static:: of a final class: [0,64,128,255] Int64
array argument and list destructuring: [[2,1],[4,3]] Float32
array parameter in a function: [3,7] Float32
?: between an array parameter and a literal: [[1,2],[0,0]] Float32
-- rejected
recursion: ValueError: Buffers\Tensor::each(): the recursive call of fact() is not supported in a tensor expression (line 11)
float for an int parameter: TypeError: Buffers\Tensor::each(): parameter $n does not accept Float64 (line 11)
missing argument: ValueError: Buffers\Tensor::each(): a call of clamp01() without its argument $v is not supported in a tensor expression (line 9)
instance method: ValueError: Buffers\Tensor::each(): $this, instance methods and properties is not supported in a tensor expression (line 26)
static:: that a subclass could override: ValueError: Buffers\Tensor::each(): static::f(), which a subclass could override (use self::) is not supported in a tensor expression (line 31)
