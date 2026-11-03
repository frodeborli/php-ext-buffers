--TEST--
JSON: BufferView and Shape are JsonSerializable
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, BufferView, Shape, ForeignDevice};
$b = new Buffer(64);
$raw = $b->view(DType::UInt8); for ($i = 0; $i < 64; $i++) $raw[$i] = $i;
var_dump($v = $b->view(DType::UInt8, [2, 3], [4, 1], 1) instanceof JsonSerializable);
$v = $b->view(DType::UInt8, [2, 3], [4, 1], 1);
echo json_encode($v), "\n";
echo json_encode($b->view(DType::UInt8, 5)), "\n";
echo json_encode($b->view(DType::UInt8, [2, 2, 2])), "\n";
echo json_encode($b->view(DType::UInt8, [0])), json_encode($b->view(DType::UInt8, [0, 3])), json_encode($b->view(DType::UInt8, [2, 0])), "\n";
echo "0-d: the scalar\n";
$z = (new Buffer(8))->view(DType::Float32, []); $z->value = 2.5;
echo json_encode($b->view(DType::UInt8, [], null, 7)), json_encode($z), "\n";
echo "dtypes\n";
$f = (new Buffer(12))->view(DType::Float32, 3); $f[0] = 1.5; $f[1] = -2.25; $f[2] = 0.1;
echo json_encode($f), "\n";
$c = (new Buffer(16))->view(DType::Complex64, 2); $c[0] = [1.5, 2]; $c[1] = 3;
echo json_encode($c), "\n";
$i4 = (new Buffer(3))->view(DType::Int4, 6); for ($k = 0; $k < 6; $k++) $i4[$k] = $k - 3;
echo json_encode($i4), "\n";
$u2 = (new Buffer(2))->view(DType::UInt2, [2, 4]); for ($k = 0; $k < 2; $k++) for ($j = 0; $j < 4; $j++) $u2[$k][$j] = $j;
echo json_encode($u2), "\n";
$f6 = (new Buffer(6))->view(DType::Float6E2M3FN, [2, 4]); $f6[1][2] = 1.5;
echo json_encode($f6), "\n";
$bo = (new Buffer(3))->view(DType::Bool, 3); $bo[1] = true; if (PHP_INT_SIZE === 8) { $i8 = (new Buffer(8))->view(DType::Int64, 1); $i8[0] = -5; $j8 = json_encode($i8); } else { $j8 = "[-5]"; }
echo json_encode($bo), $j8, "\n";
echo "NaN and INF: json_encode's own rules\n";
$n = (new Buffer(16))->view(DType::Float64, 2); $n[0] = NAN; $n[1] = INF;
var_dump(json_encode($n), json_last_error_msg());
var_dump(json_encode($n, JSON_PARTIAL_OUTPUT_ON_ERROR));
try { json_encode($n, JSON_THROW_ON_ERROR); } catch (JsonException $e) { echo $e->getMessage(), "\n"; }
echo "nesting\n";
echo json_encode(['a' => $v, 'shape' => $v->shape, 'dtype' => $v->dtype, 'n' => [$b->view(DType::UInt8, [], null, 2)], 'z' => $z]), "\n";
echo json_encode($v->shape, JSON_PRETTY_PRINT), "\n";
echo "Shape\n";
echo json_encode(new Shape([2, 3, 4])), json_encode(new Shape([])), json_encode($v->shape), "\n";
var_dump((new Shape([1]))->jsonSerialize(), $v->jsonSerialize()[1]);
echo "device memory\n";
$file = tempnam(sys_get_temp_dir(), 'buf'); file_put_contents($file, "\x01\x02\x03\x04");
echo json_encode(Buffer::fromFile($file)->view(DType::UInt8, 4)), "\n";   // a mapped file is host memory
$d = Buffer::fromFile($file, 0, null, false)->view(DType::UInt8, 4);   // storage only
try { json_encode($d, JSON_THROW_ON_ERROR); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { json_encode($d); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
unlink($file);
echo "end\n";
--EXPECT--
bool(true)
[[1,2,3],[5,6,7]]
[0,1,2,3,4]
[[[0,1],[2,3]],[[4,5],[6,7]]]
[][][[],[]]
0-d: the scalar
72.5
dtypes
[1.5,-2.25,0.10000000149011612]
[[1.5,2],[3,0]]
[-3,-2,-1,0,1,2]
[[0,1,2,3],[0,1,2,3]]
[[0,0,0,0],[0,0,1.5,0]]
[false,true,false][-5]
NaN and INF: json_encode's own rules
bool(false)
string(34) "Inf and NaN cannot be JSON encoded"
string(5) "[0,0]"
Inf and NaN cannot be JSON encoded
nesting
{"a":[[1,2,3],[5,6,7]],"shape":[2,3],"dtype":"uint8","n":[2],"z":2.5}
[
    2,
    3
]
Shape
[2,3,4][][2,3]
array(1) {
  [0]=>
  int(1)
}
array(3) {
  [0]=>
  int(5)
  [1]=>
  int(6)
  [2]=>
  int(7)
}
device memory
[1,2,3,4]
Error: Cannot access the elements of a tensor on device file: its memory is not host accessible; transfer to the host first with to()
Error: Cannot access the elements of a tensor on device file: its memory is not host accessible; transfer to the host first with to()
end
