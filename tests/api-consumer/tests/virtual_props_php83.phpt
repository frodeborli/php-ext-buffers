--TEST--
PHP 8.3: buffers_declare_virtual_props() - read-only properties served by internal __get
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); if (PHP_VERSION_ID >= 80400) die('skip PHP 8.3 only: 8.4+ uses property hooks (virtual_props.phpt)'); ?>
--FILE--
<?php
$o = (new ReflectionClass('BatProps'))->newInstanceWithoutConstructor();
var_dump($o->answer, $o->label, isset($o->answer), isset($o->nope));
try { $o->answer = 1; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { unset($o->label); } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
--EXPECTF--
int(42)
string(5) "label"
bool(true)
bool(false)
Error: Property BatProps::$answer is read-only
Error: Cannot unset property BatProps::$label
