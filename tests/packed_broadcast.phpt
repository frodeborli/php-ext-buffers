--TEST--
Packed sub-byte dtypes: stride 0 (a broadcast) is refused on the last axis, which has no byte stride; other axes broadcast
--FILE--
<?php
use Buffers\{Buffer, DType};
function t(string $label, callable $f): void {
    try { $r = $f(); echo "$label: ", json_encode($r->shape), ' ', json_encode($r->strides), ' ', json_encode($r->toArray()), "\n"; }
    catch (Throwable $e) { echo "$label: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}
$b = new Buffer(4);
$b->view(DType::UInt8)[0] = 0x21;            // Int4 elements 1, 2
$b->view(DType::UInt8)[1] = 0x43;            // 3, 4
t('view(), last stride 0', fn() => $b->view(DType::Int4, [2, 4], [2, 0]));
t('broadcastTo [2,1] -> [2,4]', fn() => $b->view(DType::Int4, [2, 1], [2, 1])->broadcastTo([2, 4]));
t('broadcastTo 0-d -> [4]', fn() => $b->view(DType::Int4, [])->broadcastTo([4]));
t('broadcastTo 0-d -> [3, 4]', fn() => $b->view(DType::UInt2, [])->broadcastTo([3, 4]));
t('broadcastTo 0-d -> [3, 1]', fn() => $b->view(DType::Int4, [])->broadcastTo([3, 1]));
t('broadcastTo [1,4] -> [3,4]', fn() => $b->view(DType::Int4, [1, 4])->broadcastTo([3, 4]));
t('broadcastTo [4] -> [2,4]', fn() => $b->view(DType::Int4, [4])->broadcastTo([2, 4]));
t('view(), stride 0 on axis 0', fn() => $b->view(DType::Int4, [3, 4], [0, 1]));
?>
--EXPECT--
view(), last stride 0: ValueError: Buffers\Buffer::view(): Argument #3 ($strides) the last axis of a packed Int4 view must have stride 1
broadcastTo [2,1] -> [2,4]: ValueError: The last axis of a packed Int4 tensor cannot be broadcast: its elements are bits apart
broadcastTo 0-d -> [4]: ValueError: The last axis of a packed Int4 tensor cannot be broadcast: its elements are bits apart
broadcastTo 0-d -> [3, 4]: ValueError: The last axis of a packed UInt2 tensor cannot be broadcast: its elements are bits apart
broadcastTo 0-d -> [3, 1]: [3,1] [0,1] [[1],[1],[1]]
broadcastTo [1,4] -> [3,4]: [3,4] [0,1] [[1,2,3,4],[1,2,3,4],[1,2,3,4]]
broadcastTo [4] -> [2,4]: [2,4] [0,1] [[1,2,3,4],[1,2,3,4]]
view(), stride 0 on axis 0: [3,4] [0,1] [[1,2,3,4],[1,2,3,4],[1,2,3,4]]
