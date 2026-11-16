--TEST--
Arithmetic: an operand of a class with arithmetic of its own gets the operation, on either side (decision 5)
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
use Buffers\{DType, Tensor};
$t = Tensor::fromArray([1, 2], DType::Int16);
$o = new BatOperand;
echo $t + $o, "\n", $o - $t, "\n", $t * $o, "\n", $o / $t, "\n", $t % $o, "\n", $t ** $o, "\n";
echo $t << $o, "\n", $o >> $t, "\n", $t & $o, "\n", $o | $t, "\n", $t ^ $o, "\n";
$x = $t;
$x += $o;
echo "compound: ", $x, "\n";
echo "the tensor is unchanged: ", json_encode($t), "\n";
echo "an error between tensors is thrown before the other object is involved: ";
try { echo $t + Tensor::fromArray([1, 2, 3], DType::Int16) + $o, "\n"; } catch (Throwable $e) { echo get_class($e), "\n"; }
echo "end\n";
?>
--EXPECT--
add(view<Int16 2>, BatOperand)
sub(BatOperand, view<Int16 2>)
mul(view<Int16 2>, BatOperand)
div(BatOperand, view<Int16 2>)
mod(view<Int16 2>, BatOperand)
pow(view<Int16 2>, BatOperand)
shl(view<Int16 2>, BatOperand)
shr(BatOperand, view<Int16 2>)
and(view<Int16 2>, BatOperand)
or(BatOperand, view<Int16 2>)
xor(view<Int16 2>, BatOperand)
compound: add(view<Int16 2>, BatOperand)
the tensor is unchanged: [1,2]
an error between tensors is thrown before the other object is involved: ValueError
end
