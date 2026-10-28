<?php
/*
 * ucache smoke test.
 *
 * build.bat runs it against the freshly built DLL:
 *   php -n -d extension=<dll> -d ucache.enablecli=1 test-build.php <dll>
 *
 * What the run needs:
 * - -n (no php.ini), so that the defaults of the ucache.* settings can be checked. Settings given on
 *   the command line with -d are not checked against the defaults, so -d ucache.apppoolid=... or
 *   -d ucache.namesalt=... can keep the test away from another cache;
 * - ucache.enablecli=1, without which the CLI does not use the cache;
 * - a cache that no other process uses while the test runs: the test clears it and checks its hit
 *   and miss counts and its free memory. Processes with the same ucache.apppoolid and ucache.namesalt
 *   share one cache; without ucache.apppoolid, so do the processes started by the same parent process
 *   (php.exe started from one command prompt, the php-cgi.exe of one FastCGI pool);
 * - optionally, the path of the DLL under test as the first argument. The checks with several
 *   processes start other php.exe processes with that DLL, on caches of their own (one unique
 *   ucache.apppoolid per group), php -S servers among them. Without the argument they are skipped,
 *   with one "SKIP:" line.
 *
 * Prints one line per check ("OK: ..." or "FAIL: ...", or "SKIP: ..." for checks that cannot run) and
 * exits with 1 if any check failed.
 * The expected results follow the semantics of the original user cache implementation (the
 * functions were only renamed), apart from the differences that README.md lists. The test clears the
 * user cache it runs against.
 *
 * The sections after "TTL expiry" check the code that copies values into the shared
 * memory and back (ucache_zvcache.c), which depends on the zval, HashTable and zend_reference layouts
 * of each PHP version. The same checks run on every supported version (7.2 to 7.4, 8.0 to 8.5); the
 * few that need a newer PHP feature (typed properties, __serialize(), WeakReference, unserialize_max_depth,
 * named arguments, WeakMap, enums, Fibers, readonly properties and classes) only run where it exists.
 * If PHP crashes (no summary line, negative exit code), the crash happened in the check after the last
 * line printed. Adding -d zend_extension=php_opcache.dll -d opcache.enable_cli=1 (only the second
 * setting on PHP 8.5, where opcache is built in) runs the same checks with the immutable arrays of
 * opcache.
 */

error_reporting(E_ALL);

$failures = 0;
$errors   = [];

/* Collect warnings and notices instead of printing them; checks that expect one look at them. Errors
   silenced with @ are left out (the checks with several processes try ports and files that way) */
set_error_handler(function ($errno, $errstr) use (&$errors) {
    if ((error_reporting() & $errno) !== 0) {
        $errors[] = $errstr;
    }
    return true;
});

function check($name, $ok, $detail = null)
{
    global $failures;

    if ($ok) {
        echo "OK: $name\n";
    } else {
        $failures++;
        echo "FAIL: $name" . ($detail === null ? '' : ' -- got ' . dump($detail)) . "\n";
    }
}

function dump($value)
{
    return preg_replace('/\s+/', ' ', var_export($value, true));
}

/* Calls $fn and returns its result; the warnings and notices raised by the call go to $raised */
function capture($fn, &$raised)
{
    global $errors;

    $saved  = $errors;
    $errors = [];
    $result = $fn();
    $raised = $errors;
    $errors = $saved;

    return $result;
}

/* True if exactly one warning was raised and it contains $text */
function warned($raised, $text)
{
    return count($raised) === 1 && strpos($raised[0], $text) !== false;
}

/* Returns the entry for $key from a ucache_info() result, or null */
function find_entry($info, $key)
{
    if (!is_array($info) || !isset($info['ucache_entries'])) {
        return null;
    }

    foreach ($info['ucache_entries'] as $entry) {
        if ($entry['key_name'] === $key) {
            return $entry;
        }
    }

    return null;
}

/* Returns every entry for $key in a ucache_info() result, in a list */
function find_entries($info, $key)
{
    $found = [];
    if (is_array($info) && isset($info['ucache_entries'])) {
        foreach ($info['ucache_entries'] as $entry) {
            if ($entry['key_name'] === $key) {
                $found[] = $entry;
            }
        }
    }

    return $found;
}

class UcacheSmokePoint
{
    public $x;
    public $y;

    public function __construct($x, $y)
    {
        $this->x = $x;
        $this->y = $y;
    }
}

/* Its __wakeup() deletes the entry being read ($key), then stores other entries that can take its memory */
class UcacheSmokeWakeupDeletes
{
    public static $key = '';
    public $data = 'x';

    public function __wakeup()
    {
        ucache_delete(self::$key);
        for ($i = 0; $i < 50; $i++) {
            ucache_set("zv_wakeup_filler$i", str_repeat(chr(65 + $i % 26), 2000));
        }
    }
}

/* Its __wakeup() replaces the entry being read ($key) with another value */
class UcacheSmokeWakeupReplaces
{
    public static $key = '';
    public $data = 'x';

    public function __wakeup()
    {
        ucache_set(self::$key, ['replaced' => str_repeat('r', 5000)]);
        for ($i = 0; $i < 50; $i++) {
            ucache_set("zv_wakeup_filler$i", str_repeat(chr(97 + $i % 26), 2000));
        }
    }
}

class UcacheSmokeWakeupThrows
{
    public $v = 1;

    public function __wakeup()
    {
        throw new RuntimeException('__wakeup failed');
    }
}

/* Its __sleep() stores another entry while the cache copies the value that holds the object */
class UcacheSmokeSleepStores
{
    public $v = 1;

    public function __sleep()
    {
        ucache_set('zv_sleep_other', ['from' => '__sleep']);
        return ['v'];
    }
}

/* An object that refers to itself (and maybe to a peer): a serialization numbers it, and refers back to it with r:N */
class UcacheSmokeNode
{
    public $name;
    public $self;
    public $peer;

    public function __construct($name)
    {
        $this->name = $name;
        $this->self = $this;
    }
}

/* Serialized by __serialize() and restored by __unserialize() (PHP 7.4+), which sets $restored */
class UcacheSmokeSerializeMagic
{
    public $name;
    public $list;
    public $restored;

    public function __construct($name, $list, $restored = false)
    {
        $this->name = $name;
        $this->list = $list;
        $this->restored = $restored;
    }

    public function __serialize()
    {
        return ['n' => $this->name, 'l' => $this->list];
    }

    public function __unserialize($data)
    {
        $this->name = $data['n'];
        $this->list = $data['l'];
        $this->restored = true;
    }
}

/*
 * Objects whose __sleep() changes, through PHP references, a value that ucache_set() is copying in
 * (see "Changes made by __sleep()" below). The strings are made at runtime (fresh_string()): opcache
 * makes the result of str_repeat() with constant arguments an interned string, which is never freed.
 */
function fresh_string($char, $length)
{
    return str_repeat($char, $length);
}

/* Frees the array being copied in (only a reference holds it), and new arrays and strings take its memory */
class UcacheSmokeFreesValue
{
    public function __sleep()
    {
        global $zvFreed, $zvJunk;

        $zvFreed = null;
        $zvJunk = [];
        for ($i = 0; $i < 2000; $i++) {
            $zvJunk[] = [$i, fresh_string('z', 37)];
        }
        return [];
    }
}

/* Frees a string of the array being copied in (changed in place through a reference), and a new string
   of the same size takes its memory */
class UcacheSmokeChangesValue
{
    public $x = 1;

    public function __sleep()
    {
        global $zvChanged, $zvReused;

        $zvChanged['s'] = null;
        $zvReused = fresh_string('Q', 40);
        return ['x'];
    }
}

/* Frees a string of the value being copied in (a reference holds it), and a new string of the same size,
   stored in another reference of the value, takes its memory */
class UcacheSmokeReplacesValue
{
    public $x = 1;

    public function __sleep()
    {
        global $zvFirst, $zvSecond;

        $zvFirst = null;
        $zvSecond = fresh_string('b', 24);
        return ['x'];
    }
}

/* Calls $fn and returns [its result, null], or [null, the exception or error it threw] */
function attempt($fn)
{
    try {
        return [$fn(), null];
    } catch (Throwable $e) {
        return [null, $e];
    }
}

/* What an attempt() returned, for FAIL details */
function outcome($attempt)
{
    return ($attempt[1] === null) ? brief($attempt[0]) : get_class($attempt[1]) . ': ' . $attempt[1]->getMessage();
}

/* A short description of a value, for FAIL details */
function brief($value)
{
    if (is_array($value)) {
        return 'array(' . count($value) . ')';
    }
    if (is_object($value)) {
        return 'object(' . get_class($value) . ')';
    }

    $text = dump($value);
    return (strlen($text) > 60) ? substr($text, 0, 60) . '...' : $text;
}

/*
 * Describes the first difference between $expected and $actual, or returns null if there is none.
 * Scalars and arrays are compared as with === (same keys in the same order, same types), except that
 * NAN equals NAN; objects are equal if they have the same class and the same properties. Arrays are
 * compared down to 128 levels, which also ends the recursion on arrays that contain themselves.
 */
function value_diff($expected, $actual, $path = 'value', $depth = 0)
{
    $where = (strlen($path) > 100) ? '...' . substr($path, -100) : $path;

    if (is_float($expected) && is_float($actual) && is_nan($expected) && is_nan($actual)) {
        return null;
    }

    if (is_object($expected) && is_object($actual) && get_class($expected) === get_class($actual)) {
        $expected = (array)$expected;
        $actual   = (array)$actual;
    }

    if (!is_array($expected) || !is_array($actual)) {
        return ($expected === $actual) ? null : "$where is " . brief($actual) . ' instead of ' . brief($expected);
    }

    if ($depth >= 128) {
        return null;
    }

    $expectedKeys = array_keys($expected);
    $actualKeys   = array_keys($actual);
    if ($expectedKeys !== $actualKeys) {
        $n = 0;
        while (isset($expectedKeys[$n], $actualKeys[$n]) && $expectedKeys[$n] === $actualKeys[$n]) {
            $n++;
        }
        return "$where: key #$n is " . (isset($actualKeys[$n]) ? brief($actualKeys[$n]) : 'missing') . ' instead of '
             . (isset($expectedKeys[$n]) ? brief($expectedKeys[$n]) : 'missing') . ' (' . count($actualKeys) . ' keys instead of '
             . count($expectedKeys) . ')';
    }

    foreach ($expectedKeys as $key) {
        $diff = value_diff($expected[$key], $actual[$key], $path . '[' . brief($key) . ']', $depth + 1);
        if ($diff !== null) {
            return $diff;
        }
    }

    return null;
}

/*
 * Changes an array in place the way scripts do: changes its nested arrays (two levels down), appends
 * elements, adds string keys and removes the first element. On an array that ucache_get() just
 * returned, the writes go straight into the HashTables that ucache built, unless they are shared.
 */
function change_array(&$array, $depth = 0)
{
    $keys = array_keys($array);

    if ($depth < 2) {
        foreach ($keys as $key) {
            if (is_array($array[$key])) {
                change_array($array[$key], $depth + 1);
            }
        }
    }

    $intKeys   = array_filter($keys, 'is_int');
    $canAppend = ($intKeys === [] || max($intKeys) < PHP_INT_MAX - 100);
    for ($i = 0; $i < 20; $i++) {
        if ($canAppend) {
            $array[] = $i;
        }
        $array["added $i"] = "value $i";
    }

    if ($keys !== []) {
        unset($array[$keys[0]]);
    }
}

/*
 * Stores the value that $make() returns under $key and checks that the value read back is the same
 * (value_diff), that it ends up the same as a fresh copy when both get the same changes (change_array),
 * and that reading the key again afterwards still gives the stored value. $make() builds a new copy
 * every time, so the copies share nothing (not even references) and have the same internal state.
 */
function check_roundtrip($name, $key, $make)
{
    $set = ucache_set($key, $make());
    $success = 'unset';
    $got = ucache_get($key, $success);
    $readBack = value_diff($make(), $got);

    $changed = null;
    if ($readBack === null && is_array($got)) {
        $expected = $make();
        change_array($expected);
        change_array($got);
        $changed = value_diff($expected, $got);
    }

    $readAgain = value_diff($make(), ucache_get($key));

    check($name, $set === true && $success === true && $readBack === null && $changed === null && $readAgain === null,
          ['set' => $set, 'success' => $success, 'read back' => $readBack, 'after the same changes' => $changed,
           'read again' => $readAgain]);
}

/* An array with $count string keys, holding integers, strings, floats and small arrays */
function string_keys($count)
{
    $array = [];
    for ($i = 0; $i < $count; $i++) {
        $values = [$i, "value $i", $i + 0.5, ['nested' => $i]];
        $array["key $i"] = $values[$i % 4];
    }

    return $array;
}

/* Growth of memory_get_usage() from the 100th to the 2000th ucache_get($key) ($key can be an array of
   keys), each result being dropped */
function get_growth($key)
{
    for ($i = 0; $i < 100; $i++) {
        $value = ucache_get($key);
        unset($value);
    }
    gc_collect_cycles();
    $before = memory_get_usage();

    for (; $i < 2000; $i++) {
        $value = ucache_get($key);
        unset($value);
    }
    gc_collect_cycles();

    return memory_get_usage() - $before;
}

/* Shared memory (ucache_meminfo() memory_free) that 200 calls of $round lose, after one call to warm up */
function shm_loss($round)
{
    $round();
    $before = ucache_meminfo()['memory_free'];

    for ($i = 0; $i < 200; $i++) {
        $round();
    }

    return $before - ucache_meminfo()['memory_free'];
}

/* Growth of memory_get_usage() from the 100th to the 2000th call of $call($success), $success holding a
   new array each time: the function that assigns the bool to $success must release the array */
function success_growth($call)
{
    for ($i = 0; $i < 100; $i++) {
        $success = [$i, fresh_string('s', 200)];
        $call($success);
    }
    unset($success);
    gc_collect_cycles();
    $before = memory_get_usage();

    for (; $i < 2000; $i++) {
        $success = [$i, fresh_string('s', 200)];
        $call($success);
    }
    unset($success);
    gc_collect_cycles();

    return memory_get_usage() - $before;
}

/* An INI size ("128M", "-1", ...) in bytes */
function ini_bytes($value)
{
    $value = trim((string)$value);
    $bytes = (int)$value;
    switch (strtolower(substr($value, -1))) {
        case 'g':
            $bytes *= 1024;
            /* no break */
        case 'm':
            $bytes *= 1024;
            /* no break */
        case 'k':
            $bytes *= 1024;
    }

    return $bytes;
}

/* The exact representation of a number, or of each number in an array: tells -0.0 from 0.0, and NAN is NAN */
function bits($value)
{
    if (is_array($value)) {
        return array_map('bits', $value);
    }
    if (is_float($value)) {
        return is_nan($value) ? 'float NAN' : 'float ' . bin2hex(pack('e', $value));
    }

    return gettype($value) . ' ' . dump($value);
}

function ucache_smoke_generator()
{
    yield 1;
}

/* Constant arrays, written in the source (opcache makes such arrays immutable) */
const UCACHE_SMOKE_CONST = ['list' => [1, 2, 3], 'map' => ['a' => 'b', 'c' => [true, null]], 'empty' => []];

class UcacheSmokeConstants
{
    const TABLE = [10 => 'ten', 'twenty' => 20, 'list' => ['x', 'y'], 'pi' => 3.14];
}

echo "=== ucache smoke test ===\n";

/*
 * Extension, functions and INI settings
 */

$loaded = extension_loaded('ucache');
check("extension 'ucache' is loaded", $loaded);
if (!$loaded) {
    exit(1);
}

$version = phpversion('ucache');
check("phpversion('ucache') is a version string ($version)", is_string($version) && preg_match('/^\d+(\.\d+)+$/', $version) === 1, $version);

$expectedFuncs = ['ucache_add', 'ucache_cas', 'ucache_clear', 'ucache_dec', 'ucache_delete', 'ucache_exists',
                  'ucache_get', 'ucache_inc', 'ucache_info', 'ucache_meminfo', 'ucache_set'];
$testOnlyFuncs = ['ucache_lasterror', 'ucache_runtests'];   /* only in UCACHE_TEST (debug) builds */

$funcs = get_extension_funcs('ucache');
$funcs = is_array($funcs) ? $funcs : [];
$missing = array_values(array_diff($expectedFuncs, $funcs));
$extra   = array_values(array_diff($funcs, $expectedFuncs, $testOnlyFuncs));
check('all 11 ucache_* functions are exported', $missing === [], $missing);
check('no other functions are exported', $extra === [], $extra);
if ($missing !== []) {
    exit(1);
}

$expectedIni = [
    /* name                => [default, access] */
    'ucache.enabled'      => ['1',  INI_ALL],
    'ucache.enablecli'    => ['0',  INI_SYSTEM],
    'ucache.size'         => ['8',  INI_SYSTEM],
    'ucache.namesalt'     => ['',   INI_SYSTEM],
    'ucache.apppoolid'    => ['',   INI_SYSTEM],
    'ucache.filemapdir'   => ['',   INI_SYSTEM],
    'ucache.debuglevel'   => ['0',  INI_ALL],
    'ucache.gc.threshold' => ['0',  INI_SYSTEM],
    'ucache.gc.interval'  => ['10', INI_SYSTEM],
    'ucache.gc.step'      => ['0',  INI_SYSTEM],
    'ucache.gc.ttl'       => ['0',  INI_SYSTEM],
    'ucache.gc.evict'     => ['0',  INI_SYSTEM],
    'ucache.prefix'       => ['ucache_', INI_SYSTEM],
];

$ini = ini_get_all('ucache');
$ini = is_array($ini) ? $ini : [];
$names = array_keys($ini);
$expectedNames = array_keys($expectedIni);
sort($names);
sort($expectedNames);
check('INI settings are exactly the 13 ucache.* settings', $names === $expectedNames, $names);

$badAccess = [];
foreach ($expectedIni as $name => $spec) {
    if (isset($ini[$name]) && $ini[$name]['access'] !== $spec[1]) {
        $badAccess[$name] = $ini[$name]['access'];
    }
}
check('INI settings have the expected modifiable levels', $badAccess === [], $badAccess);

/* Defaults can only be checked when no php.ini is loaded (build.bat runs php -n), and not for the
   settings given on the command line (-d ...), which get_cfg_var() returns: ucache.enablecli, and any
   other setting that keeps the test away from another cache */
if (php_ini_loaded_file() === false) {
    $badDefault = [];
    foreach ($expectedIni as $name => $spec) {
        if (get_cfg_var($name) === false && isset($ini[$name]) && (string)$ini[$name]['global_value'] !== $spec[0]) {
            $badDefault[$name] = $ini[$name]['global_value'];
        }
    }
    check('INI settings have the expected defaults', $badDefault === [], $badDefault);
}

if (PHP_SAPI === 'cli' && ini_get('ucache.enablecli') !== '1') {
    check('ucache.enablecli=1 (needed to use the cache from the CLI)', false, ini_get('ucache.enablecli'));
    exit(1);
}

/*
 * Start from an empty cache
 */

check('ucache_clear() returns true', ucache_clear() === true);
$info = ucache_info(true);
check('the cache is empty after ucache_clear()', is_array($info) && $info['total_item_count'] === 0, $info);
/* What ucache_info() says about the scavenger now: another process may have used this cache before */
$scavengerAtStart = is_array($info) ? [$info['last_scavenge_seconds'], $info['last_scavenge_count'], $info['total_scavenged_count'],
                                       $info['total_evicted_count']] : null;

/*
 * ucache_set / ucache_get round trips
 */

$values = [
    'smoke_long'   => 42,
    'smoke_double' => 3.25,
    'smoke_string' => "binary\0safe string",
    'smoke_true'   => true,
    'smoke_false'  => false,
    'smoke_null'   => null,
    'smoke_array'  => ['a' => 1, 'b' => [2, 'x', ['deep' => true]], 7 => 'seven', 'f' => 1.5],
];

foreach ($values as $key => $value) {
    $set = ucache_set($key, $value);
    $success = 'unset';
    $got = ucache_get($key, $success);
    check("set/get $key (" . gettype($value) . ')', $set === true && $got === $value && $success === true,
          ['set' => $set, 'get' => $got, 'success' => $success]);
}

$point = new UcacheSmokePoint(1, 'two');
$set = ucache_set('smoke_object', $point);
$success = 'unset';
$got = ucache_get('smoke_object', $success);
check('set/get smoke_object (object comes back as an equal copy)',
      $set === true && $got instanceof UcacheSmokePoint && $got == $point && $success === true, $got);

$mixed = ['p' => $point, 'list' => [1, 2, 3]];
$set = ucache_set('smoke_mixed', $mixed);
$got = ucache_get('smoke_mixed');
check('set/get an array holding an object',
      $set === true && is_array($got) && isset($got['p']) && $got['p'] instanceof UcacheSmokePoint
      && $got['p'] == $point && $got['list'] === [1, 2, 3], $got);

check('a non-string key is converted to a string',
      ucache_set(12345, 'int key') === true && ucache_get('12345') === 'int key' && ucache_get(12345) === 'int key');

/* An object without __toString() cannot be converted: PHP 7.4 and later throw an Error. PHP 7.2 and 7.3 raise an
   E_RECOVERABLE_ERROR, which this test's error handler catches (they then use the key "Object") */
$objectKey = new stdClass();
$bad = [];
foreach (['ucache_get' => [$objectKey], 'ucache_set' => [$objectKey, 1], 'ucache_delete' => [$objectKey]] as $fn => $args) {
    $r = capture(function () use ($fn, $args) {
        return attempt(function () use ($fn, $args) { return call_user_func_array($fn, $args); });
    }, $raised);
    $ok = (PHP_VERSION_ID >= 70400)
        ? ($r[1] instanceof Error && strpos($r[1]->getMessage(), 'could not be converted to string') !== false && $raised === [])
        : ($r[1] === null && warned($raised, 'could not be converted to string'));
    if (!$ok) {
        $bad[$fn] = ['result' => outcome($r), 'errors' => $raised];
    }
}
ucache_delete('Object');
check('an object key without __toString() throws an Error (PHP 7.4+) or raises E_RECOVERABLE_ERROR (PHP 7.2 and 7.3)', $bad === [], $bad);

check('ucache_set() overwrites an existing key', ucache_set('smoke_long', 43) === true && ucache_get('smoke_long') === 43);

$success = 'unset';
$got = ucache_get('smoke_missing', $success);
check('get of a missing key returns false and sets $success to false', $got === false && $success === false,
      ['get' => $got, 'success' => $success]);

/*
 * ucache_exists
 */

check('ucache_exists() is true for a stored key', ucache_exists('smoke_long') === true);
check('ucache_exists() is true for a key holding false', ucache_exists('smoke_false') === true);
check('ucache_exists() is false for a missing key', ucache_exists('smoke_missing') === false);

/*
 * Invalid ucache_set arguments
 */

/* Optional in the argument parsing ("z|zl"), so PHP 8 does not throw ArgumentCountError either */
$r = attempt(function () { return ucache_set('smoke_novalue'); });
check('set with a string key and no value returns false', $r === [false, null], outcome($r));

$r = capture(function () { return ucache_set('', 1); }, $raised);
check('set with a blank key returns false with a warning', $r === false && warned($raised, 'key cannot be blank string'), $raised);

check('set with a key longer than 4096 characters returns false', ucache_set(str_repeat('k', 4097), 1) === false);

$res = fopen('php://memory', 'r');
$r = capture(function () use ($res) { return ucache_set('smoke_resource', $res); }, $raised);
check('set of a resource returns false with a warning', $r === false && warned($raised, 'value cannot be a resource'), $raised);

$r = capture(function () { return ucache_set('smoke_negttl', 1, -1); }, $raised);
check('set with a negative ttl returns false with a warning',
      $r === false && warned($raised, 'ttl cannot be less than 0') && ucache_exists('smoke_negttl') === false, $raised);

/* A ttl is kept as given up to 4294967295, the entry's unsigned int, and counts as that above it: cut to 32 bits,
   2147483648 was refused as negative and 4294967297 meant one second */
$bigTtl = [];
foreach ([2147483648, 4294967295, 4294967297, PHP_INT_MAX] as $ttl) {
    $r = capture(function () use ($ttl) { return ucache_set("smoke_bigttl_$ttl", 'v', $ttl); }, $raised);
    $entry = find_entry(ucache_info(false, "smoke_bigttl_$ttl"), "smoke_bigttl_$ttl");
    $bigTtl[$ttl] = [$r, $raised === [], ($entry !== null) ? $entry['ttl_seconds'] : null, ucache_get("smoke_bigttl_$ttl")];
    ucache_delete("smoke_bigttl_$ttl");
}
check('a ttl above 2147483647 is kept as given up to 4294967295, and counts as 4294967295 above it',
      $bigTtl === [2147483648 => [true, true, 2147483648, 'v'], 4294967295 => [true, true, 4294967295, 'v'],
                   4294967297 => [true, true, 4294967295, 'v'], PHP_INT_MAX => [true, true, 4294967295, 'v']], $bigTtl);

/*
 * TTL (the expiry itself is checked further down, after a pause)
 */

$set = ucache_set('smoke_ttl', 'short-lived', 1);
$entry = find_entry(ucache_info(false, 'smoke_ttl'), 'smoke_ttl');
check('set with ttl 1 stores the entry with ttl_seconds 1', $set === true && $entry !== null && $entry['ttl_seconds'] === 1, $entry);
/* Two more for the checks after the pause: one stored again, one deleted once it has expired */
ucache_set('smoke_ttl_listed', 'short-lived', 1);
ucache_set('smoke_ttl_delete', 'short-lived', 1);
$ttlSetAt = microtime(true);

/*
 * Arrays of keys
 */

$r = ucache_set(['smoke_m1' => 1, 'smoke_m2' => 'two', 'smoke_m3' => $res]);
check('set with an array returns the keys that failed, with value -1', $r === ['smoke_m3' => -1], $r);

$r = ucache_set(['smoke_mttl' => 'with ttl'], null, 300);
$entry = find_entry(ucache_info(false, 'smoke_mttl'), 'smoke_mttl');
check('set with an array, null and a ttl stores the keys with that ttl', $r === [] && $entry !== null && $entry['ttl_seconds'] === 300,
      ['set' => $r, 'entry' => $entry]);

$success = 'unset';
$r = ucache_get(['smoke_m1', 'smoke_m2', 'smoke_missing'], $success);
check('get with an array returns only the keys found, and $success true', $r === ['smoke_m1' => 1, 'smoke_m2' => 'two'] && $success === true,
      ['get' => $r, 'success' => $success]);

$r = ucache_delete(['smoke_m1', 'smoke_missing', 'smoke_m2']);
check('delete with an array returns the keys that were deleted',
      $r === ['smoke_m1', 'smoke_m2'] && ucache_exists('smoke_m1') === false && ucache_exists('smoke_m2') === false, $r);

/* Integer keys in the array forms use their full decimal string, the same key as the scalar forms */
$intKeys = [7, -7, 2147483648, -2147483649, 4294967296, PHP_INT_MAX, PHP_INT_MIN];
$r = ucache_set(array_fill_keys($intKeys, 'int key'));
$missing = [];
foreach ($intKeys as $k) {
    if (ucache_get((string)$k) !== 'int key') {
        $missing[] = $k;
    }
}
check('set with an array of integer keys stores each one under its decimal string', $r === [] && $missing === [],
      ['set' => $r, 'missing' => $missing]);

$r = ucache_get($intKeys);
$expected = [];
foreach ($intKeys as $k) {
    $expected[(string)$k] = 'int key';   /* keyed the way PHP itself keys an array */
}
check('get with an array of integer keys returns them as integer keys', $r === $expected && $r[PHP_INT_MIN] === 'int key', $r);

ucache_set(['07' => 'zero seven', '-0' => 'minus zero']);
$r = ucache_get(['07', '-0', 'smoke_missing']);
check('get with an array keeps keys such as "07" and "-0" as strings, as PHP arrays do',
      $r === ['07' => 'zero seven', '-0' => 'minus zero'] && array_keys($r) === ['07', '-0'], $r);
ucache_delete(['07', '-0']);

$r = ucache_add(array_fill_keys($intKeys, 'other'));
check('add with an array of integer keys sees each one as existing',
      is_array($r) && count($r) === count($intKeys) && ucache_get((string)PHP_INT_MAX) === 'int key', $r);

$r = ucache_delete($intKeys);
check('delete with an array of integer keys deletes each one and leaves the array as it was',
      $r === array_map('strval', $intKeys) && $intKeys === [7, -7, 2147483648, -2147483649, 4294967296, PHP_INT_MAX, PHP_INT_MIN], $r);

/*
 * ucache_delete
 */

ucache_set('smoke_delete', 'bye');
check('ucache_delete() of a stored key returns true', ucache_delete('smoke_delete') === true && ucache_exists('smoke_delete') === false);
check('ucache_delete() of a missing key returns false', ucache_delete('smoke_delete') === false);

/*
 * ucache_add
 */

check('ucache_add() of a new key returns true', ucache_add('smoke_add', 'first') === true && ucache_get('smoke_add') === 'first');

$r = capture(function () { return ucache_add('smoke_add', 'second'); }, $raised);
check('ucache_add() of an existing key returns false with a warning and keeps the value',
      $r === false && warned($raised, 'key which already exists') && ucache_get('smoke_add') === 'first', $raised);

$r = ucache_add(['smoke_add' => 'x', 'smoke_add2' => 'y']);
check('add with an array returns the keys that already existed, with value -1',
      $r === ['smoke_add' => -1] && ucache_get('smoke_add') === 'first' && ucache_get('smoke_add2') === 'y', $r);

$set = ucache_add('smoke_addttl', 'v', 300);
$entry = find_entry(ucache_info(false, 'smoke_addttl'), 'smoke_addttl');
check('ucache_add() with a ttl stores the entry with that ttl', $set === true && $entry !== null && $entry['ttl_seconds'] === 300, $entry);

$r = capture(function () { return ucache_add('smoke_negttl', 1, -1); }, $raised);
check('ucache_add() with a negative ttl returns false with a warning',
      $r === false && warned($raised, 'ttl cannot be less than 0') && ucache_exists('smoke_negttl') === false, $raised);

/*
 * ucache_inc / ucache_dec
 */

ucache_set('smoke_counter', 10);
check('ucache_inc() without a step adds 1', ucache_inc('smoke_counter') === 11);
$success = 'unset';
check('ucache_inc() with a step and $success', ucache_inc('smoke_counter', 5, $success) === 16 && $success === true);
check('ucache_dec() without a step subtracts 1', ucache_dec('smoke_counter') === 15);
$success = 'unset';
check('ucache_dec() with a step and $success', ucache_dec('smoke_counter', 20, $success) === -5 && $success === true);
check('the changed counter is stored', ucache_get('smoke_counter') === -5);

foreach (['ucache_inc', 'ucache_dec'] as $fn) {
    $success = 'unset';
    $r = capture(function () use ($fn, &$success) { return $fn('smoke_nocounter', 1, $success); }, $raised);
    check("$fn() of a missing key returns false and sets \$success to false",
          $r === false && $success === false && $raised === [], ['result' => $r, 'success' => $success, 'warnings' => $raised]);

    $success = 'unset';
    $r = capture(function () use ($fn, &$success) { return $fn('smoke_string', 1, $success); }, $raised);
    check("$fn() of a non-long value returns false with a warning",
          $r === false && $success === false && warned($raised, 'value is long'), ['result' => $r, 'success' => $success, 'warnings' => $raised]);
}

/*
 * $success is assigned through the reference it is passed by. Since PHP 7.4 that can be a reference to a
 * typed property, which converts the bool to its type (this file does not declare strict_types) or
 * throws a TypeError. ZEND_TRY_ASSIGN_REF_BOOL would store true in such a property whatever the result.
 */
if (PHP_VERSION_ID >= 70400) {
    eval('class UcacheSmokeSuccess { public ?bool $flag = null; public int $count = 7; public array $list = []; }');   /* typed properties do not parse before 7.4 */
    $typedSuccess = new UcacheSmokeSuccess();
    ucache_set('smoke_typed_counter', 10);
    $got = [];
    ucache_get('smoke_missing', $typedSuccess->flag);
    $got['?bool, get of a missing key'] = $typedSuccess->flag;
    ucache_get('smoke_long', $typedSuccess->flag);
    $got['?bool, get of a stored key'] = $typedSuccess->flag;
    $typedSuccess->flag = null;
    ucache_inc('smoke_nocounter', 1, $typedSuccess->flag);
    $got['?bool, inc of a missing key'] = $typedSuccess->flag;
    ucache_inc('smoke_typed_counter', 1, $typedSuccess->flag);
    $got['?bool, inc of a counter'] = $typedSuccess->flag;
    ucache_get('smoke_missing', $typedSuccess->count);
    $got['int, get of a missing key'] = $typedSuccess->count;
    ucache_get('smoke_long', $typedSuccess->count);
    $got['int, get of a stored key'] = $typedSuccess->count;
    $typedSuccess->count = 7;
    ucache_dec('smoke_nocounter', 1, $typedSuccess->count);
    $got['int, dec of a missing key'] = $typedSuccess->count;
    ucache_dec('smoke_typed_counter', 1, $typedSuccess->count);
    $got['int, dec of a counter'] = $typedSuccess->count;
    $expected = ['?bool, get of a missing key' => false, '?bool, get of a stored key' => true, '?bool, inc of a missing key' => false,
                 '?bool, inc of a counter' => true, 'int, get of a missing key' => 0, 'int, get of a stored key' => 1,
                 'int, dec of a missing key' => 0, 'int, dec of a counter' => 1];
    check('a typed property given as $success gets false or true, converted to its type (PHP 7.4+)', $got === $expected, $got);

    $inc = attempt(function () use ($typedSuccess) { return ucache_inc('smoke_typed_counter', 1, $typedSuccess->list); });
    $get = attempt(function () use ($typedSuccess) { return ucache_get('smoke_long', $typedSuccess->list); });
    check('a typed property that cannot hold a bool, given as $success, makes the call throw a TypeError and leaves the counter unchanged (PHP 7.4+)',
          $inc[1] instanceof TypeError && $get[1] instanceof TypeError && $typedSuccess->list === [] && ucache_get('smoke_typed_counter') === 10,
          ['inc' => outcome($inc), 'get' => outcome($get), 'counter' => ucache_get('smoke_typed_counter')]);
    ucache_delete('smoke_typed_counter');
}

/*
 * ucache_cas
 */

ucache_set('smoke_cas', 100);
check('ucache_cas() with the current value stores the new value', ucache_cas('smoke_cas', 100, 200) === true && ucache_get('smoke_cas') === 200);

$r = capture(function () { return ucache_cas('smoke_cas', 100, 300); }, $raised);
check('ucache_cas() with another value returns false and keeps the value',
      $r === false && $raised === [] && ucache_get('smoke_cas') === 200, $raised);

$r = capture(function () { return ucache_cas('smoke_nocas', 1, 2); }, $raised);
check('ucache_cas() of a missing key returns false', $r === false && $raised === [], $raised);

$r = capture(function () { return ucache_cas('smoke_string', 1, 2); }, $raised);
check('ucache_cas() of a non-long value returns false with a warning', $r === false && warned($raised, 'value is long'), $raised);

/*
 * Parameters. The array forms of ucache_set() and ucache_add() take no value, so only the key is
 * required. On PHP 8 the optional parameters have defaults, so that named arguments can skip them.
 */

$expectedRequired = ['ucache_add' => 1, 'ucache_cas' => 3, 'ucache_clear' => 0, 'ucache_dec' => 1, 'ucache_delete' => 1,
                     'ucache_exists' => 1, 'ucache_get' => 1, 'ucache_inc' => 1, 'ucache_info' => 0,
                     'ucache_meminfo' => 0, 'ucache_set' => 1];
$required = [];
foreach ($expectedRequired as $fn => $count) {
    $required[$fn] = (new ReflectionFunction($fn))->getNumberOfRequiredParameters();
}
check('Reflection gives the number of required parameters (1 for ucache_set() and ucache_add())', $required === $expectedRequired, $required);

if (PHP_VERSION_ID >= 80000) {
    ucache_set('smoke_named_counter', 1);
    ucache_delete(['smoke_named_ttl', 'smoke_named_novalue']);
    $named = eval('return [
        "set" => attempt(function () { return ucache_set(["smoke_named_ttl" => 1], ttl: 5); }),
        "inc" => attempt(function () { $success = "unset"; return [ucache_inc("smoke_named_counter", success: $success), $success]; }),
        "info" => attempt(function () { return ucache_info(key: "smoke_long"); }),
        "page" => attempt(function () { return ucache_info(offset: 1, limit: 2); }),
        "novalue" => attempt(function () { return ucache_set("smoke_named_novalue", ttl: 60); }),
    ];');   /* named arguments do not parse before 8.0 */

    $entry = find_entry(ucache_info(false, 'smoke_named_ttl'), 'smoke_named_ttl');
    check('ucache_set($values, ttl: 5) stores the values with ttl_seconds 5 (PHP 8)',
          $named['set'] === [[], null] && $entry !== null && $entry['ttl_seconds'] === 5, ['set' => outcome($named['set']), 'entry' => $entry]);

    check('ucache_inc($key, success: $success) adds 1 and sets $success to true (PHP 8)', $named['inc'] === [[2, true], null], outcome($named['inc']));

    $info = $named['info'][0];
    check('ucache_info(key: $key) returns only that entry (PHP 8)',
          is_array($info) && isset($info['ucache_entries']) && count($info['ucache_entries']) === 1 && find_entry($info, 'smoke_long') !== null,
          outcome($named['info']));

    $info = $named['page'][0];
    check('ucache_info(offset: 1, limit: 2) returns the 2nd and 3rd entries, indexed 2 and 3 (PHP 8)',
          is_array($info) && isset($info['ucache_entries']) && array_keys($info['ucache_entries']) === [2, 3],
          outcome($named['page']));

    /* The value takes its default, null: as documented, the call stores null (positionally, ucache_set('k') fails) */
    $entry = find_entry(ucache_info(false, 'smoke_named_novalue'), 'smoke_named_novalue');
    $success = 'unset';
    $got = ucache_get('smoke_named_novalue', $success);
    check('ucache_set($key, ttl: 60) stores null with ttl_seconds 60 (PHP 8)',
          $named['novalue'] === [true, null] && $got === null && $success === true && $entry !== null && $entry['value_type'] === 'null'
          && $entry['ttl_seconds'] === 60, ['set' => outcome($named['novalue']), 'entry' => $entry]);
    ucache_delete(['smoke_named_counter', 'smoke_named_ttl', 'smoke_named_novalue']);
}

/*
 * ucache_info
 */

$info = ucache_info();
check('ucache_info() returns the expected keys', is_array($info) && array_keys($info) ===
      ['total_cache_uptime', 'is_local_cache', 'total_item_count', 'total_hit_count', 'total_miss_count', 'last_scavenge_seconds',
       'last_scavenge_count', 'total_scavenged_count', 'total_evicted_count', 'ucache_entries'],
      is_array($info) ? array_keys($info) : $info);
check('the user cache is not a local cache', $info['is_local_cache'] === false, $info['is_local_cache']);
/* Writes do not run the scavenger here (ucache.gc.threshold 0), none has run out of shared memory yet (which runs a full
   scavenger pass), and the end of the request is still to come: the counts are those at the start, and the seconds since
   the last step are still null, or have grown */
check('ucache_info() reports no scavenger step during this request: last_scavenge_count, total_scavenged_count and total_evicted_count as at the start, last_scavenge_seconds still null or not smaller',
      is_array($scavengerAtStart) && $info['last_scavenge_count'] === $scavengerAtStart[1] && $info['total_scavenged_count'] === $scavengerAtStart[2]
      && $info['total_evicted_count'] === $scavengerAtStart[3]
      && ($scavengerAtStart[0] === null ? $info['last_scavenge_seconds'] === null
                                        : is_int($info['last_scavenge_seconds']) && $info['last_scavenge_seconds'] >= $scavengerAtStart[0]),
      ['at the start' => $scavengerAtStart,
       'now' => [$info['last_scavenge_seconds'], $info['last_scavenge_count'], $info['total_scavenged_count'], $info['total_evicted_count']]]);

$entries = $info['ucache_entries'];
check('ucache_entries is indexed from 1 and has total_item_count entries',
      $entries !== [] && array_keys($entries) === range(1, count($entries)) && count($entries) === $info['total_item_count'],
      ['keys' => array_keys($entries), 'total_item_count' => $info['total_item_count']]);

$entry = find_entry($info, 'smoke_array');
check('an entry has key_name, value_type, value_size, ttl_seconds, age_seconds, hitcount',
      $entry !== null && array_keys($entry) === ['key_name', 'value_type', 'value_size', 'ttl_seconds', 'age_seconds', 'hitcount']
      && $entry['value_size'] > 0, $entry);

$badTypes = [];
$types = ['smoke_long' => 'long', 'smoke_double' => 'double', 'smoke_string' => 'string', 'smoke_true' => 'bool',
          'smoke_null' => 'null', 'smoke_array' => 'array', 'smoke_object' => 'object'];
foreach ($types as $key => $type) {
    $entry = find_entry($info, $key);
    if ($entry === null || $entry['value_type'] !== $type) {
        $badTypes[$key] = ($entry === null) ? null : $entry['value_type'];
    }
}
check('value_type names the stored type', $badTypes === [], $badTypes);

$summary = ucache_info(true);
check('ucache_info(true) returns the totals without entries',
      is_array($summary) && $summary['ucache_entries'] === [] && $summary['total_item_count'] === $info['total_item_count'], $summary);

$one = ucache_info(false, 'smoke_long');
$entry = find_entry($one, 'smoke_long');
check('ucache_info(false, key) returns only that entry',
      is_array($one) && count($one['ucache_entries']) === 1 && isset($one['ucache_entries'][1]) && $entry !== null
      && $entry['value_type'] === 'long' && $entry['ttl_seconds'] === 0, $one);

$one = ucache_info(true, 'smoke_long');
check('ucache_info(true, key) still returns that entry', is_array($one) && count($one['ucache_entries']) === 1, $one);

$none = ucache_info(false, 'smoke_missing');
check('ucache_info(false, missing key) returns no entries', is_array($none) && $none['ucache_entries'] === [], $none);

/* offset and limit: a page of the full listing. The keys and their positions are compared, not the whole entries,
   whose age_seconds may have grown by a second since $info */
function entry_keys($info)
{
    return is_array($info) ? array_map(function ($entry) { return $entry['key_name']; }, $info['ucache_entries']) : $info;
}

$fullKeys = entry_keys($info);
$page = ucache_info(false, null, 2, 3);
check('ucache_info(false, null, 2, 3) returns the 3rd to 5th entries of the full listing, indexed 3 to 5, with the totals of the whole cache',
      count($fullKeys) >= 5 && entry_keys($page) === array_slice($fullKeys, 2, 3, true)
      && $page['total_item_count'] === $info['total_item_count'], ['page' => entry_keys($page), 'full' => $fullKeys]);

check('ucache_info(false, null) and ucache_info(false, null, 0, 0) list every entry',
      entry_keys(ucache_info(false, null)) === $fullKeys && entry_keys(ucache_info(false, null, 0, 0)) === $fullKeys,
      [entry_keys(ucache_info(false, null)), entry_keys(ucache_info(false, null, 0, 0))]);

$total = $info['total_item_count'];
$tail = ucache_info(false, null, $total - 1, 10);
$past = ucache_info(false, null, $total, 10);
$far = ucache_info(false, null, PHP_INT_MAX, 1);
check('a limit past the end returns the entries left, an offset at or past the end returns none',
      entry_keys($tail) === array_slice($fullKeys, $total - 1, 1, true) && is_array($past) && $past['ucache_entries'] === []
      && $past['total_item_count'] === $total && is_array($far) && $far['ucache_entries'] === [],
      ['tail' => entry_keys($tail), 'past' => entry_keys($past), 'far' => entry_keys($far)]);

$one = ucache_info(false, 'smoke_long', 5, 1);
check('with a key, offset and limit are ignored', entry_keys($one) === [1 => 'smoke_long'], entry_keys($one));

$summary = ucache_info(true, null, 0, 3);
check('ucache_info(true, null, 0, 3) still returns no entries', is_array($summary) && $summary['ucache_entries'] === [], entry_keys($summary));

$r = capture(function () { return ucache_info(false, null, -1); }, $raised);
$r2 = capture(function () { return ucache_info(false, null, 0, -1); }, $raised2);
check('ucache_info() with a negative offset or limit returns false with a warning',
      $r === false && warned($raised, 'cannot be less than 0') && $r2 === false && warned($raised2, 'cannot be less than 0'),
      [$r, $raised, $r2, $raised2]);

$before = ucache_info(true);
$hitsBefore = find_entry(ucache_info(false, 'smoke_long'), 'smoke_long')['hitcount'];
ucache_get('smoke_long');
ucache_get('smoke_missing');
$after = ucache_info(true);
$hitsAfter = find_entry(ucache_info(false, 'smoke_long'), 'smoke_long')['hitcount'];
check('a hit and a miss are counted',
      $after['total_hit_count'] === $before['total_hit_count'] + 1 && $after['total_miss_count'] === $before['total_miss_count'] + 1
      && $hitsAfter === $hitsBefore + 1,
      ['before' => $before, 'after' => $after, 'entry hitcount' => [$hitsBefore, $hitsAfter]]);

/*
 * ucache_meminfo
 */

$m1 = ucache_meminfo();
check('ucache_meminfo() returns the expected keys', is_array($m1) && array_keys($m1) ===
      ['memory_total', 'memory_free', 'num_used_blks', 'num_free_blks', 'memory_overhead'], $m1);
check('ucache_meminfo() values are consistent',
      is_array($m1) && is_int($m1['memory_total']) && is_int($m1['memory_free']) && $m1['memory_total'] > 0
      && $m1['memory_free'] > 0 && $m1['memory_free'] < $m1['memory_total'] && $m1['num_used_blks'] > 0, $m1);

ucache_set('smoke_big', str_repeat('x', 100000));
$m2 = ucache_meminfo();
ucache_delete('smoke_big');
$m3 = ucache_meminfo();
check('storing 100000 bytes takes shared memory and deleting them gives it back',
      $m1['memory_free'] - $m2['memory_free'] >= 100000 && $m3['memory_free'] - $m2['memory_free'] >= 100000,
      [$m1['memory_free'], $m2['memory_free'], $m3['memory_free']]);

/*
 * ucache.enabled can be switched off at runtime
 */

$old = ini_set('ucache.enabled', '0');
$r = [ucache_get('smoke_long'), ucache_set('smoke_off', 1), ucache_exists('smoke_long')];

/* The arguments are still checked first: PHP 8 throws ArgumentCountError or TypeError, PHP 7 warns */
$badCalls = [
    'ucache_get()'                    => ['ArgumentCountError', function () { return ucache_get(); }],
    'ucache_set()'                    => ['ArgumentCountError', function () { return ucache_set(); }],
    "ucache_get('k', \$success, 1)"   => ['ArgumentCountError', function () { return ucache_get('smoke_long', $success, 1); }],
    'ucache_exists([])'               => ['TypeError', function () { return ucache_exists([]); }],
    "ucache_inc('k', 'x')"            => ['TypeError', function () { return ucache_inc('smoke_long', 'x'); }],
    "ucache_dec('k', 'x')"            => ['TypeError', function () { return ucache_dec('smoke_long', 'x'); }],
    'ucache_clear(1)'                 => ['ArgumentCountError', function () { return ucache_clear(1); }],
];
$badArgs = [];
foreach ($badCalls as $call => $case) {
    $attempt = capture(function () use ($case) { return attempt($case[1]); }, $raised);
    $ok = (PHP_VERSION_ID >= 80000)
        ? ($attempt[1] !== null && get_class($attempt[1]) === $case[0] && $raised === [])
        : ($attempt === [false, null] && warned($raised, 'expects'));
    if (!$ok) {
        $badArgs[$call] = ['result' => outcome($attempt), 'warnings' => $raised];
    }
}

ini_set('ucache.enabled', ($old === false) ? '1' : $old);
check('with ucache.enabled=0 the cache functions return false',
      $old === '1' && $r === [false, false, false] && ucache_exists('smoke_off') === false && ucache_get('smoke_long') === 43,
      ['old' => $old, 'results' => $r]);
check('with ucache.enabled=0 invalid arguments still throw ArgumentCountError or TypeError (PHP 8) or warn (PHP 7)',
      $badArgs === [] && ucache_get('smoke_long') === 43, $badArgs);

/*
 * TTL expiry. An expired entry cannot be read, and the calls that look a key up (get, exists, set, add,
 * delete, inc, dec, cas) remove the expired entries they pass in the key's hash bucket, the key's own
 * included. ucache_info() removes nothing: an expired entry that no call has looked up stays listed
 * until the scavenger removes it, at the end of requests (ucache.gc.interval), on writes (with
 * ucache.gc.threshold, 0 here) or when a write runs out of shared memory, none of which happens in this
 * process before these checks. The three keys are in different hash buckets of the default cache (16381
 * buckets), so each call below removes only the entry of its own key. The checks with several processes
 * look at the scavenger.
 */

$wait = 2.5 - (microtime(true) - $ttlSetAt);
if ($wait > 0) {
    usleep((int)($wait * 1000000));
}

/* First, before any call looks a key up */
$info = ucache_info();
$listed = [];
foreach (['smoke_ttl', 'smoke_ttl_listed', 'smoke_ttl_delete'] as $key) {
    $entry = find_entry($info, $key);
    $listed[$key] = ($entry === null) ? null : $entry['ttl_seconds'];
}
check('expired entries that no call has looked up are still listed by ucache_info(), with ttl_seconds 1',
      $listed === ['smoke_ttl' => 1, 'smoke_ttl_listed' => 1, 'smoke_ttl_delete' => 1], ['ttl_seconds of each key (null: not listed)' => $listed]);

$freeBefore = ucache_meminfo()['memory_free'];
$success = 'unset';
$r = ucache_get('smoke_ttl', $success);
$freeAfter = ucache_meminfo()['memory_free'];
$entry = find_entry(ucache_info(), 'smoke_ttl');
check('an entry cannot be read once its ttl expired', $r === false && $success === false && ucache_exists('smoke_ttl') === false,
      ['get' => $r, 'success' => $success]);
check('ucache_get() of an expired entry removes it, and its memory is free when the call returns: ucache_info() no longer lists it, and memory_free has grown',
      $entry === null && $freeAfter > $freeBefore, ['entry listed' => $entry, 'memory_free before and after' => [$freeBefore, $freeAfter]]);

$set = ucache_set('smoke_ttl_listed', 'again');
$entries = find_entries(ucache_info(), 'smoke_ttl_listed');
check('ucache_set() of a key whose entry has expired replaces that entry: ucache_info() lists the key once, with ttl_seconds 0',
      $set === true && count($entries) === 1 && $entries[0]['ttl_seconds'] === 0 && ucache_get('smoke_ttl_listed') === 'again',
      ['set' => $set, 'entries listed for the key' => $entries]);

$deleted = ucache_delete('smoke_ttl_delete');
$entry = find_entry(ucache_info(), 'smoke_ttl_delete');
check('ucache_delete() of an expired entry returns false (the key is missing) and removes the entry: ucache_info() no longer lists it',
      $deleted === false && $entry === null, ['delete' => $deleted, 'entry listed' => $entry]);

/*
 * Arrays. ucache_zvcache.c copies a value into the shared memory (copyin_*) and back into the request
 * (copyout_*), so it has to follow the HashTable layout of the PHP version: packed arrays hold zvals
 * instead of Buckets since PHP 8.2, the hash part is twice the table size since 7.3, the empty array
 * is one shared immutable array since 7.3, the GC flags moved in 7.3 and HASH_FLAG_INITIALIZED became
 * HASH_FLAG_UNINITIALIZED in 7.4. check_roundtrip() reads each value back, changes the copy (which
 * writes into the HashTables that ucache_get() built) and reads it again.
 */

define('UCACHE_SMOKE_DEFINED', ['defined' => [1, 2], 'by' => 'define()']);

$arrays = [
    /* key => [what, function that returns a new copy of the value] */
    'zv_list' => ['a list', function () {
        return [1, 2, 3, 'four', 5.5, true, null];
    }],
    'zv_holes' => ['a list with holes left by unset()', function () {
        $list = range(0, 9);
        unset($list[2], $list[5], $list[9]);
        return $list;
    }],
    'zv_offset' => ['a list that does not start at 0', function () {
        return [3 => 'c', 4 => 'd', 5 => 'e'];
    }],
    'zv_grown' => ['a list that grew past its initial size', function () {
        $list = [];
        for ($i = 0; $i < 100; $i++) {
            $list[] = "item $i";
        }
        return $list;
    }],
    'zv_list2000' => ['a list of 2000 integers and strings', function () {
        $list = [];
        for ($i = 0; $i < 2000; $i++) {
            $list[] = ($i % 3 === 0) ? "string $i" : $i;
        }
        return $list;
    }],
    'zv_mixed' => ['a list of mixed values', function () {
        return [0, 1, -1, PHP_INT_MAX, PHP_INT_MIN, 0.5, -1.5e300, true, false, null, '', '0', 'a', "nul\0byte", [], [null]];
    }],
    'zv_nestedlists' => ['nested lists', function () {
        return [[1, 2], [3, [4, [5, 6]]], ['x'], [[[]]]];
    }],
    'zv_intkeys' => ['an array with negative and huge integer keys', function () {
        return [-1 => 'a', 5 => 'b', PHP_INT_MAX => 'c', PHP_INT_MIN => 'd', 0 => 'e', 4294967296 => 'f', -4294967296 => 'g'];
    }],
    'zv_mixedkeys' => ['an array with integer and string keys', function () {
        return [5 => 'a', 'b' => 'c', -3 => 'd', '10' => 'e', 'x' => [1], 0 => 'zero'];
    }],
    'zv_hash9' => ['an array with 9 string keys', function () {
        return string_keys(9);
    }],
    'zv_hash65' => ['an array with 65 string keys', function () {
        return string_keys(65);
    }],
    'zv_hash100' => ['an array with 100 string keys after 80 of them were unset', function () {
        $hash = string_keys(100);
        for ($i = 0; $i < 100; $i++) {
            if ($i % 5 !== 0) {
                unset($hash["key $i"]);
            }
        }
        return $hash;
    }],
    'zv_compacted' => ['an array less than half full after PHP compacted it on a resize', function () {
        $hash = string_keys(100);                  /* 100 of 128 slots used */
        for ($i = 0; $i < 80; $i++) {
            unset($hash["key $i"]);
        }
        for ($i = 100; $i < 130; $i++) {
            $hash["key $i"] = $i;                  /* filling the table makes PHP compact it, not grow it */
        }
        unset($hash['key 85'], $hash['key 120']);  /* and two holes */
        return $hash;
    }],
    'zv_numerickeys' => ['string keys that look numeric ("07", "-0", " 1", ...)', function () {
        return ['07' => 1, '-0' => 2, ' 1' => 3, '1 ' => 4, '0x1A' => 5, '1e3' => 6, '1.5' => 7, '9223372036854775808' => 8,
                '-9223372036854775809' => 9, '' => 10, '00' => 11, '+1' => 12];
    }],
    'zv_binarykeys' => ['binary string keys with NUL bytes', function () {
        return ["a\0b" => 1, "\0" => 2, "\0\0" => 3, "tail\0" => 4, "\xff\xfe" => 5, str_repeat('long key ', 50) => 6];
    }],
    'zv_empty' => ['the empty array', function () {
        return [];
    }],
    'zv_emptynested' => ['nested empty arrays', function () {
        return [[], [[]], 'e' => [], 'n' => ['a' => [], 'b' => [[]]]];
    }],
    'zv_literal' => ['an array literal from the source', function () {
        return ['literal' => [1, 2, 3], 'map' => ['a' => 'b', 'c' => [true, null, 1.5]], 'empty' => []];
    }],
    'zv_const' => ['a constant array (const)', function () {
        return UCACHE_SMOKE_CONST;
    }],
    'zv_classconst' => ['a class constant array', function () {
        return UcacheSmokeConstants::TABLE;
    }],
    'zv_define' => ['an array constant made by define()', function () {
        return UCACHE_SMOKE_DEFINED;
    }],
    'zv_floats' => ['an array of special floats (NAN, INF, -0.0, ...)', function () {
        return [NAN, INF, -INF, -0.0, 0.1, 1e308, 5e-324, PHP_FLOAT_EPSILON];
    }],
    'zv_bigstring' => ['an array holding a 1 MB string', function () {
        return ['big' => str_repeat("0123456789abcde\0", 65536), 'small' => 's'];
    }],
    'zv_deep' => ['100 levels of nested arrays', function () {
        $deep = 'bottom';
        for ($i = 0; $i < 100; $i++) {
            $deep = ['level' => $i, 'next' => $deep];
        }
        return $deep;
    }],
    'zv_deeplist' => ['100 levels of nested lists', function () {
        $deep = [];
        for ($i = 0; $i < 100; $i++) {
            $deep = [$deep, $i];
        }
        return $deep;
    }],
    'zv_refs' => ['an array with references', function () {
        $array = [1, 2];
        $array[2] = &$array[0];
        $array['n'] = ['x' => 1];
        $array['m'] = &$array['n'];
        return $array;
    }],
];

foreach ($arrays as $key => $case) {
    check_roundtrip('set/get ' . $case[0], $key, $case[1]);
}
ucache_delete('zv_bigstring');

/* The array forms of ucache_set() and ucache_get() copy each value the same way */
$multi = ['zv_multi_list' => $arrays['zv_list'][1], 'zv_multi_hash' => $arrays['zv_hash65'][1], 'zv_multi_refs' => $arrays['zv_refs'][1],
          'zv_multi_empty' => $arrays['zv_empty'][1]];
$make = function () use ($multi) {
    return array_map(function ($fn) { return $fn(); }, $multi);
};
$set = ucache_set($make());
$got = ucache_get(array_keys($multi));
$readBack = value_diff($make(), $got);
check('set and get with arrays of keys copy arrays as the single-key forms do', $set === [] && $readBack === null,
      ['set' => $set, 'read back' => $readBack]);

/*
 * Numbers and strings
 */

$numbers = ['zv_nan' => NAN, 'zv_inf' => INF, 'zv_neginf' => -INF, 'zv_negzero' => -0.0, 'zv_intmax' => PHP_INT_MAX,
            'zv_intmin' => PHP_INT_MIN, 'zv_numberlist' => [NAN, -0.0, INF, -INF, 5e-324, PHP_INT_MIN]];
$bad = [];
foreach ($numbers as $key => $value) {
    $set = ucache_set($key, $value);
    $got = ucache_get($key);
    if ($set !== true || bits($got) !== bits($value)) {
        $bad[$key] = ['set' => $set, 'got' => bits($got)];
    }
}
check('NAN, INF, -INF, -0.0 (with its sign), PHP_INT_MAX and PHP_INT_MIN come back exactly, alone and in a list', $bad === [], $bad);

$megabyte = str_repeat("0123456789abcde\0", 65536);
check('set/get a 1 MB string', ucache_set('zv_megabyte', $megabyte) === true && ucache_get('zv_megabyte') === $megabyte);
ucache_delete('zv_megabyte');

check('set/get the empty string', ucache_set('zv_emptystring', '') === true && ucache_get('zv_emptystring') === '');

/*
 * Copies are independent: a value read back is an ordinary PHP value. Arrays it holds twice are
 * copied on write, its references point into the copy, and changing it changes neither the cached
 * value nor the original.
 */

$sub   = ['p' => 1, 'q' => [2, 3]];
$twice = ['first' => $sub, 'second' => $sub, 'list' => [$sub, $sub]];
ucache_set('zv_twice', $twice);
$got = ucache_get('zv_twice');
$readBack = value_diff($twice, $got);
if ($readBack === null) {
    $got['first']['p'] = 'changed';
    $got['list'][1]['q'][] = 4;
}
check('a sub-array stored twice in a value comes back twice, and changing one copy leaves the others alone',
      $readBack === null && $got['first'] === ['p' => 'changed', 'q' => [2, 3]] && $got['second'] === $sub
      && $got['list'] === [$sub, ['p' => 1, 'q' => [2, 3, 4]]] && ucache_get('zv_twice') === $twice,
      ['read back' => $readBack, 'after the changes' => $got]);

$original = ['s' => str_repeat('x', 1000), 'list' => range(1, 100), 'map' => ['k' => 'v']];
ucache_set('zv_reuse', $original);
$got = ucache_get('zv_reuse');
ucache_delete('zv_reuse');
ucache_set('zv_reuse_other', ['s' => str_repeat('y', 1000), 'list' => range(101, 200), 'map' => ['k' => 'w']]);
check('a value read back does not change when its entry is deleted and the memory is used again',
      value_diff($original, $got) === null, value_diff($original, $got));

check('a value read back can be stored again', ucache_set('zv_again', $got) === true && ucache_get('zv_again') === $original);

$withRef = [1, 2];
$withRef[2] = &$withRef[0];
ucache_set('zv_ref', $withRef);
$got = ucache_get('zv_ref');
$readBack = ($got === [1, 2, 1]);
if ($readBack) {
    $got[2] = 99;
}
$withRef[0] = 'original changed';
check('a reference inside an array is still a reference after ucache_get, into the copy and not to the original',
      $readBack && $got === [99, 2, 99] && $withRef === ['original changed', 2, 'original changed'] && ucache_get('zv_ref') === [1, 2, 1],
      ['read back' => $readBack, 'after the change' => $got, 'cached' => ucache_get('zv_ref')]);

$shared = 'v';
$pair = [['r' => &$shared], ['r' => &$shared]];
ucache_set('zv_sharedref', $pair);
$got = ucache_get('zv_sharedref');
$readBack = ($got === [['r' => 'v'], ['r' => 'v']]);
if ($readBack) {
    $got[0]['r'] = 'w';
}
check('a reference shared by two nested arrays is still shared after ucache_get',
      $readBack && $got === [['r' => 'w'], ['r' => 'w']] && $pair === [['r' => 'v'], ['r' => 'v']], $got);

$selfRef = ['x' => 1];
$selfRef['self'] = &$selfRef;
$set = ucache_set('zv_self', $selfRef);
$got = ucache_get('zv_self');
$shape = is_array($got) && array_keys($got) === ['x', 'self'] && $got['x'] === 1 && is_array($got['self'])
         && array_keys($got['self']) === ['x', 'self'] && isset($got['self']['self']['self']['x']) && $got['self']['self']['self']['x'] === 1;
if ($shape) {
    /* What else changes depends on the PHP version (when it separates an array holding the only reference
       to itself), but the original must not change */
    $got['self']['x'] = 2;
}
check('an array that contains a reference to itself can be stored and read back',
      $set === true && $shape && $got['x'] === 1 && $got['self']['x'] === 2 && $selfRef['x'] === 1,
      ['set' => $set, 'read back' => is_array($got) ? array_keys($got) : brief($got)]);

/* PHP 7.4 added typed properties: a reference to one points to the property (zend_reference.sources) */
if (PHP_VERSION_ID >= 70400) {
    eval('class UcacheSmokeTyped { public int $n = 1; }');     /* typed properties do not parse before 7.4 */
    $typed = new UcacheSmokeTyped();
    $value = ['n' => &$typed->n, 'other' => 2];
    $set = ucache_set('zv_typedref', $value);
    unset($typed, $value);
    $got = ucache_get('zv_typedref');
    $r = attempt(function () use (&$got) {
        if (is_array($got)) {
            $got['n'] = 'not an int';
        }
        return $got;
    });
    check('a reference to a typed property comes back as a plain reference (PHP 7.4+)',
          $set === true && $r === [['n' => 'not an int', 'other' => 2], null], ($r[1] === null) ? $r[0] : outcome($r));
}

/* The array form of ucache_set() gets the values as they are in the array, including references */
$list = [1, 2, 3];
ucache_set(['zv_byref' => &$list]);
$got = ucache_get(['zv_byref']);
$copy = $got;
if (isset($copy['zv_byref']) && is_array($copy['zv_byref'])) {
    $copy['zv_byref'][0] = 'changed';
}
check('a value given by reference to the array form of ucache_set() is stored and read back as a value',
      ucache_get('zv_byref') === [1, 2, 3] && $got === ['zv_byref' => [1, 2, 3]], $got);

/* ...so an integer given by reference is stored as an integer, which ucache_inc() can change */
$counter = 5;
$set = ucache_set(['zv_byref_counter' => &$counter]);
$inc = capture(function () { return ucache_inc('zv_byref_counter'); }, $raised);
$entry = find_entry(ucache_info(false, 'zv_byref_counter'), 'zv_byref_counter');
check('an integer given by reference to the array form of ucache_set() is stored as an integer (value_type long) that ucache_inc() changes',
      $set === [] && $inc === 6 && $raised === [] && $entry !== null && $entry['value_type'] === 'long'
      && ucache_get('zv_byref_counter') === 6 && $counter === 5,
      ['set' => $set, 'inc' => $inc, 'warnings' => $raised, 'entry' => $entry]);
ucache_delete('zv_byref_counter');

/*
 * Objects are stored serialized (php_var_serialize) and come back as new objects with the same class
 * and properties
 */

$objects = [
    'zv_objects' => ['objects inside arrays', function () {
        return ['point' => new UcacheSmokePoint(1, 'two'), 'list' => [new UcacheSmokePoint(3.5, null), new stdClass()], 'n' => 1];
    }],
    'zv_objectrefs' => ['an object with an array property holding references', function () {
        $object = new stdClass();
        $object->list = [1, 2];
        $object->list[2] = &$object->list[0];
        return $object;
    }],
    'zv_nestedobjects' => ['nested objects', function () {
        $inner = new stdClass();
        $inner->deep = new UcacheSmokePoint([1, 2], new ArrayObject(['k' => 'v']));
        $outer = new stdClass();
        $outer->inner = $inner;
        $outer->list = [$inner->deep, 'x'];
        return $outer;
    }],
    'zv_dates' => ['DateTime and DateTimeImmutable objects', function () {
        return ['mutable'   => new DateTime('2024-02-29 13:14:15.123456', new DateTimeZone('Europe/Athens')),
                'immutable' => new DateTimeImmutable('1999-12-31 23:59:59', new DateTimeZone('UTC'))];
    }],
];

foreach ($objects as $key => $case) {
    check_roundtrip('set/get ' . $case[0], $key, $case[1]);
}

$got = ucache_get('zv_objectrefs');
if (is_object($got) && isset($got->list) && is_array($got->list)) {
    $got->list[2] = 'changed';
}
check('references inside an object are still references after ucache_get',
      is_object($got) && isset($got->list) && $got->list === ['changed', 2, 'changed'], is_object($got) ? $got : brief($got));

$got = ucache_get('zv_dates');
check('DateTime objects come back with the same date, time and time zone',
      is_array($got) && isset($got['mutable'], $got['immutable']) && $got['mutable'] instanceof DateTime
      && $got['mutable']->format('Y-m-d H:i:s.u e') === '2024-02-29 13:14:15.123456 Europe/Athens'
      && $got['immutable'] instanceof DateTimeImmutable && $got['immutable']->format('Y-m-d H:i:s e') === '1999-12-31 23:59:59 UTC',
      $got);

/* PHP 7.4 added __serialize() and __unserialize(). PHP calls __unserialize() once the whole value is unserialized, as it
   calls __wakeup(): the object must come back as __unserialize() restores it */
if (PHP_VERSION_ID >= 70400) {
    $magic = [
        /* key => function that returns the value, its object restored by __unserialize() or not */
        'zv_magic' => function ($restored) {
            return new UcacheSmokeSerializeMagic('alone', [1, 2], $restored);
        },
        'zv_magic_array' => function ($restored) {
            return ['object' => new UcacheSmokeSerializeMagic('inside', ['k' => [3]], $restored), 'n' => 1];
        },
    ];
    $bad = [];
    foreach ($magic as $key => $make) {
        $set = ucache_set($key, $make(false));
        $readBack = value_diff($make(true), ucache_get($key));
        if ($set !== true || $readBack !== null) {
            $bad[$key] = ['set' => $set, 'read back' => $readBack];
        }
    }
    check('an object with __serialize() and __unserialize() comes back as __unserialize() restores it, alone and inside an array (PHP 7.4+)',
          $bad === [], $bad);
}

/* PHP 8.1 enums: a case is a singleton object (serialized as E:...) and must come back as the same case, pure or backed,
   alone or inside an array */
if (PHP_VERSION_ID >= 80100) {
    eval('enum UcacheSmokeSuit: string { case Hearts = "H"; case Spades = "S"; }
          enum UcacheSmokeLight { case Red; case Green; }');    /* enums do not parse before 8.1 */
    $hearts = constant('UcacheSmokeSuit::Hearts');
    $spades = constant('UcacheSmokeSuit::Spades');
    $red    = constant('UcacheSmokeLight::Red');
    $green  = constant('UcacheSmokeLight::Green');
    $enums = ['zv_enum' => ['card' => $hearts, 'list' => [$spades, $hearts], 'light' => $green, 'lights' => [$red, $green]],
              'zv_enum_backed' => $spades, 'zv_enum_pure' => $red];
    $bad = [];
    foreach ($enums as $key => $value) {
        $set = ucache_set($key, $value);
        $got = ucache_get($key);
        if ($set !== true || $got !== $value) {
            $bad[$key] = ['set' => $set, 'get' => $got];
        }
    }
    check('pure and backed enum cases come back as the same cases (===), alone and inside an array (PHP 8.1+)', $bad === [], $bad);
}

/* PHP 8.1 readonly properties and PHP 8.2 readonly classes: unserializing the object initializes its readonly
   properties, which stay readonly */
$readonly = [];
if (PHP_VERSION_ID >= 80100) {
    eval('class UcacheSmokeReadonly { public function __construct(public readonly int $id, public readonly array $list) {} }');   /* readonly properties do not parse before 8.1 */
    $readonly['UcacheSmokeReadonly'] = ['with readonly properties', 'PHP 8.1+'];
}
if (PHP_VERSION_ID >= 80200) {
    eval('readonly class UcacheSmokeReadonlyClass { public function __construct(public int $id, public array $list) {} }');   /* readonly classes do not parse before 8.2 */
    $readonly['UcacheSmokeReadonlyClass'] = ['of a readonly class', 'PHP 8.2+'];
}
foreach ($readonly as $class => $what) {
    $readonlyValues = ['zv_readonly' => new $class(7, ['k' => [1, 2]]), 'zv_readonly_array' => ['object' => new $class(8, []), 'n' => 1]];
    $results = [];
    foreach ($readonlyValues as $key => $value) {
        $results[$key] = [ucache_set($key, $value), value_diff($value, ucache_get($key))];
    }
    $got = ucache_get('zv_readonly');
    $write = attempt(function () use ($got) { $got->id = 9; });
    check("an object $what[0] comes back with the same properties, alone and inside an array, and they stay readonly ($what[1])",
          $results === ['zv_readonly' => [true, null], 'zv_readonly_array' => [true, null]] && $write[1] instanceof Error
          && strpos($write[1]->getMessage(), 'Cannot modify readonly property') !== false && $got->id === 7,
          ['[set, read back] of each key' => $results, 'write' => outcome($write)]);
}

/* Key arrays whose elements are references, as a foreach by reference leaves them: ucache_get() and ucache_delete()
   read the value of each element. ucache_get() holds the key while it unserializes a stored object, whose __wakeup()
   can give the variable that the element shares another value (the key string is not interned, so that frees it) */
eval('class UcacheSmokeKeyChanger { public function __wakeup() { $GLOBALS["refKeyText"] = str_repeat("z", 64); } }');
$refKeyText = 'zv_refkey_' . str_repeat('o', 3);
$refKeyInt = 4242;
ucache_set(['zv_refkey_ooo' => new UcacheSmokeKeyChanger(), 'zv_refkey_b' => 'B', 4242 => 'int key']);
$refKeys = [&$refKeyText, 'zv_refkey_b', &$refKeyInt];
$got = capture(function () use ($refKeys) { return ucache_get($refKeys); }, $raised);
check('ucache_get() reads a key array whose elements are references, also when a __wakeup() gives such an element another value while its key is read',
      is_array($got) && array_keys($got) === ['zv_refkey_ooo', 'zv_refkey_b', 4242] && $got['zv_refkey_ooo'] instanceof UcacheSmokeKeyChanger
      && $got['zv_refkey_b'] === 'B' && $got[4242] === 'int key' && $raised === [] && $refKeyText === str_repeat('z', 64),
      ['result' => $got, 'warnings' => $raised]);
$refKeyText = 'zv_refkey_ooo';
$deleted = capture(function () use ($refKeys) { return ucache_delete($refKeys); }, $raised);
check('ucache_delete() deletes the keys of a key array whose elements are references, returns them as strings and leaves the array as it was',
      $deleted === ['zv_refkey_ooo', 'zv_refkey_b', '4242'] && $raised === [] && ucache_exists('zv_refkey_b') === false
      && ucache_exists(4242) === false && $refKeyText === 'zv_refkey_ooo' && $refKeyInt === 4242, ['result' => $deleted, 'warnings' => $raised]);
unset($refKeys, $refKeyText, $refKeyInt);

/* An object that refers to itself through a PHP reference ($o->self = &$o) comes back as a value, inside an array and as
   the value of a reference, as unserialize() gives it: not as a reference shared with its own property, which a copy of
   the array would share and a foreach by value would overwrite */
$selfRef = new stdClass();
$selfRef->tag = 'T';
$selfRef->self = &$selfRef;
$selfSet = [ucache_set('zv_selfref', ['x' => $selfRef, 'n' => 7]), ucache_set('zv_selfref_ref', ['x' => &$selfRef, 'n' => 7])];
$got = ucache_get('zv_selfref');
$copy = $got;
$copy['x'] = 'changed';
$gotRef = ucache_get('zv_selfref_ref');
foreach ($gotRef as $value) {
}
check('an object that refers to itself through a reference comes back as a value, not as a reference shared with its own property, inside an array and as the value of a reference',
      $selfSet === [true, true] && is_object($got['x']) && $got['x']->tag === 'T' && $got['x']->self === $got['x']
      && is_object($gotRef['x']) && $gotRef['x']->tag === 'T' && $gotRef['x']->self === $gotRef['x'],
      ['in an array' => brief($got['x']), 'as the value of a reference' => brief($gotRef['x'])]);
ucache_delete(['zv_selfref', 'zv_selfref_ref']);
unset($selfRef, $got, $copy, $gotRef, $value);

/* An object of the value that PHP cannot unserialize when it is read (here one nested deeper than unserialize_max_depth,
   which PHP 7.4 added; also an enum, or a class, that is not defined or changed since the value was stored) fails the
   read, as unserialize() fails: ucache_get() returns false with $success false, its array form leaves the key out,
   PHP's warning says why, and the entry stays for the processes that can read it */
if (PHP_VERSION_ID >= 70400) {
    $deep = 'leaf';
    for ($i = 0; $i < 20; $i++) {
        $deep = [$deep];
    }
    $box = new stdClass();
    $box->deep = $deep;
    $boxSet = [ucache_set('zv_too_deep', ['box' => $box, 'n' => 1]), ucache_set('zv_depth_other', 1)];
    $savedDepth = ini_set('unserialize_max_depth', '10');
    $got = capture(function () { return [ucache_get('zv_too_deep', $success), $success]; }, $raised);
    $gotArray = capture(function () { return [ucache_get(['zv_too_deep', 'zv_depth_other'], $success), $success]; }, $raisedArray);
    ini_set('unserialize_max_depth', $savedDepth);
    $readable = ucache_get('zv_too_deep');
    check('an object that cannot be unserialized when it is read (here deeper than unserialize_max_depth) fails the read with PHP\'s warning, the array form leaves its key out, and the entry stays (PHP 7.4+)',
          $boxSet === [true, true] && $got === [false, false] && count($raised) === 1 && stripos($raised[0], 'depth') !== false
          && $gotArray === [['zv_depth_other' => 1], true] && count($raisedArray) === 1
          && is_array($readable) && $readable['box'] instanceof stdClass && $readable['box']->deep === $deep,
          ['get' => $got, 'warnings' => $raised, 'array form' => $gotArray, 'its warnings' => $raisedArray, 'read back with the default depth' => brief($readable)]);
    ucache_delete(['zv_too_deep', 'zv_depth_other']);
    unset($deep, $box, $got, $gotArray, $readable);
}

/* A recursion built in a value read back is detected, as in any PHP array. On PHP 7.2 a copy keeps the flags of the
   stored table, and opcache clears the recursion protection (HASH_FLAG_APPLY_PROTECTION) of the arrays it makes
   immutable, such as this literal in the run with opcache: without it count() overflowed the C stack */
$literalSet = ucache_set('zv_literal', ['p' => [10, 20], 'q' => 'x']);
$copy = ucache_get('zv_literal');
$copy['self'] = &$copy;
$count = capture(function () use (&$copy) { return count($copy, COUNT_RECURSIVE); }, $raised);
check('count() detects a recursion built in a value read back (here from a literal array, which opcache makes immutable)',
      $literalSet === true && is_int($count) && count($raised) === 1 && stripos($raised[0], 'recursion detected') !== false,
      ['count' => $count, 'warnings' => $raised]);
unset($copy['self'], $copy);
ucache_delete('zv_literal');

/*
 * __wakeup() and __sleep() can call the cache functions. ucache_get() unserializes the objects after it
 * has copied the whole value out and released the cache, so a __wakeup() that deletes or replaces the
 * entry being read (its shared memory is then freed and used again) does not change the value read.
 */
foreach (['UcacheSmokeWakeupDeletes' => 'deletes', 'UcacheSmokeWakeupReplaces' => 'replaces'] as $class => $what) {
    foreach (['single key', 'array of keys'] as $form) {
        $class::$key = "zv_wakeup_$what";
        $make = function () use ($class) {
            return ['first' => new $class(), 'rest' => range(1, 500), 'tail' => str_repeat('t', 3000)];
        };
        $set = ucache_set($class::$key, $make());
        $success = 'unset';
        if ($form === 'single key') {
            $got = ucache_get($class::$key, $success);
        } else {
            $got = ucache_get([$class::$key], $success);
            $got = (is_array($got) && array_keys($got) === [$class::$key]) ? $got[$class::$key] : $got;
        }
        $readBack = value_diff($make(), $got);
        $now = ucache_get($class::$key);
        $nowOk = ($what === 'deletes') ? ($now === false) : ($now === ['replaced' => str_repeat('r', 5000)]);
        check("a __wakeup() that $what the entry being read does not change the value read ($form)",
              $set === true && $success === true && $readBack === null && $nowOk,
              ['set' => $set, 'success' => $success, 'read back' => $readBack, 'entry afterwards' => brief($now)]);
    }
}
for ($i = 0; $i < 50; $i++) {
    ucache_delete("zv_wakeup_filler$i");
}
ucache_delete('zv_wakeup_replaces');

/* A __wakeup() that throws: the exception reaches the caller, which gets no value, and $success is false */
ucache_set('zv_wakeup_throws', ['a' => 1, 'o' => new UcacheSmokeWakeupThrows(), 'z' => 2]);
foreach (['single key' => 'zv_wakeup_throws', 'array of keys' => ['smoke_long', 'zv_wakeup_throws', 'zv_twice']] as $form => $key) {
    $success = 'unset';
    $r = attempt(function () use ($key, &$success) { return ucache_get($key, $success); });
    check("a __wakeup() that throws makes ucache_get() throw, with \$success false ($form)",
          $r[0] === null && $r[1] instanceof RuntimeException && $r[1]->getMessage() === '__wakeup failed' && $success === false
          && ucache_exists('zv_wakeup_throws') === true,
          ['result' => outcome($r), 'success' => $success]);
}
ucache_delete('zv_wakeup_throws');

ucache_delete('zv_sleep_other');
$set = ucache_set('zv_sleep_outer', ['o' => new UcacheSmokeSleepStores(), 'n' => 1]);
$readBack = value_diff(['o' => new UcacheSmokeSleepStores(), 'n' => 1], ucache_get('zv_sleep_outer'));
$other = ucache_get('zv_sleep_other');
check('a __sleep() that calls ucache_set() for another key: both values are stored',
      $set === true && $readBack === null && $other === ['from' => '__sleep'],
      ['set' => $set, 'read back' => $readBack, 'other key' => $other]);
ucache_delete(['zv_sleep_outer', 'zv_sleep_other']);

/*
 * Serializable::serialize() and Serializable::unserialize() run inside PHP's serializer and share its context:
 * the serialize() and unserialize() calls they make number their back-references (r:N, R:N) together with the
 * value around them, which is how the payload of an object can refer to the object itself. ucache serializes
 * and unserializes each object as a top-level operation, with a context of its own: a cache function called from
 * such a method (or from __sleep(), __wakeup(), ...) must neither join the caller's context (the value it stores
 * or reads would refer to the wrong objects) nor disturb it (the caller's value would). Since PHP 8.1 declaring a
 * class that implements only Serializable raises E_DEPRECATED, so these classes are declared by eval() while
 * E_DEPRECATED is not reported. The checks with several processes test the objects whose payload refers to the
 * object itself, which recurse without end when the context is not right.
 */
$reporting = error_reporting();
error_reporting($reporting & ~E_DEPRECATED);
eval(<<<'DECLARE'
/* Its serialize() stores two objects that refer to themselves and to each other ($key) */
class UcacheSmokeSetInSerialize implements Serializable
{
    public static $key = '';
    public static $set = null;
    public $data;

    public function serialize()
    {
        self::$set = ucache_set(self::$key, self_referring_value('stored'));
        return serialize($this->data);
    }

    public function unserialize($data)
    {
        $this->data = unserialize($data);
    }
}

/* Its unserialize() reads another entry ($key) */
class UcacheSmokeGetInUnserialize implements Serializable
{
    public static $key = '';
    public static $got = null;
    public $data;

    public function serialize()
    {
        return serialize($this->data);
    }

    public function unserialize($data)
    {
        self::$got = ucache_get(self::$key);
        $this->data = unserialize($data);
    }
}
DECLARE
);
error_reporting($reporting);

function node_ok($node, $name)
{
    return $node instanceof UcacheSmokeNode && $node->name === $name && $node->self === $node;
}

/* Two objects that refer to themselves and to each other, in an array */
function self_referring_value($name)
{
    $node = new UcacheSmokeNode($name);
    $node->peer = new UcacheSmokeNode("$name peer");
    $node->peer->peer = $node;
    return ['node' => $node, 'list' => [1, 2]];
}

function self_referring_ok($value, $name)
{
    return is_array($value) && isset($value['node'], $value['list']) && node_ok($value['node'], $name)
           && node_ok($value['node']->peer, "$name peer") && $value['node']->peer->peer === $value['node'] && $value['list'] === [1, 2];
}

/* An object of a Serializable $class whose data holds two objects that refer to themselves, between two more */
function serializable_value($class)
{
    $object = new $class();
    $object->data = [new UcacheSmokeNode('d1'), new UcacheSmokeNode('d2')];
    return ['before' => new UcacheSmokeNode('before'), 'object' => $object, 'after' => new UcacheSmokeNode('after')];
}

function serializable_ok($value, $class)
{
    return is_array($value) && isset($value['before'], $value['object'], $value['after']) && node_ok($value['before'], 'before')
           && $value['object'] instanceof $class && is_array($value['object']->data) && count($value['object']->data) === 2
           && node_ok($value['object']->data[0], 'd1') && node_ok($value['object']->data[1], 'd2') && node_ok($value['after'], 'after');
}

UcacheSmokeSetInSerialize::$key = 'zv_ser_stored';
foreach (['serialize()', 'ucache_set()'] as $outer) {
    ucache_delete('zv_ser_stored');
    UcacheSmokeSetInSerialize::$set = null;
    $around = capture(function () use ($outer) {
        if ($outer === 'serialize()') {
            return unserialize(serialize(serializable_value('UcacheSmokeSetInSerialize')));
        }
        return ucache_set('zv_ser_outer', serializable_value('UcacheSmokeSetInSerialize')) ? ucache_get('zv_ser_outer') : 'ucache_set() failed';
    }, $raised);
    $stored = capture(function () { return ucache_get('zv_ser_stored'); }, $raisedGet);
    check("a ucache_set() inside Serializable::serialize() of an object that $outer serializes: the value stored and the value around the object read back intact, without notices",
          UcacheSmokeSetInSerialize::$set === true && self_referring_ok($stored, 'stored') && serializable_ok($around, 'UcacheSmokeSetInSerialize')
          && $raised === [] && $raisedGet === [],
          ['set' => UcacheSmokeSetInSerialize::$set, 'value stored intact' => self_referring_ok($stored, 'stored'),
           'value around intact' => serializable_ok($around, 'UcacheSmokeSetInSerialize'), 'notices' => array_merge($raised, $raisedGet)]);
}

UcacheSmokeGetInUnserialize::$key = 'zv_ser_read';
ucache_set('zv_ser_read', self_referring_value('read'));
foreach (['unserialize()', 'ucache_get()'] as $outer) {
    UcacheSmokeGetInUnserialize::$got = null;
    $around = capture(function () use ($outer) {
        if ($outer === 'unserialize()') {
            return unserialize(serialize(serializable_value('UcacheSmokeGetInUnserialize')));
        }
        return ucache_set('zv_ser_outer', serializable_value('UcacheSmokeGetInUnserialize')) ? ucache_get('zv_ser_outer') : 'ucache_set() failed';
    }, $raised);
    $read = UcacheSmokeGetInUnserialize::$got;
    check("a ucache_get() inside Serializable::unserialize() of an object that $outer reads: the value read and the value around the object are intact, without notices",
          self_referring_ok($read, 'read') && serializable_ok($around, 'UcacheSmokeGetInUnserialize') && $raised === [],
          ['value read intact' => self_referring_ok($read, 'read'), 'value around intact' => serializable_ok($around, 'UcacheSmokeGetInUnserialize'),
           'notices' => $raised]);
}

ucache_delete(['zv_ser_stored', 'zv_ser_outer', 'zv_ser_read']);
unset($read, $around, $stored);

/*
 * Changes made by __sleep() while ucache_set() copies a value in. The objects of the value are serialized
 * during the copy, and through a PHP reference their __sleep() can change or free other parts of the value:
 * parts not copied yet, or parts already copied, whose memory the engine can then give to new values. The
 * cache must store the value as it was when ucache_set() was called: neither what __sleep() changed nor
 * memory that was freed meanwhile.
 */

function zv_freed_value()
{
    $value = ['obj' => new UcacheSmokeFreesValue()];
    for ($i = 0; $i < 50; $i++) {
        $value["k$i"] = [fresh_string('s', 33) . $i, $i];
    }
    return $value;
}

$zvFreed = zv_freed_value();
$value = [&$zvFreed];
$set = ucache_set('zv_sleep_frees', $value);
unset($value);
$zvJunk = null;
$readBack = value_diff([zv_freed_value()], ucache_get('zv_sleep_frees'));
check('a __sleep() that frees the array being copied in (only a reference held it): the array is stored as it was',
      $set === true && $readBack === null && $zvFreed === null, ['set' => $set, 'read back' => $readBack]);

$zvChanged = ['o' => new UcacheSmokeChangesValue(), 's' => fresh_string('a', 40)];
$value = ['b' => &$zvChanged];
$set = ucache_set('zv_sleep_changes', $value);
unset($value);
$readBack = value_diff(['b' => ['o' => new UcacheSmokeChangesValue(), 's' => fresh_string('a', 40)]],
                       ucache_get('zv_sleep_changes'));
check('a __sleep() that changes, in place through a reference, the array being copied in: the array is stored as it was',
      $set === true && $readBack === null && $zvChanged['s'] === null, ['set' => $set, 'read back' => $readBack]);

$zvFirst = fresh_string('a', 24);
$zvSecond = null;
$value = ['a' => &$zvFirst, 'o' => new UcacheSmokeReplacesValue(), 'b' => &$zvSecond];
$set = ucache_set('zv_sleep_replaces', $value);
unset($value);
$readBack = value_diff(['a' => fresh_string('a', 24), 'o' => new UcacheSmokeReplacesValue(), 'b' => null],
                       ucache_get('zv_sleep_replaces'));
check('a __sleep() that frees a string of the value (through a reference) and gives its memory to a new one: the value is stored as it was',
      $set === true && $readBack === null && $zvFirst === null && $zvSecond === fresh_string('b', 24),
      ['set' => $set, 'read back' => $readBack]);
ucache_delete(['zv_sleep_frees', 'zv_sleep_changes', 'zv_sleep_replaces']);
unset($zvFreed, $zvChanged, $zvReused, $zvFirst, $zvSecond);

/*
 * Values that cannot be stored: objects that cannot be serialized, resources, values bigger than the
 * shared memory. Storing one must fail - ucache_set() returns false, or the serializer's Exception
 * reaches the caller - and must leave the cache as it was: no entry for a new key, and the previous
 * value for an existing key (an entry that reads back with a part missing or replaced by null is a
 * broken value).
 */

$closure = function () { return 'closure'; };
$holder = new stdClass();
$holder->name = 'holder';
$holder->callback = $closure;
$unserializable = [
    'a Closure'                        => $closure,
    'an array holding a Closure'       => ['a' => 1, 'f' => $closure],
    'a nested array holding a Closure' => ['list' => [1, 2, ['deep' => [$closure]]]],
    'an object holding a Closure'      => $holder,
    'an array holding a Generator'     => ['g' => ucache_smoke_generator()],
    'an anonymous class object'        => new class { public $x = 1; },
];
/* WeakReference and WeakMap objects cannot be serialized either */
if (PHP_VERSION_ID >= 70400) {
    $unserializable['a WeakReference (PHP 7.4+)'] = WeakReference::create($holder);
}
if (PHP_VERSION_ID >= 80000) {
    $weakMap = new WeakMap();
    $weakMap[$holder] = 'value';
    $unserializable['an array holding a WeakMap (PHP 8.0+)'] = ['map' => $weakMap];
}

/* An attempt() at storing a value failed: false (with at most one warning) or an Exception */
$failed = function ($attempt, $raised) {
    return count($raised) <= 1 && ($attempt[0] === false || $attempt[1] instanceof Exception);
};

foreach ($unserializable as $what => $value) {
    ucache_delete('zv_unserializable_new');
    ucache_set('zv_unserializable_old', 'previous value');
    $new = capture(function () use ($value) {
        return attempt(function () use ($value) { return ucache_set('zv_unserializable_new', $value); });
    }, $raisedNew);
    $old = capture(function () use ($value) {
        return attempt(function () use ($value) { return ucache_set('zv_unserializable_old', $value); });
    }, $raisedOld);
    /* Reading back a broken entry can raise unserialize() notices: they belong to this check */
    $now = capture(function () {
        return [ucache_exists('zv_unserializable_new'), ucache_get('zv_unserializable_old')];
    }, $raisedRead);
    check("ucache_set() of $what fails and leaves the cache as it was",
          $failed($new, $raisedNew) && $failed($old, $raisedOld) && $now === [false, 'previous value'] && $raisedRead === [],
          ['new key' => outcome($new), 'new key stored' => $now[0], 'existing key' => outcome($old), 'existing key holds' => brief($now[1]),
           'warnings' => array_merge($raisedNew, $raisedOld, $raisedRead)]);
}

ucache_delete('zv_unserializable_new');
$r = capture(function () use ($closure) {
    return attempt(function () use ($closure) { return ucache_add('zv_unserializable_new', ['f' => $closure]); });
}, $raised);
check('ucache_add() of an array holding a Closure fails and stores nothing',
      $failed($r, $raised) && ucache_exists('zv_unserializable_new') === false, ['add' => outcome($r), 'warnings' => $raised]);

$r = capture(function () use ($closure) {
    return attempt(function () use ($closure) { return ucache_set(['zv_unserializable_k1' => [$closure], 'zv_unserializable_k2' => 2]); });
}, $raised);
check('set with an array stores nothing for a value that cannot be serialized',
      ($r[0] === ['zv_unserializable_k1' => -1] || $r[1] instanceof Exception) && ucache_exists('zv_unserializable_k1') === false,
      ['set' => outcome($r), 'warnings' => $raised]);

ucache_set('zv_resource', 'previous value');
$r = capture(function () use ($res) {
    return attempt(function () use ($res) { return ucache_set('zv_resource', ['ok' => 1, 'resource' => $res]); });
}, $raised);
check('ucache_set() of an array holding a resource fails and keeps the previous value',
      $failed($r, $raised) && ucache_get('zv_resource') === 'previous value',
      ['set' => outcome($r), 'value now' => brief(ucache_get('zv_resource')), 'warnings' => $raised]);

/* Bigger than the whole cache, so the copy runs out of shared memory: at once, or part way through an array.
   The full scavenger pass that ucache_set() then runs before it fails (the first time; for a second after a
   pass that found nothing, the processes skip it) removes nothing here (no entry has expired, and
   ucache.gc.ttl and ucache.gc.evict are 0). The values take about 2.25 times the size of the
   cache in request memory; memory_limit (INI_ALL) is raised for them if needed */
$tooBig = [];
$total = ucache_meminfo()['memory_total'];
$needed = memory_get_usage() + (int)(2.5 * $total) + 32 * 1048576;
$limit = ini_bytes(ini_get('memory_limit'));
if ($total > 128 * 1048576) {
    echo "SKIP: ucache_set() of a value bigger than the shared memory (with ucache.size above 128 it takes too much request memory)\n";
} else {
    if ($limit > 0 && $limit < $needed) {
        ini_set('memory_limit', (string)$needed);
    }
    $huge = str_repeat('h', $total + 1048576);
    $many = [];
    for ($i = 0; $i < 20; $i++) {
        $many[] = str_repeat(chr(65 + $i), (int)($total / 16));    /* 20 different strings, 1.25 times the cache */
    }
    $tooBig = ['a string' => $huge, 'an array holding a string' => ['list' => range(1, 100), 'huge' => $huge, 'after' => 1],
               'an array of strings' => $many];
    unset($huge, $many);

    ucache_set('zv_toobig', 'previous value');
    $results = [];
    foreach ($tooBig as $what => $value) {
        $results[$what] = capture(function () use ($value) { return ucache_set('zv_toobig', $value); }, $raised);
        $results[$what] = [$results[$what], count($raised)];
    }
    check('ucache_set() of a value bigger than the shared memory returns false and keeps the previous value',
          $results === array_fill_keys(array_keys($tooBig), [false, 0]) && ucache_get('zv_toobig') === 'previous value',
          ['results and warnings' => $results, 'value now' => brief(ucache_get('zv_toobig'))]);
}

/*
 * Nesting depth. The copy-in (both phases) and the copy-out recurse once per level of nested arrays and
 * references, so ucache_set() refuses a value nested more than 10000 levels deep before it takes any shared
 * memory: it returns false, without a warning, as for a resource inside the value. The value itself is the first
 * level, and every array and every reference in it is a level of its own. Without the limit, a value nested about
 * 128000 levels deep (75000 through references) overflowed the C stack of PHP 7.2 in phase 2, which lost the
 * half-built entry for every process of the pool.
 */

/* $levels arrays, each one the only element of the one before; the deepest holds 'leaf' */
function nested_arrays($levels)
{
    $value = 'leaf';
    for ($i = 0; $i < $levels; $i++) {
        $value = [$value];
    }
    return $value;
}

/* $levels levels of arrays and references in turn: element 0 of each array is a reference to the next level. The
   value (level 1) is an array, so the deepest level is a reference to 'leaf' if $levels is even, and an array that
   holds 'leaf' if it is odd. Nothing may read such a chain before it is stored: PHP 7.2 replaces a reference with
   refcount 1 by its value when it reads the array element that holds it */
function reference_chain($levels)
{
    $top = [];
    $array = &$top;
    for ($depth = 1; $depth + 1 < $levels; $depth += 2) {
        $array[0] = [];
        $array = &$array[0];        /* level $depth + 1 is this reference, $depth + 2 the array it holds */
    }
    $array[0] = 'leaf';
    if ($depth + 1 === $levels) {
        $array = &$array[0];        /* the deepest level: a reference to 'leaf' */
    }
    unset($array);
    return $top;
}

/* The number of arrays down element 0, and what the deepest one holds */
function nesting_shape($value)
{
    $arrays = 0;
    while (is_array($value) && array_key_exists(0, $value)) {
        $arrays++;
        $value = $value[0];
    }
    return [$arrays, $value];
}

$nesting = [
    /* what => [function that returns the value, stored] */
    '10000 levels deep (nested arrays)' => [function () { return nested_arrays(10000); }, true],
    '10001 levels deep (nested arrays)' => [function () { return nested_arrays(10001); }, false],
    '10000 levels deep (5000 arrays and 5000 references in turn, the deepest a reference)'
        => [function () { return reference_chain(10000); }, true],
    '10000 levels deep (an array, then 4999 arrays and 5000 references in turn, the deepest an array)'
        => [function () { return [reference_chain(9999)]; }, true],
    '10001 levels deep (5001 arrays and 5000 references in turn, the deepest an array)'
        => [function () { return reference_chain(10001); }, false],
    '10001 levels deep (an array, then 5000 arrays and 5000 references in turn, the deepest a reference)'
        => [function () { return [reference_chain(10000)]; }, false],
];
foreach ($nesting as $what => $case) {
    $value = $case[0]();
    ucache_delete('zv_nesting');
    $free = ucache_meminfo()['memory_free'];
    $set = capture(function () use ($value) { return ucache_set('zv_nesting', $value); }, $raised);
    if ($case[1]) {
        $got = ucache_get('zv_nesting');
        check("set/get a value nested $what: it reads back equal",
              $set === true && $raised === [] && $got === $value,
              ['set' => $set, 'warnings' => $raised, 'arrays and leaf read back' => nesting_shape($got), 'stored' => nesting_shape($value)]);
    } else {
        $stored = ucache_exists('zv_nesting');
        $taken = $free - ucache_meminfo()['memory_free'];
        check("ucache_set() of a value nested $what returns false without a warning, and stores nothing and takes no shared memory",
              $set === false && $raised === [] && $stored === false && $taken === 0,
              ['set' => $set, 'warnings' => $raised, 'stored' => $stored, 'shared memory taken' => $taken]);
    }
    unset($value, $got);
}
ucache_delete('zv_nesting');

/*
 * Memory. A value read back must be freed with the last variable that holds it: reading it 2000 times
 * must not use more request memory than reading it 100 times, and gc_collect_cycles() must be able to
 * walk and free copies with references and cycles. An entry must give back all of its shared memory
 * when it is deleted or replaced, and so must a ucache_set() that fails.
 */

$refs = [1, 2, 'map' => ['k' => 'v']];
$refs[2] = &$refs[0];
$refs['alias'] = &$refs['map'];
$loop = ['x' => 1, 'list' => [1, 2]];
$loop['self'] = &$loop;

$leaks = [
    'zv_leak_nested' => ['a nested array', ['a' => [1, 2, ['x' => 'y', 'z' => [3.5, true, null]]], 'b' => 'string', 'c' => [[], ['k' => 'v']]]],
    'zv_leak_string' => ['a string', str_repeat('s', 1000)],
    'zv_leak_refs'   => ['an array with references', $refs],
    'zv_leak_loop'   => ['an array that contains a reference to itself', $loop],
    'zv_leak_object' => ['an array holding an object', ['o' => new UcacheSmokePoint(1, [2, 3])]],
];
foreach ($leaks as $key => $case) {
    ucache_set($key, $case[1]);
    $growth = get_growth($key);
    check("reading {$case[0]} 2000 times does not leak request memory", $growth <= 65536, "$growth bytes");
}
$growth = get_growth(['zv_leak_nested', 'zv_leak_refs', 'zv_leak_object']);
check('reading arrays 2000 times with an array of keys does not leak request memory', $growth <= 65536, "$growth bytes");

/* $success holding an array: the array must be released when $success gets its bool */
ucache_set('zv_leak_counter', 0);
$successCalls = [
    'ucache_get() of a stored key'            => function (&$success) { ucache_get('zv_leak_string', $success); },
    'ucache_get() of a missing key'           => function (&$success) { ucache_get('zv_leak_missing', $success); },
    'ucache_get() with an array of keys'      => function (&$success) { ucache_get(['zv_leak_string', 'zv_leak_missing'], $success); },
    'ucache_inc() of a counter'               => function (&$success) { ucache_inc('zv_leak_counter', 1, $success); },
    'ucache_dec() of a missing key'           => function (&$success) { ucache_dec('zv_leak_missing', 1, $success); },
];
$growths = [];
foreach ($successCalls as $what => $call) {
    $growths[$what] = success_growth($call);
}
check('a $success variable that holds an array releases it when it gets its bool (2000 calls do not leak request memory)',
      max($growths) <= 65536, $growths);
ucache_delete('zv_leak_counter');

$kept = [];
for ($i = 0; $i < 500; $i++) {
    $gotRefs = ucache_get('zv_leak_refs');
    $gotLoop = ucache_get('zv_leak_loop');
    if ($i % 100 === 0) {
        $kept[] = [$gotRefs, $gotLoop];
    }
    unset($gotRefs, $gotLoop);
}
$collected = gc_collect_cycles();
$intact = true;
foreach ($kept as $pair) {
    $intact = $intact && value_diff($refs, $pair[0]) === null
              && isset($pair[1]['self']['self']['list']) && $pair[1]['self']['self']['list'] === [1, 2];
}
check('gc_collect_cycles() after reading arrays with references and cycles 500 times leaves the kept copies intact',
      is_int($collected) && $intact, ['collected' => $collected, 'kept copies intact' => $intact]);

$loss = shm_loss(function () {
    ucache_set('zv_shm_hash', string_keys(65));    /* replaces the copy of the previous round */
    ucache_set('zv_shm_list', ['list' => range(1, 300), 'deep' => [[[['x']]]], 'empty' => [], 'text' => str_repeat('t', 300)]);
    ucache_delete('zv_shm_list');
});
check('replacing and deleting nested arrays 200 times gives back their shared memory', abs($loss) <= 1024, "$loss bytes");

$loss = shm_loss(function () use ($refs, $loop) {
    ucache_set('zv_shm_refs', $refs);
    ucache_set('zv_shm_loop', $loop);
    ucache_delete(['zv_shm_refs', 'zv_shm_loop']);
});
check('storing and deleting arrays with references 200 times gives back their shared memory', abs($loss) <= 1024, "$loss bytes");

$loss = shm_loss(function () {
    ucache_set('zv_shm_object', new UcacheSmokePoint(1, ['a' => new stdClass()]));
    ucache_set('zv_shm_objects', ['p' => new UcacheSmokePoint(2, 3), 'list' => [new ArrayObject([1])]]);
    ucache_delete(['zv_shm_object', 'zv_shm_objects']);
});
check('storing and deleting objects 200 times gives back their shared memory', abs($loss) <= 1024, "$loss bytes");

$loss = shm_loss(function () {
    $list = [1, 2, 3];
    $text = str_repeat('t', 500);
    ucache_set(['zv_shm_reflist' => &$list, 'zv_shm_reftext' => &$text]);
    ucache_delete(['zv_shm_reflist', 'zv_shm_reftext']);
});
check('storing and deleting values given by reference to the array form 200 times gives back their shared memory',
      abs($loss) <= 1024, "$loss bytes");

$loss = shm_loss(function () use ($res, $closure, $tooBig) {
    capture(function () use ($res, $closure, $tooBig) {
        attempt(function () use ($res) { return ucache_set('zv_shm_failed', ['list' => [1, 2, 3], 'resource' => $res]); });
        attempt(function () use ($closure) { return ucache_set('zv_shm_failed', ['list' => [1, 2, 3], 'closure' => $closure]); });
        attempt(function () use ($closure) { return ucache_set('zv_shm_failed', $closure); });
        foreach ($tooBig as $value) {
            ucache_set('zv_shm_failed', $value);
        }
    }, $ignored);
    ucache_delete('zv_shm_failed');
});
check('ucache_set() calls that fail give back the shared memory they took', abs($loss) <= 1024, "$loss bytes");
unset($tooBig);

/*
 * Several processes. With the path of the DLL under test as the first argument, these checks start other
 * php.exe processes with that DLL, each group on a cache of its own (a unique ucache.apppoolid): a holder
 * keeps a cache alive while other processes use it, and php -S servers stand for the FastCGI processes of
 * a pool, which survive the fatal error of a request. Every process is bounded by a timeout (a hang is a
 * failure, and the process is ended), and all of them are ended at the end.
 */

/* Quotes one argument of a Windows command line (the rules of CommandLineToArgvW and of the C runtime) */
function cmd_arg($arg)
{
    if ($arg !== '' && strpbrk($arg, " \t\n\v\"") === false) {
        return $arg;
    }

    $quoted = '"';
    $backslashes = 0;
    $length = strlen($arg);
    for ($i = 0; $i < $length; $i++) {
        if ($arg[$i] === '\\') {
            $backslashes++;
            continue;
        }
        $quoted .= ($arg[$i] === '"') ? str_repeat('\\', $backslashes * 2 + 1) . '"' : str_repeat('\\', $backslashes) . $arg[$i];
        $backslashes = 0;
    }

    return $quoted . str_repeat('\\', $backslashes * 2) . '"';
}

/* The arguments of a child php.exe: the DLL under test, on the cache of group $pool. The path of the DLL is
   quoted for the INI parser as build.bat does: in single quotes (taken as they are), or in double quotes if
   the path has a single quote */
function child_php_args($pool, array $ini, $script = null, array $scriptArgs = [])
{
    global $childDll;

    $quote = (strpos($childDll, "'") === false) ? "'" : '"';
    $args = ['-n', '-d', "extension=$quote$childDll$quote", '-d', 'ucache.enablecli=1', '-d', "ucache.apppoolid=$pool"];
    foreach ($ini as $name => $value) {
        $args[] = '-d';
        $args[] = "$name=$value";
    }
    if ($script !== null) {
        $args[] = $script;
    }

    return array_merge($args, $scriptArgs);
}

/* Starts PHP_BINARY with $args; its stdin is a pipe, its stdout and stderr go to files of the work directory */
function child_start($name, array $args)
{
    global $children, $childDir;

    $child = new stdClass();
    $child->name  = $name;
    $child->out   = $childDir . DIRECTORY_SEPARATOR . "$name.out";
    $child->err   = $childDir . DIRECTORY_SEPARATOR . "$name.err";
    $child->exit  = null;
    $child->proc  = null;
    $child->stdin = null;

    $command = implode(' ', array_map('cmd_arg', array_merge([PHP_BINARY], $args)));
    $proc = @proc_open($command, [0 => ['pipe', 'r'], 1 => ['file', $child->out, 'w'], 2 => ['file', $child->err, 'w']], $pipes,
                       $childDir, null, ['bypass_shell' => true]);
    if (is_resource($proc)) {
        $child->proc  = $proc;
        $child->stdin = $pipes[0];
        $children[]   = $child;
    } else {
        $child->exit = 'proc_open() failed';
    }

    return $child;
}

/* The exit code of a child, or null while it runs (proc_get_status() gives the code only once) */
function child_exit($child)
{
    if ($child->exit === null && $child->proc !== null) {
        $status = proc_get_status($child->proc);
        if (!$status['running']) {
            $child->exit = $status['exitcode'];
        }
    }

    return $child->exit;
}

/* What a child printed so far, stdout then stderr */
function child_output($child)
{
    return (string)@file_get_contents($child->out) . (string)@file_get_contents($child->err);
}

/* The end of what a child printed, for FAIL details (without the request log of php -S) */
function child_tail($child)
{
    $output = preg_replace('/^.*(127\.0\.0\.1:\d+ (Accepted|Closing|Closed without sending)|127\.0\.0\.1:\d+ \[\d{3}\]: ).*$/m', '',
                           child_output($child));
    $output = trim(preg_replace('/\s+/', ' ', $output));
    return (strlen($output) > 400) ? '...' . substr($output, -400) : $output;
}

/* The result a child printed ("RESULT " and its serialize()d result in base64), or null */
function child_result($child)
{
    if (preg_match('/^RESULT (\S+)/m', child_output($child), $match)) {
        return unserialize(base64_decode($match[1]));
    }

    return null;
}

/* Waits until a child prints something that matches $pattern: false if it ends or $timeout passes first */
function child_wait_for($child, $pattern, $timeout)
{
    $end = microtime(true) + $timeout;
    while (true) {
        $ended = (child_exit($child) !== null);
        if (preg_match($pattern, child_output($child))) {
            return true;
        }
        if ($ended || microtime(true) >= $end) {
            return false;
        }
        usleep(20000);
    }
}

/* Sends a line to the stdin of a child */
function child_send($child, $line)
{
    if ($child->stdin !== null) {
        @fwrite($child->stdin, "$line\n");
        @fflush($child->stdin);
    }
}

/* Waits for a child to end and returns its exit code; after $timeout it is ended and the result is null */
function child_finish($child, $timeout)
{
    $end = microtime(true) + $timeout;
    while (child_exit($child) === null) {
        if (microtime(true) >= $end) {
            child_kill($child);
            return null;
        }
        usleep(20000);
    }
    child_close($child);

    return $child->exit;
}

/* Ends a child that still runs */
function child_kill($child)
{
    if ($child->proc !== null && child_exit($child) === null) {
        proc_terminate($child->proc);
        for ($i = 0; $i < 250 && child_exit($child) === null; $i++) {
            usleep(20000);
        }
    }
    child_close($child);
}

function child_close($child)
{
    if ($child->stdin !== null) {
        @fclose($child->stdin);
        $child->stdin = null;
    }
    if ($child->proc !== null && child_exit($child) !== null) {
        proc_close($child->proc);
        $child->proc = null;
    }
}

/* Ends every child and removes the work directory */
function children_cleanup()
{
    global $children, $childDir;

    foreach ($children as $child) {
        child_kill($child);
    }
    $children = [];

    /* A file that was just closed can stay locked for a moment (a virus scanner reading it) */
    for ($i = 0; $i < 20 && $childDir !== null && is_dir($childDir); $i++) {
        $files = glob($childDir . DIRECTORY_SEPARATOR . '*');
        foreach (is_array($files) ? $files : [] as $file) {
            @unlink($file);
        }
        if (!@rmdir($childDir)) {
            usleep(50000);
        }
    }
}

/* The reply of a php -S server to GET $path (headers and body), '' if it closed the connection without
   one, null if it did not accept the connection or did not answer within $timeout seconds */
function http_get($port, $path, $timeout)
{
    /* A short connect timeout: Windows takes 2 s to report that nothing listens on the port */
    $socket = @stream_socket_client("tcp://127.0.0.1:$port", $errno, $errstr, 0.5);
    if ($socket === false) {
        return null;
    }
    stream_set_timeout($socket, $timeout);
    @fwrite($socket, "GET $path HTTP/1.0\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n");
    $reply = @stream_get_contents($socket);
    $meta = stream_get_meta_data($socket);
    fclose($socket);

    return $meta['timed_out'] ? null : (string)$reply;
}

/* Starts php -S with the router of these checks, on a free port of 127.0.0.1; null if no server answers */
function server_start($name, $pool, array $ini, &$port)
{
    global $childDir;

    for ($attempt = 1; $attempt <= 5; $attempt++) {
        $port = mt_rand(20000, 60000);
        $socket = @stream_socket_server("tcp://127.0.0.1:$port");
        if ($socket === false) {
            continue;
        }
        fclose($socket);

        $server = child_start("$name-$attempt", array_merge(child_php_args($pool, $ini),
                              ['-S', "127.0.0.1:$port", '-t', $childDir, $childDir . DIRECTORY_SEPARATOR . 'router.php']));
        $end = microtime(true) + 15;
        while (child_exit($server) === null && microtime(true) < $end) {
            if (strpos((string)http_get($port, '/?op=ping', 10), 'pong') !== false) {
                return $server;
            }
            usleep(20000);
        }
        child_kill($server);
    }

    return null;
}

/* Gives a server whose request ended without a reply the time to end, if it does */
function server_exit($server, $reply)
{
    for ($i = 0; $i < 100 && child_exit($server) === null && trim((string)$reply) === ''; $i++) {
        usleep(50000);
    }

    return child_exit($server);
}

/* [memory_free, num_used_blks] of the cache of a holder (the "memory" command), null if it does not answer */
function holder_memory($holder, $timeout = 30)
{
    $before = preg_match_all('/^MEMORY \d+ \d+\s*$/m', child_output($holder));
    child_send($holder, 'memory');
    $end = microtime(true) + $timeout;
    while (child_exit($holder) === null && microtime(true) < $end) {
        if (preg_match_all('/^MEMORY (\d+) (\d+)\s*$/m', child_output($holder), $matches) > $before) {
            return [(int)end($matches[1]), (int)end($matches[2])];
        }
        usleep(20000);
    }

    return null;
}

/* The expected exit code of a process that reaches memory_limit: the fatal error (255), or exit code 1 on PHP 7
   builds such as the CVE-patched 7.2, which end the process instead */
function limit_exit_ok($exit)
{
    return (PHP_VERSION_ID >= 80000) ? $exit === 255 : ($exit === 1 || $exit === 255);
}

$children = [];
$childDir = null;
$childDll = isset($argv[1]) ? $argv[1] : null;

if ($childDll === null) {
    echo "SKIP: the checks with several processes (they need the path of the DLL under test as the first argument)\n";
} elseif (PHP_BINARY === '' || !is_file($childDll)) {
    check('the first argument is the path of the DLL under test', false, ['argument' => $childDll, 'PHP_BINARY' => PHP_BINARY]);
} else {
    $childDll = realpath($childDll);
    $childDir = sys_get_temp_dir() . DIRECTORY_SEPARATOR . 'ucache-test-' . getmypid() . '-' . bin2hex(random_bytes(4));
    $poolBase = 'ucachetest' . bin2hex(random_bytes(4));
    register_shutdown_function('children_cleanup');
}

if ($childDir !== null && !@mkdir($childDir)) {
    check('the work directory of the checks with several processes can be created', false, $childDir);
    $childDir = null;
}

if ($childDir !== null) {
    $holderScript = $childDir . DIRECTORY_SEPARATOR . 'holder.php';
    $clientScript = $childDir . DIRECTORY_SEPARATOR . 'client.php';

    file_put_contents($holderScript, <<<'CHILDSCRIPT'
<?php
/* Keeps the cache of its group alive. Stores its arguments (key value key value ...), prints "READY" and
   the ucache.namesalt it sees, then waits for a line on stdin: "memory" prints "MEMORY ", memory_free and
   num_used_blks, and waits for the next line; "free" deletes every entry and gives memory_free before and
   after; another line is a key to read (serialize()d). Prints "RESULT " and its serialize()d result in
   base64. */
for ($i = 1; $i + 1 < $argc; $i += 2) {
    ucache_set($argv[$i], $argv[$i + 1]);
}
$before = ucache_meminfo();
echo "READY ", ini_get('ucache.namesalt'), "\n";

$line = trim((string)fgets(STDIN));
while ($line === 'memory') {
    $memory = ucache_meminfo();
    echo "MEMORY ", $memory['memory_free'], " ", $memory['num_used_blks'], "\n";
    $line = trim((string)fgets(STDIN));
}
if ($line === 'free') {
    $keys = [];
    $info = ucache_info();
    foreach ($info['ucache_entries'] as $entry) {
        $keys[] = $entry['key_name'];
    }
    if ($keys !== []) {
        ucache_delete($keys);
    }
    $after = ucache_meminfo();
    $result = ['memory_free' => [$before['memory_free'], $after['memory_free']],
               'num_used_blks' => [$before['num_used_blks'], $after['num_used_blks']]];
} else {
    $result = serialize(ucache_get($line));
}
echo "RESULT ", base64_encode(serialize($result)), "\n";
CHILDSCRIPT
    );

    file_put_contents($clientScript, <<<'CHILDSCRIPT'
<?php
/* A process of the checks with several processes; argv[1] says what it does. Prints "RESULT " and its
   serialize()d result in base64. */
class UcacheChildPoint
{
    public $x;
    public $y;

    public function __construct($x, $y)
    {
        $this->x = $x;
        $this->y = $y;
    }
}

/* Its __sleep() raises a fatal error while ucache_set() copies the value that holds the object */
class UcacheChildSleepFatal
{
    public $x = 1;

    public function __sleep()
    {
        trigger_error('fatal error in __sleep', E_USER_ERROR);
        return ['x'];
    }
}

/* Its __sleep() frees a string of the value being copied in (a reference holds it), and a new array of
   the same size (56 bytes), stored in another reference of the value, takes its memory */
class UcacheChildMutator
{
    public $x = 1;

    public function __sleep()
    {
        global $first, $second;

        $first = null;
        $array = [];
        $array[] = 1;
        $array[] = 2;
        $second = $array;
        return ['x'];
    }
}

/* Its __sleep() frees the array being copied in (only a reference holds it), and strings of the size of
   its arrays (56 bytes) take their memory */
class UcacheChildFreer
{
    public $tag = 'm';

    public function __sleep()
    {
        global $data, $junk;

        $data = null;
        $junk = [];
        for ($i = 0; $i < 6000; $i++) {
            $junk[] = str_repeat(chr(65 + $i % 26), 31) . '';
        }
        return ['tag'];
    }
}

/* Its __sleep() passes memory_limit with one allocation (one that grows inside an internal function can end PHP
   8.2.35 and 8.3.36 at their hard limit, exit code 137, instead of the memory_limit fatal error) */
class UcacheChildHungrySleep
{
    public $x = 1;

    public function __sleep()
    {
        $GLOBALS['hog'] = str_repeat('h', 64 * 1048576);
        return ['x'];
    }
}

/* Its __sleep() stores ('sleep_inner') a Serializable whose payload refers to the object itself */
class UcacheChildSleepStores
{
    public static $set = null;
    public $x = 1;

    public function __sleep()
    {
        self::$set = ucache_set('sleep_inner', self_serializable('inner'));
        return ['x'];
    }
}

/* Its __wakeup() reads such a Serializable ('self_alone') */
class UcacheChildWakeupReads
{
    public static $got = null;
    public $x = 1;

    public function __wakeup()
    {
        self::$got = ucache_get('self_alone');
    }
}

/* Declares UcacheChildSelfSerializable, whose payload can refer to the object itself (the serialize() in its
   serialize() shares the context that numbered it). It implements only Serializable, for which PHP 8.1+ raises
   E_DEPRECATED, not reported meanwhile */
function declare_self_serializable()
{
    $reporting = error_reporting();
    error_reporting($reporting & ~E_DEPRECATED);
    eval(<<<'DECLARE'
class UcacheChildSelfSerializable implements Serializable
{
    public $data;

    public function serialize()
    {
        return serialize($this->data);
    }

    public function unserialize($data)
    {
        $this->data = unserialize($data);
    }
}
DECLARE
    );
    error_reporting($reporting);
}

function self_serializable($name)
{
    $object = new UcacheChildSelfSerializable();
    $object->data = ['me' => $object, 'name' => $name];
    return $object;
}

function self_serializable_ok($object, $name)
{
    return $object instanceof UcacheChildSelfSerializable && is_array($object->data) && isset($object->data['me'])
           && $object->data['me'] === $object && $object->data['name'] === $name;
}

/* From now on the warnings and notices go to $notices */
function collect_notices()
{
    $GLOBALS['notices'] = [];
    set_error_handler(function ($errno, $errstr) {
        if ((error_reporting() & $errno) !== 0) {
            $GLOBALS['notices'][] = $errstr;
        }
        return true;
    });
}

/* Prints "SHUTDOWN " and the last error at the end of the process (not after exit code 1 or 137) */
function report_shutdown()
{
    register_shutdown_function(function () {
        $error = error_get_last();
        echo "SHUTDOWN ", ($error === null) ? 'no error' : $error['message'], "\n";
    });
}

function shared_value()
{
    return ['list' => [1, 2, 3], 'map' => ['k' => 'v', 'n' => null], 'point' => new UcacheChildPoint(1, 'two'), 'text' => str_repeat('t', 1000)];
}

function freed_value()
{
    $value = ['first' => new UcacheChildFreer()];
    for ($i = 0; $i < 60; $i++) {
        $value["key$i"] = [str_repeat('v', 40) . $i, $i];
    }
    return $value;
}

/* The objects of 'objects_after_big_step': declared properties, so that each object takes one small block */
class UcacheChildEntity
{
    public $id;
    public $payload;
}

/* $success of the 'success_dtor' case: a static property that holds the only reference to an object whose destructor
   records what the property holds, then writes it */
class UcacheChildSuccessHolder
{
    public static $flag;
}

class UcacheChildSuccessBomb
{
    public static $seen = [];
    public $call;

    public function __construct($call)
    {
        $this->call = $call;
    }

    public function __destruct()
    {
        $flag = UcacheChildSuccessHolder::$flag;
        self::$seen[$this->call] = is_object($flag) ? get_class($flag) : $flag;
        unset($flag);
        UcacheChildSuccessHolder::$flag = str_repeat('w', 100);
    }
}

/* Each __sleep() adds 300 globals, which reallocates the symbol table, then makes strings that take the freed memory */
class UcacheChildGlobalsGrower
{
    public static $calls = 0;
    public $n = 1;

    public function __sleep()
    {
        $calls = ++self::$calls;
        for ($i = 0; $i < 300; $i++) {
            $GLOBALS["grow_{$calls}_$i"] = $i;
        }
        $junk = [];
        for ($i = 0; $i < 2000; $i++) {
            $junk[] = str_repeat(chr(65 + $i % 26), 1000 + $i % 700);
        }
        return ['n'];
    }
}

/* The size of UcacheChildSuccessBomb, without a destructor */
class UcacheChildSuccessFiller
{
    public $call;
}

/* $levels arrays, each one the only element of the one before; the deepest holds 'leaf' (the value is level 1) */
function nested_arrays($levels)
{
    $value = 'leaf';
    for ($i = 0; $i < $levels; $i++) {
        $value = [$value];
    }
    return $value;
}

/* A string of about 50 KB, different for each $i: the values of the cases that fill a cache */
function fill_value($i)
{
    return str_repeat(chr(65 + $i % 26), 50000) . $i;
}

/* The shortest of 5 ucache_set() calls of $value, each one followed by ucache_delete(), in seconds; -1 if one fails */
function best_store_time($value)
{
    $best = INF;
    for ($i = 0; $i < 5; $i++) {
        $start = microtime(true);
        $set = ucache_set('timed', $value);
        $best = min($best, microtime(true) - $start);
        ucache_delete('timed');
        if ($set !== true) {
            return -1;
        }
    }
    return $best;
}

switch ($argv[1]) {
    case 'set':
        $result = ucache_set('shared', shared_value());
        break;

    case 'get':
        $got = ucache_get('shared', $success);
        $result = ['same' => serialize($got) === serialize(shared_value()), 'serialized' => serialize($got), 'success' => $success];
        break;

    case 'salted':
        $result = [ucache_get('salted'), ini_get('ucache.namesalt')];
        break;

    case 'exists':
        /* Whether the cache holds each of the keys argv[2], argv[3], ... */
        $result = [];
        foreach (array_slice($argv, 2) as $key) {
            $result[$key] = ucache_exists($key);
        }
        break;

    case 'setbig':
        $value = [];
        for ($i = 0; $i < 400; $i++) {
            $value[] = str_repeat(chr(65 + $i % 26), 50000) . $i;
        }
        $result = ucache_set('big', $value);
        break;

    case 'lock':
        $start = microtime(true);
        $result = [ucache_set('lock_probe', 1), round(microtime(true) - $start, 3)];
        break;

    case 'fatal':
        $value = ['list' => range(1, 5000), 'text' => str_repeat('x', 100000)];
        for ($i = 0; $i < 200; $i++) {
            $value["k$i"] = "string $i";
        }
        $value['o'] = new UcacheChildSleepFatal();
        $result = ucache_set('victim', $value);
        break;

    case 'stale':
        $first = str_repeat('a', 24);
        $second = null;
        $value = ['a' => &$first, 'o' => new UcacheChildMutator(), 'b' => &$second];
        $set = ucache_set('stale', $value);
        unset($value);
        $got = ucache_get('stale');
        $result = [$set, $got['a'], $got['b']];
        break;

    case 'freed':
        $data = freed_value();
        $outer = [&$data];
        $set = ucache_set('freed', $outer);
        unset($outer);
        $data = null;
        $junk = null;
        $got = ucache_get('freed');
        $count = $bad = -1;
        if (is_array($got) && isset($got[0]) && is_array($got[0])) {
            $count = count($got[0]);
            $bad = 0;
            foreach (freed_value() as $key => $value) {
                $ok = ($key === 'first') ? (isset($got[0][$key]) && $got[0][$key] instanceof UcacheChildFreer)
                                         : (isset($got[0][$key]) && $got[0][$key] === $value);
                $bad += $ok ? 0 : 1;
            }
        }
        $result = [$set, $count, $bad];
        break;

    case 'success_dtor':
        /* $success holds the only reference to an object whose destructor writes $success itself: each call stores false
           before it releases the object, and true at the end if it succeeds ([what each destructor saw, $success after each
           call]). Releasing first freed the object twice; the objects and strings made afterwards reuse freed memory, so
           that such a double free ends the process */
        ucache_set('dtor_counter', 100);
        $after = [];
        foreach (['get', 'get_miss', 'get_array', 'inc', 'dec'] as $call) {
            UcacheChildSuccessHolder::$flag = new UcacheChildSuccessBomb($call);
            switch ($call) {
                case 'get':       ucache_get('dtor_counter', UcacheChildSuccessHolder::$flag); break;
                case 'get_miss':  ucache_get('dtor_missing', UcacheChildSuccessHolder::$flag); break;
                case 'get_array': ucache_get(['dtor_counter'], UcacheChildSuccessHolder::$flag); break;
                case 'inc':       ucache_inc('dtor_counter', 1, UcacheChildSuccessHolder::$flag); break;
                case 'dec':       ucache_dec('dtor_counter', 1, UcacheChildSuccessHolder::$flag); break;
            }
            $after[$call] = UcacheChildSuccessHolder::$flag;
        }
        $churn = [];
        for ($i = 0; $i < 30000; $i++) {
            $churn[] = new UcacheChildSuccessFiller();
            $churn[] = 'k' . $i;
        }
        $churn = null;
        $result = [UcacheChildSuccessBomb::$seen, $after];
        break;

    case 'fiber_set':
        /* ucache_set() inside a Fiber of a value nested 10000 levels deep, which the stack of the main thread takes: [result,
           warnings, stored, shared memory taken] (after a first ucache_set() and ucache_delete() to warm the cache up) */
        collect_notices();
        $value = nested_arrays(10000);
        ucache_set('warm_up', [[1]]);
        ucache_delete('warm_up');
        $free = ucache_meminfo()['memory_free'];
        try {
            $fiber = new Fiber(function () use ($value) {
                return ucache_set('fiber_deep', $value);
            });
            $fiber->start();
            $set = $fiber->getReturn();
        } catch (Throwable $e) {
            $set = get_class($e) . ': ' . $e->getMessage();
        }
        $result = [$set, $notices, ucache_exists('fiber_deep'), $free - ucache_meminfo()['memory_free']];
        break;

    case 'fiber_get':
        /* ucache_get() inside a Fiber of an entry nested 10000 levels deep, stored from the main thread: [stored, result,
           $success, warnings, the entry read from the main thread afterwards is intact] */
        collect_notices();
        $value = nested_arrays(10000);
        $set = ucache_set('fiber_deep', $value);
        try {
            $fiber = new Fiber(function () {
                $got = ucache_get('fiber_deep', $success);
                return [($got === false) ? false : gettype($got), $success];
            });
            $fiber->start();
            $got = $fiber->getReturn();
        } catch (Throwable $e) {
            $got = [get_class($e) . ': ' . $e->getMessage(), null];
        }
        $result = [$set, $got[0], $got[1], $notices, ucache_get('fiber_deep') === $value];
        break;

    case 'free_speed':
        /* 40000 small entries, then every second one deleted: each freed block lies between used blocks ([stored, seconds of
           the deletes, the rest read back, the free memory of the empty cache back after a clear]) */
        $n = 40000;
        ucache_clear();
        $free0 = ucache_meminfo()['memory_free'];
        $stored = 0;
        for ($i = 0; $i < $n; $i++) {
            $stored += ucache_set("f$i", [$i]) ? 1 : 0;
        }
        $start = microtime(true);
        for ($i = 0; $i < $n; $i += 2) {
            ucache_delete("f$i");
        }
        $seconds = microtime(true) - $start;
        $rest = true;
        for ($i = 1; $i < $n; $i += 2) {
            $rest = $rest && ucache_get("f$i") === [$i];
        }
        ucache_clear();
        $result = [$stored, $seconds, $rest, ucache_meminfo()['memory_free'] === $free0];
        break;

    case 'globals':
        /* ucache_set() of $GLOBALS as the array form and as a value: [stored, read back with the variable set and without
           the one unset, keys that failed, the variable stored under its name]. Before PHP 8.1 its slots point to the
           variables (IS_INDIRECT), and the copy takes their values, as serialize() does. An object of the globals adds
           globals when it is serialized: the array form walks a copy of the symbol table, which it reallocates */
        $probe = ['n' => 1, 'list' => [1, 2]];
        $gone = 'x';
        unset($gone);
        $grower = new UcacheChildGlobalsGrower();
        $asArray = ucache_set($GLOBALS);
        $asValue = ucache_set('globals_value', $GLOBALS);
        $got = ucache_get('globals_value');
        $result = [$asValue, is_array($got) && isset($got['probe']) && $got['probe'] === $probe && !array_key_exists('gone', $got),
                   $asArray, ucache_get('probe') === $probe];
        break;

    case 'fiber_fits':
        /* ucache_set() and ucache_get() inside a Fiber of a value nested 1000 levels deep, which the stack of the Fiber
           takes: [stored, read back intact, warnings] */
        collect_notices();
        $value = nested_arrays(1000);
        try {
            $fiber = new Fiber(function () use ($value) {
                $set = ucache_set('fiber_fits', $value);
                return [$set, ucache_get('fiber_fits') === $value];
            });
            $fiber->start();
            $got = $fiber->getReturn();
        } catch (Throwable $e) {
            $got = [get_class($e) . ': ' . $e->getMessage(), null];
        }
        $result = [$got[0], $got[1], $notices];
        break;

    case 'sleep_oom':
        /* A list, then an object whose __sleep() passes memory_limit (8 MB above the usage) */
        report_shutdown();
        $list = [];
        for ($i = 0; $i < 100000; $i++) {
            $list[] = "item $i";
        }
        $value = ['list' => $list, 'object' => new UcacheChildHungrySleep()];
        unset($list);
        ini_set('memory_limit', (string)(memory_get_usage(true) + 8 * 1048576));
        $result = ucache_set('sleep_oom', $value);
        break;

    case 'shared_strings':
        /* 200000 strings, each held by two lists: phase 2 of the copy-in records each one in its translation table,
           for which a memory_limit 4 MB above the usage leaves no room in request memory */
        report_shutdown();
        $first = [];
        $second = [];
        for ($i = 0; $i < 200000; $i++) {
            $string = "string $i";
            $first[] = $string;
            $second[] = $string;
        }
        $value = ['first' => $first, 'second' => $second];
        unset($first, $second, $string);
        gc_collect_cycles();
        ini_set('memory_limit', (string)(memory_get_usage(true) + 4 * 1048576));
        $result = ucache_set('shared_strings', $value);
        break;

    case 'shared_ratio':
        /* 10000 distinct strings of 4 KB held by two lists, then by one list: the best time of ucache_set() for each, in
           seconds. Held by two lists, each string goes through the translation table of phase 2 */
        $first = [];
        $second = [];
        for ($i = 0; $i < 10000; $i++) {
            $string = str_repeat(chr(65 + $i % 26), 4088) . sprintf('%08d', $i);
            $first[] = $string;
            $second[] = $string;
        }
        unset($string);
        $twice = ['first' => $first, 'second' => $second];
        unset($second);
        $result = ['two lists' => best_store_time($twice)];
        unset($twice);                  /* each string is held by $first only now */
        $once = ['first' => $first];
        unset($first);
        $result['one list'] = best_store_time($once);
        break;

    case 'transient':
        /* 150000 small arrays. Phase 1 holds them in a table that doubles to about 10 MB at the end: for a moment the
           old and the new table take 15 MB, and memory_limit is 12 MB above the usage */
        report_shutdown();
        $value = [];
        for ($i = 0; $i < 150000; $i++) {
            $value[] = [$i, "s$i"];
        }
        gc_collect_cycles();
        ini_set('memory_limit', (string)(memory_get_usage(true) + 12 * 1048576));
        $result = ucache_set('transient', $value);
        break;

    case 'transient_array':
        /* The same 150000 small arrays through the array form of ucache_set(), followed by two small values */
        report_shutdown();
        $value = [];
        for ($i = 0; $i < 150000; $i++) {
            $value[] = [$i, "s$i"];
        }
        gc_collect_cycles();
        ini_set('memory_limit', (string)(memory_get_usage(true) + 12 * 1048576));
        $result = ucache_set(['transient_a' => $value, 'transient_b' => 'stored after it', 'transient_c' => [1, 2, 3]]);
        break;

    case 'objects':
        /* 20000 objects whose serializations take about 350 bytes each */
        $value = [];
        for ($i = 0; $i < 20000; $i++) {
            $object = new stdClass();
            $object->id = $i;
            $object->payload = str_repeat(chr(65 + $i % 26), 300);
            $value[] = $object;
        }
        unset($object);
        $length = strlen(serialize($value[0]));
        if (function_exists('memory_reset_peak_usage')) {
            memory_reset_peak_usage();   /* PHP 8.2+; before, the peak is above the usage by at most the last growth of $value */
        }
        $before = memory_get_usage();
        $set = ucache_set('objects', $value);
        $growth = memory_get_peak_usage() - $before;
        $got = ucache_get('objects');
        $different = 0;
        foreach ($value as $i => $object) {
            if (!isset($got[$i]) || !($got[$i] instanceof stdClass) || get_object_vars($got[$i]) !== get_object_vars($object)) {
                $different++;
            }
        }
        $result = ['set' => $set, 'serialized length' => $length, 'peak growth' => $growth,
                   'read back' => is_array($got) ? count($got) : gettype($got), 'different' => $different];
        break;

    case 'objects_over_limit':
        /* 200 objects that share one 1 MB string: their serializations take 200 MB, memory_limit is 16 MB above the usage.
           Also the growth of the peak working set during ucache_set() (ru_maxrss: in KB, on Windows) */
        report_shutdown();
        $data = str_repeat('m', 1048576);
        $value = [];
        for ($i = 0; $i < 200; $i++) {
            $object = new stdClass();
            $object->i = $i;
            $object->data = $data;
            $value[] = $object;
        }
        unset($object, $data);
        gc_collect_cycles();
        ini_set('memory_limit', (string)(memory_get_usage(true) + 16 * 1048576));
        collect_notices();
        $before = getrusage()['ru_maxrss'];
        $set = ucache_set('over_limit', $value);
        $growth = getrusage()['ru_maxrss'] - $before;
        $result = [$set, ucache_exists('over_limit'), $notices, $growth];
        break;

    case 'objects_after_big_step':
        /* About 60 MB of small strings, freed: PHP keeps the pages they took, so the heap stays about 80 MB big while little of
           it is in use (as in a FastCGI process after a big request). Then 60000 objects that share one 1 KB string, whose
           serializations take about 64 MB */
        $strings = [];
        for ($i = 0; $i < 480000; $i++) {
            $strings[] = str_repeat('x', 100) . $i;
        }
        unset($strings);
        $payload = str_repeat('p', 1000);
        $value = [];
        for ($i = 0; $i < 60000; $i++) {
            $object = new UcacheChildEntity();
            $object->id = $i;
            $object->payload = $payload;
            $value[] = $object;
        }
        unset($object);
        $usage = [round(memory_get_usage() / 1048576, 1), round(memory_get_usage(true) / 1048576, 1)];
        $set = ucache_set('after_big_step', $value);
        ini_set('memory_limit', '512M');    /* read back, the objects hold 60000 strings of 1 KB */
        $got = ucache_get('after_big_step');
        $result = ['set' => $set, 'read back' => is_array($got) ? count($got) : gettype($got),
                   'last intact' => is_array($got) && isset($got[59999]) && $got[59999] == $value[59999],
                   'memory in use and heap size before, in MB' => $usage];
        break;

    case 'list_oom':
    case 'info_oom':
    case 'getarray_oom':
        /* Each one is sized so that the loop it checks is the first to pass memory_limit, 8 MB above the usage */
        report_shutdown();
        $keys = [];
        $count = ($argv[1] === 'list_oom') ? 8000 : 100000;
        for ($i = 0; $i < $count; $i++) {
            if ($argv[1] === 'list_oom') {
                ucache_set(sprintf('%07d', $i) . str_repeat('L', 4083), $i);      /* the key list takes 32 MB */
            } elseif ($argv[1] === 'info_oom') {
                ucache_set("i$i", $i);                                            /* the key list fits, the array of entries does not */
            } else {
                $keys[] = "g$i";
                ucache_set("g$i", str_repeat('v', 194) . sprintf('%06d', $i));    /* strings: no copy-out loop, only the array of values */
            }
        }
        gc_collect_cycles();
        ini_set('memory_limit', (string)(memory_get_usage(true) + 8 * 1048576));
        $result = ($argv[1] === 'getarray_oom') ? ucache_get($keys) : ucache_info();
        $result = is_array($result) ? count($result) : $result;
        break;

    case 'big_delete':
        /* Stores an entry of 100000 small arrays, waits for a line on stdin, deletes the entry, then creates the file argv[2] */
        $value = [];
        for ($i = 0; $i < 100000; $i++) {
            $value[] = ['id' => $i, 'name' => "name $i"];
        }
        $set = ucache_set('big_entry', $value);
        unset($value);
        echo "STORED\n";
        fgets(STDIN);
        $start = microtime(true);
        $deleted = ucache_delete('big_entry');
        $seconds = round(microtime(true) - $start, 3);
        touch($argv[2]);
        $result = [$set, $deleted, $seconds];
        break;

    case 'stall_reader':
        /* Waits for a line on stdin, then reads a small entry in a loop until the file argv[2] exists: the longest read */
        ucache_set('small', 1);
        echo "READY\n";
        fgets(STDIN);
        echo "READING\n";
        $longest = 0;
        $end = microtime(true) + 30;
        while (!file_exists($argv[2]) && microtime(true) < $end) {
            $start = microtime(true);
            ucache_get('small');
            $longest = max($longest, microtime(true) - $start);
        }
        $result = [round($longest, 3), file_exists($argv[2])];
        break;

    case 'self_serializable':
        /* A Serializable whose payload refers to the object itself, stored and read at the top level; then read by a
           ucache_get() inside __wakeup() of an object that unserialize() or ucache_get() reads ([read intact, outer
           value intact, notices] each) */
        collect_notices();
        declare_self_serializable();
        $result = [];
        try {
            $set = ucache_set('self', ['object' => self_serializable('self'), 'list' => [1, 2]]);
            $got = ucache_get('self');
            $result['top level'] = [$set, is_array($got) && isset($got['object']) && self_serializable_ok($got['object'], 'self')
                                    && $got['list'] === [1, 2], $notices];
            ucache_set('self_alone', self_serializable('self'));
            foreach (['unserialize()', 'ucache_get()'] as $outer) {
                $notices = [];
                UcacheChildWakeupReads::$got = null;
                $value = ['reader' => new UcacheChildWakeupReads(), 'n' => 1];
                $back = ($outer === 'unserialize()') ? unserialize(serialize($value))
                                                     : (ucache_set('wakeup_outer', $value) ? ucache_get('wakeup_outer') : false);
                $result[$outer] = [self_serializable_ok(UcacheChildWakeupReads::$got, 'self'),
                                   is_array($back) && isset($back['reader']) && $back['reader'] instanceof UcacheChildWakeupReads && $back['n'] === 1,
                                   $notices];
            }
        } catch (Throwable $e) {
            $result['error'] = get_class($e) . ': ' . $e->getMessage();
        }
        break;

    case 'sleep_serializable':
        /* The same, stored by a ucache_set() inside __sleep() of an object that ucache_set() stores, then serialize() serializes */
        collect_notices();
        declare_self_serializable();
        try {
            $outerSet = ucache_set('sleep_outer', ['object' => new UcacheChildSleepStores(), 'n' => 1]);
            $innerSet = UcacheChildSleepStores::$set;
            $inner = self_serializable_ok(ucache_get('sleep_inner'), 'inner');
            $outer = ucache_get('sleep_outer');
            $outerOk = is_array($outer) && isset($outer['object']) && $outer['object'] instanceof UcacheChildSleepStores && $outer['n'] === 1;
            ucache_delete('sleep_inner');
            UcacheChildSleepStores::$set = null;
            $serialized = serialize(['object' => new UcacheChildSleepStores(), 'n' => 1]);
            $back = unserialize($serialized);
            $result = [$outerSet, $innerSet, $inner, $outerOk, UcacheChildSleepStores::$set, self_serializable_ok(ucache_get('sleep_inner'), 'inner'),
                       is_array($back) && $back['object'] instanceof UcacheChildSleepStores && $back['n'] === 1, $notices];
        } catch (Throwable $e) {
            $result = [get_class($e) . ': ' . $e->getMessage()];
        }
        break;

    case 'inc_alive':
        /* Run with ucache.gc.threshold 100 (every write runs a scavenger step) and ucache.gc.ttl 2. Only ucache_inc()
           uses 'counter' and only ucache_cas() uses 'swapped', every 0.5 s; nothing uses 'untouched'. [values of
           ucache_inc(), results of ucache_cas(), counter exists, swapped exists, untouched exists] */
        ucache_set('counter', 0);
        ucache_set('swapped', 0);
        ucache_set('untouched', 1);
        $values = [];
        $swaps = [];
        for ($i = 0; $i < 6; $i++) {
            usleep(500000);
            $values[] = ucache_inc('counter');
            $swaps[] = ucache_cas('swapped', $i, $i + 1);
        }
        $result = [$values, $swaps, ucache_exists('counter'), ucache_exists('swapped'), ucache_exists('untouched')];
        break;

    case 'fill_expired':
        /* Run with ucache.size 5. Values of about 50 KB with ttl 1 until a ucache_set() fails (the cache is full, and
           nothing has expired yet), then, once they have expired, a value of 1 MB without a ttl: more than the lookup of
           its key can free in its hash bucket, so it needs the full scavenger pass */
        $stored = 0;
        $set = true;
        for ($i = 0; $i < 1000 && $set; $i++) {
            $set = ucache_set("e$i", fill_value($i), 1);
            $stored += $set ? 1 : 0;
        }
        $left = ucache_info(true)['total_item_count'];
        usleep(2500000);
        $after = ucache_set('after', str_repeat('a', 1048576));
        $info = ucache_info();
        $keys = [];
        foreach ($info['ucache_entries'] as $entry) {
            $keys[] = $entry['key_name'];
        }
        $result = ['values stored' => $stored, 'the set that ran out failed' => !$set, 'entries after that set' => $left,
                   '1 MB set after the expiry' => $after, 'entries listed at the end' => count($keys),
                   '1 MB value listed' => in_array('after', $keys, true), 'total_scavenged_count' => $info['total_scavenged_count'],
                   'last_scavenge_count' => $info['last_scavenge_count'], 'last_scavenge_seconds' => $info['last_scavenge_seconds'],
                   'total_evicted_count' => $info['total_evicted_count']];
        break;

    case 'fill_evict':
        /* Run with ucache.size 5. 'Cold' values of about 50 KB without a ttl until less than half of the memory is free,
           then, 2.2 s later, ucache_get() of the first 10 cold ones (which makes them recently used), then 60 'hot' ones
           (3 MB, more than the free memory), one by one. Counts */
        $cold = 0;
        $coldFailed = 0;
        $memory = ucache_meminfo();
        while ($cold < 1000 && $memory['memory_free'] >= $memory['memory_total'] / 2) {
            $coldFailed += ucache_set("c$cold", fill_value($cold)) ? 0 : 1;
            $cold++;
            $memory = ucache_meminfo();
        }
        usleep(2200000);
        $coldRead = 0;
        for ($i = 0; $i < 10; $i++) {
            $coldRead += (ucache_get("c$i") === fill_value($i)) ? 1 : 0;
        }
        $hotStored = 0;
        for ($i = 0; $i < 60; $i++) {
            $hotStored += ucache_set("h$i", fill_value($i)) ? 1 : 0;
        }
        $hotPresent = 0;
        for ($i = 0; $i < 60; $i++) {
            $hotPresent += ucache_exists("h$i") ? 1 : 0;
        }
        $readGone = 0;
        $coldGone = 0;
        for ($i = 0; $i < $cold; $i++) {
            $gone = ucache_exists("c$i") ? 0 : 1;
            $coldGone += $gone;
            $readGone += ($i < 10) ? $gone : 0;
        }
        $info = ucache_info(true);
        $result = ['cold stored' => $cold - $coldFailed, 'cold failed' => $coldFailed, 'cold read' => $coldRead,
                   'memory free before the hot ones' => $memory['memory_free'], 'hot stored' => $hotStored, 'hot present' => $hotPresent,
                   'cold gone' => $coldGone, 'cold read gone' => $readGone,
                   'total_scavenged_count' => $info['total_scavenged_count'], 'total_evicted_count' => $info['total_evicted_count']];
        if (ini_get('ucache.gc.evict') === '0') {
            /* The full pass of the first hot set that did not fit removed nothing: for a second, the processes skip it.
               A set 0.6 s later fails without a pass, so 1.2 s after that pass, it is still the last one */
            usleep(600000);
            $result['set while the pass is skipped'] = ucache_set('guard', fill_value(999));
            usleep(600000);
            $result['seconds since the last pass'] = ucache_info(true)['last_scavenge_seconds'];
        }
        break;

    case 'fill_mixed':
        /* Run with ucache.size 5 and ucache.gc.evict 25. Values of about 50 KB without a ttl until less than 40% of the
           memory is free, then with ttl 1 until less than 10% is free; 2.5 s later, a value of 1 MB (more than is free,
           less than what has expired). 'big' has a hash bucket of its own among t0..t999 (10223 buckets in 5 MB), so its
           lookup removes none of them on contact: the full pass removes them all. Counts */
        $cold = 0;
        $expiring = 0;
        $memory = ucache_meminfo();
        while ($cold < 1000 && $memory['memory_free'] >= $memory['memory_total'] * 0.4) {
            ucache_set("c$cold", fill_value($cold));
            $cold++;
            $memory = ucache_meminfo();
        }
        while ($expiring < 1000 && $memory['memory_free'] >= $memory['memory_total'] * 0.1) {
            ucache_set("t$expiring", fill_value($expiring), 1);
            $expiring++;
            $memory = ucache_meminfo();
        }
        usleep(2500000);
        $set = ucache_set('big', str_repeat('b', 1048576));
        $coldGone = 0;
        for ($i = 0; $i < $cold; $i++) {
            $coldGone += ucache_exists("c$i") ? 0 : 1;
        }
        $info = ucache_info(true);
        $result = ['cold' => $cold, 'expiring' => $expiring, 'memory free before the wait' => $memory['memory_free'], '1 MB set' => $set,
                   'cold gone' => $coldGone, 'total_scavenged_count' => $info['total_scavenged_count'],
                   'total_evicted_count' => $info['total_evicted_count']];
        break;

    case 'contact_free':
        /* Expired entries, each the only one in its hash bucket (8 MB cache: 16381 buckets): what memory_free gains
           during the ucache_exists(), ucache_inc() and ucache_cas() that remove them */
        ucache_set('smoke_ttl_exists', str_repeat('e', 4000), 1);
        ucache_set('smoke_ttl_inc', 1, 1);
        ucache_set('smoke_ttl_cas', 1, 1);
        usleep(2200000);
        $result = [];
        foreach (['exists' => 'smoke_ttl_exists', 'inc' => 'smoke_ttl_inc', 'cas' => 'smoke_ttl_cas'] as $call => $key) {
            $before = ucache_meminfo()['memory_free'];
            $value = ($call === 'exists') ? ucache_exists($key) : (($call === 'inc') ? ucache_inc($key) : ucache_cas($key, 1, 2));
            $result[$call] = [$value, ucache_meminfo()['memory_free'] - $before];
        }
        $result['entries left'] = ucache_info(true)['total_item_count'];
        break;
}
echo "RESULT ", base64_encode(serialize($result)), "\n";
CHILDSCRIPT
    );

    file_put_contents($childDir . DIRECTORY_SEPARATOR . 'router.php', <<<'CHILDSCRIPT'
<?php
/* Router of the php -S servers of the checks with several processes */

/* Its __sleep() raises a fatal error (E_USER_ERROR, deprecated since PHP 8.4) */
class UcacheRouterSleepFatal
{
    public $x = 1;

    public function __sleep()
    {
        trigger_error('fatal error in __sleep', E_USER_ERROR);
        return ['x'];
    }
}

$op = isset($_GET['op']) ? $_GET['op'] : '';
if ($op === 'sleep_fatal') {
    /* 3000 objects with 1 KB payloads, then an object whose __sleep() raises a fatal error: phase 1 of the copy-in has
       serialized the 3000 objects (3 MB of persistent memory) when it comes. With &shutdown, a shutdown function then
       serializes a Serializable whose payload refers to the object itself (PHP 8.1+ deprecates such a class: not reported) */
    error_reporting(E_ALL & ~E_DEPRECATED);
    if (isset($_GET['shutdown'])) {
        eval('class UcacheRouterSelfSerializable implements Serializable {
                  public $data;
                  public function serialize() { return serialize($this->data); }
                  public function unserialize($data) { $this->data = unserialize($data); }
              }');
        register_shutdown_function(function () {
            /* A serialize() that recursed without end would reach it soon, before the end of the C stack */
            ini_set('memory_limit', (string)(memory_get_usage(true) + 2 * 1048576));
            $object = new UcacheRouterSelfSerializable();
            $object->data = ['me' => $object];
            echo "\nSHUTDOWN ", serialize($object), "\n";
        });
    }
    $value = [];
    for ($i = 0; $i < 3000; $i++) {
        $object = new stdClass();
        $object->id = $i;
        $object->payload = str_repeat(chr(65 + $i % 26), 1000);
        $value[] = $object;
    }
    $value[] = new UcacheRouterSleepFatal();
    ucache_set('sleep_fatal', $value);
    echo 'not reached';
} elseif ($op === 'maxrss') {
    /* The peak working set of the process, in KB on Windows */
    $usage = getrusage();
    echo 'maxrss ', $usage['ru_maxrss'];
} elseif ($op === 'get') {
    $value = ucache_get('big');   /* 20 MB: the copy runs out of memory_limit (8M) */
    echo 'got ', is_array($value) ? count($value) : var_export($value, true);
} elseif ($op === 'inc_loop') {
    /* With ucache.gc.threshold 100, every ucache_inc() runs a scavenger step holding the cache lock. Every
       32-byte block this loop allocates stays in use, except one that the step would allocate and free
       there (an alloc_info), which substr() takes back. So when the 32-byte blocks run out, such an
       allocation is the one that needs a new chunk, past memory_limit */
    ucache_set('gc_key', 1);
    $count = 200000;
    $strings = array_fill(0, $count, null);
    $base = 'abcdefghijkl' . $op;
    ini_set('memory_limit', ((int)(memory_get_usage(true) / 1048576) + 4) . 'M');
    for ($i = 0; $i < $count; $i++) {
        ucache_inc('gc_key');
        $strings[$i] = substr($base, 0, 7);
    }
    echo 'no fatal error';
} elseif ($op === 'ttl_set') {
    /* An entry that expires after 1 second, one without a ttl, and one that expires after 300 seconds (a scavenger that
       compared milliseconds with the ttl would remove it within the 2.5 s wait) */
    echo 'stored ', var_export(ucache_set('expiring', 'e', 1) && ucache_set('lasting', 'l') && ucache_set('later', 'h', 300), true);
} elseif ($op === 'listed') {
    /* The keys that ucache_info() lists, expired entries included (it removes nothing and does not count as a use), and
       what it reports about the scavenger. With &disable, the request then switches the cache off, which the end of the
       request still sees */
    $keys = [];
    $info = ucache_info();
    foreach ($info['ucache_entries'] as $entry) {
        $keys[] = $entry['key_name'];
    }
    sort($keys);
    echo 'listed [', implode(',', $keys), '] scavenged ', $info['total_scavenged_count'], ' ', $info['last_scavenge_count'], ' ',
         var_export($info['last_scavenge_seconds'], true);
    if (isset($_GET['disable'])) {
        ini_set('ucache.enabled', '0');
    }
} elseif ($op === 'poke') {
    /* A write, which runs a scavenger step while ucache.gc.threshold allows it. Its lookup removes the expired entries of
       the hash bucket of 'poke', which is not the one of 'expiring' (of the 16381 buckets of the default cache): the step
       is what removes 'expiring' */
    echo 'poked ', var_export(ucache_set('poke', 1), true);
} elseif ($op === 'seed') {
    /* 8000 entries with 4000-character keys: their list takes 32 MB */
    $stored = 0;
    for ($i = 0; $i < 8000; $i++) {
        $stored += ucache_set(str_pad((string)$i, 4000, 'k'), $i) ? 1 : 0;
    }
    echo 'seeded ', $stored;
} elseif ($op === 'info') {
    $info = ucache_info();
    echo 'info ', is_array($info) ? count($info['ucache_entries']) : var_export($info, true);
} else {
    echo 'pong';
}
CHILDSCRIPT
    );

    /* One process stores a value and another one reads it, while a third keeps the cache alive (a cache
       exists while a process has it open) */
    $pool = $poolBase . 'a';
    $holder = child_start('a-holder', child_php_args($pool, [], $holderScript));
    $ready = child_wait_for($holder, '/^READY/m', 30);
    $setter = child_start('a-set', child_php_args($pool, [], $clientScript, ['set']));
    $setExit = child_finish($setter, 30);
    $reader = child_start('a-get', child_php_args($pool, [], $clientScript, ['get']));
    $getExit = child_finish($reader, 30);
    child_send($holder, 'shared');
    $holderExit = child_finish($holder, 30);
    $set = child_result($setter);
    $got = child_result($reader);
    $held = child_result($holder);
    check('a value stored by one process is read by another one with the same ucache.apppoolid, while a third keeps the cache',
          $ready && $setExit === 0 && $set === true && $getExit === 0 && is_array($got) && $got['same'] === true && $got['success'] === true
          && $holderExit === 0 && $held === $got['serialized'],
          ['exit codes (holder, setter, reader)' => [$holderExit, $setExit, $getExit], 'set' => $set, 'read' => is_array($got) ? $got['same'] : $got,
           'output' => [child_tail($holder), child_tail($setter), child_tail($reader)]]);

    /* A php -S process runs out of memory_limit while ucache_get() copies a big value out, which it does
       holding the cache lock: the fatal error must not leave the lock taken. On PHP 8.2.35 and 8.3.36
       memory_limit is only raised when ucache_get() has returned (false); some PHP 7 builds end the process
       instead */
    $pool = $poolBase . 'b';
    $server = server_start('b-server', $pool, ['ucache.size' => '32', 'memory_limit' => '8M', 'display_errors' => '1', 'html_errors' => '0'], $port);
    if ($server === null) {
        check('a php -S server starts', false);
    } else {
        $setter = child_start('b-set', child_php_args($pool, ['ucache.size' => '32'], $clientScript, ['setbig']));
        $setExit = child_finish($setter, 60);
        $reply = http_get($port, '/?op=get', 60);
        $serverExit = server_exit($server, $reply);
        $alive = ($serverExit === null) && strpos((string)http_get($port, '/?op=ping', 10), 'pong') !== false;
        $probe = child_start('b-lock', child_php_args($pool, ['ucache.size' => '32'], $clientScript, ['lock']));
        $probeExit = child_finish($probe, 10);
        $lock = child_result($probe);
        $outOfMemory = strpos((string)$reply, 'Allowed memory size') !== false || strpos((string)$reply, 'got false') !== false;
        $endedOnLimit = PHP_VERSION_ID < 80000 && $serverExit === 1 && strpos(child_output($server), 'Allowed memory size') !== false;
        check('a php -S process that runs out of memory_limit while ucache_get() copies a value out survives and gives the cache lock back'
              . ($endedOnLimit ? ' (this PHP 7 build ends the process on memory_limit instead, which gives the lock back too)' : ''),
              $setExit === 0 && child_result($setter) === true && (($alive && $outOfMemory) || $endedOnLimit)
              && $probeExit === 0 && is_array($lock) && $lock[0] === true && $lock[1] < 5,
              ['set' => [$setExit, child_result($setter)], 'reply' => substr(trim(preg_replace('/\s+/', ' ', (string)$reply)), -300),
               'server exit code' => $serverExit, 'server alive' => $alive, 'server output' => child_tail($server),
               'other process (exit code, [set, seconds])' => [$probeExit, $lock]]);
        child_kill($server);
    }

    /* A fatal error while ucache_set() copies a value in, which it does without the cache lock: the shared
       memory the copy had taken must come back (a holder keeps the cache and measures it) */
    $pool = $poolBase . 'c';
    $holder = child_start('c-holder', child_php_args($pool, [], $holderScript));
    $ready = child_wait_for($holder, '/^READY/m', 30);
    $victim = child_start('c-victim', child_php_args($pool, [], $clientScript, ['fatal']));
    $victimExit = child_finish($victim, 30);
    child_send($holder, 'free');
    $holderExit = child_finish($holder, 30);
    $memory = child_result($holder);
    $fatal = strpos(child_output($victim), 'fatal error in __sleep') !== false;
    check('a fatal error in __sleep() while ucache_set() copies a value in gives back the shared memory the copy had taken',
          $ready && $fatal && $victimExit === 255 && $holderExit === 0 && is_array($memory) && $memory['memory_free'][0] === $memory['memory_free'][1],
          ['exit codes (holder, process with the fatal error)' => [$holderExit, $victimExit], 'memory' => $memory,
           'output' => [child_tail($holder), child_tail($victim)]]);

    /* The same in the requests of a php -S process, which serves the next request as a FastCGI process does. Phase 1 has
       serialized the objects before the one whose __sleep() raises the fatal error, into persistent memory, which request
       shutdown does not free: it must be freed before the error propagates (each request makes 3000 serializations of
       1 KB: leaked, 20 requests would add about 65 MB to the peak working set of the process). The serializer state that
       ucache set aside must be given back too: a fatal error in __sleep() leaves BG(serialize_lock) raised, and in a
       shutdown function every serialize() would then open a context of its own, in which a Serializable whose payload
       refers to the object itself recurses without end */
    $server = server_start('s-server', $poolBase . 's', ['display_errors' => '1', 'html_errors' => '0'], $port);
    if ($server === null) {
        check('a php -S server starts', false);
    } else {
        $peak = function () use ($port) {
            return preg_match('/maxrss (\d+)/', (string)http_get($port, '/?op=maxrss', 10), $match) ? (int)$match[1] : null;
        };
        $fatal = 0;
        $peakBefore = null;
        for ($i = 0; $i < 22; $i++) {
            if ($i === 2) {
                $peakBefore = $peak();      /* after 2 requests to warm up */
            }
            $fatal += (strpos((string)http_get($port, '/?op=sleep_fatal', 30), 'fatal error in __sleep') !== false) ? 1 : 0;
        }
        $peakAfter = $peak();
        check('a php -S process whose requests end with a fatal error in __sleep() while ucache_set() serializes 3000 objects of 1 KB frees the serializations: 20 such requests add less than 16 MB to its peak working set',
              $fatal === 22 && $peakBefore !== null && $peakAfter !== null && $peakAfter - $peakBefore < 16 * 1024,
              ['replies with the fatal error' => $fatal, 'peak working set before and after the 20 requests, KB' => [$peakBefore, $peakAfter],
               'server exit code' => child_exit($server), 'server output' => child_tail($server)]);

        $reply = http_get($port, '/?op=sleep_fatal&shutdown=1', 30);
        check('after such a fatal error, a shutdown function serializes a Serializable whose payload refers to the object itself (ucache gives the request its serializer state back)',
              strpos((string)$reply, 'fatal error in __sleep') !== false
              && strpos((string)$reply, 'SHUTDOWN C:28:"UcacheRouterSelfSerializable":19:{a:1:{s:2:"me";r:1;}}') !== false,
              ['reply' => substr(trim(preg_replace('/\s+/', ' ', (string)$reply)), -300), 'server exit code' => child_exit($server),
               'server output' => child_tail($server)]);
        child_kill($server);
    }

    /* ucache.namesalt: only its first 8 characters are used in the names, but ini_get() shows all of it */
    $pool = $poolBase . 'd';
    $salt = 'ucacheTestSalt12';
    $otherSalt = 'otherTestSalt123';
    $holder = child_start('d-holder', child_php_args($pool, ['ucache.namesalt' => $salt], $holderScript, ['salted', 'shared through the salt']));
    $ready = child_wait_for($holder, '/^READY/m', 30);
    $reader = child_start('d-get', child_php_args($pool, ['ucache.namesalt' => $salt], $clientScript, ['salted']));
    $readerExit = child_finish($reader, 30);
    $other = child_start('d-other', child_php_args($pool, ['ucache.namesalt' => $otherSalt], $clientScript, ['salted']));
    $otherExit = child_finish($other, 30);
    child_send($holder, 'salted');
    $holderExit = child_finish($holder, 30);
    check('ucache.namesalt longer than 8 characters: ini_get() returns all of it, and processes with the same salt share a cache (not with another salt)',
          $ready && preg_match('/^READY ' . preg_quote($salt, '/') . '\s*$/m', child_output($holder)) === 1 && $holderExit === 0
          && $readerExit === 0 && child_result($reader) === ['shared through the salt', $salt]
          && $otherExit === 0 && child_result($other) === [false, $otherSalt],
          ['exit codes (holder, same salt, other salt)' => [$holderExit, $readerExit, $otherExit],
           'results (same salt, other salt)' => [child_result($reader), child_result($other)], 'holder output' => child_tail($holder)]);

    /* A php -S process runs out of memory_limit in a loop of writes that run the scavenger holding the cache
       lock (ucache.gc.threshold): the fatal error must not leave the lock taken */
    $pool = $poolBase . 'e';
    $server = server_start('e-server', $pool, ['ucache.gc.threshold' => '100', 'ucache.gc.step' => '1', 'display_errors' => '1',
                                               'html_errors' => '0'], $port);
    if ($server === null) {
        check('a php -S server starts', false);
    } else {
        $reply = http_get($port, '/?op=inc_loop', 60);
        $serverExit = server_exit($server, $reply);
        $alive = ($serverExit === null) && strpos((string)http_get($port, '/?op=ping', 10), 'pong') !== false;
        $probe = child_start('e-lock', child_php_args($pool, [], $clientScript, ['lock']));
        $probeExit = child_finish($probe, 10);
        $lock = child_result($probe);
        $outOfMemory = strpos((string)$reply, 'Allowed memory size') !== false;
        $endedOnLimit = PHP_VERSION_ID < 80000 && $serverExit === 1 && strpos(child_output($server), 'Allowed memory size') !== false;
        check('a php -S process that runs out of memory_limit in a loop of ucache_inc() calls that run the scavenger (ucache.gc.threshold 100) does not keep the cache lock'
              . ($endedOnLimit ? ' (this PHP 7 build ends the process on memory_limit instead, which gives the lock back too)' : ''),
              (($alive && $outOfMemory) || $endedOnLimit) && $probeExit === 0 && is_array($lock) && $lock[0] === true && $lock[1] < 5,
              ['reply' => substr(trim(preg_replace('/\s+/', ' ', (string)$reply)), -300), 'server exit code' => $serverExit, 'server alive' => $alive,
               'server output' => child_tail($server), 'other process (exit code, [set, seconds])' => [$probeExit, $lock]]);
        child_kill($server);
    }

    /* The scavenger at the end of requests (ucache.gc.interval), one php -S server per case. Each stores an entry that
       expires after 1 second, one without a ttl and one that expires after 300 seconds, then, once the first one has
       expired, lists the entries in each of its requests. ucache_info() removes nothing, so the first list still has the
       expired entry; where the end of a request runs the scavenger, the next list shows what it removed. ucache.gc.step
       covers the whole table, so that one run sees every entry (16381 buckets and three entries take far less than the
       1 ms a step may last). The last list also gives what ucache_info() reports about the scavenger: the entries
       removed in all and by the last step, and the seconds since that step (null if none ran, else at most the bound) */
    $all = '[expiring,lasting,later]';
    $periodic = [
        /* pool => [settings, requests after the expiry, the lists they print,
                    [total removed, last removed, seconds since the last step: null or at most], check] */
        'w1' => [['ucache.gc.interval' => '1'], ['listed', 'listed'], [$all, '[lasting,later]'], [1, 1, 1],
                 'with ucache.gc.interval 1, the end of a request runs the scavenger once 1 second has passed since the last run: it removes the expired entry and keeps the ones without a ttl or whose ttl has not passed, and ucache_info() reports the step'],
        'w2' => [['ucache.gc.interval' => '0'], ['listed', 'listed'], [$all, $all], [0, 0, null],
                 'with ucache.gc.interval 0, the end of a request does not run the scavenger'],
        'w3' => [['ucache.gc.interval' => '3600'], ['listed', 'listed'], [$all, $all], [0, 0, null],
                 'with ucache.gc.interval 3600, the end of a request does not run the scavenger before 3600 seconds have passed since the cache was created'],
        'w4' => [['ucache.gc.interval' => '1', 'ucache.gc.ttl' => '1'], ['listed', 'listed'], [$all, '[]'], [3, 3, 1],
                 'with ucache.gc.ttl 1, the scavenger at the end of a request removes every entry not read or stored for 1 second, whatever its ttl'],
        'w5' => [['ucache.gc.interval' => '1', 'ucache.gc.threshold' => '1'], ['listed', 'listed'], [$all, $all], [0, 0, null],
                 'with ucache.gc.threshold 1 and most of the memory free, neither the end of a request nor a write runs the scavenger, and ucache_info() reports no step'],
        'w6' => [['ucache.gc.interval' => '1'], ['listed&disable=1', 'listed', 'listed'], [$all, $all, '[lasting,later]'], [1, 1, 1],
                 'the end of a request that switched ucache.enabled off does not run the scavenger, and the end of the next request does'],
        'w7' => [['ucache.gc.interval' => '0', 'ucache.gc.threshold' => '100'], ['listed', 'poke', 'poke', 'listed'],
                 [$all, null, null, '[lasting,later,poke]'], [1, 0, 1],
                 'with ucache.gc.threshold 100, every write runs a scavenger step: the first one removes the expired entry, the second one nothing, and ucache_info() reports 1 removed in all, 0 by the last step'],
        'w8' => [['ucache.gc.interval' => '1', 'ucache.gc.ttl' => '3600'], ['listed', 'listed'], [$all, '[lasting,later]'], [1, 1, 1],
                 'with ucache.gc.ttl 3600, the scavenger at the end of a request still removes the entries whose own ttl has passed: it removes the expired entry and keeps the ones without a ttl or whose ttl has not passed, all used less than 3600 seconds ago'],
    ];
    $servers = [];
    foreach ($periodic as $name => $case) {
        $server = server_start("$name-server", $poolBase . $name, $case[0] + ['ucache.gc.step' => '1000000'], $port);
        $servers[$name] = [$server, $port, ($server === null) ? null : http_get($port, '/?op=ttl_set', 10)];
    }
    usleep(2500000);
    foreach ($periodic as $name => $case) {
        list($server, $port, $stored) = $servers[$name];
        if ($server === null) {
            check('a php -S server starts', false);
            continue;
        }
        $lists = [];
        $reports = [];
        foreach ($case[1] as $request) {
            if (preg_match('/listed (\[[a-z,]*\]) scavenged (\d+) (\d+) (\d+|NULL)/', (string)http_get($port, "/?op=$request", 10), $match)) {
                $lists[] = $match[1];
                $reports[] = [(int)$match[2], (int)$match[3], ($match[4] === 'NULL') ? null : (int)$match[4]];
            } else {
                $lists[] = null;
            }
        }
        $last = ($reports === []) ? null : end($reports);
        $expected = $case[3];
        $reported = is_array($last) && $last[0] === $expected[0] && $last[1] === $expected[1]
                    && (($expected[2] === null) ? $last[2] === null : is_int($last[2]) && $last[2] <= $expected[2]);
        if ($name === 'w7') {
            /* Its first list comes 2.5 s after the steps of the writes of ttl_set: a count in milliseconds would be 2500 or more */
            $reported = $reported && is_int($reports[0][2]) && $reports[0][2] >= 2 && $reports[0][2] < 1000;
        }
        check($case[4], strpos((string)$stored, 'stored true') !== false && $lists === $case[2] && $reported,
              ['stored' => substr(trim(preg_replace('/\s+/', ' ', (string)$stored)), -100), 'lists' => $lists,
               '[total removed, last removed, seconds since the last step] of each list' => $reports, 'server exit code' => child_exit($server),
               'server output' => child_tail($server)]);
        child_kill($server);
    }

    /* The scavenger on writes, and a full cache, in CLI processes that run at the same time, each on a cache of its own.
       x: with ucache.gc.threshold 100 every write runs a scavenger step, which with ucache.gc.ttl 2 removes the entries
       not used for 2 seconds; ucache_inc() counts as a use. y, z, z0: a ucache_set() that runs out of shared memory runs
       a full scavenger pass first (with ucache.gc.evict, if that does not leave about that percentage of the cache free,
       it then evicts the entries not used for the longest time until it does), and tries once more; their caches of
       5 MB are filled with values of about 50 KB */
    $incAlive = child_start('x-inc-alive', child_php_args($poolBase . 'x', ['ucache.gc.threshold' => '100', 'ucache.gc.ttl' => '2',
                                                                           'ucache.gc.step' => '1000000'], $clientScript, ['inc_alive']));
    $fillExpired = child_start('y-fill-expired', child_php_args($poolBase . 'y', ['ucache.size' => '5'], $clientScript, ['fill_expired']));
    $fillEvict = child_start('z-fill-evict', child_php_args($poolBase . 'z', ['ucache.size' => '5', 'ucache.gc.evict' => '25'], $clientScript,
                                                            ['fill_evict']));
    $fillKeep = child_start('z0-fill-keep', child_php_args($poolBase . 'z0', ['ucache.size' => '5'], $clientScript, ['fill_evict']));
    $fillMixed = child_start('zm-fill-mixed', child_php_args($poolBase . 'zm', ['ucache.size' => '5', 'ucache.gc.evict' => '25'], $clientScript,
                                                            ['fill_mixed']));
    $contactFree = child_start('cf-contact-free', child_php_args($poolBase . 'cf', [], $clientScript, ['contact_free']));

    $exit = child_finish($incAlive, 60);
    $result = child_result($incAlive);
    check('(in another process) with ucache.gc.ttl 2 and a scavenger step on every write (ucache.gc.threshold 100), entries that only ucache_inc() or only ucache_cas() use, every 0.5 s for 3 s, stay (both count as a use), and an entry not used for 2 seconds is removed',
          $exit === 0 && $result === [[1, 2, 3, 4, 5, 6], [true, true, true, true, true, true], true, true, false],
          ['exit code' => $exit, '[values of ucache_inc(), results of ucache_cas(), counter exists, swapped exists, unused entry exists]' => $result,
           'output' => child_tail($incAlive)]);

    $exit = child_finish($fillExpired, 60);
    $result = child_result($fillExpired);
    check('(in another process) ucache_set() in a full cache: with nothing expired and ucache.gc.evict 0 it fails and removes nothing; once the entries have expired, it runs a full scavenger pass that removes all of them and stores the value (1 MB), and ucache_info() counts the pass as a step, with nothing evicted',
          $exit === 0 && is_array($result) && $result['values stored'] > 0 && $result['the set that ran out failed'] === true
          && $result['entries after that set'] === $result['values stored'] && $result['1 MB set after the expiry'] === true
          && $result['entries listed at the end'] === 1 && $result['1 MB value listed'] === true && $result['total_scavenged_count'] >= 1
          && $result['last_scavenge_count'] === $result['total_scavenged_count'] && is_int($result['last_scavenge_seconds'])
          && $result['last_scavenge_seconds'] <= 1 && $result['total_evicted_count'] === 0,
          ['exit code' => $exit, 'result' => $result, 'output' => child_tail($fillExpired)]);

    $exit = child_finish($fillEvict, 60);
    $result = child_result($fillEvict);
    check('(in another process) with ucache.gc.evict 25, ucache_set() in a full cache of entries without a ttl evicts the entries not used for the longest time, and only until 25% is free: every one of 60 new values is stored and stays, values stored 2.2 s before them and not read since are evicted, but not all of them, the 10 read just before are kept, and ucache_info() counts the evicted ones in total_evicted_count and total_scavenged_count',
          $exit === 0 && is_array($result) && $result['cold failed'] === 0 && $result['cold read'] === 10 && $result['hot stored'] === 60
          && $result['hot present'] === 60 && $result['cold read gone'] === 0 && $result['cold gone'] >= 1
          && $result['cold gone'] < $result['cold stored'] - 10 && $result['total_evicted_count'] === $result['cold gone']
          && $result['total_scavenged_count'] === $result['total_evicted_count'],
          ['exit code' => $exit, 'counts' => $result, 'output' => child_tail($fillEvict)]);

    $exit = child_finish($fillKeep, 60);
    $result = child_result($fillKeep);
    check('(in another process) with ucache.gc.evict 0, ucache_set() in a full cache of entries without a ttl fails, and nothing is evicted',
          $exit === 0 && is_array($result) && $result['cold failed'] === 0 && $result['cold read'] === 10 && $result['hot stored'] < 60
          && $result['cold gone'] === 0 && $result['total_evicted_count'] === 0 && $result['total_scavenged_count'] === 0,
          ['exit code' => $exit, 'counts' => $result, 'output' => child_tail($fillKeep)]);
    check('(in another process) after a full pass that removed nothing and left no room, the processes skip the pass for a second: a set that does not fit 0.6 s later fails without one',
          $exit === 0 && is_array($result) && isset($result['set while the pass is skipped']) && $result['set while the pass is skipped'] === false
          && $result['seconds since the last pass'] === 1,
          ['exit code' => $exit, 'counts' => $result]);

    $exit = child_finish($fillMixed, 60);
    $result = child_result($fillMixed);
    check('(in another process) with ucache.gc.evict 25, a ucache_set() that does not fit runs the full pass, which removes the expired entries, and that leaves 25% free: nothing is evicted, and the value is stored',
          $exit === 0 && is_array($result) && $result['expiring'] > 0 && $result['1 MB set'] === true && $result['cold gone'] === 0
          && $result['total_evicted_count'] === 0 && $result['total_scavenged_count'] === $result['expiring'],
          ['exit code' => $exit, 'counts' => $result, 'output' => child_tail($fillMixed)]);

    $exit = child_finish($contactFree, 60);
    $result = child_result($contactFree);
    check('(in another process) ucache_exists(), ucache_inc() and ucache_cas() of an expired key remove its entry, and its memory is free when the call returns',
          $exit === 0 && is_array($result) && $result['exists'][0] === false && $result['exists'][1] > 4000
          && $result['inc'][0] === false && $result['inc'][1] > 0 && $result['cas'][0] === false && $result['cas'][1] > 0
          && $result['entries left'] === 0,
          ['exit code' => $exit, '[result, memory_free gained] of each call' => $result, 'output' => child_tail($contactFree)]);

    /* Changes made by __sleep() while the value is copied in (see above), in the forms that crash PHP or
       store a broken entry when the copy reads freed memory, so in a process of their own */
    $stale = child_start('f-stale', child_php_args($poolBase . 'f', [], $clientScript, ['stale']));
    $staleExit = child_finish($stale, 30);
    $result = child_result($stale);
    check('(in another process) a __sleep() that frees a string of the value (through a reference) and gives its memory to a new array: the value is stored as it was',
          $staleExit === 0 && $result === [true, str_repeat('a', 24), null],
          ['exit code' => $staleExit, '[set, a, b]' => $result, 'output' => child_tail($stale)]);

    $freed = child_start('g-freed', child_php_args($poolBase . 'g', [], $clientScript, ['freed']));
    $freedExit = child_finish($freed, 30);
    $result = child_result($freed);
    check('(in another process) a __sleep() that frees the array being copied in, and strings take the memory of its arrays: the array is stored as it was',
          $freedExit === 0 && $result === [true, 61, 0], ['exit code' => $freedExit, '[set, elements, wrong elements]' => $result, 'output' => child_tail($freed)]);

    /* $GLOBALS, in a process of its own (the globals of this script are many, and some cannot be stored) */
    $globals = child_start('gb-globals', child_php_args($poolBase . 'gb', [], $clientScript, ['globals']));
    $globalsExit = child_finish($globals, 30);
    $result = child_result($globals);
    check('(in another process) ucache_set() stores $GLOBALS as a value and as an array of values, with the values of its variables and without those that are not set, also while serializing one adds globals',
          $globalsExit === 0 && $result === [true, true, [], true],
          ['exit code' => $globalsExit, '[stored, read back, keys that failed, variable stored]' => $result, 'output' => child_tail($globals)]);

    /* A block freed between two used blocks goes into the free list, which is kept in address order, without walking the
       entries after it: with WinCache's allocator, deleting every second one of 10000 entries took 0.8 seconds, and the
       time grew with the square of the number of entries. The bound is wide, for a loaded machine */
    $speed = child_start('fs-free-speed', child_php_args($poolBase . 'fs', ['ucache.size' => '64'], $clientScript, ['free_speed']));
    $speedExit = child_finish($speed, 120);
    $result = child_result($speed);
    check('(in another process) deleting every second one of 40000 entries takes less than 5 seconds, and the cache gives all its memory back',
          $speedExit === 0 && is_array($result) && $result[0] === 40000 && $result[1] < 5 && $result[2] === true && $result[3] === true,
          ['exit code' => $speedExit, '[stored, seconds, rest read back, memory back]' => $result, 'output' => child_tail($speed)]);

    /* $success holding the only reference to an object whose destructor writes $success: the bool is stored before the
       object is released (in PHP 7.4 to 8.4 the engine's ZEND_TRY_ASSIGN_* macros release first, which freed the object
       twice). In another process, as a double free ends it */
    $dtor = child_start('l-success-dtor', child_php_args($poolBase . 'l', [], $clientScript, ['success_dtor']));
    $dtorExit = child_finish($dtor, 30);
    $result = child_result($dtor);
    $wrote = str_repeat('w', 100);
    check('(in another process) $success holding the only reference to an object whose destructor writes $success: the destructor sees the bool, and the process goes on',
          $dtorExit === 0 && $result === [['get' => false, 'get_miss' => false, 'get_array' => false, 'inc' => false, 'dec' => false],
                                          ['get' => true, 'get_miss' => $wrote, 'get_array' => true, 'inc' => true, 'dec' => true]],
          ['exit code' => $dtorExit, '[what each destructor saw, $success after each call]' => $result, 'output' => child_tail($dtor)]);

    /* A Fiber runs on a C stack of its own, 2 MB by default (fiber.stack_size), which the recursion of the copy overflows
       on a value nested a few thousand levels deep, far less than the 10000 levels that ucache_set() takes (see "Nesting
       depth"). The copy checks the stack that is left and fails cleanly, instead of crashing the process (in phase 2 of
       the copy-in, a crash lost the half-built entry for every process of the pool): against PHP's stack limit on 8.3+,
       against a limit of its own on 8.1 and 8.2, which have Fibers but no stack limit. A value that the stack of the
       Fiber takes is stored and read back as anywhere else. Fibers arrived in PHP 8.1 */
    if (PHP_VERSION_ID >= 80100) {
        $fiber = child_start('w-fiber-fits', child_php_args($poolBase . 'w', ['fiber.stack_size' => '2M'], $clientScript, ['fiber_fits']));
        $fiberExit = child_finish($fiber, 30);
        $result = child_result($fiber);
        check('(in another process) inside a Fiber, ucache_set() and ucache_get() of a value nested 1000 levels deep, which the stack of the Fiber takes, store it and read it back intact (PHP 8.1+)',
              $fiberExit === 0 && $result === [true, true, []],
              ['exit code' => $fiberExit, '[set, read back intact, warnings]' => $result, 'output' => child_tail($fiber)]);

        $fiber = child_start('u-fiber-set', child_php_args($poolBase . 'u', ['fiber.stack_size' => '2M'], $clientScript, ['fiber_set']));
        $fiberExit = child_finish($fiber, 30);
        $result = child_result($fiber);
        check('(in another process) inside a Fiber, ucache_set() of a value nested 10000 levels deep returns false without a warning instead of overflowing the C stack, and stores nothing and takes no shared memory (PHP 8.1+)',
              $fiberExit === 0 && $result === [false, [], false, 0],
              ['exit code' => $fiberExit, '[set, warnings, stored, shared memory taken]' => $result, 'output' => child_tail($fiber)]);

        $fiber = child_start('v-fiber-get', child_php_args($poolBase . 'v', ['fiber.stack_size' => '2M'], $clientScript, ['fiber_get']));
        $fiberExit = child_finish($fiber, 30);
        $result = child_result($fiber);
        check('(in another process) inside a Fiber, ucache_get() of an entry nested 10000 levels deep returns false without a warning instead of overflowing the C stack, and the entry stays intact (PHP 8.1+)',
              $fiberExit === 0 && $result === [true, false, false, [], true],
              ['exit code' => $fiberExit, '[set, get, success, warnings, entry intact]' => $result, 'output' => child_tail($fiber)]);
    }

    /* memory_limit while ucache_set() copies a value in. The CVE-patched PHP 7.2 of production ends the process with
       exit code 1 on memory_limit, which no zend_try can intercept: had phase 2 of the copy-in (which builds the entry
       in shared memory) needed request memory, the half-built entry would be lost for every process of the pool. So
       what needs request memory or runs user code (holding the value, serializing its objects) is done in phase 1,
       before any shared memory is taken, and phase 2's translation table is persistent memory. A holder keeps the
       cache and measures its free memory */
    $pool = $poolBase . 'h';
    $holder = child_start('h-holder', child_php_args($pool, ['ucache.size' => '64'], $holderScript));
    $ready = child_wait_for($holder, '/^READY/m', 30);
    $baseline = $ready ? holder_memory($holder) : null;
    $victim = child_start('h-sleep', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['sleep_oom']));
    $victimExit = child_finish($victim, 60);
    $afterVictim = holder_memory($holder);
    check('memory_limit reached in __sleep() of an object that follows a big list, while ucache_set() copies them in, leaves the shared memory as it was (the objects are serialized before anything is copied)',
          $baseline !== null && strpos(child_output($victim), 'Allowed memory size') !== false && limit_exit_ok($victimExit) && $afterVictim === $baseline,
          ['exit code' => $victimExit, '[memory_free, num_used_blks] before and after' => [$baseline, $afterVictim], 'output' => child_tail($victim)]);

    /* Compared with memory_free before this child, so that what the one before lost does not count here */
    $sharer = child_start('h-strings', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['shared_strings']));
    $sharerExit = child_finish($sharer, 60);
    $stored = child_result($sharer);
    child_send($holder, 'free');
    $holderExit = child_finish($holder, 30);
    $memory = child_result($holder);
    check('ucache_set() of 200000 strings held by two lists each, with memory_limit 4 MB above the usage, stores them (the copy needs no request memory), and deleting them gives back all the shared memory',
          $sharerExit === 0 && $stored === true && $holderExit === 0 && $afterVictim !== null && is_array($memory)
          && $memory['memory_free'][1] === $afterVictim[0],
          ['exit codes (holder, process that stores)' => [$holderExit, $sharerExit], 'set' => $stored,
           'memory_free before and after' => [($afterVictim === null) ? null : $afterVictim[0], is_array($memory) ? $memory['memory_free'][1] : null],
           'output' => child_tail($sharer)]);

    /* The translation table of phase 2 is keyed by the addresses of what it records, such as the strings that a value holds
       more than once. zend_mm gives every block over 3 KB pages of its own, so the addresses of 4 KB strings are multiples
       of 4096: as keys they would fall into a few slots of the table, and every lookup would walk a long chain (storing
       10000 such strings held by two lists took 5 to 6.5 times as long as when one list holds them). The keys mix the
       higher bits of the address into the lower ones */
    $ratio = child_start('t-ratio', child_php_args($poolBase . 't', ['ucache.size' => '64', 'memory_limit' => '256M'], $clientScript, ['shared_ratio']));
    $ratioExit = child_finish($ratio, 60);
    $result = child_result($ratio);
    check('ucache_set() of 10000 distinct 4 KB strings held by two lists takes less than 2.5 times as long as when one list holds them (the best of 5 each): the translation table spreads their page-aligned addresses',
          $ratioExit === 0 && is_array($result) && $result['two lists'] > 0 && $result['one list'] > 0 && $result['two lists'] < 2.5 * $result['one list'],
          ['exit code' => $ratioExit, 'best times in seconds' => $result, 'output' => child_tail($ratio)]);

    /* PHP 8.2.35, 8.3.36 and the other runtimes with delayed OOM (see below) let a ucache_set() pass memory_limit for
       a moment and go on: the call must then store nothing, since PHP raises the fatal error when it returns. Elsewhere
       the fatal error comes at once. A holder keeps the cache and looks for the entry afterwards */
    $pool = $poolBase . 'i';
    $holder = child_start('i-holder', child_php_args($pool, ['ucache.size' => '64'], $holderScript));
    $ready = child_wait_for($holder, '/^READY/m', 30);
    $victim = child_start('i-transient', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['transient']));
    $victimExit = child_finish($victim, 60);
    child_send($holder, 'transient');
    $holderExit = child_finish($holder, 30);
    $stored = child_result($holder);
    check('a ucache_set() that passes memory_limit only for a moment ends with the memory_limit fatal error and stores nothing',
          $ready && strpos(child_output($victim), 'Allowed memory size') !== false && limit_exit_ok($victimExit) && $holderExit === 0
          && $stored === serialize(false),
          ['exit codes (holder, process that stores)' => [$holderExit, $victimExit], 'entry' => is_string($stored) ? brief(@unserialize($stored)) : $stored,
           'output' => child_tail($victim)]);

    /* The same value through the array form of ucache_set(), followed by two small values: the call must stop at the key
       whose store passed memory_limit, since PHP raises the fatal error when it returns (where the fatal error comes at once,
       the process stops there anyway). A holder keeps the cache, and another process looks for the three keys afterwards */
    $pool = $poolBase . 'q';
    $holder = child_start('q-holder', child_php_args($pool, ['ucache.size' => '64'], $holderScript));
    $ready = child_wait_for($holder, '/^READY/m', 30);
    $victim = child_start('q-transient', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['transient_array']));
    $victimExit = child_finish($victim, 60);
    $looker = child_start('q-exists', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['exists', 'transient_a', 'transient_b', 'transient_c']));
    $lookerExit = child_finish($looker, 30);
    child_send($holder, 'transient_a');
    $holderExit = child_finish($holder, 30);
    $present = child_result($looker);
    check('ucache_set() of an array of values whose first one passes memory_limit only for a moment ends with the memory_limit fatal error and stores none of them (the call stops at that key)',
          $ready && strpos(child_output($victim), 'Allowed memory size') !== false && limit_exit_ok($victimExit) && $lookerExit === 0
          && $present === ['transient_a' => false, 'transient_b' => false, 'transient_c' => false] && $holderExit === 0,
          ['exit codes (holder, process that stores, process that looks)' => [$holderExit, $victimExit, $lookerExit], 'keys stored' => $present,
           'output' => child_tail($victim)]);

    /* Phase 1 serializes the objects of a value before phase 2 copies them in, and keeps each serialization in
       persistent memory of its exact size: request memory holds one at a time, as when each object was serialized and
       copied in at once. Kept on the request heap until ucache_set() returned, a 350-byte serialization took 4 KB of
       memory_limit (its smart_str allocation) */
    $objects = child_start('j-objects', child_php_args($poolBase . 'j', ['ucache.size' => '32'], $clientScript, ['objects']));
    $objectsExit = child_finish($objects, 60);
    $result = child_result($objects);
    check('ucache_set() of 20000 objects with 350-byte serializations needs less than 512 bytes of request memory per object, and the objects read back equal',
          $objectsExit === 0 && is_array($result) && $result['set'] === true && abs($result['serialized length'] - 350) <= 20
          && $result['peak growth'] < 20000 * 512 && $result['read back'] === 20000 && $result['different'] === 0,
          ['exit code' => $objectsExit, 'result' => $result, 'output' => child_tail($objects)]);

    /* The serializations kept off the request heap still count against memory_limit (PHP itself does not count
       persistent memory): past it, phase 1 stops, and ucache_set() returns false without a fatal error or a warning.
       It stops as soon as they pass it: checked only at the end, all 200 serializations would be made first, and the
       process would grow by 200 MB (its peak working set) before it gave up */
    $over = child_start('k-over', child_php_args($poolBase . 'k', ['ucache.size' => '64'], $clientScript, ['objects_over_limit']));
    $overExit = child_finish($over, 60);
    $result = child_result($over);
    check('ucache_set() of objects whose serializations pass memory_limit together (200 of 1 MB that share one string, 16 MB above the usage) returns false, without a fatal error or a warning, stores nothing, and adds less than 32 MB to the peak working set',
          $overExit === 0 && is_array($result) && count($result) === 4 && array_slice($result, 0, 3) === [false, false, []]
          && is_int($result[3]) && $result[3] < 32 * 1024 && strpos(child_output($over), 'Allowed memory size') === false,
          ['exit code' => $overExit, '[set, stored, warnings, growth of the peak working set in KB]' => $result, 'output' => child_tail($over)]);

    /* The serializations are checked against memory_limit together with the memory in use, not with the size of the heap,
       which also counts memory that PHP keeps once it is freed (the pages of small blocks, the chunks that a FastCGI
       process carries from one request to the next). After 60 MB of small strings were freed, the heap is about 80 MB big
       with little of it in use, and the 64 MB of serializations of 60000 objects that share one 1 KB string fit under
       memory_limit (128M) next to the memory in use */
    $bigStep = child_start('r-after-big-step', child_php_args($poolBase . 'r', ['ucache.size' => '128', 'memory_limit' => '128M'], $clientScript, ['objects_after_big_step']));
    $bigStepExit = child_finish($bigStep, 60);
    $result = child_result($bigStep);
    check('ucache_set() of 60000 objects that share one 1 KB string (64 MB of serializations) at memory_limit 128M, after 60 MB of strings were freed, stores them: the serializations are counted with the memory in use, not with the free pages that PHP keeps',
          $bigStepExit === 0 && is_array($result) && $result['set'] === true && $result['read back'] === 60000 && $result['last intact'] === true,
          ['exit code' => $bigStepExit, 'result' => $result, 'output' => child_tail($bigStep)]);

    /* PHP 8.2.35, 8.3.36 and the other runtimes with "delayed OOM" do not raise the memory_limit fatal error inside
       internal code: the allocations go on up to a hard limit, twice memory_limit, where the process ends with exit
       code 137, without the fatal error and the shutdown functions. The loops of ucache_info() (the key list, the array
       of entries) and of ucache_get() with an array of keys watch memory_limit themselves and stop, so that PHP raises
       the fatal error when the function returns. Elsewhere memory_limit raises the fatal error at once (or ends the
       process with exit code 1 on the CVE-patched PHP 7.2) */
    $loops = ['list_oom'     => 'ucache_info() copies the key list (8000 keys of 4090 characters)',
              'info_oom'     => 'ucache_info() builds its array of entries (100000 entries)',
              'getarray_oom' => 'ucache_get() with an array of 100000 keys builds the array of values'];
    $n = 0;
    foreach ($loops as $mode => $what) {
        $n++;
        $child = child_start("l-$mode", child_php_args($poolBase . "l$n", ['ucache.size' => '64'], $clientScript, [$mode]));
        $exit = child_finish($child, 60);
        $output = child_output($child);
        $endedOnLimit = PHP_VERSION_ID < 80000 && $exit === 1;
        check("memory_limit reached while $what: the memory_limit fatal error and the shutdown functions (exit code 255), not the hard limit (exit code 137)"
              . ($endedOnLimit ? ' (this PHP 7 build ends the process on memory_limit instead)' : ''),
              strpos($output, 'Allowed memory size') !== false && limit_exit_ok($exit)
              && ($endedOnLimit || preg_match('/^SHUTDOWN .*Allowed memory size/m', $output) === 1),
              ['exit code' => $exit, 'output' => child_tail($child)]);
    }

    /* A php -S process runs out of memory_limit while ucache_info() copies the list of keys, which it does holding the
       cache lock: the fatal error must not leave the lock taken (the 8000 keys of 4000 characters do not fit in 10M) */
    $pool = $poolBase . 'm';
    $server = server_start('m-server', $pool, ['ucache.size' => '64', 'memory_limit' => '10M', 'display_errors' => '1', 'html_errors' => '0'], $port);
    if ($server === null) {
        check('a php -S server starts', false);
    } else {
        $seeded = http_get($port, '/?op=seed', 60);
        $reply = http_get($port, '/?op=info', 60);
        $serverExit = server_exit($server, $reply);
        $alive = ($serverExit === null) && strpos((string)http_get($port, '/?op=ping', 10), 'pong') !== false;
        $probe = child_start('m-lock', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['lock']));
        $probeExit = child_finish($probe, 10);
        $lock = child_result($probe);
        $outOfMemory = strpos((string)$reply, 'Allowed memory size') !== false;
        $endedOnLimit = PHP_VERSION_ID < 80000 && $serverExit === 1 && strpos(child_output($server), 'Allowed memory size') !== false;
        check('a php -S process that runs out of memory_limit inside ucache_info() survives and gives the cache lock back'
              . ($endedOnLimit ? ' (this PHP 7 build ends the process on memory_limit instead, which gives the lock back too)' : ''),
              strpos((string)$seeded, 'seeded 8000') !== false && (($alive && $outOfMemory) || $endedOnLimit)
              && $probeExit === 0 && is_array($lock) && $lock[0] === true && $lock[1] < 5,
              ['seed' => substr(trim(preg_replace('/\s+/', ' ', (string)$seeded)), -40), 'reply' => substr(trim(preg_replace('/\s+/', ' ', (string)$reply)), -300),
               'server exit code' => $serverExit, 'server alive' => $alive, 'server output' => child_tail($server),
               'other process (exit code, [set, seconds])' => [$probeExit, $lock]]);
        child_kill($server);
    }

    /* Deleting an entry of 100000 small arrays frees about 100000 blocks of shared memory, holding the cache lock. The
       chunks of a memory pool are freed from the last to the first, each data block before its header, so that a block
       usually merges with a free neighbour at once: freed in the order of the list, every data block had used blocks on
       both sides, and each free walked all the used blocks after it (15 to 25 s here), while every other process of the
       pool waited for the lock. Another process reads meanwhile */
    $pool = $poolBase . 'n';
    $flag = 'n-deleted.flag';
    $reader = child_start('n-reader', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['stall_reader', $flag]));
    $readerReady = child_wait_for($reader, '/^READY/m', 30);
    $deleter = child_start('n-delete', child_php_args($pool, ['ucache.size' => '64'], $clientScript, ['big_delete', $flag]));
    $deleterStored = $readerReady && child_wait_for($deleter, '/^STORED/m', 60);
    child_send($reader, 'go');
    $reading = child_wait_for($reader, '/^READING/m', 30);
    child_send($deleter, 'go');
    $deleterExit = child_finish($deleter, 10);
    if ($deleterExit === null) {
        child_kill($reader);
    }
    $readerExit = ($deleterExit === null) ? null : child_finish($reader, 30);
    $deleted = child_result($deleter);
    $longest = child_result($reader);
    check('deleting an entry of 100000 small arrays takes less than 3 s, and another process that reads the cache meanwhile does not wait longer',
          $deleterStored && $reading && $deleterExit === 0 && is_array($deleted) && $deleted[0] === true && $deleted[1] === true && $deleted[2] < 3
          && $readerExit === 0 && is_array($longest) && $longest[0] < 3 && $longest[1] === true,
          ['delete done within 10 s' => $deleterExit !== null, 'exit codes (process that deletes, process that reads)' => [$deleterExit, $readerExit],
           '[set, deleted, seconds]' => $deleted, '[longest read in seconds, end seen]' => $longest,
           'output' => [child_tail($deleter), child_tail($reader)]]);

    /* Serializable objects whose payload refers to the object itself (see the checks of Serializable in one process).
       Storing or reading one recurses without end when the serializer context is not right (with BG(serialize_lock)
       raised, for instance, every serialize() opens a context of its own, which does not know the object yet), so
       they run in processes of their own, whose small memory_limit ends such a recursion soon. Inside __wakeup() PHP's
       own unserialize() would open such a context: ucache_get() must open the one that unserialize() at the top level
       would */
    $self = child_start('o-self', child_php_args($poolBase . 'o', ['memory_limit' => '16M'], $clientScript, ['self_serializable']));
    $selfExit = child_finish($self, 30);
    $result = child_result($self);
    check('(in another process) a Serializable whose serialize() returns serialize() of a payload that refers to the object itself is stored and read back intact, without notices',
          $selfExit === 0 && is_array($result) && isset($result['top level']) && $result['top level'] === [true, true, []],
          ['exit code' => $selfExit, 'result ([set, intact, notices] ...)' => $result, 'output' => child_tail($self)]);
    check('(in another process) such a Serializable, read by a ucache_get() inside __wakeup() of an object that unserialize() or ucache_get() reads, is intact, and so is the outer value, without notices',
          $selfExit === 0 && is_array($result) && isset($result['unserialize()'], $result['ucache_get()'])
          && $result['unserialize()'] === [true, true, []] && $result['ucache_get()'] === [true, true, []],
          ['exit code' => $selfExit, 'result ([read intact, outer value intact, notices] ...)' => $result, 'output' => child_tail($self)]);

    $sleep = child_start('p-sleep', child_php_args($poolBase . 'p', ['memory_limit' => '16M'], $clientScript, ['sleep_serializable']));
    $sleepExit = child_finish($sleep, 30);
    $result = child_result($sleep);
    check('(in another process) such a Serializable, stored by a ucache_set() inside __sleep() of an object that ucache_set() or serialize() serializes, reads back intact, and so does the outer value, without notices',
          $sleepExit === 0 && $result === [true, true, true, true, true, true, true, []],
          ['exit code' => $sleepExit, 'ucache_set(): [set, inner set, inner intact, outer intact], serialize(): [inner set, inner intact, outer intact], notices' => $result,
           'output' => child_tail($sleep)]);

    /* ucache.prefix: the functions take its value in place of "ucache_". A prefix that does not make valid function
       names, or that makes a name another extension uses (date_add() of ext/date, always there), leaves them all out
       with a startup warning, and PHP still runs */
    $prefixScript = $childDir . DIRECTORY_SEPARATOR . 'prefix.php';
    file_put_contents($prefixScript, <<<'CHILDSCRIPT'
<?php
$funcs = get_extension_funcs('ucache');   /* false without functions */
$funcs = is_array($funcs) ? $funcs : [];
sort($funcs);
$result = ['funcs' => $funcs, 'ucache_get exists' => function_exists('ucache_get')];
if (function_exists('my_cache_set')) {
    $result['set'] = my_cache_set('k', [1, 'two']);
    $result['get'] = My_Cache_Get('k');
    $result['extension'] = (new ReflectionFunction('my_cache_info'))->getExtensionName();
}
echo "RESULT ", base64_encode(serialize($result)), "\n";
CHILDSCRIPT
    );

    $expectedPrefixed = array_map(function ($name) { return 'my_cache_' . substr($name, strlen('ucache_')); }, $expectedFuncs);
    $prefixed = child_start('q-prefix', child_php_args($poolBase . 'q', ['ucache.prefix' => 'my_cache_'], $prefixScript));
    $prefixedExit = child_finish($prefixed, 30);
    $result = child_result($prefixed);
    check('(in another process) with ucache.prefix=my_cache_ the functions are my_cache_*() (called in any case), ucache_*() are gone, and they belong to the extension',
          $prefixedExit === 0 && is_array($result) && array_values(array_diff($result['funcs'], $expectedPrefixed)) === []
          && array_values(array_diff($expectedPrefixed, $result['funcs'])) === [] && $result['ucache_get exists'] === false
          && $result['set'] === true && $result['get'] === [1, 'two'] && $result['extension'] === 'ucache',
          ['exit code' => $prefixedExit, 'result' => $result, 'output' => child_tail($prefixed)]);

    foreach (['invalid' => '1cache', 'duplicate' => 'date_'] as $kind => $prefix) {
        $bad = child_start("q-prefix-$kind", child_php_args($poolBase . 'q', ['ucache.prefix' => $prefix], $prefixScript));
        $badExit = child_finish($bad, 30);
        $result = child_result($bad);
        $warned = ($kind === 'invalid') ? strpos(child_output($bad), 'is not a valid function name prefix') !== false
                                        : stripos(child_output($bad), 'duplicate name') !== false;
        check("(in another process) with ucache.prefix=$prefix ($kind) PHP starts with a warning and none of the functions",
              $badExit === 0 && $warned && is_array($result) && $result['funcs'] === [] && $result['ucache_get exists'] === false,
              ['exit code' => $badExit, 'result' => $result, 'output' => child_tail($bad)]);
    }

    children_cleanup();
}

/*
 * ucache_clear
 */

check('ucache_clear() returns true', ucache_clear() === true);
$info = ucache_info();
$success = 'unset';
$got = ucache_get('smoke_long', $success);
check('ucache_clear() removes every entry',
      $info['total_item_count'] === 0 && $info['ucache_entries'] === [] && $got === false && $success === false, $info);

fclose($res);

check('no unexpected warnings or notices', $errors === [], $errors);

echo "\n=== " . (($failures === 0) ? 'all checks passed' : "$failures check(s) FAILED") . " ===\n";
exit(($failures === 0) ? 0 : 1);
