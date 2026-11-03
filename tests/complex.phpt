--TEST--
Complex dtypes
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$b = new ArrayBuffer(64);
foreach (['Complex32', 'BComplex32', 'Complex64', 'Complex128'] as $name) {
    $v = $b->view(constant("DType::$name"), 3);
    echo $name, " ", $v->dtype()->byteSize(), " bytes/element\n";
    $v[0] = [1.5, -2.25];
    $v[1] = 3;
    $v[2] = 0.5;
    foreach ([0, 1, 2] as $i) echo "  ", json_encode($v[$i]), "\n";
    foreach ([[1], [1, 2, 3], [7 => 1, 8 => 2], ['a', 'b'], 'x', null, [[1], 2]] as $bad) {
        try { $v[1] = $bad; echo "  no error\n"; } catch (TypeError $e) { echo "  ", $e->getMessage(), "\n"; }
    }
}
--EXPECT--
Complex32 4 bytes/element
  [1.5,-2.25]
  [3,0]
  [0.5,0]
  Cannot write array to a TypedView of dtype Complex32
  Cannot write array to a TypedView of dtype Complex32
  Cannot write array to a TypedView of dtype Complex32
  Cannot write array to a TypedView of dtype Complex32
  Cannot write string to a TypedView of dtype Complex32
  Cannot write null to a TypedView of dtype Complex32
  Cannot write array to a TypedView of dtype Complex32
BComplex32 4 bytes/element
  [1.5,-2.25]
  [3,0]
  [0.5,0]
  Cannot write array to a TypedView of dtype BComplex32
  Cannot write array to a TypedView of dtype BComplex32
  Cannot write array to a TypedView of dtype BComplex32
  Cannot write array to a TypedView of dtype BComplex32
  Cannot write string to a TypedView of dtype BComplex32
  Cannot write null to a TypedView of dtype BComplex32
  Cannot write array to a TypedView of dtype BComplex32
Complex64 8 bytes/element
  [1.5,-2.25]
  [3,0]
  [0.5,0]
  Cannot write array to a TypedView of dtype Complex64
  Cannot write array to a TypedView of dtype Complex64
  Cannot write array to a TypedView of dtype Complex64
  Cannot write array to a TypedView of dtype Complex64
  Cannot write string to a TypedView of dtype Complex64
  Cannot write null to a TypedView of dtype Complex64
  Cannot write array to a TypedView of dtype Complex64
Complex128 16 bytes/element
  [1.5,-2.25]
  [3,0]
  [0.5,0]
  Cannot write array to a TypedView of dtype Complex128
  Cannot write array to a TypedView of dtype Complex128
  Cannot write array to a TypedView of dtype Complex128
  Cannot write array to a TypedView of dtype Complex128
  Cannot write string to a TypedView of dtype Complex128
  Cannot write null to a TypedView of dtype Complex128
  Cannot write array to a TypedView of dtype Complex128
