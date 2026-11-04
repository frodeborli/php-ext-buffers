--TEST--
Tensor::fromGdImage() and toGdImage(): pixels, alpha mapping, palette images, strides, errors
--SKIPIF--
<?php
if (!extension_loaded('buffers')) die('skip buffers extension not loaded');
if (!extension_loaded('gd')) die('skip gd extension not loaded');
if (!method_exists(Buffers\Tensor::class, 'fromGdImage')) die('skip built without the GD integration');
?>
--INI--
memory_limit=512M
--FILE--
<?php
use Buffers\{Tensor, Buffer, DType};

function t(string $label, callable $f): void {
    try { $f(); echo $label, ": ok\n"; }
    catch (Throwable $e) { echo $label, ': ', get_class($e), ': ', $e->getMessage(), "\n"; }
}

// the alpha mapping: gd 0 (opaque) .. 127 (transparent) -> 255 .. 0, and back, for every gd value
$im = imagecreatetruecolor(128, 1);
imagealphablending($im, false);
for ($a = 0; $a < 128; $a++) imagesetpixel($im, $a, 0, imagecolorallocatealpha($im, $a, 255 - $a, 7, $a));
$t = Tensor::fromGdImage($im);
echo $t->dtype->name, ' ', json_encode($t->shape->toArray()), ' ', json_encode($t->strides), "\n";
$alpha = [];
for ($a = 0; $a < 128; $a++) {
    $alpha[] = $t[0][$a][3];
    if ($t[0][$a]->toArray() !== [$a, 255 - $a, 7, (int)round((127 - $a) * 255 / 127)]) echo "pixel $a differs\n";
}
echo $alpha[0], ' ', $alpha[1], ' ', $alpha[63], ' ', $alpha[64], ' ', $alpha[126], ' ', $alpha[127], "\n";
echo count(array_unique($alpha)), " distinct alpha values\n";
$back = $t->toGdImage();
$same = true;
for ($a = 0; $a < 128; $a++) $same = $same && imagecolorat($back, $a, 0) === imagecolorat($im, $a, 0);
var_dump($same);   // gd -> tensor -> gd keeps every pixel
// tensor -> gd -> tensor keeps every alpha that gd can represent; the others move to the nearest of 128 levels
$rgba = new Buffer(256 * 4);
$v = $rgba->view(DType::UInt8, [1, 256, 4]);
for ($a = 0; $a < 256; $a++) { $v[0][$a][3] = $a; }
$round = Tensor::fromGdImage($v->toGdImage());
$maxerr = 0;
for ($a = 0; $a < 256; $a++) $maxerr = max($maxerr, abs($round[0][$a][3] - $a));
echo "max alpha error ", $maxerr, "\n";
echo $round[0][0][3], ' ', $round[0][255][3], "\n";

// 1x1
$one = imagecreatetruecolor(1, 1);
imagesetpixel($one, 0, 0, 0x336699);
$t = Tensor::fromGdImage($one);
echo json_encode($t->shape->toArray()), ' ', json_encode($t->toArray()), "\n";
$g = Tensor::fromArray([[[1, 2, 3, 255]]], DType::UInt8)->toGdImage();
echo imagesx($g), 'x', imagesy($g), ' ', dechex(imagecolorat($g, 0, 0)), ' ', var_export(imageistruecolor($g), true), "\n";

// RGB (opaque), RGBA, grey
$rgb = Tensor::fromArray([[[255, 0, 0], [0, 255, 0], [0, 0, 255]], [[1, 2, 3], [4, 5, 6], [7, 8, 9]]], DType::UInt8);
$g = $rgb->toGdImage();
echo imagesx($g), 'x', imagesy($g), ' ';
for ($y = 0; $y < 2; $y++) for ($x = 0; $x < 3; $x++) echo sprintf('%06x ', imagecolorat($g, $x, $y));
echo "\n";
echo json_encode(Tensor::fromGdImage($g)->toArray()), "\n";
$grey = Tensor::fromArray([[0, 128, 255], [7, 8, 9]], DType::UInt8)->toGdImage();
for ($y = 0; $y < 2; $y++) for ($x = 0; $x < 3; $x++) echo sprintf('%06x ', imagecolorat($grey, $x, $y));
echo "\n";
$rgba = Tensor::fromArray([[[10, 20, 30, 255], [10, 20, 30, 0], [10, 20, 30, 128]]], DType::UInt8)->toGdImage();
for ($x = 0; $x < 3; $x++) echo sprintf('%08x ', imagecolorat($rgba, $x, 0));
echo "\n";
echo json_encode(Tensor::fromGdImage($rgba)->toArray()), "\n";

// palette images: the colors of the palette, with their alpha
$pal = imagecreate(4, 2);
$c0 = imagecolorallocate($pal, 255, 0, 0);
$c1 = imagecolorallocate($pal, 0, 255, 0);
$c2 = imagecolorallocatealpha($pal, 0, 0, 255, 64);
$c3 = imagecolorallocatealpha($pal, 9, 9, 9, 127);
foreach ([[0, 0, $c0], [1, 0, $c1], [2, 0, $c2], [3, 0, $c3], [0, 1, $c3], [1, 1, $c2], [2, 1, $c1], [3, 1, $c0]] as [$x, $y, $c]) imagesetpixel($pal, $x, $y, $c);
var_dump(imageistruecolor($pal));
echo json_encode(Tensor::fromGdImage($pal)->toArray()), "\n";
$tc = imagecreatetruecolor(4, 2); imagealphablending($tc, false); imagecopy($tc, $pal, 0, 0, 0, 0, 4, 2);
var_dump(Tensor::fromGdImage($tc)->toArray() === Tensor::fromGdImage($pal)->toArray());

// strided and offset views: a crop, every second column, a channel stride
$big = Tensor::fromArray([[[1, 1, 1], [2, 2, 2], [3, 3, 3], [4, 4, 4]], [[5, 5, 5], [6, 6, 6], [7, 7, 7], [8, 8, 8]]], DType::UInt8);
$crop = $big->buffer->view(DType::UInt8, [2, 3, 3], [12, 3, 1], 3);   // skips the first column
$g = $crop->toGdImage();
echo imagesx($g), 'x', imagesy($g), ' ', sprintf('%06x %06x', imagecolorat($g, 0, 0), imagecolorat($g, 2, 1)), "\n";
$cols = $big->buffer->view(DType::UInt8, [2, 2, 3], [12, 6, 1]);   // every second column
$g = $cols->toGdImage();
echo imagesx($g), 'x', imagesy($g), ' ', sprintf('%06x %06x %06x %06x', imagecolorat($g, 0, 0), imagecolorat($g, 1, 0), imagecolorat($g, 0, 1), imagecolorat($g, 1, 1)), "\n";
$wide = $big->buffer->view(DType::UInt8, [2, 2, 3], [12, 3, 4]);   // the channels are 4 bytes apart
echo json_encode($wide->toArray()), ' ', sprintf('%06x', imagecolorat($wide->toGdImage(), 1, 1)), "\n";
$grey = $big->buffer->view(DType::UInt8, [2, 4], [12, 3]);          // a strided grey image
echo sprintf('%06x', imagecolorat($grey->toGdImage(), 3, 1)), "\n";

// errors
t('Float32', fn() => Tensor::fromArray([[1.0]], DType::Float32)->toGdImage());
t('Int8', fn() => Tensor::fromArray([[1]], DType::Int8)->toGdImage());
t('1-d', fn() => Tensor::fromArray([1, 2], DType::UInt8)->toGdImage());
t('0-d', fn() => Tensor::fromArray(1, DType::UInt8)->toGdImage());
t('2 channels', fn() => Tensor::fromArray([[[1, 2]]], DType::UInt8)->toGdImage());
t('1 channel axis', fn() => Tensor::fromArray([[[1]]], DType::UInt8)->toGdImage());
t('5 channels', fn() => Tensor::fromArray([[[1, 2, 3, 4, 5]]], DType::UInt8)->toGdImage());
t('4-d', fn() => Tensor::fromArray([[[[1, 2, 3]]]], DType::UInt8)->toGdImage());
t('empty', fn() => Tensor::fromArray([], DType::UInt8)->toGdImage());
t('zero width', fn() => (new Buffer(0))->view(DType::UInt8, [2, 0, 3])->toGdImage());
t('zero height', fn() => (new Buffer(0))->view(DType::UInt8, [0, 2])->toGdImage());
t('Bool', fn() => Tensor::fromArray([[true]])->toGdImage());
t('not an image', fn() => Tensor::fromGdImage(new stdClass));
t('not an object', fn() => Tensor::fromGdImage(1));
t('read-only buffer is fine', fn() => Buffer::fromString("\1\2\3", false)->view(DType::UInt8, [1, 1, 3])->toGdImage());
t('no arguments', fn() => Tensor::fromGdImage());

// a large image: 2048 x 2048, both ways
$n = 2048;
$img = imagecreatetruecolor($n, $n);
imagealphablending($img, false);
imagefilledrectangle($img, 0, 0, $n - 1, $n - 1, imagecolorallocatealpha($img, 1, 2, 3, 0));
imagesetpixel($img, $n - 1, $n - 1, imagecolorallocatealpha($img, 200, 100, 50, 127));
imagesetpixel($img, 5, 1000, imagecolorallocatealpha($img, 9, 8, 7, 0));
$t = Tensor::fromGdImage($img);
echo json_encode($t->shape->toArray()), ' ', $t->buffer->byteLength, "\n";
echo json_encode([$t[0][0]->toArray(), $t[1000][5]->toArray(), $t[$n - 1][$n - 1]->toArray()]), "\n";
$back = $t->toGdImage();
var_dump(imagesx($back) === $n && imagesy($back) === $n);
$same = true;
foreach ([[0, 0], [5, 1000], [$n - 1, $n - 1], [100, 100], [$n - 1, 0], [0, $n - 1]] as [$x, $y]) $same = $same && imagecolorat($back, $x, $y) === imagecolorat($img, $x, $y);
var_dump($same);
?>
--EXPECT--
UInt8 [1,128,4] [512,4,1]
255 253 129 126 2 0
128 distinct alpha values
bool(true)
max alpha error 1
0 255
[1,1,4] [[[51,102,153,255]]]
1x1 10203 true
3x2 ff0000 00ff00 0000ff 010203 040506 070809 
[[[255,0,0,255],[0,255,0,255],[0,0,255,255]],[[1,2,3,255],[4,5,6,255],[7,8,9,255]]]
000000 808080 ffffff 070707 080808 090909 
000a141e 7f0a141e 3f0a141e 
[[[10,20,30,255],[10,20,30,0],[10,20,30,129]]]
bool(false)
[[[255,0,0,255],[0,255,0,255],[0,0,255,126],[9,9,9,0]],[[9,9,9,0],[0,0,255,126],[0,255,0,255],[255,0,0,255]]]
bool(true)
3x2 020202 080808
2x2 010101 030303 050505 070707
[[[1,2,3],[2,3,4]],[[5,6,7],[6,7,8]]] 060708
080808
Float32: Error: Tensor::toGdImage() needs a UInt8 tensor, this one is Float32
Int8: Error: Tensor::toGdImage() needs a UInt8 tensor, this one is Int8
1-d: Error: Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]
0-d: Error: Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]
2 channels: Error: Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]
1 channel axis: Error: Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]
5 channels: Error: Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]
4-d: Error: Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]
empty: Error: Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]
zero width: Error: Tensor::toGdImage(): the height and width must be between 1 and 2147483647
zero height: Error: Tensor::toGdImage(): the height and width must be between 1 and 2147483647
Bool: Error: Tensor::toGdImage() needs a UInt8 tensor, this one is Bool
not an image: TypeError: Buffers\Tensor::fromGdImage(): Argument #1 ($image) must be of type GdImage, stdClass given
not an object: TypeError: Buffers\Tensor::fromGdImage(): Argument #1 ($image) must be of type GdImage, int given
read-only buffer is fine: ok
no arguments: ArgumentCountError: Buffers\Tensor::fromGdImage() expects exactly 1 argument, 0 given
[2048,2048,4] 16777216
[[1,2,3,255],[9,8,7,255],[200,100,50,0]]
bool(true)
bool(true)
