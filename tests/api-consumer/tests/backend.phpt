--TEST--
C API: an arithmetic backend computes operators on its device; host operations fall back to ext-buffers
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{CpuDevice, DType, Tensor};
$dev = bat_device_buffer(1)->device;
$a = Tensor::fromArray([[1.5, 2.5], [3.5, 4.5]], DType::Float32, $dev);
$b = Tensor::fromArray([[0.5, 0.5], [1.0, 2.0]], DType::Float32, $dev);
$before = bat_counts();
$c = $a + $b;
$d = $a - $b;
echo $c->buffer->device->name, ' ', $c->dtype->name, ' ', json_encode($c->shape), ' ', json_encode($c->to(CpuDevice::get())), "\n";
echo json_encode($d->to(CpuDevice::get())), "\n";
$after = bat_counts();
echo 'backend runs: ', $after['backend_runs'] - $before['backend_runs'], "\n";

echo "host operations are asked about and fall back\n";
$asks = bat_counts()['backend_asks'];
$h = Tensor::fromArray([1.0, 2.0], DType::Float32) + 1;
echo json_encode($h), ' asked: ', bat_counts()['backend_asks'] - $asks, ', runs: ', bat_counts()['backend_runs'] - $after['backend_runs'], "\n";

echo "what the backend declines\n";
foreach ([
    'multiplication' => fn() => $a * $b,
    'a scalar' => fn() => $a + 1.0,
    'mixed devices' => fn() => $a + Tensor::fromArray([[1.0, 1.0], [1.0, 1.0]], DType::Float32),
    'broadcast' => fn() => $a + Tensor::fromArray([1.0, 2.0], DType::Float32, $dev),
    'checks first' => fn() => $a + Tensor::fromArray([1.0, 2.0, 3.0], DType::Float32, $dev),
    'promotion first' => fn() => Tensor::fromArray([1.0], DType::Float16, $dev) + Tensor::fromArray([1.0], DType::BFloat16, $dev),
] as $name => $f) {
    try { $f(); echo "$name: no error\n"; } catch (Throwable $e) { echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n"; }
}
echo "end\n";
?>
--EXPECT--
bat:5 Float32 [2,2] [[2,3],[4.5,6.5]]
[[1,2],[2.5,2.5]]
backend runs: 2
host operations are asked about and fall back
[2,3] asked: 1, runs: 0
what the backend declines
multiplication: Error: Cannot compute * on device bat:5: no arithmetic backend for it is registered; move the operands to the host with to()
a scalar: Error: Cannot compute + on device bat:5: no arithmetic backend for it is registered; move the operands to the host with to()
mixed devices: Error: Cannot compute + on operands on different devices (bat:5 and cpu); move one with to()
broadcast: Error: Cannot compute + on device bat:5: no arithmetic backend for it is registered; move the operands to the host with to()
checks first: ValueError: Operands could not be broadcast together: shapes [2, 2] and [3]
promotion first: TypeError: Unsupported operand dtypes for +: Float16 and BFloat16 have no common dtype; convert() one of them
end
