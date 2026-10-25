--TEST--
Ucache - Testing Global Variables
--SKIPIF--
<?php include('skipif.inc'); ?>
--INI--
ucache.enablecli=1
ucache.fcenabled=1
ucache.ucenabled=1
--FILE--
<?php
$variable = 2.0;
function testGlobal()
{
    global $variable;
    var_dump($variable);
}
testGlobal();
$variable += 1;
testGlobal();
$variable = "Changing to string.";
testGlobal();
?>
==DONE==
--EXPECTF--
float(2)
float(3)
string(19) "Changing to string."
==DONE==
