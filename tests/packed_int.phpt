--TEST--
Packed integers: Int2, Int4, UInt2 and UInt4 are bits apart, little-bit-endian, sign-extended and wrapping
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, BufferView};
mt_srand(11);
function pack_codes(array $codes, int $bits): string {
    $out = array_fill(0, intdiv(count($codes) * $bits + 7, 8), 0);
    foreach ($codes as $i => $c) {
        for ($k = 0; $k < $bits; $k++) if ($c >> $k & 1) { $bit = $i * $bits + $k; $out[$bit >> 3] |= 1 << ($bit & 7); }
    }
    return implode('', array_map('chr', $out));
}
foreach ([DType::Int2, DType::Int4, DType::UInt2, DType::UInt4] as $t) {
    $bits = $t->bits(); $signed = str_starts_with($t->name, 'Int'); $mask = (1 << $bits) - 1;
    printf("%s: bits=%d byteSize=%s aligned=%s integer=%s float=%s\n", $t->name, $bits, var_export($t->byteSize(), true), var_export($t->isByteAligned(), true), var_export($t->isInteger(), true), var_export($t->isFloatingPoint(), true));
    $ext = fn(int $c) => $signed && $c >= 1 << ($bits - 1) ? $c - (1 << $bits) : $c;
    // every code reads as its (sign-extended) value
    $vals = [];
    for ($c = 0; $c <= $mask; $c++) {
        $v = Buffer::fromString(chr($c))->view($t, 1);
        $vals[] = $v[0];
    }
    echo "  codes: ", implode(',', $vals), "\n";
    // random packed data reads as the model says; random writes (ints and floats, in and out of range) wrap
    foreach ([1, 3, 4, 5, 8, 17, 64] as $n) {
        $codes = []; for ($i = 0; $i < $n; $i++) $codes[] = mt_rand(0, $mask);
        $v = Buffer::fromString(pack_codes($codes, $bits))->view($t, $n);
        $bad = 0; for ($i = 0; $i < $n; $i++) if ($v[$i] !== $ext($codes[$i])) $bad++;
        echo "  read n=$n mismatches $bad\n";
    }
    $n = 29; $model = array_fill(0, $n, 0); $buf = new Buffer(intdiv($n * $bits + 7, 8)); $v = $buf->view($t, $n); $bad = 0;
    for ($k = 0; $k < 500; $k++) {
        $i = mt_rand(0, $n - 1);
        $x = match (mt_rand(0, 3)) { 0 => mt_rand(-300, 300), 1 => mt_rand(PHP_INT_MIN, PHP_INT_MAX), 2 => mt_rand(-40, 40) + 0.75, default => mt_rand(0, $mask) };
        $v[$i] = $x; $model[$i] = (int)$x & $mask;
        if ($buf->toString() !== pack_codes($model, $bits)) $bad++;
        if ($v[$i] !== $ext($model[$i])) $bad++;
    }
    echo "  write fuzz: mismatches $bad\n";
    try { $v[0] = 'x'; } catch (TypeError $e) { echo "  ", $e->getMessage(), "\n"; }
    try { $v[0] = true; } catch (TypeError $e) { echo "  ", $e->getMessage(), "\n"; }
    $v[0] = NAN; $v[1] = INF; var_dump($v[0], $v[1]);
}
echo "bytes\n";
$b = new Buffer(2); $v = $b->view(DType::Int4, 4);
$v[0] = -1; $v[1] = 7; $v[2] = -8; $v[3] = 1;
var_dump(bin2hex($b->toString()), $v[0], $v[1], $v[2], $v[3]);
$b = new Buffer(1); $v = $b->view(DType::UInt2, 4);
$v[0] = 1; $v[1] = 2; $v[2] = 3; $v[3] = 0;
var_dump(bin2hex($b->toString()));
$b = new Buffer(1); $v = $b->view(DType::Int2, 4);
$v[0] = -2; $v[1] = -1; $v[2] = 1; $v[3] = 0;
var_dump(bin2hex($b->toString()), $v[0], $v[1], $v[2], $v[3]);
echo "n-D and view() rules\n";
$buf = new Buffer(16);
foreach ([
    'int4 3x4'       => [DType::Int4, [3, 4]],
    'int4 3x3'       => [DType::Int4, [3, 3]],
    'int4 3x3 [4,1]' => [DType::Int4, [3, 3], [4, 1]],
    'int2 2x4'       => [DType::Int2, [2, 4]],
    'int2 2x6'       => [DType::UInt2, [2, 6]],
    'uint2 2x6 [8,1]'=> [DType::UInt2, [2, 6], [8, 1]],
    'uint4 last 2'   => [DType::UInt4, [4], [2]],
    'int2 default'   => [DType::Int2, null, null, 3],
    'int2 end'       => [DType::Int2, 64],
    'int2 one past'  => [DType::Int2, 65],
    'int4 end offset'=> [DType::Int4, 0, null, 16],
] as $name => $args) {
    try { $v = $buf->view(...$args); printf("%-15s ok shape=%s strides=%s offset=%d\n", $name, json_encode($v->shape->toArray()), json_encode($v->strides), $v->offset); }
    catch (Throwable $e) { printf("%-15s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
$m = $buf->view(DType::UInt4, [3, 4]);
for ($i = 0; $i < 3; $i++) for ($j = 0; $j < 4; $j++) $m[$i][$j] = $i * 4 + $j;
var_dump(bin2hex(substr($buf->toString(), 0, 6)), $m[2]->offset, $m[2][3], $m[1]->strides);

echo "end\n";
--EXPECT--
Int2: bits=2 byteSize=NULL aligned=false integer=true float=false
  codes: 0,1,-2,-1
  read n=1 mismatches 0
  read n=3 mismatches 0
  read n=4 mismatches 0
  read n=5 mismatches 0
  read n=8 mismatches 0
  read n=17 mismatches 0
  read n=64 mismatches 0
  write fuzz: mismatches 0
  Cannot write string to a BufferView of dtype Int2
  Cannot write bool to a BufferView of dtype Int2
int(0)
int(0)
Int4: bits=4 byteSize=NULL aligned=false integer=true float=false
  codes: 0,1,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1
  read n=1 mismatches 0
  read n=3 mismatches 0
  read n=4 mismatches 0
  read n=5 mismatches 0
  read n=8 mismatches 0
  read n=17 mismatches 0
  read n=64 mismatches 0
  write fuzz: mismatches 0
  Cannot write string to a BufferView of dtype Int4
  Cannot write bool to a BufferView of dtype Int4
int(0)
int(0)
UInt2: bits=2 byteSize=NULL aligned=false integer=true float=false
  codes: 0,1,2,3
  read n=1 mismatches 0
  read n=3 mismatches 0
  read n=4 mismatches 0
  read n=5 mismatches 0
  read n=8 mismatches 0
  read n=17 mismatches 0
  read n=64 mismatches 0
  write fuzz: mismatches 0
  Cannot write string to a BufferView of dtype UInt2
  Cannot write bool to a BufferView of dtype UInt2
int(0)
int(0)
UInt4: bits=4 byteSize=NULL aligned=false integer=true float=false
  codes: 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
  read n=1 mismatches 0
  read n=3 mismatches 0
  read n=4 mismatches 0
  read n=5 mismatches 0
  read n=8 mismatches 0
  read n=17 mismatches 0
  read n=64 mismatches 0
  write fuzz: mismatches 0
  Cannot write string to a BufferView of dtype UInt4
  Cannot write bool to a BufferView of dtype UInt4
int(0)
int(0)
bytes
string(4) "7f18"
int(-1)
int(7)
int(-8)
int(1)
string(2) "39"
string(2) "1e"
int(-2)
int(-1)
int(1)
int(0)
n-D and view() rules
int4 3x4        ok shape=[3,4] strides=[4,1] offset=0
int4 3x3        ValueError: Buffers\Buffer::view(): Argument #2 ($shape) axis 0 of a packed Int4 view has a stride of 3 elements = 12 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 2 elements
int4 3x3 [4,1]  ok shape=[3,3] strides=[4,1] offset=0
int2 2x4        ok shape=[2,4] strides=[4,1] offset=0
int2 2x6        ValueError: Buffers\Buffer::view(): Argument #2 ($shape) axis 0 of a packed UInt2 view has a stride of 6 elements = 12 bits, which is not a whole number of bytes; pass explicit strides that are multiples of 4 elements
uint2 2x6 [8,1] ok shape=[2,6] strides=[8,1] offset=0
uint4 last 2    ValueError: Buffers\Buffer::view(): Argument #3 ($strides) the last axis of a packed UInt4 view must have stride 1
int2 default    ok shape=[52] strides=[1] offset=3
int2 end        ok shape=[64] strides=[1] offset=0
int2 one past   ValueError: Buffers\Buffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (16 bytes)
int4 end offset ok shape=[0] strides=[1] offset=16
string(12) "1032547698ba"
int(4)
int(11)
array(1) {
  [0]=>
  int(1)
}
end
