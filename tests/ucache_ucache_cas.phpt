--TEST--
Ucache - Testing ucache_cas function
--SKIPIF--
<?php include('skipif.inc'); ?>
--INI--
ucache.enablecli=1
ucache.ucenabled=1
--FILE--
<?php
ucache_set('counter', 2922);
var_dump(ucache_cas('counter', 2922, 1));
var_dump(ucache_get('counter'));
?>
==DONE==
--EXPECTF--
bool(true)
int(1)
==DONE==
