--TEST--
Tensor::each(): closures declared in a namespace (unqualified and namespaced function names)
--SKIPIF--
<?php if (!extension_loaded('buffers') || !method_exists(Buffers\Tensor::class, 'each')) die('skip built without the extras'); ?>
--FILE--
<?php
namespace App\Imaging;

use Buffers\{DType, Tensor};

function clampish(float $v): float { return $v; }

$a = Tensor::fromArray([-2.5, 0.25, 7.0], DType::Float32);
// unqualified min(), max() and abs() in a namespace resolve to the global functions (frameless calls on PHP 8.4+)
echo json_encode($a->each(fn($v) => max(0.0, min(abs($v), 1.0)))->toArray()), "\n";
echo json_encode($a->each(fn($v) => \max(0.0, \min($v, 1.0)))->toArray()), "\n";

// a namespaced function of the same name is what PHP would call: not one of the supported functions
function round(float $v): float { return $v; }
try { $a->each(fn($v) => round($v)); } catch (\ValueError $e) { echo $e->getMessage(), "\n"; }
try { $a->each(fn($v) => clampish($v)); } catch (\ValueError $e) { echo $e->getMessage(), "\n"; }
echo json_encode($a->each(fn($v) => \round($v))->toArray()), "\n";
--EXPECT--
[1,0.25,1]
[0,0.25,1]
Buffers\Tensor::each(): the namespaced function App\Imaging\round() is not supported in a tensor expression (line 15)
Buffers\Tensor::each(): the namespaced function App\Imaging\clampish() is not supported in a tensor expression (line 16)
[-3,0,7]
