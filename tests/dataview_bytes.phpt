--TEST--
DataView getBytes, setBytes, fill, copyBytes: byte ranges of the view, bounds-checked, copyBytes overlap-safe
--SKIPIF--
<?php if (!extension_loaded('buffers')) die('skip buffers extension not loaded'); if (PHP_INT_SIZE < 8) die('skip 64-bit PHP only: 64-bit dtypes'); ?>
--FILE--
<?php
use Buffers\{Buffer, DataView};

$b = Buffer::fromString('0123456789abcdef');
$d = new DataView($b, 2, 12);                  // "23456789abcd"

// getBytes: relative to the view, empty ranges allowed (also at the very end)
var_dump($d->getBytes(0, 4), $d->getBytes(8, 4), $d->getBytes(3, 0), $d->getBytes(12, 0));

// setBytes writes everything at the offset, never outside the view
$d->setBytes(1, 'XYZ');
$d->setBytes(12, '');
echo $b->toString(), "\n";

// fill: wraps the byte value like setUInt8
$d->fill(4, 3, 0x41);
$d->fill(0, 1, 0x142);                         // 0x42
$d->fill(11, 0, 0);
echo $b->toString(), "\n";

// copyBytes: memmove semantics in both directions
$m = new DataView(Buffer::fromString('abcdefghij'));
$m->copyBytes(2, 0, 5);  echo $m->getBytes(0, 10), "\n";   // forward overlap
$m->copyBytes(0, 3, 5);  echo $m->getBytes(0, 10), "\n";   // backward overlap
$m->copyBytes(9, 9, 1);  $m->copyBytes(10, 0, 0);
echo $m->getBytes(0, 10), "\n";

// binary safe
$z = new DataView(new Buffer(4));
$z->setBytes(0, "\0\xff\0\x80");
var_dump(bin2hex($z->getBytes(0, 4)), $z->getUInt32(0) === 0x8000ff00);

// errors: nothing is written when a range does not fit
foreach ([
    fn() => $d->getBytes(-1, 1), fn() => $d->getBytes(13, 0), fn() => $d->getBytes(10, 3), fn() => $d->getBytes(0, -1),
    fn() => $d->setBytes(10, 'abc'), fn() => $d->setBytes(-1, 'a'),
    fn() => $d->fill(5, 8, 0), fn() => $d->fill(0, -1, 0),
    fn() => $d->copyBytes(10, 0, 3), fn() => $d->copyBytes(0, 10, 3), fn() => $d->copyBytes(0, 0, -1),
    fn() => $d->getBytes(PHP_INT_MAX, PHP_INT_MAX), fn() => $d->fill(1, PHP_INT_MAX, 0),
] as $f) {
    try { $f(); echo "no error\n"; } catch (ValueError $e) { echo $e->getMessage(), "\n"; }
}
echo $b->toString(), "\n";

// read-only buffers: reads work, writes throw
$ro = new DataView(Buffer::fromString('readonly', false));
var_dump($ro->getBytes(4, 4));
foreach ([fn() => $ro->setBytes(0, 'x'), fn() => $ro->fill(0, 1, 0), fn() => $ro->copyBytes(0, 1, 1)] as $f) {
    try { $f(); } catch (Error $e) { echo $e->getMessage(), "\n"; }
}
?>
--EXPECT--
string(4) "2345"
string(4) "abcd"
string(0) ""
string(0) ""
012XYZ6789abcdef
01BXYZAAA9abcdef
ababcdehij
bcdehdehij
bcdehdehij
string(8) "00ff0080"
bool(true)
Buffers\DataView::getBytes(): Argument #1 ($offset) must be between 0 and 12 (the byte length of the view)
Buffers\DataView::getBytes(): Argument #1 ($offset) must be between 0 and 12 (the byte length of the view)
Buffers\DataView::getBytes(): Argument #2 ($length) must be between 0 and 2 (the bytes from the offset to the end of the view)
Buffers\DataView::getBytes(): Argument #2 ($length) must be between 0 and 12 (the bytes from the offset to the end of the view)
Buffers\DataView::setBytes(): Argument #2 ($bytes) must be between 0 and 2 (the bytes from the offset to the end of the view)
Buffers\DataView::setBytes(): Argument #1 ($offset) must be between 0 and 12 (the byte length of the view)
Buffers\DataView::fill(): Argument #2 ($length) must be between 0 and 7 (the bytes from the offset to the end of the view)
Buffers\DataView::fill(): Argument #2 ($length) must be between 0 and 12 (the bytes from the offset to the end of the view)
Buffers\DataView::copyBytes(): Argument #3 ($length) must be between 0 and 2 (the bytes from the offset to the end of the view)
Buffers\DataView::copyBytes(): Argument #3 ($length) must be between 0 and 2 (the bytes from the offset to the end of the view)
Buffers\DataView::copyBytes(): Argument #3 ($length) must be between 0 and 12 (the bytes from the offset to the end of the view)
Buffers\DataView::getBytes(): Argument #1 ($offset) must be between 0 and 12 (the byte length of the view)
Buffers\DataView::fill(): Argument #2 ($length) must be between 0 and 11 (the bytes from the offset to the end of the view)
01BXYZAAA9abcdef
string(4) "only"
Cannot write to a read-only Buffer
Cannot write to a read-only Buffer
Cannot write to a read-only Buffer
