--TEST--
Buffer::createFile(), writable mappings, flush(), writeTo() and readFrom()
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_OS_FAMILY === 'Windows') die('skip POSIX paths'); ?>
--FILE--
<?php
use Buffers\{Buffer, DType, FileDevice};
$path = tempnam(sys_get_temp_dir(), 'buf');

echo "createFile, write, flush, reopen\n";
$b = Buffer::createFile($path, 64);
var_dump($b->isWritable(), $b->isHostAccessible(), $b->byteLength, $b->device === FileDevice::get(), filesize($path));
$f = $b->view(DType::Float32, 4);
foreach ([1.5, -2.5, 3.5, 4.5] as $i => $x) $f[$i] = $x;
$p = $b->view(DType::Int4, 8, null, 32);
for ($i = 0; $i < 8; $i++) $p[$i] = $i - 4;
$b->flush();
$bytes = file_get_contents($path);
var_dump(strlen($bytes), $bytes === $b->toString());
unset($f, $p, $b);
$r = Buffer::fromFile($path);
var_dump($r->isWritable(), iterator_to_array($r->view(DType::Float32, 4)), iterator_to_array($r->view(DType::Int4, 8, null, 32)));
try { $r->view(DType::Float32, 1)[0] = 1.0; } catch (Error $e) { echo $e->getMessage(), "\n"; }
$r->flush();   // nothing to do
(new Buffer(4))->flush();
echo "createFile truncates, and sizes zero, one and many bytes\n";
foreach ([0, 1, 10000] as $n) { $z = Buffer::createFile($path, $n); var_dump($z->byteLength, filesize($path), $z->isHostAccessible(), $z->isWritable()); }
unset($z);
echo "a writable mapping of an existing file, with an offset and a length\n";
file_put_contents($path, str_repeat("\x00", 100) . "tail");
$w = Buffer::fromFile($path, 50, 30, true, true);
var_dump($w->isWritable(), $w->byteLength);
$u = $w->view(DType::UInt8, 30); $u[0] = 65; $u[29] = 66;
$w->flush();
$all = file_get_contents($path);
var_dump(strlen($all), $all[50], $all[79], $all[49] === "\0", $all[80] === "\0", substr($all, -4));
echo "errors\n";
foreach ([fn() => Buffer::fromFile($path, 0, null, false, true), fn() => Buffer::fromFile($path . '.none', 0, null, true, true), fn() => Buffer::createFile('/nonexistent-dir/x', 8), fn() => Buffer::createFile($path, -1), fn() => Buffer::fromFile($path, 0, 1000, true, true)] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', preg_replace('~/[^ \']*/buf[^ \']*|/nonexistent-dir/x~', 'PATH', $e->getMessage()), "\n"; }
}
unset($w, $u);

echo "writeTo and readFrom with php://memory\n";
$b = Buffer::fromString("0123456789abcdef");
$m = fopen('php://memory', 'w+');
var_dump($b->writeTo($m), $b->writeTo($m, 4, 4), $b->writeTo($m, 14), $b->writeTo($m, 16), $b->writeTo($m, 3, 0));
rewind($m); var_dump(stream_get_contents($m));
rewind($m); ftruncate($m, 0); fwrite($m, "ABCDEFGH"); rewind($m);
var_dump($b->readFrom($m, 2, 3), $b->toString());                 // reads three bytes at offset 2
var_dump($b->readFrom($m, 10), $b->toString());                   // the rest: five bytes, then EOF
var_dump($b->readFrom($m), $b->readFrom($m, 0, 4));               // at EOF: nothing
rewind($m);
var_dump($b->readFrom($m, 12), $b->toString());                   // a short stream into a long range: stops at the end of the buffer
fclose($m);
echo "argument errors\n";
$m = fopen('php://memory', 'w+'); fwrite($m, "xyz"); rewind($m);
$ro = Buffer::fromFile(__FILE__, 0, 16);
foreach ([fn() => $b->writeTo($m, 17), fn() => $b->writeTo($m, -1), fn() => $b->writeTo($m, 10, 7), fn() => $b->writeTo($m, 0, -1), fn() => $b->readFrom($m, 17), fn() => $b->readFrom($m, 10, 7),
          fn() => $ro->readFrom($m), fn() => $ro->writeTo(fopen('php://memory', 'w')), fn() => $b->writeTo('not a stream'),
          fn() => Buffer::fromFile(__FILE__, 0, null, false)->writeTo(fopen('php://memory', 'w')), fn() => Buffer::fromFile(__FILE__, 0, null, false)->readFrom($m)] as $f) {
    try { $f(); echo "no error\n"; } catch (Throwable $e) { echo get_class($e), ': ', preg_replace('~on device [^:]*:~', 'on device X:', $e->getMessage()), "\n"; }
}
echo "a file\n";
$out = fopen($path, 'w+');
$big = new Buffer(100000); $v = $big->view(DType::UInt8, 100000);
for ($i = 0; $i < 100000; $i++) $v[$i] = $i % 251;
var_dump($big->writeTo($out), $big->writeTo($out, 99990, 10), ftell($out), filesize($path));
rewind($out);
$back = new Buffer(100010);
var_dump($back->readFrom($out), $back->toString() === $big->toString() . substr($big->toString(), 99990));
fclose($out);
echo "a mapped file buffer is a source too\n";
$mapped = Buffer::fromFile($path, 10, 1000);
$sink = fopen('php://memory', 'w+');
var_dump($mapped->writeTo($sink, 500, 100)); rewind($sink); var_dump(stream_get_contents($sink) === substr($big->toString(), 510, 100));
echo "a socket pair\n";
[$a, $c] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, STREAM_IPPROTO_IP);
$msg = Buffer::fromString(str_repeat("socket!", 1000));
var_dump($msg->writeTo($a, 7, 7000 - 7));
stream_set_blocking($c, false);
$got = '';
while (strlen($got) < 6993) { $x = fread($c, 8192); if ($x === '' || $x === false) break; $got .= $x; }
var_dump($got === substr($msg->toString(), 7), strlen($got));
stream_set_blocking($c, true);
$dst = new Buffer(20);
fwrite($a, "hello, buffer"); fclose($a);
var_dump($dst->readFrom($c, 5), bin2hex($dst->toString()));
echo "short writes: a non-blocking socket takes only what fits\n";
[$a, $c] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, STREAM_IPPROTO_IP);
stream_set_blocking($a, false);
$huge = new Buffer(8 * 1024 * 1024);
$n = $huge->writeTo($a);
var_dump($n > 0, $n < 8 * 1024 * 1024);
$drained = 0; stream_set_blocking($c, false);
while (($x = fread($c, 1 << 20)) !== '' && $x !== false) $drained += strlen($x);
var_dump($drained === $n);
unlink($path);
echo "end\n";
--EXPECT--
createFile, write, flush, reopen
bool(true)
bool(true)
int(64)
bool(true)
int(64)
int(64)
bool(true)
bool(false)
array(4) {
  [0]=>
  float(1.5)
  [1]=>
  float(-2.5)
  [2]=>
  float(3.5)
  [3]=>
  float(4.5)
}
array(8) {
  [0]=>
  int(-4)
  [1]=>
  int(-3)
  [2]=>
  int(-2)
  [3]=>
  int(-1)
  [4]=>
  int(0)
  [5]=>
  int(1)
  [6]=>
  int(2)
  [7]=>
  int(3)
}
Cannot write to a read-only Tensor
createFile truncates, and sizes zero, one and many bytes
int(0)
int(0)
bool(true)
bool(true)
int(1)
int(0)
bool(true)
bool(true)
int(10000)
int(0)
bool(true)
bool(true)
a writable mapping of an existing file, with an offset and a length
bool(true)
int(30)
int(104)
string(1) "A"
string(1) "B"
bool(true)
bool(true)
string(4) "tail"
errors
ValueError: Buffers\Buffer::fromFile(): Argument #5 ($writable) cannot be true with $map false: writable file buffers must be mapped
Error: Cannot open 'PATH': No such file or directory
Error: Cannot open 'PATH': No such file or directory
ValueError: Buffers\Buffer::createFile(): Argument #2 ($byteLength) must be greater than or equal to 0
ValueError: Buffers\Buffer::fromFile(): Argument #3 ($length) extends beyond the end of the file (104 bytes, from offset 0)
writeTo and readFrom with php://memory
int(16)
int(4)
int(2)
int(0)
int(0)
string(22) "0123456789abcdef4567ef"
int(3)
string(16) "01ABC56789abcdef"
int(5)
string(16) "01ABC56789DEFGHf"
int(0)
int(0)
int(4)
string(16) "01ABC56789DEABCD"
argument errors
ValueError: Buffers\Buffer::writeTo(): Argument #2 ($offset) must be between 0 and the byte length of the buffer
ValueError: Buffers\Buffer::writeTo(): Argument #2 ($offset) must be between 0 and the byte length of the buffer
ValueError: Buffers\Buffer::writeTo(): Argument #3 ($length) must be between 0 and the bytes left after the offset (6)
ValueError: Buffers\Buffer::writeTo(): Argument #3 ($length) must be between 0 and the bytes left after the offset (16)
ValueError: Buffers\Buffer::readFrom(): Argument #2 ($offset) must be between 0 and the byte length of the buffer
ValueError: Buffers\Buffer::readFrom(): Argument #3 ($length) must be between 0 and the bytes left after the offset (6)
Error: Cannot write to a read-only Buffer
no error
TypeError: Buffers\Buffer::writeTo(): Argument #1 ($stream) must be of type resource, string given
Error: Cannot access the bytes of a buffer on device X: its memory is not host accessible; transfer to the host first with Tensor::to()
Error: Cannot access the bytes of a buffer on device X: its memory is not host accessible; transfer to the host first with Tensor::to()
a file
int(100000)
int(10)
int(100010)
int(100010)
int(100010)
bool(true)
a mapped file buffer is a source too
int(100)
bool(true)
a socket pair
int(6993)
bool(true)
int(6993)
int(13)
string(40) "000000000068656c6c6f2c206275666665720000"
short writes: a non-blocking socket takes only what fits
bool(true)
bool(true)
bool(true)
end
