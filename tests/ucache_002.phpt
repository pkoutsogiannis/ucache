--TEST--
Testing Function call in case insensitive way
--SKIPIF--
<?php include('skipif.inc'); ?>
--INI--
ucache.enablecli=1
ucache.fcenabled=1
ucache.ucenabled=1
--FILE--
<?php
function simple()
{
    var_dump("Hello World");
}
Simple();
SIMPLE();
?>
==DONE==
--EXPECTF--
string(11) "Hello World"
string(11) "Hello World"
==DONE==

