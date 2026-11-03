--TEST--
Shared mappings: two processes see each other's writes
--SKIPIF--
<?php
if (!extension_loaded('buffers')) die('skip buffers extension not loaded');
if (PHP_OS_FAMILY !== 'Linux' || !is_readable('/proc/self/cmdline') || !function_exists('proc_open')) die('skip needs Linux and proc_open');
?>
--FILE--
<?php
use Buffers\{Buffer, DType};

/* the child gets the same extensions as this process: -d extension=... arguments of our own command line */
$args = explode("\0", rtrim(file_get_contents('/proc/self/cmdline'), "\0"));
$ext = [];
for ($i = 0; $i < count($args) - 1; $i++) if ($args[$i] === '-d' && str_starts_with($args[$i + 1], 'extension=')) array_push($ext, '-d', $args[$i + 1]);
$dir = is_dir('/dev/shm') && is_writable('/dev/shm') ? '/dev/shm' : sys_get_temp_dir();
$path = "$dir/buffers-shared-" . getmypid();
$child = tempnam(sys_get_temp_dir(), 'child');
file_put_contents($child, <<<'PHP'
<?php
use Buffers\{Buffer, DType};
[$path, $mode] = [$argv[1], $argv[2]];
$buf = Buffer::fromFile($path, 0, null, true, $mode === 'rw');
$t = $buf->view(DType::Int32, 1024);
echo "child sees ", $t[0], "\n";
if ($mode === 'rw') { $t[1] = 222; $buf->view(DType::Float64, 1, null, 16)[0] = 2.5; $buf->view(DType::Int4, 2, null, 32)[0] = -3; }
echo "ready\n";
fflush(STDOUT);
for ($i = 0; $i < 500 && $t[2] !== 333; $i++) usleep(10000);   // waits for the parent's write
echo "child saw ", $t[2], "\n";
if ($mode === 'rw') $t[3] = 444;
PHP);
function run(array $cmd, callable $between): string {
    $p = proc_open($cmd, [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes);
    $out = fgets($pipes[1]) . fgets($pipes[1]);     // "child sees N" and "ready"
    $between();
    $out .= stream_get_contents($pipes[1]);
    $err = stream_get_contents($pipes[2]);
    proc_close($p);
    return $out . $err;
}

$buf = Buffer::createFile($path, 4096);
var_dump($buf->isWritable(), $buf->isHostAccessible(), $buf->byteLength, $buf->device->name);
$t = $buf->view(DType::Int32, 1024);
$t[0] = 111;
echo "a writable mapping: the child writes, the parent sees it\n";
echo run([PHP_BINARY, '-n', ...$ext, $child, $path, 'rw'], function () use ($t) { $t[2] = 333; });
var_dump($t[1], $buf->view(DType::Float64, 1, null, 16)[0], $buf->view(DType::Int4, 2, null, 32)[0], $t[3]);
echo "a read-only mapping of the same file in the child sees the parent's later writes at once\n";
$t[2] = 0;
echo run([PHP_BINARY, '-n', ...$ext, $child, $path, 'ro'], function () use ($t) { $t[2] = 333; });
$buf->flush();
var_dump(unpack('l', file_get_contents($path, false, null, 4, 4))[1] === 222);
unlink($path); unlink($child);
echo "end\n";
--EXPECT--
bool(true)
bool(true)
int(4096)
string(4) "file"
a writable mapping: the child writes, the parent sees it
child sees 111
ready
child saw 333
int(222)
float(2.5)
int(-3)
int(444)
a read-only mapping of the same file in the child sees the parent's later writes at once
child sees 111
ready
child saw 333
bool(true)
end
