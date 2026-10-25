--TEST--
Testing ini directive include_path
--SKIPIF--
<?php include('skipif.inc'); ?>
--INI--
ucache.enablecli=1
ucache.fcenabled=1
ucache.ucenabled=1
--FILE--
<?php
ini_set("include_path", ".;" . __DIR__ . "/foo");
include 'ucache_005_1.php';
ini_set("include_path", ".;" . __DIR__ . "/bar");
include 'ucache_005_2.php';
?>
==DONE==
--EXPECTF--
foo subdir
bar subdir
==DONE==


