--TEST--
Ucache - Testing ucache_[inc|dec] functions
--SKIPIF--
<?php include('skipif.inc'); ?>
--INI--
ucache.enablecli=1
ucache.fcenabled=1
ucache.ucenabled=1
--FILE--
<?php
$key = 'mykey';
$success = FALSE;

ucache_delete($key);
ucache_inc($key, 1, $success);
var_dump($success);
ucache_set($key, 0);
ucache_inc($key, 1, $success);
var_dump($success);
ucache_dec($key, 1, $success);
var_dump($success);
?>
==DONE==
--EXPECTF--
bool(false)
bool(true)
bool(true)
==DONE==
