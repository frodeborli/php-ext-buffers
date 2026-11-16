--TEST--
Views at any byte offset: reads and writes match pack()/unpack() of the same bytes
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer, Tensor, CpuDevice};

// elements are little-endian on every host: pack() and unpack() in the host's order, with each element's bytes reversed
// on a big-endian host (s = int16, q = int64, f = float32, d = float64)
function le(string $bytes, int $size): string {
    return pack('S', 1) === "\x01\x00" ? $bytes : implode('', array_map('strrev', str_split($bytes, $size)));
}
function le_unpack(string $fmt, int $size, string $bytes, int $off = 0, int $n = 1): array {
    return array_values(unpack("$fmt$n", le(substr($bytes, $off, $n * $size), $size)));
}
$types = [[DType::Int16, 's', 2], [DType::Int64, 'q', 8], [DType::Float32, 'f', 4], [DType::Float64, 'd', 8]];
$vals = [[-2, 12345, -32768], [-5, PHP_INT_MAX, PHP_INT_MIN], [1.5, -0.25, 1048576.0], [M_PI, -1e300, 0.1]];
$bad = 0;
foreach ($types as $k => [$dt, $fmt, $size]) {
    foreach ([1, 3, 5] as $off) {
        $n = 3;
        $b = new Buffer($off + $n * $size + 7);
        $raw = $b->view(DType::UInt8);
        for ($i = 0; $i < $b->byteLength; $i++) $raw[$i] = ($i * 37 + 11) & 0xff;
        $v = $b->view($dt, $n, null, $off);
        // reads: the same as unpack() of the bytes at the offset
        $want = le_unpack($fmt, $size, $b->toString(), $off, $n);
        if (iterator_to_array($v) != $want) { echo "read mismatch {$dt->name} @$off\n"; $bad++; }
        // writes: the bytes are pack() of the values; neighbours untouched
        $before = $b->toString();
        foreach ($vals[$k] as $i => $x) $v[$i] = $x;
        $after = $b->toString();
        if (substr($after, $off, $n * $size) !== le(pack("$fmt*", ...$vals[$k]), $size)) { echo "write mismatch {$dt->name} @$off\n"; $bad++; }
        if (substr($after, 0, $off) !== substr($before, 0, $off) || substr($after, $off + $n * $size) !== substr($before, $off + $n * $size)) { echo "neighbours changed {$dt->name} @$off\n"; $bad++; }
        if (iterator_to_array($v) !== $vals[$k]) { echo "read back mismatch {$dt->name} @$off\n"; $bad++; }
        // the copy paths: toArray(), convert() into the same dtype (a new aligned buffer) and into Float64
        $same = $v->convert($dt);
        if ($v->toArray() !== $vals[$k] || iterator_to_array($same) !== $vals[$k] || $same->offset !== 0) { echo "copy mismatch {$dt->name} @$off\n"; $bad++; }
        $c = $v->convert(DType::Float64);
        if ($dt !== DType::Int64 && iterator_to_array($c) !== array_map('floatval', $vals[$k])) { echo "convert mismatch {$dt->name} @$off\n"; $bad++; }
        // a 0-d view at the offset
        $z = $b->view($dt, [], null, $off + $size);
        if ($z->value !== $vals[$k][1]) { echo "0-d mismatch {$dt->name} @$off\n"; $bad++; }
    }
}
echo "contiguous: $bad mismatches\n";

echo "strided, 2-D, unaligned\n";
$b = new Buffer(64);
$raw = $b->view(DType::UInt8);
for ($i = 0; $i < 64; $i++) $raw[$i] = $i;
$m = $b->view(DType::Float32, [3, 2], [4, 2], 3);   // rows 16 bytes apart, every other float, from byte 3
$s = $b->toString();
$ok = true;
for ($r = 0; $r < 3; $r++) for ($c = 0; $c < 2; $c++) {
    $o = 3 + $r * 16 + $c * 8;
    if ($m[$r][$c] !== unpack('g', $s, $o)[1]) $ok = false;
}
var_dump($ok, $m->offset, $m->strides, $m[1]->offset);
$m[2][1] = -7.5;
var_dump(unpack('g', $b->toString(), 3 + 2 * 16 + 8)[1], $m[2][1]);
$t = $m->convert(DType::Float32);
var_dump($t->toArray() === $m->toArray(), $t->strides, $t->offset);
// a transposed view of it
$tr = $b->view(DType::Float32, [2, 3], [2, 4], 3);
var_dump($tr[1][2] === $m[2][1]);

echo "default shape with an odd offset\n";
$b = new Buffer(9);
$v = $b->view(DType::Int32, null, null, 1);         // 8 bytes left: 2 elements
var_dump(count($v), $v->offset);
$v[1] = -123456789;
var_dump(le_unpack('l', 4, $b->toString(), 5)[0]);
try { $b->view(DType::Int32, null, null, 2); } catch (ValueError $e) { echo $e->getMessage(), "\n"; }

echo "a complex 0-d view at an odd offset\n";
$b = new Buffer(17);
$c = $b->view(DType::Complex128, [], null, 1);
$c->value = [1.25, -2.5];
var_dump($c->value, unpack('e2', $b->toString(), 1));
--EXPECT--
contiguous: 0 mismatches
strided, 2-D, unaligned
bool(true)
int(3)
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(2)
}
int(19)
float(-7.5)
float(-7.5)
bool(true)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(1)
}
int(0)
bool(true)
default shape with an odd offset
int(2)
int(1)
int(-123456789)
Buffers\Buffer::view(): Argument #2 ($shape) is required: the buffer length minus the offset is not a multiple of the element size (4 bytes)
a complex 0-d view at an odd offset
array(2) {
  [0]=>
  float(1.25)
  [1]=>
  float(-2.5)
}
array(2) {
  [1]=>
  float(1.25)
  [2]=>
  float(-2.5)
}
