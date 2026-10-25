/*
   +----------------------------------------------------------------------------------------------+
   | Windows Cache for PHP                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Copyright (c) 2009, Microsoft Corporation. All rights reserved.                              |
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
   | Module: php_ucache.c                                                                       |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#define NAMESALT_LENGTH_MAXIMUM     8
#define LOCK_KEY_MAXLEN             150
#define RMDIR_WAIT_TIME             1000
#define MAX_ARRAY_INDEX_DIGITS      21

/* START OF PHP EXTENSION MACROS STUFF */
PHP_MINIT_FUNCTION(ucache);
PHP_MSHUTDOWN_FUNCTION(ucache);
PHP_RINIT_FUNCTION(ucache);
PHP_RSHUTDOWN_FUNCTION(ucache);
PHP_MINFO_FUNCTION(ucache);

/* Info functions exposed by this extension */
PHP_FUNCTION(ucache_rplist_fileinfo);
PHP_FUNCTION(ucache_rplist_meminfo);
PHP_FUNCTION(ucache_fcache_fileinfo);
PHP_FUNCTION(ucache_fcache_meminfo);
PHP_FUNCTION(ucache_meminfo);
PHP_FUNCTION(ucache_scache_meminfo);

/* Utility functions exposed by this extension */
PHP_FUNCTION(ucache_refresh_if_changed);

/* ZVAL cache functions matching other caches */
PHP_FUNCTION(ucache_get);
PHP_FUNCTION(ucache_set);
PHP_FUNCTION(ucache_add);
PHP_FUNCTION(ucache_delete);
PHP_FUNCTION(ucache_clear);
PHP_FUNCTION(ucache_exists);
PHP_FUNCTION(ucache_info);
PHP_FUNCTION(ucache_scache_info);
PHP_FUNCTION(ucache_inc);
PHP_FUNCTION(ucache_dec);
PHP_FUNCTION(ucache_cas);
PHP_FUNCTION(ucache_lock);
PHP_FUNCTION(ucache_unlock);

/* Wrapper functions for standard PHP functions */
static void ucache_file_exists(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_file_get_contents(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_filesize(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_is_dir(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_is_file(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_is_readable(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_is_writable(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_readfile(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_realpath(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_unlink(INTERNAL_FUNCTION_PARAMETERS);
static void ucache_rename(INTERNAL_FUNCTION_PARAMETERS);

#ifdef UCACHE_TEST
PHP_FUNCTION(ucache_lasterror);
PHP_FUNCTION(ucache_runtests);
PHP_FUNCTION(ucache_fcache_find);
PHP_FUNCTION(ucache_fcnotify_fileinfo);
PHP_FUNCTION(ucache_fcnotify_meminfo);
#endif

ZEND_DECLARE_MODULE_GLOBALS(ucache)

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_rplist_fileinfo, 0, 0, 0)
    ZEND_ARG_INFO(0, summaryonly)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_rplist_meminfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_fcache_fileinfo, 0, 0, 0)
    ZEND_ARG_INFO(0, summaryonly)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_fcache_meminfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_meminfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_scache_meminfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_refresh_if_changed, 0, 0, 0)
    ZEND_ARG_INFO(0, file_list)
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_exists, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_info, 0, 0, 0)
    ZEND_ARG_INFO(0, summaryonly)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_scache_info, 0, 0, 0)
    ZEND_ARG_INFO(0, summaryonly)
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_lock, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, isglobal)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_unlock, 0, 0, 1)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

#ifdef UCACHE_TEST
ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_lasterror, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_runtests, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_fcache_find, 0, 0, 1)
    ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_fcnotify_fileinfo, 0, 0, 0)
    ZEND_ARG_INFO(0, summaryonly)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ucache_fcnotify_meminfo, 0, 0, 0)
ZEND_END_ARG_INFO()
#endif

#define UCACHE_FUNC(name) static PHP_NAMED_FUNCTION(name)

/* Put all user defined functions here */
zend_function_entry ucache_functions[] = {
    PHP_FE(ucache_rplist_fileinfo, arginfo_ucache_rplist_fileinfo)
    PHP_FE(ucache_rplist_meminfo, arginfo_ucache_rplist_meminfo)
    PHP_FE(ucache_fcache_fileinfo, arginfo_ucache_fcache_fileinfo)
    PHP_FE(ucache_fcache_meminfo, arginfo_ucache_fcache_meminfo)
    PHP_FE(ucache_meminfo, arginfo_ucache_meminfo)
    PHP_FE(ucache_scache_meminfo, arginfo_ucache_scache_meminfo)
    PHP_FE(ucache_refresh_if_changed, arginfo_ucache_refresh_if_changed)
    PHP_FE(ucache_get, arginfo_ucache_get)
    PHP_FE(ucache_set, arginfo_ucache_set)
    PHP_FE(ucache_add, arginfo_ucache_add)
    PHP_FE(ucache_delete, arginfo_ucache_delete)
    PHP_FE(ucache_clear, arginfo_ucache_clear)
    PHP_FE(ucache_exists, arginfo_ucache_exists)
    PHP_FE(ucache_info, arginfo_ucache_info)
    PHP_FE(ucache_scache_info, arginfo_ucache_scache_info)
    PHP_FE(ucache_inc, arginfo_ucache_inc)
    PHP_FE(ucache_dec, arginfo_ucache_dec)
    PHP_FE(ucache_cas, arginfo_ucache_cas)
    PHP_FE(ucache_lock, arginfo_ucache_lock)
    PHP_FE(ucache_unlock, arginfo_ucache_unlock)
#ifdef UCACHE_TEST
    PHP_FE(ucache_lasterror, arginfo_ucache_lasterror)
    PHP_FE(ucache_runtests, arginfo_ucache_runtests)
    PHP_FE(ucache_fcache_find, arginfo_ucache_fcache_find)
    PHP_FE(ucache_fcnotify_fileinfo, arginfo_ucache_fcnotify_fileinfo)
    PHP_FE(ucache_fcnotify_meminfo, arginfo_ucache_fcnotify_meminfo)
#endif
    {NULL, NULL, NULL}
};

/* ucache_module_entry */
zend_module_entry ucache_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_UCACHE_EXTNAME,
    ucache_functions,      /* Functions */
    PHP_MINIT(ucache),     /* MINIT */
    PHP_MSHUTDOWN(ucache), /* MSHUTDOWN */
    PHP_RINIT(ucache),     /* RINIT */
    PHP_RSHUTDOWN(ucache), /* RSHUTDOWN */
    PHP_MINFO(ucache),     /* MINFO */
    PHP_UCACHE_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_UCACHE
    ZEND_GET_MODULE(ucache)
#endif

PHP_INI_BEGIN()
/* index 0 */  STD_PHP_INI_BOOLEAN("ucache.fcenabled", "1", PHP_INI_ALL, OnUpdateBool, fcenabled, zend_ucache_globals, ucache_globals)
/* index 1 */  STD_PHP_INI_BOOLEAN("ucache.enablecli", "0", PHP_INI_SYSTEM, OnUpdateBool, enablecli, zend_ucache_globals, ucache_globals)
/* index 2 */  STD_PHP_INI_ENTRY("ucache.fcachesize", "24", PHP_INI_SYSTEM, OnUpdateLong, fcachesize, zend_ucache_globals, ucache_globals)
/* index 3 */  STD_PHP_INI_ENTRY("ucache.maxfilesize", "256", PHP_INI_SYSTEM, OnUpdateLong, maxfilesize, zend_ucache_globals, ucache_globals)
/* index 4 */  STD_PHP_INI_ENTRY("ucache.filecount", "4096", PHP_INI_SYSTEM, OnUpdateLong, numfiles, zend_ucache_globals, ucache_globals)
/* index 5 */  STD_PHP_INI_ENTRY("ucache.chkinterval", "30", PHP_INI_SYSTEM, OnUpdateLong, fcchkfreq, zend_ucache_globals, ucache_globals)
/* index 6 */  STD_PHP_INI_ENTRY("ucache.ttlmax", "1200", PHP_INI_SYSTEM, OnUpdateLong, ttlmax, zend_ucache_globals, ucache_globals)
/* index 7 */  STD_PHP_INI_ENTRY("ucache.debuglevel", "0", PHP_INI_ALL, ucache_modify_debuglevel, debuglevel, zend_ucache_globals, ucache_globals)
/* index 8 */ STD_PHP_INI_ENTRY("ucache.ignorelist", NULL, PHP_INI_ALL, OnUpdateString, ignorelist, zend_ucache_globals, ucache_globals)
/* index 9 */ STD_PHP_INI_ENTRY("ucache.fcenabledfilter", NULL, PHP_INI_SYSTEM, OnUpdateString, fcefilter, zend_ucache_globals, ucache_globals)
/* index 10 */ STD_PHP_INI_ENTRY("ucache.namesalt", NULL, PHP_INI_SYSTEM, OnUpdateString, namesalt, zend_ucache_globals, ucache_globals)
/* index 11 */ STD_PHP_INI_ENTRY("ucache.localheap", "0", PHP_INI_SYSTEM, OnUpdateBool, localheap, zend_ucache_globals, ucache_globals)
/* index 12 */ STD_PHP_INI_BOOLEAN("ucache.ucenabled", "1", PHP_INI_ALL, OnUpdateBool, ucenabled, zend_ucache_globals, ucache_globals)
/* index 13 */ STD_PHP_INI_ENTRY("ucache.ucachesize", "8", PHP_INI_SYSTEM, OnUpdateLong, ucachesize, zend_ucache_globals, ucache_globals)
/* index 14 */ STD_PHP_INI_ENTRY("ucache.scachesize", "8", PHP_INI_SYSTEM, OnUpdateLong, scachesize, zend_ucache_globals, ucache_globals)
/* index 15 */ STD_PHP_INI_BOOLEAN("ucache.fcndetect", "1", PHP_INI_SYSTEM, OnUpdateBool, fcndetect, zend_ucache_globals, ucache_globals)
/* index 16 */ STD_PHP_INI_ENTRY("ucache.apppoolid", NULL, PHP_INI_SYSTEM, OnUpdateString, apppoolid, zend_ucache_globals, ucache_globals)
/* index 17 */ STD_PHP_INI_BOOLEAN("ucache.reroute_enabled", "0", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateBool, reroute_enabled, zend_ucache_globals, ucache_globals)
/* index 18 */ STD_PHP_INI_ENTRY("ucache.filemapdir", NULL, PHP_INI_SYSTEM, OnUpdateString, filemapdir, zend_ucache_globals, ucache_globals)
PHP_INI_END()

/* END OF PHP EXTENSION MACROS STUFF */

static void globals_initialize(zend_ucache_globals * globals);
static void globals_terminate(zend_ucache_globals * globals);
static unsigned char isin_ignorelist(const char * ignorelist, const char * filename);
static unsigned char isin_cseplist(const char * cseplist, const char * szinput);

/* Initialize globals */
static void globals_initialize(zend_ucache_globals * globals)
{
    dprintverbose("start globals_initialize");

#if defined(COMPILE_DL_UCACHE) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    /* Function pointers we will override */
    original_resolve_path           = zend_resolve_path;
    original_stream_open_function   = zend_stream_open_function;

    /* Initalize the ucache_globals items, before parsing the INI file */
    UCG(fcenabled)   = 1;    /* File cache enabled by default */
    UCG(ucenabled)   = 1;    /* User cache enabled by default */
    UCG(enablecli)   = 0;    /* UCache not enabled by default for CLI */
    UCG(fcachesize)  = 24;   /* File cache size is 24 MB by default */
    UCG(ucachesize)  = 8;    /* User cache size is 8 MB by default */
    UCG(scachesize)  = 8;    /* Session cache size is 8 MB by default */
    UCG(maxfilesize) = 256;  /* Maximum file size to cache is 256 KB */
    UCG(numfiles)    = 4096; /* 4096 hashtable keys */
    UCG(fcchkfreq)   = 30;   /* File change check done every 30 secs */
    UCG(ttlmax)      = 1200; /* File removed if not used for 1200 secs */
    UCG(debuglevel)  = 0;    /* Debug dump not enabled by default */
    UCG(ignorelist)  = NULL; /* List of files to ignore for caching */
    UCG(fcefilter)   = NULL; /* List of sites for which fcenabled is toggled */
    UCG(namesalt)    = NULL; /* Salt to use in names used by ucache */
    UCG(fcndetect)   = 1;    /* File change notification enabled by default */
    UCG(localheap)   = 0;    /* Local heap is disabled by default */
    UCG(apppoolid)   = NULL; /* Use this application id */
    UCG(reroute_enabled) = 0;/* Enable wrappers around standard PHP functions */
    UCG(filemapdir)  = NULL; /* Directory where temp filemap files should be created */

    UCG(lasterror)   = 0;    /* GetLastError() value set by ucache */
    UCG(uclasterror) = 0;    /* Last error returned by user cache */
    UCG(lfcache)     = NULL; /* Aplist to use for file/rpath cache */
    UCG(zvucache)    = NULL; /* zvcache_context for user zvals */
    UCG(zvscache)    = NULL; /* zvcache_context for session data */
    UCG(phscache)    = NULL; /* Hashtable containing zvcache_contexts */
    UCG(wclocks)     = NULL; /* HashTable for locks created by ucache_lock */
    UCG(zvcopied)    = NULL; /* HashTable which helps with refcounting */
    UCG(parentpid)   = 0;    /* Parent process identifier to use */
    UCG(fmapgdata)   = NULL; /* Global filemap information data */
    UCG(errmsglist)  = NULL; /* Error messages list generated by PHP */
    UCG(inifce)      = NULL; /* Ini entry null until register_ini called */
    UCG(inisavepath) = NULL; /* Fill when ps_open is called */
    UCG(dofctoggle)  = 0;    /* If set to 1, toggle value of fcenabled */

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

static unsigned char isin_ignorelist(const char * ignorelist, const char * filename)
{
    unsigned char retvalue   = 0;
    char *        searchstr  = NULL;
    char *        fslash     = NULL;
    char          filestr[     MAX_PATH];
    char          tempchar   = 0;
    size_t        length     = 0;

    dprintverbose("start isin_ignorelist");

    _ASSERT(filename != NULL);

    if(ignorelist == NULL)
    {
        goto Finished;
    }

    /* Get the file name portion from filename */
    searchstr = strrchr(filename, '\\');
    if(searchstr != NULL)
    {
        fslash = strrchr(filename, '/');
        if(fslash > searchstr)
        {
            filename = fslash + 1;
        }
        else
        {
            filename = searchstr + 1;
        }
    }
    else
    {
        searchstr = strrchr(filename, '/');
        if(searchstr != NULL)
        {
            filename = searchstr + 1;
        }
    }

    length = strlen(filename);
    if(length == 0 || length > MAX_PATH - 3)
    {
        goto Finished;
    }

    /* filestr is "|filename|" */
    ZeroMemory(filestr, MAX_PATH);

    filestr[0] = '|';
    strncpy(filestr + 1, filename, length);
    _strlwr(filestr);

    /* Check if filename exactly matches ignorelist */
    /* Both are lowercase and case sensitive lookup is fine */
    if(strcmp(ignorelist, filestr + 1) == 0)
    {
        retvalue = 1;
        goto Finished;
    }

    /* Check if filename is in end or middle of ignorelist */
    searchstr = strstr(ignorelist, filestr);
    if(searchstr != NULL)
    {
        tempchar = *(searchstr + length + 1);
        if(tempchar == '|' || tempchar == 0)
        {
            retvalue = 1;
            goto Finished;
        }
    }

    /* Check if filename is in the start of ignorelist */
    filestr[length + 1] = '|';
    searchstr = strstr(ignorelist, filestr + 1);
    if(searchstr != NULL && searchstr == ignorelist)
    {
        retvalue = 1;
        goto Finished;
    }

Finished:

    dprintverbose("end isin_ignorelist");

    return retvalue;
}

static unsigned char isin_cseplist(const char * cseplist, const char * szinput)
{
    unsigned char retvalue   = 0;
    char *        searchstr  = NULL;
    char          inputstr[    MAX_PATH];
    char          tempchar   = 0;
    size_t        length     = 0;

    dprintverbose("start isin_cseplist");

    _ASSERT(szinput != NULL);

    /* If list is NULL or input length is 0, return false */
    if(cseplist == NULL)
    {
        goto Finished;
    }

    length = strlen(szinput);
    if(length == 0)
    {
        goto Finished;
    }

    /* inputstr is ",szinput," */
    ZeroMemory(inputstr, MAX_PATH);

    inputstr[0] = ',';
    strncpy(inputstr + 1, szinput, length);
    _strlwr(inputstr);

    /* Check if szinput exactly matches cseplist */
    /* Both are lowercase and case sensitive lookup is fine */
    if(strcmp(cseplist, inputstr + 1) == 0)
    {
        retvalue = 1;
        goto Finished;
    }

    /* Check if szinput is in end or middle of cseplist */
    searchstr = strstr(cseplist, inputstr);
    if(searchstr != NULL)
    {
        tempchar = *(searchstr + length + 1);
        if(tempchar == ',' || tempchar == 0)
        {
            retvalue = 1;
            goto Finished;
        }
    }

    /* Check if szinput is in the start of cseplist */
    inputstr[length + 1] = ',';
    searchstr = strstr(cseplist, inputstr + 1);
    if(searchstr != NULL && searchstr == cseplist)
    {
        retvalue = 1;
        goto Finished;
    }

Finished:

    dprintverbose("end isin_cseplist");

    return retvalue;
}

PHP_MINIT_FUNCTION(ucache)
{
    int                result    = NONFATAL;
    aplist_context *   plcache   = NULL;
    zvcache_context *  pzcache   = NULL;
    int                resnumber = -1;
    zend_extension     extension = {0};
    zend_ini_entry *   pinientry = NULL;
    int                rethash   = 0;

    ZEND_INIT_MODULE_GLOBALS(ucache, globals_initialize, globals_terminate);
    REGISTER_INI_ENTRIES();

    dprintverbose("start php_minit");

    EventRegisterPHP_Ucache();

    pinientry = zend_hash_str_find_ptr(EG(ini_directives), "ucache.fcenabled", sizeof("ucache.fcenabled")-1);
    _ASSERT(pinientry != NULL);
    UCG(inifce) = pinientry;

    /* If enablecli is set to 0, don't initialize when run with cli sapi */
    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        goto Finished;
    }

    /* set up the app pool ID */
    UCG(apppoolid) = utils_get_apppool_name();

    /* Compare value of globals with minimum and maximum allowed */
    UCG(numfiles)    = (UCG(numfiles)    < NUM_FILES_MINIMUM)   ? NUM_FILES_MINIMUM   : UCG(numfiles);
    UCG(numfiles)    = (UCG(numfiles)    > NUM_FILES_MAXIMUM)   ? NUM_FILES_MAXIMUM   : UCG(numfiles);
    UCG(fcachesize)  = (UCG(fcachesize)  < FCACHE_SIZE_MINIMUM) ? FCACHE_SIZE_MINIMUM : UCG(fcachesize);
    UCG(fcachesize)  = (UCG(fcachesize)  > FCACHE_SIZE_MAXIMUM) ? FCACHE_SIZE_MAXIMUM : UCG(fcachesize);
    UCG(ucachesize)  = (UCG(ucachesize)  < ZCACHE_SIZE_MINIMUM) ? ZCACHE_SIZE_MINIMUM : UCG(ucachesize);
    UCG(ucachesize)  = (UCG(ucachesize)  > ZCACHE_SIZE_MAXIMUM) ? ZCACHE_SIZE_MAXIMUM : UCG(ucachesize);
    UCG(scachesize)  = (UCG(scachesize)  < SCACHE_SIZE_MINIMUM) ? SCACHE_SIZE_MINIMUM : UCG(scachesize);
    UCG(scachesize)  = (UCG(scachesize)  > SCACHE_SIZE_MAXIMUM) ? SCACHE_SIZE_MAXIMUM : UCG(scachesize);
    UCG(maxfilesize) = (UCG(maxfilesize) < FILE_SIZE_MINIMUM)   ? FILE_SIZE_MINIMUM   : UCG(maxfilesize);
    UCG(maxfilesize) = (UCG(maxfilesize) > FILE_SIZE_MAXIMUM)   ? FILE_SIZE_MAXIMUM   : UCG(maxfilesize);

    /* ttlmax can be set to 0 which means scavenger is completely disabled */
    if(UCG(ttlmax) != 0)
    {
        UCG(ttlmax)  = (UCG(ttlmax)      < TTL_VALUE_MINIMUM)   ? TTL_VALUE_MINIMUM   : UCG(ttlmax);
        UCG(ttlmax)  = (UCG(ttlmax)      > TTL_VALUE_MAXIMUM)   ? TTL_VALUE_MAXIMUM   : UCG(ttlmax);
    }

    /* fcchkfreq can be set to 0 which will mean check is completely disabled */
    if(UCG(fcchkfreq) != 0)
    {
        UCG(fcchkfreq) = (UCG(fcchkfreq) < FCHECK_FREQ_MINIMUM) ? FCHECK_FREQ_MINIMUM : UCG(fcchkfreq);
        UCG(fcchkfreq) = (UCG(fcchkfreq) > FCHECK_FREQ_MAXIMUM) ? FCHECK_FREQ_MAXIMUM : UCG(fcchkfreq);
    }

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

    /* Convert ignorelist to lowercase soon enough */
    if(UCG(ignorelist) != NULL)
    {
        _strlwr(UCG(ignorelist));
    }

    /* Even if enabled is set to false, create cache and set */
    /* the hook because scripts can selectively enable it */

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

    /* issession = 0, islocal = 0, cachekey = 1, shmfilepath = NULL */
    result = zvcache_initialize(pzcache, 0, 0, 1, UCG(ucachesize), NULL);
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

    /* User cache is now operational, even if things below fail */
    pzcache = NULL;

    /*
     * Register ucache session handler
     */

    php_session_register_module(ps_ucache_ptr);

    /*
     * Create filelist cache
     */

    result = aplist_create(&plcache);
    if(FAILED(result))
    {
        goto Finished;
    }

    result = aplist_initialize(plcache, APLIST_TYPE_GLOBAL, UCG(numfiles), UCG(fcchkfreq), UCG(ttlmax));
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Initialize file cache */
    result = aplist_fcache_initialize(plcache, UCG(fcachesize), UCG(maxfilesize));
    if(FAILED(result))
    {
        goto Finished;
    }

    UCG(lfcache) = plcache;

    ucache_intercept_functions_init();

    zend_stream_open_function = ucache_stream_open_function;

    zend_resolve_path = ucache_resolve_path;

    dprintverbose("Installed function hooks for stream_open");

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        EventWriteModuleInitErrorEvent(result);
        dprintimportant("failure %d in php_minit", result);

        ucache_intercept_functions_shutdown();

        if(plcache != NULL)
        {
            aplist_terminate(plcache);
            aplist_destroy(plcache);

            plcache = NULL;

            UCG(lfcache) = NULL;
        }

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

    zend_resolve_path = original_resolve_path;
    zend_stream_open_function = original_stream_open_function;

    ucache_intercept_functions_shutdown();

    /* wclocks_destructor will destroy the locks properly */
    if(UCG(wclocks) != NULL)
    {
        zend_hash_destroy(UCG(wclocks));
        alloc_pefree(UCG(wclocks));

        UCG(wclocks) = NULL;
    }

    if(UCG(zvcopied) != NULL)
    {
        alloc_pefree(UCG(zvcopied));
        UCG(zvcopied) = NULL;
    }

    if(UCG(lfcache) != NULL)
    {
        aplist_terminate(UCG(lfcache));
        aplist_destroy(UCG(lfcache));

        UCG(lfcache) = NULL;
    }

    if(UCG(zvucache) != NULL)
    {
        zvcache_terminate(UCG(zvucache));
        zvcache_destroy(UCG(zvucache));

        UCG(zvucache) = NULL;
    }

    /* Must shut down this table before shutting down UCG(zvscache) */
    if(UCG(phscache) != NULL)
    {
        /* destructor in ucache_session.c will destroy zvcache_context */
        zend_hash_destroy(UCG(phscache));
        alloc_pefree(UCG(phscache));

        UCG(phscache) = NULL;
    }

    if(UCG(zvscache) != NULL)
    {
        zvcache_terminate(UCG(zvscache));
        zvcache_destroy(UCG(zvscache));

        UCG(zvscache) = NULL;
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

    UCG(inifce)      = NULL;
    UCG(inisavepath) = NULL;

    EventUnregisterPHP_Ucache();

    dprintverbose("end php_mshutdown");
    return SUCCESS;
}

PHP_RINIT_FUNCTION(ucache)
{
    zval * siteid = NULL;

    /* If enablecli is 0, short circuit this call in cli mode */
    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        goto Finished;
    }

    if ((UCG(fcefilter) != NULL))
    {
        /* Read site id from list of variables */
        zend_is_auto_global_str("_SERVER", sizeof("_SERVER") - 1);

        if (Z_ISUNDEF(PG(http_globals)[TRACK_VARS_SERVER]))
        {
            goto Finished;
        }
        if (Z_TYPE(PG(http_globals)[TRACK_VARS_SERVER]) != IS_ARRAY)
        {
            goto Finished;
        }
        siteid = zend_hash_str_find(Z_ARR(PG(http_globals)[TRACK_VARS_SERVER]), "INSTANCE_ID", sizeof("INSTANCE_ID")-1);
        if (siteid == NULL)
        {
            goto Finished;
        }
    }

    if(UCG(fcefilter) != NULL && isin_cseplist(UCG(fcefilter), Z_STRVAL_P(siteid)))
    {
        UCG(dofctoggle) = 1;
    }

Finished:

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(ucache)
{
    /* If enablecli is 0, short circuit this call in cli mode */
    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        goto Finished;
    }

    /* Destroy errmsglist now before zend_mm does it */
    if(UCG(errmsglist) != NULL)
    {
        zend_llist_destroy(UCG(errmsglist));
        alloc_efree(UCG(errmsglist));
        UCG(errmsglist) = NULL;
    }

    UCG(dofctoggle) = 0;

Finished:

    return SUCCESS;
}

PHP_MINFO_FUNCTION(ucache)
{
    dprintverbose("start php_minfo");

    php_info_print_table_start();

    if(!UCG(enablecli) && !strcmp(sapi_module.name, "cli"))
    {
        php_info_print_table_row(2, "File cache", "disabled");
    }
    else
    {
        php_info_print_table_row(2, "File cache", UCG(fcenabled) ? "enabled" : "disabled");
    }

    php_info_print_table_row(2, "Version", PHP_UCACHE_VERSION);
    php_info_print_table_row(2, "Owner", "iisphp@microsoft.com");
    php_info_print_table_row(2, "Build Date", __DATE__ " " __TIME__);

    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();

    dprintverbose("end php_minfo");
    return;
}

zend_string * ucache_resolve_path(const char * filename, int filename_len)
{
    int            result       = NONFATAL;
    char *         res_path_str = NULL;
    zend_string *  resolve_path = NULL;
    unsigned char  cenabled     = 0;
    fcache_value * pfvalue      = NULL;

    cenabled = UCG(fcenabled);

    /* If fcenabled is not modified in php code and toggle is set, change cenabled */
    _ASSERT(UCG(inifce) != NULL);
    if(UCG(inifce)->modified == 0 && UCG(dofctoggle) == 1)
    {
        /* toggle the current value of cenabled */
        cenabled = !cenabled;
    }

    /* If ucache.fcenabled is set to 0 but some how */
    /* this method is called, use original_resolve_path */
    if(!cenabled || filename == NULL || UCG(lfcache) == NULL)
    {
        return original_resolve_path(filename, filename_len);
    }

    dprintverbose("zend_resolve_path called for %s", filename);

    if(isin_ignorelist(UCG(ignorelist), filename))
    {
        dprintimportant("cache is disabled for the file because of ignore list");
        return original_resolve_path(filename, filename_len);
    }

    /* Keep last argument as NULL to indicate that we only want fullpath of file */
    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &res_path_str, &pfvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

    resolve_path = zend_string_init(res_path_str, strlen(res_path_str), 0);

Finished:

    if(pfvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pfvalue);
        pfvalue = NULL;
    }

    if (res_path_str != NULL)
    {
        alloc_efree(res_path_str);
        res_path_str = NULL;
    }

    if(FAILED(result))
    {
        dprintverbose("ucache_resolve_path failed with error %u", result);

        return original_resolve_path(filename, filename_len);
    }

    return resolve_path;
}

int ucache_stream_open_function(const char * filename, zend_file_handle * file_handle)
{
    int            result   = NONFATAL;
    fcache_value * pfvalue  = NULL;
    char *         fullpath = NULL;
    unsigned char  cenabled = 0;

    dprintverbose("start ucache_stream_open_function");

    cenabled = UCG(fcenabled);

    /* If fcenabled is not modified in php code and toggle is set, change cenabled */
    _ASSERT(UCG(inifce) != NULL);
    if(UCG(inifce)->modified == 0 && UCG(dofctoggle) == 1)
    {
        /* toggle the current value of cenabled */
        cenabled = !cenabled;
    }

    /* If ucache.fcenabled is set to 0 but some how */
    /* this method is called, use original_stream_open_function */
    if(!cenabled || filename == NULL || UCG(lfcache) == NULL)
    {
        return original_stream_open_function(filename, file_handle);
    }

    dprintverbose("zend_stream_open_function called for %s", filename);

    if(isin_ignorelist(UCG(ignorelist), filename))
    {
        dprintverbose("cache is disabled for the file because of ignore list");
        return original_stream_open_function(filename, file_handle);
    }

    result = aplist_fcache_get(UCG(lfcache), filename, USE_STREAM_OPEN_CHECK, &fullpath, &pfvalue);
    if(FAILED(result))
    {
        /* If original_stream_open failed, do not try again */
        if(result == FATAL_FCACHE_ORIGINAL_OPEN)
        {
            return FAILURE;
        }

        goto Finished;
    }

    if(pfvalue != NULL)
    {
        result = aplist_fcache_use(UCG(lfcache), fullpath, pfvalue, &file_handle);
        if(FAILED(result))
        {
            aplist_fcache_close(UCG(lfcache), pfvalue);
            goto Finished;
        }

        /* fullpath will be freed when close is called */
        file_handle->free_filename = 1;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        /* We can fail for big files or if we are not able to find file */
        /* If we fail, let original stream open function do its job */
        dprintimportant("ucache_stream_open_function failed with error %u", result);

        if(fullpath != NULL)
        {
            alloc_efree(fullpath);
            fullpath = NULL;
        }

        return original_stream_open_function(filename, file_handle);
    }

    dprintverbose("end ucache_stream_open_function");

    return SUCCESS;
}

/* Functions */
PHP_FUNCTION(ucache_rplist_fileinfo)
{
    int                  result      = NONFATAL;
    rplist_info *        pcinfo      = NULL;
    rplist_entry_info *  peinfo      = NULL;
    zval                 zfentries;
    zval                 zfentry;
    zend_ulong           index       = 1;
    zend_bool            summaryonly = 0;

    if(UCG(lfcache) == NULL)
    {
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &summaryonly) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    result = rplist_getinfo(UCG(lfcache)->prplist, summaryonly, &pcinfo);
    if(FAILED(result))
    {
        goto Finished;
    }

    array_init(return_value);
    add_assoc_long(return_value, "total_file_count", pcinfo->itemcount);

    array_init(&zfentries);

    peinfo = pcinfo->entries;
    while(peinfo != NULL)
    {
        array_init(&zfentry);

        add_assoc_string(&zfentry, "resolve_path", peinfo->pathkey);
        add_assoc_string(&zfentry, "subkey_data", peinfo->subkey);

        if(peinfo->abspath != NULL)
        {
            add_assoc_string(&zfentry, "absolute_path", peinfo->abspath);
        }

        add_index_zval(&zfentries, index, &zfentry);
        peinfo = peinfo->next;
        index++;
    }

    add_assoc_zval(return_value, "rplist_entries", &zfentries);

Finished:

    if(pcinfo != NULL)
    {
        rplist_freeinfo(pcinfo);
        pcinfo = NULL;
    }

    return;
}

PHP_FUNCTION(ucache_rplist_meminfo)
{
    int          result = NONFATAL;
    alloc_info * pinfo  = NULL;

    if(UCG(lfcache) == NULL)
    {
        goto Finished;
    }

    result = alloc_getinfo(UCG(lfcache)->prplist->rpalloc, &pinfo);
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

PHP_FUNCTION(ucache_fcache_fileinfo)
{
    int                  result      = NONFATAL;
    cache_info *         pcinfo      = NULL;
    cache_entry_info *   peinfo      = NULL;
    fcache_entry_info *  pinfo       = NULL;
    zval                 zfentries;
    zval                 zfentry;
    zend_ulong           index       = 1;
    zend_bool            summaryonly = 0;

    if(UCG(lfcache) == NULL)
    {
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &summaryonly) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    result = aplist_getinfo(UCG(lfcache), CACHE_TYPE_FILECONTENT, summaryonly, &pcinfo);
    if(FAILED(result))
    {
        goto Finished;
    }

    array_init(return_value);

    add_assoc_long(return_value, "total_cache_uptime", pcinfo->initage);
    add_assoc_bool(return_value, "is_local_cache", pcinfo->islocal);
    add_assoc_long(return_value, "total_file_count", pcinfo->itemcount);
    add_assoc_long(return_value, "total_hit_count", pcinfo->hitcount);
    add_assoc_long(return_value, "total_miss_count", pcinfo->misscount);

    array_init(&zfentries);

    peinfo = pcinfo->entries;
    while(peinfo != NULL)
    {
        array_init(&zfentry);
        add_assoc_string(&zfentry, "file_name", peinfo->filename);
        add_assoc_long(&zfentry, "add_time", peinfo->addage);
        add_assoc_long(&zfentry, "use_time", peinfo->useage);
        add_assoc_long(&zfentry, "last_check", peinfo->lchkage);

        if(peinfo->cdata != NULL)
        {
            pinfo = (fcache_entry_info *)peinfo->cdata;
            add_assoc_long(&zfentry, "file_size", pinfo->filesize);
            add_assoc_long(&zfentry, "hit_count", pinfo->hitcount);
        }

        add_index_zval(&zfentries, index, &zfentry);
        peinfo = peinfo->next;
        index++;
    }

    add_assoc_zval(return_value, "file_entries", &zfentries);

Finished:

    if(pcinfo != NULL)
    {
        aplist_freeinfo(CACHE_TYPE_FILECONTENT, pcinfo);
        pcinfo = NULL;
    }

    return;
}

PHP_FUNCTION(ucache_fcache_meminfo)
{
    int          result = NONFATAL;
    alloc_info * pinfo  = NULL;

    if(UCG(lfcache) == NULL)
    {
        goto Finished;
    }

    result = alloc_getinfo(UCG(lfcache)->pfcache->palloc, &pinfo);
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

PHP_FUNCTION(ucache_scache_meminfo)
{
    int                result    = NONFATAL;
    alloc_info         pinfo     = {0};
    alloc_info *       ptempinfo = NULL;
    zvcache_context *  pcache   = NULL;

    array_init(return_value);

    if(UCG(phscache) != NULL)
    {
        ZEND_HASH_FOREACH_PTR(UCG(phscache), pcache)
        {
            result = alloc_getinfo((pcache)->zvalloc, &ptempinfo);
            if(FAILED(result))
            {
                goto Finished;
            }

            pinfo.total_size   += ptempinfo->total_size;
            pinfo.free_size    += ptempinfo->free_size;
            pinfo.usedcount    += ptempinfo->usedcount;
            pinfo.freecount    += ptempinfo->freecount;
            pinfo.mem_overhead += ptempinfo->mem_overhead;

            alloc_freeinfo(ptempinfo);
            ptempinfo = NULL;
        } ZEND_HASH_FOREACH_END();

        add_assoc_long(return_value, "memory_total", pinfo.total_size);
        add_assoc_long(return_value, "memory_free", pinfo.free_size);
        add_assoc_long(return_value, "num_used_blks", pinfo.usedcount);
        add_assoc_long(return_value, "num_free_blks", pinfo.freecount);
        add_assoc_long(return_value, "memory_overhead", pinfo.mem_overhead);
    }
    else
    {
        /* If cache is not initialized, set everything to 0 */
        add_assoc_long(return_value, "memory_total", 0);
        add_assoc_long(return_value, "memory_free", 0);
        add_assoc_long(return_value, "num_used_blks", 0);
        add_assoc_long(return_value, "num_free_blks", 0);
        add_assoc_long(return_value, "memory_overhead", 0);
    }

Finished:

    if(ptempinfo != NULL)
    {
        alloc_freeinfo(ptempinfo);
        ptempinfo = NULL;
    }

    return;
}

PHP_FUNCTION(ucache_refresh_if_changed)
{
    int    result   = NONFATAL;
    zval * filelist = NULL;

    /* If file cache is not active, return false */
    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "|z!", &filelist) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(!ZEND_NUM_ARGS())
    {
        /* last check of all the entries need to be changed */
        filelist = NULL;
    }

    result = aplist_force_fccheck(UCG(lfcache), filelist);
    if(FAILED(result))
    {
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in ucache_refresh_if_changed", result);

        RETURN_FALSE;
    }

    RETURN_TRUE;
}

static void ucache_file_exists(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result   = NONFATAL;
    char *         filename = NULL;
    size_t         flength  = 0;
    char *         respath  = NULL;
    fcache_value * pfvalue  = NULL;
    unsigned char  retval   = 0;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p", &filename, &flength) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (flength == 0)
    {
        goto Finished;
    }

    dprintverbose("ucache_file_exists - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &respath, &pfvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    retval = 1;

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pfvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pfvalue);
        pfvalue = NULL;
    }

Finished:
    if(retval)
    {
        RETURN_TRUE;
    }
    else
    {
        UCG(orig_file_exists)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
}

/* file_get_contents implemented in ext\standard\file.c */
static void ucache_file_get_contents(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result           = NONFATAL;
    char *         filename         = NULL;
    size_t         filename_len     = 0;
    zend_bool      use_include_path = 0;
    char *         fullpath         = NULL;
    char *         respath          = NULL;
    fcache_value * pfvalue          = NULL;
    unsigned char  iscached         = 0;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if (ZEND_NUM_ARGS() > 2)
    {
        /* We only handle the first two args.  If caller specifies more, we need
         * to fall through to the orig_file_get_contents. */
        goto Finished;
    }

    /* TBD?? Call original function if filename contains "://" */
    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p|b", &filename, &filename_len, &use_include_path) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_file_get_contents - %s", filename);

    if(!IS_ABSOLUTE_PATH(filename, filename_len) && (!use_include_path))
    {
        fullpath = utils_fullpath(filename, filename_len);
    }

    result = aplist_fcache_get(UCG(lfcache), (fullpath == NULL ? filename : fullpath), USE_STREAM_OPEN_CHECK, &respath, &pfvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    iscached = 1;

    if (pfvalue && pfvalue->file_size)
    {
        RETVAL_STRINGL((UCG(lfcache)->pfcache->memaddr + pfvalue->file_content), pfvalue->file_size);
    }
    else
    {
        RETVAL_EMPTY_STRING();
    }

Finished:

    if(fullpath != NULL)
    {
        alloc_efree(fullpath);
        fullpath = NULL;
    }

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pfvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pfvalue);
        pfvalue = NULL;
    }

    if (!iscached)
    {
        UCG(orig_file_get_contents)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
    else if(FAILED(result))
    {
        dprintimportant("ucache_file_get_contents failed with error %u", result);
        RETURN_FALSE;
    }

    return;
}

static void ucache_filesize(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result       = NONFATAL;
    char *         filename     = NULL;
    size_t         filename_len = 0;
    char *         respath      = NULL;
    fcache_value * pfvalue      = NULL;
    unsigned char  iscached     = 0;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p", &filename, &filename_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_filesize - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &respath, &pfvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    iscached = 1;

    RETVAL_LONG(pfvalue->file_size);

Finished:

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pfvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pfvalue);
        pfvalue = NULL;
    }

    if (!iscached)
    {
        UCG(orig_filesize)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
    else if(FAILED(result))
    {
        dprintimportant("ucache_filesize failed with error %u", result);

        RETURN_FALSE;
    }

    return;
}

/* readfile implemented in ext\standard\file.c */
static void ucache_readfile(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result       = NONFATAL;
    char *         filename     = NULL;
    size_t         filename_len = 0;
    zend_bool      flags        = 0;
    char *         respath      = NULL;
    fcache_value * pfvalue      = NULL;
    unsigned char  iscached     = 0;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if (ZEND_NUM_ARGS() > 2)
    {
        /* We only handle the first two args.  If caller specifies more, we need
         * to fall through to the orig_readfile. */
        goto Finished;
    }

    /* TBD?? Call original function if filename contains "://" */
    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p|b", &filename, &filename_len, &flags) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_readfile - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, USE_STREAM_OPEN_CHECK, &respath, &pfvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    iscached = 1;

    PHPWRITE(UCG(lfcache)->pfcache->memaddr + pfvalue->file_content, pfvalue->file_size);
    RETVAL_LONG(pfvalue->file_size);

Finished:

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pfvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pfvalue);
        pfvalue = NULL;
    }

    if (!iscached)
    {
        UCG(orig_readfile)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }

    if(FAILED(result))
    {
        dprintimportant("ucache_readfile failed with error %u", result);
    }

    return;
}

/* is_readable implemented in tsrm\tsrm_win32.c */
static void ucache_is_readable(INTERNAL_FUNCTION_PARAMETERS)
{
    int             result           = NONFATAL;
    char *          filename         = NULL;
    size_t          filename_len     = 0;
    char *          respath          = NULL;
    fcache_value *  pvalue           = NULL;
    unsigned char   iscached         = 0;

    HANDLE          threadtoken      = NULL;
    unsigned char   isprocesstoken   = 0;
    HANDLE          impersonationtok = NULL;
    unsigned char   isreadable       = 0;

    unsigned int    priv_set_length  = sizeof(PRIVILEGE_SET);
    PRIVILEGE_SET   privilege_set    = {0};
    unsigned int    desired_access   = 0;
    unsigned int    granted_access   = 0;
    BOOL            faccess          = FALSE;
    GENERIC_MAPPING gen_map          = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p", &filename, &filename_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_is_readable - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &respath, &pvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Remember that we fetched something out of the cache */
    iscached = 1;

    /* If access returns -1, just return immediately */
    if(access(respath, 04))
    {
        isreadable = 0;
        goto Finished;
    }

    /* Get thread token. If not impersonated, get the value fcache_value */
    if(!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &threadtoken))
    {
        if (GetLastError() == ERROR_NO_TOKEN)
        {
            if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &threadtoken))
            {
                result = FATAL_OPEN_TOKEN;
                goto Finished;
            }

            if((pvalue->file_flags & FILE_IS_RUNAWARE) == 0)
            {
                isreadable = pvalue->file_flags & FILE_IS_READABLE;
                goto Finished;
            }

            isprocesstoken = 1;
        }
        else
        {
            result = FATAL_OPEN_TOKEN;
            goto Finished;
        }
    }

    /* Get the impersonated token which is required for AccessCheck call */
    if(!DuplicateToken(threadtoken, SecurityImpersonation, &impersonationtok))
    {
        result = FATAL_OPEN_TOKEN;
        goto Finished;
    }

    desired_access = FILE_GENERIC_READ;
    MapGenericMask(&desired_access, &gen_map);

    AccessCheck((PSECURITY_DESCRIPTOR)(UCG(lfcache)->pfcache->memaddr + pvalue->file_sec), impersonationtok,
                    desired_access, &gen_map, &privilege_set, &priv_set_length, &granted_access, &faccess);
    isreadable = (faccess ? 1 : 0);

    if(isprocesstoken)
    {
        pvalue->file_flags |= (faccess ? FILE_IS_READABLE : 0);
        pvalue->file_flags &= (~(FILE_IS_RUNAWARE));
    }

Finished:

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(threadtoken != NULL)
    {
        CloseHandle(threadtoken);
        threadtoken = NULL;
    }

    if(impersonationtok != NULL)
    {
        CloseHandle(impersonationtok);
        impersonationtok = NULL;
    }

    if(pvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pvalue);
        pvalue = NULL;
    }

    if(isreadable)
    {
        RETURN_TRUE;
    }
    else if (!iscached)
    {
        UCG(orig_is_readable)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
    else
    {
        RETURN_FALSE;
    }
}

/* is_writable implemented in tsrm\tsrm_win32.c */
/*
 * NOTE: PHP has two versions of the exact same function, with a one-letter
 * difference between the two: is_writable and is_writeable.
 * Not sure why they have two; but apparently is_writable came first, and
 * is_writeable is an alias.
 * In any* case, we have to hook *both* of them.  To facilitate this, we create
 * a function pointer variable.
 */
static void (*ucache_is_writeable)(INTERNAL_FUNCTION_PARAMETERS) = ucache_is_writable;
static void ucache_is_writable(INTERNAL_FUNCTION_PARAMETERS)
{
    int             result           = NONFATAL;
    char *          filename         = NULL;
    size_t          filename_len     = 0;
    char *          respath          = NULL;
    fcache_value *  pvalue           = NULL;

    HANDLE          threadtoken      = NULL;
    unsigned char   isprocesstoken   = 0;
    HANDLE          impersonationtok = NULL;
    unsigned char   iswritable       = 0;
    unsigned char   iscached         = 0;

    unsigned int    priv_set_length  = sizeof(PRIVILEGE_SET);
    PRIVILEGE_SET   privilege_set    = {0};
    unsigned int    desired_access   = 0;
    unsigned int    granted_access   = 0;
    BOOL            faccess          = FALSE;
    GENERIC_MAPPING gen_map          = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p", &filename, &filename_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_is_writable - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &respath, &pvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    iscached = 1;

    /* If access returns -1, just return immediately */
    if(access(respath, 02))
    {
        iswritable = 0;
        goto Finished;
    }

    /* Get thread token. If not impersonated, get the value fcache_value */
    if(!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &threadtoken))
    {
        if (GetLastError() == ERROR_NO_TOKEN)
        {
            if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &threadtoken))
            {
                result = FATAL_OPEN_TOKEN;
                goto Finished;
            }

            if((pvalue->file_flags & FILE_IS_WUNAWARE) == 0)
            {
                iswritable = pvalue->file_flags & FILE_IS_WRITABLE;
                goto Finished;
            }

            isprocesstoken = 1;
        }
        else
        {
            result = FATAL_OPEN_TOKEN;
            goto Finished;
        }
    }

    /* Get the impersonated token which is required for AccessCheck call */
    if(!DuplicateToken(threadtoken, SecurityImpersonation, &impersonationtok))
    {
        result = FATAL_OPEN_TOKEN;
        goto Finished;
    }

    desired_access = FILE_GENERIC_WRITE;
    MapGenericMask(&desired_access, &gen_map);

    AccessCheck((PSECURITY_DESCRIPTOR)(UCG(lfcache)->pfcache->memaddr + pvalue->file_sec), impersonationtok,
                    desired_access, &gen_map, &privilege_set, &priv_set_length, &granted_access, &faccess);
    iswritable = (faccess ? 1 : 0);

    if(isprocesstoken)
    {
        pvalue->file_flags |= (faccess ? FILE_IS_WRITABLE : 0);
        pvalue->file_flags &= (~(FILE_IS_WUNAWARE));
    }

Finished:

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(threadtoken != NULL)
    {
        CloseHandle(threadtoken);
        threadtoken = NULL;
    }

    if(impersonationtok != NULL)
    {
        CloseHandle(impersonationtok);
        impersonationtok = NULL;
    }

    if(pvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pvalue);
        pvalue = NULL;
    }

    if(iswritable)
    {
        RETURN_TRUE;
    }
    else if (!iscached)
    {
        UCG(orig_is_writable)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
    else
    {
        if(FAILED(result))
        {
            dprintimportant("ucache_is_writable failed with error %u", result);
        }

        RETURN_FALSE;
    }
}

/* is_file implemented in ext\standard\file.c */
static void ucache_is_file(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result       = NONFATAL;
    char *         filename     = NULL;
    size_t         filename_len = 0;
    char *         respath      = NULL;
    fcache_value * pvalue       = NULL;
    unsigned char  retval       = 0;
    unsigned char  iscached     = 0;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p", &filename, &filename_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_is_file - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &respath, &pvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    iscached = 1;

    if(pvalue != NULL && (pvalue->file_flags & FILE_IS_FOLDER) == 0)
    {
        retval = 1;
    }

Finished:
    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pvalue);
        pvalue = NULL;
    }

    if(retval)
    {
        RETURN_TRUE;
    }
    else if(!iscached)
    {
        UCG(orig_is_file)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
    else
    {
        if(FAILED(result))
        {
            dprintimportant("ucache_is_file failed with error %u", result);
        }

        RETURN_FALSE;
    }
}

/* is_dir implemented in ext\standard\file.c */
static void ucache_is_dir(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result       = NONFATAL;
    char *         filename     = NULL;
    size_t         filename_len = 0;
    char *         respath      = NULL;
    fcache_value * pvalue       = NULL;
    unsigned char  retval       = 0;
    unsigned char  iscached     = 0;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p", &filename, &filename_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_is_dir - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &respath, &pvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    iscached = 1;

    if(pvalue != NULL && (pvalue->file_flags & FILE_IS_FOLDER) == 1)
    {
        retval = 1;
    }

Finished:

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pvalue);
        pvalue = NULL;
    }

    if(retval)
    {
        RETURN_TRUE;
    }
    else if (!iscached)
    {
        UCG(orig_is_dir)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }
    else
    {
        if(FAILED(result))
        {
            dprintimportant("ucache_is_dir failed with error %u", result);
        }

        RETURN_FALSE;
    }
}

/* overwriting the rmdir implemented in ext\standard\file.c */
UCACHE_FUNC(ucache_rmdir)
{
    int                result       = NONFATAL;
    char *             dirname      = NULL;
    size_t             dirname_len  = 0;
    char *             respath      = NULL;
    fcache_value *     pvalue       = NULL;
    unsigned char      retval       = 1;
    char *             fullpath     = NULL;
    unsigned char      directory_removed = 0;
    zval              *zcontext     = NULL;

    dprintverbose("start ucache_rmdir");

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p|r", &dirname, &dirname_len, &zcontext) == FAILURE)
    {
        RETURN_FALSE;
    }

    if(dirname_len == 0)
    {
        goto Finished;
    }

    if(IS_ABSOLUTE_PATH(dirname, dirname_len))
    {
        fullpath = dirname;
    }
    else
    {
        fullpath = utils_fullpath(dirname, dirname_len);
        if(fullpath == NULL)
        {
            fullpath = dirname;
        }
    }

    result = aplist_fcache_get(UCG(lfcache), fullpath, SKIP_STREAM_OPEN_CHECK, &respath, &pvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* remove directory first */
    dprintverbose("ucache_rmdir - %s. Calling intercepted function.", dirname);
    UCG(orig_rmdir)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    directory_removed = 1;

    if (Z_TYPE_INFO_P(return_value) == IS_TRUE)
    {
        utils_wait_for_listener(respath, RMDIR_WAIT_TIME);

        /* Mark the aplist entry as deleted */
        result = aplist_fcache_delete(UCG(lfcache), fullpath);
    }

Finished:

    if(fullpath != NULL && fullpath != dirname)
    {
        alloc_efree(fullpath);
        fullpath = NULL;
    }

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pvalue);
        pvalue = NULL;
    }

    if (!directory_removed)
    {
        dprintverbose("ucache_rmdir - %s. Calling intercepted function.", dirname);
        UCG(orig_rmdir)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
        directory_removed = 1;
    }
    else
    {
        if(FAILED(result))
        {
            dprintimportant("ucache_rmdir failed with error %u", result);
        }
    }

    dprintverbose("end ucache_rmdir");
}

/* file_get_contents implemented in tsrm\tsrm_win32.c */
static void ucache_realpath(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result        = NONFATAL;
    char *         filename      = NULL;
    size_t         filename_len  = 0;
    char *         respath       = NULL;
    fcache_value * pfvalue       = NULL;
    unsigned char  iscached      = 0;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p", &filename, &filename_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_realpath - %s", filename);

    result = aplist_fcache_get(UCG(lfcache), filename, SKIP_STREAM_OPEN_CHECK, &respath, &pfvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    iscached = 1;

    RETVAL_STRING(respath);

Finished:

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pfvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pfvalue);
        pfvalue = NULL;
    }

    if (!iscached)
    {
        UCG(orig_realpath)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    }

    return;
}

static void ucache_unlink(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result        = NONFATAL;
    char *         filename      = NULL;
    size_t         filename_len  = 0;
    char *         respath       = NULL;
    char *         fullpath      = NULL;
    unsigned char  file_removed  = 0;
    fcache_value * pvalue        = NULL;
    zval         * zcontext      = NULL;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "p|r", &filename, &filename_len, &zcontext) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (!filename || filename_len == 0 || filename[0] == '\0')
    {
        goto Finished;
    }

    dprintverbose("ucache_unlink - %s", filename);

    if(IS_ABSOLUTE_PATH(filename, filename_len))
    {
        fullpath = filename;
    }
    else
    {
        fullpath = utils_fullpath(filename, filename_len);
        if(fullpath == NULL)
        {
            fullpath = filename;
        }
    }

    result = aplist_fcache_get(UCG(lfcache), fullpath, SKIP_STREAM_OPEN_CHECK, &respath, &pvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* remove directory first */
    dprintverbose("ucache_unlink - %s. Calling intercepted function.", filename);
    UCG(orig_unlink)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    file_removed = 1;

    if (Z_TYPE_INFO_P(return_value) == IS_TRUE)
    {
        utils_wait_for_listener(respath, RMDIR_WAIT_TIME);

        /* Mark the aplist entry as deleted */
        result = aplist_fcache_delete(UCG(lfcache), fullpath);
    }

Finished:
    if(fullpath != NULL && fullpath != filename)
    {
        alloc_efree(fullpath);
        fullpath = NULL;
    }

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pvalue);
        pvalue = NULL;
    }

    if (!file_removed)
    {
        dprintverbose("ucache_unlink - %s. Calling intercepted function.", filename);
        UCG(orig_unlink)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
        file_removed = 1;
    }
    else
    {
        if(FAILED(result))
        {
            dprintimportant("ucache_unlink failed with error %u", result);
        }
    }

    dprintverbose("end ucache_unlink");
}

static void ucache_rename(INTERNAL_FUNCTION_PARAMETERS)
{
    int            result        = NONFATAL;
    char *         srcname       = NULL;
    size_t         srcname_len   = 0;
    char *         dstname       = NULL;
    size_t         dstname_len   = 0;
    char *         respath       = NULL;
    char *         srcfullpath   = NULL;
    unsigned char  file_removed  = 0;
    fcache_value * pvalue        = NULL;
    zval         * zcontext      = NULL;

    if (!UCG(reroute_enabled))
    {
        goto Finished;
    }

    if(UCG(lfcache) == NULL)
    {
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "pp|r", &srcname, &srcname_len, &dstname, &dstname_len, &zcontext) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (srcname_len == 0 || dstname_len == 0)
    {
        goto Finished;
    }

    dprintverbose("ucache_rename - %s,%s", srcname, dstname);

    if(IS_ABSOLUTE_PATH(srcname, srcname_len))
    {
        srcfullpath = srcname;
    }
    else
    {
        srcfullpath = utils_fullpath(srcname, srcname_len);
        if(srcfullpath == NULL)
        {
            srcfullpath = srcname;
        }
    }

    result = aplist_fcache_get(UCG(lfcache), srcfullpath, SKIP_STREAM_OPEN_CHECK, &respath, &pvalue);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* remove directory first */
    dprintverbose("ucache_rename - %s,$s. Calling intercepted function.", srcname, dstname);
    UCG(orig_rename)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
    file_removed = 1;

    if (Z_TYPE_INFO_P(return_value) == IS_TRUE)
    {
        utils_wait_for_listener(respath, RMDIR_WAIT_TIME);

        /* Mark the aplist entry as deleted */
        result = aplist_fcache_delete(UCG(lfcache), srcfullpath);
    }

Finished:
    if(srcfullpath != NULL && srcfullpath != srcname)
    {
        alloc_efree(srcfullpath);
        srcfullpath = NULL;
    }

    if(respath != NULL)
    {
        alloc_efree(respath);
        respath = NULL;
    }

    if(pvalue != NULL)
    {
        aplist_fcache_close(UCG(lfcache), pvalue);
        pvalue = NULL;
    }

    if (!file_removed)
    {
        dprintverbose("ucache_rename - %s,$s. Calling intercepted function.", srcname, dstname);
        UCG(orig_rename)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
        file_removed = 1;
    }
    else
    {
        if(FAILED(result))
        {
            dprintimportant("ucache_rename failed with error %u", result);
        }
    }

    dprintverbose("end ucache_rename");
}


/* {{{ void ucache_intercept_functions_init() */
#define UCACHE_INTERCEPT(func) \
    UCG(orig_##func) = NULL;\
    orig = zend_hash_str_find_ptr(CG(function_table), #func, sizeof(#func)-1); \
    if (orig && orig->type == ZEND_INTERNAL_FUNCTION) { \
    UCG(orig_##func) = orig->internal_function.handler; \
    orig->internal_function.handler = ucache_##func; \
    }

void ucache_intercept_functions_init()
{
    zend_function * orig;

    UCACHE_INTERCEPT(rmdir);
    UCACHE_INTERCEPT(file_exists);
    UCACHE_INTERCEPT(file_get_contents);
    UCACHE_INTERCEPT(filesize);
    UCACHE_INTERCEPT(is_dir);
    UCACHE_INTERCEPT(is_file);
    UCACHE_INTERCEPT(is_readable);
    UCACHE_INTERCEPT(is_writable);
    UCACHE_INTERCEPT(is_writeable);
    UCACHE_INTERCEPT(readfile);
    UCACHE_INTERCEPT(realpath);
    UCACHE_INTERCEPT(unlink);
    UCACHE_INTERCEPT(rename);
    dprintverbose("ucache_intercept_functions_init called");
}
/* }}} */

/* {{{ void ucache_intercept_functions_shutdown() */
#define UCACHE_RELEASE(func) \
    if (UCG(orig_##func) && NULL != (orig = zend_hash_str_find_ptr(CG(function_table), #func, sizeof(#func)-1))) { \
        orig->internal_function.handler = UCG(orig_##func); \
    } \
    UCG(orig_##func) = NULL;

void ucache_intercept_functions_shutdown()
{
    zend_function * orig;

    UCACHE_RELEASE(rmdir);
    UCACHE_RELEASE(file_exists);
    UCACHE_RELEASE(file_get_contents);
    UCACHE_RELEASE(filesize);
    UCACHE_RELEASE(is_dir);
    UCACHE_RELEASE(is_file);
    UCACHE_RELEASE(is_readable);
    UCACHE_RELEASE(is_writable);
    UCACHE_RELEASE(is_writeable);
    UCACHE_RELEASE(readfile);
    UCACHE_RELEASE(realpath);
    UCACHE_RELEASE(unlink);
    UCACHE_RELEASE(rename);
    dprintverbose("ucache_intercept_functions_shutdown called");
}
/* }}} */

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

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z|zl", &pzkey, &pzval, &ttl) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

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

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "z|zl", &pzkey, &pzval, &ttl) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

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

            if(Z_TYPE_P(hentry) == IS_LONG)
            {
                convert_to_string(hentry);
            }

            result = zvcache_delete(UCG(zvucache), Z_STRVAL_P(hentry));
            if(SUCCEEDED(result))
            {
                add_next_index_zval(return_value, hentry);
                Z_TRY_ADDREF_P(hentry);
            }
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

PHP_FUNCTION(ucache_scache_info)
{
    int                  result      = NONFATAL;
    zend_llist *         plist       = NULL;
    zvcache_info_entry * peinfo      = NULL;
    zval                 zfentries;
    zval                 zfentry;
    zend_ulong           index       = 1;
    const char *         valuetype   = NULL;
    zvcache_info         zvinfo      = {0};
    zvcache_info         zvtempinfo  = {0};
    zend_bool            summaryonly = 0;
    zvcache_context *    pcache      = NULL;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &summaryonly) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* Fill the array and then call zend_llist_destroy */
    array_init(return_value);

    array_init(&zfentries);

    if(UCG(phscache) != NULL)
    {
        plist = (zend_llist *)alloc_emalloc(sizeof(zend_llist));
        if(plist == NULL)
        {
            result = FATAL_OUT_OF_LMEMORY;
            goto Finished;
        }

        ZEND_HASH_FOREACH_PTR(UCG(phscache), pcache)
        {
            result = zvcache_list(pcache, summaryonly, NULL, &zvtempinfo, plist);
            if(FAILED(result))
            {
                goto Finished;
            }

            zvinfo.initage   =  zvtempinfo.initage;
            zvinfo.islocal   =  zvtempinfo.islocal;
            zvinfo.itemcount += zvtempinfo.itemcount;
            zvinfo.hitcount  += zvtempinfo.hitcount;
            zvinfo.misscount += zvtempinfo.misscount;

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

            zend_llist_destroy(plist);
            zend_hash_move_forward(UCG(phscache));
        } ZEND_HASH_FOREACH_END();

        alloc_efree(plist);
        plist = NULL;
    }

    /* If cache is not initialized, properties will be 0 */
    add_assoc_long(return_value, "total_cache_uptime", zvinfo.initage);
    add_assoc_bool(return_value, "is_local_cache", zvinfo.islocal);
    add_assoc_long(return_value, "total_item_count", zvinfo.itemcount);
    add_assoc_long(return_value, "total_hit_count", zvinfo.hitcount);
    add_assoc_long(return_value, "total_miss_count", zvinfo.misscount);

    add_assoc_zval(return_value, "scache_entries", &zfentries);

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in ucache_scache_info", result);

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

static void wclocks_destructor(void * pdestination)
{
    wclock_context *  plock  = NULL;
    wclock_context ** pplock = NULL;

    _ASSERT(pdestination != NULL);

    pplock = (wclock_context **)pdestination;
    plock  = *pplock;
    pplock = NULL;

    _ASSERT(plock != NULL);

    lock_terminate(plock->lockobj);
    lock_destroy(plock->lockobj);

    alloc_pefree(plock);
    plock = NULL;

    return;
}

PHP_FUNCTION(ucache_lock)
{
    int               result   = NONFATAL;
    char *            key      = NULL;
    size_t            keylen   = 0;
    char              lockname[  MAX_PATH];
    zend_bool         isglobal = 0;
    wclock_context *  plock    = NULL;
    lock_context *    pcontext = NULL;

    /* Create hashtable if required */
    if(UCG(wclocks) == NULL)
    {
        UCG(wclocks) = (HashTable *)alloc_pemalloc(sizeof(HashTable));
        if(UCG(wclocks) == NULL)
        {
            result = FATAL_OUT_OF_LMEMORY;
            goto Finished;
        }

        zend_hash_init(UCG(wclocks), 0, NULL, wclocks_destructor, 1);
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "s|b", &key, &keylen, &isglobal) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(keylen > LOCK_KEY_MAXLEN)
    {
        php_error_docref(NULL, E_ERROR, "lock key should be less than %d characters", LOCK_KEY_MAXLEN);

        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* Look for this key in wclocks hashtable */
    plock = zend_hash_str_find_ptr(UCG(wclocks), key, keylen);
    if(plock == NULL)
    {
        ZeroMemory(lockname, MAX_PATH);
        strcpy(lockname, "__wclocks__");
        strcat(lockname, key);

        result = lock_create(&pcontext);
        if(FAILED(result))
        {
            goto Finished;
        }

        /* Use global or shared locktype based on isglobal value */
        result = lock_initialize(pcontext, lockname, 1, ((isglobal) ? LOCK_TYPE_GLOBAL : LOCK_TYPE_SHARED), NULL);
        if(FAILED(result))
        {
            goto Finished;
        }

        plock = alloc_pemalloc(sizeof(wclock_context));
        if(plock == NULL)
        {
            result = FATAL_OUT_OF_LMEMORY;
            goto Finished;
        }

        plock->lockobj = pcontext;
        plock->tcreate = GetTickCount();
        plock->tuse    = 0;

        zend_hash_str_add_ptr(UCG(wclocks), key, keylen, plock);
    }
    else
    {
        pcontext = plock->lockobj;
    }

    _ASSERT(plock    != NULL);
    _ASSERT(pcontext != NULL);

    if(pcontext->state == LOCK_STATE_LOCKED)
    {
        /* Ignoring call to ucache_lock as lock is already acquired */
        result = WARNING_LOCK_IGNORE;
        goto Finished;
    }

    lock_lock(pcontext);
    plock->tuse = GetTickCount();

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in ucache_lock", result);

        /* Delete the lock object in case of fatal errors */
        if(result < WARNING_COMMON_BASE && plock == NULL)
        {
            if(pcontext != NULL)
            {
                lock_terminate(pcontext);
                lock_destroy(pcontext);

                pcontext = NULL;
            }
        }

        RETURN_FALSE;
    }

    RETURN_TRUE;
}

PHP_FUNCTION(ucache_unlock)
{
    int               result   = NONFATAL;
    lock_context *    pcontext = NULL;
    wclock_context *  plock    = NULL;
    char *            key      = NULL;
    size_t            keylen   = 0;

    if(UCG(wclocks) == NULL)
    {
        result = FATAL_UNEXPECTED_FCALL;
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "s", &key, &keylen) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    if(keylen > LOCK_KEY_MAXLEN)
    {
        php_error_docref(NULL, E_ERROR, "lock key should be less than %d characters", LOCK_KEY_MAXLEN);

        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    /* Look for this key in wclocks hashtable */
    plock = zend_hash_str_find_ptr(UCG(wclocks), key, keylen);
    if(plock == NULL)
    {
        result = FATAL_LOCK_NOT_FOUND;
        goto Finished;
    }

    pcontext = plock->lockobj;
    _ASSERT(pcontext != NULL);

    if(pcontext->state != LOCK_STATE_LOCKED)
    {
        /* Ignoring call to unlock as the lock is not acquired yet */
        result = WARNING_LOCK_IGNORE;
        goto Finished;
    }

    lock_unlock(pcontext);
    plock->tuse = GetTickCount();

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in ucache_unlock", result);

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
    aplist_runtest();
    rplist_runtest();
    fcache_runtest();

    dprintverbose("end ucache_runtests");
    return;
}
#endif /* UCACHE_TEST */

#ifdef UCACHE_TEST
PHP_FUNCTION(ucache_fcache_find)
{
    int                  result    = NONFATAL;
    cache_info *         pcinfo    = NULL;
    cache_entry_info *   peinfo    = NULL;
    char *               filename  = NULL;
    size_t               filelen   = 0;
    int                  found     = 0;

    if(UCG(lfcache) == NULL ||
       zend_parse_parameters(ZEND_NUM_ARGS(), "s", &filename, &filelen) == FAILURE)
    {
        goto Finished;
    }

    result = aplist_getinfo(UCG(lfcache), CACHE_TYPE_FILECONTENT, FALSE, &pcinfo);
    if(FAILED(result))
    {
        goto Finished;
    }

    peinfo = pcinfo->entries;
    while(peinfo != NULL)
    {
        if (!_stricmp(peinfo->filename, filename))
        {
            found = 1;
            break;
        }

        peinfo = peinfo->next;
    }

Finished:

    if(pcinfo != NULL)
    {
        aplist_freeinfo(CACHE_TYPE_FILECONTENT, pcinfo);
        pcinfo = NULL;
    }

    if(found)
    {
        RETURN_TRUE;
    }

    RETURN_FALSE;
}
#endif /* UCACHE_TEST */

#ifdef UCACHE_TEST
PHP_FUNCTION(ucache_fcnotify_fileinfo)
{
    int                   result      = NONFATAL;
    fcnotify_info *       pcinfo      = NULL;
    fcnotify_entry_info * peinfo      = NULL;
    zval                  zfentries;
    zval                  zfentry;
    zend_ulong            index       = 1;
    zend_bool             summaryonly = 0;

    if(UCG(lfcache) == NULL || UCG(lfcache)->pnotify == NULL)
    {
        goto Finished;
    }

    if(zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &summaryonly) == FAILURE)
    {
        result = FATAL_INVALID_ARGUMENT;
        goto Finished;
    }

    result = fcnotify_getinfo(UCG(lfcache)->pnotify, summaryonly, &pcinfo);
    if(FAILED(result))
    {
        goto Finished;
    }

    array_init(return_value);
    add_assoc_long(return_value, "total_folder_count", pcinfo->itemcount);

    array_init(&zfentries);

    peinfo = pcinfo->entries;
    while(peinfo != NULL)
    {
        array_init(&zfentry);

        add_assoc_string(&zfentry, "folder_path", peinfo->folderpath);
        add_assoc_long(&zfentry, "owner_pid", peinfo->ownerpid);
        add_assoc_long(&zfentry, "file_count", peinfo->filecount);

        add_index_zval(&zfentries, index, &zfentry);
        peinfo = peinfo->next;
        index++;
    }

    add_assoc_zval(return_value, "fcnotify_entries", &zfentries);

Finished:

    if(pcinfo != NULL)
    {
        fcnotify_freeinfo(pcinfo);
        pcinfo = NULL;
    }

    return;
}
#endif /* UCACHE_TEST */

#ifdef UCACHE_TEST
PHP_FUNCTION(ucache_fcnotify_meminfo)
{
    int          result = NONFATAL;
    alloc_info * pinfo  = NULL;

    if(UCG(lfcache) == NULL || UCG(lfcache)->pnotify == NULL)
    {
        goto Finished;
    }

    result = alloc_getinfo(UCG(lfcache)->pnotify->fcalloc, &pinfo);
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
#endif /* UCACHE_TEST */

