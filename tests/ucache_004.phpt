--TEST--
Testing return statement from an included file
--SKIPIF--
<?php include('skipif.inc'); ?>
--INI--
ucache.enablecli=1
ucache.fcenabled=1
ucache.ucenabled=1
--FILE--
<?php
$foo = require_once 'ucache_004_require.php';
echo("$foo\r\n"); // prints 'Ucache'
?>
==DONE==
--EXPECTF--
Ucache
==DONE==

