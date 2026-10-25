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

/* START OF PHP EXTENSION MACROS STUFF */
PHP_MINIT_FUNCTION(ucache);
PHP_MSHUTDOWN_FUNCTION(ucache);
PHP_MINFO_FUNCTION(ucache);

/* Info functions exposed by this extension */
PHP_FUNCTION(ucache_meminfo);

/* ZVAL cache functions matching other caches */
PHP_FUNCTION(ucache_get);
PHP_FUNCTION(ucache_set);
PHP_FUNCTION(ucache_add);
PHP_FUNCTION(ucache_delete);
PHP_FUNCTION(ucache_clear);
PHP_FUNCTION(ucache_gc);
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
    ZEND_ARG_INFO(1, success)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_set, 0, 0, 2)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, value)
    ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_add, 0, 0, 2)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, value)
    ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_delete, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_clear, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_gc, 0, 0, 3)
    ZEND_ARG_INFO(0, interval)
    ZEND_ARG_INFO(0, step)
    ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_exists, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_info, 0, 0, 0)
    ZEND_ARG_INFO(0, summaryonly)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_inc, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, delta)
    ZEND_ARG_INFO(1, success)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_dec, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, delta)
    ZEND_ARG_INFO(1, success)
ZEND_END_ARG_INFO()

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

/* Put all user defined functions here */
zend_function_entry ucache_functions[] = {
    PHP_FE(ucache_meminfo, arginfo_ucache_meminfo)
    PHP_FE(ucache_get, arginfo_ucache_get)
    PHP_FE(ucache_set, arginfo_ucache_set)
    PHP_FE(ucache_add, arginfo_ucache_add)
    PHP_FE(ucache_delete, arginfo_ucache_delete)
    PHP_FE(ucache_clear, arginfo_ucache_clear)
    PHP_FE(ucache_gc, arginfo_ucache_gc)
    PHP_FE(ucache_exists, arginfo_ucache_exists)
    PHP_FE(ucache_info, arginfo_ucache_info)
    PHP_FE(ucache_inc, arginfo_ucache_inc)
    PHP_FE(ucache_dec, arginfo_ucache_dec)
    PHP_FE(ucache_cas, arginfo_ucache_cas)
#ifdef UCACHE_TEST
    PHP_FE(ucache_lasterror, arginfo_ucache_lasterror)
    PHP_FE(ucache_runtests, arginfo_ucache_runtests)
#endif
    {NULL, NULL, NULL}
};

/* ucache_module_entry */
zend_module_entry ucache_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_UCACHE_EXTNAME,
    ucache_functions,        /* Functions */
    PHP_MINIT(ucache),       /* MINIT */
    PHP_MSHUTDOWN(ucache),   /* MSHUTDOWN */
    NULL,                    /* RINIT */
    NULL,                    /* RSHUTDOWN */
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
/* index 2 */  STD_PHP_INI_ENTRY("ucache.size", "8", PHP_INI_SYSTEM, OnUpdateLong, ucachesize, zend_ucache_globals, ucache_globals)
/* index 3 */  STD_PHP_INI_ENTRY("ucache.namesalt", NULL, PHP_INI_SYSTEM, OnUpdateString, namesalt, zend_ucache_globals, ucache_globals)
/* index 4 */  STD_PHP_INI_ENTRY("ucache.localheap", "0", PHP_INI_SYSTEM, OnUpdateBool, localheap, zend_ucache_globals, ucache_globals)
/* index 5 */  STD_PHP_INI_ENTRY("ucache.apppoolid", NULL, PHP_INI_SYSTEM, OnUpdateString, apppoolid, zend_ucache_globals, ucache_globals)
/* index 6 */  STD_PHP_INI_ENTRY("ucache.filemapdir", NULL, PHP_INI_SYSTEM, OnUpdateString, filemapdir, zend_ucache_globals, ucache_globals)
/* index 7 */  STD_PHP_INI_ENTRY("ucache.debuglevel", "0", PHP_INI_ALL, ucache_modify_debuglevel, debuglevel, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.threshold", "0", PHP_INI_SYSTEM, OnUpdateLong, ucgc_threshold, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.interval", "10", PHP_INI_SYSTEM, OnUpdateLong, ucgc_interval, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.step", "16", PHP_INI_SYSTEM, OnUpdateLong, ucgc_step, zend_ucache_globals, ucache_globals)
/* index --*/  STD_PHP_INI_ENTRY("ucache.gc.ttl", "0", PHP_INI_SYSTEM, OnUpdateLong, ucgc_ttl, zend_ucache_globals, ucache_globals)
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
    UCG(ucachesize)  = 8;    /* User cache size is 8 MB by default */
    UCG(ucgc_threshold) = 0;   /* Automatic scavenging disabled by default */
    UCG(ucgc_interval)  = 10;   /* Minimum secs between forced scavenger runs */
    UCG(ucgc_ttl)       = 0;   /* Scavenger uses each entry's own ttl */
    UCG(ucgc_step)      = 16;   /* Hash buckets scanned per scavenger run */
    UCG(debuglevel)  = 0;    /* Debug dump not enabled by default */
    UCG(namesalt)    = NULL; /* Salt to use in names used by ucache */
    UCG(localheap)   = 0;    /* Local heap is disabled by default */
    UCG(apppoolid)   = NULL; /* Use this application id */
    UCG(filemapdir)  = NULL; /* Directory where temp filemap files should be created */

    UCG(uclasterror) = 0;    /* Last error returned by user cache */
    UCG(zvucache)    = NULL; /* zvcache_context for user zvals */
    UCG(zvcopied)    = NULL; /* HashTable which helps with refcounting */
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

PHP_MINIT_FUNCTION(ucache)
{
    int                result    = NONFATAL;
    zvcache_context *  pzcache   = NULL;
    zend_ini_entry *   pinientry = NULL;

    ZEND_INIT_MODULE_GLOBALS(ucache, globals_initialize, globals_terminate);
    REGISTER_INI_ENTRIES();

    dprintverbose("start php_minit");

    /* If enablecli is set to 0, don't initialize when run with cli sapi */
    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        goto Finished;
    }

    /* set up the app pool ID */
    UCG(apppoolid) = utils_get_apppool_name();

    /* Compare value of globals with minimum and maximum allowed */
    UCG(ucachesize)  = (UCG(ucachesize)  < ZCACHE_SIZE_MINIMUM) ? ZCACHE_SIZE_MINIMUM : UCG(ucachesize);
    UCG(ucachesize)  = (UCG(ucachesize)  > ZCACHE_SIZE_MAXIMUM) ? ZCACHE_SIZE_MAXIMUM : UCG(ucachesize);

    /* Truncate namesalt to 8 characters */
    if(UCG(namesalt) != NULL && strlen(UCG(namesalt)) > NAMESALT_LENGTH_MAXIMUM)
    {
        // TODO: instead of stomping on memory we don't own, we should just
        // TODO: allocate off a duplicate and truncate the value.  Maybe even
        // TODO: have a static array we use whenever this happens?

        pinientry = zend_hash_str_find_ptr(EG(ini_directives), "ucache.namesalt", sizeof("ucache.namesalt")-1);
        _ASSERT(pinientry != NULL);

        *(ZSTR_VAL(pinientry->value) + NAMESALT_LENGTH_MAXIMUM) = 0;
        ZSTR_LEN(pinientry->value) = NAMESALT_LENGTH_MAXIMUM;

        /* since we touched it, we should be polite and clear the hash value. */
        zend_string_forget_hash_val(pinientry->value);

        /* UCG(namesalt) is already pointing to pinientry->value */
    }

    /* Even if enabled is set to false, create cache */
    /* because scripts can selectively enable it */

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

    /* islocal = 0, cachekey = 1 */
    result = zvcache_initialize(pzcache, 0, 1, UCG(ucachesize));
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Create hashtable zvcopied */
    UCG(zvcopied) = (HashTable *)alloc_pemalloc(sizeof(HashTable));
    if(UCG(zvcopied) == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
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

        if(pzcache != NULL)
        {
            if(UCG(zvcopied) != NULL)
            {
                alloc_pefree(UCG(zvcopied));
                UCG(zvcopied) = NULL;
            }

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

    if(UCG(zvcopied) != NULL)
    {
        alloc_pefree(UCG(zvcopied));
        UCG(zvcopied) = NULL;
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

    /* Unregister ini entries. globals_terminate will get */
    /* called by zend engine after this */
    UNREGISTER_INI_ENTRIES();

    dprintverbose("end php_mshutdown");
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

/* Functions */
PHP_FUNCTION(ucache_meminfo)
{
    int          result = NONFATAL;
    alloc_info * pinfo  = NULL;

    if(UCG(zvucache) == NULL)
    {
        goto Finished;
    }

    result = alloc_getinfo(UCG(zvucache)->zvalloc, &pinfo);
    if(FAILED(result))
    {
        goto Finished;
    }

    array_init(return_value);

    add_assoc_long(return_value, "memory_total", pinfo->total_size);
    add_assoc_long(return_value, "memory_free", pinfo->free_size);
    add_assoc_long(return_value, "num_used_blks", pinfo->usedcount);
    add_assoc_long(return_value, "num_free_blks", pinfo->freecount);
    add_assoc_long(return_value, "memory_overhead", pinfo->mem_overhead);

Finished:

    if(pinfo != NULL)
    {
        alloc_freeinfo(pinfo);
        pinfo = NULL;
    }

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
    char         digits[MAX_ARRAY_INDEX_DIGITS] = {0};

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z|z", &pzkey, &success) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(success != NULL)
    {
        while (Z_TYPE_P(success) == IS_REFERENCE) {
            success = Z_REFVAL_P(success);
        }

        ZVAL_BOOL(success, 0);
    }

    /* Convert zval to string zval */
    if(Z_TYPE_P(pzkey) != IS_STRING && Z_TYPE_P(pzkey) != IS_ARRAY)
    {
        convert_to_string(pzkey);
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
        htable = Z_ARRVAL_P(pzkey);
        ZEND_HASH_FOREACH_VAL(htable, hentry)
        {
            if(Z_TYPE_P(hentry) != IS_STRING && Z_TYPE_P(hentry) != IS_LONG)
            {
                php_error_docref(NULL, E_WARNING, "key array elements can only be string or long");

                result = WARNING_ZVCACHE_ARGUMENT;
                goto Finished;
            }

            if(Z_TYPE_P(hentry) == IS_LONG)
            {
                keylen = snprintf(digits, MAX_ARRAY_INDEX_DIGITS, "%ld", Z_LVAL_P(hentry));
                key = digits;
            }
            else
            {
                _ASSERT(Z_TYPE_P(hentry) == IS_STRING);

                key = Z_STRVAL_P(hentry);
                keylen = Z_STRLEN_P(hentry);
            }

            result = zvcache_get(UCG(zvucache), key, &pnentry);

            /* Ignore failures and try getting values of other keys */
            if(SUCCEEDED(result))
            {
                zend_hash_str_add(Z_ARRVAL_P(return_value), key, keylen, &nentry);
            }

            result = NONFATAL;
            key    = NULL;
            keylen = 0;
        } ZEND_HASH_FOREACH_END();
    }
    else
    {
        _ASSERT(FALSE);
    }

    if(success != NULL)
    {
        ZVAL_BOOL(success, 1);
    }

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_get", result);

        RETURN_FALSE;
    }
}

PHP_FUNCTION(ucache_set)
{
    int           result   = NONFATAL;
    zval *        pzkey    = NULL;
    zval *        pzval    = NULL;
    int           ttl      = 0;
    zend_long     lttl     = 0;
    HashTable *   htable   = NULL;
    zval *        hentry   = NULL;
    zend_ulong    num_key;
    zend_string * key      = NULL;
    int           keylen   = 0;
    char          digits[MAX_ARRAY_INDEX_DIGITS] = {0};

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    /* "l" stores a full zend_long, so parse into lttl and keep ttl an int */
    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z|zl", &pzkey, &pzval, &lttl) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    ttl = (int)lttl;

    /* Negative ttl and resource values are not allowed */
    if(ttl < 0)
    {
        php_error_docref(NULL, E_WARNING, "ttl cannot be less than 0");

        result = WARNING_ZVCACHE_ARGUMENT;
        goto Finished;
    }

    if(pzval && Z_TYPE_P(pzval) == IS_RESOURCE)
    {
        php_error_docref(NULL, E_WARNING, "value cannot be a resource");

        result = WARNING_ZVCACHE_RESCOPYIN;
        goto Finished;
    }

    if(Z_TYPE_P(pzkey) != IS_STRING && Z_TYPE_P(pzkey) != IS_ARRAY)
    {
        convert_to_string(pzkey);
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

        /* isadd = 0 */
        result = zvcache_set(UCG(zvucache), Z_STRVAL_P(pzkey), pzval, ttl, 0);
        if(FAILED(result))
        {
            goto Finished;
        }

        ZVAL_BOOL(return_value, 1);
    }
    else if(Z_TYPE_P(pzkey) == IS_ARRAY)
    {
        array_init(return_value);
        htable = Z_ARRVAL_P(pzkey);
        ZEND_HASH_FOREACH_KEY_VAL(htable, num_key, key, hentry)
        {
            char alloc_str = 0;

            /* We are taking care of long keys properly */
            if(!key)
            {
                /* Convert num_key to string and use that instead */
                keylen = snprintf(digits, MAX_ARRAY_INDEX_DIGITS, "%ld", num_key);
                key = zend_string_init(digits, keylen, 0);
                alloc_str = 1;
            }

            if(ZSTR_LEN(key) > 4096 || Z_TYPE_P(hentry) == IS_RESOURCE)
            {
                result = WARNING_ZVCACHE_ARGUMENT;
            }
            else
            {
                /* isadd = 0 */
                result = zvcache_set(UCG(zvucache), ZSTR_VAL(key), hentry, ttl, 0);
            }

            if(FAILED(result))
            {
                add_assoc_long_ex(return_value, ZSTR_VAL(key), ZSTR_LEN(key), -1);
            }

            if (alloc_str == 1)
            {
                zend_string_release(key);
                alloc_str = 0;
            }

            result  = NONFATAL;
            key     = NULL;
        } ZEND_HASH_FOREACH_END();
    }
    else
    {
        _ASSERT(FALSE);
    }

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_set", result);

        RETURN_FALSE;
    }
}

PHP_FUNCTION(ucache_add)
{
    int           result   = NONFATAL;
    zval *        pzkey    = NULL;
    zval *        pzval    = NULL;
    int           ttl      = 0;
    zend_long     lttl     = 0;
    HashTable *   htable   = NULL;
    zval *        hentry   = NULL;
    zend_ulong    num_key;
    zend_string * key      = NULL;
    int           keylen   = 0;
    char          digits[MAX_ARRAY_INDEX_DIGITS] = {0};

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    /* "l" stores a full zend_long, so parse into lttl and keep ttl an int */
    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z|zl", &pzkey, &pzval, &lttl) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    ttl = (int)lttl;

    /* Negative ttl and resource values are not allowed */
    if(ttl < 0)
    {
        php_error_docref(NULL, E_WARNING, "ttl cannot be less than 0");

        result = WARNING_ZVCACHE_ARGUMENT;
        goto Finished;
    }

    if(pzval && Z_TYPE_P(pzval) == IS_RESOURCE)
    {
        php_error_docref(NULL, E_WARNING, "value cannot be a resource");

        result = WARNING_ZVCACHE_RESCOPYIN;
        goto Finished;
    }

    if(Z_TYPE_P(pzkey) != IS_STRING && Z_TYPE_P(pzkey) != IS_ARRAY)
    {
        convert_to_string(pzkey);
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

        /* isadd = 1 */
        result = zvcache_set(UCG(zvucache), Z_STRVAL_P(pzkey), pzval, ttl, 1);
        if(FAILED(result))
        {
            goto Finished;
        }

        ZVAL_BOOL(return_value, 1);
    }
    else if(Z_TYPE_P(pzkey) == IS_ARRAY)
    {
        array_init(return_value);
        htable = Z_ARRVAL_P(pzkey);
        ZEND_HASH_FOREACH_KEY_VAL(htable, num_key, key, hentry)
        {
            char alloc_str = 0;

            /* We are taking care of long keys properly */
            if(!key)
            {
                /* Convert num_key to string and use that instead */
                keylen = snprintf(digits, MAX_ARRAY_INDEX_DIGITS, "%ld", num_key);
                key = zend_string_init(digits, keylen, 0);
                alloc_str = 1;
            }

            if(ZSTR_LEN(key) > 4096 || Z_TYPE_P(hentry) == IS_RESOURCE)
            {
                result = WARNING_ZVCACHE_ARGUMENT;
            }
            else
            {
                /* isadd = 1 */
                result = zvcache_set(UCG(zvucache), ZSTR_VAL(key), hentry, ttl, 1);
            }

            if(FAILED(result))
            {
                add_assoc_long_ex(return_value, ZSTR_VAL(key), ZSTR_LEN(key), -1);
            }

            if (alloc_str == 1)
            {
                zend_string_release(key);
                alloc_str = 0;
            }

            result  = NONFATAL;
            key     = NULL;
        } ZEND_HASH_FOREACH_END();
    }
    else
    {
        _ASSERT(FALSE);
    }

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_add", result);

        if(result == WARNING_ZVCACHE_EXISTS)
        {
            php_error_docref(NULL, E_WARNING, "function called with a key which already exists");
        }

        RETURN_FALSE;
    }
}

PHP_FUNCTION(ucache_delete)
{
    int           result   = NONFATAL;
    zval *        pzkey    = NULL;
    HashTable *   htable   = NULL;
    zval *        hentry   = NULL;
    char *        key      = NULL;
    char          digits[MAX_ARRAY_INDEX_DIGITS] = {0};

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z", &pzkey) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(Z_TYPE_P(pzkey) != IS_STRING && Z_TYPE_P(pzkey) != IS_ARRAY)
    {
        convert_to_string(pzkey);
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
        htable = Z_ARRVAL_P(pzkey);
        ZEND_HASH_FOREACH_VAL(htable, hentry)
        {
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

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_delete", result);

        RETURN_FALSE;
    }
}

PHP_FUNCTION(ucache_clear)
{
    int result = NONFATAL;

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(ZEND_NUM_ARGS())
    {
        result = FATAL_INVALID_ARGUMENT;
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

PHP_FUNCTION(ucache_gc)
{
    int result = NONFATAL; 

    zvcache_context *  pcache  = UCG(zvucache);

    unsigned char  flock     = 0;
    unsigned int count = 0;
    zend_long    interval = 0;  /* zend_long: "l" stores a full zend_long */
    zend_long    step     = 0;
    zend_long    ttl      = 0;

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }
        

    if(pcache == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }


    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lll", &interval, &step, &ttl) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    UCG(ucgc_interval) = interval;
    UCG(ucgc_step) = step;
    UCG(ucgc_ttl) = ttl;

    lock_lock(pcache->zvlock);
    flock = 1;

    count = zvcache_run_scavenger(pcache,1);

Finished:


    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }


    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_gc", result);

        RETURN_FALSE;
    }
	
   RETURN_LONG(count);
}

PHP_FUNCTION(ucache_exists)
{
    int           result = NONFATAL;
    char *        key    = NULL;
    size_t        keylen = 0;
    unsigned char exists = 0;

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "s", &key, &keylen) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
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
    zend_bool            summaryonly = 0;
    char *               entrykey    = NULL;
    size_t               entrylen    = 0;

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "|bs", &summaryonly, &entrykey, &entrylen) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    plist = (zend_llist *)alloc_emalloc(sizeof(zend_llist));
    if(plist == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    result = zvcache_list(UCG(zvucache), summaryonly, entrykey, &zvinfo, plist);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Fill the array and then call zend_llist_destroy */
    array_init(return_value);
    add_assoc_long(return_value, "total_cache_uptime", zvinfo.initage);
    add_assoc_bool(return_value, "is_local_cache", zvinfo.islocal);
    add_assoc_long(return_value, "total_item_count", zvinfo.itemcount);
    add_assoc_long(return_value, "total_hit_count", zvinfo.hitcount);
    add_assoc_long(return_value, "total_miss_count", zvinfo.misscount);

    array_init(&zfentries);

    peinfo = (zvcache_info_entry *)zend_llist_get_first(plist);
    while(peinfo != NULL)
    {
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

    add_assoc_zval(return_value, "ucache_entries", &zfentries);

    zend_llist_destroy(plist);
    alloc_efree(plist);
    plist = NULL;

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

PHP_FUNCTION(ucache_inc)
{
    int          result  = NONFATAL;
    char *       key     = NULL;
    size_t       keylen  = 0;
    zend_long    delta   = 1;
    zend_long    newval  = 0;
    zval *       success = NULL;

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "s|lz", &key, &keylen, &delta, &success) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(success != NULL)
    {
        while (Z_TYPE_P(success) == IS_REFERENCE) {
            success = Z_REFVAL_P(success);
        }

        ZVAL_BOOL(success, 0);
    }

    result = zvcache_change(UCG(zvucache), key, delta, &newval);
    if(FAILED(result))
    {
        goto Finished;
    }

    if(success != NULL)
    {
        ZVAL_BOOL(success, 1);
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_inc", result);

        if(result == WARNING_ZVCACHE_NOTLONG)
        {
            php_error_docref(NULL, E_WARNING, "function can only be called for key whose value is long");
        }

        RETURN_FALSE;
    }

    RETURN_LONG(newval);
}

PHP_FUNCTION(ucache_dec)
{
    int          result  = NONFATAL;
    char *       key     = NULL;
    size_t       keylen  = 0;
    zend_long    delta   = 1;
    zend_long    newval  = 0;
    zval *       success = NULL;

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "s|lz", &key, &keylen, &delta, &success) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(success != NULL)
    {
        while (Z_TYPE_P(success) == IS_REFERENCE) {
            success = Z_REFVAL_P(success);
        }

        ZVAL_BOOL(success, 0);
    }

    /* Convert to negative number */
    delta = -delta;

    result = zvcache_change(UCG(zvucache), key, delta, &newval);
    if(FAILED(result))
    {
        goto Finished;
    }

    if(success != NULL)
    {
        ZVAL_BOOL(success, 1);
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        UCG(uclasterror) = result;

        dprintimportant("failure %d in ucache_dec", result);

        if(result == WARNING_ZVCACHE_NOTLONG)
        {
            php_error_docref(NULL, E_WARNING, "function can only be called for key whose value is long");
        }

        RETURN_FALSE;
    }

    RETURN_LONG(newval);
}

PHP_FUNCTION(ucache_cas)
{
    int          result = NONFATAL;
    char *       key    = NULL;
    size_t       keylen = 0;
    zend_long    cvalue = 0;
    zend_long    nvalue = 0;

    /* If user cache is enabled, return false */
    if(!UCG(ucenabled))
    {
        RETURN_FALSE;
    }

    if(UCG(zvucache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "sll", &key, &keylen, &cvalue, &nvalue) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
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

