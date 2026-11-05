--TEST--
Tensor::fromGdImage() into an existing tensor; contiguous, unaligned and strided destinations; errors
--SKIPIF--
<?php
if (!extension_loaded('buffers')) die('skip buffers extension not loaded');
if (!extension_loaded('gd')) die('skip gd extension not loaded');
if (!method_exists(Buffers\Tensor::class, 'fromGdImage')) die('skip built without the GD integration');
?>
--FILE--
<?php
use Buffers\{Tensor, Buffer, DType};

function t(string $label, callable $f): void {
    try { $f(); echo $label, ": ok\n"; }
    catch (Throwable $e) { echo $label, ': ', get_class($e), ': ', $e->getMessage(), "\n"; }
}
function image(int $w, int $h, int $seed): GdImage {
    $im = imagecreatetruecolor($w, $h);
    imagealphablending($im, false);
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) {
        imagesetpixel($im, $x, $y, imagecolorallocatealpha($im, ($x * 7 + $seed) & 255, ($y * 13 + $seed) & 255, ($x + $y + $seed) & 255, ($x * 3 + $y + $seed) % 128));
    }
    return $im;
}

// reuse in a loop: the same tensor and buffer every time, the result equals a fresh conversion
$dst = (new Buffer(5 * 3 * 4))->view(DType::UInt8, [3, 5, 4]);
$ok = true;
for ($i = 0; $i < 4; $i++) {
    $im = image(5, 3, $i * 40);
    $r = Tensor::fromGdImage($im, $dst);
    $ok = $ok && $r === $dst && $dst->toArray() === Tensor::fromGdImage($im)->toArray();
}
var_dump($ok);
var_dump(Tensor::fromGdImage(image(5, 3, 1), null)->toArray() === Tensor::fromGdImage(image(5, 3, 1))->toArray());

// a palette image into a tensor
$pal = imagecreate(2, 1);
imagecolorallocate($pal, 1, 2, 3);
imagesetpixel($pal, 1, 0, imagecolorallocatealpha($pal, 9, 8, 7, 127));
echo json_encode(Tensor::fromGdImage($pal, (new Buffer(8))->view(DType::UInt8, [1, 2, 4]))->toArray()), "\n";

// a destination with an unaligned start (contiguous, but not 4-byte aligned)
$im = image(6, 4, 9);
$want = Tensor::fromGdImage($im)->toArray();
$buf = new Buffer(6 * 4 * 4 + 1);
$un = $buf->view(DType::UInt8, [4, 6, 4], null, 1);
Tensor::fromGdImage($im, $un);
var_dump($un->toArray() === $want, ord($buf->toString()[0]));

// a strided destination: rows with padding, and a crop of a bigger image (the other pixels stay untouched)
$big = (new Buffer(10 * 8 * 4))->view(DType::UInt8, [8, 10, 4]);
for ($y = 0; $y < 8; $y++) for ($x = 0; $x < 10; $x++) $big[$y][$x][0] = 77;
$crop = $big->buffer->view(DType::UInt8, [4, 6, 4], [40, 4, 1], (2 * 10 + 3) * 4);
Tensor::fromGdImage($im, $crop);
var_dump($crop->toArray() === $want);
$touched = 0; $kept = 0;
for ($y = 0; $y < 8; $y++) for ($x = 0; $x < 10; $x++) {
    $inside = $y >= 2 && $y < 6 && $x >= 3 && $x < 9;
    if ($inside) { $touched += $big[$y][$x]->toArray() === $want[$y - 2][$x - 3] ? 1 : 0; }
    else { $kept += $big[$y][$x]->toArray() === [77, 0, 0, 0] ? 1 : 0; }
}
echo "$touched $kept\n";
// pixel and channel strides that are not 4 and 1: the channels are 2 bytes apart, the pixels 8
$wide = (new Buffer(6 * 4 * 8))->view(DType::UInt8, [4, 6, 4], [48, 8, 2]);
Tensor::fromGdImage($im, $wide);
var_dump($wide->toArray() === $want);

// errors
$im = image(5, 3, 0);
t('wrong width', fn() => Tensor::fromGdImage($im, (new Buffer(3 * 6 * 4))->view(DType::UInt8, [3, 6, 4])));
t('transposed', fn() => Tensor::fromGdImage($im, (new Buffer(60))->view(DType::UInt8, [5, 3, 4])));
t('3 channels', fn() => Tensor::fromGdImage($im, (new Buffer(45))->view(DType::UInt8, [3, 5, 3])));
t('2-d', fn() => Tensor::fromGdImage($im, (new Buffer(60))->view(DType::UInt8, [15, 4])));
t('Int8', fn() => Tensor::fromGdImage($im, (new Buffer(60))->view(DType::Int8, [3, 5, 4])));
t('Float32', fn() => Tensor::fromGdImage($im, (new Buffer(240))->view(DType::Float32, [3, 5, 4])));
t('read-only', fn() => Tensor::fromGdImage($im, (new Buffer(60))->readOnly()->view(DType::UInt8, [3, 5, 4])));
t('not a tensor', fn() => Tensor::fromGdImage($im, new stdClass));
// memory the CPU cannot address: an unmapped file buffer
$f = tempnam(sys_get_temp_dir(), 'bufgd');
file_put_contents($f, str_repeat("\0", 60));
$fb = Buffer::fromFile($f, 0, null, false, false)->view(DType::UInt8, [3, 5, 4]);
t('not host accessible', fn() => Tensor::fromGdImage($im, $fb));
unlink($f);
// nothing was written by any failed call
$z = (new Buffer(60))->view(DType::UInt8, [3, 5, 4]);
t('wrong shape leaves the data alone', fn() => Tensor::fromGdImage($im, (new Buffer(60))->view(DType::UInt8, [5, 3, 4])));
echo array_sum(array_map('array_sum', array_map(fn($r) => array_map('array_sum', $r), $z->toArray()))), "\n";
?>
--EXPECT--
bool(true)
bool(true)
[[[1,2,3,255],[9,8,7,0]]]
bool(true)
int(0)
bool(true)
24 56
bool(true)
wrong width: ValueError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be a UInt8 tensor of shape [3, 5, 4], got UInt8 with shape [3, 6, 4]
transposed: ValueError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be a UInt8 tensor of shape [3, 5, 4], got UInt8 with shape [5, 3, 4]
3 channels: ValueError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be a UInt8 tensor of shape [3, 5, 4], got UInt8 with shape [3, 5, 3]
2-d: ValueError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be a UInt8 tensor of shape [3, 5, 4], got UInt8 with shape [15, 4]
Int8: ValueError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be a UInt8 tensor of shape [3, 5, 4], got Int8 with shape [3, 5, 4]
Float32: ValueError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be a UInt8 tensor of shape [3, 5, 4], got Float32 with shape [3, 5, 4]
read-only: Error: Cannot write to a read-only Tensor
not a tensor: TypeError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be of type ?Buffers\Tensor, stdClass given
not host accessible: Error: Cannot access the elements of a tensor on device file: its memory is not host accessible; transfer to the host first with to()
wrong shape leaves the data alone: ValueError: Buffers\Tensor::fromGdImage(): Argument #2 ($into) must be a UInt8 tensor of shape [3, 5, 4], got UInt8 with shape [5, 3, 4]
0
