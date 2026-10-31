<p align="center">
    <img style="height:80px" src="ucache-logo.svg">
</p>

<p align="center">
  <img src="https://img.shields.io/github/issues/pkoutsogiannis/ucache" alt="Issues">
  <img src="https://img.shields.io/github/languages/top/pkoutsogiannis/ucache" alt="Language">
  <img src="https://img.shields.io/github/stars/pkoutsogiannis/ucache" alt="Stars">
  <img src="https://img.shields.io/github/forks/pkoutsogiannis/ucache" alt="Forks">
</p>

ucache is the user cache of WinCache (Microsoft's Windows Cache Extension for PHP, 2.0.0.8), extracted into a
standalone PHP extension and renamed. It keeps PHP values in a shared memory segment, so all PHP processes of the
same IIS application pool, or more generally with the same parent process (for example the php-cgi.exe processes
started by one FastCGI host), use the same cache.

Only the user cache is left. The file cache, resolve-path cache, file change notification, session handler,
`wincache_lock()`/`wincache_unlock()`, PHP function rerouting and ETW tracing have been removed. The functions take
the same arguments and return the same values as the original `wincache_ucache_*` functions; only the names changed
(see [Migrating from WinCache](#migrating-from-wincache)).

- Platform: Windows x64, PHP 7.2 NTS.
- Extension name `ucache`, file `php_ucache.dll`, version 1.0.0.0.
- License: see [LICENSE](LICENSE) (the original Microsoft license).

## Behaviour

- Values are copied into shared memory on write and copied back on read. Scalars, strings, null and arrays
  (including nested arrays) are stored as they are; objects are serialized on write and unserialized on read;
  resources cannot be stored.
- Keys are strings; other scalar keys are converted to strings. A key ends at its first NUL byte (`"a\0b"` and
  `"a\0c"` are both the key `"a"`). `ucache_set()` and `ucache_add()` store keys of at most 4096 characters, and
  reject an empty key only in their single-key form: the array form does not check, so `''` can be stored that way
  (and then read with `ucache_get('')`).
- An entry stored with a ttl expires `ttl` seconds after it was stored; reads do not extend it. Expired entries can
  no longer be read, but they keep their memory, and still appear in the full `ucache_info()` listing, until the
  scavenger removes them: every write (set, add, delete, inc, dec, cas) runs a scavenger step at most every 10
  seconds, which looks at 16 hash buckets and removes their entries whose ttl has passed since they were last used.
- Processes share a cache when they have the same parent process id, or the same `ucache.apppoolid` (default: the
  `APP_POOL_ID` environment variable set by IIS). There is no private per-process cache: `ucache.localheap` is a
  debugging setting that makes the cache unusable (see [Settings](#settings)). The cache is not persistent: it starts
  empty when the first process of a group creates it.
- With `ucache.enabled=0` all functions except `ucache_info()` and `ucache_meminfo()` return false. The cache is
  still created at startup, so a script can switch it back on with `ini_set()`.

## Functions

Argument names are the ones in the extension's arginfo, except `$values` and `$unused` in the array forms of
`ucache_set()` and `ucache_add()`, which arginfo calls `$key` and `$value`.

```
mixed       ucache_get(mixed $key [, bool &$success])
bool|array  ucache_set(mixed $key, mixed $value [, int $ttl = 0])
bool|array  ucache_set(array $values [, mixed $unused = null [, int $ttl = 0]])
bool|array  ucache_add(mixed $key, mixed $value [, int $ttl = 0])
bool|array  ucache_add(array $values [, mixed $unused = null [, int $ttl = 0]])
bool|array  ucache_delete(mixed $key)
bool        ucache_clear()
bool        ucache_exists(string $key)
array|false ucache_info([bool $summaryonly = false [, string $key]])
array|null  ucache_meminfo()
int|false   ucache_inc(string $key [, int $delta = 1 [, bool &$success]])
int|false   ucache_dec(string $key [, int $delta = 1 [, bool &$success]])
bool        ucache_cas(string $key, int $cvalue, int $nvalue)
```

- `ucache_get($key, &$success)`: returns the stored value, or false if the key is missing or expired. With an array
  of keys (strings or ints), returns an array `key => value` of the keys that were found; if an element is neither a
  string nor an int, it returns false with a warning instead. `$success` is set to true when the value was found and
  to false otherwise; with an array of keys it is true unless the call failed because of such an element. Use it to
  tell a stored `false` from a miss. When the cache is switched off (`ucache.enabled=0`) or was not created (CLI
  without `ucache.enablecli=1`), the function returns false and leaves `$success` unchanged.
- `ucache_set($key, $value, $ttl)`: stores the value, replacing any existing one. `$ttl` is in seconds; 0 means no
  expiry. Returns true, or false on failure (blank key, key over 4096 characters, no value, resource value, negative
  ttl, out of memory). With an array `key => value`, stores every pair and returns an array of the keys that could
  not be stored, each with the value -1 (an empty array when all were stored); the second argument is not used in
  this form, but a resource there still makes the call return false with a warning.
- `ucache_add(...)`: like `ucache_set()`, but never replaces an entry: returns false, with a warning, if the key
  exists. In the array form, existing keys are returned among the failures (-1), without a warning.
- `ucache_delete($key)`: returns true if the entry was deleted, false if it did not exist. With an array of keys,
  returns a list of the keys that were deleted (int keys as strings), but false if the last key of the array was not
  deleted, whatever happened to the others. The array passed in is not modified.
- `ucache_clear()`: deletes every entry and returns true.
- `ucache_exists($key)`: true if the key exists and has not expired.
- `ucache_inc($key, $delta, &$success)`, `ucache_dec(...)`: add `$delta` (default 1) to, or subtract it from, an
  integer value in place and return the new value. Return false if the key does not exist, and false with a warning
  if the value is not an integer. `$success` tells whether it worked; like `ucache_get()`, they leave it unchanged
  when the cache is switched off or was not created. The entry's ttl is not restarted.
- `ucache_cas($key, $cvalue, $nvalue)`: if the integer stored under `$key` equals `$cvalue`, replaces it with
  `$nvalue` and returns true; otherwise returns false (with a warning if the value is not an integer).
- `ucache_info($summaryonly, $key)`: returns an array with
  - `total_cache_uptime`: seconds since the cache was created
  - `is_local_cache`: always false (the user cache is always the shared one)
  - `total_item_count`, `total_hit_count`, `total_miss_count`
  - `ucache_entries`: a list, indexed from 1, of arrays with `key_name`, `value_type`, `value_size` (bytes),
    `ttl_seconds`, `age_seconds` and `hitcount`

  With `$summaryonly = true`, `ucache_entries` is empty. With `$key`, `ucache_entries` holds only that entry (nothing
  if it is missing or expired), whatever `$summaryonly` is.
- `ucache_meminfo()`: returns `memory_total`, `memory_free`, `num_used_blks`, `num_free_blks` and `memory_overhead`
  for the shared memory segment.

Builds compiled with `UCACHE_DEBUG` (which defines `UCACHE_TEST`) also have `ucache_lasterror()`, the last internal
error code of the user cache in this process, and `ucache_runtests()`, which runs the internal self tests (results go
to the debug trace).

## Settings

| Setting | Default | Changeable | Meaning |
|---|---|---|---|
| `ucache.enabled` | 1 | PHP_INI_ALL | Turns the cache functions on or off (see [Behaviour](#behaviour)). |
| `ucache.enablecli` | 0 | PHP_INI_SYSTEM | Create the cache under the CLI. Without it the functions fail under the CLI. |
| `ucache.size` | 8 | PHP_INI_SYSTEM | Size of the shared memory segment in MB, limited to 5..930. |
| `ucache.namesalt` | (none) | PHP_INI_SYSTEM | Up to 8 characters (longer values are cut) added to the names of the shared memory and synchronization objects, to keep apart caches that would otherwise be shared. |
| `ucache.localheap` | 0 | PHP_INI_SYSTEM | Debugging setting kept from WinCache; leave it at 0. With 1, values are allocated from the process heap instead of the shared memory segment and can never be found again: writes return true and count in `total_item_count`, but every read misses, and the memory is only given back when the process exits. It does not give a private cache. |
| `ucache.apppoolid` | (none) | PHP_INI_SYSTEM | Name that decides which processes share a cache, instead of the parent process id. If not set, the `APP_POOL_ID` environment variable is used when IIS sets it. |
| `ucache.filemapdir` | (none) | PHP_INI_SYSTEM | Directory for a file that backs the shared memory instead of the paging file. Only used when the process group id (the parent process id, or the hash of the app pool id) is above 99999. |
| `ucache.debuglevel` | 0 | PHP_INI_ALL | Debug trace written with OutputDebugString: 0 (off), 101, 201, 301, 401 or 501 (most detailed). Other values are ignored. |

## Statistics page

`ucache.php` is the WinCache statistics page (`wincache.php`) for ucache. Copy it into a web site that runs with the
extension loaded, preferably in a folder that only administrators can reach. It shows the server and cache settings,
the memory use, the hit and miss counts and the list of entries; with authentication on, it can also show the value
stored under a key and clear the whole cache.

Edit the configuration block at the top of the file before using it:

- `USE_AUTHENTICATION` (default 1) protects the page with HTTP Basic authentication against `USERNAME` and
  `PASSWORD`. Both default to `ucache`, and the page refuses to work while the password is `ucache`, so change it.
  With 0 there is no authentication, and the page no longer shows values or offers to clear the cache.
- Basic authentication through PHP only works when IIS uses anonymous authentication alone. When IIS authenticates
  the users itself, list the allowed accounts (`DOMAIN\user`) in `$user_allowed` instead, or put `'all'` in it to let
  in every authenticated user.

The charts need the GD extension (`php_gd2.dll`); without it the page shows a notice in their place.

## Building

Requirements: Visual Studio 2022 (MSBuild, v143 toolset, Windows 10 SDK) and the PHP 7.2 NTS x64 devel pack
(`php-devel-pack-7.2.34-nts-Win32-VC15-x64`).

1. Set `MSBUILD`, `DEVEL_DIR` and `PHP_DIR` at the top of `build.bat`.
2. Run `build.bat`, or `build.bat install` to also copy the DLL into `%PHP_DIR%\ext`. It rewrites `php_ucache.props`
   with the devel pack's include and lib paths, builds `bin\Release\php_ucache.dll`, and runs `test-build.php` with
   `php -n -d extension=... -d ucache.enablecli=1`. With `install`, the `bin` folder is deleted after the copy and the
   test runs against the DLL in `%PHP_DIR%\ext`.

Without `build.bat`, once `php_ucache.props` points at the devel pack:

```
MSBuild php_ucache.vcxproj /p:Configuration=Release /p:Platform=x64
```

In a PHP source tree built with the PHP SDK, `config.w32` adds the `--enable-ucache` configure option (NTS builds
only).

`test-build.php` is a smoke test: it checks the exported functions and settings and exercises every function. It
prints one `OK`/`FAIL` line per check and exits with 1 if a check failed. It clears the cache it runs against.

## Migrating from WinCache

| WinCache | ucache |
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
| `wincache_ucache_lasterror()`, `wincache_runtests()` (debug builds) | `ucache_lasterror()`, `ucache_runtests()` |
| `wincache.ucenabled` | `ucache.enabled` |
| `wincache.ucachesize` | `ucache.size` |
| `wincache.enablecli` | `ucache.enablecli` |
| `wincache.namesalt` | `ucache.namesalt` |
| `wincache.localheap` | `ucache.localheap` |
| `wincache.apppoolid` | `ucache.apppoolid` |
| `wincache.filemapdir` | `ucache.filemapdir` |
| `wincache.debuglevel` | `ucache.debuglevel` |
| `wincache.php` (statistics page) | `ucache.php` (see [Statistics page](#statistics-page)) |

Not carried over: the file and resolve-path caches (`wincache.fcenabled`, `fcachesize`, `maxfilesize`, `filecount`,
`chkinterval`, `ttlmax`, `ignorelist`, `fcenabledfilter`, `fcndetect`, `wincache_fcache_*()`, `wincache_rplist_*()`,
`wincache_refresh_if_changed()`), function rerouting (`wincache.reroute_enabled`), the session handler
(`session.save_handler = wincache`, `wincache.scachesize`, `wincache_scache_*()`), and `wincache_lock()` /
`wincache_unlock()`.

ucache does not see data stored by WinCache; it starts with an empty cache.
