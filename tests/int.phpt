--TEST--
Integer dtypes: wrapping, float conversion, type errors
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$b = new ArrayBuffer(64);
foreach (['Int8','Int16','Int32','Int64','UInt8','UInt16','UInt32','UInt64'] as $name) {
    $v = $b->view(constant("DType::$name"), 4);
    $out = [];
    foreach ([0, 1, -1, 127, 128, 255, 256, 32767, 32768, 65535, 65536, 2147483647, 2147483648, 4294967295, 4294967296, PHP_INT_MAX, PHP_INT_MIN] as $x) {
        $v[0] = $x; $out[] = $v[0];
    }
    echo $name, ': ', implode(',', $out), "\n";
}
echo "floats -> ints\n";
$v = $b->view(DType::Int32, 2);
foreach ([1.9, -1.9, 0.5, 1e10, -1e10, 1e30, NAN, INF, -INF, -0.0, 2147483647.9, 4294967296.5] as $f) {
    $v[0] = $f; echo var_export($f, true), ' => ', $v[0], ' (int) ', @(int)$f, "\n";
}
$u = $b->view(DType::UInt64, 2);
$u[0] = -1; var_dump($u[0]); $u[0] = PHP_INT_MIN; var_dump($u[0]); $u[0] = 1.8446744073709552E+19; var_dump($u[0]);
echo "bool\n";
$q = $b->view(DType::Bool, 4);
$q[0] = true; $q[1] = false; var_dump($q[0], $q[1], $q[2]);
$b->view(DType::UInt8)[3] = 2; var_dump($q[3]);
echo "type errors\n";
$i8 = $b->view(DType::Int8, 4);
foreach ([null, "5", [1], true, false, new stdClass, 'x'] as $bad) {
    try { $i8[0] = $bad; echo "no error\n"; } catch (TypeError $e) { echo $e->getMessage(), "\n"; }
}
foreach ([0, 1, 1.0, "1", null, []] as $bad) {
    try { $q[0] = $bad; echo "no error\n"; } catch (TypeError $e) { echo $e->getMessage(), "\n"; }
}
--EXPECT--
Int8: 0,1,-1,127,-128,-1,0,-1,0,-1,0,-1,0,-1,0,-1,0
Int16: 0,1,-1,127,128,255,256,32767,-32768,-1,0,-1,0,-1,0,-1,0
Int32: 0,1,-1,127,128,255,256,32767,32768,65535,65536,2147483647,-2147483648,-1,0,-1,0
Int64: 0,1,-1,127,128,255,256,32767,32768,65535,65536,2147483647,2147483648,4294967295,4294967296,9223372036854775807,-9223372036854775808
UInt8: 0,1,255,127,128,255,0,255,0,255,0,255,0,255,0,255,0
UInt16: 0,1,65535,127,128,255,256,32767,32768,65535,0,65535,0,65535,0,65535,0
UInt32: 0,1,4294967295,127,128,255,256,32767,32768,65535,65536,2147483647,2147483648,4294967295,0,4294967295,0
UInt64: 0,1,-1,127,128,255,256,32767,32768,65535,65536,2147483647,2147483648,4294967295,4294967296,9223372036854775807,-9223372036854775808
floats -> ints
1.9 => 1 (int) 1
-1.9 => -1 (int) -1
0.5 => 0 (int) 0
10000000000.0 => 1410065408 (int) 10000000000
-10000000000.0 => -1410065408 (int) -10000000000
1.0E+30 => 0 (int) 5076964154930102272
NAN => 0 (int) 0
INF => 0 (int) 0
-INF => 0 (int) 0
-0.0 => 0 (int) 0
2147483647.9 => 2147483647 (int) 2147483647
4294967296.5 => 0 (int) 4294967296
int(-1)
int(-9223372036854775808)
int(0)
bool
bool(true)
bool(false)
bool(false)
bool(true)
type errors
Cannot write null to a TypedView of dtype Int8
Cannot write string to a TypedView of dtype Int8
Cannot write array to a TypedView of dtype Int8
Cannot write bool to a TypedView of dtype Int8
Cannot write bool to a TypedView of dtype Int8
Cannot write stdClass to a TypedView of dtype Int8
Cannot write string to a TypedView of dtype Int8
Cannot write int to a TypedView of dtype Bool
Cannot write int to a TypedView of dtype Bool
Cannot write float to a TypedView of dtype Bool
Cannot write string to a TypedView of dtype Bool
Cannot write null to a TypedView of dtype Bool
Cannot write array to a TypedView of dtype Bool
