--TEST--
Tensor::fromArray(): packed dtypes (Int2, Int4, UInt2, UInt4, Float6, Float4), rows padded to whole bytes
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Tensor, Buffer, DType};

// a 1-D and a 2-D array of every packed dtype, compared with the same values written element by element into a view
$values = [
    'Int2' => [-2, -1, 0, 1, 1, 0, -1], 'Int4' => [-8, -1, 0, 7, 3, -5, 2], 'UInt2' => [0, 1, 2, 3, 3, 2, 1],
    'UInt4' => [0, 15, 7, 8, 1, 14, 9], 'Float6E2M3FN' => [0, 0.125, 1, 7.5, -3.5, 2, 4], 'Float6E3M2FN' => [0, 0.25, 1, 28, -3, 2, 6],
    'Float4E2M1FN' => [0, 0.5, 1, 6, -3, 2, 4],
];
foreach ($values as $name => $v) {
    $dt = constant(DType::class . '::' . $name);
    $one = Tensor::fromArray($v, $dt);
    $ref = (new Buffer(intdiv(count($v) * $dt->bits() + 7, 8)))->view($dt, [count($v)]);
    foreach ($v as $i => $x) $ref[$i] = $x;
    $ok = $one->buffer->toString() === $ref->buffer->toString() && $one->toArray() === $ref->toArray();
    // rows of 7 elements: every row starts on a byte boundary
    $two = Tensor::fromArray([$v, array_reverse($v), $v], $dt);
    $rowbytes = intdiv(7 * $dt->bits() + 7, 8);
    $rows = str_split($two->buffer->toString(), $rowbytes);
    $ok2 = count($rows) === 3 && $rows[0] === $rows[2] && $rows[0] === $ref->buffer->toString()
        && $two->toArray() === [$v === $one->toArray() ? $v : $one->toArray(), array_reverse($one->toArray()), $one->toArray()];
    echo $name, ' ', json_encode($one->toArray()), ' ', var_export($ok && $ok2, true), ' ', $two->buffer->byteLength, "\n";
}

// odd shapes: a 3-d packed tensor, a single element, an empty row
echo json_encode(Tensor::fromArray([[[1, 2, 3], [0, 1, 2]], [[3, 2, 1], [1, 1, 1]]], DType::UInt2)->toArray()), "\n";
echo json_encode(Tensor::fromArray([5], DType::UInt4)->toArray()), ' ', Tensor::fromArray([5], DType::UInt4)->buffer->byteLength, "\n";
echo json_encode(Tensor::fromArray(3, DType::Int4)->toArray()), "\n";
$e = Tensor::fromArray([[], []], DType::Int4);
echo json_encode($e->shape->toArray()), ' ', $e->buffer->byteLength, "\n";
$z = Tensor::fromArray([[1, 2, 3]], DType::Int4);
echo $z->buffer->byteLength, ' ', json_encode($z->strides), "\n";
?>
--EXPECT--
Int2 [-2,-1,0,1,1,0,-1] true 6
Int4 [-8,-1,0,7,3,-5,2] true 12
UInt2 [0,1,2,3,3,2,1] true 6
UInt4 [0,15,7,8,1,14,9] true 12
Float6E2M3FN [0,0.125,1,7.5,-3.5,2,4] true 18
Float6E3M2FN [0,0.25,1,28,-3,2,6] true 18
Float4E2M1FN [0,0.5,1,6,-3,2,4] true 12
[[[1,2,3],[0,1,2]],[[3,2,1],[1,1,1]]]
[5] 1
3
[2,0] 0
2 [4,1]
