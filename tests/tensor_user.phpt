--TEST--
TensorAggregate implemented in PHP: arithmetic through getTensor(), symbolic Shape dimensions
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); ?>
--FILE--
<?php
use Buffers\{DType, Shape, Tensor, TensorAggregate};

final class Thermometer implements TensorAggregate
{
    public int $calls = 0;
    public function __construct(private array $kelvin) {}
    public function getTensor(): Tensor { $this->calls++; return Tensor::fromArray($this->kelvin, DType::Float32); }
    public function getDType(): DType { return DType::Float32; }
    public function getShape(): Shape { return new Shape([count($this->kelvin)]); }
}

final class Throwing implements TensorAggregate
{
    public function getTensor(): Tensor { throw new RuntimeException('sensor offline'); }
    public function getDType(): ?DType { return null; }
    public function getShape(): Shape { return new Shape([]); }
}

$t = new Thermometer([273.15, 300.0, 310.5]);
$celsius = $t - Tensor::fromArray(273.15, DType::Float32);
echo json_encode($celsius), ' ', $celsius->dtype->name, "\n";
echo json_encode(Tensor::fromArray([1.0, 2.0, 3.0], DType::Float32) * $t), "\n";
echo json_encode($t / Tensor::fromArray([2.0], DType::Float64)), ' ', ($t / Tensor::fromArray([2.0], DType::Float64))->dtype->name, "\n";
$sum = Tensor::fromArray([0.0, 0.0, 0.0], DType::Float32);
$sum += $t;
echo json_encode($sum), ' getTensor() calls: ', $t->calls, "\n";

echo "errors\n";
foreach ([
    'no tensor involved' => fn() => $t - 273.15,
    'only aggregates' => fn() => $t + $t,
    'getTensor throws' => fn() => Tensor::fromArray([1.0], DType::Float32) + new Throwing,
    'getTensor throws, left' => fn() => (new Throwing) * Tensor::fromArray([1.0], DType::Float32),
    'shapes' => fn() => $t + Tensor::fromArray([1.0, 2.0], DType::Float32),
] as $name => $f) {
    try { $f(); echo "$name: no error\n"; } catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}

echo "symbolic dimensions\n";
$s = new Shape([2, new Throwing, 4]);
var_dump(count($s), $s->concrete);
try { new Shape([$t]); } catch (Throwable $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
echo "end\n";
?>
--EXPECT--
[0,26.850006103515625,37.350006103515625] Float32
[273.1499938964844,600,931.5]
[136.5749969482422,150,155.25] Float64
[273.1499938964844,300,310.5] getTensor() calls: 5
errors
no tensor involved: TypeError: Unsupported operand types: Thermometer - float
only aggregates: TypeError: Unsupported operand types: Thermometer + Thermometer
getTensor throws: RuntimeException: sensor offline
getTensor throws, left: RuntimeException: sensor offline
shapes: ValueError: Operands could not be broadcast together: shapes [3] and [2]
symbolic dimensions
int(3)
bool(false)
ValueError: Shape dimension 0 must be a 0-d TensorAggregate
end
