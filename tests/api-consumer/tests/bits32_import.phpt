--TEST--
32-bit PHP: DLPack import of Int64, UInt64 and UInt32 tensors is rejected
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); if (PHP_INT_SIZE !== 4) die('skip 32-bit PHP only'); ?>
--FILE--
<?php
foreach ([[0, 64], [1, 64], [1, 32], [0, 32], [1, 16], [0, 4]] as [$code, $bits]) {
    try { $v = bat_import($code, $bits, 1, [2], [1]); echo "$code/$bits ok ", $v->dtype->name, "\n"; }
    catch (Throwable $e) { printf("%d/%d %s: %s\n", $code, $bits, get_class($e), $e->getMessage()); }
}
unset($v, $e);
$c = bat_counts(); echo $c['tensors_made'] === $c['tensors_deleted'] ? "deleters ran exactly once\n" : "DELETER COUNT WRONG\n";
echo "end\n";
--EXPECT--
0/64 ValueError: The dtype Int64 is not supported on 32-bit PHP: its values do not fit a PHP int
1/64 ValueError: The dtype UInt64 is not supported on 32-bit PHP: its values do not fit a PHP int
1/32 ValueError: The dtype UInt32 is not supported on 32-bit PHP: its values do not fit a PHP int
0/32 ok Int32
1/16 ok UInt16
0/4 ok Int4
deleters ran exactly once
end