--TEST--
Tensor::$value: the element of a 0-d view, readable and writable
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_VERSION_ID < 80400) die('skip PHP 8.4+: property hooks (PHP 8.3 is covered by props_php83.phpt)'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
$b = new Buffer(32);
foreach (DType::cases() as $t) {
    if (in_array($t, [DType::Int64, DType::UInt64, DType::UInt32])) continue;   // not viewable on 32-bit PHP: checked below on 64-bit
    $v = $b->view($t, [], null, $t->byteSize() === null ? 1 : 0);
    $x = match (true) { $t === DType::Bool => true, $t->isComplex() => [1.5, -2.5], $t->isFloatingPoint() => 1.5, default => 3 };
    $v->value = $x;
    $r = $v->value;
    printf("%-18s %s\n", $t->name, json_encode($r));
}
$wide = true;
if (PHP_INT_SIZE === 8) {
    $v = $b->view(DType::Int64, [], null, 8); $v->value = -7; $wide = $wide && $v->value === -7;
    $v = $b->view(DType::UInt64, [], null, 8); $v->value = PHP_INT_MIN; $wide = $wide && $v->value === PHP_INT_MIN;
    $v = $b->view(DType::UInt32, [], null, 8); $v->value = 4000000000; $wide = $wide && $v->value === 4000000000;
    $v->value = -1; $wide = $wide && $v->value === 4294967295;
}
echo $wide ? "wide integers ok\n" : "WIDE INTEGERS WRONG\n";
echo "alias: a 1-D view over the same bytes sees the write\n";
$s = $b->view(DType::Int32, [], null, 8);
$w = $b->view(DType::Int32, 4);
$s->value = -5; var_dump($w[2], $s->value);
$w[2] = 77; var_dump($s->value);
$s->value = 1.9; var_dump($s->value);
if (PHP_INT_SIZE === 8) { $s->value = 2**33 + 5; var_dump($s->value); } else { var_dump(5); }
echo "conversion errors\n";
foreach ([fn() => $s->value = 'x', fn() => $s->value = [1], fn() => $s->value = null, fn() => $b->view(DType::Bool, [])->value = 1, fn() => $b->view(DType::Float32, [])->value = 'a'] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
echo "views with axes\n";
$m = $b->view(DType::Int32, [2, 2]);
foreach ([fn() => $m->value, fn() => $m->value = 1, fn() => $m[0]->value, fn() => $w->value = 1, fn() => $m[1]->value] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
echo "properties\n";
var_dump(isset($s->value), property_exists($s, 'value'));
$s->value = 9; var_dump(array_keys(get_object_vars($s)), get_object_vars($s)['value']);
var_dump(array_key_exists('value', get_object_vars($m)));
try { $r = &$s->value; $r = 1; echo "reference ok\n"; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { $s->value++; var_dump($s->value); } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { $s->value .= 'x'; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
echo "read-only memory and memory the host cannot touch (files)\n";
$file = tempnam(sys_get_temp_dir(), 'buf'); file_put_contents($file, "\x01\x02\x03\x04");
$ro = Buffer::fromFile($file);                       // mapped: host accessible, read-only
$z = $ro->view(DType::UInt8, [], null, 2);
var_dump($z->value);
try { $z->value = 1; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
$d = Buffer::fromFile($file, 0, null, false);        // storage only
$z = $d->view(DType::UInt8, []);
foreach ([fn() => $z->value, fn() => $z->value = 1] as $f) {
    try { $f(); } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
unlink($file);
echo "end\n";
--EXPECT--
Bool               true
Int2               -1
Int4               3
Int8               3
Int16              3
Int32              3
UInt2              3
UInt4              3
UInt8              3
UInt16             3
Float16            1.5
Float32            1.5
Float64            1.5
BFloat16           1.5
Float8E3M4         1.5
Float8E4M3         1.5
Float8E4M3B11FNUZ  1.5
Float8E4M3FN       1.5
Float8E4M3FNUZ     1.5
Float8E5M2         1.5
Float8E5M2FNUZ     1.5
Float8E8M0FNU      2
Float6E2M3FN       1.5
Float6E3M2FN       1.5
Float4E2M1FN       1.5
Complex32          [1.5,-2.5]
Complex64          [1.5,-2.5]
Complex128         [1.5,-2.5]
BComplex32         [1.5,-2.5]
wide integers ok
alias: a 1-D view over the same bytes sees the write
int(-5)
int(-5)
int(77)
int(1)
int(5)
conversion errors
TypeError: Cannot write string to a Tensor of dtype Int32
TypeError: Cannot write array to a Tensor of dtype Int32
TypeError: Cannot write null to a Tensor of dtype Int32
TypeError: Cannot write int to a Tensor of dtype Bool
TypeError: Cannot write string to a Tensor of dtype Float32
views with axes
Error: value is only available on 0-d views; index the view instead
Error: value is only available on 0-d views; index the view instead
Error: value is only available on 0-d views; index the view instead
Error: value is only available on 0-d views; index the view instead
Error: value is only available on 0-d views; index the view instead
properties
bool(true)
bool(true)
array(7) {
  [0]=>
  string(5) "dtype"
  [1]=>
  string(5) "shape"
  [2]=>
  string(4) "ndim"
  [3]=>
  string(7) "strides"
  [4]=>
  string(6) "offset"
  [5]=>
  string(6) "buffer"
  [6]=>
  string(5) "value"
}
int(9)
bool(false)
Error: Indirect modification of Buffers\Tensor::$value is not allowed
int(10)
TypeError: Cannot write string to a Tensor of dtype Int32
read-only memory and memory the host cannot touch (files)
int(3)
Error: Cannot write to a read-only Tensor
Error: Cannot access the elements of a tensor on device file: its memory is not host accessible; transfer to the host first with to()
Error: Cannot write to a read-only Tensor
end
