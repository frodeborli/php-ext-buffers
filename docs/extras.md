# Extras: element-wise operations as arrow functions

`Tensor::each()` and `Tensor::assign()` are an optional part of ext-buffers, built unless `configure` gets
`--disable-buffers-extras`. Nothing in the rest of the extension depends on them, and the rest of this repository's
documentation describes the extension without them.

```php
public function each(\Closure $fn, TensorAggregate|int|float|bool ...$inputs): Tensor
public function assign(\Closure $fn, TensorAggregate|int|float|bool ...$inputs): static
```

`$fn` describes the value of one element of the result. It is never called: its compiled code is read and evaluated
over whole tensors, with the rules of Tensor arithmetic ([arithmetic.md](arithmetic.md)). Code that cannot be
evaluated that way is rejected before any element is computed.

```php
$out  = $dst->each(fn($d, $s, $k) => $d + ($s - $d) * $k, $src, $coverage);   // three inputs, broadcast
$grey = $rgba->each(fn(array $p) => 0.299 * $p[0] + 0.587 * $p[1] + 0.114 * $p[2]);
$rgba->assign(fn(array $p) => [$p[2], $p[1], $p[0], $p[3]]);                     // in place
$img->each(fn($v, $y, $x) => $x > 0 ? $img[$y][$x - 1] : $v);                    // coordinates, a captured tensor
```

## Inputs and parameters

The inputs are the tensor `each()` is called on, then `...$inputs`: tensors, other `TensorAggregate`s (their
`getTensor()`), or PHP `int`, `float` and `bool` values. Tensors must be in host memory and have a dtype with arithmetic.

The closure's parameters are, in order:

1. **One per input.** A parameter declared `array` takes the input's last axis: it holds one row of that axis, and may
   only be indexed with integer literals within the row (`$p[0]` … `$p[3]`) or returned whole. Any other parameter takes
   one element. A parameter declared `int` accepts integer dtypes, `float` float dtypes and `bool` `Bool`; an
   undeclared one accepts any.
2. **Then the coordinates** of the element, one per axis in axis order, as PHP's int (`Int64`; `Int32` on 32-bit PHP):
   the first coordinate parameter is the index along axis 0. There may be fewer than the number of axes.

The closure must take a parameter for every input. The names are free.

## The shape and dtype of the result

The inputs broadcast as for the operators (aligned from the right; a dimension of 1 stretches), each input counting
with its shape, except that an input bound to an `array` parameter counts without its last axis. The closure is
evaluated once per position of that *iteration shape*, inputs that the closure does not use included.

The closure returns one element, or an array literal of a fixed length k: the result then has a last axis of length k
after the iteration shape. Every path through the closure must return the same kind of value (an element, or an array
of the same length).

The result's dtype is the dtype of the returned expression; the elements of a returned array take their common dtype
(as for `+`). A returned constant has its PHP type: `int` gives `Int64` (`Int32` on 32-bit PHP), `float` `Float64`, `bool`
`Bool`.

## What the closure may contain

The body is an expression (an arrow function), or `return` statements, possibly inside `if` / `elseif` / `else`. Accepted:

* parameters; captured variables of type `int`, `float` and `bool` (constants), and captured tensors (below);
* literals of type `int`, `float` and `bool`;
* `+ - * / % ** << >> & | ^ ~`, with the operators' rules: the promotion of [arithmetic.md](arithmetic.md), integers
  that wrap at the width of their dtype, one IEEE operation per float operation, PHP scalars taking the other operand's
  dtype, `ValueError` for a scalar `int` that does not fit it;
* comparisons `== != === !== < <= > >=`, element-wise, giving `Bool` (the operands are first brought to their common
  dtype; `===` is `==` and `!==` is `!=`); `Bool` compares only with `Bool`;
* `? :`, the short `?:`, `&&`, `||`, `!`, `xor`, and `if` with a `return` on each path: a condition is true when it is
  non-zero (`NAN` is true, as in PHP); the values of the branches must have a common dtype;
* casts `(int)` (to PHP's int, `Int64` or on 32-bit PHP `Int32`, with PHP's conversion of floats: truncated, `NAN` and
  infinities give 0), `(float)` (to `Float64`) and `(bool)`;
* the functions below, called by their global names (also unqualified inside a namespace, unless the namespace defines a
  function of that name);
* reading a captured tensor (below).

Operations on constants only are what PHP computes for them (`2 ** 3` is `8`), as when the same expression is evaluated
by PHP before it meets a tensor.

Everything else is a `ValueError` (unsupported construct) or `TypeError` (dtypes), thrown by `each()` or `assign()`
before any element is computed: other functions, strings, arrays other than a returned literal, assignments, other
statements, loops, generators, by-reference parameters or captures, variadic parameters, named
arguments, and captured values of other types.

### Functions

| function | integer dtypes | float dtypes |
|---|---|---|
| `abs($x)` | wraps (`abs` of the minimum is the minimum) | IEEE `fabs` |
| `floor`, `ceil`, `round($x)` | the value, in its dtype | `round` rounds half away from zero, as PHP's |
| `sqrt($x)` | computed in `Float64` | correctly rounded |
| `min($a, $b, ...)`, `max($a, $b, ...)` | the common dtype | as PHP's: `min` takes a later argument when it is smaller; `max` when it is greater or the two are unordered (`NAN`) |
| `intdiv($a, $b)` | truncated; by zero `DivisionByZeroError`; the minimum by -1 wraps | `TypeError` |
| `fmod($a, $b)` | computed in `Float64` | IEEE `fmod` (exact) |
| `is_nan`, `is_finite`, `is_infinite` | constant `false`, `true`, `false` | `Bool` |

`round()` takes no precision argument here.

## Captured tensors

A tensor captured by the closure (an arrow function captures the variables it uses) can be read with one integer index
per axis: `$img[$y][$x + 1]`, `$lut[$v]`. The indices are any integer expressions. An index outside `0 .. length - 1`
is a `ValueError` naming the element of the result that needed it; a read in a branch that is not selected (`$x > 0 ?
$img[$y][$x - 1] : 0`) is not an error. A captured tensor is read as it was before the call, also when `assign()`
writes into the same memory.

## Errors of elements

Integer `%` and `intdiv()` by zero (`DivisionByZeroError`), a shift by a negative count (`ArithmeticError`), a negative
integer exponent (`ValueError`) and an index out of range are errors of an element. The error raised is the one of the
first element, in row-major order of the result, whose value needs an erroneous operation; when an element has
several, the first in evaluation order. An operation in a branch of `?:`, `&&` or `||` that the element does not select
raises nothing. After such an error, `each()` returns nothing and `assign()` leaves the tensor unchanged.

## assign()

`$t->assign($fn, ...$inputs)` evaluates `$fn` with `$t` as the first input and writes the result into `$t`, converted
to `$t`'s dtype as `convert()` converts (so a `Float64` result into `UInt8` is truncated and wraps). The result must
have `$t`'s shape. `$t` must be writable and in host memory, and, as the first input, have a dtype with arithmetic.
Inputs and captured tensors that share memory with `$t` are read as they were before the call. It returns `$t`.

## Evaluation

The expression is compiled into a graph once per call and evaluated chunk by chunk along the last axis: each operation
runs over a chunk of elements with a loop the compiler vectorizes, so intermediate values occupy a few kilobytes and are
never materialized as whole tensors. Contiguous inputs of the computation's own representation are read in place, and
`each()` writes a single computed output straight into the result. Every operation gives the bits the corresponding
operator gives, whatever the chunking or the CPU.

Opcache's optimizer and JIT do not change which closures are accepted or what they compute.
