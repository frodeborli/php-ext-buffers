--TEST--
Buffers\Compiled: PHP arrays read and written in place, by value and by reference, as PHP has them
--SKIPIF--
<?php if (!extension_loaded('buffers') || !class_exists(Buffers\Compiled::class)) die('skip built without the extras'); ?>
--FILE--
<?php
include __DIR__ . '/arrays.inc';
--EXPECT--
ok   sum of ints, in an inlined function
ok   sum of floats
ok   empty
ok   bools
ok   by reference: an int array filled with floats
ok   by reference: an int written into a float array stays an int
ok   by reference: nested lists
ok   by reference: through an inlined function
ok   by value: the caller keeps its array
ok   by value: written, then returned
ok   bitwise operators
ok   math functions
ok   foreach with keys over nested lists
ok   a list that is not packed: read
ok   a list that is not packed: written
captures: [11,[1,2],11,[9,2]]
property: [3,[3,6,9],3,[3,6,9]]
separated: [[1,2,0],[1,2,3]]
->__invoke(): [5], call_user_func(): 42
not a list: TypeError: Buffers\Compiled: this argument is not an array the code can read: a list (array_is_list()) of ints and floats, or of bools, or lists of such lists of equal lengths
bools and numbers: TypeError: Buffers\Compiled: this argument is not an array the code can read: a list (array_is_list()) of ints and floats, or of bools, or lists of such lists of equal lengths
a string in it: TypeError: Buffers\Compiled: this argument is not an array the code can read: a list (array_is_list()) of ints and floats, or of bools, or lists of such lists of equal lengths
lists of different lengths: TypeError: Buffers\Compiled: this argument is not an array the code can read: a list (array_is_list()) of ints and floats, or of bools, or lists of such lists of equal lengths
an index out of range: ValueError: Index 2 is out of range for an axis of length 1
a negative index: ValueError: Index -1 is out of range for an axis of length 1
a bool into numbers: TypeError: Buffers\Compiled: writing a bool into an array of numbers
written and copied: TypeError: Buffers\Compiled: an array the code writes into is also copied (assigned, passed by value, or iterated with foreach); write into it through one variable, with for and an index
written while iterated: TypeError: Buffers\Compiled: an array the code writes into is also copied (assigned, passed by value, or iterated with foreach); write into it through one variable, with for and an index
assigning a by-reference variable: ValueError: Buffers\Compiled: assigning to $a, a by-reference parameter or capture (write its elements) is not supported
an int by reference: ValueError: Buffers\Compiled: the by-reference parameter $n of this type (only arrays and tensors) is not supported
a literal for a by-reference parameter: Error: Buffers\Compiled::__invoke(): Argument #1 ($a) could not be passed by reference
