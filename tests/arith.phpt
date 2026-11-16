--TEST--
Arithmetic: operators, broadcasting, layouts, rebinding, errors
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, Tensor};
function show(Tensor $t): string { return json_encode($t) . ' ' . $t->dtype->name . ' [' . implode(',', $t->shape->toArray()) . ']'; }

$a = Tensor::fromArray([[1, 2, 3], [4, 5, 6]], DType::Int32);
$b = Tensor::fromArray([10, 20, 30], DType::Int32);
echo "operators\n";
echo show($a + $b), "\n", show($a - 1), "\n", show(1 - $a), "\n", show($a * 2), "\n", show($a / 2), "\n";
echo show($a % -4), "\n", show(-7 % $b), "\n", show($a ** 2), "\n", show($a << 3), "\n", show($b >> 2), "\n";
echo show($a & 6), "\n", show($a | 8), "\n", show($a ^ $a), "\n", show(~$a), "\n", show(-$a), "\n", show(+$a), "\n";

echo "promotion\n";
$u8 = Tensor::fromArray([250, 5], DType::UInt8);
$i8 = Tensor::fromArray([-128, 127], DType::Int8);
echo show($u8 + 10), "\n", show($u8 + $i8), "\n", show($u8 * 1.5), "\n", show($i8 / $i8), "\n";
$f16 = Tensor::fromArray([0.1, 1.5, -2.25], DType::Float16);
echo show($f16 * 3), "\n", show($f16 + Tensor::fromArray([1.0, 2.0, 3.0], DType::Float32)), "\n";
echo show(Tensor::fromArray([true, false, true]) ^ Tensor::fromArray([true, true, false])), "\n", show(~Tensor::fromArray([true, false])), "\n";

echo "broadcasting\n";
echo show(Tensor::fromArray([[1], [2]], DType::Int16) * Tensor::fromArray([1, 10, 100], DType::Int16)), "\n";
echo show(Tensor::fromArray(3, DType::Int16) + Tensor::fromArray([[1, 2]], DType::Int16)), "\n";
echo show(Tensor::fromArray(3, DType::Int16) * 2), "\n";
echo show(Tensor::fromArray([[]], DType::Float32) + 1), "\n";

echo "layouts: strided, offset, transposed, read-only operands\n";
$buf = Buffer::fromString(pack('V*', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11), false);
$every2 = $buf->view(DType::Int32, 6, [2]);
$offset = $buf->view(DType::Int32, 4, null, 8);
$t = $buf->view(DType::Int32, [4, 3], [1, 4]);   // the transpose of a [3, 4] matrix
echo show($every2 * 10), "\n", show($offset + $every2[0]), "\n", show($t + 0), "\n";
echo show($buf->view(DType::Int32, [3, 4])[1] - $offset), "\n";

echo "rebinding: other references and views keep the old values\n";
$x = Tensor::fromArray([1, 2, 3], DType::Int8);
$keep = $x;
$row = $x->buffer->view(DType::Int8, 3);
$x += 1;
$x++;
$x *= $x;
echo show($x), " | ", show($keep), " | ", show($row), "\n";

echo "comparisons stay identity\n";
var_dump($a == $a, $a == $b, $a === $b);

echo "errors\n";
$cases = [
    'negative to unsigned' => fn() => $u8 * -1,
    'unsigned negation' => fn() => -$u8,
    'scalar out of range' => fn() => $i8 + 300,
    'modulo by zero' => fn() => $a % 0,
    'modulo by zero element' => fn() => $a % Tensor::fromArray([1, 0, 2], DType::Int32),
    'negative exponent' => fn() => $a ** -1,
    'negative shift' => fn() => $a << -1,
    'shapes' => fn() => $a + Tensor::fromArray([1, 2], DType::Int32),
    'f16 + bf16' => fn() => Tensor::fromArray([1.0], DType::Float16) + Tensor::fromArray([1.0], DType::BFloat16),
    'u64 + i8' => fn() => Tensor::fromArray([1], DType::UInt64) + $i8,
    'bool arithmetic' => fn() => Tensor::fromArray([true]) + Tensor::fromArray([true]),
    'bool and int' => fn() => Tensor::fromArray([true]) & 1,
    'float shift' => fn() => $f16 << 1,
    'float and' => fn() => $f16 & $f16,
    'float ~' => fn() => ~$f16,
    'storage-only' => fn() => Tensor::fromArray([1.0], DType::Float8E4M3FN) + 1,
    'packed' => fn() => (new Buffer(1))->view(DType::Int4, 2) + 1,
    'complex' => fn() => (new Buffer(8))->view(DType::Complex64, 1) * 2,
    'string' => fn() => $a + '5',
    'null' => fn() => $a * null,
    'array' => fn() => $a + [1],
    'object' => fn() => $a + new stdClass,
    'concat' => fn() => $a . 'x',
];
foreach ($cases as $name => $f) {
    try { $f(); echo "$name: no error\n"; } catch (Throwable $e) { printf("%s: %s: %s\n", $name, get_class($e), $e->getMessage()); }
}
$z = Tensor::fromArray([1, 2], DType::Int8);
try { $z += 1000; } catch (ValueError $e) { echo "a failed += leaves the variable: ", show($z), "\n"; }
try { $z %= 0; } catch (DivisionByZeroError $e) { echo "a failed %= leaves the variable: ", show($z), "\n"; }
echo "end\n";
?>
--EXPECT--
operators
[[11,22,33],[14,25,36]] Int32 [2,3]
[[0,1,2],[3,4,5]] Int32 [2,3]
[[0,-1,-2],[-3,-4,-5]] Int32 [2,3]
[[2,4,6],[8,10,12]] Int32 [2,3]
[[0.5,1,1.5],[2,2.5,3]] Float64 [2,3]
[[1,2,3],[0,1,2]] Int32 [2,3]
[-7,-7,-7] Int32 [3]
[[1,4,9],[16,25,36]] Int32 [2,3]
[[8,16,24],[32,40,48]] Int32 [2,3]
[2,5,7] Int32 [3]
[[0,2,2],[4,4,6]] Int32 [2,3]
[[9,10,11],[12,13,14]] Int32 [2,3]
[[0,0,0],[0,0,0]] Int32 [2,3]
[[-2,-3,-4],[-5,-6,-7]] Int32 [2,3]
[[-1,-2,-3],[-4,-5,-6]] Int32 [2,3]
[[1,2,3],[4,5,6]] Int32 [2,3]
promotion
[4,15] UInt8 [2]
[122,132] Int16 [2]
[375,7.5] Float64 [2]
[1,1] Float64 [2]
[0.2998046875,4.5,-6.75] Float16 [3]
[1.0999755859375,3.5,0.75] Float32 [3]
[false,true,true] Bool [3]
[false,true] Bool [2]
broadcasting
[[1,10,100],[2,20,200]] Int16 [2,3]
[[4,5]] Int16 [1,2]
6 Int16 []
[[]] Float32 [1,0]
layouts: strided, offset, transposed, read-only operands
[0,20,40,60,80,100] Int32 [6]
[2,3,4,5] Int32 [4]
[[0,4,8],[1,5,9],[2,6,10],[3,7,11]] Int32 [4,3]
[2,2,2,2] Int32 [4]
rebinding: other references and views keep the old values
[9,16,25] Int8 [3] | [1,2,3] Int8 [3] | [1,2,3] Int8 [3]
comparisons stay identity
bool(true)
bool(false)
bool(false)
errors
negative to unsigned: ValueError: Operand -1 does not fit the dtype UInt8 of the other operand
unsigned negation: ValueError: Operand -1 does not fit the dtype UInt8 of the other operand
scalar out of range: ValueError: Operand 300 does not fit the dtype Int8 of the other operand
modulo by zero: DivisionByZeroError: Modulo by zero
modulo by zero element: DivisionByZeroError: Modulo by zero
negative exponent: ValueError: Negative exponent in an integer power; convert() to a float dtype first
negative shift: ArithmeticError: Bit shift by negative number
shapes: ValueError: Operands could not be broadcast together: shapes [2, 3] and [2]
f16 + bf16: TypeError: Unsupported operand dtypes for +: Float16 and BFloat16 have no common dtype; convert() one of them
u64 + i8: TypeError: Unsupported operand dtypes for +: UInt64 and Int8 have no common dtype; convert() one of them
bool arithmetic: TypeError: Unsupported operand dtypes for +: Bool and Bool (Bool supports only & | ^ ~; convert() to an integer dtype for arithmetic)
bool and int: TypeError: Unsupported operand dtypes for &: Bool and int (Bool combines only with Bool; convert() to an integer dtype)
float shift: TypeError: Unsupported operand dtypes for <<: Float16 and int (bitwise operators and shifts need integer operands)
float and: TypeError: Unsupported operand dtypes for &: Float16 and Float16 (bitwise operators and shifts need integer operands)
float ~: TypeError: Unsupported operand dtype for ~: Float16 (integer and Bool tensors only)
storage-only: TypeError: Unsupported operand dtype for +: Float8E4M3FN has no arithmetic; convert() it to a numeric dtype first
packed: TypeError: Unsupported operand dtype for +: Int4 has no arithmetic; convert() it to a numeric dtype first
complex: TypeError: Unsupported operand dtype for *: Complex64 has no arithmetic; convert() it to a numeric dtype first
string: TypeError: Unsupported operand types: Buffers\Tensor + string
null: TypeError: Unsupported operand types: Buffers\Tensor * null
array: TypeError: Unsupported operand types: Buffers\Tensor + array
object: TypeError: Unsupported operand types: Buffers\Tensor + stdClass
concat: Error: Object of class Buffers\Tensor could not be converted to string
a failed += leaves the variable: [1,2] Int8 [2]
a failed %= leaves the variable: [1,2] Int8 [2]
end
