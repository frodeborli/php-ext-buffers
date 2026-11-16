--TEST--
DataView: single values at any byte offset, little-endian by default, matching pack()/unpack()
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_INT_SIZE < 8) die('skip 64-bit PHP only: 64-bit dtypes'); ?>
--FILE--
<?php
use Buffers\{DType, Buffer, DataView};

$b = new Buffer(40);
$d = new DataView($b, 3, 30);
var_dump($d->buffer === $b, $d->byteOffset, $d->byteLength);

// [method, pack format little-endian, big-endian (null: one byte), size, values]
$cases = [
    ['Int8',    'c', null, 1, [-128, 127, -1]],
    ['UInt8',   'C', null, 1, [0, 255, 7]],
    ['Int16',   'v', 'n', 2, [-32768, 32767, -2]],
    ['UInt16',  'v', 'n', 2, [0, 65535, 4660]],
    ['Int32',   'V', 'N', 4, [-2147483648, 2147483647, -3]],
    ['UInt32',  'V', 'N', 4, [0, 4294967295, 305419896]],
    ['Int64',   'P', 'J', 8, [PHP_INT_MIN, PHP_INT_MAX, -4]],
    ['UInt64',  'P', 'J', 8, [0, PHP_INT_MAX, -1]],
    ['Float32', 'g', 'G', 4, [1.5, -0.125, 1048576.0]],
    ['Float64', 'e', 'E', 8, [M_PI, -1e300, 0.1]],
];
$signed16 = fn($u) => $u >= 0x8000 ? $u - 0x10000 : $u;
$signed32 = fn($u) => $u >= 0x80000000 ? $u - 0x100000000 : $u;
$bad = 0;
foreach ($cases as [$name, $le, $be, $size, $vals]) {
    foreach ([false, true] as $big) {
        if ($big && $be === null) continue;
        $fmt = $big ? $be : $le;
        foreach ([0, 1, 5, 30 - $size] as $off) {      // the last fits exactly
            foreach ($vals as $x) {
                $big ? $d->{"set$name"}($off, $x, true) : $d->{"set$name"}($off, $x);
                $bytes = substr($b->toString(), 3 + $off, $size);
                // pack() has no signed big/little-endian 16/32-bit formats: compare as unsigned
                $px = match ($name) { 'Int16' => $x & 0xffff, 'Int32' => $x & 0xffffffff, default => $x };
                if ($bytes !== pack($fmt, $px)) { echo "set$name($off, $x, big=$big) wrote ", bin2hex($bytes), "\n"; $bad++; }
                $got = $big ? $d->{"get$name"}($off, true) : $d->{"get$name"}($off);
                if ($got !== $x) { echo "get$name($off, big=$big) = ", var_export($got, true), "\n"; $bad++; }
                // and get from bytes written by pack()
                $raw = $b->view(DType::UInt8);
                $p = pack($fmt, $px);
                for ($i = 0; $i < $size; $i++) $raw[3 + $off + $i] = ord($p[$i]);
                $u = unpack($fmt, $p)[1];
                $u = match ($name) { 'Int8' => $u, 'Int16' => $signed16($u), 'Int32' => $signed32($u), default => $u };
                $got = $big ? $d->{"get$name"}($off, true) : $d->{"get$name"}($off);
                if ($got !== $u) { echo "get$name after pack($off, big=$big) = ", var_export($got, true), "\n"; $bad++; }
            }
        }
    }
}
echo "pack/unpack: $bad mismatches\n";

echo "the default is little-endian, the same bytes as an element of a view\n";
$d->setUInt32(0, 0x01020304);
var_dump(bin2hex(substr($b->toString(), 3, 4)));
$d->setFloat32(1, -2.75);
var_dump($b->view(DType::Float32, [], null, 4)->value);

echo "Float16, BFloat16, Float8: the conversions of Tensor elements\n";
$d->setFloat16(1, 1.0 / 3); var_dump($d->getFloat16(1), bin2hex(substr($b->toString(), 4, 2)));
$d->setFloat16(1, 65504.0, true); var_dump(bin2hex(substr($b->toString(), 4, 2)), $d->getFloat16(1, true));
$d->setBFloat16(7, -1.5); var_dump($d->getBFloat16(7), $b->view(DType::BFloat16, [], null, 10)->value);
foreach (['Float8E3M4', 'Float8E4M3', 'Float8E4M3B11FNUZ', 'Float8E4M3FN', 'Float8E4M3FNUZ', 'Float8E5M2', 'Float8E5M2FNUZ', 'Float8E8M0FNU'] as $f) {
    $d->{"set$f"}(9, 2.0);
    $t = $b->view(constant(DType::class . '::' . $f), [], null, 12);
    printf("%-18s %s %s\n", $f, var_export($d->{"get$f"}(9), true), var_export($t->value === $d->{"get$f"}(9), true));
}

echo "integers wrap when set\n";
$d->setInt8(0, 200); var_dump($d->getInt8(0), $d->getUInt8(0));
$d->setUInt16(0, -1); var_dump($d->getUInt16(0));
$d->setInt32(0, 0x1ffffffff); var_dump($d->getInt32(0));

echo "errors\n";
$tries = [
    'offset -1'       => fn() => $d->getInt8(-1),
    'offset = length' => fn() => $d->getUInt8(30),
    'straddles end'   => fn() => $d->getFloat64(23),
    'set past end'    => fn() => $d->setInt16(29, 1),
    'ctor offset'     => fn() => new DataView($b, 41),
    'ctor length'     => fn() => new DataView($b, 10, 31),
    'ctor negative'   => fn() => new DataView($b, 0, -1),
    'read-only'       => fn() => (new DataView(Buffer::fromString("abcd", false)))->setUInt8(0, 1),
    'read-only get'   => fn() => (new DataView(Buffer::fromString("abcd", false)))->getUInt32(0, true),
    'twice'           => fn() => $d->__construct($b),
    'type'            => fn() => $d->setInt32(0, "x"),
    'property'        => fn() => $d->byteOffset = 1,
    'clone'           => fn() => clone $d,
    'unconstructed'   => fn() => (new ReflectionClass(DataView::class))->newInstanceWithoutConstructor()->getInt8(0),
];
foreach ($tries as $k => $f) {
    try { $r = $f(); echo str_pad($k, 16), "ok ", var_export($r, true), "\n"; }
    catch (Throwable $e) { echo str_pad($k, 16), get_class($e), ": ", $e->getMessage(), "\n"; }
}

echo "a view of an empty window, and the window\n";
$e = new DataView($b, 40);
var_dump($e->byteLength);
try { $e->getUInt8(0); } catch (ValueError $x) { echo $x->getMessage(), "\n"; }
$w = new DataView($b, 8, 2);
$w->setUInt16(0, 0xbeef, true);
var_dump(bin2hex(substr($b->toString(), 8, 2)));

echo "the view keeps the buffer alive\n";
$wr = WeakReference::create($b);
unset($b, $raw, $t, $tries);   // the closures captured $b and $d
var_dump($wr->get() !== null);
unset($d, $e, $w);
var_dump($wr->get());
--EXPECT--
bool(true)
int(3)
int(30)
pack/unpack: 0 mismatches
the default is little-endian, the same bytes as an element of a view
string(8) "04030201"
float(-2.75)
Float16, BFloat16, Float8: the conversions of Tensor elements
float(0.333251953125)
string(4) "5535"
string(4) "7bff"
float(65504)
float(-1.5)
float(-1.5)
Float8E3M4         2.0 true
Float8E4M3         2.0 true
Float8E4M3B11FNUZ  2.0 true
Float8E4M3FN       2.0 true
Float8E4M3FNUZ     2.0 true
Float8E5M2         2.0 true
Float8E5M2FNUZ     2.0 true
Float8E8M0FNU      2.0 true
integers wrap when set
int(-56)
int(200)
int(65535)
int(-1)
errors
offset -1       ValueError: Buffers\DataView::getInt8(): Argument #1 ($offset) must be between 0 and 29 (the byte length of the view minus 1)
offset = length ValueError: Buffers\DataView::getUInt8(): Argument #1 ($offset) must be between 0 and 29 (the byte length of the view minus 1)
straddles end   ValueError: Buffers\DataView::getFloat64(): Argument #1 ($offset) must be between 0 and 22 (the byte length of the view minus 8)
set past end    ValueError: Buffers\DataView::setInt16(): Argument #1 ($offset) must be between 0 and 28 (the byte length of the view minus 2)
ctor offset     ValueError: Buffers\DataView::__construct(): Argument #2 ($offset) must be between 0 and the byte length of the buffer
ctor length     ValueError: Buffers\DataView::__construct(): Argument #3 ($length) must be between 0 and the byte length of the buffer minus the offset
ctor negative   ValueError: Buffers\DataView::__construct(): Argument #3 ($length) must be between 0 and the byte length of the buffer minus the offset
read-only       Error: Cannot write to a read-only Buffer
read-only get   ok 1633837924
twice           Error: Cannot modify a DataView: it is already constructed
type            TypeError: Buffers\DataView::setInt32(): Argument #2 ($value) must be of type int, string given
property        Error: Property Buffers\DataView::$byteOffset is read-only
clone           Error: Trying to clone an uncloneable object of class Buffers\DataView
unconstructed   ReflectionException: Class Buffers\DataView is an internal class marked as final that cannot be instantiated without invoking its constructor
a view of an empty window, and the window
int(0)
Buffers\DataView::getUInt8(): Argument #1 ($offset) is out of range: the view has 0 bytes, fewer than the 1 of the value
string(4) "beef"
the view keeps the buffer alive
bool(true)
NULL
