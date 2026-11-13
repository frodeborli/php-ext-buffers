--TEST--
0-d views: one element, no axes
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
$b = new Buffer(16);
$raw = $b->view(DType::UInt8);
for ($i = 0; $i < 16; $i++) $raw[$i] = $i + 1;

$s = $b->view(DType::Int32, [], null, 4);
var_dump($s->ndim, $s->shape->toArray(), $s->strides, $s->offset, $s->dtype, $s->shape->size, $s->shape->concrete);
$s2 = $b->view(DType::Float64, [], []);
var_dump($s2->ndim, $s2->strides);

echo "no axes\n";
$cases = [
    'count'        => fn() => count($s),
    'count method' => fn() => $s->count(),
    'read'         => fn() => $s[0],
    'read method'  => fn() => $s->offsetGet(0),
    'write'        => function () use ($s) { $s[0] = 1; },
    'write method' => fn() => $s->offsetSet(0, 1),
    'append'       => function () use ($s) { $s[] = 1; },
    'isset'        => fn() => isset($s[0]),
    'exists method'=> fn() => $s->offsetExists(0),
    'unset'        => function () use ($s) { unset($s[0]); },
    'foreach'      => function () use ($s) { foreach ($s as $x) {} },
    'iterator'     => fn() => $s->getIterator(),
    'coalesce'     => fn() => $s[0] ?? 1,
    'nested'       => fn() => $s[0][0],
];
foreach ($cases as $name => $f) {
    try { $f(); echo "$name: no error\n"; } catch (Throwable $e) { printf("%-13s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}

echo "arguments\n";
foreach ([
    'strides for 0 dims'  => [DType::Int32, [], [1]],
    'offset at the end'   => [DType::UInt8, [], null, 16],
    'past the end'        => [DType::Int32, [], null, 14],
    'last byte'           => [DType::UInt8, [], null, 15],
    'misaligned'          => [DType::Int32, [], null, 2],
    'packed fp6'          => [DType::Float6E2M3FN, [], null, 15],
    'packed fp4 at end'   => [DType::Float4E2M1FN, [], null, 16],
] as $name => $args) {
    try { $v = $b->view(...$args); printf("%-18s ok ndim=%d offset=%d\n", $name, $v->ndim, $v->offset); }
    catch (Throwable $e) { printf("%-18s %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
$empty = (new Buffer(0));
try { $empty->view(DType::UInt8, []); } catch (ValueError $e) { echo $e->getMessage(), "\n"; }

echo "a 0-d view shares memory\n";
$s = $b->view(DType::Int32, [], null, 4);
$w = $b->view(DType::Int32, 4);
$w[1] = 0x01020304;
var_dump(substr($b->toString(), 4, 4) === pack('l', 0x01020304));   // native byte order
$t = $s->buffer->view(DType::Int32, [], null, 8);
var_dump($t->offset);

echo "end\n";
--EXPECT--
int(0)
array(0) {
}
array(0) {
}
int(4)
enum(Buffers\DType::Int32)
int(1)
bool(true)
int(0)
array(0) {
}
no axes
count         Error: A 0-d view has no axes
count method  Error: A 0-d view has no axes
read          Error: A 0-d view has no axes
read method   Error: A 0-d view has no axes
write         Error: A 0-d view has no axes
write method  Error: A 0-d view has no axes
append        Error: A 0-d view has no axes
isset         Error: A 0-d view has no axes
exists method Error: A 0-d view has no axes
unset         Error: A 0-d view has no axes
foreach       Error: A 0-d view has no axes
iterator      Error: A 0-d view has no axes
coalesce      Error: A 0-d view has no axes
nested        Error: A 0-d view has no axes
arguments
strides for 0 dims ValueError: Buffers\Buffer::view(): Argument #3 ($strides) must have one entry per dimension (0)
offset at the end  ValueError: Buffers\Buffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (16 bytes)
past the end       ValueError: Buffers\Buffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (16 bytes)
last byte          ok ndim=0 offset=15
misaligned         ok ndim=0 offset=2
packed fp6         ok ndim=0 offset=15
packed fp4 at end  ValueError: Buffers\Buffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (16 bytes)
Buffers\Buffer::view(): Argument #2 ($shape) with the given strides and offset extends beyond the end of the buffer (0 bytes)
a 0-d view shares memory
bool(true)
int(8)
end
