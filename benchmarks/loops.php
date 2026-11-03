<?php
/* The hot loops. Kept in their own file so opcache compiles (and the JIT can trace) them; each
 * container kind has its own functions so the traces stay monomorphic.
 * $w x $h pixels, 4 bytes per pixel, the alpha byte is read or written. */

// ---- reads -------------------------------------------------------------------------------

function read_view_1d(TypedView $f, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $s += $f[$y * $w * 4 + $x * 4 + 3];
    return $s;
}
function read_view_1d_hoisted(TypedView $f, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) { $base = $y * $w * 4 + 3; for ($x = 0; $x < $w; $x++) $s += $f[$base + $x * 4]; }
    return $s;
}
function read_view_3d(TypedView $v, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $s += $v[$y][$x][3];
    return $s;
}
function read_view_3d_rowhoisted(TypedView $v, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) { $row = $v[$y]; for ($x = 0; $x < $w; $x++) $s += $row[$x][3]; }
    return $s;
}
function read_view_2d(TypedView $v, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $s += $v[$y][$x * 4 + 3];
    return $s;
}
function read_array_1d(array $f, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $s += $f[$y * $w * 4 + $x * 4 + 3];
    return $s;
}
function read_array_1d_hoisted(array $f, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) { $base = $y * $w * 4 + 3; for ($x = 0; $x < $w; $x++) $s += $f[$base + $x * 4]; }
    return $s;
}
function read_array_nested(array $a, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $s += $a[$y][$x][3];
    return $s;
}
function read_spl_1d(SplFixedArray $f, int $w, int $h): int {
    $s = 0;
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $s += $f[$y * $w * 4 + $x * 4 + 3];
    return $s;
}

// ---- writes ------------------------------------------------------------------------------

function write_view_1d(TypedView $f, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $f[$y * $w * 4 + $x * 4 + 3] = $x ^ $y;
    return 0;
}
function write_view_1d_hoisted(TypedView $f, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) { $base = $y * $w * 4 + 3; for ($x = 0; $x < $w; $x++) $f[$base + $x * 4] = $x ^ $y; }
    return 0;
}
function write_view_3d(TypedView $v, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $v[$y][$x][3] = $x ^ $y;
    return 0;
}
function write_view_3d_rowhoisted(TypedView $v, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) { $row = $v[$y]; for ($x = 0; $x < $w; $x++) $row[$x][3] = $x ^ $y; }
    return 0;
}
function write_view_2d(TypedView $v, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $v[$y][$x * 4 + 3] = $x ^ $y;
    return 0;
}
function write_array_1d(int $w, int $h, int $reps): int {
    $f = array_fill(0, $w * $h * 4, 7);        // a local array (refcount 1): no reference or copy-on-write on the way
    $t = hrtime(true);
    for ($r = 0; $r < $reps; $r++) for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $f[$y * $w * 4 + $x * 4 + 3] = $x ^ $y;
    return hrtime(true) - $t;
}
function write_array_1d_hoisted(int $w, int $h, int $reps): int {
    $f = array_fill(0, $w * $h * 4, 7);
    $t = hrtime(true);
    for ($r = 0; $r < $reps; $r++) for ($y = 0; $y < $h; $y++) { $base = $y * $w * 4 + 3; for ($x = 0; $x < $w; $x++) $f[$base + $x * 4] = $x ^ $y; }
    return hrtime(true) - $t;
}
function write_array_nested(int $w, int $h, int $reps): int {
    $a = array_fill(0, $h, array_fill(0, $w, [1, 2, 3, 7]));
    $t = hrtime(true);
    for ($r = 0; $r < $reps; $r++) for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $a[$y][$x][3] = $x ^ $y;
    return hrtime(true) - $t;
}
function write_spl_1d(SplFixedArray $f, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $f[$y * $w * 4 + $x * 4 + 3] = $x ^ $y;
    return 0;
}

// ---- read-modify-write (+=) --------------------------------------------------------------

function add_view_1d(TypedView $f, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $f[$y * $w * 4 + $x * 4 + 3] += 1;
    return 0;
}
function add_view_3d(TypedView $v, int $w, int $h): int {
    for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $v[$y][$x][3] += 1;
    return 0;
}
function add_array_1d(int $w, int $h, int $reps): int {
    $f = array_fill(0, $w * $h * 4, 7);
    $t = hrtime(true);
    for ($r = 0; $r < $reps; $r++) for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $f[$y * $w * 4 + $x * 4 + 3] += 1;
    return hrtime(true) - $t;
}
function add_array_nested(int $w, int $h, int $reps): int {
    $a = array_fill(0, $h, array_fill(0, $w, [1, 2, 3, 7]));
    $t = hrtime(true);
    for ($r = 0; $r < $reps; $r++) for ($y = 0; $y < $h; $y++) for ($x = 0; $x < $w; $x++) $a[$y][$x][3] += 1;
    return hrtime(true) - $t;
}

// ---- per dtype: n sequential 1-D accesses ------------------------------------------------

function dtype_read(TypedView $f, int $n) {
    $x = null;
    for ($i = 0; $i < $n; $i++) $x = $f[$i];
    return $x;
}
function dtype_write(TypedView $f, int $n, $val): int {
    for ($i = 0; $i < $n; $i++) $f[$i] = $val;
    return 0;
}
