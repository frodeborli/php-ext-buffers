<?php
// Tensor::fromGdImage() and toGdImage() on a 4096 x 4096 image, best of 5 runs (memory_limit must be at least 512M).
// usage: php -d extension=buffers -d extension=gd -d memory_limit=1G benchmarks/gd.php [size]
use Buffers\Tensor;

$n = (int)($argv[1] ?? 4096);
$img = imagecreatetruecolor($n, $n);
imagealphablending($img, false);
for ($y = 0; $y < $n; $y += 64) {
    imagefilledrectangle($img, 0, $y, $n - 1, $y + 63, imagecolorallocatealpha($img, $y & 255, ($y >> 2) & 255, 128, $y % 128));
}
$best = fn(callable $f) => min(array_map(function () use ($f) { $t = hrtime(true); $f(); return (hrtime(true) - $t) / 1e6; }, range(1, 5)));

$t = Tensor::fromGdImage($img);
printf("%d x %d, %.0f MB of pixels\n", $n, $n, $n * $n * 4 / 1048576);
printf("fromGdImage: %7.1f ms\n", $best(fn() => Tensor::fromGdImage($img)));
printf("toGdImage:   %7.1f ms\n", $best(fn() => $t->toGdImage()));
$rgb = $t->buffer->view(Buffers\DType::UInt8, [$n, $n, 3], [$n * 4, 4, 1]);   // strided RGB view of the same pixels
printf("toGdImage (strided RGB view): %7.1f ms\n", $best(fn() => $rgb->toGdImage()));

// the PHP-only alternative, on a 512 x 512 image: imagecolorat() per pixel into a Buffer (measured, then scaled to the full size)
$m = 512;
$small = imagecreatetruecolor($m, $m);
$buf = new Buffers\Buffer($m * $m * 4);
$flat = $buf->view(Buffers\DType::UInt8);
$loop = $best(function () use ($small, $flat, $m) {
    for ($y = 0, $o = 0; $y < $m; $y++) for ($x = 0; $x < $m; $x++, $o += 4) {
        $c = imagecolorat($small, $x, $y);
        $flat[$o] = ($c >> 16) & 255; $flat[$o + 1] = ($c >> 8) & 255; $flat[$o + 2] = $c & 255; $flat[$o + 3] = 255 - (($c >> 24) & 127) * 2;
    }
});
printf("imagecolorat loop, %d x %d: %.1f ms  (x%d for %d x %d = %.0f ms)\n", $m, $m, $loop, ($n / $m) ** 2, $n, $n, $loop * ($n / $m) ** 2);
