<?php
/*
   +----------------------------------------------------------------------------------------------+
   | Windows Cache for PHP                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Copyright (c) 2009, Microsoft Corporation. All rights reserved.                              |
   | Copyright (c) 2018-2026, Datasteam Technologies. All rights reserved.                        |
   |                                                                                              |
   | Redistribution and use in source and binary forms, with or without modification, are         |
   | permitted provided that the following conditions are met:                                    |
   | - Redistributions of source code must retain the above copyright notice, this list of        |
   | conditions and the following disclaimer.                                                     |
   | - Redistributions in binary form must reproduce the above copyright notice, this list of     |
   | conditions and the following disclaimer in the documentation and/or other materials provided |
   | with the distribution.                                                                       |
   | - Neither the name of the Microsoft Corporation nor the names of its contributors may be     |
   | used to endorse or promote products derived from this software without specific prior written|
   | permission.                                                                                  |
   |                                                                                              |
   | THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS  |
   | OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF              |
   | MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE   |
   | COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,    |
   | EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE|
   | GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED   |
   | AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING    |
   | NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED |
   | OF THE POSSIBILITY OF SUCH DAMAGE.                                                           |
   +----------------------------------------------------------------------------------------------+
   | Module: ucache.php                                                                           |
   +----------------------------------------------------------------------------------------------+
   | Authors: Don Venkat Raman <don.raman@microsoft.com>                                          |
   |          Ruslan Yakushev <ruslany@microsoft.com>                                             |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

/**
 * ======================== CONFIGURATION SETTINGS ==============================
 * If you do not want to use authentication for this page, set USE_AUTHENTICATION to 0.
 * If you use authentication then replace the default username and password.
 * With IIS authentication (see below) nobody types them, but PASSWORD is still the
 * only secret behind the Clear All button: make it a long random value (32 hex
 * characters, for example), and change USERNAME too, because requests that reach
 * this page without IIS authentication can still log in with the two.
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

if (!extension_loaded('ucache')) {
    die('The UCache extension (php_ucache.dll) is not loaded. No statistics to show.');
}

// The functions are named after ucache.prefix ("ucache_" by default): uc('info', true) calls ucache_info(true)
function uc($name, ...$args) {
    return (ini_get('ucache.prefix') . $name)(...$args);
}

if (!function_exists(ini_get('ucache.prefix') . 'info')) {
    die('The UCache functions are not available (see the startup warning about ucache.prefix). No statistics to show.');
}

// On unless USE_AUTHENTICATION is 0, as in every test of it below: a value such as 2 must not skip the authentication
// and still offer Clear All
if (USE_AUTHENTICATION) {
    if (!empty($_SERVER['AUTH_TYPE']) && !empty($_SERVER['REMOTE_USER']) && strcasecmp($_SERVER['REMOTE_USER'], 'anonymous')) {
        // IIS authenticated the user: only the users in $user_allowed get in
        $allowed = array_map('strtolower', $user_allowed);
        if (!in_array(strtolower($_SERVER['REMOTE_USER']), $allowed, true) && !in_array('all', $allowed, true)) {
            echo 'You are not authorised to view this page. Please contact server admin to get permission. Exiting.';
            exit;
        }
    } elseif (
        !isset($_SERVER['PHP_AUTH_USER'], $_SERVER['PHP_AUTH_PW'])
        || !hash_equals((string)USERNAME, (string)$_SERVER['PHP_AUTH_USER'])
        || !hash_equals((string)PASSWORD, (string)$_SERVER['PHP_AUTH_PW'])
    ) {
        header('WWW-Authenticate: Basic realm="UCache statistics"');
        http_response_code(401);
        exit;
    } elseif ($_SERVER['PHP_AUTH_PW'] === 'ucache') {
        echo 'Please change the default password to get this page working. Exiting.';
        exit;
    }
}

define('SUMMARY_DATA', 1);
define('UCACHE_DATA', 4); // User cache
define('CACHE_MAX_ENTRY', 250);

// ucache settings that are used for debugging purposes
$settings_to_hide = ['ucache.debuglevel'];

// The groups of the Cache settings card, each in this order; a setting that none lists goes last in the first one
$setting_groups = [
    'Cache' => ['ucache.enabled', 'ucache.enablecli', 'ucache.size', 'ucache.bucketbytes', 'ucache.prefix', 'ucache.apppoolid', 'ucache.namesalt',
                'ucache.filemapdir'],
    'Scavenger' => ['ucache.gc.interval', 'ucache.gc.step', 'ucache.gc.ttl', 'ucache.gc.threshold', 'ucache.gc.evict'],
];

// Input parameters check
$PHP_SELF = isset($_SERVER['PHP_SELF']) ? htmlentities(strip_tags($_SERVER['PHP_SELF'], ''), ENT_QUOTES, 'UTF-8') : '';

$page = SUMMARY_DATA;
if (isset($_GET['page']) && is_numeric($_GET['page']) && $_GET['page'] == UCACHE_DATA) {
    $page = UCACHE_DATA;
}

// The list shows CACHE_MAX_ENTRY entries at a time, from position start (0 for the first entry)
$ucache_start = 0;
if (isset($_GET['start']) && is_string($_GET['start']) && ctype_digit($_GET['start'])) {
    // More digits than an int holds start past the last entry (PHP 8.5 warns when it casts such a string to int)
    $digits = ltrim($_GET['start'], '0');
    $ucache_start = (strlen($digits) > 18) ? PHP_INT_MAX : (int)$digits;
}

// Clearing the cache is a POST from the page's own form, never a link
$clear_user_cache = isset($_POST['clc']) && $_POST['clc'] === '1';

// The page of an entry shows its value
$ucache_key = null;
if ($page == UCACHE_DATA && isset($_GET['key']) && is_string($_GET['key']) && $_GET['key'] !== '') {
    $ucache_key = $_GET['key'];
}
// End of input parameters check

// The user cache is available when it is enabled and the extension could create it. When it could not (for
// example because ucache.filemapdir names a folder that does not exist), ucache_info() and ucache_meminfo() fail.
$ucache_info = uc('info', true);
$ucache_mem_info = uc('meminfo');
$user_cache_available = ini_bool(ini_get('ucache.enabled')) && is_array($ucache_info) && is_array($ucache_mem_info);

// The clear form must send back a token that the page makes from PASSWORD, the random value of this cookie and the
// path of this script. Another site can neither read the token nor make the browser send the cookie (SameSite=Strict).
// Another host of the same site can set the cookie, as SameSite does not apply between the hosts of a site, but it
// cannot make the token of the value it set without guessing the password, and a copy of this page elsewhere, with
// the same password, makes other tokens. So the page does not clear the cache with the default password or none,
// which anyone knows. With IIS authentication nobody types PASSWORD, but it is still the only secret behind Clear All:
// it must be a long random value, and USERNAME should change too, since requests that reach the page without IIS
// authentication can log in with the two (see the configuration settings).
define('TOKEN_COOKIE', 'ucache_stats_token');
$can_clear = USE_AUTHENTICATION && !in_array((string)PASSWORD, ['', 'ucache'], true);
$nonce = null;
if (isset($_COOKIE[TOKEN_COOKIE]) && is_string($_COOKIE[TOKEN_COOKIE]) && preg_match('/^[0-9a-f]{32}$/', $_COOKIE[TOKEN_COOKIE])) {
    $nonce = $_COOKIE[TOKEN_COOKIE];
}

// The token of the clear form for the value of the cookie
function clear_token($nonce) {
    return hash_hmac('sha256', 'clear ' . $nonce . __FILE__, (string)PASSWORD);
}

// Whether the browser says that the clear form comes from a page of another origin (another site, host or port). The
// token alone refuses such a form; this also refuses one with a token that leaked or was guessed. Sec-Fetch-Site (sent
// over HTTPS and to localhost) is same-origin for the page's own form, or none for a request the user made. Origin
// names the origin of the page that sent the form, whose host and port must be those of the Host header (so a reverse
// proxy must pass that header on), except "null", which names none: a page under Referrer-Policy: no-referrer sends it
// for its own forms.
function cross_origin_post() {
    if (isset($_SERVER['HTTP_SEC_FETCH_SITE']) && !in_array($_SERVER['HTTP_SEC_FETCH_SITE'], ['same-origin', 'none'], true)) {
        return true;
    }
    if (!isset($_SERVER['HTTP_ORIGIN']) || $_SERVER['HTTP_ORIGIN'] === 'null') {
        return false;
    }
    return !isset($_SERVER['HTTP_HOST']) || !preg_match('~^[a-z][a-z0-9+.-]*://([^/]+)$~i', $_SERVER['HTTP_ORIGIN'], $match)
        || strcasecmp($match[1], $_SERVER['HTTP_HOST']) !== 0;
}

if ($can_clear && $user_cache_available && $clear_user_cache) {
    if ($nonce === null || !isset($_POST['token']) || !is_string($_POST['token'])
        || !hash_equals(clear_token($nonce), $_POST['token']) || cross_origin_post()) {
        http_response_code(403);
        echo 'The request to clear the user cache could not be verified. Reload the page and try again.';
        exit;
    }
    uc('clear');
    // Relative to the page itself: $PHP_SELF is escaped for HTML, which would break a path with & or non-ASCII characters
    header('Location: ?page=' . UCACHE_DATA);
    exit;
}

if ($nonce === null) {
    $nonce = bin2hex(random_bytes(16));
    $secure = (!empty($_SERVER['HTTPS']) && strcasecmp($_SERVER['HTTPS'], 'off')) ? '; Secure' : '';
    header('Set-Cookie: ' . TOKEN_COOKIE . '=' . $nonce . '; HttpOnly; SameSite=Strict' . $secure, false);
}
// The page can clear the cache: do not let another site load it in a frame and trick a click on the button
header('X-Frame-Options: DENY');
header("Content-Security-Policy: frame-ancestors 'none'");
// Nor let the browser or a proxy keep a copy: it shows cached values, and its token, with the cookie value it was
// made for (which another host can plant), clears the cache until PASSWORD changes
header('Cache-Control: no-store');

// The entries of the page, listed with the totals so that they agree with the count above them. Only those are
// copied: listing all the entries of a large cache would run past memory_limit.
$ucache_entries = null;
$list_error = null;
if ($user_cache_available && $page == UCACHE_DATA && $ucache_key === null) {
    // A php_ucache.dll built before ucache_info() took offset and limit reports the same version: tell it by the
    // number of parameters
    if ((new ReflectionFunction(ini_get('ucache.prefix') . 'info'))->getNumberOfParameters() < 4) {
        $list_error = 'the loaded php_ucache.dll is an older build, whose ucache_info() takes no offset and limit. '
            . 'Install the php_ucache.dll of the current release.';
    } else {
        $list_info = uc('info', false, null, $ucache_start, CACHE_MAX_ENTRY);
        if (is_array($list_info)) {
            $ucache_info = $list_info;
            $ucache_entries = $list_info['ucache_entries'];
        }
    }
}

function html_escape($input) {
    // ENT_SUBSTITUTE: cached keys and values can be binary, and invalid UTF-8 would otherwise escape to ''
    return htmlspecialchars((string)$input, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function format_number($number) {
    return number_format((float)$number);
}

function format_percent($percent) {
    return number_format($percent, 1) . '%';
}

// At most 2 decimals, and none that are 0: 8 MB, 4.2 MB, 287.56 kB
function format_bytes($bytes) {
    $units = ['B', 'kB', 'MB', 'GB'];
    $value = max(0, $bytes);
    $power = 0;
    while ($value >= 1024 && $power < count($units) - 1) {
        $value /= 1024;
        $power++;
    }
    return rtrim(rtrim(number_format($value, 2, '.', ''), '0'), '.') . ' ' . $units[$power];
}

// The largest unit and the next one, unless it is 0: 1 d 1 h, 2 min 5 s, 45 s
function format_duration($seconds) {
    $seconds = max(0, (int)$seconds);
    $parts = [];
    foreach (['d' => 86400, 'h' => 3600, 'min' => 60, 's' => 1] as $unit => $size) {
        $count = intdiv($seconds, $size);
        $seconds %= $size;
        $started = !empty($parts);
        if ($count > 0) {
            $parts[] = $count . ' ' . $unit;
        }
        if ($started) {
            break;
        }
    }
    return $parts ? implode(' ', $parts) : '0 s';
}

function placeholder($text) {
    return '<span class="placeholder">' . html_escape($text) . '</span>';
}

function notice($html) {
    return '<p class="notice">' . $html . "</p>\n";
}

function cache_unavailable_notice() {
    // Since 1.0.0.2, the extension says why at startup; an older php_ucache.dll does not
    $why = version_compare((string)phpversion('ucache'), '1.0.0.2', '>=')
        ? 'the warning that PHP logged when it started says why: see the PHP error log, or the Windows Application '
            . 'event log (source <strong>PHP-' . html_escape(PHP_VERSION) . '</strong>)'
        : 'check the other <strong>ucache.*</strong> directives, such as <strong>ucache.filemapdir</strong>';
    return notice('The user cache is not available. Either it is disabled (enable it with the '
        . '<strong>ucache.enabled</strong> directive in <strong>php.ini</strong>), or it could not be created ('
        . $why . ').');
}

function server_value($name) {
    return isset($_SERVER[$name]) ? (string)$_SERVER[$name] : null;
}

function env_value($name) {
    $value = getenv($name);
    return ($value === false || $value === '') ? null : $value;
}

// A boolean setting as PHP reads it: "on", "yes" or "true" in any case, or a number other than 0. ini_get() returns the
// string as it was set, which can be "On" or "2" as well as "1", and "" for Off
function ini_bool($value) {
    $value = strtolower((string)$value);
    return $value === 'on' || $value === 'yes' || $value === 'true' || (int)$value !== 0;
}

// A row of a two-column table, with a placeholder for a value that is not set
function kv_row($label, $value, $missing = 'Not set') {
    $cell = ($value === null || $value === '') ? placeholder($missing) : html_escape($value);
    return '<tr><th scope="row">' . html_escape($label) . '</th><td>' . $cell . "</td></tr>\n";
}

// The heading row of a group of rows in a two-column table
function kv_group($title) {
    return '<tr class="group"><th colspan="2">' . html_escape($title) . "</th></tr>\n";
}

// The value of a ucache setting in the Cache settings card (HTML), with what it does under it where needed
function setting_cell($name, $value) {
    if ($name === 'ucache.enabled' || $name === 'ucache.enablecli') {
        return ini_bool($value) ? 'enabled' : 'disabled';
    }
    if ($value === '') {
        return placeholder('Not set');
    }
    $note = setting_note($name, $value);
    return html_escape($value) . ($note !== null ? '<span class="note">' . html_escape($note) . '</span>' : '');
}

// What the value of a ucache setting does, or null where the value says it all. Only plain numbers are explained:
// ini_get() returns the value as written, and "512M" or "-1" would be read differently by the extension. Except
// ucache.size while the cache exists: its note is the size of the cache itself, right whatever the value says and
// whichever build of php_ucache.dll read it (1.0.0.2 reads 64M as 64 MB, earlier builds as 930 MB).
function setting_note($name, $value) {
    $number = ctype_digit($value) ? (int)$value : null;
    switch ($name) {
        case 'ucache.prefix':
            return $value . 'get(), ' . $value . 'set(), …';
        case 'ucache.size':
            if (is_array($GLOBALS['ucache_mem_info'])) {
                $size = intdiv((int)$GLOBALS['ucache_mem_info']['memory_total'], 1048576);
                $note = format_number($size) . ' MB of shared memory';
                if ($number !== null && $size != $number) {
                    // The extension clamps the setting to 5..930: any other difference is a cache that processes with
                    // another ucache.size created, which keeps its size until every process of the pool has exited
                    $clamped = min(max($number, 5), 930);
                    $note .= ($size == $clamped) ? ($size > $number ? ' (the minimum)' : ' (the maximum)')
                        : ' (the size of the cache in use: a cache keeps the size it was created with until every process of the pool has exited)';
                }
                return $note;
            }
            break;
        case 'ucache.bucketbytes':
            // The table of the cache in use, whichever setting the process that created it had: a cache keeps its table,
            // as it keeps its size, until every process of the pool has exited
            if (is_array($GLOBALS['ucache_info']) && isset($GLOBALS['ucache_info']['hash_bucket_count'])
                && $GLOBALS['ucache_info']['hash_bucket_count'] > 0 && is_array($GLOBALS['ucache_mem_info'])) {
                $buckets = (int)$GLOBALS['ucache_info']['hash_bucket_count'];
                return format_number($buckets) . ' hash buckets, one per '
                    . format_number(intdiv((int)$GLOBALS['ucache_mem_info']['memory_total'], $buckets)) . ' bytes';
            }
            break;
    }
    if ($number === null) {
        return null;
    }
    switch ($name) {
        case 'ucache.size':
            // The extension takes 5 to 930
            $size = min(max($number, 5), 930);
            return format_number($size) . ' MB of shared memory' . ($size != $number ? ($size > $number ? ' (the minimum)' : ' (the maximum)') : '');
        case 'ucache.bucketbytes':
            // The extension takes 64 to 65536
            $bytes = min(max($number, 64), 65536);
            return 'one hash bucket per ' . format_number($bytes) . ' bytes' . ($bytes != $number ? ($bytes > $number ? ' (the minimum)' : ' (the maximum)') : '');
        case 'ucache.gc.interval':
            return $number > 0 ? 'a step every ' . format_duration($number) . ', at the end of a request' : 'off';
        case 'ucache.gc.step':
            return $number > 0 ? format_number($number) . ' hash buckets per step' : 'automatic';
        case 'ucache.gc.ttl':
            return $number > 0 ? 'removes the entries unused for ' . format_duration($number) : 'off';
        case 'ucache.gc.threshold':
            return $number > 0 ? 'steps only at or below ' . min($number, 100) . '% free memory, also on writes' : 'off';
        case 'ucache.gc.evict':
            return $number > 0 ? 'a full cache evicts until ' . min($number, 100) . '% is free' : 'off';
    }
    return null;
}

// A ratio against a limit: the fill on a track of the same hue. The numbers are in the text next to it.
function meter_markup($percent, $label, $text) {
    $percent = max(0.0, min(100.0, (float)$percent));
    // A 2px gap in the surface color separates the fill from the track
    $class = ($percent > 0 && $percent < 100) ? ' class="partial"' : '';
    return '<div class="meter" role="meter" aria-valuemin="0" aria-valuemax="100" aria-valuenow="'
        . number_format($percent, 1, '.', '') . '" aria-label="' . html_escape($label) . '" aria-valuetext="'
        . html_escape($text) . '" title="' . html_escape($text) . '"><span' . $class . ' style="width: '
        . number_format($percent, 2, '.', '') . '%"></span></div>';
}

// The row under the list of entries that tells which ones it shows, with links to the other pages
function entries_pager($start, $shown, $total) {
    global $PHP_SELF;

    $pages = [];
    if ($start > 0) {
        $pages['First'] = 0;
        $pages['Previous'] = max(0, $start - CACHE_MAX_ENTRY);
    }
    if ($start + $shown < $total) {
        $pages['Next'] = $start + $shown;
        $pages['Last'] = intdiv($total - 1, CACHE_MAX_ENTRY) * CACHE_MAX_ENTRY;
    }
    $html = 'Entries ' . format_number($start + 1) . ' to ' . format_number($start + $shown) . ' of ' . format_number($total);
    foreach ($pages as $label => $from) {
        $html .= ' &middot; <a href="' . $PHP_SELF . '?page=' . UCACHE_DATA . '&amp;start=' . $from . '#entries">' . $label . '</a>';
    }
    return '<tr class="more"><td colspan="6">' . $html . "</td></tr>\n";
}

// A formatted quantity, escaped, with its units set smaller: 4.8 MB, 2 min 5 s
function with_units($text) {
    return preg_replace('/ ([^\d\s,.]+)/', ' <span class="unit">$1</span>', html_escape($text));
}

// The icons of the stat tiles: the path data of a 24 × 24 line drawing
define('ICON_HITS', 'M20 12a8 8 0 1 1-16 0a8 8 0 1 1 16 0M8.5 12.5l2.5 2.5 4.5-5');
define('ICON_MEMORY', 'M7 6h10a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1zM10 3v3M14 3v3M10 18v3M14 18v3M3 10h3M3 14h3M18 10h3M18 14h3');
define('ICON_ENTRIES', 'M9 7h11M9 12h11M9 17h11M4.5 7h.01M4.5 12h.01M4.5 17h.01');
define('ICON_UPTIME', 'M20 12a8 8 0 1 1-16 0a8 8 0 1 1 16 0M12 8v4l2.5 2.5');
define('ICON_SCAVENGER', 'M4 7h16M10 7V4.5h4V7M6.5 7l1 12.5h9l1-12.5M10 11v5M14 11v5');

// A stat tile: the label, the value and what goes under it (HTML), and the facts at its foot (label => HTML)
function stat_tile($icon, $label, $value, $under, array $facts) {
    $html = '<div class="tile"><p class="tile-label"><svg class="tile-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="'
        . $icon . '" /></svg>' . html_escape($label) . '</p><div class="tile-body"><p class="tile-value">' . $value . '</p>' . $under
        . '</div><dl class="facts">';
    foreach ($facts as $term => $detail) {
        $html .= '<div><dt>' . html_escape($term) . '</dt><dd>' . $detail . '</dd></div>';
    }
    return $html . "</dl></div>\n";
}

// The stat tiles at the top of the Summary page
function render_overview(array $info, array $mem) {
    global $PHP_SELF;

    $hits = (int)$info['total_hit_count'];
    $misses = (int)$info['total_miss_count'];
    $lookups = $hits + $misses;
    $total = (int)$mem['memory_total'];
    $free = (int)$mem['memory_free'];
    $used = max(0, $total - $free);
    $used_percent = $total > 0 ? $used / $total * 100 : 0;
    $entries = format_number($info['total_item_count']);
    $uptime = (int)$info['total_cache_uptime'];
    $created = time() - $uptime;

    echo '<section class="tiles" aria-label="User cache overview">', "\n";

    if ($lookups > 0) {
        $hit_rate = $hits / $lookups * 100;
        echo stat_tile(ICON_HITS, 'Hit rate', number_format($hit_rate, 1) . '<span class="unit">%</span>',
            meter_markup($hit_rate, 'Hit rate', format_percent($hit_rate) . ' of ' . format_number($lookups) . ' lookups were hits'),
            ['Hits' => format_number($hits), 'Misses' => format_number($misses)]);
    } else {
        echo stat_tile(ICON_HITS, 'Hit rate', '&ndash;', '', ['Hits' => '0', 'Misses' => '0']);
    }

    echo stat_tile(ICON_MEMORY, 'Memory used', with_units(format_bytes($used)),
        meter_markup($used_percent, 'Memory used', format_percent($used_percent) . ' of ' . format_bytes($total) . ' used'),
        ['Used' => format_percent($used_percent) . ' of ' . format_bytes($total), 'Free' => format_bytes($free), 'Overhead' => format_bytes($mem['memory_overhead'])]);

    // Pre-release builds of php_ucache.dll do not report the scavenger
    if (isset($info['total_scavenged_count'])) {
        $facts = ['Evicted' => format_number($info['total_evicted_count'])];
        if ($info['last_scavenge_seconds'] === null) {
            $facts['Last run'] = placeholder('none yet');
        } else {
            $facts['Last run'] = format_duration($info['last_scavenge_seconds']) . ' ago';
            $facts['Last run removed'] = format_number($info['last_scavenge_count']);
        }
        // Builds before 1.0.0.2 do not report the locks found abandoned
        if (isset($info['total_abandoned_lock_count'])) {
            $facts['Abandoned locks'] = '<span title="Times a process found the cache lock or the allocator lock abandoned by a process that ended while it held it (killed or crashed); what that process was updating was put back">'
                . format_number($info['total_abandoned_lock_count']) . '</span>';
        }
        echo stat_tile(ICON_SCAVENGER, 'Scavenged entries', format_number($info['total_scavenged_count']), '', $facts);
    }

    echo stat_tile(ICON_ENTRIES, 'Cached entries', '<a href="' . $PHP_SELF . '?page=' . UCACHE_DATA . '#entries">' . $entries . '</a>', '',
        ['Scope' => empty($info['is_local_cache']) ? 'global' : 'local']);

    echo stat_tile(ICON_UPTIME, 'Cache uptime', '<span title="' . format_number($uptime) . ' seconds">' . with_units(format_duration($uptime)) . '</span>', '',
        ['Created' => '<span title="' . html_escape(date('Y-m-d H:i:s T', $created)) . '">' . html_escape(date('j M, H:i T', $created)) . '</span>']);

    echo "</section>\n";
}

$host = server_value('HTTP_HOST');
if ($host === null) {
    $host = env_value('COMPUTERNAME');
}
?>
<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="robots" content="noindex, nofollow">
    <meta name="color-scheme" content="light dark">
    <title>ucache statistics</title>
    <style>
        :root {
            color-scheme: light dark;
            --page: #f9f9f7;
            --surface: #fcfcfb;
            --ink: #0b0b0b;
            --ink-2: #52514e;
            --hairline: #e1e0d9;
            --border: rgba(11, 11, 11, 0.10);
            --wash: rgba(11, 11, 11, 0.04);
            --chip: rgba(11, 11, 11, 0.06);
            --shadow: 0 1px 2px rgba(11, 11, 11, 0.04), 0 2px 8px rgba(11, 11, 11, 0.04);
            --accent: #2a78d6;
            --accent-track: #b7d3f6;
            --accent-tint: #cde2fb;
            --link: #1c5cab;
            --danger: #d03b3b;
            --danger-hover: #b83030;
            --sans: system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
            --mono: ui-monospace, "Cascadia Mono", "Cascadia Code", Consolas, "Liberation Mono", monospace;
        }

        @media (prefers-color-scheme: dark) {
            :root {
                --page: #0d0d0d;
                --surface: #1a1a19;
                --ink: #f0efec;
                --ink-2: #c3c2b7;
                --hairline: #2c2c2a;
                --border: rgba(255, 255, 255, 0.10);
                --wash: rgba(255, 255, 255, 0.04);
                --chip: rgba(255, 255, 255, 0.08);
                --shadow: none;
                --accent: #3987e5;
                --accent-track: #184f95;
                --accent-tint: #0d366b;
                --link: #6da7ec;
            }
        }

        *,
        *::before,
        *::after {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            background: var(--page);
            color: var(--ink);
            font: 14px/1.5 var(--sans);
        }

        a {
            color: var(--link);
            text-decoration: none;
        }

        a:hover {
            text-decoration: underline;
        }

        a:focus-visible,
        button:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 2px;
            border-radius: 4px;
        }

        code,
        pre,
        .mono {
            font-family: var(--mono);
            font-size: 13px;
        }

        .wrap {
            max-width: 1200px;
            margin: 0 auto;
            padding-left: 24px;
            padding-right: 24px;
        }

        .topbar {
            background: var(--surface);
            border-bottom: 1px solid var(--hairline);
        }

        .topbar-inner {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            justify-content: space-between;
            gap: 12px 24px;
            padding-top: 20px;
            padding-bottom: 12px;
        }

        .brand {
            display: flex;
            align-items: center;
            gap: 14px;
        }

        .logo {
            width: 42px;
            height: 42px;
            padding: 9px;
            border-radius: 12px;
            background: var(--accent);
            fill: none;
            stroke: #ffffff;
            stroke-width: 2;
            stroke-linecap: round;
        }

        h1 {
            margin: 0;
            font-size: 20px;
            font-weight: 600;
            letter-spacing: -0.01em;
            line-height: 1.25;
        }

        h1 span {
            color: var(--ink-2);
            font-weight: 400;
        }

        .subtitle {
            margin: 2px 0 0;
            color: var(--ink-2);
            font-size: 13px;
        }

        .versions {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            margin: 0;
            padding: 0;
            list-style: none;
        }

        .versions li {
            padding: 3px 11px;
            border: 1px solid var(--border);
            border-radius: 999px;
            background: var(--page);
            color: var(--ink-2);
            font-size: 12px;
        }

        .versions strong {
            color: var(--ink);
            font-weight: 600;
        }

        .tabs {
            display: flex;
            gap: 28px;
        }

        .tabs a {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            margin-bottom: -1px;
            padding: 10px 2px 11px;
            border-bottom: 2px solid transparent;
            color: var(--ink-2);
            font-weight: 500;
        }

        .tabs a:hover {
            color: var(--ink);
            text-decoration: none;
        }

        .tabs a[aria-current="page"] {
            border-bottom-color: var(--accent);
            color: var(--ink);
            font-weight: 600;
        }

        .tab-count {
            min-width: 22px;
            padding: 0 7px;
            border-radius: 999px;
            background: var(--chip);
            color: var(--ink-2);
            font-size: 12px;
            font-weight: 500;
            line-height: 20px;
            text-align: center;
        }

        .tabs a[aria-current="page"] .tab-count {
            background: var(--accent-tint);
            color: var(--link);
        }

        main {
            display: grid;
            gap: 20px;
            padding-top: 24px;
            padding-bottom: 48px;
        }

        .tiles {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(min(200px, 100%), 1fr));
            gap: 16px;
        }

        .tile,
        .card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            box-shadow: var(--shadow);
        }

        .tile {
            display: flex;
            flex-direction: column;
            padding: 16px 18px;
        }

        .tile-label {
            display: flex;
            align-items: center;
            gap: 10px;
            margin: 0;
            color: var(--ink-2);
            font-size: 13px;
            font-weight: 500;
        }

        .tile-icon {
            flex: none;
            width: 28px;
            height: 28px;
            padding: 5px;
            border-radius: 8px;
            background: var(--accent-tint);
            fill: none;
            stroke: var(--link);
            stroke-width: 2;
            stroke-linecap: round;
            stroke-linejoin: round;
        }

        /* As tall with a meter as without, so that the facts of all the tiles start at the same height */
        .tile-body {
            min-height: 83px;
            padding-bottom: 14px;
        }

        .tile-value {
            margin: 14px 0 0;
            font-size: 30px;
            font-weight: 600;
            letter-spacing: -0.01em;
            line-height: 1.15;
        }

        .tile-value a {
            color: inherit;
        }

        .unit {
            color: var(--ink-2);
            font-size: 17px;
            font-weight: 500;
            letter-spacing: 0;
        }

        .facts {
            margin: 0;
            padding-top: 10px;
            border-top: 1px solid var(--hairline);
            font-size: 13px;
        }

        .facts div {
            display: flex;
            justify-content: space-between;
            gap: 12px;
            padding: 2px 0;
        }

        .facts dt {
            color: var(--ink-2);
        }

        .facts dd {
            margin: 0;
            text-align: right;
            font-variant-numeric: tabular-nums;
        }

        .meter {
            height: 8px;
            margin-top: 12px;
            overflow: hidden;
            border-radius: 4px;
            background: var(--accent-track);
        }

        .meter span {
            display: block;
            height: 100%;
            border-radius: 0 4px 4px 0;
            background: var(--accent);
        }

        .meter span.partial {
            box-shadow: 2px 0 0 var(--surface);
        }

        /* The two cards of the Summary page side by side and as tall as each other; on narrow screens, one above the other */
        .columns {
            display: grid;
            grid-template-columns: repeat(2, minmax(0, 1fr));
            gap: 20px;
        }

        @media (max-width: 760px) {
            .columns {
                grid-template-columns: minmax(0, 1fr);
            }
        }

        /* Clips the backgrounds of the rows to the rounded corners */
        .card {
            overflow: hidden;
        }

        .card-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 12px;
            min-height: 52px;
            padding: 10px 18px;
            border-bottom: 1px solid var(--hairline);
        }

        .card-header h2 {
            margin: 0;
            font-size: 15px;
            font-weight: 600;
        }

        .count,
        .hint {
            margin-left: 8px;
            color: var(--ink-2);
            font-weight: 400;
        }

        .hint {
            font-family: var(--mono);
            font-size: 12px;
        }

        .table-wrap {
            overflow-x: auto;
        }

        table {
            width: 100%;
            border-collapse: collapse;
        }

        th,
        td {
            padding: 9px 18px;
            border-bottom: 1px solid var(--hairline);
            text-align: left;
            vertical-align: top;
        }

        tbody tr:last-child>*,
        .kv tr:last-child>* {
            border-bottom: 0;
        }

        .kv th {
            width: 40%;
            color: var(--ink-2);
            font-weight: 400;
        }

        .kv td {
            overflow-wrap: anywhere;
        }

        /* The names of the settings in one piece: their column takes the width of the longest */
        .settings th[scope="row"] {
            width: 1%;
            white-space: nowrap;
        }

        .kv .group th {
            width: auto;
            padding: 7px 18px;
            background: var(--wash);
            color: var(--ink-2);
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 0.06em;
            text-transform: uppercase;
        }

        .note {
            display: block;
            margin-top: 1px;
            color: var(--ink-2);
            font-size: 12.5px;
        }

        .placeholder {
            color: var(--ink-2);
            font-style: italic;
        }

        .entries thead th {
            background: var(--wash);
            color: var(--ink-2);
            font-size: 12px;
            font-weight: 600;
            white-space: nowrap;
        }

        .entries td {
            vertical-align: middle;
        }

        .entries tbody tr:hover {
            background: var(--wash);
        }

        .num {
            text-align: right;
            font-variant-numeric: tabular-nums;
            white-space: nowrap;
        }

        .key a,
        .key span {
            display: block;
            max-width: 52ch;
            overflow: hidden;
            font-family: var(--mono);
            font-size: 13px;
            text-overflow: ellipsis;
            white-space: nowrap;
        }

        .type {
            padding: 1px 6px;
            border-radius: 4px;
            background: var(--chip);
            color: var(--ink-2);
            font-family: var(--mono);
            font-size: 12px;
        }

        .more td,
        .empty {
            padding: 14px 18px;
            color: var(--ink-2);
            text-align: center;
        }

        .empty {
            margin: 0;
        }

        .danger {
            padding: 6px 14px;
            border: 0;
            border-radius: 8px;
            background: var(--danger);
            color: #ffffff;
            font: inherit;
            font-size: 13px;
            font-weight: 600;
            cursor: pointer;
        }

        .danger:hover {
            background: var(--danger-hover);
        }

        .notice {
            margin: 0;
            padding: 14px 18px;
            border: 1px solid var(--border);
            border-left: 3px solid var(--accent);
            border-radius: 10px;
            background: var(--surface);
            color: var(--ink-2);
        }

        .notice strong {
            color: var(--ink);
            font-weight: 600;
        }

        .breadcrumb {
            margin: 0;
            color: var(--ink-2);
        }

        pre {
            max-height: 70vh;
            margin: 0;
            padding: 16px 18px;
            overflow: auto;
            line-height: 1.55;
            white-space: pre-wrap;
            overflow-wrap: anywhere;
        }
    </style>
</head>

<body>
    <header class="topbar">
        <div class="wrap topbar-inner">
            <div class="brand">
                <svg class="logo" viewBox="0 0 24 24" aria-hidden="true" focusable="false">
                    <ellipse cx="12" cy="5.5" rx="7.5" ry="2.75" />
                    <path d="M4.5 5.5v6.5c0 1.52 3.36 2.75 7.5 2.75s7.5-1.23 7.5-2.75V5.5" />
                    <path d="M4.5 12v6.5c0 1.52 3.36 2.75 7.5 2.75s7.5-1.23 7.5-2.75V12" />
                </svg>
                <div>
                    <h1>UCache <span>statistics</span></h1>
                    <p class="subtitle"><?= $host !== null ? html_escape($host) : 'UCache extension for PHP' ?></p>
                </div>
            </div>
            <ul class="versions" aria-label="Versions">
                <li>UCache <strong><?= html_escape(phpversion('ucache')) ?></strong></li>
                <li>PHP <strong><?= html_escape(phpversion()) ?></strong></li>
            </ul>
        </div>
        <nav class="wrap tabs" aria-label="Pages">
            <?php foreach ([SUMMARY_DATA => 'Summary', UCACHE_DATA => 'User cache'] as $id => $label) { ?>
                <a href="<?= $PHP_SELF ?>?page=<?= $id ?>" <?= $page == $id ? ' aria-current="page"' : '' ?>><?= $label ?><?php if ($id == UCACHE_DATA && $user_cache_available) { ?><span class="tab-count" title="Cached entries"><?= format_number($ucache_info['total_item_count']) ?></span><?php } ?></a>
            <?php } ?>
        </nav>
    </header>
    <main class="wrap">
        <?php if ($page == SUMMARY_DATA) {
            if ($user_cache_available) {
                render_overview($ucache_info, $ucache_mem_info);
            } else {
                echo cache_unavailable_notice();
            }
        ?>
            <div class="columns">
                <section class="card">
                    <div class="card-header">
                        <h2>General information</h2>
                    </div>
                    <table class="kv">
                        <?php
                        echo kv_group('Versions');
                        echo kv_row('UCache version', phpversion('ucache'));
                        echo kv_row('PHP version', phpversion());
                        echo kv_group('Web server');
                        echo kv_row('Server software', server_value('SERVER_SOFTWARE'));
                        echo kv_row('Host name', server_value('HTTP_HOST'));
                        echo kv_row('Document root', server_value('DOCUMENT_ROOT'));
                        echo kv_row('PHPRC', server_value('PHPRC'), 'Not defined');
                        echo kv_row('Application pool ID', env_value('APP_POOL_ID'), 'Not available');
                        echo kv_row('Site ID', server_value('INSTANCE_ID'), 'Not available');
                        echo kv_row('FastCGI impersonation', ini_bool(ini_get('fastcgi.impersonate')) ? 'enabled' : 'disabled');
                        echo kv_group('Machine');
                        echo kv_row('Machine name', env_value('COMPUTERNAME'));
                        echo kv_row('Operating system', php_uname('s') . ' ' . php_uname('r'));
                        echo kv_row('Processor information', server_value('PROCESSOR_IDENTIFIER'));
                        echo kv_row('Number of processors', server_value('NUMBER_OF_PROCESSORS'));
                        ?>
                    </table>
                </section>
                <section class="card">
                    <div class="card-header">
                        <h2>Cache settings</h2>
                    </div>
                    <table class="kv settings">
                        <?php
                        // The settings used for debugging are not shown
                        $settings = array_diff_key(ini_get_all('ucache'), array_flip($settings_to_hide));
                        foreach (array_keys($settings) as $ini_name) {
                            if (!in_array($ini_name, array_merge(...array_values($setting_groups)), true)) {
                                $setting_groups['Cache'][] = $ini_name;
                            }
                        }
                        foreach ($setting_groups as $group => $names) {
                            $names = array_values(array_intersect($names, array_keys($settings)));
                            if (!$names) {
                                continue;
                            }
                            echo kv_group($group);
                            foreach ($names as $ini_name) {
                                // A string setting that is not set has the value null (PHP 8.1 deprecates passing null to string functions)
                                echo '<tr><th scope="row"><code>', html_escape($ini_name), '</code></th><td>',
                                setting_cell($ini_name, (string)$settings[$ini_name]['local_value']), "</td></tr>\n";
                            }
                        }
                        ?>
                    </table>
                </section>
            </div>
            <?php } elseif ($ucache_key === null) {
            if (!$user_cache_available) {
                echo cache_unavailable_notice();
            } else {
            ?>
                <section class="card" id="entries">
                    <div class="card-header">
                        <h2>Entries<?php if ($ucache_entries !== null) { ?><span class="count"><?= format_number($ucache_info['total_item_count']) ?></span><?php } ?></h2>
                        <?php if ($can_clear && $ucache_info['total_item_count'] > 0) { ?>
                            <form method="post" action="<?= $PHP_SELF ?>?page=<?= UCACHE_DATA ?>">
                                <input type="hidden" name="clc" value="1">
                                <input type="hidden" name="token" value="<?= clear_token($nonce) ?>">
                                <button type="submit" class="danger">Clear All</button>
                            </form>
                        <?php   } elseif (USE_AUTHENTICATION && $ucache_info['total_item_count'] > 0) { ?>
                            <span class="note">To clear the cache here, set PASSWORD in this script to a long random value and change USERNAME too: with IIS authentication, PASSWORD is the only secret behind Clear All.</span>
                        <?php   } ?>
                    </div>
                    <?php if ($ucache_entries === null) { ?>
                        <p class="empty">The entries could not be listed<?= $list_error !== null ? ': ' . html_escape($list_error) : '.' ?></p>
                    <?php   } elseif (!$ucache_entries && $ucache_info['total_item_count'] > 0) { ?>
                        <p class="empty">The user cache has <?= format_number($ucache_info['total_item_count']) ?> entries, none from entry <?= format_number($ucache_start + 1) ?> on. <a href="<?= $PHP_SELF ?>?page=<?= UCACHE_DATA ?>#entries">First page</a></p>
                    <?php   } elseif (!$ucache_entries) { ?>
                        <p class="empty">The user cache is empty.</p>
                    <?php   } else { ?>
                        <div class="table-wrap">
                            <table class="entries">
                                <thead>
                                    <tr>
                                        <th scope="col" title="Name of the key which is used to store the data">Key</th>
                                        <th scope="col" title="Type of the value stored">Type</th>
                                        <th scope="col" class="num" title="Memory taken by the value">Size</th>
                                        <th scope="col" class="num" title="The ttl the entry was stored with">TTL</th>
                                        <th scope="col" class="num" title="Time elapsed since the entry was added to the cache">Age</th>
                                        <th scope="col" class="num" title="Number of times the entry has been served from the cache">Hits</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    <?php
                                    foreach ($ucache_entries as $entry) {
                                        $key = html_escape($entry['key_name']);
                                        echo '<tr>';
                                        echo '<td class="key"><a href="', $PHP_SELF, '?page=', UCACHE_DATA, '&amp;key=', urlencode($entry['key_name']),
                                        '" title="', $key, '">', $key, '</a></td>';
                                        echo '<td><span class="type">', html_escape($entry['value_type']), '</span></td>';
                                        echo '<td class="num" title="', format_number($entry['value_size']), ' bytes">', format_bytes($entry['value_size']), '</td>';
                                        if ($entry['ttl_seconds'] > 0) {
                                            echo '<td class="num" title="', format_number($entry['ttl_seconds']), ' seconds">', format_duration($entry['ttl_seconds']), '</td>';
                                        } else {
                                            echo '<td class="num">', placeholder('never'), '</td>';
                                        }
                                        echo '<td class="num" title="', format_number($entry['age_seconds']), ' seconds">', format_duration($entry['age_seconds']), '</td>';
                                        echo '<td class="num">', format_number($entry['hitcount']), "</td></tr>\n";
                                    }
                                    $shown = count($ucache_entries);
                                    if ($ucache_start > 0 || $shown < $ucache_info['total_item_count']) {
                                        echo entries_pager($ucache_start, $shown, $ucache_info['total_item_count']);
                                    }
                                    ?>
                                </tbody>
                            </table>
                        </div>
                    <?php   } ?>
                </section>
            <?php
            }
        } else {
            ?>
            <p class="breadcrumb"><a href="<?= $PHP_SELF ?>?page=<?= UCACHE_DATA ?>">User cache</a> &rsaquo; Entry</p>
            <?php
            $entry = null;
            if ($user_cache_available && uc('exists', $ucache_key)) {
                $entry_info = uc('info', true, $ucache_key);
                if (is_array($entry_info) && isset($entry_info['ucache_entries'][1])) {
                    $entry = $entry_info['ucache_entries'][1];
                }
            }
            if (!$user_cache_available) {
                echo cache_unavailable_notice();
            } elseif ($entry === null) {
                echo notice('The variable with this key does not exist in the user cache.');
            } else {
                // An object of the value that this page cannot unserialize (an enum or a class that it does not
                // define, a class changed since) fails the read with PHP's warning, shown in a notice instead.
                // Called directly, not through uc(): a by-reference argument does not pass through ...$args
                $value_errors = [];
                set_error_handler(function ($errno, $errstr) use (&$value_errors) {
                    $value_errors[] = $errstr;
                    return true;
                });
                $get = ini_get('ucache.prefix') . 'get';
                $value_read = false;
                $value = $get($ucache_key, $value_read);
                restore_error_handler();
                ob_start();
                var_dump($value);
                $value_dump = ob_get_clean();
            ?>
                <section class="card">
                    <div class="card-header">
                        <h2>Entry</h2>
                    </div>
                    <table class="kv">
                        <tr>
                            <th scope="row">Key</th>
                            <td class="mono"><?= html_escape($entry['key_name']) ?></td>
                        </tr>
                        <tr>
                            <th scope="row">Value type</th>
                            <td><span class="type"><?= html_escape($entry['value_type']) ?></span></td>
                        </tr>
                        <tr>
                            <th scope="row">Size</th>
                            <td><?= format_bytes($entry['value_size']) ?> <span class="count">(<?= format_number($entry['value_size']) ?> bytes)</span></td>
                        </tr>
                        <tr>
                            <th scope="row">Time to live</th>
                            <td><?php if ($entry['ttl_seconds'] > 0) { ?><?= format_duration($entry['ttl_seconds']) ?> <span class="count">(<?= format_number($entry['ttl_seconds']) ?> seconds)</span><?php } else {
                                                                                                                                                                                                        echo placeholder('never expires');
                                                                                                                                                                                                    } ?></td>
                        </tr>
                        <tr>
                            <th scope="row">Age</th>
                            <td><?= format_duration($entry['age_seconds']) ?> <span class="count">(<?= format_number($entry['age_seconds']) ?> seconds)</span></td>
                        </tr>
                        <tr>
                            <th scope="row">Hit count</th>
                            <td><?= format_number($entry['hitcount']) ?></td>
                        </tr>
                    </table>
                </section>
                <section class="card">
                    <div class="card-header">
                        <h2>Value<span class="hint">var_dump()</span></h2>
                    </div>
                    <?php if ($value_read) { ?>
                    <pre><?= html_escape($value_dump) ?></pre>
                    <?php } else {
                        echo notice('The value could not be read here' . ($value_errors ? ': ' . html_escape(implode(' ', $value_errors)) : '') . '.');
                    } ?>
                </section>
        <?php
            }
        }
        ?>
    </main>
</body>

</html>