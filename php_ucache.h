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
   | Module: php_ucache.h                                                                         |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _PHP_UCACHE_H_
#define _PHP_UCACHE_H_

extern zend_module_entry ucache_module_entry;
#define phpext_ucache_ptr &ucache_module_entry

/* For static builds we need all the headers as this */
/* file is included in main/internal_functions.c */
#ifndef COMPILE_DL_UCACHE
#include "precomp.h"
#endif

/* Module globals */
/* Fields backed by OnUpdateLong ini entries are zend_long because */
/* OnUpdateLong writes a full zend_long at the field address */
ZEND_BEGIN_MODULE_GLOBALS(ucache)
    zvcache_context *        zvucache;    /* User controlled user cache */
    zend_long                ucgc_threshold;  /* Free memory % at or below which writes run the scavenger, 0 = off */
    zend_long                ucgc_interval;  /* Minimum seconds between forced (ucache_gc) scavenger runs */
    zend_long                ucgc_ttl;  /* Scavenger ttl in seconds, 0 = use each entry's ttl */
    zend_long                ucgc_step;  /* Hash buckets the scavenger scans per run */
    zend_bool                enablecli;   /* Enable ucache for command line sapi */
    zend_bool                ucenabled;   /* User cache enabled or disabled */
    zend_long                ucachesize;  /* User cache size in MBs */
    unsigned int             debuglevel;  /* Debug dump level (0/101/201/301/401/501) */
    char *                   namesalt;    /* Salt to use in all the names */
    zend_bool                localheap;   /* Local heap is enabled or disabled */

    HashTable *              zvcopied;    /* Copied zvals to make refcounting work */
    unsigned int             uclasterror; /* Last error value encountered by user cache */
    unsigned int             parentpid;   /* Parent process identifier */
    filemap_global_context * fmapgdata;   /* Global data for filemap */
    const char *             apppoolid;   /* The application id. */
    char *                   filemapdir;  /* Directory where temp filemap files should be created */
ZEND_END_MODULE_GLOBALS(ucache)

ZEND_EXTERN_MODULE_GLOBALS(ucache)

#ifdef ZTS
#define UCG(v) ZEND_TSRMG(ucache_globals_id, zend_ucache_globals *, v)
# ifdef COMPILE_DL_UCACHE
ZEND_TSRMLS_CACHE_EXTERN();
# endif
#else
#define UCG(v) (ucache_globals.v)
#endif

#endif /* _PHP_UCACHE_H_ */
