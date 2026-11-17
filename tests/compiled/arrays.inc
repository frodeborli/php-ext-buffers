<?php
/* PHP arrays read and written in place: each closure runs as PHP and compiled, on equal copies of its arguments (an
 * argument unpacked from an array goes to a by-reference parameter as a reference to that array's element); the
 * results and the arguments afterwards must be identical (var_export: an int and a float differ) */
use Buffers\Compiled;

function helper_set(array &$a, int $i, $v) { $a[$i] = $v; }
function helper_sum(array $a) { $s = 0; foreach ($a as $v) $s += $v; return $s; }

class ArrHolder {
    public array $px = [1, 2, 3];
    public function scale(int $k) { for ($i = 0; $i < count($this->px); $i++) $this->px[$i] *= $k; return $this->px[0]; }
}

function hash_list(): array {   // a list (array_is_list()) that is not packed
    $h = ['x' => 0];
    unset($h['x']);
    $h[] = 5; $h[] = 6; $h[] = 7;
    return $h;
}

$cases = [
    'sum of ints, in an inlined function' => [fn(array $a) => helper_sum($a), [[1, 2, 3]]],
    'sum of floats' => [function (array $a) { $s = 0.0; for ($i = 0; $i < count($a); $i++) $s += $a[$i]; return $s; }, [[1.5, 2.25, -3.0]]],
    'empty' => [function (array $a) { $s = 0; foreach ($a as $v) $s += $v; return [$s, count($a)]; }, [[]]],
    'bools' => [function (array &$b) { for ($i = 0; $i < count($b); $i++) $b[$i] = !$b[$i]; return $b[0] || $b[1]; }, [[true, false, true]]],
    'by reference: an int array filled with floats' => [function (array &$a, int $n) { for ($i = 0; $i < $n; $i++) $a[$i] = $i * 0.5; }, [[0, 0, 0, 0], 4]],
    'by reference: an int written into a float array stays an int' => [function (array &$a) { $a[0] = 7; $a[1] = 2.5; }, [[1.0, 1.0, 1.0]]],
    'by reference: nested lists' => [function (array &$m) { for ($y = 0; $y < count($m); $y++) for ($x = 0; $x < count($m[$y]); $x++) $m[$y][$x] = $y * 10 + $x; }, [[[0, 0, 0], [0, 0, 0]]]],
    'by reference: through an inlined function' => [function (array &$a) { helper_set($a, 1, 42); }, [[1, 2, 3]]],
    'by value: the caller keeps its array' => [function (array $a) { $a[0] = 100; return $a[0] + $a[1]; }, [[1, 2]]],
    'by value: written, then returned' => [function (array $a) { for ($i = 0; $i < count($a); $i++) $a[$i] = $a[$i] * 2; return $a; }, [[1, 2, 3]]],
    'bitwise operators' => [function (array &$a) { for ($i = 0; $i < count($a); $i++) $a[$i] = ($a[$i] << 4 | $a[$i] >> 2) & 0xff ^ ~$a[$i]; }, [[1, 200, 77, -5]]],
    'math functions' => [function (array &$a) { for ($i = 0; $i < count($a); $i++) $a[$i] = sqrt(abs($a[$i])) + floor($a[$i] / 3); }, [[1.0, -9.0, 16.5]]],
    'foreach with keys over nested lists' => [function (array $m) { $s = 0; foreach ($m as $y => $row) foreach ($row as $x => $v) $s += ($y + 1) * ($x + 1) * $v; return $s; }, [[[1, 2], [3, 4], [5, 6]]]],
    'a list that is not packed: read' => [fn(array $a) => $a[0] + $a[2], [hash_list()]],
    'a list that is not packed: written' => [function (array &$a) { $a[1] = -1; }, [hash_list()]],
];

foreach ($cases as $name => [$fn, $args]) {
    $php = $args;
    $cmp = $args;
    $r1 = $fn(...$php);
    $r2 = (new Compiled($fn))(...$cmp);
    $a = var_export([$r1, $php], true);
    $b = var_export([$r2, $cmp], true);
    echo $a === $b ? "ok   $name\n" : "DIFF $name\n  php:      $a\n  compiled: $b\n";
}

/* a captured array: by value a copy for the call, by reference the variable's */
$v = [1, 2]; $w = [1, 2];
$byValue = function () use ($v) { $v[0] = 9; return $v[0] + $v[1]; };
$byRef = function () use (&$w) { $w[0] = 9; return $w[0] + $w[1]; };
echo 'captures: ', json_encode([(new Compiled($byValue))(), $v, (new Compiled($byRef))(), $w]), "\n";

/* a property of $this, written in place */
$o = new ArrHolder;
$p = new ArrHolder;
echo 'property: ', json_encode([(new Compiled($o->scale(...)))(3), $o->px, $p->scale(3), $p->px]), "\n";

/* the caller's other copies of the array are untouched (it is separated before the first write, as PHP does) */
$x = [1, 2, 3];
$copy = $x;
(new Compiled(function (array &$a) { $a[2] = 0; }))($x);
echo 'separated: ', json_encode([$x, $copy]), "\n";

/* ->__invoke() and call_user_func() */
$y = [1];
(new Compiled(function (array &$a) { $a[0] = 5; }))->__invoke($y);
echo '->__invoke(): ', json_encode($y), ', call_user_func(): ', call_user_func(new Compiled(fn(array $a) => $a[0] * 2), [21]), "\n";

/* errors */
function err(string $name, callable $f): void {
    try {
        $f();
        echo "$name: no error\n";
    } catch (Throwable $e) {
        $m = $e->getMessage();
        $p = strrpos($m, ' (line ');   // (no preg: PCRE's JIT reads past strings, which valgrind reports)
        echo "$name: ", get_class($e), ': ', $p === false ? $m : substr($m, 0, $p), "\n";
    }
}
err('not a list', fn() => (new Compiled(fn(array $a) => count($a)))(['a' => 1]));
err('bools and numbers', fn() => (new Compiled(fn(array $a) => count($a)))([true, 1]));
err('a string in it', fn() => (new Compiled(fn(array $a) => count($a)))([1, '2']));
err('lists of different lengths', fn() => (new Compiled(fn(array $a) => count($a)))([[1, 2], [3]]));
err('an index out of range', fn() => (new Compiled(fn(array $a) => $a[2]))([1]));
err('a negative index', fn() => (new Compiled(fn(array $a) => $a[-1]))([1]));
err('a bool into numbers', fn() => (new Compiled(function (array $a) { $a[0] = true; }))([1]));
err('written and copied', fn() => (new Compiled(function (array $a) { $b = $a; $a[0] = 1; return $b[0]; }))([5]));
err('written while iterated', fn() => (new Compiled(function (array $a) { foreach ($a as $i => $v) $a[$i] = $v + 1; }))([5]));
err('assigning a by-reference variable', function () { $z = [1]; (new Compiled(function (array &$a) { $a = [1]; }))($z); });
err('an int by reference', function () { $n = 1; (new Compiled(function (int &$n) { return $n; }))($n); });
err('a literal for a by-reference parameter', fn() => (new Compiled(function (array &$a) {}))([1]));
