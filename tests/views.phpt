--TEST--
View methods: slice, transpose, permute, reshape, squeeze, unsqueeze, broadcastTo; copy; zero strides
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};

function attempt(string $name, callable $f): void {
    try { $r = $f(); echo "$name: ", $r instanceof Tensor ? json_encode($r) . ' ' . json_encode($r->shape) : var_export($r, true), "\n"; }
    catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}
$t = Tensor::fromArray(range(0, 23), DType::Int32)->reshape([2, 3, 4]);
echo json_encode($t), "\n";

echo "slice\n";
attempt('axis 2 from 1', fn() => $t->slice(2, 1));
attempt('axis -1, 2 of step 2', fn() => $t->slice(-1, 0, 2, 2));
attempt('axis 1, length 0', fn() => $t->slice(1, 3, 0));
attempt('shares memory', function () use ($t) { $s = Tensor::fromArray([1, 2, 3, 4], DType::Int8); $v = $s->slice(0, 1, 2); $v[0] = 9; return $s; });
attempt('start past the end', fn() => $t->slice(1, 4));
attempt('length too long', fn() => $t->slice(2, 1, 4));
attempt('step 0', fn() => $t->slice(0, 0, null, 0));
attempt('axis 3', fn() => $t->slice(3, 0));
attempt('0-d', fn() => Tensor::fromArray(1)->slice(0, 0));

echo "transpose and permute\n";
$m = Tensor::fromArray([[1, 2, 3], [4, 5, 6]], DType::Float32);
attempt('matrix', fn() => $m->transpose());
attempt('axes 0 and 2', fn() => $t->transpose(0, 2));
attempt('permute [2, 0, 1]', fn() => $t->permute([2, 0, 1]));
attempt('permute twice is identity', fn() => json_encode($t->permute([2, 0, 1])->permute([1, 2, 0])) === json_encode($t));
attempt('permute duplicate', fn() => $t->permute([0, 0, 1]));
attempt('permute too short', fn() => $t->permute([0, 1]));

echo "reshape\n";
attempt('[4, 6]', fn() => $t->reshape([4, 6]));
attempt('[-1, 2]', fn() => $t->reshape([-1, 2])->shape->toArray() === [12, 2]);
attempt('[] of one element', fn() => Tensor::fromArray([7])->reshape([]));
attempt('wrong count', fn() => $t->reshape([5, 5]));
attempt('two -1', fn() => $t->reshape([-1, -1]));
attempt('a transpose (not a view)', fn() => $m->transpose()->reshape([6]));
attempt('a transpose, copied', fn() => $m->transpose()->copy()->reshape([6]));
attempt('a slice keeping rows contiguous', fn() => $t->slice(0, 1)->reshape([3, 4]));
attempt('every other row, rows split', fn() => $t->slice(1, 0, null, 2)->reshape([2, 2, 2, 2]));
attempt('empty', fn() => Tensor::fromArray([[]], DType::Float32)->reshape([0, 5, 2]));

echo "squeeze and unsqueeze\n";
$u = $t->slice(1, 1, 1);  // [2, 1, 4]
attempt('squeeze without an axis', fn() => $u->squeeze());
attempt('squeeze axis 1', fn() => $u->squeeze(1));
attempt('squeeze axis 0', fn() => $u->squeeze(0));
attempt('unsqueeze 0', fn() => $m->unsqueeze(0));
attempt('unsqueeze -1', fn() => $m->unsqueeze(-1));
attempt('unsqueeze 3', fn() => $m->unsqueeze(3));
attempt('unsqueeze then squeeze', fn() => json_encode($m->unsqueeze(1)->squeeze(1)) === json_encode($m));
attempt('a batch of one keeps its other axes', fn() => Tensor::fromArray([[[1, 2]]], DType::Int8)->squeeze(0));

echo "broadcastTo\n";
$row = Tensor::fromArray([1, 2, 3], DType::Int16);
$b = $row->broadcastTo([2, 3]);
attempt('[3] to [2, 3]', fn() => $b);
attempt('strides', fn() => $b->strides);
attempt('is read-only', function () use ($b) { $b[0][0] = 5; });
attempt('[3, 1] to [3, 4]', fn() => Tensor::fromArray([[1], [2], [3]], DType::Int8)->broadcastTo([3, 4]));
attempt('incompatible', fn() => $row->broadcastTo([2, 4]));
attempt('fewer axes', fn() => $m->broadcastTo([3]));
attempt('arithmetic with it', fn() => $b + $b);

echo "zero strides in view()\n";
$buf = Buffer::fromString(pack('V*', 7, 8));
$z = $buf->view(DType::Int32, [3, 2], [0, 1]);
attempt('[3, 2] with strides [0, 1]', fn() => $z);
attempt('read-only', function () use ($z) { $z[1][0] = 1; });
attempt('a length-1 axis with stride 0 stays writable', function () use ($buf) { $w = $buf->view(DType::Int32, [1, 2], [0, 1]); $w[0][1] = 9; return $w; });
attempt('negative stride', fn() => $buf->view(DType::Int32, [2], [-1]));

echo "copy\n";
$c = $m->transpose()->copy();
attempt('contiguous copy of a transpose', fn() => [$c->strides, $c->toArray()]);
$dst = Tensor::fromArray([[0, 0], [0, 0], [0, 0]], DType::Float32);
attempt('into', fn() => $m->transpose()->copy($dst) === $dst ? json_encode($dst) : 'not the same object');
$big = Tensor::fromArray([[0, 0, 0, 0], [0, 0, 0, 0]], DType::Int32);
attempt('into a slice of a bigger tensor', function () use ($big) { Tensor::fromArray([[1, 2], [3, 4]], DType::Int32)->copy($big->slice(1, 1, 2)); return $big; });
$o = Tensor::fromArray([1, 2, 3, 4, 5], DType::Int8);
attempt('overlapping (shift right by one)', function () use ($o) { $o->slice(0, 0, 4)->copy($o->slice(0, 1, 4)); return $o; });
attempt('NaN payload kept', fn() => bin2hex(Buffer::fromString(pack('v', 0x7d01))->view(DType::Float16, 1)->copy()->buffer->toString()));
attempt('wrong shape', fn() => $m->copy($dst));
attempt('wrong dtype', fn() => $m->copy(Tensor::fromArray([[1, 2, 3], [4, 5, 6]], DType::Float64)));
attempt('into a broadcast', fn() => Tensor::fromArray([[1, 2, 3], [4, 5, 6]], DType::Int16)->copy($row->broadcastTo([2, 3])));
attempt('into read-only', fn() => $m->copy(Buffer::fromString(str_repeat("\0", 24), false)->view(DType::Float32, [2, 3])));

echo "packed\n";
$p = (new Buffer(4))->view(DType::Int4, [2, 4]);
for ($i = 0; $i < 2; $i++) for ($j = 0; $j < 4; $j++) $p[$i][$j] = $i * 4 + $j - 4;
attempt('rows', fn() => $p->slice(0, 1));
attempt('last axis from a whole byte', fn() => $p->slice(1, 2));
attempt('last axis mid-byte', fn() => $p->slice(1, 1));
attempt('last axis with a step', fn() => $p->slice(1, 0, null, 2));
attempt('transpose', fn() => $p->transpose());
attempt('reshape keeping the last axis', fn() => $p->reshape([2, 1, 4]));
attempt('reshape changing it', fn() => $p->reshape([8]));
attempt('unsqueeze at the end', fn() => $p->unsqueeze(2));
attempt('copy', fn() => $p->copy());
echo "end\n";
?>
--EXPECT--
[[[0,1,2,3],[4,5,6,7],[8,9,10,11]],[[12,13,14,15],[16,17,18,19],[20,21,22,23]]]
slice
axis 2 from 1: [[[1,2,3],[5,6,7],[9,10,11]],[[13,14,15],[17,18,19],[21,22,23]]] [2,3,3]
axis -1, 2 of step 2: [[[0,2],[4,6],[8,10]],[[12,14],[16,18],[20,22]]] [2,3,2]
axis 1, length 0: [[],[]] [2,0,4]
shares memory: [1,9,3,4] [4]
start past the end: ValueError: Buffers\Tensor::slice(): Argument #2 ($start) must be between 0 and the length of axis 1 (3), 4 given
length too long: ValueError: Buffers\Tensor::slice(): Argument #3 ($length) must be between 0 and 3 (the elements from 1 to the end of axis 2 with step 1), 4 given
step 0: ValueError: Buffers\Tensor::slice(): Argument #4 ($step) must be at least 1, 0 given
axis 3: ValueError: Buffers\Tensor::slice(): Argument #1 ($axis) must be between -3 and 2, 3 given
0-d: ValueError: Buffers\Tensor::slice(): Argument #1 ($axis) is out of range: a 0-d tensor has no axes
transpose and permute
matrix: [[1,4],[2,5],[3,6]] [3,2]
axes 0 and 2: [[[0,12],[4,16],[8,20]],[[1,13],[5,17],[9,21]],[[2,14],[6,18],[10,22]],[[3,15],[7,19],[11,23]]] [4,3,2]
permute [2, 0, 1]: [[[0,4,8],[12,16,20]],[[1,5,9],[13,17,21]],[[2,6,10],[14,18,22]],[[3,7,11],[15,19,23]]] [4,2,3]
permute twice is identity: true
permute duplicate: ValueError: Buffers\Tensor::permute(): Argument #1 ($axes) must list each of the 3 axes once (axis 0 appears twice)
permute too short: ValueError: Buffers\Tensor::permute(): Argument #1 ($axes) must list each of the 3 axes once
reshape
[4, 6]: [[0,1,2,3,4,5],[6,7,8,9,10,11],[12,13,14,15,16,17],[18,19,20,21,22,23]] [4,6]
[-1, 2]: true
[] of one element: 7 []
wrong count: ValueError: Buffers\Tensor::reshape(): Argument #1 ($shape) must describe the same number of elements (24), 25 given
two -1: ValueError: Buffers\Tensor::reshape(): Argument #1 ($shape) must contain only non-negative values and at most one -1
a transpose (not a view): ValueError: Cannot reshape this tensor as a view: its strides do not allow the new shape; reshape a contiguous copy() instead
a transpose, copied: [1,4,2,5,3,6] [6]
a slice keeping rows contiguous: [[12,13,14,15],[16,17,18,19],[20,21,22,23]] [3,4]
every other row, rows split: [[[[0,1],[2,3]],[[8,9],[10,11]]],[[[12,13],[14,15]],[[20,21],[22,23]]]] [2,2,2,2]
empty: [] [0,5,2]
squeeze and unsqueeze
squeeze without an axis: ArgumentCountError: Buffers\Tensor::squeeze() expects exactly 1 argument, 0 given
squeeze axis 1: [[4,5,6,7],[16,17,18,19]] [2,4]
squeeze axis 0: ValueError: Buffers\Tensor::squeeze(): Argument #1 ($axis) must be an axis of length 1, axis 0 has length 2
unsqueeze 0: [[[1,2,3],[4,5,6]]] [1,2,3]
unsqueeze -1: [[[1],[2],[3]],[[4],[5],[6]]] [2,3,1]
unsqueeze 3: ValueError: Buffers\Tensor::unsqueeze(): Argument #1 ($axis) must be between -3 and 2, 3 given
unsqueeze then squeeze: true
a batch of one keeps its other axes: [[1,2]] [1,2]
broadcastTo
[3] to [2, 3]: [[1,2,3],[1,2,3]] [2,3]
strides: array (
  0 => 0,
  1 => 1,
)
is read-only: Error: Cannot write to a read-only Tensor
[3, 1] to [3, 4]: [[1,1,1,1],[2,2,2,2],[3,3,3,3]] [3,4]
incompatible: ValueError: Buffers\Tensor::broadcastTo(): Argument #1 ($shape) is not a broadcast of the tensor's shape: axis 0 has length 3, which is neither 4 nor 1
fewer axes: ValueError: Buffers\Tensor::broadcastTo(): Argument #1 ($shape) must have at least as many axes as the tensor (2)
arithmetic with it: [[2,4,6],[2,4,6]] [2,3]
zero strides in view()
[3, 2] with strides [0, 1]: [[7,8],[7,8],[7,8]] [3,2]
read-only: Error: Cannot write to a read-only Tensor
a length-1 axis with stride 0 stays writable: [[7,9]] [1,2]
negative stride: ValueError: Buffers\Buffer::view(): Argument #3 ($strides) must contain only non-negative values
copy
contiguous copy of a transpose: array (
  0 => 
  array (
    0 => 2,
    1 => 1,
  ),
  1 => 
  array (
    0 => 
    array (
      0 => 1.0,
      1 => 4.0,
    ),
    1 => 
    array (
      0 => 2.0,
      1 => 5.0,
    ),
    2 => 
    array (
      0 => 3.0,
      1 => 6.0,
    ),
  ),
)
into: '[[1,4],[2,5],[3,6]]'
into a slice of a bigger tensor: [[0,1,2,0],[0,3,4,0]] [2,4]
overlapping (shift right by one): [1,1,2,3,4] [5]
NaN payload kept: '017d'
wrong shape: ValueError: Buffers\Tensor::copy(): Argument #1 ($into) must have the dtype and shape of the tensor (Float32, 2 axes)
wrong dtype: ValueError: Buffers\Tensor::copy(): Argument #1 ($into) must have the dtype and shape of the tensor (Float32, 2 axes)
into a broadcast: Error: Cannot write to a read-only Tensor
into read-only: Error: Cannot write to a read-only Tensor
packed
rows: [[0,1,2,3]] [1,4]
last axis from a whole byte: [[-2,-1],[2,3]] [2,2]
last axis mid-byte: ValueError: A slice of the last axis of a packed Int4 tensor must have step 1 and start on a whole byte (a multiple of 2 elements)
last axis with a step: ValueError: A slice of the last axis of a packed Int4 tensor must have step 1 and start on a whole byte (a multiple of 2 elements)
transpose: ValueError: The last axis of a packed Int4 tensor must stay last: its elements are bits apart
reshape keeping the last axis: [[[-4,-3,-2,-1]],[[0,1,2,3]]] [2,1,4]
reshape changing it: ValueError: A packed Int4 tensor can only be reshaped with the same last axis (4): its elements are bits apart
unsqueeze at the end: ValueError: A new axis cannot follow the last axis of a packed Int4 tensor: its elements are bits apart
copy: [[-4,-3,-2,-1],[0,1,2,3]] [2,4]
end
