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
   | Module: ucache_zvcache.h                                                                     |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _UCACHE_ZVCACHE_H_
#define _UCACHE_ZVCACHE_H_

typedef struct _zvcache_hashtable_pool_tracker zvcache_hashtable_pool_tracker;
struct _zvcache_hashtable_pool_tracker
{
    size_t        val;                   /* Hashtable *ht; Offset */
    size_t        hoff;                  /* Memory pool offset */
};

/*
 * An entry.  Its key, with the NUL, follows the structure in the same block (ZVCACHE_KEY in
 * ucache_zvcache.c).  Times are GetTickCount64() values, which do not wrap.
 */
typedef struct zvcache_value zvcache_value;
struct zvcache_value
{
    size_t            zvalue;            /* Offset of zval value stored */
    size_t            sizeb;             /* Memory allocated for zvalue in bytes */

    ULONGLONG         add_ticks;         /* GetTickCount64() when the entry was created */
    ULONGLONG         use_ticks;         /* GetTickCount64() when the entry was last used */
    unsigned int      ttlive;            /* Max time to live for this entry */

    unsigned int      hitcount;          /* Hit count for this entry */
    size_t            next_value;        /* Next value with same index */
    size_t            prev_value;        /* Previous value with same index */
};

typedef struct zvcache_header zvcache_header;
struct zvcache_header
{
    unsigned int      mapcount;          /* Number of processes using this zval cache */
    unsigned int      hitcount;          /* Cache items cumulative hit count */
    unsigned int      misscount;         /* Cache items cumulative miss count */
    ULONGLONG         init_ticks;        /* GetTickCount64() when the cache was created */
    unsigned int      last_owner;        /* PID of last owner for lock */

    ULONGLONG         lscavenge;         /* GetTickCount64() of the last scavenger step, or of a look at the free memory that skipped one (see zvcache_run_scavenger) */
    ULONGLONG         scwait;            /* Milliseconds after lscavenge before a write runs the next step (see zvcache_run_scavenger) */
    unsigned int      scstart;           /* Index from where scavenger should start */
    unsigned int      itemcount;         /* Number of active items in zval cache */
    unsigned int      valuecount;        /* Total values starting from last entry */
    ULONGLONG         scticks;           /* GetTickCount64() when the last scavenger step ran */
    unsigned int      sccount;           /* Entries the last scavenger step removed */
    size_t            scruns;            /* Scavenger steps run since the cache was created */
    size_t            scremoved;         /* Entries the scavenger removed since the cache was created */
    size_t            scevicted;         /* Of those, entries evicted before their time (ucache.gc.evict) */
    ULONGLONG         rcticks;           /* GetTickCount64() of the last full pass for a full cache that left no room */
    unsigned int      rcfutile;          /* 1: that pass removed nothing and the write still did not fit */
    ULONGLONG         rcwait;            /* Milliseconds the processes skip the full pass after such a pass */
    unsigned int      rcgen;             /* Full passes run since the cache was created */
    size_t            values[1];         /* valuecount zvcache_value offsets */
};

/* What phase 1 of a copy-in collected (see copyin_prepare in ucache_zvcache.c) */
typedef struct zvcopy_prepared zvcopy_prepared;

/* The C stack that the walk of a top-level copy may use (see ucache_stack_exhausted in ucache_zvcache.c) */
typedef struct zvcopy_stack zvcopy_stack;

typedef struct zvcopy_context zvcopy_context;
struct zvcopy_context
{
    unsigned int      oomcode;           /* Out of memory error code to use */
    void *            palloc;            /* Allocator for alloc_a* functions */
    char *            pbaseadr;          /* Base address of the segment */
    size_t            hoffset;           /* Offset of mpool_header */
    size_t            allocsize;         /* Amount of memory allocated */

    fn_malloc         fnmalloc;          /* Function to use for malloc */
    fn_free           fnfree;            /* Function to use for free */

    /* In the private copies of incopy and outcopy that create_zvcache_data() and zvcache_get() use */
    zvcopy_stack *    pstack;            /* The C stack of the copy */

    /*
     * Copy-in only, in the private copy of incopy that create_zvcache_data() uses. The last four
     * record what the copy has allocated so far, so that a fatal error (zend_bailout) during the
     * copy can give it back (copyin_rollback).
     */
    zvcopy_prepared * pprepared;         /* Serialized objects and reference values of the value */
    zvcache_value *   pnewvalue;         /* The entry being created */
    zval *            pnewzval;          /* Its top-level zval */
    zvcache_hashtable_pool_tracker * ptracker; /* Pool tracker of a top-level array */
    size_t            mpool;             /* Memory pool of a top-level array (offset) */
};

typedef struct zvcache_context zvcache_context;
struct zvcache_context
{
    unsigned int      id;                /* unique identifier for cache */
    unsigned short    cachekey;          /* unique cache key used in names */
    HANDLE            hinitdone;         /* init mutex (ZVCACHE_INIT), owned while the process creates or opens the cache */

    zvcopy_context *  incopy;            /* zvcopy context to use for non-array copyin */
    zvcopy_context *  outcopy;           /* zvcopy context to use for all copyout */

    char *            zvmemaddr;         /* base addr of memory segment */
    zvcache_header *  zvheader;          /* zvcache cache header */
    filemap_context * zvfilemap;         /* filemap where zvcache is kept */
    lock_context *    zvlock;            /* lock for zvcache header */
    alloc_context *   zvalloc;           /* alloc context for zvcache segment */
    size_t            freelist;          /* Entries unlinked under the lock, freed once it is released (chained by next_value) */
    unsigned int      freecount;         /* How many entries freelist holds */
};

typedef struct zvcache_info zvcache_info;
struct zvcache_info
{
    unsigned int      initage;           /* Seconds elapsed after cache init */
    unsigned int      hitcount;          /* Cumulative hitcount of zvcache */
    unsigned int      misscount;         /* Cumulative misscount of zvcache */
    unsigned int      itemcount;         /* Number of items including stale ones */
    zend_bool         scavenged;         /* A scavenger step ran since the cache was created */
    unsigned int      scage;             /* Seconds since the last scavenger step */
    unsigned int      sccount;           /* Entries the last scavenger step removed */
    size_t            scremoved;         /* Entries the scavenger removed since the cache was created */
    size_t            scevicted;         /* Of those, entries evicted before their time */
};

typedef struct zvcache_info_entry zvcache_info_entry;
struct zvcache_info_entry
{
    char *            key;               /* cache item key */
    unsigned int      ttl;               /* ttl of this entry */
    unsigned int      age;               /* Age in seconds */
    zend_uchar        type;              /* type of zval which is stored as value */
    size_t            sizeb;             /* memory allocated for zval in bytes */
    unsigned int      hitcount;          /* hitcount for this entry */
};

extern int  zvcache_create(zvcache_context ** ppcache);
extern void zvcache_destroy(zvcache_context * pcache);

extern int  zvcache_initialize(
    zvcache_context * pcache,
    unsigned short cachekey,
    unsigned int cachesize
    );

extern void zvcache_terminate(zvcache_context * pcache);

extern int  zvcache_get(zvcache_context * pcache, const char * key, zval ** pvalue);
extern int  zvcache_set(zvcache_context * pcache, const char * key, zval * pzval, unsigned int ttl, unsigned char isadd);
extern int  zvcache_delete(zvcache_context * pcache, const char * key);
extern int  zvcache_clear(zvcache_context * pcache);
extern int  zvcache_exists(zvcache_context * pcache, const char * key, unsigned char * pexists);
extern int  zvcache_list(zvcache_context * pcache, zend_bool summaryonly, char * pkey, zend_ulong offset, zend_ulong limit, zvcache_info * pcinfo, zend_llist * plist);
extern int  zvcache_change(zvcache_context * pcache, const char * key, zend_long delta, zend_long * newvalue);
extern int  zvcache_compswitch(zvcache_context * pcache, const char * key, zend_long oldvalue, zend_long newvalue);
extern int  zvcache_run_scavenger(zvcache_context * pcache, unsigned int force);
extern void zvcache_scavenge_periodic(zvcache_context * pcache);
extern zend_bool zvcache_over_memory_limit(void);
extern zend_bool zvcache_peak_over_memory_limit(size_t peak0);

#endif /* _UCACHE_ZVCACHE_H_ */
