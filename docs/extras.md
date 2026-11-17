# Extras: `Buffers\Compiled`

`Buffers\Compiled` is an optional part of ext-buffers, built unless `configure` gets `--disable-buffers-extras`. Nothing in
the rest of the extension depends on it, and the rest of this repository's documentation describes the extension without it.

```php
final class Compiled
{
    public function __construct(\Closure $fn) {}
    public function __invoke(mixed ...$args): mixed {}
}
```

`new Compiled($fn)` keeps a closure written in PHP. Calling the object runs the closure as machine code: its compiled
opcodes are read and translated, and the closure itself is never called. The language is a subset of PHP over numbers
and tensors, with the rules of Tensor arithmetic ([arithmetic.md](arithmetic.md)). Code outside the subset is rejected
before any of it runs.

```php
$sum = new Compiled(function (Tensor $t): float {
    $s = 0.0;
    for ($i = 0; $i < count($t); $i++) $s += $t[$i];
    return $s;
});
echo $sum($tensor);

$blur = new Compiled(function (Tensor $src, Tensor $dst) {
    [$h, $w] = [$src->shape[0], $src->shape[1]];
    for ($y = 0; $y < $h; $y++) {
        for ($x = 1; $x < $w - 1; $x++) {
            $dst[$y][$x] = ($src[$y][$x - 1] + 2 * $src[$y][$x] + $src[$y][$x + 1]) / 4;
        }
    }
});
$blur($image, $out);
```

## Values

The code computes with numbers: values of a dtype with arithmetic (`Bool`, the integer dtypes, `Float16`, `BFloat16`,
`Float32`, `Float64`) and PHP's `int`, `float` and `bool`. A value has one type, known before the code runs:

* An element read from a tensor has the tensor's dtype.
* PHP's `int`, `float` and `bool` are *weak*. A literal, an argument, a captured variable or a property of one of these
  types meets another operand as a PHP scalar meets a tensor: it takes the other operand's dtype (`$int8 + 1` is `Int8`;
  an `int` that does not fit is a `ValueError` when the operation runs). Between two PHP scalars, `int` is computed as
  PHP's int (`Int64`, `Int32` on 32-bit PHP) and `float` as `Float64`, with the same rules: `/` gives a `float`, `%` of
  floats is `fmod`.
* Integers wrap at the width of their dtype; an `int` never becomes a `float`.
* `(int)` gives PHP's int, `(float)` `Float64`, `(bool)` `Bool`, with PHP's conversions (`(int)` of a float truncates;
  `NAN` and infinities give 0).
* A local variable takes the type of what is assigned to it. Where paths meet (after `if`, `?:`, `&&`, `||`, at the start
  of a loop), a variable that differs between them takes the common type of its values, as for `+`: PHP scalars of the
  same type stay PHP scalars, a PHP `int` and `float` give `float`, a PHP scalar and a dtype give the dtype, two dtypes
  their promotion. Values that have no common type (`Bool` and a number) make the variable unusable there: reading it is a
  `TypeError`.

## What the code may contain

* Parameters of type `int`, `float`, `bool`, `string`, `array`, `Buffers\Tensor`, unions and nullable types of these, or no type
  (the argument's type). Default values.
* Local variables, assignments and compound assignments, `++` and `--`.
* `if`/`elseif`/`else`, `for`, `while`, `do`/`while`, `foreach` (over a tensor's first axis: its elements, or views of its
  rows; over an array of fixed length; with keys; not by reference), `break`, `continue`, `return`.
* `+ - * / % ** << >> & | ^ ~`, comparisons `== != === !== < <= > >=` (`===` is `==` between numbers), `?:`, `?:` without
  the middle, `&&`, `||`, `!`, `xor`, casts, with the operators' rules (arithmetic.md).
* `abs`, `floor`, `ceil`, `round` (no precision argument), `sqrt`, `min`, `max`, `intdiv`, `fmod`, `is_nan`, `is_finite`,
  `is_infinite`, `count`, and the functions below; called by their global names (also unqualified inside a namespace that does not define a
  function of that name). On integers, `floor`, `ceil` and `round` give the value, `abs` wraps (`abs` of the minimum is the
  minimum), `sqrt` computes in `Float64`, `is_nan` and `is_infinite` are `false` and `is_finite` `true`. `min` takes a later
  argument when it is smaller, `max` when it is greater or the two are unordered (`NAN`), as PHP's do.
* `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `exp`,
  `expm1`, `log` (with an optional base), `log10`, `log1p`, `hypot`, `fdiv`, `deg2rad`, `rad2deg`, `pi`: computed in
  double as PHP's own functions compute them (the same results, NaN payloads aside), then rounded once to a float dtype
  of the arguments (`Float32` stays `Float32`); integers are computed as `Float64`. `log()` with a base that is not
  greater than 0 is PHP's `ValueError`. `pow()` is the `**` operator.
* Functions and static methods written in PHP (`helper($x)`, `self::helper($x)`, `ClassName::helper($x)`, `static::` where
  no subclass can override the method), compiled into their callers, with PHP's conversions of arguments and return
  values for their declared types (an `int` passed to a `float` parameter becomes a `float`). Recursion is rejected, and so
  are calls nested more than 8 deep.
* Arrays of fixed length: literals (`[$a, $b, 0.5]`), `$list[$i]` with any integer `$i`, `[$a, $b] = $list`, as arguments
  and return values of functions, and as the returned value.
* Global and class constants (`M_PI`, `self::OPAQUE`) of type `int`, `float`, `bool`.
* `null`: a value from outside that is `null` is known when the code is compiled, so `$x === null`, `$x !== null` and
  `is_null($x)` (and the other `is_*()` type checks) are decided then, and the branch that cannot run is not compiled.
  Called with `null` and with a value, the closure is compiled once for each.
* Strings from outside, read-only: `\ord($s[$i])` reads a byte, `strlen($s)` is the length.
* Tensors: below.

Everything else is a `ValueError` (unsupported construct) or `TypeError` (types) when the code is compiled, before any of
it runs: other internal functions, instance methods, strings made or changed in the code, arrays with keys, assignments to properties, `foreach` by reference, `switch`, `match`, `try`, generators, by-reference parameters or captures, variadic parameters,
named arguments, and values of other types.

## Tensors

A tensor comes from an argument, a captured variable or a property of `$this` (a `Buffers\Tensor`, or a `TensorAggregate`,
whose `getTensor()` is called once per call). Its dtype must have arithmetic, and its memory must be host memory.

* `$t[$i]` indexes the first axis, `$t[$i][$j]` the second, and so on; with an index for every axis it is an element. A
  partial index is a view that can be kept in a variable (`$row = $t[$y]`) and indexed further. An index is any integer;
  one outside `0 .. length - 1` is a `ValueError` ("Index … is out of range for an axis of length …") when it is used.
* `$t->shape[$k]` (`$k` a literal), `$t->ndim`, `count($t)` (the length of the first axis).
* `$t[$i] = $v`, `$t[$i] += $v`, `$t[$i]++` write an element, converting the value to the tensor's dtype as `convert()`
  converts (a `Float64` result into `UInt8` is truncated and wraps). A tensor written to must be writable: otherwise the
  call throws `Error("Cannot write to a read-only Tensor")` before the code runs.
* Reads and writes happen in the order of the code: a write is seen by every later read of the same memory, through any
  view.

## Values from outside

A captured variable (`use ($x)`, or what an arrow function captures), a property of `$this` (for a closure bound to an
object) and an argument are read at every call. Their types are part of the compiled code: the first call with a new
combination of types compiles the closure for it, and later calls with the same types reuse that code, also through
another `Compiled` of the same function (a closure made again, the same method of another object of the class) for the
rest of the request. A captured `float`
that changes from one call to the next does not compile the closure again.

* `int`, `float`, `bool`: weak values (above).
* A tensor, or an object implementing `TensorAggregate`.
* A PHP array of numbers, read as `Tensor::fromArray()` reads it: all `int`s give `Int64`, any `float` gives `Float64`, all
  `bool`s give `Bool`; nested arrays give more axes. It is copied at every call, so writing into it is a `TypeError`.
* A string (its bytes), or `null`.
* A property must be declared, readable from the closure's scope, without hooks, and initialized; it is read, never
  written (writing into a tensor it holds is allowed).

Assigning to a captured variable changes the closure's local copy only, as in PHP.

## Errors

Integer `%` and `intdiv()` by zero (`DivisionByZeroError`), a shift by a negative count (`ArithmeticError`), a negative
integer exponent (`ValueError`), an index out of range and a PHP `int` that does not fit a dtype (`ValueError`) are
raised when the operation runs, where PHP raises errors: the first one the code meets in the order it runs, and only on
the paths it takes. Writes made before the error stay. Loops can be interrupted as PHP's own code can (the time limit,
signals).

## The returned value

A number (an `int` for integer dtypes and `int`, a `float` for float dtypes and `float`, a `bool` for `Bool` and `bool`),
an array of numbers, or nothing (`null`). All `return` statements must give values of a common type, or arrays of the
same length.

## Running

The code is compiled for x86-64 and AArch64 with IR (`extras/ir`), the code generator of PHP's own JIT. On other CPUs it
runs in an interpreter of the same operations. Each operation gives the same bits as the Tensor operators.
