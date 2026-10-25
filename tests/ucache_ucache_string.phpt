--TEST--
Ucache - Testing ucache_* functions with simple strings
--SKIPIF--
<?php include('skipif.inc'); ?>
--INI--
ucache.enablecli=1
ucache.fcenabled=1
ucache.ucenabled=1
--FILE--
<?php

echo("clearing ucache\n");
var_dump(ucache_clear());

echo("setting 'foo'\n");
$bar = "BAR";

var_dump(ucache_add('foo', $bar));
var_dump(ucache_add('foo', $bar));
var_dump(ucache_get('foo'));
var_dump(ucache_set('foo', 'BAR2'));
var_dump(ucache_get('foo'));
var_dump(ucache_exists('foo'));
var_dump(ucache_info(false, 'foo'));
var_dump(ucache_delete('foo'));

echo("setting \$foo\n");
$foo = "FOO";

var_dump(ucache_add($foo, $bar));
var_dump(ucache_add($foo, $bar));
var_dump(ucache_get($foo));
var_dump(ucache_set($foo, 'BAR2'));
var_dump(ucache_get($foo));
var_dump(ucache_exists($foo));
var_dump(ucache_info(false, $foo));
var_dump(ucache_delete($foo));

echo("Done!");

?>
--EXPECTF--
clearing ucache
bool(true)
setting 'foo'
bool(true)

Warning: ucache_add(): function called with a key which already exists in %sucache_string.php on line %d
bool(false)
string(3) "BAR"
bool(true)
string(4) "BAR2"
bool(true)
array(6) {
  ["total_cache_uptime"]=>
  int(0)
  ["is_local_cache"]=>
  bool(false)
  ["total_item_count"]=>
  int(1)
  ["total_hit_count"]=>
  int(2)
  ["total_miss_count"]=>
  int(0)
  ["ucache_entries"]=>
  array(1) {
    [1]=>
    array(6) {
      ["key_name"]=>
      string(3) "foo"
      ["value_type"]=>
      string(6) "string"
      ["value_size"]=>
      int(%d)
      ["ttl_seconds"]=>
      int(0)
      ["age_seconds"]=>
      int(0)
      ["hitcount"]=>
      int(1)
    }
  }
}
bool(true)
setting $foo
bool(true)

Warning: ucache_add(): function called with a key which already exists in %sucache_string.php on line %d
bool(false)
string(3) "BAR"
bool(true)
string(4) "BAR2"
bool(true)
array(6) {
  ["total_cache_uptime"]=>
  int(0)
  ["is_local_cache"]=>
  bool(false)
  ["total_item_count"]=>
  int(1)
  ["total_hit_count"]=>
  int(4)
  ["total_miss_count"]=>
  int(0)
  ["ucache_entries"]=>
  array(1) {
    [1]=>
    array(6) {
      ["key_name"]=>
      string(3) "FOO"
      ["value_type"]=>
      string(6) "string"
      ["value_size"]=>
      int(%d)
      ["ttl_seconds"]=>
      int(0)
      ["age_seconds"]=>
      int(0)
      ["hitcount"]=>
      int(1)
    }
  }
}
bool(true)
Done!