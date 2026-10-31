<p align="center">
    <img style="height:80px" src="ucache-logo.svg">
</p>

<p align="center">
  <img src="https://img.shields.io/github/issues/pkoutsogiannis/ucache" alt="Issues">
  <img src="https://img.shields.io/github/languages/top/pkoutsogiannis/ucache" alt="Language">
  <img src="https://img.shields.io/github/stars/pkoutsogiannis/ucache" alt="Stars">
  <img src="https://img.shields.io/github/forks/pkoutsogiannis/ucache" alt="Forks">
</p>

UCache is the user cache of WinCache (Microsoft's Windows Cache Extension for PHP, 2.0.0.8) as a PHP extension of
its own, for PHP 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4 and 8.5 on Windows x64. Extension `ucache`, file
`php_ucache.dll`, version 1.0.0.1. License: see [LICENSE](LICENSE) (the original Microsoft license).

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
- [The Scavenger](#the-scavenger)
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
  - [ucache_set](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in
    the cache
- [Building for Windows](#building-for-windows)
  - [Prerequisites](#prerequisites)
  - [Compiling and building](#compiling-and-building)
  - [Verifying the build](#verifying-the-build)
  - [Debug builds](#debug-builds)
- [Migrating from WinCache](#migrating-from-wincache)
  - [Differences from WinCache 2.0.0.8](#differences-from-wincache-2008)

## Introduction

UCache keeps PHP values in a shared memory segment, so that all the PHP processes of the same IIS application pool,
or more generally with the same parent process (for example the php-cgi.exe processes started by one FastCGI host),
use the same cache: a value that one request stores can be read by the later requests of every process of the pool,
until it expires or is deleted.

UCache is the user cache of WinCache 2.0.0.8, extracted and renamed. The file cache, resolve-path cache, file change
notification, session handler, `wincache_lock()`/`wincache_unlock()`, PHP function rerouting and ETW tracing have been
removed. The functions take the same arguments and return the same values as the `wincache_ucache_*` functions of
WinCache 2.0.0.8, apart from bug fixes and a few additions, such as the scavenger keys and the `offset` and `limit`
parameters of `ucache_info()` (see [Migrating from WinCache](#migrating-from-wincache)).

## Installing/Configuring

### Requirements

The extension is supported on the following configurations:

Windows:

- Windows x64. On a web server, PHP must run through FastCGI (php-cgi.exe), as IIS runs it: the processes of one IIS
  application pool share a cache. The CLI and the built-in web server (`php -S`) work too.

PHP:

- PHP 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4 or 8.5, non-thread-safe (NTS) x64 build. There is no thread-safe (ZTS)
  build.

> **Note:** Each PHP version needs the DLL built for it (see [Building for Windows](#building-for-windows)), and
> processes of different PHP versions never share a cache. A DLL loads in any NTS x64 PHP of the version it was built
> for that uses the same compiler (the windows.php.net builds of that version), whatever the patch release: PHP checks
> the build ID (`PHP Extension Build` in `phpinfo()`, for example `API20240924,NTS,VS17`).

### Installation

UCache is not a PECL package: build php_ucache.dll from the sources (see
[Building for Windows](#building-for-windows)), or take a DLL built for the PHP version in use.

To install and enable the extension, follow these steps:

1. Copy the php_ucache.dll file into the PHP extensions folder: the "ext" folder next to php.exe, or the folder set
   by `extension_dir` in php.ini. For example: `C:\php84\ext`.
2. Using a text editor, open the php.ini file, which is usually located in the folder of php.exe. For example:
   `C:\php84\php.ini`.
3. Add the following line to the php.ini file: `extension=php_ucache.dll`. Add the `ucache.*` settings to change, if
   any (see [Runtime Configuration](#runtime-configuration)).
4. Save and close the php.ini file.
5. Recycle the IIS application pools for PHP to pick up the configuration changes, or restart the web server.
6. To check that the extension has been enabled, create a file called phpinfo.php with PHP code that calls the
   `phpinfo()` function, save it in the root folder of an IIS web site that uses PHP, then open a browser and make a
   request to `http://localhost/phpinfo.php`. Search within the returned web page for a section called `ucache`. If
   the extension is enabled, then the `phpinfo()` output lists the `ucache.*` settings. From the command line,
   `php -m` lists `ucache`.

> **Note:** Do not forget to remove the phpinfo.php file from the web site's root folder after verifying that the
> extension has been enabled.

> **Note:** To replace an older php_ucache.dll, stop the application pool or the web server completely, replace the
> DLL, and then start it. Old and new PHP processes must never run at the same time: they would share one cache,
> because the names of its shared memory and synchronization objects stay the same, although the two DLLs can store
> values differently (an old process can crash on arrays that a new one stored). An overlapped recycle, the IIS
> default, starts the new processes while the old ones are still running. For the same reason, do not rename a loaded
> DLL or switch PHP folders under a running pool, unless `ucache.namesalt` changes in its first 8 characters (the only
> ones used) at the same time, which gives the new processes a cache of their own.

> **Note:** Under the CLI, the cache is only created when `ucache.enablecli=1`, in php.ini or on the command line:
> `php -d ucache.enablecli=1 script.php`.

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
| `ucache.gc.threshold` | "0" | "0" | "100" | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.gc.interval` | "10" | "0" | | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.gc.step` | "0" | "0" | | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.gc.ttl` | "0" | "0" | | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.gc.evict` | "0" | "0" | "100" | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |
| `ucache.prefix` | "ucache_" | | | PHP_INI_SYSTEM | Available as of UCache 1.0.0 |

For further details and definitions of the PHP_INI_* modes, see
[Where a configuration setting may be set](https://www.php.net/manual/en/configuration.changes.modes.php). The
PHP_INI_SYSTEM settings come from php.ini, or from `-d` on the command line of PHP, when its process starts:
`ini_set()` returns false for them, and `.user.ini` files cannot set them. Only `ucache.enabled` and
`ucache.debuglevel` can be changed by a script.

Here's a short explanation of the configuration directives.

`ucache.enabled` *bool*

Enables or disables the user cache functionality. With 0, all functions except `ucache_info()` and `ucache_meminfo()`
return false, and the end of the request does not run the scavenger. Their arguments are still checked first: invalid
arguments throw `ArgumentCountError` or `TypeError` even then, or raise a warning on PHP 7 without `strict_types`. The
cache is still created at startup, so a script can switch it back on with `ini_set()`.

`ucache.enablecli` *bool*

Enables or disables the user cache when PHP runs from the command line (CLI). Without it, the cache is not created
under the CLI, and the functions fail there.

`ucache.size` *int*

Defines the size of the shared memory segment of the user cache, in megabytes. Values outside 5..930 are clamped to
the nearest limit.

`ucache.namesalt` *string*

Defines a string added to the names of the shared memory and synchronization objects, to keep apart caches that
would otherwise be shared. Only the first 8 characters are used: `ini_get()`, `phpinfo()` and ucache.php show the
whole value, but two salts that differ only after the 8th character give the same cache.

`ucache.apppoolid` *string*

Defines the name that decides which processes share a cache, instead of the parent process id. If not set, the
`APP_POOL_ID` environment variable is used when IIS sets it.

`ucache.filemapdir` *string*

Defines the folder of a file that backs the shared memory instead of the paging file. Only used when the process
group id (the parent process id, or the hash of the application pool id) is above 99999.

`ucache.debuglevel` *int*

Enables a debug trace written with OutputDebugString: 0 (off), 101, 201, 301, 401 or 501 (the most detailed). Other
values are ignored.

`ucache.gc.threshold` *int*

Defines a free memory percentage. When > 0, every write runs a scavenger step while free memory is at or below it,
and the steps at the end of requests run only then. 0: writes do not run steps. See [The Scavenger](#the-scavenger).

`ucache.gc.interval` *int*

Defines the seconds between the scavenger steps at the end of requests. 0 turns them off. See
[The Scavenger](#the-scavenger).

`ucache.gc.step` *int*

Defines the hash buckets that a scavenger step goes through. 0: automatic, at least 16, so that the steps at the end
of requests go through the whole table in at most about 5 minutes (about 2 minutes for the default 8 MB). See
[The Scavenger](#the-scavenger).

`ucache.gc.ttl` *int*

Defines, in seconds, how long an entry can go unused. When > 0, the scavenger also removes the entries not used for
this long, whatever their ttl. 0: the scavenger removes expired entries only. See [The Scavenger](#the-scavenger).

`ucache.gc.evict` *int*

Defines a free memory percentage, 0 to 100 (higher values act as 100). When > 0 and `ucache_set()` or `ucache_add()`
finds the cache full, the entries not used for the longest time are evicted until this percentage of the cache is
free, or more if the value still does not fit. 0: no eviction. See [The Scavenger](#the-scavenger).

`ucache.prefix` *string*

Defines the prefix of the function names, in place of `ucache_`, for code that already has functions of these names:
with `ucache.prefix=my_cache_`, the functions are `my_cache_get()`, `my_cache_set()`, ... and `ucache_get()` and the
others do not exist. The prefix may contain letters, digits, underscores and bytes 0x80 to 0xff, at most 64 of them,
and must not start with a digit; empty gives `get()`, `set()`, .... When the prefix is not valid, or makes a name that
another extension already uses (`date_` would make `date_add()`), PHP starts with a warning and without any of the
functions. This manual names the functions with the default prefix. ucache.php follows the setting.

#### Settings per IIS application pool

Each IIS application pool runs php-cgi processes of its own, and they share a cache of their own (see
[How the Cache Works](#how-the-cache-works)). To give a pool its own `ucache.*` settings, give its sites a FastCGI
application of their own. IIS tells FastCGI applications apart by the program and its arguments, and php-cgi takes `-d`
arguments. In `applicationHost.config`:

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

Instead of `-d` arguments, the `environmentVariables` of a FastCGI application can set `PHP_INI_SCAN_DIR` (the
folders, separated by `;`, of extra .ini files read after php.ini) or `PHPRC` (the folder of a php.ini of its own).
The application still needs arguments that differ from those of the other applications of the same php-cgi.exe.

The processes that share a cache must use the same `ucache.*` settings. Two sites of one pool that use different
FastCGI applications still share the pool's cache: give them different `ucache.namesalt` values if their settings
differ. php-cgi reads the settings when it starts: after changing them, recycle the pool. After changing
`ucache.size`, stop the pool and start it again instead: an overlapped recycle (the IIS default) starts the new
processes while the old ones still run, and they would open the cache the old ones created (see
[Installation](#installation)).
`phpinfo()` on a site shows the settings its processes use.

### Resource Types

This extension has no resource types defined.

### Predefined Constants

This extension has no constants defined.

### UCache Statistics Script

The sources of UCache include a PHP script, ucache.php (WinCache's wincache.php for UCache), that can be used to
obtain cache information and statistics: the server and cache settings, the memory use, the hit and miss counts, what
the scavenger removed and evicted and when it last ran, the list of entries (250 per page, with links to the other
pages) and the value stored under a key; with authentication on, it can also clear the whole cache.

To use ucache.php, copy it into a root folder of a web site that runs with the extension loaded, or into any
subfolder, preferably one that only administrators can reach. To protect the script, open it in any text editor and
replace the values of the `USERNAME` and `PASSWORD` constants: both default to `ucache`, and the page refuses to work
while the password is `ucache`. If any other IIS authentication is enabled on the server, then follow the
instructions in the comments:

**Example #1 Authentication configuration for ucache.php**

```php
<?php
/**
 * ======================== CONFIGURATION SETTINGS ==============================
 * If you do not want to use authentication for this page, set USE_AUTHENTICATION to 0.
 * If you use authentication then replace the default password.
 */
define('USE_AUTHENTICATION', 1);
define('USERNAME', 'ucache');
define('PASSWORD', 'ucache');

/**
 * The Basic PHP authentication will work only when IIS is configured to support
 * Anonymous Authentication' and nothing else. If IIS is configured to support/use
 * any other kind of authentication like Basic/Negotiate/Digest etc, this will not work.
 * In that case use the array below to define the names of users in your
 * domain/network/workgroup which you want to grant access to.
 */
$user_allowed = array('DOMAIN\user1', 'DOMAIN\user2', 'DOMAIN\user3');

/**
 * If the array contains string 'all', then all the users authenticated by IIS
 * will have access to the page. Uncomment the below line and comment above line
 * to grant access to all users who gets authenticated by IIS.
 */
/* $user_allowed = array('all'); */

/** ===================== END OF CONFIGURATION SETTINGS ========================== */
?>
```

With `USE_AUTHENTICATION` set to 0 there is no authentication, and the page no longer offers to clear the cache;
it still shows values, so anyone who can reach it can read everything in the cache. The Clear All button only works from the page itself: it sends back a token that the page keeps in
a `SameSite=Strict` cookie, so the browser has to accept that cookie. The page also refuses to be shown inside a
frame.

> **Note:** Always protect the ucache.php script by using either the built-in authentication or the server's
> authentication mechanism. Leaving this script unprotected may compromise the security of your web application and
> web server.

## How the Cache Works

### Sharing

Processes share a cache when they have the same parent process id, or the same `ucache.apppoolid` (default: the
`APP_POOL_ID` environment variable set by IIS). There is no private per-process cache. The cache is not persistent:
it starts empty when the first process of a group creates it.

### Values

Values are copied into shared memory on write and copied back on read. Scalars, strings, null and arrays (including
nested arrays and references between their elements) are stored as they are; objects are serialized on write and
unserialized on read; resources cannot be stored. `$GLOBALS`, as a value or as the array of the array forms, is
stored as the global variables it holds, without those that are not set, as `serialize()` takes them (before PHP 8.1
its entries point to the variables).

An entry holds the value as it was when `ucache_set()` was called: changes that serializing an object (`__sleep()`,
`__serialize()`) makes to the rest of the value are not stored, and each object is stored as it is when it is
serialized. An object that occurs more than once in the value's arrays and references is serialized once, but reads
back as separate copies (one that another object of the value also holds is serialized again as part of that object).

A value that cannot be serialized (a Closure, an object of an anonymous class, a Generator, an object whose
`__sleep()` or `__serialize()` throws) is not stored: the exception propagates out of `ucache_set()` or
`ucache_add()`. A value whose arrays and references are nested more than 10,000 levels deep (each array and each
reference is one level, not counting what objects hold) is not stored either: `ucache_set()` and `ucache_add()`
return false without a warning, as for a resource inside the value. On PHP 8.1 and later, so is a value nested too
deeply for the C stack that is left, and `ucache_get()` of such an entry returns false. This matters in a Fiber
(PHP 8.1 and later), whose stack is 2 MB by default (`fiber.stack_size`): a few thousand levels can be too many
there. What objects hold is serialized by PHP itself, which checks the stack only from PHP 8.3 on: on 8.1 and 8.2,
inside a Fiber with that stack, an object whose properties nest arrays more than about 1,250 levels deep ends the
process, as `serialize()` of it does (8.3 and later throw an `Error`, which propagates out of `ucache_set()`).

An object that cannot be unserialized when it is read makes the read fail, as it makes `unserialize()` fail:
`ucache_get()` returns false with PHP's warning, which says why, and the entry stays for the processes that can read
it. This happens to an enum or an enum case that the reading process does not define (PHP 8.1 and later), to an
object of a class that changed since it was stored (one that implements `Serializable` now, for example), and, on
PHP 7.4 and later, to an object nested deeper than `unserialize_max_depth` (4096 levels by default). An object of a
class that is not defined comes back as `__PHP_Incomplete_Class`, as from `unserialize()`.

### Keys

Keys are strings; other scalar keys are converted to strings. Keys are case sensitive. A key ends at its first NUL
byte (`"a\0b"` and `"a\0c"` are both the key `"a"`). `ucache_set()` and `ucache_add()` store keys of at most 4096
characters, and reject an empty key only in their single-key form: the array form does not check, so `''` can be
stored that way (and then read with `ucache_get('')`).

### Expiry

An entry stored with a ttl can be read for up to `ttl` + 1 seconds after it was stored, and reads do not extend it.
An expired entry keeps its memory until it is removed: see [The Scavenger](#the-scavenger) for the exact rules.

### Serialization and the cache lock

Values are copied in without holding the cache lock, and objects are unserialized once it has been released, so
`__sleep()`, `__serialize()`, `__wakeup()`, `__unserialize()`, `Serializable::serialize()` and
`Serializable::unserialize()` can call the UCache functions and do not hold up the other processes. A fatal error in
a cache function does not leave the cross-process lock held either: the other processes of the pool go on. UCache
serializes and unserializes each object as a top-level `serialize()` or `unserialize()` would, independently of any
`serialize()` or `unserialize()` that the caller is in the middle of.

### memory_limit

`ucache_set()` and `ucache_add()` serialize every object of the value before they copy anything into the cache. Each
serialization is built in request memory and then moved out of PHP's heap, so only the one being built counts
against PHP's own memory_limit: an object whose serialization alone does not fit still raises PHP's memory_limit
error (which ends the process on PHP builds that exit at memory_limit). The serializations kept until the value is
copied are counted against memory_limit by UCache itself: on every PHP version, if the memory in use plus those
serializations would exceed memory_limit, the value is not stored, and the call returns false without an error (in
the array forms, its key is among the failures).

On PHP 8.2.35, 8.3.36, 8.4.27, 8.5.12 and later, reaching memory_limit inside some internal functions, UCache's among
them, raises the fatal error only when the function returns, and the process is killed if it reaches a hard limit (by
default twice memory_limit) first. `ucache_get()`, `ucache_info()`, `ucache_set()` and `ucache_add()` therefore watch
memory_limit while they copy: when it is exceeded they stop and fail, and PHP then raises the fatal error. In their
single-key form, `ucache_set()` and `ucache_add()` store nothing if memory_limit was exceeded at any time during the
call, even briefly (unless the script had lowered memory_limit below a peak it had already reached). Their array
forms stop at the value during which memory_limit was exceeded: the keys before it are stored, that one and the ones
after it are not. The checks run between allocations, though: one string, or the serialization of one object, too big
to fit under the hard limit still gets the process killed (exit code 137, without a fatal error or shutdown
functions). Older versions raise the fatal error at once.

The full listing of `ucache_info()` copies every entry into request memory, about 800 bytes for an entry with a short
key: list a large cache a part at a time, with `offset` and `limit` (see [ucache_info](#ucache_info)).

## The Scavenger

The scavenger removes the entries that have expired and, if the settings ask for it, the entries that have not been
used for a while. It works in steps, at the end of requests and on writes, and in a full pass when a value does not
fit in the cache. Calls that look up a key also remove the expired entries they come across.

### Expiry and use

An entry stored with a ttl > 0 expires once more than `ttl` whole seconds have passed since it was stored: it can be
read for up to `ttl` + 1 seconds, and reads do not extend it. An entry stored with ttl 0 never expires. An expired
entry can no longer be read, but it keeps its memory until a call, a step or a full pass removes it.

An entry is used when `ucache_get()` reads it, when `ucache_set()` or `ucache_add()` stores it, and when
`ucache_inc()`, `ucache_dec()` or `ucache_cas()` finds it, whether or not they change it. `ucache_exists()` and
`ucache_info()` are not uses. The time since the last use matters only for `ucache.gc.ttl` and for eviction; it does
not change when an entry expires.

### Removal on contact

Every call that looks up a key (`ucache_get()`, `ucache_exists()`, `ucache_set()`, `ucache_add()`, `ucache_delete()`,
`ucache_inc()`, `ucache_dec()`, `ucache_cas()`, and each key of their array forms) removes the expired entries it
passes in the key's hash bucket: the key's own expired entry, and possibly expired entries of other keys that share
the bucket. So storing a key again after it expired leaves a single entry, and `ucache_delete()` of an expired key
returns false (the key is missing) and removes the entry. This removal on contact happens whatever the `ucache.gc.*`
settings are, and concerns expired entries only: the entries that `ucache.gc.ttl` targets are left to the scavenger.
`ucache_info()` and `ucache_meminfo()` never remove anything: the full `ucache_info()` listing shows the expired
entries that nothing has removed yet.

### Steps

A scavenger step goes through a number of hash buckets, starting where the previous step stopped, and removes:

- the expired entries;
- when `ucache.gc.ttl` > 0, also the entries not used for `ucache.gc.ttl` seconds or more, whatever their ttl (ttl 0
  included). This does not make them expire: until a step reaches such an entry, it can still be read, and reading
  it is a use.

Steps run:

- At the end of requests: once `ucache.gc.interval` seconds (10 by default) have passed since the cache was created
  or since the last step, in any process that shares the cache, the first request to end runs one step. The requests
  that end in between do not take the cache lock. `ucache.gc.interval=0` turns these steps off, and a request that
  ends with `ucache.enabled=0` does not run one.
- On writes, when `ucache.gc.threshold` > 0: every `ucache_delete()`, `ucache_inc()`, `ucache_dec()` and
  `ucache_cas()`, and every `ucache_set()` or `ucache_add()` that gets as far as storing its value, runs one step
  while free memory is at or below `ucache.gc.threshold` percent of the cache. The steps at the end of requests then
  also run only while free memory is that low; one that is skipped for this reason still counts as a step for
  `ucache.gc.interval`.

A step goes through `ucache.gc.step` buckets, at most the whole table. The default, 0, takes the larger of 16 and
buckets x `ucache.gc.interval` / 300, rounded up, so that the steps at the end of requests go through the whole table
in at most about 5 minutes while requests keep coming (16 buckets when `ucache.gc.interval` is 0). The table has a
prime number of buckets, about one per 32 to 64 KB of `ucache.size`: 197 for the default 8 MB (16 buckets a step, the
whole table in about 2 minutes), 25717 for 930 MB (858 buckets a step). A step also stops after about 1 ms of work
under the cache lock, and the next step goes on from there. So a step holds the cache lock for about 1 ms at most,
but going through a table that holds many entries can take more than 5 minutes.

### A full cache

When `ucache_set()` or `ucache_add()` runs out of shared memory for a value (in the array forms, for any of the
keys), it does not fail at once. It takes the cache lock and runs a full pass over the whole table, without the 1 ms
limit, that removes what a step removes. If `ucache.gc.evict` is > 0 and less than `ucache.gc.evict` percent of the
cache is then free, it evicts the entries not used for the longest time, whatever their ttl, one by one, until that
percentage is free. Then it tries to store the value once more. With `ucache.gc.evict`, a value that still does not
fit makes it evict again, until twice that percentage is free, then four times and so on, up to the whole cache:
free memory in many small pieces may not hold a value that needs one large block, so such a value, in a cache full
of small entries, can empty most of it. If the value still does not fit, the call fails: it returns false, or the
array form lists the key among the failures. A write that finds that another process ran a full pass meanwhile only
tries again. When a full pass removed nothing and the value still did not fit, the processes that share the cache
skip the pass for a while, at least a second and 20 times as long as the pass took (at most a minute): their writes
that run out of memory meanwhile fail at once (so do the following keys of an array form), and an entry that
expires meanwhile is removed by the next pass.

Eviction goes by groups of similar idle time (powers of two of seconds), the longest idle first; within a group,
entries go in the order of the table, so the order is only roughly that of the last use. With `ucache.gc.evict=0`,
the default, nothing is evicted: entries are removed only by their ttl, `ucache.gc.ttl`, a delete, a replace or a
clear.

### Freeing memory

Every removal (a delete, a replace by `ucache_set()`, a removal on contact, a step) unlinks the entries under the
cache lock and frees their memory right after the lock is released, before the call returns, so freeing a big entry
does not hold up the other processes. That is for up to 32 entries per call; the entries a call removes beyond that
(`ucache_clear()` of a full cache, for example) are freed at once, under the lock, and so is everything the full
pass of a full cache removes, so that the write waiting for the memory, and the next process that runs out of it,
see what is really free. If a process is killed while it frees, for example by an IIS FastCGI timeout, the memory of
the entries it had not freed yet (32 at most) stays lost until every process of the pool has exited; a process
killed in the middle of a free under the lock loses the rest of that entry.

### What ucache_info() reports

`ucache_info()` shows what the scavenger did: `last_scavenge_seconds` and `last_scavenge_count` for the last step or
full pass, `total_scavenged_count` for all the entries that steps and full passes removed, evicted entries included,
and `total_evicted_count` for the evicted ones. A step that `ucache.gc.threshold` skips is not counted, and neither
are removals on contact, deletes, replaces and clears.

### Tuning

- To go through the table faster or slower than the automatic step does, set `ucache.gc.step`: going through it then
  takes about buckets / `ucache.gc.step` x `ucache.gc.interval` seconds, more if steps reach the 1 ms limit. A fixed
  value does not grow with the cache: 16 buckets every 10 seconds take about 4.5 hours to go through the 25717
  buckets of a 930 MB cache.
- With the defaults, an entry without a ttl stays until it is deleted, replaced or cleared. If keys go out of use
  (one per user, per page or per day, for example), set `ucache.gc.ttl` above the longest time between two uses of
  an entry that should stay, for example 86400 (a day).
- If writes fail because the cache is full, give it a larger `ucache.size`, or set `ucache.gc.evict` (for example 10)
  instead of calling `ucache_clear()` from PHP when a write fails: eviction removes the entries not used for the
  longest time, `ucache_clear()` removes everything, for every process of the pool. A very low value frees little,
  so full passes come often, and each one holds the cache lock while it goes through the whole table. A
  `total_evicted_count` that keeps growing means that the cache is too small for what is stored in it.
- `ucache.gc.interval=0` turns off the steps at the end of requests: expired entries are then removed only on
  contact, by steps on writes (`ucache.gc.threshold`) and by the full pass of a full cache. It suits a cache where no
  entry has a ttl and `ucache.gc.ttl` is 0, since steps have nothing to remove there.

## UCache Functions

Argument names are the ones in the extension's arginfo, except `$values` and `$unused` in the array forms of
`ucache_set()` and `ucache_add()`, which arginfo calls `$key` and `$value`. On PHP 8 the optional parameters have
their defaults in arginfo, so named arguments can skip them: `ucache_set($values, ttl: 60)`,
`ucache_inc($key, success: $ok)`, `ucache_info(key: $key)`, `ucache_info(limit: 250)`. Reflection reports one
required parameter for `ucache_set()` and `ucache_add()`, as their array form takes only the array.

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

Adds a variable in the user cache, only if this variable does not already exist in the cache. The added variable
remains in the user cache unless its time to live expires or it is deleted by using the `ucache_delete()` or
`ucache_clear()` functions.

#### Parameters

`key`

Store the variable using this key name. If a variable with the same key is already present, the function fails and
returns false. `key` is case sensitive (see [Keys](#keys)). To overwrite the value even if `key` is present, use the
`ucache_set()` function instead.

`value`

Value of a variable to store. Value supports all data types except resources, such as file handles. Objects are
serialized (see [Values](#values)). Called with the key alone, without a value, the function fails; with named
arguments (PHP 8), an omitted `value` is null and is stored: `ucache_add('k', ttl: 60)` stores null.

`values`

Associative array of keys and values, which adds all the pairs in one call.

`unused`

Not used in the array form. A resource there still makes the call fail, with a warning.

`ttl`

Time for the variable to live in the cache in seconds. After the value specified in `ttl` has passed, the stored
variable can no longer be read, and the scavenger frees it (see [The Scavenger](#the-scavenger)). The default value
of 0 means that the variable never expires. A negative `ttl` makes the call fail. A `ttl` above 4294967295 counts as
4294967295, and a variable with a `ttl` above 4294967 (about 49.7 days) never expires: the age of an entry is
measured with the millisecond tick count of Windows, a 32-bit number.

#### Return Values

If `key` is a string, the function returns true on success and false on failure: the key exists, the key is blank or
longer than 4096 characters, no value was given, the value is or holds a resource, is nested too deeply or does not
fit in the cache, its serialized objects would pass memory_limit, or `ttl` is negative.

If `key` is an array, the function returns an empty array if all the name => value pairs were added, or else an
array of the names that could not be added, the existing ones included, each with the value -1. An element given by
reference is stored as its value.

#### Errors/Exceptions

Raises an `E_WARNING` when `key` is a string that already exists ("function called with a key which already exists";
in the array form, existing keys are listed among the failures without a warning), for a blank key ("key cannot be
blank string"), a negative ttl ("ttl cannot be less than 0") and a resource given as the value ("value cannot be a
resource"). A key longer than 4096 characters, a resource inside an array value, or a value nested too deeply, fails
without a warning.

If serializing an object of the value throws (a Closure, an object of an anonymous class, a Generator, a `__sleep()`
or `__serialize()` that throws), the exception propagates and the value is not stored. In the array form, the pairs
before that element are stored, that one and the ones after it are not.

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

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in
  the cache
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

Returns true on success or false on failure: the key is missing or has expired, the value is not `cvalue`, or the
stored value is not an integer. The entry keeps its ttl, and the call counts as a use of the entry
(`ucache.gc.ttl`), whether or not it changes the value.

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

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in
  the cache
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

Returns the decremented value on success, or false if the key is missing or has expired, or if the stored value is
not an integer. The value changes in place: the entry keeps its ttl, and the call counts as a use of the entry
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

If `key` is a string, returns true on success or false on failure: the key is missing. An expired key is missing:
the call returns false, and removes the expired entry (see [Removal on contact](#removal-on-contact)).

If `key` is an array, returns an array of the keys that were deleted (int keys as strings), but false if the last
key of the array was not deleted, whatever happened to the others. The array passed in is not modified.

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

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in
  the cache
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

Returns true if the variable with the `key` exists and has not expired, otherwise returns false. The call does not
count as a use of the entry (`ucache.gc.ttl`), and removes the expired entry of the key (see
[Removal on contact](#removal-on-contact)).

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

- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in
  the cache
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
[__wakeup()](https://www.php.net/manual/en/language.oop5.magic.php#object.wakeup) for details on unserializing
objects.

`success`

Will be set to true on success and false on failure. It is left as it was when the cache is switched off
(`ucache.enabled=0`) or was not created (CLI without `ucache.enablecli=1`). If `success` is a typed property (PHP 7.4
and later), the bool is assigned under the property's type rules: converted (an `int` property gets 1 or 0), or a
`TypeError` (an `array` property, or an `int` one under `strict_types`).

#### Return Values

If `key` is a string, the function returns the value of the variable stored with that key, or false if the key is
missing or has expired, or if an object of the value cannot be unserialized (see [Values](#values)). `success` is set
to true when the value was found and read without an exception, and to false otherwise: use it to tell a stored false
from a miss.

If `key` is an array, the function returns an array `key => value` of the keys that were found and read, keyed the
way PHP keys any array (`5` and `'5'` give the integer key 5, `'07'` stays a string); if none was found, the array is
empty. An element of `key` that is a reference (as a `foreach` by reference leaves them) is read as its value.
`success` is set to true, unless the call failed.

The call counts as a use of the entry (`ucache.gc.ttl`), and a read of an expired key removes its entry (see
[Removal on contact](#removal-on-contact)).

#### Errors/Exceptions

Raises an `E_WARNING` ("key array elements can only be string or long") and returns false when an element of a key
array is neither a string nor an int.

If unserializing a stored object throws (`__wakeup()`, `__unserialize()`, `Serializable::unserialize()`), the
exception propagates, and `success` stays false although the entry exists and is kept; with an array of keys, the
keys after that one are not read. If it fails (see [Values](#values)), PHP raises its warning (for example "Maximum
depth of 4096 exceeded") and the call returns false; with an array of keys, that key is left out. On reaching
memory_limit, see [memory_limit](#memory_limit).

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
- [ucache_set()](#ucache_set) - Adds a variable in the user cache and overwrites a variable if it already exists in
  the cache
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

Returns the incremented value on success, or false if the key is missing or has expired, or if the stored value is
not an integer. The value changes in place: the entry keeps its ttl, and the call counts as a use of the entry
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
`summaryonly` is false, the returned array contains information about all the entries in the cache, or the part
of them that `offset` and `limit` select.

`offset`

Without `key`, the number of entries to skip at the start of the listing. The entries are listed in the order of
the cache's hash table, which stays the same while no entry is added or removed: a listing taken a page at a time
while other requests write to the cache may skip or repeat entries. Ignored with `key`.

`limit`

Without `key`, the largest number of entries to list, 0 meaning all of them. The full listing of a large cache can
run past memory_limit: each entry takes several hundred bytes of request memory, plus twice the length of its key
(about 800 MB for a million short keys). Ignored with `key`.

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
- `total_scavenged_count` - the entries that steps and full passes removed since the cache was created, evicted
  entries included
- `total_evicted_count` - the entries evicted from a full cache since the cache was created
- `ucache_entries` - an array, indexed from 1 (from `offset` + 1 without `key`: the position of the entry in the
  full listing), that contains the information about the cached entries:
  - `key_name` - name of the key which is used to store the data
  - `value_type` - type of value stored by the key (`long`, `double`, `string`, `bool`, `null`, `array` or
    `object`)
  - `value_size` - memory taken by the value, in bytes
  - `ttl_seconds` - the ttl the entry was stored with, in seconds, 0 meaning that it never expires
  - `age_seconds` - time elapsed from the time data has been added in the cache
  - `hitcount` - number of times data has been served from the cache

With `summaryonly` set to true, `ucache_entries` is empty. `total_item_count` always counts the whole cache, whatever
`offset` and `limit` select. `ucache_info()` removes nothing, does not count as a use of the entries, and also works
with `ucache.enabled=0`: its full listing shows the expired entries that nothing has removed yet (see
[The Scavenger](#the-scavenger)).

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

On PHP 7.2 to 8.1, the `value_size` of `blue` is 160: the elements of a packed array take 32 bytes there, 16 since
PHP 8.2.

**Example #2 `ucache_info()` for one key**

```php
<?php
ucache_set('green', 1);
print_r(ucache_info(false, 'green'));
?>
```

The above example will output:

```
Array
(
    [total_cache_uptime] => 0
    [is_local_cache] => 
    [total_item_count] => 1
    [total_hit_count] => 0
    [total_miss_count] => 0
    [last_scavenge_seconds] => 
    [last_scavenge_count] => 0
    [total_scavenged_count] => 0
    [total_evicted_count] => 0
    [ucache_entries] => Array
        (
            [1] => Array
                (
                    [key_name] => green
                    [value_type] => long
                    [value_size] => 16
                    [ttl_seconds] => 0
                    [age_seconds] => 0
                    [hitcount] => 0
                )

        )

)
```

**Example #3 Listing the entries a page at a time**

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

Retrieves information about the memory usage of the user cache, which is one shared memory segment of
`ucache.size` megabytes.

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
variable remains in the user cache unless its time to live expires or it is deleted by using the `ucache_delete()`
or `ucache_clear()` functions.

#### Parameters

`key`

Store the variable using this key name. If a variable with the same key is already present, the function overwrites
the previous value with the new one. `key` is case sensitive (see [Keys](#keys)).

`value`

Value of a variable to store. Value supports all data types except resources, such as file handles. Objects are
serialized (see [Values](#values)). Called with the key alone, without a value, the function fails; with named
arguments (PHP 8), an omitted `value` is null and is stored: `ucache_set('k', ttl: 60)` stores null, like
`ucache_set('k', null, 60)`.

`values`

Associative array of keys and values, which stores all the pairs in one call.

`unused`

Not used in the array form. A resource there still makes the call fail, with a warning.

`ttl`

Time for the variable to live in the cache in seconds. After the value specified in `ttl` has passed, the stored
variable can no longer be read, and the scavenger frees it (see [The Scavenger](#the-scavenger)). The default value
of 0 means that the variable never expires. A negative `ttl` makes the call fail. A `ttl` above 4294967295 counts as
4294967295, and a variable with a `ttl` above 4294967 (about 49.7 days) never expires: the age of an entry is
measured with the millisecond tick count of Windows, a 32-bit number.

#### Return Values

If `key` is a string, the function returns true on success and false on failure: the key is blank or longer than
4096 characters, no value was given, the value is or holds a resource, is nested too deeply or does not fit in the
cache, its serialized objects would pass memory_limit, or `ttl` is negative.

If `key` is an array, the function returns an empty array if all the name => value pairs were stored, or else an
array of the names that could not be stored, each with the value -1. An element given by reference is stored as its
value.

#### Errors/Exceptions

Raises an `E_WARNING` for a blank key ("key cannot be blank string"), a negative ttl ("ttl cannot be less than 0")
and a resource given as the value ("value cannot be a resource"). A key longer than 4096 characters, a resource
inside an array value, or a value nested too deeply, fails without a warning.

If serializing an object of the value throws (a Closure, an object of an anonymous class, a Generator, a `__sleep()`
or `__serialize()` that throws), the exception propagates and the value is not stored. In the array form, the pairs
before that element are stored, that one and the ones after it are not.

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

### Prerequisites

- Visual Studio 2022 with MSBuild and the C++ toolset of each PHP version to build for, which the Visual Studio
  Installer adds as individual components:
  - 7.2, 7.3 and 7.4: "MSVC v141 - VS 2017 C++ x64/x86 build tools"
  - 8.0, 8.1, 8.2 and 8.3: "MSVC v142 - VS 2019 C++ x64/x86 build tools"
  - 8.4 and 8.5: "MSVC v143 - VS 2022 C++ x64/x86 build tools", the default toolset of Visual Studio 2022

  Each DLL is linked with the toolset of its PHP binaries because, since PHP 7.4, PHP refuses to load a module linked
  with a newer linker than its own ("Can't load module ... as it's linked with 14.44, but the core is linked with
  14.29"). It compares the tens of the minor version only, so older patch releases, linked with 14.28 (8.0.0) or
  14.41 (8.4.0), load the DLL of their version too. In the releases of the table below, the core is linked with 14.16
  (v141) in 7.4, 14.29 (v142) in 8.0 to 8.3 and 14.44 (v143) in 8.4 and 8.5; 7.2 and 7.3 do not check.
- A Windows 10 or 11 SDK.
- `curl` and `tar`, which come with Windows 10 1803 and later, to download and unpack the devel pack.

### Compiling and building

```
build.bat                      show the usage
build.bat <version>            build for PHP 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4 or 8.5 and test
build.bat <version> install    build and test, then copy the DLL into %PHP_DIR%\ext if the tests passed
```

There is no thread-safe build: UCache does not support ZTS. The table in `:set_php_version` at the end of
`build.bat` gives each version its devel pack, toolset and `PHP_DIR`:

| Version | Devel pack | Import library | Toolset | `PHP_DIR` |
|---|---|---|---|---|
| 7.2 | `php-devel-pack-7.2.34-nts-Win32-VC15-x64` | `php7.lib` | v141 | `c:\php72` |
| 7.3 | `php-devel-pack-7.3.33-nts-Win32-VC15-x64` | `php7.lib` | v141 | `c:\php73` |
| 7.4 | `php-devel-pack-7.4.33-nts-Win32-vc15-x64` | `php7.lib` | v141 | `c:\php74` |
| 8.0 | `php-devel-pack-8.0.30-nts-Win32-vs16-x64` | `php8.lib` | v142 | `c:\php80` |
| 8.1 | `php-devel-pack-8.1.34-nts-Win32-vs16-x64` | `php8.lib` | v142 | `c:\php81` |
| 8.2 | `php-devel-pack-8.2.35-nts-Win32-vs16-x64` | `php8.lib` | v142 | `c:\php82` |
| 8.3 | `php-devel-pack-8.3.36-nts-Win32-vs16-x64` | `php8.lib` | v142 | `c:\php83` |
| 8.4 | `php-devel-pack-8.4.27-nts-Win32-vs17-x64` | `php8.lib` | v143 | `c:\php84` |
| 8.5 | `php-devel-pack-8.5.12-nts-Win32-vs17-x64` | `php8.lib` | v143 | `c:\php85` |

`build.bat` looks for the devel pack in `PHP_DEVEL_ROOT` (an environment variable, default `D:\Dev\php-devel`), one
folder per pack. A missing pack is downloaded with `curl` (the current releases, then the archive), unpacked with
`tar` and checked (it must have `include\main\php_version.h` and the import library); the zip is then deleted. If
the download or the unpacking fails, the zip and the partly unpacked folder are deleted and the message says why: a
pack that windows.php.net does not have (HTTP 404), another `curl` error, or a damaged zip.

It then rewrites `php_ucache.props` with the pack's include and lib paths and import library, and builds
`bin\Release\<version>\php<major><minor>\php_ucache.dll`, where `<version>` is the name of the current git branch (the
branches are named after the versions, `1.0.0.2` for example), or else the version in `precomp.h`, and
`php<major><minor>` is `php72`, `php73`, `php74`, `php80`, `php81`, `php82`, `php83`, `php84` or `php85`, so that the
builds of different versions do not overwrite each other, with the version's toolset and the newest Windows SDK it finds
(in `%ProgramFiles(x86)%\Windows Kits\10`, or else in the folder of the registry value `KitsRoot10`), without PDB files;
the `obj` folder is deleted. If the build fails and vswhere finds no Visual Studio with that toolset, `build.bat` names
the component to install. Do not retarget the project to another toolset, as MSBuild's error MSB8020 suggests: on PHP
7.4 and later, the DLL would not load.

The DLL is tested before anything is installed (see [Verifying the build](#verifying-the-build)). With `install`, the
DLL is copied into `%PHP_DIR%\ext` only if both test runs passed, and the version's folder is then deleted
(`bin\Release\<version>`, `bin\Release` and `bin` too, once they are empty; the other builds stay). When `PHP_DIR` has
no `php.exe`, the test is skipped, and `install` fails without installing anything. The exit code is 1 on any failure, a
failed or crashed test included, and 0 otherwise. `MSBUILD` at the top of `build.bat` is the path of MSBuild.exe. The
paths (`PHP_DIR`, `PHP_DEVEL_ROOT`, the folder of the sources) may contain spaces and parentheses.

Without `build.bat`, point `php_ucache.props` at the devel pack, or pass the pack on the command line with
`"/p:PhpSdkRoot=<pack>\include" "/p:PhpLibName=<pack>\lib\php8.lib"` (`php7.lib` for 7.2 to 7.4), which take
precedence over the file, and build with the toolset of the version:

```
MSBuild php_ucache.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v142
MSBuild php_ucache.vcxproj /p:Configuration=Release /p:Platform=x64
MSBuild php_ucache.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v141 /p:WindowsTargetPlatformVersion=10.0.26100.0
```

The first command is for 8.0 to 8.3, which refuse a DLL linked with the project's default toolset, v143. The second
(v143) is for 8.4 and 8.5, and also works for 7.2 and 7.3, which do not check the linker (7.4 refuses a v143 or v142
DLL). The third builds 7.2 to 7.4 with their own toolset, v141, which does not understand the project's
`WindowsTargetPlatformVersion` of `10.0` (the newest SDK) and fails with MSB8036: give it the version of an installed
Windows 10/11 SDK, a folder name in `%ProgramFiles(x86)%\Windows Kits\10\Include`. Building in Visual Studio uses
v143 as well.

In a PHP source tree built with the PHP SDK, `config.w32` adds the `--enable-ucache` configure option (NTS builds
only).

### Verifying the build

`build.bat` runs `%PHP_DIR%\php.exe -n -d extension=<dll> -d ucache.enablecli=1 test-build.php <dll>`, then the same
with opcache (`-d zend_extension=%PHP_DIR%\ext\php_opcache.dll -d opcache.enable_cli=1`; only
`-d opcache.enable_cli=1` on PHP 8.5, where opcache is built in; skipped if there is no `php_opcache.dll`).

`test-build.php` checks the exported functions and settings, exercises every function, and checks the copying of
values into shared memory and back (nested, packed and sparse arrays, references, objects and their serialization
methods, the enums, readonly properties and weak references of the versions that have them), request and shared
memory leaks, argument errors, the scavenger, named arguments (PHP 8), typed `$success` properties (PHP 7.4 and
later) and deep values in a Fiber (PHP 8.1 and later). It prints one `OK`/`FAIL` line per check (`SKIP` for a section
it cannot run) and exits with 1 if a check failed. It clears the cache it runs against, and needs:

- `-n` (no php.ini), so that it can check the defaults of the `ucache.*` settings; settings given with `-d` are not
  checked, so `-d ucache.apppoolid=...` or `-d ucache.namesalt=...` can keep it away from another cache;
- `ucache.enablecli=1`, without which the CLI has no cache;
- a cache that no other process uses while it runs, since it checks hit and miss counts and free memory.

With the path of the DLL under test as its argument, as `build.bat` passes it, it also starts other PHP processes
that load that DLL, each group on a cache of its own (a unique `ucache.apppoolid`): a holder keeps a cache alive
while other processes use it, and `php -S` servers on 127.0.0.1 stand for the FastCGI processes of a pool, which
survive the fatal error of a request. They check what a single process cannot, such as a cache shared by several
processes, the release of the cache lock after a fatal error and the scavenger at the end of requests. They need a
writable temporary folder and free local ports, and every process they start has a timeout. Without the argument
these checks are skipped, with one `SKIP` line. The second run, with opcache, goes through the immutable arrays of
opcache, which take other paths through the copy code.

### Debug builds

Builds compiled with `UCACHE_DEBUG` (which defines `UCACHE_TEST`) also have `ucache_lasterror()`, the last internal
error code of the user cache in this process, and `ucache_runtests()`, which runs the internal self tests (results go
to the debug trace).

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
| `wincache_ucache_lasterror()`, `wincache_runtests()` (debug builds) | `ucache_lasterror()`, `ucache_runtests()` (debug builds) |
| `wincache.ucenabled` | `ucache.enabled` |
| `wincache.ucachesize` | `ucache.size` |
| `wincache.enablecli` | `ucache.enablecli` |
| `wincache.namesalt` | `ucache.namesalt` |
| `wincache.apppoolid` | `ucache.apppoolid` |
| `wincache.filemapdir` | `ucache.filemapdir` |
| `wincache.debuglevel` | `ucache.debuglevel` |
| `wincache.php` (statistics page) | `ucache.php` (see [UCache Statistics Script](#ucache-statistics-script)) |

Not carried over: the file and resolve-path caches (`wincache.fcenabled`, `fcachesize`, `maxfilesize`, `filecount`,
`chkinterval`, `ttlmax`, `ignorelist`, `fcenabledfilter`, `fcndetect`, `wincache_fcache_*()`, `wincache_rplist_*()`,
`wincache_fcnotify_*()`, `wincache_refresh_if_changed()`), function rerouting (`wincache.reroute_enabled`), the
session handler (`session.save_handler = wincache`, `wincache.scachesize`, `wincache_scache_*()`),
`wincache_lock()` / `wincache_unlock()`, and the debugging setting `wincache.localheap`, with which every value went
to the process heap and could never be read back.

UCache does not see data stored by WinCache; it starts with an empty cache.

Code that defines `ucache_*` functions of its own (a wrapper around `wincache_ucache_*()`, for example) can keep
them: set `ucache.prefix` (see [Runtime Configuration](#runtime-configuration)) to a prefix of their own.

### Differences from WinCache 2.0.0.8

Several of these change what existing code sees.

- UCache supports PHP 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4 and 8.5, each with a DLL of its own (see
  [Building for Windows](#building-for-windows)), and on PHP 8 named arguments work. WinCache 2.0.0.8 supported
  PHP 7.0, 7.1 and 7.2 only (its sources do not compile for PHP 7.3 and later); UCache does not support 7.0 and 7.1.
- The scavenger works differently (see [The Scavenger](#the-scavenger)). Its steps also run at the end of requests,
  sized to the cache, and apply the same expiry rule as reads (the time since the entry was stored); calls that look
  up a key remove the expired entries they pass; and a `ucache_set()` or `ucache_add()` that finds the cache full
  first runs a full pass and, with `ucache.gc.evict`, evicts the entries not used for the longest time. WinCache
  2.0.0.8 ran a step of 16 hash buckets only inside writes (set, add, delete, inc, dec, cas), at most every 10
  seconds, so a cache that was only read was never scavenged; it removed an entry once its ttl had passed since it
  was last read or stored, up to a second before it expired or up to another ttl after; calls left expired entries in
  place, so storing an expired key again left two entries, and deleting one left it in the cache; and a full cache
  made writes fail. The `ucache.gc.*` settings and the scavenger keys of `ucache_info()` are new.
- Integer keys in the array forms of `ucache_get()`, `ucache_set()` and `ucache_add()` use their full decimal string.
  WinCache 2.0.0.8 cut them to 32 bits, so `4294967296` was stored as `0` and `PHP_INT_MAX` as `-1`.
- `ucache_get()` with an array of keys returns numeric keys as integer keys. WinCache 2.0.0.8 returned them as string
  keys that neither `$result[5]` nor `$result['5']` could read.
- `ucache_delete()` with an array of keys leaves the caller's array as it is. WinCache 2.0.0.8 turned its integer
  elements into strings in place, which could crash a FastCGI worker running opcache.
- `ucache_get()` and `ucache_delete()` read the elements of a key array that are references (as a `foreach` by
  reference leaves them) as their values. WinCache 2.0.0.8 raised its "key array elements can only be string or long"
  warning and returned false.
- Deleting, replacing or scavenging an entry does not take time in proportion to the number of entries: deleting every
  second one of 40,000 entries takes about 0.1 second. WinCache 2.0.0.8 walked the blocks that follow a freed
  block in shared memory to find the next free one, which took time in proportion to the number of entries: deleting
  every second one of 10,000 entries took 0.8 seconds, of 100,000 entries minutes, with every process of the pool
  waiting for the allocator meanwhile.
- A `ttl` is taken as given up to 4294967295 seconds, and as 4294967295 above. WinCache 2.0.0.8 cut it to 32 bits:
  2147483648 to 4294967295 were refused as negative ttls, 4294967296 meant no expiry and 4294967297 one second.
- `$GLOBALS` can be stored, as a value or with the array forms (see [Values](#values)). WinCache 2.0.0.8 stored none
  of it on PHP 7, whose `$GLOBALS` holds its variables through slots that it did not follow.
- `ucache.size` is used as written. WinCache 2.0.0.8 stored `wincache.ucachesize` as an 8-byte value into a 4-byte
  field, so sizes were read as 32-bit unsigned numbers (a negative size meant 930 MB, 4 GB meant 5 MB). Sizes
  outside 5..930 MB are clamped to the nearest limit.
- Reading an array with `ucache_get()` does not leak request memory. WinCache 2.0.0.8 leaked about 1.2 KB per read,
  until the end of the request.
- A reference that only the fetched value holds has reference count 1, as in any PHP array where no variable shares
  it: `var_dump()` shows no `&`, `serialize()` writes it as a plain value, and a copy of the fetched array does not
  share it (writing through the copy does not change the original). WinCache 2.0.0.8 counted extra references, as if
  another variable still shared it.
- An object that refers to itself through a reference (`$o->self = &$o`) reads back as a value, as `unserialize()`
  returns it. WinCache 2.0.0.8 returned it as a reference shared with that property: a copy of the array holding it
  shared that reference, and writing to the element also changed the object's property.
- On PHP 7.2, an array read back has PHP's recursion protection, also when it was stored from an array that opcache
  made immutable (a literal, a constant): `var_dump()`, `print_r()`, `var_export()`, `json_encode()`, `count()` and
  `==` detect a recursion built in it. WinCache 2.0.0.8 kept the flags of the stored array, without that protection,
  and these functions overflowed the C stack and ended the process on such a recursion.
- `ucache_get()` returns false, and `success` is false, when an object of the value cannot be unserialized (see
  [Values](#values)), as `unserialize()` returns false; the array form leaves that key out. WinCache 2.0.0.8 returned
  null in the object's place and set `success` to true.
- Shared memory is not lost by values given by reference to the array forms of `ucache_set()` and `ucache_add()`, by
  `ucache_set()` calls that fail, or by a fatal error during `ucache_set()` (raised in `__sleep()`, or by
  max_execution_time or memory_limit). In WinCache 2.0.0.8 that memory belonged to no entry, so neither a delete nor
  a clear could give it back, and it stayed lost until every process of the pool had exited.
- A value whose serialization throws (a Closure, an anonymous class, a Generator, a throwing `__sleep()`) is not
  stored, and the array form stops at that element: the elements after it are not stored. WinCache 2.0.0.8 let the
  exception propagate too, but stored a broken entry (`wincache_ucache_exists()` true, the value null or cut short).
- A value whose arrays and references are nested more than 10,000 levels deep is not stored: `ucache_set()` and
  `ucache_add()` return false (see [Values](#values)). On PHP 8.1 and later, neither is a value nested too deeply for
  the C stack that is left (in a Fiber, for example), and reading one returns false. On very deep values, WinCache
  2.0.0.8 overflowed the C stack and crashed the process, and the shared memory of the half-copied entry was lost for
  every process of the pool.
- In the array forms of `ucache_set()` and `ucache_add()`, a value given by reference is stored as its value.
  WinCache 2.0.0.8 stored it as a reference, which came back sharing data with the other such values and which
  `wincache_ucache_inc()` refused.
- `$success` (`ucache_get()`, `ucache_inc()`, `ucache_dec()`): the value that the variable held before is released;
  WinCache 2.0.0.8 leaked a string or an array there. On PHP 7.4 and later, a typed property gets the bool under its
  type rules: converted, or a `TypeError`.
- Arguments are checked before the state of the cache: with `ucache.enabled=0`, or a cache that was not created,
  invalid arguments throw `ArgumentCountError` or `TypeError`, or raise a warning on PHP 7 without `strict_types`.
  WinCache 2.0.0.8 returned false silently.
- `ucache_clear()` and `ucache_meminfo()` called with an argument throw `ArgumentCountError`, or raise a warning on
  PHP 7 without `strict_types`. In WinCache 2.0.0.8, `wincache_ucache_clear(1)` returned false silently, and
  `wincache_ucache_meminfo(1)` ignored the argument.
- Reflection shows one required parameter for `ucache_set()` and `ucache_add()`, two for WinCache 2.0.0.8.
- The prefix of the function names is a setting, `ucache.prefix`. WinCache 2.0.0.8's names were fixed
  (`wincache_ucache_*`).
- `ini_get('ucache.namesalt')`, `phpinfo()` and `ucache.php` show the whole value of the setting, although only its
  first 8 characters are used. WinCache 2.0.0.8 cut the setting itself to those 8 characters.
- Objects are unserialized after the cache lock has been released, and values are copied in without holding it, so
  `__sleep()`, `__serialize()`, `__wakeup()`, `__unserialize()`, `Serializable::serialize()` and
  `Serializable::unserialize()` do not run under the lock, and readers do not wait while a value is copied in.
  WinCache 2.0.0.8 did both under the lock. The cost: with several processes writing at once, aggregate
  `ucache_set()` throughput is lower than with WinCache 2.0.0.8 (about 2 to 2.7 times lower with 2 to 6 writers on
  PHP 7.2), because concurrent copies take turns on the allocator's lock for each allocation. A single writer is as
  fast.
- The UCache functions work inside `Serializable::serialize()` and `Serializable::unserialize()`: UCache serializes
  and unserializes each object independently of the caller's `serialize()` or `unserialize()` (see
  [Serialization and the cache lock](#serialization-and-the-cache-lock)). WinCache 2.0.0.8 joined PHP's serializer
  context there, which corrupted both the nested value and the enclosing object. Inside `__sleep()` and
  `__wakeup()`, a value holding a `Serializable` object that refers to itself recursed until memory_limit (when
  stored) or lost the self-reference (when read).
- A fatal error during a cache call (for example memory_limit while `ucache_get()` copies a big value) does not leave
  the cross-process lock held. In WinCache 2.0.0.8 it did, which blocked every other process of the pool until the
  failing one exited.
- Deleting, replacing, clearing or expiring a large entry takes time linear in its size, and its memory is freed
  after the cache lock has been released (see [Freeing memory](#freeing-memory)). WinCache 2.0.0.8 took time
  quadratic in its size, while the cache lock was held: for an entry of 100,000 small arrays it took 14-21 s, and
  every other process waited.
- Changes that `__sleep()` or `__serialize()` make to a value while `ucache_set()` stores it are not stored: the entry
  holds the value as it was when `ucache_set()` was called, or the call fails. WinCache 2.0.0.8 could store an entry
  made of freed or unrelated data, which crashed or broke every process that read it.
- `__sleep()` and `__serialize()` are called once per distinct object that the value holds in its arrays and
  references; WinCache 2.0.0.8 called `__sleep()` once per occurrence. An object that another object of the value
  holds is still serialized again as part of that object. `__wakeup()` and `__unserialize()` still run once per
  occurrence.
- `ucache_set()` and `ucache_add()` serialize every object of a value before they copy anything into the cache, and
  keep the serializations until the value is copied; WinCache 2.0.0.8 serialized one object at a time. UCache counts
  them against memory_limit, so close to the limit a value with many objects may not be stored: the call then
  returns false, without an error (see [memory_limit](#memory_limit)).
- `value_size` (`ucache_info()`) and the memory an entry uses differ slightly from WinCache 2.0.0.8: sparse hash
  arrays keep a larger hash part, and an object that occurs more than once is stored once.
- `ucache_info()` takes `offset` and `limit`, which list a part of the entries without copying the others, and a null
  `key` means no key (see [ucache_info](#ucache_info)). WinCache 2.0.0.8's `wincache_ucache_info()` always copied
  every entry into the array it returned, so the listing of a cache of many entries ran past memory_limit, and it
  took a null `key` as the empty key, listing no entry.
- `ucache.php` escapes everything it prints, its Clear All button is a token-protected form instead of a link, and it
  works on PHP 8 (the hit/miss chart of `wincache.php` called `each()`, which PHP 8 removed). It lists the entries 250
  per page and copies only those; `wincache.php` copied every entry even to show the first 250 (and showed all of
  them on one page with its Show all entries link), so on a cache of many entries it failed with the memory_limit
  error.
