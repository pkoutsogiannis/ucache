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
   | Module: php_ucache.c                                                                         |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#define NAMESALT_LENGTH_MAXIMUM     8
#define MAX_ARRAY_INDEX_DIGITS      21
#define PREFIX_DEFAULT              "ucache_"
#define PREFIX_LENGTH_MAXIMUM       64

/* The default of ucache.size, in megabytes */
#define SIZE_DEFAULT                8

/* Where ucache_modify_size stops adding digits: any number this large is above the maximum anyway */
#define SIZE_NUMBER_SATURATED       ((zend_ulong)1 << 40)

/* How ucache_modify_size read ucache.size, for the warning that MINIT raises (see size_validate) */
#define SIZE_READ_AS_WRITTEN        0   /* A size, or an empty value (the default) */
#define SIZE_READ_LEADING_NUMBER    1   /* Digits, then other text: the digits, in megabytes */
#define SIZE_READ_NOT_A_SIZE        2   /* Anything else: the default */

/* Default values in arginfo (PHP 8.0+) let named arguments skip optional parameters */
#if PHP_VERSION_ID >= 80000
# define UCACHE_ARG_INFO_DEFAULT(pass_by_ref, name, default_value) \
    ZEND_ARG_INFO_WITH_DEFAULT_VALUE(pass_by_ref, name, default_value)
#else
# define UCACHE_ARG_INFO_DEFAULT(pass_by_ref, name, default_value) \
    ZEND_ARG_INFO(pass_by_ref, name)
#endif

/* ucache.namesalt cut to NAMESALT_LENGTH_MAXIMUM characters for the object names. The INI */
/* value itself is never changed: since PHP 7.3 it is an interned string, shared with every */
/* other INI value or name that has the same text */
static char ucache_namesalt[NAMESALT_LENGTH_MAXIMUM + 1];

/* ucache_functions with the names that ucache.prefix gives them, registered in MINIT and */
/* unregistered in MSHUTDOWN. NULL when they are not registered */
static zend_function_entry * ucache_registered_functions = NULL;

/* How ucache_modify_size read ucache.size (SIZE_READ_*) */
static unsigned char ucache_size_read = SIZE_READ_AS_WRITTEN;

static void set_success(zval * success, zend_bool value);
static HashTable * array_form_table(zval * pzarray, zval * psnapshot);
static void set_or_add(INTERNAL_FUNCTION_PARAMETERS, unsigned char isadd);
static void inc_or_dec(INTERNAL_FUNCTION_PARAMETERS, unsigned char isdec);
static int functions_register(int type);
static void functions_unregister(void);
static ZEND_INI_MH(ucache_modify_size);
static void size_validate(void);
static void startup_failure_warning(int result);

/* START OF PHP EXTENSION MACROS STUFF */
PHP_MINIT_FUNCTION(ucache);
PHP_MSHUTDOWN_FUNCTION(ucache);
PHP_RSHUTDOWN_FUNCTION(ucache);
PHP_MINFO_FUNCTION(ucache);

/* Info functions exposed by this extension */
PHP_FUNCTION(ucache_meminfo);

/* ZVAL cache functions matching other caches */
PHP_FUNCTION(ucache_get);
PHP_FUNCTION(ucache_set);
PHP_FUNCTION(ucache_add);
PHP_FUNCTION(ucache_delete);
PHP_FUNCTION(ucache_clear);
PHP_FUNCTION(ucache_exists);
PHP_FUNCTION(ucache_info);
PHP_FUNCTION(ucache_inc);
PHP_FUNCTION(ucache_dec);
PHP_FUNCTION(ucache_cas);

#ifdef UCACHE_TEST
PHP_FUNCTION(ucache_lasterror);
PHP_FUNCTION(ucache_runtests);
#endif

ZEND_DECLARE_MODULE_GLOBALS(ucache)

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_meminfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_get, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
    UCACHE_ARG_INFO_DEFAULT(1, success, "null")
ZEND_END_ARG_INFO()

/* One required argument, as in zend_parse_parameters: the array form takes only the array */
ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_set, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
    UCACHE_ARG_INFO_DEFAULT(0, value, "null")
    UCACHE_ARG_INFO_DEFAULT(0, ttl, "0")
ZEND_END_ARG_INFO()

/* The same parameters (see set_or_add), as PHP 8's gen_stub.php writes identical arginfo */
#define arginfo_ucache_add arginfo_ucache_set

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_delete, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_clear, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_exists, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_info, 0, 0, 0)
    UCACHE_ARG_INFO_DEFAULT(0, summaryonly, "false")
    UCACHE_ARG_INFO_DEFAULT(0, key, "null")
    UCACHE_ARG_INFO_DEFAULT(0, offset, "0")
    UCACHE_ARG_INFO_DEFAULT(0, limit, "0")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_inc, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
    UCACHE_ARG_INFO_DEFAULT(0, delta, "1")
    UCACHE_ARG_INFO_DEFAULT(1, success, "null")
ZEND_END_ARG_INFO()

/* The same parameters (see inc_or_dec) */
#define arginfo_ucache_dec arginfo_ucache_inc

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_cas, 0, 0, 3)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, cvalue)
    ZEND_ARG_INFO(0, nvalue)
ZEND_END_ARG_INFO()

#ifdef UCACHE_TEST
ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_lasterror, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_runtests, 0, 0, 0)
ZEND_END_ARG_INFO()
#endif

/* Put all user defined functions here. The names all start with PREFIX_DEFAULT, which MINIT */
/* replaces with ucache.prefix (see functions_register) */
static const zend_function_entry ucache_functions[] = {
    PHP_FE(ucache_meminfo, arginfo_ucache_meminfo)
    PHP_FE(ucache_get, arginfo_ucache_get)
    PHP_FE(ucache_set, arginfo_ucache_set)
    PHP_FE(ucache_add, arginfo_ucache_add)
    PHP_FE(ucache_delete, arginfo_ucache_delete)
    PHP_FE(ucache_clear, arginfo_ucache_clear)
    PHP_FE(ucache_exists, arginfo_ucache_exists)
    PHP_FE(ucache_info, arginfo_ucache_info)
    PHP_FE(ucache_inc, arginfo_ucache_inc)
    PHP_FE(ucache_dec, arginfo_ucache_dec)
    PHP_FE(ucache_cas, arginfo_ucache_cas)
#ifdef UCACHE_TEST
    PHP_FE(ucache_lasterror, arginfo_ucache_lasterror)
    PHP_FE(ucache_runtests, arginfo_ucache_runtests)
#endif
    PHP_FE_END
};

/* ucache_module_entry */
zend_module_entry ucache_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_UCACHE_EXTNAME,
    NULL,                    /* Functions: registered by MINIT, under ucache.prefix */
    PHP_MINIT(ucache),       /* MINIT */
    PHP_MSHUTDOWN(ucache),   /* MSHUTDOWN */
    NULL,                    /* RINIT */
    PHP_RSHUTDOWN(ucache),   /* RSHUTDOWN */
    PHP_MINFO(ucache),       /* MINFO */
    PHP_UCACHE_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_UCACHE
    ZEND_GET_MODULE(ucache)
#endif

PHP_INI_BEGIN()
/* index 0 */  STD_PHP_INI_BOOLEAN("ucache.enabled", "1", PHP_INI_ALL, OnUpdateBool, ucenabled, zend_ucache_globals, ucache_globals)
/* index 1 */  STD_PHP_INI_BOOLEAN("ucache.enablecli", "0", PHP_INI_SYSTEM, OnUpdateBool, enablecli, zend_ucache_globals, ucache_globals)
/* index 2 */  STD_PHP_INI_ENTRY("ucache.size", "8", PHP_INI_SYSTEM, ucache_modify_size, ucachesize, zend_ucache_globals, ucache_globals)
/* index 3 */  STD_PHP_INI_ENTRY("ucache.namesalt", NULL, PHP_INI_SYSTEM, OnUpdateString, namesalt, zend_ucache_globals, ucache_globals)
/* index 4 */  STD_PHP_INI_ENTRY("ucache.apppoolid", NULL, PHP_INI_SYSTEM, OnUpdateString, apppoolid, zend_ucache_globals, ucache_globals)
/* index 5 */  STD_PHP_INI_ENTRY("ucache.filemapdir", NULL, PHP_INI_SYSTEM, OnUpdateString, filemapdir, zend_ucache_globals, ucache_globals)
/* index 6 */  STD_PHP_INI_ENTRY("ucache.debuglevel", "0", PHP_INI_ALL, ucache_modify_debuglevel, debuglevel, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.threshold", "0", PHP_INI_SYSTEM, OnUpdateLong, ucgc_threshold, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.interval", "10", PHP_INI_SYSTEM, OnUpdateLong, ucgc_interval, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.step", "0", PHP_INI_SYSTEM, OnUpdateLong, ucgc_step, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.ttl", "0", PHP_INI_SYSTEM, OnUpdateLong, ucgc_ttl, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.evict", "0", PHP_INI_SYSTEM, OnUpdateLong, ucgc_evict, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.prefix", PREFIX_DEFAULT, PHP_INI_SYSTEM, OnUpdateString, prefix, zend_ucache_globals, ucache_globals)
PHP_INI_END()

/* END OF PHP EXTENSION MACROS STUFF */

static void globals_initialize(zend_ucache_globals * globals);
static void globals_terminate(zend_ucache_globals * globals);

/* Initialize globals */
static void globals_initialize(zend_ucache_globals * globals)
{
    dprintverbose("start globals_initialize");

#if defined(COMPILE_DL_UCACHE) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    /* Initalize the ucache_globals items, before parsing the INI file */
    UCG(ucenabled)   = 1;    /* User cache enabled by default */
    UCG(enablecli)   = 0;    /* ucache not enabled by default for CLI */
    UCG(ucachesize)  = SIZE_DEFAULT; /* User cache size is 8 MB by default */
    UCG(ucgc_threshold) = 0;   /* Writes do not run the scavenger by default */
    UCG(ucgc_interval)  = 10;   /* Secs between scavenger runs at the end of requests */
    UCG(ucgc_ttl)       = 0;   /* Scavenger removes expired entries only */
    UCG(ucgc_step)      = 0;   /* Hash buckets scanned per scavenger step: automatic */
    UCG(ucgc_evict)     = 0;   /* A full cache evicts nothing */
    UCG(debuglevel)  = 0;    /* Debug dump not enabled by default */
    UCG(namesalt)    = NULL; /* Salt to use in names used by ucache */
    UCG(prefix)      = NULL; /* Prefix of the function names: PREFIX_DEFAULT */
    UCG(apppoolid)   = NULL; /* Use this application id */
    UCG(filemapdir)  = NULL; /* Directory where temp filemap files should be created */

    UCG(uclasterror) = 0;    /* Last error returned by user cache */
    UCG(zvucache)    = NULL; /* zvcache_context for user zvals */
    UCG(parentpid)   = 0;    /* Parent process identifier to use */
    UCG(fmapgdata)   = NULL; /* Global filemap information data */

    dprintverbose("end globals_initialize");

    return;
}

/* Terminate globals */
static void globals_terminate(zend_ucache_globals * globals)
{
    dprintverbose("start globals_terminate");
    dprintverbose("end globals_terminate");

    return;
}

/*
 * ucache.size as written: a number of megabytes (64), or a size with a K, M or G suffix in any case, which a B may
 * follow (64M, 64MB, 1G, 65536K), with blanks allowed around the number and the suffix; K is rounded up to whole
 * megabytes. Digits followed by other text (64MiB) are taken as megabytes, as 1.0.0.1 took them, and anything else
 * (abc, -1, 0x40) gives the default, as does an empty value. OnUpdateLong, which 1.0.0.1 used, applied the suffix
 * to the number of megabytes (64M asked for 67108864 MB, which the clamp turned into 930 without a word) and read 064
 * as octal. Sets UCG(ucachesize), saturated but not clamped, and ucache_size_read. It never fails, so ini_get() and
 * phpinfo() show the value as written: MINIT clamps the size and says what the cache gets (size_validate), where it
 * creates the cache, so the CLI without ucache.enablecli stays silent. The size is only used there: a [PATH=] or
 * [HOST=] section of php.ini (php-cgi) that sets it per request changes nothing
 */
static ZEND_INI_MH(ucache_modify_size)
{
    const char *  value  = ZSTR_VAL(new_value);
    size_t        index  = 0;
    size_t        digits = 0;
    zend_ulong    number = 0;
    zend_ulong    size   = SIZE_DEFAULT;
    unsigned char read   = SIZE_READ_AS_WRITTEN;

    while(value[index] == ' ' || value[index] == '\t')
    {
        index++;
    }

    /* Empty: the default, as when the setting is not there */
    if(value[index] == '\0')
    {
        goto Finished;
    }

    /* A hexadecimal number, which OnUpdateLong read as such (0x40 was 64), is not the 0 it starts with */
    if(value[index] == '0' && (value[index + 1] == 'x' || value[index + 1] == 'X'))
    {
        read = SIZE_READ_NOT_A_SIZE;
        goto Finished;
    }

    for(; value[index] >= '0' && value[index] <= '9'; index++, digits++)
    {
        if(number < SIZE_NUMBER_SATURATED)
        {
            number = number * 10 + (zend_ulong)(value[index] - '0');
        }
    }

    if(digits == 0)
    {
        read = SIZE_READ_NOT_A_SIZE;
        goto Finished;
    }

    size = number;

    while(value[index] == ' ' || value[index] == '\t')
    {
        index++;
    }

    /* The suffix, and a B after it */
    if(value[index] == 'k' || value[index] == 'K' || value[index] == 'm' || value[index] == 'M'
        || value[index] == 'g' || value[index] == 'G')
    {
        if(value[index] == 'k' || value[index] == 'K')
        {
            size = (number + 1023) / 1024;
        }
        else if(value[index] == 'g' || value[index] == 'G')
        {
            size = number * 1024;
        }

        index++;

        if(value[index] == 'b' || value[index] == 'B')
        {
            index++;
        }
    }

    while(value[index] == ' ' || value[index] == '\t')
    {
        index++;
    }

    /* Other text after the number: the number, in megabytes */
    if(value[index] != '\0')
    {
        size = number;
        read = SIZE_READ_LEADING_NUMBER;
    }

Finished:

    UCG(ucachesize)  = (zend_long)size;
    ucache_size_read = read;

    return SUCCESS;
}

/* Clamps UCG(ucachesize) to ZCACHE_SIZE_MINIMUM..ZCACHE_SIZE_MAXIMUM, and raises a startup warning when the cache */
/* does not get the ucache.size that was written. Called by MINIT where it creates the cache */
static void size_validate(void)
{
    const char * value   = INI_STR("ucache.size");
    zend_long    size    = UCG(ucachesize);
    zend_bool    clamped = 0;

    if(value == NULL)
    {
        value = "";
    }

    if(size < ZCACHE_SIZE_MINIMUM || size > ZCACHE_SIZE_MAXIMUM)
    {
        size    = (size < ZCACHE_SIZE_MINIMUM) ? ZCACHE_SIZE_MINIMUM : ZCACHE_SIZE_MAXIMUM;
        clamped = 1;

        UCG(ucachesize) = size;
    }

    if(ucache_size_read == SIZE_READ_NOT_A_SIZE)
    {
        zend_error(E_CORE_WARNING, "ucache.size \"%s\" is not a number of megabytes nor a size such as 64M: the cache has the default size, %d MB",
                   value, SIZE_DEFAULT);
    }
    else if(ucache_size_read == SIZE_READ_LEADING_NUMBER && clamped)
    {
        zend_error(E_CORE_WARNING, "ucache.size \"%s\" is not a number of megabytes nor a size such as 64M, and the number it starts with is outside %d to %d MB: the cache has %d MB",
                   value, ZCACHE_SIZE_MINIMUM, ZCACHE_SIZE_MAXIMUM, (int)size);
    }
    else if(ucache_size_read == SIZE_READ_LEADING_NUMBER)
    {
        zend_error(E_CORE_WARNING, "ucache.size \"%s\" is not a number of megabytes nor a size such as 64M: the cache has %d MB, the number it starts with",
                   value, (int)size);
    }
    else if(clamped)
    {
        zend_error(E_CORE_WARNING, "ucache.size \"%s\" is outside %d to %d MB: the cache has %d MB",
                   value, ZCACHE_SIZE_MINIMUM, ZCACHE_SIZE_MAXIMUM, (int)size);
    }
}

/*
 * The startup warning of a cache that could not be created, with the likely cause. The functions then fail, and
 * phpinfo() says unavailable. PHP writes startup warnings to its error log and, on Windows, to the Application event
 * log (PHP 7 always, PHP 8 with log_errors on or display_startup_errors off), where an administrator of IIS finds the
 * warnings of php-cgi. The Windows error is the one recorded where the step failed, for the results whose step records
 * it: error_glerror is kept for the whole process and other steps record errors they recover from, so MINIT clears it
 * first and the warning leaves it out for the other results
 */
static void startup_failure_warning(int result)
{
    unsigned int winerror    = error_getlasterror();
    zend_bool    haswinerror = 0;
    const char * cause       = NULL;

    switch(result)
    {
        case FATAL_LOCK_INIT_CREATEMUTEX:
        case FATAL_FILEMAP_INIT_EVENT:
        case FATAL_FILEMAP_INFOMAP:
        case FATAL_FILEMAP_MAPVIEW:
        case FATAL_FILEMAP_CREATEFILE:
        case FATAL_FILEMAP_CREATEFILEMAP:
        case FATAL_FILEMAP_REVERT_FAILED:
        case FATAL_ZVCACHE_INIT_EVENT:
            haswinerror = (winerror != 0);
            break;
        default:
            break;
    }

    if(result == FATAL_FILEMAP_CREATEFILE)
    {
        /* Not necessarily the folder: a file of another account can be in the way too (Windows error 5) */
        zend_error(E_CORE_WARNING, "ucache: the user cache could not be created, so the ucache functions fail: its file in ucache.filemapdir \"%s\" could not be created or opened (error %d, Windows error %u)",
                   (UCG(filemapdir) != NULL) ? UCG(filemapdir) : "", result, winerror);
        return;
    }

    /* utils_create_init_event records WAIT_TIMEOUT when the wait for an init mutex times out */
    if(haswinerror && winerror == WAIT_TIMEOUT)
    {
        zend_error(E_CORE_WARNING, "ucache: the user cache could not be created, so the ucache functions fail: a process that shares it kept its initialization lock for %d seconds (error %d, Windows error %u)",
                   MAX_INIT_EVENT_WAIT / 1000, result, winerror);
        return;
    }

    if(haswinerror)
    {
        switch(winerror)
        {
            case ERROR_ACCESS_DENIED:
                cause = "access to its shared memory or locks was denied: processes of another account may use the same ucache.apppoolid (or APP_POOL_ID) and ucache.namesalt";
                break;
            case ERROR_COMMITMENT_LIMIT:
                cause = "the paging file has no room for it (the system commit limit)";
                break;
            default:
                break;
        }
    }

    if(cause != NULL)
    {
        zend_error(E_CORE_WARNING, "ucache: the user cache could not be created, so the ucache functions fail: %s (error %d, Windows error %u)",
                   cause, result, winerror);
    }
    else if(haswinerror)
    {
        zend_error(E_CORE_WARNING, "ucache: the user cache could not be created, so the ucache functions fail (error %d, Windows error %u)",
                   result, winerror);
    }
    else
    {
        zend_error(E_CORE_WARNING, "ucache: the user cache could not be created, so the ucache functions fail (error %d)", result);
    }
}

/* A prefix that makes valid PHP function names: letters, digits, underscores and the bytes */
/* 0x80 to 0xff, not starting with a digit. Empty is valid: the names are then "get", "set", ... */
static zend_bool prefix_is_valid(const char * prefix)
{
    size_t length = strlen(prefix);
    size_t index  = 0;

    if(length > PREFIX_LENGTH_MAXIMUM || (length > 0 && prefix[0] >= '0' && prefix[0] <= '9'))
    {
        return 0;
    }

    for(index = 0; index < length; index++)
    {
        unsigned char c = (unsigned char)prefix[index];

        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80))
        {
            return 0;
        }
    }

    return 1;
}

/* Registers ucache_functions with ucache.prefix in place of PREFIX_DEFAULT. Called from MINIT, */
/* where EG(current_module) is this module, so the functions belong to it (get_extension_funcs, */
/* reflection). An invalid prefix, or a name that another extension already uses, leaves the */
/* functions out, with a startup warning, rather than stop PHP from starting */
static int functions_register(int type)
{
    const char *          prefix         = (UCG(prefix) != NULL) ? UCG(prefix) : PREFIX_DEFAULT;
    size_t                prefix_length  = 0;
    size_t                default_length = sizeof(PREFIX_DEFAULT) - 1;
    size_t                count          = sizeof(ucache_functions) / sizeof(ucache_functions[0]) - 1;
    size_t                index          = 0;
    zend_function_entry * entries        = NULL;

    if(!prefix_is_valid(prefix))
    {
        zend_error(E_CORE_WARNING, "ucache.prefix \"%s\" is not a valid function name prefix: the ucache functions are not available", prefix);
        return FAILURE;
    }

    prefix_length = strlen(prefix);

    /* Zeroed: the entry after the last one is the end marker */
    entries = (zend_function_entry *)pecalloc(count + 1, sizeof(zend_function_entry), 1);

    for(index = 0; index < count; index++)
    {
        const char * suffix        = ucache_functions[index].fname + default_length;
        size_t       suffix_length = strlen(suffix);
        char *       name          = (char *)pemalloc(prefix_length + suffix_length + 1, 1);

        _ASSERT(strncmp(ucache_functions[index].fname, PREFIX_DEFAULT, default_length) == 0);

        memcpy(name, prefix, prefix_length);
        memcpy(name + prefix_length, suffix, suffix_length + 1);

        entries[index] = ucache_functions[index];
        entries[index].fname = name;
    }

    /* On a duplicate name, zend_register_functions warns and unregisters the ones it registered */
    if(zend_register_functions(NULL, entries, NULL, type) != SUCCESS)
    {
        for(index = 0; index < count; index++)
        {
            pefree((char *)entries[index].fname, 1);
        }

        pefree(entries, 1);
        return FAILURE;
    }

    ucache_registered_functions = entries;
    return SUCCESS;
}

/* Unregisters the functions that functions_register registered. Called from MSHUTDOWN */
static void functions_unregister(void)
{
    zend_function_entry * entry = NULL;

    if(ucache_registered_functions == NULL)
    {
        return;
    }

    zend_unregister_functions(ucache_registered_functions, -1, NULL);

    for(entry = ucache_registered_functions; entry->fname != NULL; entry++)
    {
        pefree((char *)entry->fname, 1);
    }

    pefree(ucache_registered_functions, 1);
    ucache_registered_functions = NULL;
}

PHP_MINIT_FUNCTION(ucache)
{
    int                result    = NONFATAL;
    zvcache_context *  pzcache   = NULL;

    ZEND_INIT_MODULE_GLOBALS(ucache, globals_initialize, globals_terminate);
    REGISTER_INI_ENTRIES();

    dprintverbose("start php_minit");

    /* The functions exist even where the cache is not created (CLI without enablecli): they fail */
    functions_register(type);

    /* If enablecli is set to 0, don't initialize when run with cli sapi */
    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        goto Finished;
    }

    /* First, before anything uses them: an empty ucache.apppoolid, ucache.filemapdir or ucache.namesalt is not set */
    /* ("ucache.apppoolid=", or none, off, no, false or null, which the INI parser makes empty). Otherwise every process */
    /* with an empty id would share one cache whatever APP_POOL_ID or its parent process, the file of the cache would go */
    /* to the root of the current drive, and an empty salt would keep a cache apart from the unsalted one although */
    /* ini_get() shows the same for both. The INI values themselves are left alone (see ucache_namesalt) */
    if(UCG(apppoolid) != NULL && UCG(apppoolid)[0] == '\0')
    {
        UCG(apppoolid) = NULL;
    }

    if(UCG(filemapdir) != NULL && UCG(filemapdir)[0] == '\0')
    {
        UCG(filemapdir) = NULL;
    }

    if(UCG(namesalt) != NULL && UCG(namesalt)[0] == '\0')
    {
        UCG(namesalt) = NULL;
    }

    /* Then the size, with its startup warning */
    size_validate();

    /* Truncate namesalt to 8 characters, in a copy (see ucache_namesalt) */
    if(UCG(namesalt) != NULL && strlen(UCG(namesalt)) > NAMESALT_LENGTH_MAXIMUM)
    {
        memcpy(ucache_namesalt, UCG(namesalt), NAMESALT_LENGTH_MAXIMUM);
        ucache_namesalt[NAMESALT_LENGTH_MAXIMUM] = '\0';

        UCG(namesalt) = ucache_namesalt;
    }

    /* Even if enabled is set to false, create cache */
    /* because scripts can selectively enable it */

    /* The warning of a failure gives the Windows error that the failing step recorded: start from none, since */
    /* error_glerror is kept for the whole process (see startup_failure_warning) */
    SetLastError(ERROR_SUCCESS);
    error_setlasterror();

    /* Call filemap global initialized. Terminate in MSHUTDOWN */
    result = filemap_global_initialize();
    if(FAILED(result))
    {
        goto Finished;
    }

    /*
     * Create user cache
     */

    /* Always create user cache as ucenabled can be set by script */
    result = zvcache_create(&pzcache);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* cachekey = 1 */
    result = zvcache_initialize(pzcache, 1, UCG(ucachesize));
    if(FAILED(result))
    {
        goto Finished;
    }

    UCG(zvucache) = pzcache;

    /* User cache is now operational */
    pzcache = NULL;

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in php_minit", result);

        /* PHP keeps running and the functions fail, as before, but the warning says why */
        startup_failure_warning(result);

        if(pzcache != NULL)
        {
            zvcache_terminate(pzcache);
            zvcache_destroy(pzcache);

            pzcache = NULL;
            UCG(zvucache) = NULL;
        }
    }

    dprintverbose("end php_minit");
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(ucache)
{
    dprintverbose("start php_mshutdown");

    /* If enablecli is 0, short circuit this call in cli mode */
    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        goto Finished;
    }

    if(UCG(zvucache) != NULL)
    {
        zvcache_terminate(UCG(zvucache));
        zvcache_destroy(UCG(zvucache));

        UCG(zvucache) = NULL;
    }

#ifdef ZTS
    ts_free_id(ucache_globals_id);
#else
    globals_terminate(&ucache_globals);
#endif /* ZTS */

    filemap_global_terminate();

Finished:

    functions_unregister();

    /* Unregister ini entries. globals_terminate will get */
    /* called by zend engine after this */
    UNREGISTER_INI_ENTRIES();

    dprintverbose("end php_mshutdown");
    return SUCCESS;
}

/* ucache.gc.interval > 0: the scavenger runs at the end of requests (see zvcache_scavenge_periodic) */
PHP_RSHUTDOWN_FUNCTION(ucache)
{
    if(UCG(ucgc_interval) > 0 && UCG(ucenabled) && UCG(zvucache) != NULL)
    {
        zvcache_scavenge_periodic(UCG(zvucache));
    }

    return SUCCESS;
}

PHP_MINFO_FUNCTION(ucache)
{
    dprintverbose("start php_minfo");

    php_info_print_table_start();

    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        php_info_print_table_row(2, "User cache", "disabled");
    }
    else if(UCG(zvucache) == NULL)
    {
        /* MINIT could not create it: its startup warning says why */
        php_info_print_table_row(2, "User cache", "unavailable");
    }
    else
    {
        php_info_print_table_row(2, "User cache", UCG(ucenabled) ? "enabled" : "disabled");
    }

    php_info_print_table_row(2, "Version", PHP_UCACHE_VERSION);
    php_info_print_table_row(2, "Build Date", __DATE__ " " __TIME__);

    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();

    dprintverbose("end php_minfo");
    return;
}

/* Stores a bool in a by-reference "success" argument. The bool is stored first and the value it */
/* held is released after, so that a destructor run by the release sees the bool. Since PHP 7.4, */
/* a typed reference (a reference to a typed property) checks the bool against its types first, */
/* which can coerce it or throw a TypeError. Not the ZEND_TRY_ASSIGN_* macros nor */
/* zend_try_assign_typed_ref_bool(): in PHP 7.4 to 8.4 they release the old value before storing, */
/* and a destructor that writes the variable then frees its object twice; and */
/* ZEND_TRY_ASSIGN_REF_BOOL stores true in a typed reference whatever the value */
static void set_success(zval * success, zend_bool value)
{
#if PHP_VERSION_ID >= 70400
    zval old;
    zval val;

    ZVAL_BOOL(&val, value);

    if(Z_ISREF_P(success))
    {
        zend_reference * ref = Z_REF_P(success);

        /* A typed reference: check (and coerce) the bool before touching the old value */
        if(ZEND_REF_HAS_TYPE_SOURCES(ref)
            && !zend_verify_ref_assignable_zval(ref, &val, ZEND_ARG_USES_STRICT_TYPES()))
        {
            zval_ptr_dtor(&val);
            return;
        }

        success = &ref->val;
    }

    /* Assign before releasing, so that a destructor run by the release sees the new value */
    ZVAL_COPY_VALUE(&old, success);
    ZVAL_COPY_VALUE(success, &val);
    zval_ptr_dtor(&old);
#else
    zval old;

    ZVAL_DEREF(success);

    /* Assign before releasing, so that a destructor run by the release sees the new value */
    ZVAL_COPY_VALUE(&old, success);
    ZVAL_BOOL(success, value);
    zval_ptr_dtor(&old);
#endif
}

/* The table that the array forms of ucache_get(), ucache_set(), ucache_add() and ucache_delete() */
/* walk for the array *pzarray. $GLOBALS before PHP 8.1 is the symbol table itself, which nothing */
/* holds (its zval is not refcounted): the user code that getting or storing a key can run */
/* (__sleep(), __serialize(), __wakeup(), __unserialize(), destructors) can add a global, which */
/* reallocates the table under the loop. So they walk a copy of it, made before any of that runs, */
/* which takes the values of the variables (IS_INDIRECT slots) and leaves out those that are not */
/* set, as $GLOBALS is from PHP 8.1 on; ucache_delete(), which runs no user code, walks one too, */
/* so that the four read such an array alike. *psnapshot receives the copy: the caller */
/* initializes it as UNDEF and releases it with zval_ptr_dtor() */
static HashTable * array_form_table(zval * pzarray, zval * psnapshot)
{
    HashTable * htable = Z_ARRVAL_P(pzarray);

#if PHP_VERSION_ID < 80100
    if(htable == &EG(symbol_table))
    {
        ZVAL_ARR(psnapshot, zend_array_dup(htable));
        htable = Z_ARRVAL_P(psnapshot);
    }
#endif

    return htable;
}

/* Functions */

/* Every function parses its arguments first: on PHP 8, a call that does not match the arginfo */
/* must throw (ArgumentCountError, TypeError), even when the cache is switched off */

PHP_FUNCTION(ucache_meminfo)
{
    alloc_info   info;

    if(zend_parse_parameters_none() == FAILURE)
    {
        return;
    }

    if(UCG(zvucache) == NULL)
    {
        return;
    }

    alloc_getinfo(UCG(zvucache)->zvalloc, &info);

    array_init(return_value);

    add_assoc_long(return_value, "memory_total", info.total_size);
    add_assoc_long(return_value, "memory_free", info.free_size);
    add_assoc_long(return_value, "num_used_blks", info.usedcount);
    add_assoc_long(return_value, "num_free_blks", info.freecount);
    add_assoc_long(return_value, "memory_overhead", info.mem_overhead);

    return;
}

PHP_FUNCTION(ucache_get)
{
    int          result    = NONFATAL;
    zval *       pzkey     = NULL;
    zval *       success   = NULL;
    HashTable *  htable    = NULL;
    zval *       hentry    = NULL;
    zval         nentry;
    zval *       pnentry   = &nentry;
    char *       key       = NULL;
    size_t       keylen    = 0;
    zend_string * zkey     = NULL;
    char         digits[MAX_ARRAY_INDEX_DIGITS] = {0};
    zend_bool    isarray   = 0;
    zval         snapshot;

    /* See array_form_table; released at Finished */
    ZVAL_UNDEF(&snapshot);

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z|z", &pzkey, &success) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* If the user cache is disabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(success != NULL)
    {
        set_success(success, 0);

        /* A typed reference that cannot hold a bool threw a TypeError */
        if(EG(exception))
        {
            result = FATAL_INVALID_ARGUMENT;
            goto Finished;
        }
    }

    /* Convert zval to string zval */
    if(Z_TYPE_P(pzkey) != IS_STRING && Z_TYPE_P(pzkey) != IS_ARRAY)
    {
        convert_to_string(pzkey);

        /* An object without __toString(): PHP 7.4 and later throw an Error, PHP 7.2 and 7.3 raise */
        /* a recoverable error that an error handler can turn into an exception */
        if(EG(exception))
        {
            result = FATAL_INVALID_ARGUMENT;
            goto Finished;
        }
    }

    if(Z_TYPE_P(pzkey) == IS_STRING)
    {
        result = zvcache_get(UCG(zvucache), Z_STRVAL_P(pzkey), &return_value);
        if(FAILED(result))
        {
            goto Finished;
        }
    }
    else if(Z_TYPE_P(pzkey) == IS_ARRAY)
    {
        array_init(return_value);
        isarray = 1;
        htable = array_form_table(pzkey, &snapshot);
        ZEND_HASH_FOREACH_VAL(htable, hentry)
        {
            /* An element can be a reference (a foreach by reference leaves them in the array) */
            ZVAL_DEREF(hentry);

            if(Z_TYPE_P(hentry) != IS_STRING && Z_TYPE_P(hentry) != IS_LONG)
            {
                php_error_docref(NULL, E_WARNING, "key array elements can only be string or long");

                result = WARNING_ZVCACHE_ARGUMENT;
                goto Finished;
            }

            if(Z_TYPE_P(hentry) == IS_LONG)
            {
                keylen = snprintf(digits, MAX_ARRAY_INDEX_DIGITS, ZEND_LONG_FMT, Z_LVAL_P(hentry));
                key = digits;
            }
            else
            {
                _ASSERT(Z_TYPE_P(hentry) == IS_STRING);

                /* Hold the string: unserializing a stored object runs user code, which can assign */
                /* another value to the variable that a referenced element shares */
                zkey   = zend_string_copy(Z_STR_P(hentry));
                key    = ZSTR_VAL(zkey);
                keylen = ZSTR_LEN(zkey);
            }

            result = zvcache_get(UCG(zvucache), key, &pnentry);

            /* Ignore failures and try getting values of other keys */
            if(SUCCEEDED(result))
            {
                /* Symtable insert: numeric keys become integer keys, as in any PHP array */
                add_assoc_zval_ex(return_value, key, keylen, &nentry);
            }

            if(zkey != NULL)
            {
                zend_string_release(zkey);
                zkey = NULL;
            }

            result = NONFATAL;
            key    = NULL;
            keylen = 0;

            /* Unserializing a stored object threw (__wakeup, __unserialize) */
            if(EG(exception))
            {
                break;
            }

            /* The array of values can pass memory_limit (see zvcache_over_memory_limit) */
            if(zvcache_over_memory_limit())
            {
                result = FATAL_OUT_OF_LMEMORY;
                goto Finished;
            }
        } ZEND_HASH_FOREACH_END();
    }
    else
    {
        _ASSERT(FALSE);
    }

    if(success != NULL && !EG(exception))
    {
        set_success(success, 1);
    }

Finished:

    zval_ptr_dtor(&snapshot);

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_get", result);

        /* RETURN_FALSE does not release the array of values built so far */
        if(isarray)
        {
            zval_ptr_dtor(return_value);
        }

        RETURN_FALSE;
    }
}

/* ucache_set() and ucache_add(), which differ only in isadd: an add fails on a key that exists */
static void set_or_add(INTERNAL_FUNCTION_PARAMETERS, unsigned char isadd)
{
    int           result   = NONFATAL;
    zval *        pzkey    = NULL;
    zval *        pzval    = NULL;
    unsigned int  ttl      = 0;
    zend_long     lttl     = 0;
    HashTable *   htable   = NULL;
    zval *        hentry   = NULL;
    zend_ulong    num_key;
    zend_string * key      = NULL;
    int           keylen   = 0;
    char          digits[MAX_ARRAY_INDEX_DIGITS] = {0};
    size_t        peak0    = 0;
    unsigned char stop     = 0;
    zval          snapshot;

    /* "l" stores a full zend_long, so parse into lttl and keep ttl the entry's unsigned int */
    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z|zl", &pzkey, &pzval, &lttl) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* If the user cache is disabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    /* Negative ttl and resource values are not allowed */
    if(lttl < 0)
    {
        php_error_docref(NULL, E_WARNING, "ttl cannot be less than 0");

        result = WARNING_ZVCACHE_ARGUMENT;
        goto Finished;
    }

    /* A ttl beyond the entry's unsigned int (4294967295 seconds, about 136 years) is its maximum */
    ttl = ((zend_ulong)lttl > UINT_MAX) ? UINT_MAX : (unsigned int)lttl;

    if(pzval && Z_TYPE_P(pzval) == IS_RESOURCE)
    {
        php_error_docref(NULL, E_WARNING, "value cannot be a resource");

        result = WARNING_ZVCACHE_RESCOPYIN;
        goto Finished;
    }

    if(Z_TYPE_P(pzkey) != IS_STRING && Z_TYPE_P(pzkey) != IS_ARRAY)
    {
        convert_to_string(pzkey);

        /* An object without __toString() (see ucache_get) */
        if(EG(exception))
        {
            result = FATAL_INVALID_ARGUMENT;
            goto Finished;
        }
    }

    if(Z_TYPE_P(pzkey) == IS_STRING)
    {
        /* Blank string as key is not allowed */
        if(Z_STRLEN_P(pzkey) == 0 || *(Z_STRVAL_P(pzkey)) == '\0')
        {
            php_error_docref(NULL, E_WARNING, "key cannot be blank string");

            result = WARNING_ZVCACHE_ARGUMENT;
            goto Finished;
        }

        /* When first argument is string, value is required */
        if(pzval == NULL || Z_STRLEN_P(pzkey) > 4096)
        {
            result = WARNING_ZVCACHE_ARGUMENT;
            goto Finished;
        }

        result = zvcache_set(UCG(zvucache), Z_STRVAL_P(pzkey), pzval, ttl, isadd);
        if(FAILED(result))
        {
            goto Finished;
        }

        ZVAL_BOOL(return_value, 1);
    }
    else if(Z_TYPE_P(pzkey) == IS_ARRAY)
    {
        array_init(return_value);
        ZVAL_UNDEF(&snapshot);
        htable = array_form_table(pzkey, &snapshot);

        /* See zvcache_peak_over_memory_limit */
        peak0 = zend_memory_peak_usage(1);

        /* _IND: before PHP 8.1, an array that array_merge(), array_slice(), ... made of $GLOBALS */
        /* holds slots of the symbol table (IS_INDIRECT), which point to the variables: the loop */
        /* takes their values, and leaves out the slots of the variables that are not set */
        ZEND_HASH_FOREACH_KEY_VAL_IND(htable, num_key, key, hentry)
        {
            char alloc_str = 0;

            /* We are taking care of long keys properly */
            if(!key)
            {
                /* Convert num_key to string and use that instead (integer keys are signed) */
                keylen = snprintf(digits, MAX_ARRAY_INDEX_DIGITS, ZEND_LONG_FMT, (zend_long)num_key);
                key = zend_string_init(digits, keylen, 0);
                alloc_str = 1;
            }

            if(ZSTR_LEN(key) > 4096 || Z_TYPE_P(hentry) == IS_RESOURCE)
            {
                result = WARNING_ZVCACHE_ARGUMENT;
            }
            else
            {
                result = zvcache_set(UCG(zvucache), ZSTR_VAL(key), hentry, ttl, isadd);
            }

            if(FAILED(result))
            {
                add_assoc_long_ex(return_value, ZSTR_VAL(key), ZSTR_LEN(key), -1);

                /* memory_limit passed with delayed OOM: PHP raises the fatal error when this
                 * call returns, store nothing more (see zvcache_peak_over_memory_limit) */
                if(result == FATAL_OUT_OF_LMEMORY && zvcache_peak_over_memory_limit(peak0))
                {
                    stop = 1;
                }
            }

            if (alloc_str == 1)
            {
                zend_string_release(key);
                alloc_str = 0;
            }

            result  = NONFATAL;
            key     = NULL;

            /* Serializing an object threw (Closure, __sleep, __serialize), or memory_limit was
             * passed (see above): stop there */
            if(EG(exception) || stop)
            {
                break;
            }
        } ZEND_HASH_FOREACH_END();

        zval_ptr_dtor(&snapshot);
    }
    else
    {
        _ASSERT(FALSE);
    }

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in %s", result, isadd ? "ucache_add" : "ucache_set");

        /* Only an add gets WARNING_ZVCACHE_EXISTS */
        if(result == WARNING_ZVCACHE_EXISTS)
        {
            php_error_docref(NULL, E_WARNING, "function called with a key which already exists");
        }

        RETURN_FALSE;
    }
}

PHP_FUNCTION(ucache_set)
{
    set_or_add(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}

PHP_FUNCTION(ucache_add)
{
    set_or_add(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}

PHP_FUNCTION(ucache_delete)
{
    int           result   = NONFATAL;
    zval *        pzkey    = NULL;
    HashTable *   htable   = NULL;
    zval *        hentry   = NULL;
    char *        key      = NULL;
    char          digits[MAX_ARRAY_INDEX_DIGITS] = {0};
    zend_bool     isarray  = 0;
    zval          snapshot;

    /* See array_form_table; released at Finished */
    ZVAL_UNDEF(&snapshot);

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z", &pzkey) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* If the user cache is disabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(Z_TYPE_P(pzkey) != IS_STRING && Z_TYPE_P(pzkey) != IS_ARRAY)
    {
        convert_to_string(pzkey);

        /* An object without __toString() (see ucache_get) */
        if(EG(exception))
        {
            result = FATAL_INVALID_ARGUMENT;
            goto Finished;
        }
    }

    if(Z_TYPE_P(pzkey) == IS_STRING)
    {
        result = zvcache_delete(UCG(zvucache), Z_STRVAL_P(pzkey));
        if(FAILED(result))
        {
            goto Finished;
        }

        ZVAL_BOOL(return_value, 1);
    }
    else if(Z_TYPE_P(pzkey) == IS_ARRAY)
    {
        array_init(return_value);
        isarray = 1;
        htable = array_form_table(pzkey, &snapshot);
        ZEND_HASH_FOREACH_VAL(htable, hentry)
        {
            /* An element can be a reference (see ucache_get); the string, not the reference, is returned */
            ZVAL_DEREF(hentry);

            if(Z_TYPE_P(hentry) != IS_STRING && Z_TYPE_P(hentry) != IS_LONG)
            {
                php_error_docref(NULL, E_WARNING, "key array elements can only be string or long");

                result = WARNING_ZVCACHE_ARGUMENT;
                goto Finished;
            }

            /* Don't convert hentry in place: the key array belongs to the */
            /* caller and can be shared or immutable (opcache) */
            if(Z_TYPE_P(hentry) == IS_LONG)
            {
                snprintf(digits, MAX_ARRAY_INDEX_DIGITS, ZEND_LONG_FMT, Z_LVAL_P(hentry));
                key = digits;
            }
            else
            {
                _ASSERT(Z_TYPE_P(hentry) == IS_STRING);
                key = Z_STRVAL_P(hentry);
            }

            result = zvcache_delete(UCG(zvucache), key);
            if(SUCCEEDED(result))
            {
                if(Z_TYPE_P(hentry) == IS_LONG)
                {
                    /* Deleted keys are returned as strings */
                    add_next_index_string(return_value, digits);
                }
                else
                {
                    add_next_index_zval(return_value, hentry);
                    Z_TRY_ADDREF_P(hentry);
                }
            }

            key = NULL;
        } ZEND_HASH_FOREACH_END();
    }
    else
    {
        _ASSERT(FALSE);
    }

Finished:

    zval_ptr_dtor(&snapshot);

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_delete", result);

        /* RETURN_FALSE does not release the array of deleted keys built so far */
        if(isarray)
        {
            zval_ptr_dtor(return_value);
        }

        RETURN_FALSE;
    }
}

PHP_FUNCTION(ucache_clear)
{
    int result = NONFATAL;

    /* PHP 8 throws an ArgumentCountError, PHP 7 warns (or throws too, under strict_types) */
    if(zend_parse_parameters_none() == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* If the user cache is disabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    result = zvcache_clear(UCG(zvucache));
    if(FAILED(result))
    {
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_clear", result);

        RETURN_FALSE;
    }

    RETURN_TRUE;
}

PHP_FUNCTION(ucache_exists)
{
    int           result = NONFATAL;
    char *        key    = NULL;
    size_t        keylen = 0;
    unsigned char exists = 0;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "s", &key, &keylen) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* If the user cache is disabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    result = zvcache_exists(UCG(zvucache), key, &exists);
    if(FAILED(result))
    {
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_exists", result);

        RETURN_FALSE;
    }

    if(!exists)
    {
        RETURN_FALSE;
    }

    RETURN_TRUE;
}

PHP_FUNCTION(ucache_info)
{
    int                  result      = NONFATAL;
    zend_llist *         plist       = NULL;
    zvcache_info_entry * peinfo      = NULL;
    zval                 zfentries;
    zval                 zfentry;
    zend_ulong           index       = 1;
    const char *         valuetype   = NULL;
    zvcache_info         zvinfo      = {0};
    zend_bool            summaryonly = 0;   /* "b" stores a zend_bool (bool since PHP 8.0) */
    char *               entrykey    = NULL;
    size_t               entrylen    = 0;
    zend_long            offset      = 0;
    zend_long            limit       = 0;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "|bs!ll", &summaryonly, &entrykey, &entrylen, &offset, &limit) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    /* Negative offset and limit are not allowed */
    if(offset < 0 || limit < 0)
    {
        php_error_docref(NULL, E_WARNING, "offset and limit cannot be less than 0");

        result = WARNING_ZVCACHE_ARGUMENT;
        goto Finished;
    }

    /* The full listing is indexed by position: the entries after offset start at offset + 1 */
    if(entrykey == NULL)
    {
        index = (zend_ulong)offset + 1;
    }

    plist = (zend_llist *)alloc_emalloc(sizeof(zend_llist));
    if(plist == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    result = zvcache_list(UCG(zvucache), summaryonly, entrykey, (zend_ulong)offset, (zend_ulong)limit, &zvinfo, plist);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Fill the array and then call zend_llist_destroy */
    array_init(return_value);
    add_assoc_long(return_value, "total_cache_uptime", zvinfo.initage);
    /* Always false: the user cache is always the one shared by the processes of the pool */
    add_assoc_bool(return_value, "is_local_cache", 0);
    add_assoc_long(return_value, "total_item_count", zvinfo.itemcount);
    add_assoc_long(return_value, "total_hit_count", zvinfo.hitcount);
    add_assoc_long(return_value, "total_miss_count", zvinfo.misscount);

    /* The scavenger: seconds since its last step (null before the first one), what that step removed, */
    /* what all the steps removed since the cache was created, and how many of those were evicted */
    if(zvinfo.scavenged)
    {
        add_assoc_long(return_value, "last_scavenge_seconds", zvinfo.scage);
    }
    else
    {
        add_assoc_null(return_value, "last_scavenge_seconds");
    }
    add_assoc_long(return_value, "last_scavenge_count", zvinfo.sccount);
    add_assoc_long(return_value, "total_scavenged_count", (zend_long)zvinfo.scremoved);
    add_assoc_long(return_value, "total_evicted_count", (zend_long)zvinfo.scevicted);

    array_init(&zfentries);

    peinfo = (zvcache_info_entry *)zend_llist_get_first(plist);
    while(peinfo != NULL)
    {
        /* The array of many entries can pass memory_limit (see zvcache_over_memory_limit) */
        if(zvcache_over_memory_limit())
        {
            result = FATAL_OUT_OF_LMEMORY;
            break;
        }

        array_init(&zfentry);

        valuetype = utils_get_typename(peinfo->type);

        add_assoc_string(&zfentry, "key_name", peinfo->key);
        add_assoc_string(&zfentry, "value_type", (char *)valuetype);
        add_assoc_long(&zfentry, "value_size", peinfo->sizeb);
        add_assoc_long(&zfentry, "ttl_seconds", peinfo->ttl);
        add_assoc_long(&zfentry, "age_seconds", peinfo->age);
        add_assoc_long(&zfentry, "hitcount", peinfo->hitcount);

        add_index_zval(&zfentries, index++, &zfentry);
        peinfo = (zvcache_info_entry *)zend_llist_get_next(plist);
    }

    zend_llist_destroy(plist);
    alloc_efree(plist);
    plist = NULL;

    if(FAILED(result))
    {
        /* RETURN_FALSE does not release the arrays built so far */
        zval_ptr_dtor(&zfentries);
        zval_ptr_dtor(return_value);
        goto Finished;
    }

    add_assoc_zval(return_value, "ucache_entries", &zfentries);

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_info", result);

        if(plist != NULL)
        {
            alloc_efree(plist);
            plist = NULL;
        }

        RETURN_FALSE;
    }

    return;
}

/* ucache_inc() and ucache_dec(), which differ only in isdec: a dec adds -delta */
static void inc_or_dec(INTERNAL_FUNCTION_PARAMETERS, unsigned char isdec)
{
    int          result  = NONFATAL;
    char *       key     = NULL;
    size_t       keylen  = 0;
    zend_long    delta   = 1;
    zend_long    newval  = 0;
    zval *       success = NULL;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "s|lz", &key, &keylen, &delta, &success) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* If the user cache is disabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(success != NULL)
    {
        set_success(success, 0);

        /* A typed reference that cannot hold a bool threw a TypeError */
        if(EG(exception))
        {
            result = FATAL_INVALID_ARGUMENT;
            goto Finished;
        }
    }

    /* Wraps around like zvcache_change(), without signed overflow: -ZEND_LONG_MIN is ZEND_LONG_MIN */
    if(isdec)
    {
        delta = (zend_long)(0 - (zend_ulong)delta);
    }

    result = zvcache_change(UCG(zvucache), key, delta, &newval);
    if(FAILED(result))
    {
        goto Finished;
    }

    if(success != NULL)
    {
        set_success(success, 1);
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in %s", result, isdec ? "ucache_dec" : "ucache_inc");

        if(result == WARNING_ZVCACHE_NOTLONG)
        {
            php_error_docref(NULL, E_WARNING, "function can only be called for key whose value is long");
        }

        RETURN_FALSE;
    }

    RETURN_LONG(newval);
}

PHP_FUNCTION(ucache_inc)
{
    inc_or_dec(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}

PHP_FUNCTION(ucache_dec)
{
    inc_or_dec(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}

PHP_FUNCTION(ucache_cas)
{
    int          result = NONFATAL;
    char *       key    = NULL;
    size_t       keylen = 0;
    zend_long    cvalue = 0;
    zend_long    nvalue = 0;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "sll", &key, &keylen, &cvalue, &nvalue) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* If the user cache is disabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    result = zvcache_compswitch(UCG(zvucache), key, cvalue, nvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        if(result == WARNING_ZVCACHE_CASNEQ)
        {
            RETURN_FALSE;
        }

        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_cas", result);

        if(result == WARNING_ZVCACHE_NOTLONG)
        {
            php_error_docref(NULL, E_WARNING, "function can only be called for key whose value is long");
        }

        RETURN_FALSE;
    }

    RETURN_TRUE;
}

#ifdef UCACHE_TEST
PHP_FUNCTION(ucache_lasterror)
{
    if(UCG(zvucache) == NULL)
    {
        RETURN_LONG(0);
    }

    RETURN_LONG(UCG(uclasterror));
}

PHP_FUNCTION(ucache_runtests)
{
    dprintverbose("start ucache_runtests");

    lock_runtest();
    filemap_runtest();
    alloc_runtest();

    dprintverbose("end ucache_runtests");
    return;
}
#endif /* UCACHE_TEST */

