--TEST--
C API: buffers_dtype_promote() matches every row of tests/promotion.tsv (the shared promotion conformance table)
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
$rows = file(__DIR__ . '/../../promotion.tsv', FILE_IGNORE_NEW_LINES);
array_shift($rows);
$checked = $wrong = 0;
foreach ($rows as $row) {
    [$a, $b, $op, $want] = explode("\t", $row);
    $got = bat_dtype_promote($a, $b, $op);
    $checked++;
    if ($got !== $want && $wrong++ < 20) {
        echo "$a $op $b: got $got, table says $want\n";
    }
}
echo "checked $checked, wrong $wrong\n";
var_dump(count(array_filter($rows, fn($r) => !str_ends_with($r, "\terror"))));

// written by hand from the Array API standard's promotion table and the gaps docs/arithmetic.md fills
$spot = [
    ['uint8', 'int8', '+', 'int16'], ['uint16', 'int8', '*', 'int32'], ['uint32', 'int16', '-', 'int64'],
    ['uint8', 'int16', '+', 'int16'], ['uint64', 'int8', '+', 'error'], ['uint64', 'uint8', '+', 'uint64'],
    ['int32', 'int64', '%', 'int64'], ['int8', 'int8', '/', 'float64'], ['int32', 'float32', '+', 'float32'],
    ['int64', 'float16', '*', 'float16'], ['float16', 'float32', '+', 'float32'], ['bfloat16', 'float64', '-', 'float64'],
    ['float16', 'bfloat16', '+', 'error'], ['bfloat16', 'bfloat16', '/', 'bfloat16'],
    ['uint8', 'php-int', '+', 'uint8'], ['php-int', 'int16', '-', 'int16'], ['int32', 'php-float', '*', 'float64'],
    ['float16', 'php-float', '*', 'float16'], ['float16', 'php-int', '/', 'float16'], ['php-float', 'php-int', '+', 'error'],
    ['bool', 'bool', '&', 'bool'], ['bool', 'php-bool', '^', 'bool'], ['bool', 'bool', '+', 'error'],
    ['bool', 'int8', '&', 'error'], ['int8', 'php-bool', '+', 'error'], ['float32', 'int32', '<<', 'error'],
    ['uint8', 'int8', '<<', 'int16'], ['int16', 'php-int', '>>', 'int16'], ['float32', 'php-int', '|', 'error'],
    ['int8', '-', '~', 'int8'], ['bool', '-', '~', 'bool'], ['float32', '-', '~', 'error'], ['php-int', '-', '~', 'error'],
    ['float8_e4m3fn', 'float32', '+', 'error'], ['int4', 'int8', '+', 'error'], ['complex64', 'float32', '*', 'error'],
    ['float32', 'float32', '**', 'float32'], ['int32', 'int32', '**', 'int32'], ['int32', 'php-float', '**', 'float64'],
    ['float64', 'float64', '%', 'float64'],
];
foreach ($spot as [$a, $b, $op, $want]) {
    $got = bat_dtype_promote($a, $b, $op);
    if ($got !== $want) echo "spot $a $op $b: got $got, expected $want\n";
}
echo count($spot), " spot checks done\n";
--EXPECT--
checked 13510, wrong 0
int(1470)
40 spot checks done
