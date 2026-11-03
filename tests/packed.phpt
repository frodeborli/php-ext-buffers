--TEST--
Packed sub-byte storage: float6 and float4 elements are bits apart, little-bit-endian
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
mt_srand(7);

/** the bytes of $codes packed little-bit-endian, $bits per element */
function pack_codes(array $codes, int $bits): string {
    $out = array_fill(0, intdiv(count($codes) * $bits + 7, 8), 0);
    foreach ($codes as $i => $c) {
        for ($k = 0; $k < $bits; $k++) {
            if ($c >> $k & 1) { $bit = $i * $bits + $k; $out[$bit >> 3] |= 1 << ($bit & 7); }
        }
    }
    return implode('', array_map('chr', $out));
}
/** the value of every code of a format, taken from the byte-aligned decode of a single element */
function values(DType $t): array {
    $b = new Buffer(1); $raw = $b->view(DType::UInt8, 1); $v = $b->view($t, 1);
    $r = [];
    for ($c = 0; $c < 1 << $t->bits(); $c++) { $raw[0] = $c; $r[$c] = $v[0]; }
    return $r;
}

foreach ([DType::Float4E2M1FN, DType::Float6E2M3FN, DType::Float6E3M2FN] as $t) {
    $bits = $t->bits();
    echo $t->name, ": bits=$bits byteSize=", var_export($t->byteSize(), true), " aligned=", var_export($t->isByteAligned(), true), "\n";
    $val = values($t);

    // reads of random packed data
    foreach ([1, 5, 8, 16, 33] as $n) {
        $codes = []; for ($i = 0; $i < $n; $i++) $codes[] = mt_rand(0, (1 << $bits) - 1);
        $bytes = pack_codes($codes, $bits);
        $buf = Buffer::fromString($bytes);
        $v = $buf->view($t, $n);
        $bad = 0;
        for ($i = 0; $i < $n; $i++) { if (!($v[$i] === $val[$codes[$i]] || (is_nan($v[$i]) && is_nan($val[$codes[$i]])))) $bad++; }
        printf("  read n=%-2d %d bytes, mismatches %d\n", $n, strlen($bytes), $bad);
    }

    // writes, and neighbours stay as they were
    $n = 24; $model = array_fill(0, $n, 0);
    $buf = new Buffer(intdiv($n * $bits, 8)); $v = $buf->view($t, $n);
    $bad = 0;
    for ($k = 0; $k < 400; $k++) {
        $i = mt_rand(0, $n - 1); $c = mt_rand(0, (1 << $bits) - 1);
        if (is_nan($val[$c])) continue;
        $v[$i] = $val[$c]; $model[$i] = $c;
        if ($buf->toString() !== pack_codes($model, $bits)) $bad++;
    }
    printf("  write fuzz: bytes differ from the model %d times\n", $bad);

    // default shape: the rest of the buffer
    foreach ([3, 5, 6, 12] as $len) {
        try { printf("  %2d bytes -> %d elements\n", $len, count((new Buffer($len))->view($t))); }
        catch (ValueError $e) { printf("  %2d bytes -> %s\n", $len, $e->getMessage()); }
    }
    $o = (new Buffer(12))->view($t, null, null, 3);
    printf("  offset 3 of 12 bytes -> %d elements, offset %d\n", count($o), $o->offset);
}

echo "layout of float4: element 0 in the low nibble\n";
$buf = new Buffer(2); $f4 = $buf->view(DType::Float4E2M1FN, 4);
$f4[0] = 1.0; $f4[1] = -1.0; $f4[2] = 6.0; $f4[3] = 0.5;
var_dump(bin2hex($buf->toString()), $f4[0], $f4[1], $f4[2], $f4[3]);
echo "float6 straddles bytes\n";
$buf = new Buffer(3); $f6 = $buf->view(DType::Float6E3M2FN, 4);
foreach ([1.0, 2.0, 3.0, 4.0] as $i => $x) $f6[$i] = $x;
var_dump(bin2hex($buf->toString()), $f6[1], $f6[2], $f6[3]);

echo "n-D views: rows start on byte boundaries\n";
$buf = new Buffer(48);
$m = $buf->view(DType::Float6E2M3FN, [4, 8]);
var_dump($m->strides, $m->offset, $m[1]->offset, $m[3]->offset, $m[2]->strides, $m[2]->shape->toArray(), $m[2]->ndim);
$m[2][5] = 1.5; $m[3][0] = -0.5; $m[0][7] = 7.5;
var_dump($m[2][5], $m[3][0], $m[0][7], $m[1][0], bin2hex($buf->toString()));
$s = $buf->view(DType::Float6E2M3FN, [3, 5], [8, 1], 3);   // rows 8 elements apart, only 5 used, from byte 3
var_dump($s->strides, $s->offset, $s[1]->offset, $s[2][4] === $m[3][0] ? 'same bytes' : 'different');
$o = $buf->view(DType::Float6E2M3FN, [3, 4], [4, 1]);       // overlapping rows share nothing but can be written independently
$o[1][0] = 1.0; var_dump($o[1][0], $o[0][3], $o[2][0]);
$f4m = $buf->view(DType::Float4E2M1FN, [3, 6], [8, 1], 1);
var_dump($f4m->strides, $f4m[2]->offset, count($f4m), count($f4m[1]));
$cube = $buf->view(DType::Float4E2M1FN, [2, 3, 4]);
var_dump($cube->strides, $cube[1]->offset, $cube[1][2]->offset);
foreach ($m as $i => $row) { echo $i, ':', count($row), ' '; } echo "\n";
foreach ($m[1] as $i => $x) { echo $i, ':', var_export($x, true), ' '; } echo "\n";

echo "view() rules\n";
foreach ([
    'fp4 default 3x3'      => [DType::Float4E2M1FN, [3, 3]],
    'fp4 3x4'              => [DType::Float4E2M1FN, [3, 4]],
    'fp4 explicit rows'    => [DType::Float4E2M1FN, [3, 3], [4, 1]],
    'fp4 odd row stride'   => [DType::Float4E2M1FN, [3, 3], [3, 1]],
    'fp4 last stride 2'    => [DType::Float4E2M1FN, [4], [2]],
    'fp4 last stride 2 2d' => [DType::Float4E2M1FN, [2, 4], [8, 2]],
    'fp6 default 2x4'      => [DType::Float6E2M3FN, [2, 4]],
    'fp6 default 2x3'      => [DType::Float6E2M3FN, [2, 3]],
    'fp6 row stride 4'     => [DType::Float6E2M3FN, [2, 3], [4, 1]],
    'fp6 row stride 6'     => [DType::Float6E2M3FN, [2, 3], [6, 1]],
    'fp6 row stride 2'     => [DType::Float6E2M3FN, [2, 3], [2, 1]],
    'fp6 1-D 64 elements'  => [DType::Float6E2M3FN, 64],
    'fp6 1-D 65 elements'  => [DType::Float6E2M3FN, 65],
    'fp6 end exactly'      => [DType::Float6E2M3FN, 8, null, 42],
    'fp6 one past'         => [DType::Float6E2M3FN, 9, null, 42],
    'fp6 offset beyond'    => [DType::Float6E2M3FN, null, null, 49],
    'fp6 offset at end'    => [DType::Float6E2M3FN, 0, null, 48],
    'fp4 empty 2-D'        => [DType::Float4E2M1FN, [0, 3]],
    'fp4 huge'             => [DType::Float4E2M1FN, [PHP_INT_MAX]],
    'fp4 huge stride'      => [DType::Float4E2M1FN, [2, 2], [PHP_INT_MAX, 1]],
] as $name => $args) {
    try { $v = (new Buffer(48))->view(...$args); printf("%-21s ok shape=%s strides=%s offset=%d\n", $name, json_encode($v->shape->toArray()), json_encode($v->strides), $v->offset); }
    catch (Throwable $e) { printf("%-21s %s: %s\n", $name, get_class($e), preg_match('/^(fp4 huge)/', $name) ? '(too large)' : $e->getMessage()); }
}

echo "errors on elements\n";
$f4 = (new Buffer(4))->view(DType::Float4E2M1FN, 8);
foreach ([fn() => $f4[8], fn() => $f4[-1], fn() => $f4[0] = 'x', fn() => $f4[0] = [1], fn() => $f4["1"]] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
}
$f4[0] = 1;       // int
$f4[1] = 100.0;   // saturates
$f4[2] = INF;
$f4[3] = -INF;
$f4[4] = NAN;
$f4[5] = -NAN;
$f4[6] = 0.2;     // rounds to nearest even
$f4[7] = 0.25;
var_dump($f4[0], $f4[1], $f4[2], $f4[3], $f4[4], $f4[5], $f4[6], $f4[7]);
echo "end\n";
--EXPECT--
Float4E2M1FN: bits=4 byteSize=NULL aligned=false
  read n=1  1 bytes, mismatches 0
  read n=5  3 bytes, mismatches 0
  read n=8  4 bytes, mismatches 0
  read n=16 8 bytes, mismatches 0
  read n=33 17 bytes, mismatches 0
  write fuzz: bytes differ from the model 0 times
   3 bytes -> 6 elements
   5 bytes -> 10 elements
   6 bytes -> 12 elements
  12 bytes -> 24 elements
  offset 3 of 12 bytes -> 18 elements, offset 3
Float6E2M3FN: bits=6 byteSize=NULL aligned=false
  read n=1  1 bytes, mismatches 0
  read n=5  4 bytes, mismatches 0
  read n=8  6 bytes, mismatches 0
  read n=16 12 bytes, mismatches 0
  read n=33 25 bytes, mismatches 0
  write fuzz: bytes differ from the model 0 times
   3 bytes -> 4 elements
   5 bytes -> Buffers\Buffer::view(): Argument #2 ($shape) is required: the number of bits in the buffer after the offset is not a multiple of the element width (6 bits)
   6 bytes -> 8 elements
  12 bytes -> 16 elements
  offset 3 of 12 bytes -> 12 elements, offset 3
Float6E3M2FN: bits=6 byteSize=NULL aligned=false
  read n=1  1 bytes, mismatches 0
  read n=5  4 bytes, mismatches 0
  read n=8  6 bytes, mismatches 0
  read n=16 12 bytes, mismatches 0
  read n=33 25 bytes, mismatches 0
  write fuzz: bytes differ from the model 0 times
   3 bytes -> 4 elements
   5 bytes -> Buffers\Buffer::view(): Argument #2 ($shape) is required: the number of bits in the buffer after the offset is not a multiple of the element width (6 bits)
   6 bytes -> 8 elements
  12 bytes -> 16 elements
  offset 3 of 12 bytes -> 12 elements, offset 3
layout of float4: element 0 in the low nibble
string(4) "a217"
float(1)
float(-1)
float(6)
float(0.5)
float6 straddles bytes
string(6) "0c2451"
float(2)
float(3)
float(4)
n-D views: rows start on byte boundaries
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(1)
}
int(0)
int(6)
int(18)
array(1) {
  [0]=>
  int(1)
}
array(1) {
  [0]=>
  int(8)
}
int(1)
float(1.5)
float(-0.5)
float(7.5)
float(0)
string(96) "00000000007c000000000000000000000300240000000000000000000000000000000000000000000000000000000000"
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(1)
}
int(3)
int(9)
string(10) "same bytes"
float(1)
float(0)
float(0)
array(2) {
  [0]=>
  int(8)
  [1]=>
  int(1)
}
int(9)
int(3)
int(6)
array(3) {
  [0]=>
  int(12)
  [1]=>
  int(4)
  [2]=>
  int(1)
}
int(6)
int(10)
0:8 1:8 2:8 3:8 
0:0.0 1:0.0 2:0.0 3:0.0 4:0.0 5:0.0 6:0.0 7:0.0 
view() rules
fp4 default 3x3       ValueError: Buffers\Buffer::view(): Argument #2 ($shape) axis 0 of a packed Float4E2M1FN view has a stride of 3 elements = 12 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 2 elements
fp4 3x4               ok shape=[3,4] strides=[4,1] offset=0
fp4 explicit rows     ok shape=[3,3] strides=[4,1] offset=0
fp4 odd row stride    ValueError: Buffers\Buffer::view(): Argument #3 ($strides) axis 0 of a packed Float4E2M1FN view has a stride of 3 elements = 12 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 2 elements
fp4 last stride 2     ValueError: Buffers\Buffer::view(): Argument #3 ($strides) the last axis of a packed Float4E2M1FN view must have stride 1
fp4 last stride 2 2d  ValueError: Buffers\Buffer::view(): Argument #3 ($strides) the last axis of a packed Float4E2M1FN view must have stride 1
fp6 default 2x4       ok shape=[2,4] strides=[4,1] offset=0
fp6 default 2x3       ValueError: Buffers\Buffer::view(): Argument #2 ($shape) axis 0 of a packed Float6E2M3FN view has a stride of 3 elements = 18 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 4 elements
fp6 row stride 4      ok shape=[2,3] strides=[4,1] offset=0
fp6 row stride 6      ValueError: Buffers\Buffer::view(): Argument #3 ($strides) axis 0 of a packed Float6E2M3FN view has a stride of 6 elements = 36 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 4 elements
fp6 row stride 2      ValueError: Buffers\Buffer::view(): Argument #3 ($strides) axis 0 of a packed Float6E2M3FN view has a stride of 2 elements = 12 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 4 elements
fp6 1-D 64 elements   ok shape=[64] strides=[1] offset=0
fp6 1-D 65 elements   ValueError: Buffers\Buffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (48 bytes)
fp6 end exactly       ok shape=[8] strides=[1] offset=42
fp6 one past          ValueError: Buffers\Buffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (48 bytes)
fp6 offset beyond     ValueError: Buffers\Buffer::view(): Argument #4 ($offset) must be between 0 and the byte length of the buffer
fp6 offset at end     ok shape=[0] strides=[1] offset=48
fp4 empty 2-D         ValueError: Buffers\Buffer::view(): Argument #2 ($shape) axis 0 of a packed Float4E2M1FN view has a stride of 3 elements = 12 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 2 elements
fp4 huge              ValueError: (too large)
fp4 huge stride       ValueError: (too large)
errors on elements
ValueError: Index 8 is out of range for an axis of length 8
ValueError: Index -1 is out of range for an axis of length 8
TypeError: Cannot write string to a Tensor of dtype Float4E2M1FN
TypeError: Cannot write array to a Tensor of dtype Float4E2M1FN
TypeError: Tensor offset must be of type int, string given
float(1)
float(6)
float(6)
float(-6)
float(-0)
float(-0)
float(0)
float(0)
end
