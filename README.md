<p align="center">
    <img style="height:80px" src="ucache-logo.svg">
</p>

<p align="center">
  <img src="https://img.shields.io/github/issues/pkoutsogiannis/ucache" alt="Issues">
  <img src="https://img.shields.io/github/languages/top/pkoutsogiannis/ucache" alt="Language">
  <img src="https://img.shields.io/github/stars/pkoutsogiannis/ucache" alt="Stars">
  <img src="https://img.shields.io/github/forks/pkoutsogiannis/ucache" alt="Forks">
</p>

UCache is the user cache of WinCache (Microsoft's Windows Cache Extension for PHP, 2.0.0.8) as a PHP extension of its
own. It keeps PHP values in shared memory, so that all the PHP processes of an IIS application pool use the same cache:
a value that one request stores can be read by the later requests of every process of the pool, until it expires or is
deleted.

- Platform: Windows x64, PHP 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4 and 8.5 NTS.
- Extension name `ucache`, file `php_ucache.dll`, version 1.0.1.
- Maintained by [Datasteam Technologies](https://datasteam.com).
- License: see [LICENSE](LICENSE) (the original Microsoft license, with the copyright of Datasteam Technologies).
- What each version changed: [CHANGELOG.md](CHANGELOG.md). How fast it reads and writes: [BENCHMARKS.md](BENCHMARKS.md).

## Table of Contents

- [Introduction](#introduction)
- [Installing/Configuring](#installingconfiguring)
  - [Requirements](#requirements)
  - [Installation](#installation)
  - [Runtime Configuration](#runtime-configuration)
  - [Resource Types](#resource-types)
  - [Predefined Constants](#predefined-constants)
  - [UCache Statistics Script](#ucache-statistics-script)
- [How the Cache Works](#how-the-cache-works)
- [UCache Functions](#ucache-functions)
  - [ucache_add](#ucache_add) - Adds a variable in the user cache only if it does not already exist in the cache
  - [ucache_cas](#ucache_cas) - Compares the variable with an old value and assigns a new value to it
  - [ucache_clear](#ucache_clear) - Deletes the entire content of the user cache
  - [ucache_dec](#ucache_dec) - Decrements the value associated with the key
  - [ucache_delete](#ucache_delete) - Deletes variables from the user cache
  - [ucache_exists](#ucache_exists) - Checks if a variable exists in the user cache
  - [ucache_get](#ucache_get) - Gets a variable stored in the user cache
  - [ucache_inc](#ucache_inc) - Increments the value associated with the key
  - [ucache_info](#ucache_info) - Retrieves information about data stored in the user cache
  - [ucache_meminfo](#ucache_meminfo) - Retrieves information about user cache memory usage
  - [ucache_set](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in the
    cache
- [Building for Windows](#building-for-windows)
- [Migrating from WinCache](#migrating-from-wincache)

## Introduction

UCache started as the user cache of WinCache 2.0.0.8, extracted and renamed, without the file cache, resolve-path cache,
file change notification, session handler, `wincache_lock()`/`wincache_unlock()`, PHP function rerouting and ETW
tracing. Its functions take the same arguments and return the same values as the `wincache_ucache_*` functions of
WinCache 2.0.0.8, under the names `ucache_*`, or under their own names with `ucache.prefix=wincache_ucache_`, apart from
the few differences listed in [Migrating from WinCache](#migrating-from-wincache).

## Installing/Configuring

### Requirements

The extension is supported on the following configurations:

Windows:

- Windows x64. On a web server, PHP runs through FastCGI (php-cgi.exe), as IIS runs it: the processes of one IIS
  application pool share a cache. The CLI and the built-in web server (`php -S`) work too.

PHP:

- PHP 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4 or 8.5, non-thread-safe (NTS) x64 build. There is no thread-safe (ZTS)
  build.

> **Note:** Each PHP version needs the DLL built for it. A DLL loads in any NTS x64 PHP of the version it was built for
> that uses the same compiler (the builds of windows.php.net), whatever the patch release.

### Installation

To install and enable the extension, follow these steps:

1. Download the zip of your PHP version from the [releases page](https://github.com/pkoutsogiannis/ucache/releases), for
   example `php_ucache-v1.0.1-8.4-nts-vs17-x86_64.zip` for PHP 8.4, unzip it, and rename the DLL in it to
   `php_ucache.dll`. Or build it (see [Building for Windows](#building-for-windows)).
2. Copy php_ucache.dll into the PHP extensions folder: the `ext` folder next to php.exe, or the folder that
   `extension_dir` sets in php.ini. For example: `C:\php84\ext`.
3. Add the following line to php.ini: `extension=php_ucache.dll`. Add the `ucache.*` settings to change, if any (see
   [Runtime Configuration](#runtime-configuration)).
4. Recycle the IIS application pools for PHP to pick up the configuration changes, or restart the web server.
5. To check that the extension has been enabled, look for a section called `ucache` in the output of `phpinfo()`, which
   lists the `ucache.*` settings. From the command line, `php -m` lists `ucache`.

> **Note:** Under the CLI, the cache is only created when `ucache.enablecli=1`, in php.ini or on the command line:
> `php -d ucache.enablecli=1 script.php`.

> **Note:** To replace php_ucache.dll by another version or another build, stop the application pool or the web server,
> wait for its php-cgi processes to exit, replace the DLL, and then start it. An overlapped recycle, the IIS default,
> starts the new processes while the old ones are still running, and old and new processes would share one cache,
> although two DLLs can store values differently (an old process can crash on arrays that a new one stored).

### Runtime Configuration

The behaviour of these functions is affected by settings in php.ini.

The following table lists and explains the configuration settings provided by the UCache extension:

| Name | Default | Minimum | Maximum | Changeable | Changelog |
|---|---|---|---|---|---|
| `ucache.enabled` | "1" | "0" | "1" | PHP_INI_ALL | Available as of UCache 1.0.0 |
| `ucache.enablecli` | "0" | "0" | "1" | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.size` | "8" | "5" | "930" | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.namesalt` | NULL | | | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.apppoolid` | NULL | | | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.filemapdir` | NULL | | | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.debuglevel` | "0" | "0" | "501" | PHP_INI_ALL | Available as of UCache 1.0.0 |
| `ucache.gc.threshold` | "0" | "0" | "100" | PHP_INI_SYSTEM | Available as of UCache 1.0.1 |
| `ucache.gc.interval` | "10" | "0" | | PHP_INI_SYSTEM | Available as of UCache 1.0.1 |
| `ucache.gc.step` | "0" | "0" | | PHP_INI_SYSTEM | Available as of UCache 1.0.1 |
| `ucache.gc.ttl` | "0" | "0" | | PHP_INI_SYSTEM | Available as of UCache 1.0.1 |
| `ucache.gc.evict` | "0" | "0" | "100" | PHP_INI_SYSTEM | Available as of UCache 1.0.1 |
| `ucache.prefix` | "ucache_" | | | PHP_INI_SYSTEM | Available as of UCache 1.0.1 |

For further details and definitions of the PHP_INI_* modes, see
[Where a configuration setting may be set](https://www.php.net/manual/en/configuration.changes.modes.php). The
PHP_INI_SYSTEM settings come from php.ini, or from `-d` on the command line of PHP, when its process starts: only
`ucache.enabled` and `ucache.debuglevel` can be changed by a script.

Here's a short explanation of the configuration directives.

`ucache.enabled` *bool*

Enables or disables the user cache functionality. With 0, all functions except `ucache_info()` and `ucache_meminfo()`
return false, and the end of the request does not run the scavenger. The cache is still created at startup, so a script
can switch it back on with `ini_set()`.

`ucache.enablecli` *bool*

Enables or disables the user cache when PHP runs from the command line (CLI). Without it, the cache is not created under
the CLI, and the functions fail there.

`ucache.size` *int*

Defines the size of the shared memory segment of the user cache, in megabytes. Values outside 5..930 are clamped to the
nearest limit. Write it as a plain number: `512M` means 512 x 1048576 megabytes, which is clamped to 930.

`ucache.namesalt` *string*

Defines a string added to the names of the shared memory and synchronization objects, to keep apart caches that would
otherwise be shared. Only the first 8 characters are used.

`ucache.apppoolid` *string*

Defines the name that decides which processes share a cache, instead of the parent process id. If not set, the
`APP_POOL_ID` environment variable is used when IIS sets it.

`ucache.filemapdir` *string*

Defines the folder of a file that backs the shared memory instead of the paging file. Only used when the process group
id (the parent process id, or the hash of the application pool id) is above 99999.

`ucache.debuglevel` *int*

Enables a debug trace written with OutputDebugString: 0 (off), 101, 201, 301, 401 or 501 (the most detailed). Other
values are ignored.

`ucache.gc.threshold` *int*

Defines a free memory percentage, 0 to 100. When > 0, writes also run scavenger steps while free memory is at or below
it, and the steps at the end of requests run only then. 0: writes do not run steps. See
[Expiry and the scavenger](#expiry-and-the-scavenger).

`ucache.gc.interval` *int*

Defines the seconds between the scavenger steps at the end of requests. 0 turns them off. See
[Expiry and the scavenger](#expiry-and-the-scavenger).

`ucache.gc.step` *int*

Defines the hash buckets that a scavenger step goes through. 0: automatic, at least 16, so that the steps at the end of
requests go through the whole table in about 5 minutes. See [Expiry and the scavenger](#expiry-and-the-scavenger).

`ucache.gc.ttl` *int*

Defines, in seconds, how long an entry can go unused. When > 0, the scavenger also removes the entries not used for this
long, whatever their ttl. 0: the scavenger removes expired entries only. See
[Expiry and the scavenger](#expiry-and-the-scavenger).

`ucache.gc.evict` *int*

Defines a free memory percentage, 0 to 100. When > 0 and `ucache_set()` or `ucache_add()` finds the cache full, the
entries not used for the longest time are evicted until this percentage of the cache is free. 0: no eviction. See
[A full cache](#a-full-cache).

`ucache.prefix` *string*

Defines the prefix of the function names, in place of `ucache_`, for code that already has functions of these names:
with `ucache.prefix=my_cache_`, the functions are `my_cache_get()`, `my_cache_set()` and so on, and `ucache_get()` and
the others do not exist; with `ucache.prefix=wincache_ucache_`, they take the names of WinCache's (see
[Migrating from WinCache](#migrating-from-wincache)). The prefix may contain letters, digits, underscores and bytes 0x80
to 0xff, at most 64 of them, and must not start with a digit. When the prefix is not valid, or makes a name that another
extension already uses, PHP starts with a warning and without any of the functions. This manual names the functions with
the default prefix. ucache.php follows the setting.

#### Settings per IIS application pool

Each IIS application pool runs php-cgi processes of its own, and they share a cache of their own. To give a pool its own
`ucache.*` settings, give its sites a FastCGI application of their own. IIS tells FastCGI applications apart by the
program and its arguments, and php-cgi takes `-d` arguments. In `applicationHost.config`:

```xml
<fastCgi>
  <application fullPath="C:\php84\php-cgi.exe"
               arguments="-d ucache.gc.interval=10 -d ucache.gc.step=1000 -d ucache.gc.ttl=84000" />
  <application fullPath="C:\php84\php-cgi.exe" arguments="-d ucache.gc.evict=10" />
</fastCgi>
```

The PHP handler of each site then names its application, as `fullPath|arguments`, exactly:

```xml
<handlers>
  <remove name="PHP_via_FastCGI" />
  <add name="PHP_via_FastCGI" path="*.php" verb="*" modules="FastCgiModule" resourceType="Either"
       scriptProcessor="C:\php84\php-cgi.exe|-d ucache.gc.interval=10 -d ucache.gc.step=1000 -d ucache.gc.ttl=84000" />
</handlers>
```

The processes that share a cache must use the same `ucache.*` settings. php-cgi reads them when it starts: after
changing them, recycle the pool. After changing `ucache.size`, stop the pool, wait for its php-cgi processes to exit and
start it again: the new processes of an overlapped recycle would open the cache that the old ones created.

### Resource Types

This extension has no resource types defined.

### Predefined Constants

This extension has no constants defined.

### UCache Statistics Script

The sources of UCache include a PHP script, ucache.php (WinCache's wincache.php for UCache), that can be used to obtain
cache information and statistics: the settings, the memory use, the hit and miss counts, what the scavenger removed, the
list of entries (250 per page) and the value stored under a key; with authentication on, it can also clear the whole
cache.

To use ucache.php, copy it into a folder of a web site that runs with the extension loaded, preferably one that only
administrators can reach, and replace the values of the `USERNAME` and `PASSWORD` constants at its top: both default to
`ucache`, and the page refuses to work while the password is `ucache`. If an IIS authentication other than Anonymous is
enabled on the server, list the users allowed in `$user_allowed` instead, as the comments of the file explain.

**Example #1 Authentication configuration for ucache.php**

```php
<?php
define('USE_AUTHENTICATION', 1);
define('USERNAME', 'ucache');
define('PASSWORD', 'ucache');
$user_allowed = array('DOMAIN\user1', 'DOMAIN\user2', 'DOMAIN\user3');
?>
```

With `USE_AUTHENTICATION` set to 0, there is no authentication, and the page no longer offers to clear the cache; it
still shows values, so anyone who can reach it can read everything in the cache.

> **Note:** Always protect the ucache.php script by using either the built-in authentication or the server's
> authentication mechanism. Leaving this script unprotected may compromise the security of your web application and web
> server.

## How the Cache Works

### Sharing

Processes share a cache when they have the same `ucache.apppoolid` (by default the `APP_POOL_ID` that IIS sets) or,
without one, the same parent process, and the same `ucache.namesalt`. Processes of different PHP versions never share a
cache. The cache is not persistent: it starts empty when the first process creates it, and lasts while a process has it
open.

### Values

Values are copied into shared memory on write and copied back on read. Scalars, strings, null and arrays (including
nested arrays and references between their elements) are stored as they are; objects are serialized on write and
unserialized on read, as `serialize()` and `unserialize()` do; resources cannot be stored. The cache lock is not held
while objects are serialized or unserialized, so `__sleep()`, `__wakeup()` and the like can call the UCache functions.

A value that cannot be serialized (a Closure, an object of an anonymous class, a Generator) is not stored: the exception
propagates out of `ucache_set()` or `ucache_add()`. A value nested more than 10,000 levels deep is not stored either:
the call returns false. An object that cannot be unserialized when it is read (an object of a class that changed, an
enum that the reading process does not define) makes `ucache_get()` return false with PHP's warning; an object of a
class that is not defined comes back as `__PHP_Incomplete_Class`, as from `unserialize()`.

### Keys

Keys are strings; other scalar keys are converted to strings. Keys are case sensitive. A key ends at its first NUL byte
(`"a\0b"` and `"a\0c"` are both the key `"a"`), and `ucache_set()` and `ucache_add()` store keys of at most 4096
characters.

### Expiry and the scavenger

An entry stored with a `ttl` of more than 0 can be read for up to `ttl` + 1 seconds after it was stored, and reads do
not extend it. An entry stored with a `ttl` of 0, the default, stays until it is deleted, replaced or cleared, or
removed as below.

An expired entry keeps its memory until it is removed, by a call that looks up a key of the same hash bucket, or by the
scavenger. The scavenger goes through the hash table in steps:

- at the end of requests, one step every `ucache.gc.interval` seconds (10 by default; 0 turns these steps off);
- on writes, while free memory is at or below `ucache.gc.threshold` percent of the cache (0, the default, turns these
  steps off; when it is set, the steps at the end of requests also run only then).

A step goes through `ucache.gc.step` hash buckets (0, the default: enough to go through the whole table in about 5
minutes, at least 16), and holds the cache lock for about 1 ms at most. It removes the expired entries and, when
`ucache.gc.ttl` is more than 0, the entries that have not been used for that many seconds, whatever their ttl. An entry
is used when `ucache_get()` reads it, when `ucache_set()` or `ucache_add()` stores it, and when `ucache_inc()`,
`ucache_dec()` or `ucache_cas()` finds it.

`ucache_info()` reports what the scavenger did: `last_scavenge_seconds`, `last_scavenge_count`, `total_scavenged_count`
and `total_evicted_count`.

### A full cache

When `ucache_set()` or `ucache_add()` runs out of memory, it runs a full pass over the table, which removes what a step
removes. With `ucache.gc.evict` more than 0, it then evicts the entries that have not been used for the longest time,
until that percentage of the cache is free, and tries again. With 0, the default, nothing is evicted, and the call fails
if the value still does not fit.

`ucache_clear()` empties the cache for every process, and holds the cache lock until it has freed every entry.

### The cache lock

Every call takes the cache lock while it looks its key up, and a read also while it copies the value out (a write copies
its value in before it takes the lock), so processes that call the cache back to back take turns. Requests that do other
work between their calls rarely find the lock taken. A fatal error in a cache function does not leave the lock held.

### memory_limit

`ucache_set()` and `ucache_add()` serialize the objects of a value before they copy it, and count the serializations
against memory_limit: close to the limit, a value with many objects is not stored, and the call returns false. The full
listing of `ucache_info()` copies every entry into request memory: list a large cache a part at a time, with `offset`
and `limit`.

### Tuning

- If writes fail because the cache is full, give it a larger `ucache.size`, or set `ucache.gc.evict` (for example 10)
  rather than calling `ucache_clear()` when a write fails: eviction removes the entries not used for the longest time,
  `ucache_clear()` everything, for every process of the pool. A `total_evicted_count` that keeps growing means that the
  cache is too small for what is stored in it.
- With the defaults, an entry without a ttl stays until it is deleted, replaced or cleared. If keys go out of use (one
  per user, per page or per day, for example), set `ucache.gc.ttl` above the longest time between two uses of an entry
  that should stay, for example 86400 (a day).
- In a cache where no entry has a ttl and `ucache.gc.ttl` is 0, the scavenger has nothing to remove:
  `ucache.gc.interval=0` turns its steps off.
- `ucache.gc.step` sets how fast the steps go through the table: in about buckets / `ucache.gc.step` x
  `ucache.gc.interval` seconds at the end of requests. A fixed value does not grow with the cache: 16 buckets every 10
  seconds take about 4.5 hours to go through the 25717 buckets of a 930 MB cache.
- `ucache.gc.threshold` makes the scavenger work harder once the cache is that full. It only helps where entries expire
  or go unused (`ucache.gc.ttl`).

## UCache Functions

On PHP 8, named arguments can skip the optional parameters: `ucache_set($values, ttl: 60)`,
`ucache_inc($key, success: $ok)`, `ucache_info(limit: 250)`.

Arguments are checked before anything else, whatever the state of the cache. Invalid ones (a wrong type, a missing
argument, an argument to `ucache_clear()` or `ucache_meminfo()`, which take none) throw `ArgumentCountError` or
`TypeError`. On PHP 7 they do so only when the calling file declares `strict_types=1`: otherwise they raise a warning
and the function returns false (`ucache_meminfo()`: null). With `ucache.enabled=0`, every function except
`ucache_info()` and `ucache_meminfo()` then returns false.

### ucache_add

(UCache >= 1.0.0)

ucache_add - Adds a variable in the user cache only if it does not already exist in the cache

#### Description

```php
ucache_add(mixed $key, mixed $value = null, int $ttl = 0): bool|array
ucache_add(array $values, mixed $unused = null, int $ttl = 0): bool|array
```

Adds a variable in the user cache, only if this variable does not already exist in the cache. The added variable remains
in the user cache unless its time to live expires or it is deleted by using the `ucache_delete()` or `ucache_clear()`
functions.

#### Parameters

`key`

Store the variable using this key name. If a variable with the same key is already present, the function fails and
returns false. `key` is case sensitive (see [Keys](#keys)). To overwrite the value even if `key` is present, use the
`ucache_set()` function instead.

`value`

Value of a variable to store. Value supports all data types except resources, such as file handles. Objects are
serialized (see [Values](#values)).

`values`

Associative array of keys and values, which adds all the pairs in one call.

`unused`

Not used in the array form.

`ttl`

Time for the variable to live in the cache in seconds. After the value specified in `ttl` has passed, the stored
variable can no longer be read, and the scavenger frees it (see [Expiry and the scavenger](#expiry-and-the-scavenger)).
The default value of 0 means that the variable never expires. A negative `ttl` makes the call fail, and a variable with
a `ttl` above 4294967 (about 49.7 days) never expires.

#### Return Values

If `key` is a string, the function returns true on success and false on failure: the key exists, the key is blank or
longer than 4096 characters, no value was given, the value is or holds a resource, is nested too deeply or does not fit
in the cache, its serialized objects would pass memory_limit, or `ttl` is negative.

If `key` is an array, the function returns an empty array if all the name => value pairs were added, or else an array of
the names that could not be added, the existing ones included, each with the value -1.

#### Errors/Exceptions

Raises an `E_WARNING` when `key` is a string that already exists ("function called with a key which already exists"; in
the array form, existing keys are listed among the failures without a warning), for a blank key ("key cannot be blank
string"), a negative ttl ("ttl cannot be less than 0") and a resource given as the value ("value cannot be a resource").
A key longer than 4096 characters, a resource inside an array value, or a value nested too deeply, fails without a
warning.

If serializing an object of the value throws (a Closure, an object of an anonymous class, a Generator, a `__sleep()` or
`__serialize()` that throws), the exception propagates and the value is not stored. In the array form, the pairs before
that element are stored, that one and the ones after it are not.

#### Examples

**Example #1 `ucache_add()` with `key` as a string**

```php
<?php
var_dump(ucache_add('green', 1));
var_dump(ucache_add('green', 2));
var_dump(ucache_get('green'));
?>
```

The above example will output:

```
bool(true)

Warning: ucache_add(): function called with a key which already exists in C:\inetpub\wwwroot\example.php on line 3
bool(false)
int(1)
```

**Example #2 `ucache_add()` with `key` as an array**

```php
<?php
$array = array('green' => 1, 'blue' => 2);
var_dump(ucache_add($array));
var_dump(ucache_add(array('green' => 3, 'red' => 4)));
var_dump(ucache_get(array('green', 'blue', 'red')));
?>
```

The above example will output:

```
array(0) {
}
array(1) {
  ["green"]=>
  int(-1)
}
array(3) {
  ["green"]=>
  int(1)
  ["blue"]=>
  int(2)
  ["red"]=>
  int(4)
}
```

#### See Also

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in the
  cache
- [ucache_get()](#ucache_get) - Gets a variable stored in the user cache
- [ucache_delete()](#ucache_delete) - Deletes variables from the user cache
- [ucache_clear()](#ucache_clear) - Deletes the entire content of the user cache
- [ucache_exists()](#ucache_exists) - Checks if a variable exists in the user cache
- [ucache_meminfo()](#ucache_meminfo) - Retrieves information about user cache memory usage
- [ucache_info()](#ucache_info) - Retrieves information about data stored in the user cache
- [__sleep()](https://www.php.net/manual/en/language.oop5.magic.php#object.sleep)

### ucache_cas

(UCache >= 1.0.0)

ucache_cas - Compares the variable with an old value and assigns a new value to it

#### Description

```php
ucache_cas(string $key, int $cvalue, int $nvalue): bool
```

Compares the variable associated with the `key` with `cvalue` and, if it matches, assigns `nvalue` to it.

#### Parameters

`key`

The key that is used to store the variable in the cache. `key` is case sensitive.

`cvalue`

The value to which the variable pointed by `key` is compared. It has to be an integer.

`nvalue`

The value to be assigned to the variable pointed by `key` if a match is found. It has to be an integer.

#### Return Values

Returns true on success or false on failure: the key is missing or has expired, the value is not `cvalue`, or the stored
value is not an integer. The entry keeps its ttl.

#### Errors/Exceptions

Raises an `E_WARNING` ("function can only be called for key whose value is long") when the stored value is not an
integer.

#### Examples

**Example #1 Using `ucache_cas()`**

```php
<?php
ucache_set('counter', 2922);
var_dump(ucache_cas('counter', 2922, 1));
var_dump(ucache_get('counter'));
var_dump(ucache_cas('counter', 2922, 5));
?>
```

The above example will output:

```
bool(true)
int(1)
bool(false)
```

#### See Also

- [ucache_inc()](#ucache_inc) - Increments the value associated with the key
- [ucache_dec()](#ucache_dec) - Decrements the value associated with the key

### ucache_clear

(UCache >= 1.0.0)

ucache_clear - Deletes the entire content of the user cache

#### Description

```php
ucache_clear(): bool
```

Clears/deletes all the values stored in the user cache, for every process that shares the cache.

#### Parameters

This function has no parameters.

#### Return Values

Returns true.

#### Examples

**Example #1 Using `ucache_clear()`**

```php
<?php
ucache_set('green', 1);
ucache_set('blue', 2);
var_dump(ucache_clear());
var_dump(ucache_exists('green'));
var_dump(ucache_exists('blue'));
?>
```

The above example will output:

```
bool(true)
bool(false)
bool(false)
```

#### See Also

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in the
  cache
- [ucache_add()](#ucache_add) - Adds a variable in the user cache only if it does not already exist in the cache
- [ucache_delete()](#ucache_delete) - Deletes variables from the user cache
- [ucache_get()](#ucache_get) - Gets a variable stored in the user cache
- [ucache_exists()](#ucache_exists) - Checks if a variable exists in the user cache
- [ucache_meminfo()](#ucache_meminfo) - Retrieves information about user cache memory usage
- [ucache_info()](#ucache_info) - Retrieves information about data stored in the user cache

### ucache_dec

(UCache >= 1.0.0)

ucache_dec - Decrements the value associated with the key

#### Description

```php
ucache_dec(string $key, int $delta = 1, bool &$success = null): int|false
```

Decrements the value associated with the `key` by 1 or as specified by `delta`.

#### Parameters

`key`

The key that was used to store the variable in the cache. `key` is case sensitive.

`delta`

The value by which the variable is decremented. It has to be an integer.

`success`

Will be set to true on success and false on failure. It is left as it was when the cache is switched off
(`ucache.enabled=0`) or was not created.

#### Return Values

Returns the decremented value on success, or false if the key is missing or has expired, or if the stored value is not
an integer. The value changes in place: the entry keeps its ttl, and the call counts as a use of the entry
(`ucache.gc.ttl`). The result wraps around at the limits of the integer range.

#### Errors/Exceptions

Raises an `E_WARNING` ("function can only be called for key whose value is long") when the stored value is not an
integer. A missing key fails without a warning.

#### Examples

**Example #1 Using `ucache_dec()`**

```php
<?php
ucache_set('counter', 1);
var_dump(ucache_dec('counter', 2923, $success));
var_dump($success);
?>
```

The above example will output:

```
int(-2922)
bool(true)
```

#### See Also

- [ucache_inc()](#ucache_inc) - Increments the value associated with the key
- [ucache_cas()](#ucache_cas) - Compares the variable with an old value and assigns a new value to it

### ucache_delete

(UCache >= 1.0.0)

ucache_delete - Deletes variables from the user cache

#### Description

```php
ucache_delete(mixed $key): bool|array
```

Deletes the elements in the user cache pointed by `key`.

#### Parameters

`key`

The key that was used to store the variable in the cache. `key` is case sensitive. `key` can be an array of keys.

#### Return Values

If `key` is a string, returns true on success or false on failure: the key is missing or has expired.

If `key` is an array, returns an array of the keys that were deleted (int keys as strings), but false if the last key of
the array was not deleted, whatever happened to the others. The array passed in is not modified.

#### Examples

**Example #1 `ucache_delete()` with `key` as a string**

```php
<?php
ucache_set('green', 1);
var_dump(ucache_delete('green'));
var_dump(ucache_exists('green'));
?>
```

The above example will output:

```
bool(true)
bool(false)
```

**Example #2 `ucache_delete()` with `key` as an array**

```php
<?php
ucache_set(array('green' => 1, 'blue' => 2, 'cyan' => 3));
var_dump(ucache_delete(array('green', 'blue', 'cyan')));
?>
```

The above example will output:

```
array(3) {
  [0]=>
  string(5) "green"
  [1]=>
  string(4) "blue"
  [2]=>
  string(4) "cyan"
}
```

**Example #3 `ucache_delete()` with an array whose last key is missing**

```php
<?php
ucache_set(array('green' => 1, 'blue' => 2));
var_dump(ucache_delete(array('green', 'blue', 'red')));
var_dump(ucache_exists('green'));
?>
```

The above example will output:

```
bool(false)
bool(false)
```

#### See Also

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in the
  cache
- [ucache_add()](#ucache_add) - Adds a variable in the user cache only if it does not already exist in the cache
- [ucache_get()](#ucache_get) - Gets a variable stored in the user cache
- [ucache_clear()](#ucache_clear) - Deletes the entire content of the user cache
- [ucache_exists()](#ucache_exists) - Checks if a variable exists in the user cache
- [ucache_meminfo()](#ucache_meminfo) - Retrieves information about user cache memory usage
- [ucache_info()](#ucache_info) - Retrieves information about data stored in the user cache

### ucache_exists

(UCache >= 1.0.0)

ucache_exists - Checks if a variable exists in the user cache

#### Description

```php
ucache_exists(string $key): bool
```

Checks if a variable with the `key` exists in the user cache or not.

#### Parameters

`key`

The key that was used to store the variable in the cache. `key` is case sensitive.

#### Return Values

Returns true if the variable with the `key` exists and has not expired, otherwise returns false. The call does not count
as a use of the entry (`ucache.gc.ttl`).

#### Examples

**Example #1 Using `ucache_exists()`**

```php
<?php
ucache_set('green', 1);
var_dump(ucache_exists('green'));
var_dump(ucache_exists('red'));
?>
```

The above example will output:

```
bool(true)
bool(false)
```

#### See Also

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in the
  cache
- [ucache_add()](#ucache_add) - Adds a variable in the user cache only if it does not already exist in the cache
- [ucache_get()](#ucache_get) - Gets a variable stored in the user cache
- [ucache_delete()](#ucache_delete) - Deletes variables from the user cache
- [ucache_clear()](#ucache_clear) - Deletes the entire content of the user cache
- [ucache_meminfo()](#ucache_meminfo) - Retrieves information about user cache memory usage
- [ucache_info()](#ucache_info) - Retrieves information about data stored in the user cache

### ucache_get

(UCache >= 1.0.0)

ucache_get - Gets a variable stored in the user cache

#### Description

```php
ucache_get(mixed $key, bool &$success = null): mixed
```

Gets a variable stored in the user cache.

#### Parameters

`key`

The `key` that was used to store the variable in the cache. `key` is case sensitive. `key` can be an array of keys
(strings or ints). In this case the return value will be an array of the values of the keys that were found. If an
object, or an array containing objects, is returned, then the objects will be unserialized. See
[__wakeup()](https://www.php.net/manual/en/language.oop5.magic.php#object.wakeup) for details on unserializing objects.

`success`

Will be set to true on success and false on failure. It is left as it was when the cache is switched off
(`ucache.enabled=0`) or was not created (CLI without `ucache.enablecli=1`).

#### Return Values

If `key` is a string, the function returns the value of the variable stored with that key, or false if the key is
missing or has expired, or if an object of the value cannot be unserialized (see [Values](#values)). `success` is set to
true when the value was found and read without an exception, and to false otherwise: use it to tell a stored false from
a miss.

If `key` is an array, the function returns an array `key => value` of the keys that were found and read, keyed the way
PHP keys any array (`5` and `'5'` give the integer key 5, `'07'` stays a string); if none was found, the array is empty.
`success` is set to true, unless the call failed.

The call counts as a use of the entry (`ucache.gc.ttl`).

#### Errors/Exceptions

Raises an `E_WARNING` ("key array elements can only be string or long") and returns false when an element of a key array
is neither a string nor an int.

If unserializing a stored object throws (`__wakeup()`, `__unserialize()`, `Serializable::unserialize()`), the exception
propagates, and `success` stays false although the entry exists and is kept; with an array of keys, the keys after that
one are not read. If it fails (see [Values](#values)), PHP raises its warning (for example "Maximum depth of 4096
exceeded") and the call returns false; with an array of keys, that key is left out. On reaching memory_limit, see
[memory_limit](#memory_limit).

#### Examples

**Example #1 `ucache_get()` with `key` as a string**

```php
<?php
ucache_add('color', 'blue');
var_dump(ucache_get('color', $success));
var_dump($success);
?>
```

The above example will output:

```
string(4) "blue"
bool(true)
```

**Example #2 `ucache_get()` with `key` as an array**

```php
<?php
$array1 = array('green' => '5', 'Blue' => '6', 'yellow' => '7', 'cyan' => '8', 5 => 'five');
ucache_set($array1);
$array2 = array('green', 'Blue', 'yellow', 'cyan', 'red', '5');
var_dump(ucache_get($array2, $success));
var_dump($success);
?>
```

The above example will output:

```
array(5) {
  ["green"]=>
  string(1) "5"
  ["Blue"]=>
  string(1) "6"
  ["yellow"]=>
  string(1) "7"
  ["cyan"]=>
  string(1) "8"
  [5]=>
  string(4) "five"
}
bool(true)
```

**Example #3 Telling a stored false from a miss**

```php
<?php
ucache_set('flag', false);
var_dump(ucache_get('flag', $success), $success);
var_dump(ucache_get('missing', $success), $success);
?>
```

The above example will output:

```
bool(false)
bool(true)
bool(false)
bool(false)
```

#### See Also

- [ucache_add()](#ucache_add) - Adds a variable in the user cache only if it does not already exist in the cache
- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in the
  cache
- [ucache_delete()](#ucache_delete) - Deletes variables from the user cache
- [ucache_clear()](#ucache_clear) - Deletes the entire content of the user cache
- [ucache_exists()](#ucache_exists) - Checks if a variable exists in the user cache
- [ucache_meminfo()](#ucache_meminfo) - Retrieves information about user cache memory usage
- [ucache_info()](#ucache_info) - Retrieves information about data stored in the user cache
- [__wakeup()](https://www.php.net/manual/en/language.oop5.magic.php#object.wakeup)

### ucache_inc

(UCache >= 1.0.0)

ucache_inc - Increments the value associated with the key

#### Description

```php
ucache_inc(string $key, int $delta = 1, bool &$success = null): int|false
```

Increments the value associated with the `key` by 1 or as specified by `delta`.

#### Parameters

`key`

The key that was used to store the variable in the cache. `key` is case sensitive.

`delta`

The value by which the variable is incremented. It has to be an integer.

`success`

Will be set to true on success and false on failure. It is left as it was when the cache is switched off
(`ucache.enabled=0`) or was not created.

#### Return Values

Returns the incremented value on success, or false if the key is missing or has expired, or if the stored value is not
an integer. The value changes in place: the entry keeps its ttl, and the call counts as a use of the entry
(`ucache.gc.ttl`). The result wraps around at the limits of the integer range.

#### Errors/Exceptions

Raises an `E_WARNING` ("function can only be called for key whose value is long") when the stored value is not an
integer. A missing key fails without a warning.

#### Examples

**Example #1 Using `ucache_inc()`**

```php
<?php
ucache_set('counter', 1);
var_dump(ucache_inc('counter', 2921, $success));
var_dump($success);
?>
```

The above example will output:

```
int(2922)
bool(true)
```

#### See Also

- [ucache_dec()](#ucache_dec) - Decrements the value associated with the key
- [ucache_cas()](#ucache_cas) - Compares the variable with an old value and assigns a new value to it

### ucache_info

(UCache >= 1.0.0)

ucache_info - Retrieves information about data stored in the user cache

#### Description

```php
ucache_info(bool $summaryonly = false, ?string $key = null, int $offset = 0, int $limit = 0): array|false
```

Retrieves information about data stored in the user cache.

#### Parameters

`summaryonly`

Controls whether the returned array contains information about individual cache entries along with the user cache
summary.

`key`

The key of an entry in the user cache. If specified, the returned array contains information only about that cache
entry, whatever `summaryonly` is (nothing if the key is missing or has expired). If null or not specified and
`summaryonly` is false, the returned array contains information about all the entries in the cache, or the part of them
that `offset` and `limit` select.

`offset`

Without `key`, the number of entries to skip at the start of the listing. The entries are listed in the order of the
cache's hash table, which stays the same while no entry is added or removed: a listing taken a page at a time while
other requests write to the cache may skip or repeat entries. Ignored with `key`.

`limit`

Without `key`, the largest number of entries to list, 0 meaning all of them. The full listing of a large cache can run
past memory_limit: list it a part at a time (Example #2). Ignored with `key`.

#### Return Values

Array of meta data about the user cache, or false on failure (the cache was not created, `offset` or `limit` is
negative, or memory_limit was reached while the entries were listed). The array contains:

- `total_cache_uptime` - total time in seconds that the user cache has been active
- `is_local_cache` - always false (the user cache is always the shared one)
- `total_item_count` - total number of elements that are currently in the user cache, expired ones included
- `total_hit_count` - number of times the data has been served from the cache
- `total_miss_count` - number of times the data has not been found in the cache
- `last_scavenge_seconds` - seconds since the scavenger last ran a step (at the end of a request or on a write) or a
  full pass (when the cache was full), or null if it has not run since the cache was created. A step that
  `ucache.gc.threshold` skips does not count.
- `last_scavenge_count` - the entries that step or pass removed
- `total_scavenged_count` - the entries that steps and full passes removed since the cache was created, evicted entries
  included
- `total_evicted_count` - the entries evicted from a full cache since the cache was created
- `ucache_entries` - an array, indexed from 1 (from `offset` + 1 without `key`: the position of the entry in the full
  listing), that contains the information about the cached entries:
  - `key_name` - name of the key which is used to store the data
  - `value_type` - type of value stored by the key (`long`, `double`, `string`, `bool`, `null`, `array` or `object`)
  - `value_size` - memory taken by the value, in bytes
  - `ttl_seconds` - the ttl the entry was stored with, in seconds, 0 meaning that it never expires
  - `age_seconds` - time elapsed from the time data has been added in the cache
  - `hitcount` - number of times data has been served from the cache

With `summaryonly` set to true, `ucache_entries` is empty. `total_item_count` always counts the whole cache, whatever
`offset` and `limit` select. `ucache_info()` removes nothing, does not count as a use of the entries, and also works
with `ucache.enabled=0`: its full listing shows the expired entries that nothing has removed yet (see
[Expiry and the scavenger](#expiry-and-the-scavenger)).

#### Examples

**Example #1 Using `ucache_info()`**

```php
<?php
ucache_set('green', 1);
ucache_set('blue', array(1, 2), 300);
ucache_get('green');
ucache_get('red');
print_r(ucache_info());
?>
```

The above example will output:

```
Array
(
    [total_cache_uptime] => 0
    [is_local_cache] => 
    [total_item_count] => 2
    [total_hit_count] => 1
    [total_miss_count] => 1
    [last_scavenge_seconds] => 
    [last_scavenge_count] => 0
    [total_scavenged_count] => 0
    [total_evicted_count] => 0
    [ucache_entries] => Array
        (
            [1] => Array
                (
                    [key_name] => blue
                    [value_type] => array
                    [value_size] => 128
                    [ttl_seconds] => 300
                    [age_seconds] => 0
                    [hitcount] => 0
                )

            [2] => Array
                (
                    [key_name] => green
                    [value_type] => long
                    [value_size] => 16
                    [ttl_seconds] => 0
                    [age_seconds] => 0
                    [hitcount] => 1
                )

        )

)
```

On PHP 7.2 to 8.1, the `value_size` of `blue` is 160: the elements of a packed array take 32 bytes there, 16 since PHP
8.2.

**Example #2 Listing the entries a page at a time**

```php
<?php
// 1000 entries per call, so that a large cache does not run past memory_limit
$offset = 0;
do {
    $info = ucache_info(false, null, $offset, 1000);
    if ($info === false) {
        break;
    }
    foreach ($info['ucache_entries'] as $position => $entry) {
        echo $position, ': ', $entry['key_name'], "\n";
    }
    $offset += 1000;
} while ($offset < $info['total_item_count']);
?>
```

#### See Also

- [ucache_meminfo()](#ucache_meminfo) - Retrieves information about user cache memory usage
- [UCache Statistics Script](#ucache-statistics-script)

### ucache_meminfo

(UCache >= 1.0.0)

ucache_meminfo - Retrieves information about user cache memory usage

#### Description

```php
ucache_meminfo(): array|null
```

Retrieves information about the memory usage of the user cache, which is one shared memory segment of `ucache.size`
megabytes.

#### Parameters

This function has no parameters.

#### Return Values

Array of meta data about the user cache memory usage, or null if the cache was not created (CLI without
`ucache.enablecli=1`). It also works with `ucache.enabled=0`. The array contains:

- `memory_total` - amount of memory in bytes allocated for the user cache
- `memory_free` - amount of free memory in bytes available for the user cache
- `num_used_blks` - number of memory blocks used by the user cache
- `num_free_blks` - number of free memory blocks available for the user cache
- `memory_overhead` - amount of memory in bytes taken by the headers of the used and free blocks

#### Examples

**Example #1 Using `ucache_meminfo()`**

```php
<?php
print_r(ucache_meminfo());
?>
```

The above example will output:

```
Array
(
    [memory_total] => 8388608
    [memory_free] => 8386744
    [num_used_blks] => 1
    [num_free_blks] => 1
    [memory_overhead] => 128
)
```

#### See Also

- [ucache_info()](#ucache_info) - Retrieves information about data stored in the user cache
- [UCache Statistics Script](#ucache-statistics-script)

### ucache_set

(UCache >= 1.0.0)

ucache_set - Adds a variable in the user cache and overwrites a variable if it already exists in the cache

#### Description

```php
ucache_set(mixed $key, mixed $value = null, int $ttl = 0): bool|array
ucache_set(array $values, mixed $unused = null, int $ttl = 0): bool|array
```

Adds a variable in the user cache. Overwrites a variable if it already exists in the cache. The added or updated
variable remains in the user cache unless its time to live expires or it is deleted by using the `ucache_delete()` or
`ucache_clear()` functions.

#### Parameters

`key`

Store the variable using this key name. If a variable with the same key is already present, the function overwrites the
previous value with the new one. `key` is case sensitive (see [Keys](#keys)).

`value`

Value of a variable to store. Value supports all data types except resources, such as file handles. Objects are
serialized (see [Values](#values)).

`values`

Associative array of keys and values, which stores all the pairs in one call.

`unused`

Not used in the array form.

`ttl`

Time for the variable to live in the cache in seconds. After the value specified in `ttl` has passed, the stored
variable can no longer be read, and the scavenger frees it (see [Expiry and the scavenger](#expiry-and-the-scavenger)).
The default value of 0 means that the variable never expires. A negative `ttl` makes the call fail, and a variable with
a `ttl` above 4294967 (about 49.7 days) never expires.

#### Return Values

If `key` is a string, the function returns true on success and false on failure: the key is blank or longer than 4096
characters, no value was given, the value is or holds a resource, is nested too deeply or does not fit in the cache, its
serialized objects would pass memory_limit, or `ttl` is negative.

If `key` is an array, the function returns an empty array if all the name => value pairs were stored, or else an array
of the names that could not be stored, each with the value -1.

#### Errors/Exceptions

Raises an `E_WARNING` for a blank key ("key cannot be blank string"), a negative ttl ("ttl cannot be less than 0") and a
resource given as the value ("value cannot be a resource"). A key longer than 4096 characters, a resource inside an
array value, or a value nested too deeply, fails without a warning.

If serializing an object of the value throws (a Closure, an object of an anonymous class, a Generator, a `__sleep()` or
`__serialize()` that throws), the exception propagates and the value is not stored. In the array form, the pairs before
that element are stored, that one and the ones after it are not.

#### Examples

**Example #1 `ucache_set()` with `key` as a string**

```php
<?php
var_dump(ucache_set('green', 1));
var_dump(ucache_set('green', 2, 60));
var_dump(ucache_get('green'));
?>
```

The above example will output:

```
bool(true)
bool(true)
int(2)
```

**Example #2 `ucache_set()` with `key` as an array**

```php
<?php
$colors = array('green' => 1, 'blue' => array(2, 3), 'red' => fopen('php://memory', 'r'));
var_dump(ucache_set($colors, null, 300));
var_dump(ucache_get(array('green', 'blue', 'red')));
?>
```

The above example will output:

```
array(1) {
  ["red"]=>
  int(-1)
}
array(2) {
  ["green"]=>
  int(1)
  ["blue"]=>
  array(2) {
    [0]=>
    int(2)
    [1]=>
    int(3)
  }
}
```

#### See Also

- [ucache_add()](#ucache_add) - Adds a variable in the user cache only if it does not already exist in the cache
- [ucache_get()](#ucache_get) - Gets a variable stored in the user cache
- [ucache_delete()](#ucache_delete) - Deletes variables from the user cache
- [ucache_clear()](#ucache_clear) - Deletes the entire content of the user cache
- [ucache_exists()](#ucache_exists) - Checks if a variable exists in the user cache
- [ucache_meminfo()](#ucache_meminfo) - Retrieves information about user cache memory usage
- [ucache_info()](#ucache_info) - Retrieves information about data stored in the user cache
- [__sleep()](https://www.php.net/manual/en/language.oop5.magic.php#object.sleep)

## Building for Windows

You need Visual Studio 2022 with MSBuild and the C++ toolset of each PHP version (the Visual Studio Installer adds them
as individual components), a Windows 10 or 11 SDK, and `curl` and `tar` (in Windows 10 1803 and later):

| PHP | Toolset | Visual Studio component |
|---|---|---|
| 7.2, 7.3, 7.4 | v141 | MSVC v141 - VS 2017 C++ x64/x86 build tools |
| 8.0, 8.1, 8.2, 8.3 | v142 | MSVC v142 - VS 2019 C++ x64/x86 build tools |
| 8.4, 8.5 | v143 | MSVC v143 - VS 2022 C++ x64/x86 build tools |

Each DLL must be linked with the toolset of its PHP version: since PHP 7.4, PHP refuses to load a DLL linked with a
newer linker than its own.

```
build.bat <version>            build for PHP 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4 or 8.5, and test
build.bat <version> install    build and test, then copy the DLL into %PHP_DIR%\ext if the tests passed
build-all.bat [install]        build and test every version, one after the other
```

build.bat downloads the devel pack of the PHP version into `PHP_DEVEL_ROOT` (`D:\Dev\php-devel` by default) when it is
missing, builds `bin\Release\<version>\php<major><minor>\php_ucache.dll`, and tests it with test-build.php, without and
with opcache, using the PHP of `PHP_DIR` (`c:\php72` to `c:\php85`, set in the table at the end of build.bat).

Without build.bat, give MSBuild the devel pack and the toolset of the version (`php7.lib` for PHP 7.2 to 7.4, and for
v141 also the version of an installed Windows SDK, such as `/p:WindowsTargetPlatformVersion=10.0.26100.0`):

```
MSBuild php_ucache.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 "/p:PhpSdkRoot=<pack>\include" "/p:PhpLibName=<pack>\lib\php8.lib"
```

In a PHP source tree built with the PHP SDK, `config.w32` adds the `--enable-ucache` configure option (NTS builds only).

## Migrating from WinCache

| WinCache 2.0.0.8 | UCache |
|---|---|
| `wincache_ucache_get()` | `ucache_get()` |
| `wincache_ucache_set()` | `ucache_set()` |
| `wincache_ucache_add()` | `ucache_add()` |
| `wincache_ucache_delete()` | `ucache_delete()` |
| `wincache_ucache_clear()` | `ucache_clear()` |
| `wincache_ucache_exists()` | `ucache_exists()` |
| `wincache_ucache_info()` | `ucache_info()` |
| `wincache_ucache_meminfo()` | `ucache_meminfo()` |
| `wincache_ucache_inc()` | `ucache_inc()` |
| `wincache_ucache_dec()` | `ucache_dec()` |
| `wincache_ucache_cas()` | `ucache_cas()` |
| `wincache.ucenabled` | `ucache.enabled` |
| `wincache.ucachesize` | `ucache.size` |
| `wincache.enablecli` | `ucache.enablecli` |
| `wincache.namesalt` | `ucache.namesalt` |
| `wincache.apppoolid` | `ucache.apppoolid` |
| `wincache.filemapdir` | `ucache.filemapdir` |
| `wincache.debuglevel` | `ucache.debuglevel` |
| `wincache.php` (statistics page) | `ucache.php` (see [UCache Statistics Script](#ucache-statistics-script)) |

> **Note:** Code that calls the `wincache_ucache_*()` functions can keep calling them, without any change: set
> `ucache.prefix=wincache_ucache_`, and UCache's functions take WinCache's names (`wincache_ucache_get()`,
> `wincache_ucache_set()` and so on, while `ucache_get()` and the others do not exist). Load UCache instead of WinCache,
> not beside it: two extensions cannot define functions of the same name. The settings in php.ini take their `ucache.*`
> names, as in the table above.

Not carried over: the file and resolve-path caches, the session handler, function rerouting,
`wincache_lock()`/`wincache_unlock()` and the debugging setting `wincache.localheap`. UCache does not see data stored by
WinCache; it starts with an empty cache. Code that defines `ucache_*` functions of its own (a wrapper around
`wincache_ucache_*()`, for example) can keep them: set `ucache.prefix` to another prefix.

Existing code may notice these differences:

- `ucache_get()` with an array of keys returns numeric keys as integer keys, and integer keys keep their full value in
  the array forms. WinCache 2.0.0.8 returned numeric keys as string keys that neither `$result[5]` nor `$result['5']`
  could read, and cut integer keys to 32 bits.
- `ucache_get()` returns false, with `success` false, when an object of the value cannot be unserialized. WinCache
  2.0.0.8 returned null in its place, with `success` true.
- Invalid arguments throw `ArgumentCountError` or `TypeError` (or raise a warning, on PHP 7), also with
  `ucache.enabled=0`. WinCache 2.0.0.8 returned false without a warning.
- A null `key` given to `ucache_info()` means no key. WinCache 2.0.0.8 took it as the empty key, and listed no entry.
