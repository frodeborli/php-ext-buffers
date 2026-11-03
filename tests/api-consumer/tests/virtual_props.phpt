--TEST--
C API: buffers_declare_virtual_props() - read-only virtual properties served by get hooks
--SKIPIF--
<?php if (!extension_loaded('buffers_api_test')) die('skip needs the buffers_api_test extension'); ?>
--FILE--
<?php
$r = new ReflectionClass('BatProps');
foreach ($r->getProperties() as $p) {
    printf("%s %s virtual=%d readonly-ish=%d\n", $p->getName(), $p->getType(), $p->isVirtual(), $p->hasHook(PropertyHookType::Get));
}
$o = $r->newInstanceWithoutConstructor();
var_dump($o->answer, $o->label, isset($o->answer));
try { $o->answer = 1; } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
try { unset($o->label); } catch (Error $e) { echo get_class($e), ': ', $e->getMessage(), "\n"; }
var_dump(get_object_vars($o));
?>
--EXPECT--
answer int virtual=1 readonly-ish=1
label ?string virtual=1 readonly-ish=1
int(42)
string(5) "label"
bool(true)
Error: Property BatProps::$answer is read-only
Error: Cannot unset hooked property BatProps::$label
array(2) {
  ["answer"]=>
  int(42)
  ["label"]=>
  string(5) "label"
}