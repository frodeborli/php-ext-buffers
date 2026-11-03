--TEST--
32-bit PHP: Int64, UInt64 and UInt32 cannot be viewed; everything else works
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_INT_SIZE !== 4) die('skip 32-bit PHP only'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, BufferView};
$b = new Buffer(64);
foreach (DType::cases() as $t) {
    try { $v = $b->view($t, [1]); $v[0] = $t === DType::Bool ? true : 1; echo str_pad($t->name, 18), "ok ", var_export($v[0], true), "\n"; }
    catch (Throwable $e) { printf("%-18s %s: %s\n", $t->name, get_class($e), $e->getMessage()); }
}
echo "end\n";
--EXPECT--
Bool              ok true
Int2              ok 1
Int4              ok 1
Int8              ok 1
Int16             ok 1
Int32             ok 1
Int64              ValueError: Buffers\Buffer::view(): Argument #1 ($type) the dtype Int64 is not supported on 32-bit PHP: its values do not fit a PHP int
UInt2             ok 1
UInt4             ok 1
UInt8             ok 1
UInt16            ok 1
UInt32             ValueError: Buffers\Buffer::view(): Argument #1 ($type) the dtype UInt32 is not supported on 32-bit PHP: its values do not fit a PHP int
UInt64             ValueError: Buffers\Buffer::view(): Argument #1 ($type) the dtype UInt64 is not supported on 32-bit PHP: its values do not fit a PHP int
Float16           ok 1.0
Float32           ok 1.0
Float64           ok 1.0
BFloat16          ok 1.0
Float8E3M4        ok 1.0
Float8E4M3        ok 1.0
Float8E4M3B11FNUZ ok 1.0
Float8E4M3FN      ok 1.0
Float8E4M3FNUZ    ok 1.0
Float8E5M2        ok 1.0
Float8E5M2FNUZ    ok 1.0
Float8E8M0FNU     ok 1.0
Float6E2M3FN      ok 1.0
Float6E3M2FN      ok 1.0
Float4E2M1FN      ok 1.0
Complex32         ok array (
  0 => 1.0,
  1 => 0.0,
)
Complex64         ok array (
  0 => 1.0,
  1 => 0.0,
)
Complex128        ok array (
  0 => 1.0,
  1 => 0.0,
)
BComplex32        ok array (
  0 => 1.0,
  1 => 0.0,
)
import
0/64 ValueError: The dtype Int64 is not supported on 32-bit PHP: its values do not fit a PHP int
1/64 ValueError: The dtype UInt64 is not supported on 32-bit PHP: its values do not fit a PHP int
1/32 ValueError: The dtype UInt32 is not supported on 32-bit PHP: its values do not fit a PHP int
0/32 ok Int32
1/16 ok UInt16
end