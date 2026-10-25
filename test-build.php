<?php
/*
 * ucache smoke test.
 *
 * build.bat runs it against the freshly built DLL:
 *   php -n -d extension=php_ucache.dll -d ucache.enablecli=1 test-build.php
 *
 * Prints one line per check ("OK: ..." or "FAIL: ...") and exits with 1 if any check failed.
 * The expected results follow the semantics of the original user cache implementation (the
 * functions were only renamed). The test clears the user cache it runs against.
 */

error_reporting(E_ALL);

$failures = 0;
$errors   = [];

/* Collect warnings and notices instead of printing them; checks that expect one look at them */
set_error_handler(function ($errno, $errstr) use (&$errors) {
    $errors[] = $errstr;
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
                  'ucache_gc', 'ucache_get', 'ucache_inc', 'ucache_info', 'ucache_meminfo', 'ucache_set'];
$testOnlyFuncs = ['ucache_lasterror', 'ucache_runtests'];   /* only in UCACHE_TEST (debug) builds */

$funcs = get_extension_funcs('ucache');
$funcs = is_array($funcs) ? $funcs : [];
$missing = array_values(array_diff($expectedFuncs, $funcs));
$extra   = array_values(array_diff($funcs, $expectedFuncs, $testOnlyFuncs));
check('all 12 ucache_* functions are exported', $missing === [], $missing);
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
    'ucache.localheap'    => ['0',  INI_SYSTEM],
    'ucache.apppoolid'    => ['',   INI_SYSTEM],
    'ucache.filemapdir'   => ['',   INI_SYSTEM],
    'ucache.debuglevel'   => ['0',  INI_ALL],
    'ucache.gc.threshold' => ['0',  INI_SYSTEM],
    'ucache.gc.interval'  => ['10', INI_SYSTEM],
    'ucache.gc.step'      => ['16', INI_SYSTEM],
    'ucache.gc.ttl'       => ['0',  INI_SYSTEM],
];

$ini = ini_get_all('ucache');
$ini = is_array($ini) ? $ini : [];
$names = array_keys($ini);
$expectedNames = array_keys($expectedIni);
sort($names);
sort($expectedNames);
check('INI settings are exactly the 12 ucache.* settings', $names === $expectedNames, $names);

$badAccess = [];
foreach ($expectedIni as $name => $spec) {
    if (isset($ini[$name]) && $ini[$name]['access'] !== $spec[1]) {
        $badAccess[$name] = $ini[$name]['access'];
    }
}
check('INI settings have the expected modifiable levels', $badAccess === [], $badAccess);

/* Defaults can only be checked when no php.ini is loaded (build.bat runs php -n) */
if (php_ini_loaded_file() === false) {
    $badDefault = [];
    foreach ($expectedIni as $name => $spec) {
        /* enablecli is set on the command line for this test */
        if ($name !== 'ucache.enablecli' && isset($ini[$name]) && (string)$ini[$name]['global_value'] !== $spec[0]) {
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

check('set with a string key and no value returns false', ucache_set('smoke_novalue') === false);

$r = capture(function () { return ucache_set('', 1); }, $raised);
check('set with a blank key returns false with a warning', $r === false && warned($raised, 'key cannot be blank string'), $raised);

check('set with a key longer than 4096 characters returns false', ucache_set(str_repeat('k', 4097), 1) === false);

$res = fopen('php://memory', 'r');
$r = capture(function () use ($res) { return ucache_set('smoke_resource', $res); }, $raised);
check('set of a resource returns false with a warning', $r === false && warned($raised, 'value cannot be a resource'), $raised);

$r = capture(function () { return ucache_set('smoke_negttl', 1, -1); }, $raised);
check('set with a negative ttl returns false with a warning',
      $r === false && warned($raised, 'ttl cannot be less than 0') && ucache_exists('smoke_negttl') === false, $raised);

/*
 * TTL (the expiry itself is checked further down, after a pause)
 */

$set = ucache_set('smoke_ttl', 'short-lived', 1);
$entry = find_entry(ucache_info(false, 'smoke_ttl'), 'smoke_ttl');
check('set with ttl 1 stores the entry with ttl_seconds 1', $set === true && $entry !== null && $entry['ttl_seconds'] === 1, $entry);
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
 * ucache_info
 */

$info = ucache_info();
check('ucache_info() returns the expected keys', is_array($info) && array_keys($info) ===
      ['total_cache_uptime', 'is_local_cache', 'total_item_count', 'total_hit_count', 'total_miss_count', 'ucache_entries'],
      is_array($info) ? array_keys($info) : $info);
check('the user cache is not a local cache', $info['is_local_cache'] === false, $info['is_local_cache']);

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
ini_set('ucache.enabled', ($old === false) ? '1' : $old);
check('with ucache.enabled=0 the cache functions return false',
      $old === '1' && $r === [false, false, false] && ucache_exists('smoke_off') === false && ucache_get('smoke_long') === 43,
      ['old' => $old, 'results' => $r]);

/*
 * TTL expiry and ucache_gc
 */

$wait = 2.5 - (microtime(true) - $ttlSetAt);
if ($wait > 0) {
    usleep((int)($wait * 1000000));
}

$success = 'unset';
$r = ucache_get('smoke_ttl', $success);
check('an entry cannot be read once its ttl expired', $r === false && $success === false && ucache_exists('smoke_ttl') === false,
      ['get' => $r, 'success' => $success]);

$listedBefore = find_entry(ucache_info(), 'smoke_ttl') !== null;
/* gc(interval, step, ttl): the cache was created when this process started, which counts as the last
   scavenger run, so with interval 3600 the scavenger does not run and the expired entry stays */
$skipped = ucache_gc(3600, 1000000, 0);
$listedSkipped = find_entry(ucache_info(), 'smoke_ttl') !== null;
check('ucache_gc() does nothing if the cache was created less than interval ago',
      $skipped === 0 && $listedBefore && $listedSkipped,
      ['removed' => $skipped, 'listed before' => $listedBefore, 'listed after' => $listedSkipped]);
/* interval 0 = run now; a large step scans the whole table; two calls
   cover the table even if a previous scavenge stopped part way */
$removed  = ucache_gc(0, 1000000, 0);
$removed2 = ucache_gc(0, 1000000, 0);
$listedAfter = find_entry(ucache_info(), 'smoke_ttl') !== null;
check('ucache_gc() removes the expired entry and returns the number removed',
      is_int($removed) && is_int($removed2) && $removed + $removed2 === 1 && $listedBefore && !$listedAfter,
      ['removed' => [$removed, $removed2], 'listed before' => $listedBefore, 'listed after' => $listedAfter]);
check('ucache_gc() keeps entries that did not expire',
      ucache_exists('smoke_long') === true && ucache_exists('smoke_mttl') === true && ucache_exists('smoke_addttl') === true);
/* ttl 1 would remove every entry not read or stored in the last second, but the scavenger ran just now */
$itemsBefore = ucache_info(true)['total_item_count'];
$skipped = ucache_gc(3600, 1000000, 1);
$itemsAfter = ucache_info(true)['total_item_count'];
check('ucache_gc() does nothing if the last run is more recent than interval',
      $skipped === 0 && $itemsAfter === $itemsBefore && $itemsBefore > 0,
      ['removed' => $skipped, 'items' => [$itemsBefore, $itemsAfter]]);

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
