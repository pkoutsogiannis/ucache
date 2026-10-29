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
   | Module: ucache_zvcache.c                                                                     |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   | Revised: Eric Stenson <ericsten@microsoft.com>                                               |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"
#include "ucache_compat.h"

/*
 * NOTES:
 *
 * copyin_* - Copies zend types into memory cache by performing a deep copy.
 * In the process, all pointers are converted to offsets from the beginning of
 * the memory cache segment.  This is done because the user cache is allocated
 * in shared memory at arbitrary addresses in each process.  The copy has two
 * phases (see copyin_prepare): the first holds every part of the value and
 * serializes its objects, the second copies into shared memory without running
 * user code or allocating request memory.
 *
 * copyout_* - Creates new instances of zend types that were previously copied
 * into the memory cache.  This involves converting offsets into pointers which
 * are valid within the current process.  Every copied out value is a fresh,
 * request-local value with exact reference counts; the data that lives in the
 * memory cache is never modified.
 *
 * Both directions use a translation table (original address -> copy) that is
 * local to one top-level copy, so that arrays and references which occur more
 * than once (shared sub-arrays, $a[1] = &$a[0], $a['self'] = &$a) are copied
 * once and keep their identity.
 *
 */

/*
 * zvcache uses pointers as HashTable index values (see ucache_addr_key).  Therefore,
 * we need zend_long's to be at least as big as size_t.
 */
C_ASSERT(SIZEOF_SIZE_T <= SIZEOF_ZEND_LONG);

/*
 * One hash bucket per this many bytes of the cache (the largest prime up to that number of buckets).
 * An entry takes at least about 200 bytes (208 with a key of 40 characters and an integer value), so
 * the chains of a cache full of the smallest entries hold 2 to 3 entries on average, and the buckets
 * (8 bytes each) take about 1.6% of the cache.  WinCache had one bucket per 32 to 64 KB: 197 for 8 MB,
 * whose chains could hold hundreds of small entries each.
 */
#define ZVCACHE_BUCKET_BYTES         512

#ifndef UCACHE_MAX_ZVKEY_LENGTH
#define UCACHE_MAX_ZVKEY_LENGTH      0xFFFF
#endif /* UCACHE_MAX_ZVKEY_LENGTH */

/* Deepest nesting of arrays and references that a value may have (see copyin_prepare_walk) */
#define UCACHE_MAX_NESTING           10000

/* PHP 8.1 and 8.2, which have Fibers but no stack limit: the walks take one of their own, which leaves */
/* UCACHE_STACK_RESERVE, once they are UCACHE_STACK_LAZY deep (see ucache_stack_exhausted) */
#if PHP_VERSION_ID >= 80100 && !defined(ZEND_CHECK_STACK_LIMIT)
# define UCACHE_OWN_STACK_LIMIT      1
#else
# define UCACHE_OWN_STACK_LIMIT      0
#endif
#define UCACHE_STACK_RESERVE         (64 * 1024)
#define UCACHE_STACK_LAZY            (16 * 1024)


#define ZMALLOC(pcopy, size)         (pcopy->fnmalloc(pcopy->palloc, pcopy->hoffset, size))
#define ZFREE(pcopy, addr)           (pcopy->fnfree(pcopy->palloc, pcopy->hoffset, addr))

#define ZOFFSET(pcopy, pbuffer)      ((pbuffer == NULL) ? 0 : (((char *)pbuffer) - (pcopy)->pbaseadr))
#define ZVALUE(pcopy, offset)        ((offset == 0) ? NULL : ((pcopy)->pbaseadr + (offset)))
#define ZVCACHE_VALUE(p, o)          ((zvcache_value *)alloc_get_cachevalue(p, o))
#define ZVALUE_HT_GET_DATA_ADDR(pcopy, ht) \
  ((char*)(ZVALUE((pcopy),(size_t)(ht)->arData)) - HT_HASH_SIZE((ht)->nTableMask))


static int  copyin_prepare(zvcopy_prepared * pprep, zval * pzval);
static int  copyin_prepare_walk(zvcopy_prepared * pprep, zval * pzval, unsigned int depth);
static int  copyin_serialize_objects(zvcopy_prepared * pprep);
static void copyin_free_serialized(zvcopy_prepared * pprep);
static void copyin_release(zvcopy_prepared * pprep);
static void copyin_rollback(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * pxlate);
static int  copyin_memory(zvcopy_context * pcopy, HashTable * phtable, void * src, size_t size, void **ppdest, zend_uchar *pallocated);
static int  copyin_zval(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zval * poriginal, zval ** ppcopied);
static int  copyin_hashtable(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, HashTable * poriginal, HashTable ** ppcopied);
static int  copyin_string(zvcopy_context * pcopy, HashTable *phxlate, zend_string * orig_string, zend_string ** new_string);
static int  copyin_reference(zvcache_context * pcache, zvcopy_context * pcopy, HashTable *phtable, zend_reference * orig_ref, zend_reference ** new_ref);

static int  copyout_zval(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zend_llist * ppending, zval * pcached, zval ** ppcopied);
static int  copyout_hashtable(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zend_llist * ppending, HashTable * pcached, HashTable ** ppcopied);
static int  copyout_reference(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zend_llist * ppending, zend_reference * pcached, zend_reference ** ppnew_ref);

/* At most this many removed entries wait to be freed after the cache lock is released (see remove_zvcache_entry) */
#define FREELIST_MAXIMUM            32

static zvcache_value * find_zvcache_entry(zvcache_context * pcache, const char * key, unsigned int index, unsigned char prune);
static int  create_zvcache_data(zvcache_context * pcache, const char * key, zvcopy_prepared * pprep, unsigned int ttl, zvcache_value ** ppvalue);
static void destroy_cached_zval(zvcache_context * pcache, zval * pzval);
static void destroy_zvcache_data(zvcache_context * pcache, zvcache_value * pvalue);
static void add_zvcache_entry(zvcache_context * pcache, unsigned int index, zvcache_value * pvalue);
static void remove_zvcache_entry(zvcache_context * pcache, unsigned int index, zvcache_value * pvalue);
static void free_pending_entries(zvcache_context * pcache);
static zend_bool entry_expired(zvcache_value * pvalue, unsigned int ticks);
static unsigned int reclaim_entries(zvcache_context * pcache, zend_long percent);

/* Globals */
static unsigned short gzvcacheid = 1;
#pragma warning( push )
#pragma warning( disable : 4146 ) /* we do negative indexing with unsigneds */
static const uint32_t uninitialized_bucket[-HT_MIN_MASK] = {HT_INVALID_IDX, HT_INVALID_IDX};
#pragma warning( pop )

/*
 * The C stack that the walk of a top-level copy may use (see ucache_stack_exhausted), on the C
 * stack of the function that starts the walk.  Used on PHP 8.1 and 8.2 only.
 */
struct zvcopy_stack
{
    char *            plimit;            /* The walk fails at a frame at or below this address */
    unsigned char     known;             /* plimit is the limit; else the frame where the walk takes it */
};

/*
 * What phase 1 of a copy-in collected (see copyin_prepare), on the C stack of zvcache_set().
 * copyin_release() frees it.  The tables are request memory, keyed by ucache_addr_key() of an
 * address; the serializations in the objects table are persistent memory.
 */
struct zvcopy_prepared
{
    zval              value;             /* The value being stored, held */
    HashTable         held;              /* Address -> held zval, for every array, reference and object of the value */
    HashTable         refvals;           /* zend_reference address -> the value it had when the walk reached it, held */
    HashTable         objects;           /* zend_object address -> the object (IS_PTR, held by held) until it is serialized,
                                            then its serialization (persistent string), NULL if empty */
    HashTable         flattened;         /* Address of the symbol table, or of a table that holds slots of it -> its copy
                                            without IS_INDIRECT slots, held (see copyin_prepare_walk); before PHP 8.1 only */
    zvcopy_stack      stack;             /* The C stack of the walk */
};

/* PHP's serializer state, as the code that called ucache left it (see serializer_isolate) */
typedef struct zvcache_serializer_state zvcache_serializer_state;
struct zvcache_serializer_state
{
    unsigned               lock;             /* BG(serialize_lock) */
    php_serialize_data_t   sdata;            /* BG(serialize).data */
    unsigned               slevel;           /* BG(serialize).level */
    php_unserialize_data_t udata;            /* BG(unserialize).data */
    unsigned               ulevel;           /* BG(unserialize).level */
};

/* Private functions */

/*
 * Key of an address in the tables of a copy (held, refvals, objects and the translation tables).
 * zend_hash takes the slot of an integer key from its low bits, and those bits are the same for
 * every block of a size class of an allocator, or of a page: raw addresses would pile up in a few
 * slots, and every lookup would walk a long chain.  The xorshift folds the higher bits into the
 * low ones.  It is a bijection, so distinct addresses stay distinct keys.
 */
static zend_always_inline zend_ulong ucache_addr_key(const void * paddr)
{
    zend_ulong key = (zend_ulong)paddr;

    return key ^ (key >> 12);
}

/*
 * The walks of a copy recurse once per level of arrays and references.  UCACHE_MAX_NESTING
 * (see copyin_prepare_walk) leaves a wide margin on the 64 MB stack of the main thread of php.exe
 * and php-cgi.exe (and fits in the 8 MB of phpdbg.exe), but a Fiber (PHP 8.1+) runs on a stack of
 * its own, 2 MB by default (fiber.stack_size), and the code that calls ucache may have used most
 * of a stack already.  So the walks check the stack that is left and fail cleanly, as PHP's own
 * serialize() does on 8.3+, instead of overflowing it: an overflow ends the process, and in phase
 * 2 it loses the half-built entry for every process of the pool.
 *
 * Where PHP checks its stack limit (8.3+), the walks use it.  PHP 8.1 and 8.2 have Fibers but no
 * such limit (UCACHE_OWN_STACK_LIMIT), so there a walk takes the limit of the stack it runs on:
 * UCACHE_STACK_RESERVE above the bottom of the allocation that holds its frames (VirtualQuery),
 * the main thread's stack or the one PHP allocated for the Fiber, guard page included.  The
 * reserve is for what runs below the deepest check (an allocation, a fatal error, a debug
 * message), with a wide margin; PHP 8.3 leaves 48 KB.  VirtualQuery is a system call, which would
 * add a third to the time of a ucache_get() of a small array, so a walk only takes the limit once
 * it has gone UCACHE_STACK_LAZY below the frame where it started (ucache_stack_start): most values
 * never go that deep, and a walk that starts with less than that left above the limit still stops
 * well inside the reserve.  Each walk takes its own: between the walk of phase 1 and phase 2,
 * serializing the objects runs user code, which can suspend the Fiber or start another one, but no
 * user code runs during a walk.  PHP 7.2 to 8.0 have no Fibers, and their walks do not check: the
 * deepest value takes a few MB of the main thread's stack.
 */
static zend_always_inline void ucache_stack_start(zvcopy_stack * pstack)
{
#if UCACHE_OWN_STACK_LIMIT
    pstack->plimit = (char *)_AddressOfReturnAddress() - UCACHE_STACK_LAZY;
    pstack->known  = 0;
#else
    pstack->plimit = NULL;
    pstack->known  = 1;
#endif
}

#if UCACHE_OWN_STACK_LIMIT
/* A frame at or below plimit (see ucache_stack_exhausted): takes the limit, unless plimit is it already */
static zend_never_inline zend_bool ucache_stack_limit_reached(zvcopy_stack * pstack, char * pframe)
{
    MEMORY_BASIC_INFORMATION mbi;

    if(pstack->known)
    {
        return 1;
    }

    /* If VirtualQuery fails, the walk goes on without a limit, as before */
    pstack->known  = 1;
    pstack->plimit = NULL;

    if(VirtualQuery(pframe, &mbi, sizeof(mbi)) != 0)
    {
        pstack->plimit = (char *)mbi.AllocationBase + UCACHE_STACK_RESERVE;
    }

    return (pframe <= pstack->plimit) ? 1 : 0;
}
#endif

static zend_always_inline zend_bool ucache_stack_exhausted(zvcopy_stack * pstack)
{
#if defined(ZEND_CHECK_STACK_LIMIT)
    return zend_call_stack_overflowed(EG(stack_limit)) ? 1 : 0;
#elif UCACHE_OWN_STACK_LIMIT
    char * pframe = (char *)_AddressOfReturnAddress();

    return (pframe <= pstack->plimit) ? ucache_stack_limit_reached(pstack, pframe) : 0;
#else
    return 0;
#endif
}

/*
 * ucache serializes (copyin_prepare) and unserializes (unserialize_object) as top-level
 * operations, whatever the code that called ucache was doing.
 *
 * PHP lets the serialize() calls that a Serializable::serialize() method makes share the context
 * of the serialize() that called the method, so that back-references (r:N, R:N) can cross that
 * boundary; likewise for unserialize() and Serializable::unserialize().  Around __sleep(),
 * __wakeup(), __serialize() and __unserialize() it raises BG(serialize_lock) instead, and every
 * serialize()/unserialize() called there opens a context of its own.  So a ucache call made from
 * one of these methods would either join the context of its caller, numbering the back-references
 * of the stored value relative to it and shifting the caller's numbering (both come back broken),
 * or run under the lock, where every serialize() that a Serializable::serialize() of the stored
 * value makes opens a fresh context: an object whose serialize() returns serialize($this->data),
 * with $this->data referring back to $this, would be serialized again and again.
 *
 * So the caller's state is set aside, and ucache starts from a clean one: each object is
 * serialized exactly as a top-level serialize() of it would be, the serialize() calls of its own
 * Serializable::serialize() sharing its context, and the caller's context is neither joined nor
 * disturbed.  serializer_restore() gives the caller its state back, also when a fatal error
 * interrupts the serialization (see copyin_prepare).
 */
static void serializer_isolate(zvcache_serializer_state * psaved)
{
    _ASSERT(psaved != NULL);

    psaved->lock   = BG(serialize_lock);
    psaved->sdata  = BG(serialize).data;
    psaved->slevel = BG(serialize).level;
    psaved->udata  = BG(unserialize).data;
    psaved->ulevel = BG(unserialize).level;

    BG(serialize_lock)    = 0;
    BG(serialize).data    = NULL;
    BG(serialize).level   = 0;
    BG(unserialize).data  = NULL;
    BG(unserialize).level = 0;
}

static void serializer_restore(const zvcache_serializer_state * psaved)
{
    _ASSERT(psaved != NULL);

    BG(serialize_lock)    = psaved->lock;
    BG(serialize).data    = psaved->sdata;
    BG(serialize).level   = psaved->slevel;
    BG(unserialize).data  = psaved->udata;
    BG(unserialize).level = psaved->ulevel;
}

/*
 * Phase 1 against memory_limit, counting the serializations that it keeps in persistent memory
 * (see copyin_prepare).  On every PHP version, unlike zvcache_over_memory_limit(): PHP never
 * counts that memory, so nothing else would stop it from growing past memory_limit.  (Where PHP
 * lets a script lower memory_limit below the memory it already uses, see
 * UCACHE_MEMORY_LIMIT_CHECK, that script can store no object until its usage is back under the
 * limit.)
 *
 * The question is whether the serializations would fit under memory_limit next to the memory in
 * use, so this counts the memory in use (zend_memory_usage(0)), not the real size of the heap:
 * that also counts the free chunks that PHP keeps from earlier work, which a FastCGI process
 * carries over from one request to the next, and which serializations kept in the heap would
 * reuse.  Counting them refused values that fit, after a big request had run in the process.
 */
static zend_bool copyin_over_memory_limit(size_t persistent)
{
    zend_long limit = PG(memory_limit);

    if(limit <= 0)
    {
        return 0;
    }

    return (zend_memory_usage(0) + persistent > MAX((size_t)limit, (size_t)ZEND_MM_CHUNK_SIZE)) ? 1 : 0;
}

/*
 * Destructor of the objects table (see struct zvcopy_prepared).  ZVAL_PTR_DTOR would not do:
 * since PHP 7.3 it frees a string with efree(), and the serializations are persistent.  The
 * objects themselves (IS_PTR) are held by the held table.
 */
static void copyin_object_dtor(zval * pzval)
{
    if(Z_TYPE_P(pzval) == IS_STRING)
    {
        zend_string_release(Z_STR_P(pzval));
    }
}

/*
 * Copy-in, phase 1: everything that needs user code or request memory, done before anything is
 * allocated in shared memory.
 *
 * Phase 2 (create_zvcache_data() and the other copyin_* functions) builds the entry in shared
 * memory.  If a fatal error interrupted it, the half-built entry would be lost for every process
 * of the pool: nothing references it, so neither ucache_clear() nor the scavenger can free it.
 * So phase 2 runs no user code and allocates no request memory: the objects are serialized here,
 * and its translation table is persistent memory.  The zend_try around phase 2 is only a safety
 * net: the CVE-patched PHP 7.2 that runs in production ends the process with _exit(1) when
 * memory_limit is reached, which no zend_try can intercept.
 *
 * Serializing runs user code (__sleep(), __serialize(), Serializable::serialize()), which can
 * change or free the parts of the value that are still to be copied.  So the walk first takes a
 * reference on every array, reference and object of the value, without running any user code,
 * and copyin_release() drops them only once phase 2 has finished:
 * - a write by user code separates a held array (copy on write) instead of changing it, so its
 *   elements, keys and strings stay as they were, and stay allocated;
 * - nothing that phase 2 records in its translation table can be freed and its address reused
 *   by something else that is copied later;
 * - a PHP reference is not copied on write, user code can assign it another value.  So the walk
 *   also keeps the value each reference had, and phase 2 copies that value;
 * - the symbol table ($GLOBALS before PHP 8.1) is not copied on write either: the walk takes a
 *   copy of it, and phase 2 copies that copy (see copyin_prepare_walk).
 * The entry thus holds the value as it was when ucache_set() was called.  Only objects are taken
 * as they are when they are serialized, in the order of the walk, as serialize() does.  Each
 * object is serialized once, on its own, as a top-level serialize() of it would be (see
 * serializer_isolate): one that occurs twice comes back as two copies.  A value nested too deeply
 * is refused before any of this (see copyin_prepare_walk).
 *
 * Kept in request memory until phase 2 has copied them, the serializations of all the objects
 * would count against memory_limit at once (the copy-in before the two phases held one at a
 * time), and a value with many objects would no longer fit.  So each one is copied into a
 * persistent string of its exact size, and the request memory it was built in is freed at once.
 * PHP's memory_limit does not see persistent memory: the serializations are checked against it
 * here, together with the request memory in use, on every PHP version, and ucache_set() fails,
 * storing nothing, once they no longer fit, instead of growing the process past memory_limit.
 * Request shutdown does not free persistent memory, and a FastCGI process serves many requests:
 * a fatal error while serializing (in __sleep(), max_execution_time, ...) or in phase 2 frees the
 * serializations before it propagates.  On PHP 7.2, memory_limit reached inside a serialization
 * still ends the process, and its memory with it.
 */
static int copyin_prepare(zvcopy_prepared * pprep, zval * pzval)
{
    int                      result  = NONFATAL;
    zvcache_serializer_state saved;
    volatile unsigned char   bailout = 0;

    dprintverbose("start copyin_prepare");

    _ASSERT(pprep != NULL);
    _ASSERT(pzval != NULL);

    /* Everything is initialized first: the caller calls copyin_release() whatever happens */
    ZVAL_COPY(&pprep->value, pzval);
    zend_hash_init(&pprep->held, 8, NULL, ZVAL_PTR_DTOR, 0);
    zend_hash_init(&pprep->refvals, 8, NULL, ZVAL_PTR_DTOR, 0);
    zend_hash_init(&pprep->objects, 8, NULL, copyin_object_dtor, 0);
    zend_hash_init(&pprep->flattened, 0, NULL, ZVAL_PTR_DTOR, 0);

    ucache_stack_start(&pprep->stack);
    result = copyin_prepare_walk(pprep, &pprep->value, 0);
    if(FAILED(result))
    {
        goto Finished;
    }

    /*
     * The whole value is held now: user code may run.  It cannot reach pprep->objects.  A fatal
     * error in it longjmps out of here: free the serializations made so far, and give the caller
     * its serializer state back, before propagating it.  No goto may leave the zend_try block, so
     * the work is done in copyin_serialize_objects().
     */
    serializer_isolate(&saved);

    zend_try
    {
        result = copyin_serialize_objects(pprep);
    }
    zend_catch
    {
        bailout = 1;
    }
    zend_end_try();

    serializer_restore(&saved);

    if(bailout)
    {
        copyin_free_serialized(pprep);
        zend_bailout();
    }

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in copyin_prepare", result);
    }

    dprintverbose("end copyin_prepare");

    return result;
}

/*
 * Serializes the objects that the walk recorded, in the order of the walk (see copyin_prepare).
 * Runs user code: copyin_prepare() calls it inside a zend_try, with the serializer state isolated.
 */
static int copyin_serialize_objects(zvcopy_prepared * pprep)
{
    int                  result     = NONFATAL;
    size_t               persistent = 0;
    zval *               pentry     = NULL;
    zval                 object;
    smart_str            buffer     = {0};
    php_serialize_data_t serdata;

    ZEND_HASH_FOREACH_VAL(&pprep->objects, pentry)
    {
        /* No reference of its own: the held table keeps the object alive */
        _ASSERT(Z_TYPE_P(pentry) == IS_PTR);
        ZVAL_OBJ(&object, (zend_object *)Z_PTR_P(pentry));

        PHP_VAR_SERIALIZE_INIT(serdata);
        php_var_serialize(&buffer, &object, &serdata);
        PHP_VAR_SERIALIZE_DESTROY(serdata);

        if(EG(exception))
        {
            /*
             * Closure, anonymous class, __sleep()/__serialize() throwing, ...: the buffer holds a
             * truncated serialization.  Store nothing; the exception propagates to the caller of
             * ucache_set()/ucache_add().
             */
            smart_str_free(&buffer);
            result = FATAL_ZVCACHE_INVALID_ZVAL;
            break;
        }

        if(buffer.s != NULL)
        {
            /* Exact size, and NUL terminated, as php_var_unserialize() relies on (see copyin_prepare) */
            persistent += ZEND_MM_ALIGNED_SIZE(_ZSTR_STRUCT_SIZE(ZSTR_LEN(buffer.s)));
            ZVAL_STR(pentry, zend_string_init(ZSTR_VAL(buffer.s), ZSTR_LEN(buffer.s), 1));
            smart_str_free(&buffer);
        }
        else
        {
            ZVAL_NULL(pentry);
        }

        if(copyin_over_memory_limit(persistent))
        {
            result = FATAL_OUT_OF_LMEMORY;
            break;
        }
    }
    ZEND_HASH_FOREACH_END();

    return result;
}

/*
 * The walk of phase 1 (see copyin_prepare): holds every array, reference and object of the value
 * and records the objects to serialize.  Runs no user code.
 *
 * depth is the number of arrays and references that contain *pzval.  A value nested more than
 * UCACHE_MAX_NESTING levels deep, each array and each reference being a level (a top-level array
 * is level 1), is refused as one that contains a resource is, and so is one that would exhaust
 * the C stack (see ucache_stack_exhausted).  Phase 2 and the copy-out recurse once per level of
 * the same structure in the same order: a part that occurs twice is copied where it is reached
 * first, and found in the translation table after that.  So the limit bounds their recursion as
 * well; they check the stack themselves, since their frames are bigger.  Nothing is allocated in
 * shared memory yet, so refusing costs nothing.
 */
static int copyin_prepare_walk(zvcopy_prepared * pprep, zval * pzval, unsigned int depth)
{
    int              result   = NONFATAL;
    zval *           pelem    = NULL;
    zval             value;
    zend_reference * pref     = NULL;
    unsigned char    indirect = 0;

    switch(Z_TYPE_P(pzval))
    {
        case IS_UNDEF:
        case IS_NULL:
        case IS_TRUE:
        case IS_FALSE:
        case IS_LONG:
        case IS_DOUBLE:
        case IS_STRING:
            /* A string lives as long as the held array or reference value that contains it */
            break;

        case IS_ARRAY:
            /* Walked already: a sub-array that occurs twice, or a cycle through a reference */
            if(zend_hash_index_exists(&pprep->held, ucache_addr_key(Z_ARR_P(pzval))))
            {
                break;
            }

            if(depth >= UCACHE_MAX_NESTING || ucache_stack_exhausted(&pprep->stack))
            {
                dprintimportant("copyin_prepare_walk: value nested too deeply (level %u)", depth + 1);
                result = FATAL_ZVCACHE_INVALID_ZVAL;
                break;
            }

            /* Immutable arrays (opcache, the empty array) are not refcounted, and never freed */
            Z_TRY_ADDREF_P(pzval);
            zend_hash_index_add_new(&pprep->held, ucache_addr_key(Z_ARR_P(pzval)), pzval);

            if(zvcache_over_memory_limit())
            {
                result = FATAL_OUT_OF_LMEMORY;
                break;
            }

#if PHP_VERSION_ID < 80100
            /*
             * $GLOBALS before PHP 8.1, wherever it is in the value: the symbol table itself.  Its
             * zval is not refcounted, so holding it protects nothing (user code changes the table
             * in place, it is never copied on write), and its slots point to the variables of the
             * code (IS_INDIRECT).  So the value takes a copy of it now, before any user code runs,
             * that holds the variables themselves, as zend_array_dup() makes it (without the
             * variables that are not set), and phase 2 copies that copy (see copyin_flattened), as
             * PHP's serialize() takes the variables.  Always, not only when the table has an
             * IS_INDIRECT slot: a script without variables at the top level has none.
             */
            if(Z_ARR_P(pzval) == &EG(symbol_table))
            {
                ZVAL_ARR(&value, zend_array_dup(Z_ARR_P(pzval)));
                zend_hash_index_add_new(&pprep->flattened, ucache_addr_key(Z_ARR_P(pzval)), &value);
                result = copyin_prepare_walk(pprep, &value, depth);
                break;
            }
#endif

            ZEND_HASH_FOREACH_VAL(Z_ARR_P(pzval), pelem)
            {
                /* Scalars and strings need nothing (see above) */
                if(Z_TYPE_P(pelem) < IS_ARRAY)
                {
                    continue;
                }

                /* A slot of the symbol table that an array function copied (see below) */
                if(Z_TYPE_P(pelem) == IS_INDIRECT)
                {
                    indirect = 1;
                    break;
                }

                result = copyin_prepare_walk(pprep, pelem, depth + 1);
                if(FAILED(result))
                {
                    break;
                }
            }
            ZEND_HASH_FOREACH_END();

            /*
             * Before PHP 8.1, array_merge(), array_slice(), array_reverse(), array_chunk(),
             * array_values(), ... of $GLOBALS copy the slots of the symbol table as they are
             * (IS_INDIRECT).  A hash table: the value takes a copy of it that holds the variables,
             * as for the symbol table itself.  zend_array_dup() keeps the slots of a packed table
             * (array_values($GLOBALS)), so its copy would be copied again and again, at the same
             * depth, until memory_limit or a stack overflow ended the process: it is refused.
             */
            if(indirect && SUCCEEDED(result))
            {
                if(HT_IS_PACKED(Z_ARR_P(pzval)))
                {
                    dprintimportant("copyin_prepare_walk: a list that holds slots of the symbol table cannot be stored");
                    result = FATAL_ZVCACHE_INVALID_ZVAL;
                    break;
                }

                ZVAL_ARR(&value, zend_array_dup(Z_ARR_P(pzval)));
                zend_hash_index_add_new(&pprep->flattened, ucache_addr_key(Z_ARR_P(pzval)), &value);
                result = copyin_prepare_walk(pprep, &value, depth);
            }
            break;

        case IS_REFERENCE:
            pref = Z_REF_P(pzval);
            if(zend_hash_index_exists(&pprep->held, ucache_addr_key(pref)))
            {
                break;
            }

            if(depth >= UCACHE_MAX_NESTING || ucache_stack_exhausted(&pprep->stack))
            {
                dprintimportant("copyin_prepare_walk: value nested too deeply (level %u)", depth + 1);
                result = FATAL_ZVCACHE_INVALID_ZVAL;
                break;
            }

            Z_ADDREF_P(pzval);
            zend_hash_index_add_new(&pprep->held, ucache_addr_key(pref), pzval);

            /* The value the reference has now is the one phase 2 copies (see copyin_prepare) */
            ZVAL_COPY(&value, &pref->val);
            zend_hash_index_add_new(&pprep->refvals, ucache_addr_key(pref), &value);

            if(zvcache_over_memory_limit())
            {
                result = FATAL_OUT_OF_LMEMORY;
                break;
            }

            /* value is a plain copy of the table's zval, which holds the reference to it */
            result = copyin_prepare_walk(pprep, &value, depth + 1);
            break;

        case IS_OBJECT:
            if(zend_hash_index_exists(&pprep->objects, ucache_addr_key(Z_OBJ_P(pzval))))
            {
                break;
            }

            Z_ADDREF_P(pzval);
            zend_hash_index_add_new(&pprep->held, ucache_addr_key(Z_OBJ_P(pzval)), pzval);

            /* Serialized by copyin_serialize_objects() once the whole value is held */
            ZVAL_PTR(&value, Z_OBJ_P(pzval));
            zend_hash_index_add_new(&pprep->objects, ucache_addr_key(Z_OBJ_P(pzval)), &value);

            if(zvcache_over_memory_limit())
            {
                result = FATAL_OUT_OF_LMEMORY;
            }
            break;

        default:
            /*
             * A resource (ucache_set() refuses a top-level one with a warning, this is one inside
             * an array), ...: nothing is stored.
             */
            result = FATAL_ZVCACHE_INVALID_ZVAL;
            break;
    }

    return result;
}

/*
 * A fatal error interrupted the copy-in, while serializing or in phase 2: zvcache_set() will not
 * get to call copyin_release(), and request shutdown frees request memory only.  So free the
 * serializations (persistent memory, see copyin_prepare) before the error propagates.  The objects
 * table stays usable (empty).  Runs no user code and allocates nothing.
 */
static void copyin_free_serialized(zvcopy_prepared * pprep)
{
    _ASSERT(pprep != NULL);

    zend_hash_clean(&pprep->objects);
}

/*
 * Drops what phase 1 holds.  That can run destructors, i.e. user code: call it only when no lock
 * is held and the new entry is either in the cache or freed (a fatal error in a destructor must
 * not leave a half-built entry behind).
 */
static void copyin_release(zvcopy_prepared * pprep)
{
    _ASSERT(pprep != NULL);

    zend_hash_destroy(&pprep->objects);
    zend_hash_destroy(&pprep->flattened);
    zend_hash_destroy(&pprep->refvals);
    zend_hash_destroy(&pprep->held);

    zval_ptr_dtor(&pprep->value);
    ZVAL_UNDEF(&pprep->value);
}

/*
 * A fatal error (zend_bailout) interrupted phase 2.  None is expected (phase 2 runs no user code
 * and allocates no request memory), this is a safety net: give back what the new entry took, then
 * the caller re-raises the error.  Every nested part of a top-level array lives in its memory
 * pool, so that is the pool, its tracker, the top-level zval and the zvcache_value.  A top-level
 * string or serialized object is the last thing its copy allocates, nothing can fail after it.
 * No lock is held here.
 */
static void copyin_rollback(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * pxlate)
{
    dprintimportant("copyin_rollback: a fatal error interrupted the copy-in, freeing the new entry");

    if(pcopy->mpool != 0)
    {
        alloc_free_mpool(pcache->zvalloc, pcopy->mpool);
        pcopy->mpool = 0;
    }

    if(pcopy->ptracker != NULL)
    {
        ZFREE(pcopy, pcopy->ptracker);
        pcopy->ptracker = NULL;
    }

    if(pcopy->pnewzval != NULL)
    {
        ZFREE(pcopy, pcopy->pnewzval);
        pcopy->pnewzval = NULL;
    }

    if(pcopy->pnewvalue != NULL)
    {
        ZFREE(pcopy, pcopy->pnewvalue);
        pcopy->pnewvalue = NULL;
    }

    /* Persistent memory */
    zend_hash_destroy(pxlate);
}

static int copyin_memory(zvcopy_context * pcopy, HashTable * phtable, void * src, size_t size, void **ppdest, zend_uchar *pallocated)
{
    int         result      = NONFATAL;
    void *      pdest       = NULL;

    _ASSERT(pallocated  != NULL);
    _ASSERT(ppdest      != NULL);
    _ASSERT(*ppdest     == NULL);

    *pallocated = 0;

    if(phtable != NULL)
    {
        /* Check if memory is already copied */
        if((pdest = zend_hash_index_find_ptr(phtable, ucache_addr_key(src))) != NULL)
        {
            *ppdest = pdest;
            goto Finished;
        }
    }

    /* Allocate cache memory & copy the original into the cache. */
    pdest = ZMALLOC(pcopy, size);
    if (pdest == NULL)
    {
        result = pcopy->oomcode;
        goto Finished;
    }

    *pallocated = 1;

    pcopy->allocsize += size;
    memcpy_s(pdest, size, src, size);

    /* update the table */
    if(phtable != NULL)
    {
        zend_hash_index_update_ptr(phtable, ucache_addr_key(src), (void *)pdest);
    }

    *ppdest = pdest;

Finished:

    return result;
}

/* Phase 2: the table to copy for *ptable, phase 1's copy of it if it is the symbol table or holds slots of it (see copyin_prepare_walk) */
static zend_always_inline HashTable * copyin_flattened(zvcopy_context * pcopy, HashTable * ptable)
{
    zval * pflat = NULL;

    if(pcopy->pprepared == NULL || zend_hash_num_elements(&pcopy->pprepared->flattened) == 0)
    {
        return ptable;
    }

    pflat = zend_hash_index_find(&pcopy->pprepared->flattened, ucache_addr_key(ptable));
    return (pflat != NULL) ? Z_ARR_P(pflat) : ptable;
}

/*
 * Copy-in, phase 2 (see copyin_prepare): copies *poriginal into the cache zval *ppcopied, which
 * the caller allocated, and turns its pointers into cache offsets.  phtable is the translation
 * table of the top-level copy.  The top-level value is copied with pcopy->hoffset 0; a top-level
 * array gets a memory pool, and everything nested in it is copied into that pool.
 */
static int copyin_zval(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zval * poriginal, zval ** ppcopied)
{
    int                  result      = NONFATAL;
    zval *               pnewzv      = NULL;
    zend_uchar           allocated   = 0;
    HashTable *          phashtable  = NULL;
    zvcopy_context       poolcopy;
    size_t               offset      = 0;
    zend_string *        pzstr       = NULL;
    zvcache_hashtable_pool_tracker * table_tracker = NULL;
    zend_reference *     pzref       = NULL;
    zval *               pserialized = NULL;
    zend_string *        pdest       = NULL;

    dprintverbose("start copyin_zval");

    _ASSERT(pcache    != NULL);
    _ASSERT(pcopy     != NULL);
    _ASSERT(phtable   != NULL);
    _ASSERT(poriginal != NULL);
    _ASSERT(ppcopied  != NULL);
    _ASSERT(*ppcopied != NULL);

    pnewzv = *ppcopied;
    if (poriginal != pnewzv)
    {
        /* struct copy to pick up flags and such.  Still need to fix up value pointer. */
        *pnewzv = *poriginal;
    }

    switch(Z_TYPE_P(poriginal))
    {
        case IS_UNDEF:
        case IS_NULL:
        case IS_TRUE:
        case IS_FALSE:
        case IS_LONG:
        case IS_DOUBLE:
            break;

        case IS_STRING:
            result = copyin_string(pcopy, phtable, Z_STR_P(poriginal), &pzstr);
            if (FAILED(result))
            {
                goto Finished;
            }

            /* Interned strings are not refcounted (IS_INTERNED_STRING_EX), but copyout always
             * creates a fresh refcounted string.  IS_TYPE_COPYABLE only existed up to 7.2. */
            Z_TYPE_INFO_P(pnewzv) = IS_STRING_EX;

            /* Use offset in cached zval pointer */
            Z_STR_P(pnewzv) = (zend_string *)ZOFFSET(pcopy, pzstr);
            break;

        case IS_ARRAY:
            /*
             * Because we need to remember the offset of the memory pool, all
             * HashTables that are copied into shared memory will use the *ptr
             * field, instead of the *arr field.
             * The *ptr will point at a zvcache_hashtable_pool_tracker structure.
             */

            table_tracker = (zvcache_hashtable_pool_tracker *)ZMALLOC(pcopy, sizeof(zvcache_hashtable_pool_tracker));
            if (table_tracker == NULL)
            {
                result = pcopy->oomcode;
                goto Finished;
            }
            pcopy->allocsize += sizeof(zvcache_hashtable_pool_tracker);

            /* If pcopy is not set to use memory pool, use a different copy context */
            if(pcopy->hoffset == 0)
            {
                /* Top-level array: record its tracker and pool for copyin_rollback() */
                pcopy->ptracker = table_tracker;

                result = alloc_create_mpool(pcache->zvalloc, &offset);
                if(FAILED(result))
                {
                    /* Its FATAL_OUT_OF_MEMORY is the code of request memory: say the cache is full, */
                    /* so that zvcache_set() makes room (reclaim_entries) */
                    result = pcopy->oomcode;
                    goto Finished;
                }

                pcopy->mpool = offset;

                /* On the C stack: the copy must not allocate request memory */
                poolcopy             = *pcopy;
                poolcopy.oomcode     = FATAL_OUT_OF_SMEMORY;
                poolcopy.palloc      = pcache->zvalloc;
                poolcopy.pbaseadr    = pcache->zvmemaddr;
                poolcopy.hoffset     = offset;
                poolcopy.allocsize   = 0;
                poolcopy.fnmalloc    = alloc_ommalloc;
                poolcopy.fnfree      = alloc_omfree;
                poolcopy.pnewvalue   = NULL;
                poolcopy.pnewzval    = NULL;
                poolcopy.ptracker    = NULL;
                poolcopy.mpool       = 0;

                result = copyin_hashtable(pcache, &poolcopy, phtable, copyin_flattened(pcopy, Z_ARR_P(poriginal)), &phashtable);
                pcopy->allocsize += poolcopy.allocsize;
            }
            else
            {
                result = copyin_hashtable(pcache, pcopy, phtable, copyin_flattened(pcopy, Z_ARR_P(poriginal)), &phashtable);
            }

            if(FAILED(result))
            {
                goto Finished;
            }

            /* fix up the flags on the copied in zval (immutable arrays have no IS_TYPE_REFCOUNTED) */
            Z_TYPE_INFO_P(pnewzv) = IS_ARRAY_EX;

            /* Keep offset so that freeing shared memory is easy */
            table_tracker->hoff = offset;
            table_tracker->val = ZOFFSET(pcopy, phashtable);
            phashtable = NULL;
            Z_PTR_P(pnewzv) = (void *)ZOFFSET(pcopy, table_tracker);
            table_tracker = NULL;
            offset = 0;
            break;

        case IS_REFERENCE:
            result = copyin_reference(pcache, pcopy, phtable, Z_REF_P(poriginal), &pzref);
            if(FAILED(result))
            {
                goto Finished;
            }
            Z_TYPE_INFO_P(pnewzv) = IS_REFERENCE_EX;
            Z_REF_P(pnewzv) = (zend_reference *)ZOFFSET(pcopy, pzref);
            break;

        case IS_OBJECT:
            /* Serialized by phase 1 (copyin_prepare) */
            pserialized = zend_hash_index_find(&pcopy->pprepared->objects, ucache_addr_key(Z_OBJ_P(poriginal)));
            if(pserialized == NULL || Z_TYPE_P(pserialized) == IS_PTR)
            {
                /*
                 * An object that phase 1 did not see, or did not serialize.  Phase 2 copies what
                 * phase 1 held, and only runs once phase 1 has serialized every object, so this
                 * cannot happen; never serialize here, that would run user code.
                 */
                _ASSERT(FALSE);
                ZVAL_NULL(pnewzv);
                result = FATAL_ZVCACHE_INVALID_ZVAL;
                goto Finished;
            }

            if(Z_TYPE_P(pserialized) == IS_STRING && Z_STRLEN_P(pserialized) != 0)
            {
                /* Through the translation table: an object that occurs twice is stored once */
                result = copyin_memory(pcopy, phtable, Z_STR_P(pserialized), _ZSTR_STRUCT_SIZE(Z_STRLEN_P(pserialized)), (void **)&pdest, &allocated);
                if(FAILED(result))
                {
                    ZVAL_NULL(pnewzv);
                    goto Finished;
                }

                /* Use Offset in copied-in zval */
                Z_PTR_P(pnewzv) = (zend_string *)ZOFFSET(pcopy, pdest);
            }
            else
            {
                ZVAL_NULL(pnewzv);
            }

            break;

        default:
            /* Resources and IS_INDIRECT are refused by phase 1 already */
            result = FATAL_ZVCACHE_INVALID_ZVAL;
            goto Finished;
    }

    *ppcopied = pnewzv;
    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in copyin_zval", result);

        if(table_tracker != NULL)
        {
            if(pcopy->ptracker == table_tracker)
            {
                pcopy->ptracker = NULL;
            }

            ZFREE(pcopy, table_tracker);
            table_tracker = NULL;
        }

        if(offset != 0)
        {
            if(pcopy->mpool == offset)
            {
                pcopy->mpool = 0;
            }

            alloc_free_mpool(pcache->zvalloc, offset);
            offset = 0;
        }
    }

    dprintverbose("end copyin_zval");

    return result;
}

/*
 * Objects are cached as php_var_serialize() strings.  Unserializing runs user code
 * (autoloaders, __wakeup(), __unserialize(), Serializable::unserialize()), which can call
 * ucache functions that replace, delete or clear the very entry being copied out (its
 * memory would be freed under the copy), and which would otherwise run while the
 * cross-process cache lock is held.  So copyout only copies the string into request
 * memory, leaves a NULL placeholder, and zvcache_get() unserializes the pending objects
 * after it has released the lock.  The placeholders live in tables created by this
 * copyout, which nothing else can reach (or resize) before they are filled in.
 */
typedef struct zvcache_pending_object zvcache_pending_object;
struct zvcache_pending_object
{
    zval *            pzval;             /* NULL placeholder that receives the object */
    zend_string *     pserialized;       /* serialized object, request memory */
};

/*
 * 1 if the object was unserialized, 0 if PHP's unserializer failed (it raised a warning that
 * says why: a class that is not defined, or that changed since the value was stored, an enum or
 * an enum case that is not defined, unserialize_max_depth since 7.4) or an exception was thrown;
 * the placeholder then stays NULL.
 */
static zend_bool unserialize_object(zval * pzval, zend_string * pserialized)
{
    php_unserialize_data_t   serdata = {0};
    const unsigned char *    pbuffer = (const unsigned char *)ZSTR_VAL(pserialized);
    zval *                   ptmp    = NULL;
    zvcache_serializer_state saved;
    zend_bool                done    = 0;

    /*
     * Same pattern as PHP's own unserialize(): the value is built in a temporary that
     * belongs to the unserialize context and only taken on success.  A partially built
     * value is released by PHP_VAR_UNSERIALIZE_DESTROY, which also performs the delayed
     * __wakeup()/__unserialize() calls.  As a top-level unserialize() (see
     * serializer_isolate), wherever ucache_get() was called from.
     */
    serializer_isolate(&saved);

    PHP_VAR_UNSERIALIZE_INIT(serdata);
    ptmp = var_tmp_var(&serdata);
    if(php_var_unserialize(ptmp, &pbuffer, pbuffer + ZSTR_LEN(pserialized), &serdata))
    {
        ZVAL_COPY(pzval, ptmp);
        done = 1;
    }
    else
    {
        dprintimportant("Failed to unserialize cached object: %s", ZSTR_VAL(pserialized));
    }
    PHP_VAR_UNSERIALIZE_DESTROY(serdata);

    /*
     * An object that refers to itself through a PHP reference (R:1) turns the value into a
     * reference shared with that property.  Unwrap it as PHP's unserialize() does, after the
     * delayed __wakeup()/__unserialize() calls: a reference must not be returned, nor end up
     * as an element, or as the value of a reference (a reference inside a reference).
     */
    if(Z_ISREF_P(pzval))
    {
        zend_unwrap_reference(pzval);
    }

    serializer_restore(&saved);

    /* The delayed __wakeup()/__unserialize() calls ran in PHP_VAR_UNSERIALIZE_DESTROY */
    return (done && !EG(exception)) ? 1 : 0;
}

/* 1 if every pending object was unserialized (process must be set), 0 otherwise */
static zend_bool unserialize_pending_objects(zend_llist * ppending, unsigned char process)
{
    zend_llist_position      pos;
    zvcache_pending_object * pobj = NULL;
    zend_bool                ok   = process ? 1 : 0;

    for(pobj = (zvcache_pending_object *)zend_llist_get_first_ex(ppending, &pos);
        pobj != NULL;
        pobj = (zvcache_pending_object *)zend_llist_get_next_ex(ppending, &pos))
    {
        /* After a failure or an exception, leave the remaining placeholders NULL.  Without process
         * (the copy failed), pobj->pzval is not used: zvcache_get() releases the value whole. */
        if(ok)
        {
            ok = unserialize_object(pobj->pzval, pobj->pserialized);
        }

        zend_string_release(pobj->pserialized);
        pobj->pserialized = NULL;
    }

    return ok;
}

/*
 * pcached is the cached zval (value = offset into the cache); *ppcopied is the zval that
 * receives the copied out value.  Both can be the same zval: elements and reference values
 * are memcpy'd from the cache first and then converted in place.  After a failure *ppcopied
 * holds what was built, safe to destroy, or NULL: this runs under the cache lock, and releases
 * nothing (see zvcache_get).
 */
static int copyout_zval(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zend_llist * ppending, zval * pcached, zval ** ppcopied)
{
    int                    result     = NONFATAL;
    zval *                 pnewzv     = NULL;
    zval                   cached;
    zend_string *          ptmp_str   = NULL;
    HashTable *            phashtable = NULL;
    unsigned char          isfirst    = 0;
    HashTable              xlate;
    zvcache_hashtable_pool_tracker * table_tracker = NULL;
    zend_reference *       pref       = NULL;
    zvcache_pending_object pending;

    dprintverbose("start copyout_zval");

    _ASSERT(pcache    != NULL);
    _ASSERT(pcopy     != NULL);
    _ASSERT(pcached   != NULL);
    _ASSERT(ppcopied  != NULL);
    _ASSERT(*ppcopied != NULL);

    pnewzv = *ppcopied;
    cached = *pcached;

    switch(Z_TYPE(cached))
    {
        case IS_RESOURCE:
            /* We don't support caching of zend_resource types */
            _ASSERT(FALSE);
            ZVAL_NULL(pnewzv);
            result = FATAL_ZVCACHE_INVALID_ZVAL;
            goto Finished;

        case IS_TRUE:
        case IS_FALSE:
        case IS_UNDEF:
        case IS_LONG:
        case IS_DOUBLE:
        case IS_NULL:
            if(pnewzv != pcached)
            {
                ZVAL_COPY_VALUE(pnewzv, &cached);
            }
            break;

        case IS_STRING:
            /* create a new, non-persistent string from the cached copy */
            ptmp_str = (zend_string *)ZVALUE(pcache->incopy, (size_t)Z_STR(cached));
            ZVAL_STR(pnewzv, zend_string_init(ZSTR_VAL(ptmp_str), ZSTR_LEN(ptmp_str), 0));
            break;

        case IS_ARRAY:
            /* Translation table for one top-level copy, on the C stack: a nested ucache call
             * from __wakeup()/__unserialize() or from an autoloader cannot clobber it */
            if(phtable == NULL)
            {
                phtable = &xlate;
                isfirst = 1;
                zend_hash_init(phtable, 8, NULL, NULL, 0);
            }

            table_tracker = (zvcache_hashtable_pool_tracker *)ZVALUE(pcache->incopy, (size_t)Z_PTR(cached));

            result = copyout_hashtable(pcache, pcopy, phtable, ppending, (HashTable *)ZVALUE(pcache->incopy, table_tracker->val), &phashtable);

            if(isfirst)
            {
                zend_hash_destroy(phtable);
                phtable = NULL;
            }

            /*
             * A failed copy leaves what it has built in place of the cached zval, NULL if it built
             * nothing: the elements of the table that were not converted are holes (see
             * copyout_hashtable), so it is safe to destroy, and the callers up to zvcache_get()
             * leave their own tables the same way.  Nothing is released here: releasing can start
             * a GC run, which runs destructors (user code) under the cache lock and walks the
             * tables that are still being built, whose unconverted elements hold cache offsets.
             * zvcache_get() releases the whole value once it has released the lock.
             */
            if(phashtable == NULL)
            {
                _ASSERT(FAILED(result));
                ZVAL_NULL(pnewzv);
                goto Finished;
            }

            ZVAL_ARR(pnewzv, phashtable);
            phashtable = NULL;

            if(FAILED(result))
            {
                goto Finished;
            }
            break;

        case IS_REFERENCE:
            result = copyout_reference(pcache, pcopy, phtable, ppending, (zend_reference *)ZVALUE(pcache->incopy, (size_t)Z_REF(cached)), &pref);

            /* After a failure, what was built is left in place as for a table: its value is safe to destroy */
            if(pref == NULL)
            {
                _ASSERT(FAILED(result));
                ZVAL_NULL(pnewzv);
                goto Finished;
            }

            ZVAL_REF(pnewzv, pref);

            if(FAILED(result))
            {
                goto Finished;
            }
            break;

        case IS_OBJECT:
            /* Never leave the cache offset in a zval that PHP may destroy */
            ZVAL_NULL(pnewzv);

            /* Copy the serialized object to request memory; it is unserialized once the
             * cache lock is released (see zvcache_pending_object) */
            ptmp_str = (zend_string *)ZVALUE(pcache->incopy, (size_t)Z_PTR(cached));
            pending.pzval = pnewzv;
            pending.pserialized = zend_string_init(ZSTR_VAL(ptmp_str), ZSTR_LEN(ptmp_str), 0);
            if(ppending != NULL)
            {
                zend_llist_add_element(ppending, &pending);
            }
            else
            {
                unserialize_object(pending.pzval, pending.pserialized);
                zend_string_release(pending.pserialized);
            }
            break;

        default:
            ZVAL_NULL(pnewzv);
            result = FATAL_ZVCACHE_INVALID_ZVAL;
            goto Finished;
    }

    *ppcopied = pnewzv;
    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in copyout_zval", result);
    }

    dprintverbose("end copyout_zval");

    return result;
}

static int copyin_hashtable(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, HashTable * poriginal, HashTable ** ppcopied)
{
    int             result     = NONFATAL;
    uint32_t        idx;
    uint32_t        nIndex;
    uint32_t        hash_size;
    Bucket *        p          = NULL;
    zend_string *   pzstr      = NULL;
    zend_uchar      allocated  = 0;
    HashTable *     pnew_table = NULL;
    char *          ptemp      = NULL;
    size_t          temp_size  = 0;
    zval *          pzval      = NULL;

    _ASSERT(ppcopied);
    _ASSERT(*ppcopied == NULL);

    dprintverbose("begin copyin_hashtable");

    /* Nested too deeply for the C stack left (see ucache_stack_exhausted) */
    if (ucache_stack_exhausted(pcopy->pstack))
    {
        result = FATAL_ZVCACHE_INVALID_ZVAL;
        goto Finished;
    }

    /* allocate a hashtable & copy over existing table stuff */
    result = copyin_memory(pcopy, phtable, poriginal, sizeof(zend_array), &pnew_table, &allocated);
    if (FAILED(result))
    {
        goto Finished;
    }

    *ppcopied = pnew_table;

    if (allocated == 0)
    {
        /* already copied (shared sub-array, or an array that contains itself) */
        goto Finished;
    }

    /*
     * The copy belongs to the cache.  GC_TYPE_INFO = GC_ARRAY drops everything the header
     * of the original may carry: the GC root buffer address and colour (GC_INFO), and the
     * GC_IMMUTABLE (literal arrays, zend_empty_array), GC_PERSISTENT and GC_PROTECTED
     * flags.  HT_FLAGS keeps the low byte only (drops nIteratorsCount, and nApplyCount and
     * HASH_FLAG_PERSISTENT on 7.2).  copyout_hashtable() sets up the header again.
     */
    pnew_table->pDestructor = NULL;
    GC_SET_REFCOUNT(pnew_table, 1);
    GC_TYPE_INFO(pnew_table) = GC_ARRAY;
    HT_FLAGS(pnew_table) &= UCACHE_HT_FLAGS_KEEP;

    /* Uninitialized (zend_empty_array, or an array that never got an element): no data */
    if (!HT_IS_INITIALIZED(pnew_table))
    {
        pnew_table->nTableMask = HT_MIN_MASK;
        pnew_table->arData = NULL;
        goto Finished;
    }

    if (HT_IS_PACKED(pnew_table))
    {
        /* Packed: hash part (2 slots) + nNumUsed elements (zvals since 8.2, Buckets before) */
        result = copyin_memory(pcopy, NULL, HT_GET_DATA_ADDR(poriginal), UCACHE_HT_PACKED_USED_SIZE(poriginal), &ptemp, &allocated);
        if (FAILED(result))
        {
            goto Finished;
        }
        HT_SET_DATA_ADDR(pnew_table, ptemp);
        ptemp = NULL;

        for (idx = 0; idx < pnew_table->nNumUsed; idx++) {
            pzval = UCACHE_HT_PACKED_ZVAL(pnew_table, idx);
            if (Z_TYPE_P(pzval) == IS_UNDEF) continue;

            result = copyin_zval(pcache, pcopy, phtable, pzval, &pzval);
            if (FAILED(result))
            {
                goto Finished;
            }
        }
    }
    else
    {
        if (UCACHE_HT_SHOULD_COMPACT(poriginal))
        {
            /* Sparsely populated: shrink the hash part while copying in (see ucache_compat.h) */
            hash_size = UCACHE_HT_HASH_SLOTS(poriginal);
            while ((hash_size >> 2) > poriginal->nNumUsed) {
                hash_size >>= 1;
            }
            pnew_table->nTableMask = (uint32_t)(-(int32_t)hash_size);
            temp_size = HT_HASH_SIZE(pnew_table->nTableMask) + ((size_t)poriginal->nNumUsed * sizeof(Bucket));
            ptemp = ZMALLOC(pcopy, temp_size);
            if (!ptemp)
            {
                result = pcopy->oomcode;
                goto Finished;
            }
            pcopy->allocsize += temp_size;

            HT_SET_DATA_ADDR(pnew_table, ptemp);
            ptemp = NULL;
            HT_HASH_RESET(pnew_table);
            memcpy(pnew_table->arData, poriginal->arData, (size_t)poriginal->nNumUsed * sizeof(Bucket));

            for (idx = 0; idx < poriginal->nNumUsed; idx++) {
                p = pnew_table->arData + idx;
                if (Z_TYPE(p->val) == IS_UNDEF) continue;

                /* fix up the index table */
                nIndex = ((uint32_t)p->h) | pnew_table->nTableMask;
                Z_NEXT(p->val) = HT_HASH(pnew_table, nIndex);
                HT_HASH(pnew_table, nIndex) = HT_IDX_TO_HASH(idx);
            }
        }
        else
        {
            /* Appropriately sized: copy the hash part and the used Buckets */
            result = copyin_memory(pcopy, NULL, HT_GET_DATA_ADDR(poriginal), HT_USED_SIZE(poriginal), &ptemp, &allocated);
            if (FAILED(result))
            {
                goto Finished;
            }
            HT_SET_DATA_ADDR(pnew_table, ptemp);
            ptemp = NULL;
        }

        /* Copy in Keys and Buckets */
        for (idx = 0; idx < pnew_table->nNumUsed; idx++) {
            p = pnew_table->arData + idx;
            if (Z_TYPE(p->val) == IS_UNDEF) continue;

            /* persist bucket and key */
            if (p->key) {
                /* If we have a string key, we're no longer using static keys, since
                 * we have to copy the string into shared memory, thus it won't be
                 * interned.
                 */
                HT_FLAGS(pnew_table) &= ~HASH_FLAG_STATIC_KEYS;
                result = copyin_string(pcopy, phtable, p->key, &pzstr);
                if (FAILED(result))
                {
                    goto Finished;
                }
                /* Copy over the key string's hash from the existing hash value in
                 * the bucket.
                 */
                ZSTR_H(pzstr) = p->h;
                /* Use Offset in copied-in zval */
                p->key = (zend_string *)ZOFFSET(pcopy, pzstr);
                pzstr = NULL;
            }

            /* persist the data itself */
            pzval = &p->val;
            result = copyin_zval(pcache, pcopy, phtable, pzval, &pzval);
            if (FAILED(result))
            {
                goto Finished;
            }
        }
    }

    /* ensure pointer is translated to cache memory offset */
    pnew_table->arData = (Bucket *)ZOFFSET(pcopy, pnew_table->arData);

    *ppcopied = pnew_table;

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in copyin_hashtable", result);
    }

    dprintverbose("end copyin_hashtable");

    return result;
}

static int copyout_hashtable(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zend_llist * ppending, HashTable * pcached, HashTable ** ppcopied)
{
    int             result     = NONFATAL;
    uint32_t        idx;
    uint32_t        jdx;
    uint32_t        nIndex;
    Bucket *        p          = NULL;
    HashTable *     pnew_table = NULL;
    char *          ptemp      = NULL;
    zval *          pnewzv     = NULL;
    size_t          used_size  = 0;
    size_t          alloc_size = 0;
    zend_string *   ptmp_str   = NULL;

    dprintverbose("start copyout_hashtable");

    _ASSERT(pcache    != NULL);
    _ASSERT(pcopy     != NULL);
    _ASSERT(phtable   != NULL);
    _ASSERT(pcached   != NULL);
    _ASSERT(ppcopied  != NULL);

    /* Nested too deeply for the C stack left (see ucache_stack_exhausted) */
    if(ucache_stack_exhausted(pcopy->pstack))
    {
        result = FATAL_ZVCACHE_INVALID_ZVAL;
        goto Finished;
    }

    /* A table that occurs more than once (shared sub-array, or a cycle through a
     * reference) is copied out once and then shared, as PHP itself does. */
    if((pnew_table = zend_hash_index_find_ptr(phtable, ucache_addr_key(pcached))) != NULL)
    {
        GC_ADDREF(pnew_table);
        *ppcopied = pnew_table;
        goto Finished;
    }

    pnew_table = (HashTable *)ZMALLOC(pcopy, sizeof(HashTable));
    if(pnew_table == NULL)
    {
        result = pcopy->oomcode;
        goto Finished;
    }

    /* initialize from cached entry: sizes, mask, flags, nNextFreeElement, nInternalPointer */
    *pnew_table = *pcached;

    /* A fresh request-local array: refcount 1, plain GC_ARRAY header (not in the GC root
     * buffer, not immutable/persistent/protected), no iterators attached, and on 7.2 the
     * recursion protection of any PHP array (see UCACHE_HT_FLAGS_ADD). */
    GC_SET_REFCOUNT(pnew_table, 1);
    GC_TYPE_INFO(pnew_table) = GC_ARRAY;
    HT_FLAGS(pnew_table) = (HT_FLAGS(pnew_table) & UCACHE_HT_FLAGS_KEEP) | UCACHE_HT_FLAGS_ADD;
    pnew_table->pDestructor = ZVAL_PTR_DTOR;

    /* Uninitalized: no data part */
    if (!HT_IS_INITIALIZED(pnew_table)) {
        pnew_table->nTableMask = HT_MIN_MASK;
        HT_SET_DATA_ADDR(pnew_table, &uninitialized_bucket);
        zend_hash_index_update_ptr(phtable, ucache_addr_key(pcached), (void *)pnew_table);
        *ppcopied = pnew_table;
        goto Finished;
    }

    /*
     * The cache holds the hash part and the nNumUsed used elements only.  Allocate the
     * full nTableSize capacity: the copy has refcount 1, so PHP adds elements in place.
     */
    if (HT_IS_PACKED(pcached)) {
        used_size  = UCACHE_HT_PACKED_USED_SIZE(pcached);
        alloc_size = UCACHE_HT_PACKED_SIZE(pcached);
    } else {
        used_size  = HT_USED_SIZE(pcached);
        alloc_size = HT_SIZE(pcached);
    }

    ptemp = ZMALLOC(pcopy, alloc_size);
    if (!ptemp)
    {
        /* Nothing refers to the header yet: free it, and the caller's slot becomes NULL */
        ZFREE(pcopy, pnew_table);
        pnew_table = NULL;
        result = pcopy->oomcode;
        goto Finished;
    }

    memcpy(ptemp, ZVALUE_HT_GET_DATA_ADDR(pcache->incopy, pcached), used_size);
    HT_SET_DATA_ADDR(pnew_table, ptemp);
    ptemp = NULL;

    /*
     * Register before copying the elements, so that cycles resolve to this table, but only now
     * that its data part is in request memory: once the table is registered (or returned), other
     * parts of the value can refer to it, so it must stay safe to destroy whatever fails, and
     * every later failure turns the elements not yet converted into holes (see Abandon).
     */
    zend_hash_index_update_ptr(phtable, ucache_addr_key(pcached), (void *)pnew_table);
    *ppcopied = pnew_table;

    /*
     * Each element is checked against memory_limit once it is copied (see
     * zvcache_over_memory_limit): a big value would otherwise run past memory_limit up to the
     * hard limit of the runtimes with delayed OOM (8.2.35, 8.3.36, 8.4.27, 8.5.12 and later),
     * where the process is killed instead of getting a fatal error.
     */
    if (HT_IS_PACKED(pnew_table)) {
        for (idx = 0; idx < pnew_table->nNumUsed; idx++) {
            pnewzv = UCACHE_HT_PACKED_ZVAL(pnew_table, idx);
            if (Z_TYPE_P(pnewzv) == IS_UNDEF) continue;

            result = copyout_zval(pcache, pcopy, phtable, ppending, pnewzv, &pnewzv);
            if (FAILED(result)) {
                goto Abandon;
            }

            if (zvcache_over_memory_limit()) {
                result = FATAL_OUT_OF_LMEMORY;
                goto Abandon;
            }
        }
    } else {
        for (idx = 0; idx < pnew_table->nNumUsed; idx++) {
            p = pnew_table->arData + idx;
            if (Z_TYPE(p->val) == IS_UNDEF) continue;

            /* copy out key */
            if (p->key) {
                ptmp_str = (zend_string *)ZVALUE(pcache->incopy, (size_t)p->key);
                p->key = zend_string_init(ZSTR_VAL(ptmp_str), ZSTR_LEN(ptmp_str), 0);
                ZSTR_H(p->key) = p->h;
            }

            /* copy out the data itself */
            pnewzv = &p->val;
            result = copyout_zval(pcache, pcopy, phtable, ppending, pnewzv, &pnewzv);
            if (FAILED(result)) {
                goto Abandon;
            }

            if (zvcache_over_memory_limit()) {
                result = FATAL_OUT_OF_LMEMORY;
                goto Abandon;
            }
        }
    }

    goto Finished;

Abandon:

    /*
     * Corrupted cache entry, memory_limit reached, or nested too deeply for the C stack left.
     * Element idx has been converted, or holds what copyout_zval built before it failed, or
     * NULL; the elements after it still hold cache offsets: turn them into holes so that the
     * table stays safe to destroy.  The table is not freed here: copyout_zval leaves it in
     * place of the cached zval, and zvcache_get() releases the whole value once it has released
     * the cache lock.
     */
    for (jdx = idx + 1; jdx < pnew_table->nNumUsed; jdx++) {
        pnewzv = HT_IS_PACKED(pnew_table) ? UCACHE_HT_PACKED_ZVAL(pnew_table, jdx) : &pnew_table->arData[jdx].val;
        if (Z_TYPE_P(pnewzv) == IS_UNDEF) continue;
        ZVAL_UNDEF(pnewzv);
        if (!HT_IS_PACKED(pnew_table)) {
            pnew_table->arData[jdx].key = NULL;
        }
        pnew_table->nNumOfElements--;
    }

    /*
     * The holes are still linked into the hash chains, where an integer key lookup would
     * find them. Rebuild the chains without the holes. Buckets must not move (no
     * zend_hash_rehash()): pending objects point at their zvals.
     */
    if (!HT_IS_PACKED(pnew_table)) {
        HT_HASH_RESET(pnew_table);
        for (jdx = 0; jdx < pnew_table->nNumUsed; jdx++) {
            p = pnew_table->arData + jdx;
            if (Z_TYPE(p->val) == IS_UNDEF) continue;

            nIndex = ((uint32_t)p->h) | pnew_table->nTableMask;
            Z_NEXT(p->val) = HT_HASH(pnew_table, nIndex);
            HT_HASH(pnew_table, nIndex) = HT_IDX_TO_HASH(jdx);
        }
    }

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in copyout_hashtable", result);
    }

    dprintverbose("end copyout_hashtable");

    return result;
}

static int copyin_string(zvcopy_context * pcopy, HashTable *phtable, zend_string * orig_string, zend_string ** new_string)
{
    int                  result     = NONFATAL;
    size_t               size       = 0;
    zend_string *        pzstr      = NULL;
    zend_uchar           allocated  = 0;

    _ASSERT(orig_string != NULL);
    _ASSERT(new_string  != NULL);
    _ASSERT(*new_string == NULL);

    /*
     * Only a string that can occur more than once in the value goes through the translation
     * table: an interned string, or one with more than one reference.  A string with refcount 1
     * has a single place in the value (the arrays and references that contain it are copied
     * once), and leaving it out keeps the table small.
     */
    if(!ZSTR_IS_INTERNED(orig_string) && GC_REFCOUNT(orig_string) == 1)
    {
        phtable = NULL;
    }

    size = _ZSTR_STRUCT_SIZE(ZSTR_LEN(orig_string));
    result = copyin_memory(pcopy, phtable, orig_string, size, &pzstr, &allocated);
    if (FAILED(result))
    {
        goto Finished;
    }

    if (allocated)
    {
        *(((char *)pzstr) + size - 1) = 0;
        zend_string_forget_hash_val(pzstr);
        GC_DEL_FLAGS(pzstr, IS_STR_INTERNED | IS_STR_PERMANENT | IS_STR_PERSISTENT);
    }

    *new_string = pzstr;

Finished:

    return result;

}

static int copyin_reference(zvcache_context * pcache, zvcopy_context * pcopy, HashTable *phtable, zend_reference * orig_ref, zend_reference ** new_ref)
{
    int                  result     = NONFATAL;
    size_t               size       = 0;
    zend_reference *     pzref      = NULL;
    zval *               pnew_zval  = NULL;
    zval *               pheld      = NULL;
    zend_uchar           allocated  = 0;

    _ASSERT(orig_ref != NULL);
    _ASSERT(new_ref  != NULL);
    _ASSERT(*new_ref == NULL);

    /* Nested too deeply for the C stack left (see ucache_stack_exhausted) */
    if (ucache_stack_exhausted(pcopy->pstack))
    {
        result = FATAL_ZVCACHE_INVALID_ZVAL;
        goto Finished;
    }

    size = sizeof(zend_reference);
    result = copyin_memory(pcopy, phtable, orig_ref, size, &pzref, &allocated);
    if (FAILED(result))
    {
        goto Finished;
    }

    if (allocated)
    {
        /*
         * Plain reference header.  Since 7.4 a reference also carries the typed properties
         * it belongs to ("sources"): those are pointers into the request that stored the
         * value, they must not end up in the cache.
         */
        GC_SET_REFCOUNT(pzref, 1);
        GC_TYPE_INFO(pzref) = GC_REFERENCE;
#if PHP_VERSION_ID >= 70400
        pzref->sources.ptr = NULL;
#endif

        /*
         * Copy the value the reference had when phase 1 reached it, not the one it has now:
         * the user code that phase 1 ran (serializing objects) can have assigned it another
         * value, which phase 1 neither held nor walked (see copyin_prepare).
         */
        pheld = zend_hash_index_find(&pcopy->pprepared->refvals, ucache_addr_key(orig_ref));
        if (pheld == NULL)
        {
            /* Cannot happen: every reference copied here is in something phase 1 held */
            _ASSERT(FALSE);
            ZVAL_NULL(&pzref->val);
            result = FATAL_ZVCACHE_INVALID_ZVAL;
            goto Finished;
        }

        ZVAL_COPY_VALUE(&pzref->val, pheld);
        pnew_zval = &pzref->val;
        result = copyin_zval(pcache, pcopy, phtable, pnew_zval, &pnew_zval);
        if (FAILED(result))
        {
            goto Finished;
        }
    }

    *new_ref = pzref;

Finished:

    return result;

}

static int copyout_reference(zvcache_context * pcache, zvcopy_context * pcopy, HashTable * phtable, zend_llist * ppending, zend_reference * pcached, zend_reference ** ppnew_ref)
{
    int                  result     = NONFATAL;
    zend_reference *     pzref      = NULL;
    zval *               pnew_zval  = NULL;

    _ASSERT(pcache    != NULL);
    _ASSERT(pcopy     != NULL);
    _ASSERT(pcached   != NULL);
    _ASSERT(ppnew_ref != NULL);

    /* Nested too deeply for the C stack left (see ucache_stack_exhausted) */
    if(ucache_stack_exhausted(pcopy->pstack))
    {
        result = FATAL_ZVCACHE_INVALID_ZVAL;
        goto Finished;
    }

    /* check table to see if we've already copied this item out*/
    if(phtable != NULL)
    {
        if((pzref = zend_hash_index_find_ptr(phtable, ucache_addr_key(pcached))) != NULL)
        {
            GC_ADDREF(pzref);
            *ppnew_ref = pzref;
            goto Finished;
        }
    }

    pzref = (zend_reference *)ZMALLOC(pcopy, sizeof(zend_reference));
    if(pzref == NULL)
    {
        result = pcopy->oomcode;
        goto Finished;
    }

    /* Same header as ZVAL_NEW_REF() creates */
    GC_SET_REFCOUNT(pzref, 1);
    GC_TYPE_INFO(pzref) = GC_REFERENCE;
#if PHP_VERSION_ID >= 70400
    pzref->sources.ptr = NULL;
#endif
    /* the value still holds a cache offset; copyout_zval() converts it in place */
    ZVAL_COPY_VALUE(&pzref->val, &pcached->val);

    /* Register before copying the value: the value may contain this same reference
     * again ($a['self'] = &$a), which must resolve to this zend_reference */
    if(phtable != NULL)
    {
        zend_hash_index_update_ptr(phtable, ucache_addr_key(pcached), (void *)pzref);
    }

    *ppnew_ref = pzref;

    /* copy out zval */
    pnew_zval = &pzref->val;
    result = copyout_zval(pcache, pcopy, phtable, ppending, pnew_zval, &pnew_zval);
    if (FAILED(result))
    {
        goto Finished;
    }

Finished:

    return result;

}

/*
 * Call this method under lock.  Returns the entry of key that has not expired, NULL if there is
 * none.  With prune, the expired entries it passes in the bucket, of any key, are removed on the
 * way (their memory is freed by free_pending_entries() once the lock is released); ucache_info()
 * looks up without it.
 */
static zvcache_value * find_zvcache_entry(zvcache_context * pcache, const char * key, unsigned int index, unsigned char prune)
{
    zvcache_header * header = NULL;
    zvcache_value *  pvalue = NULL;
    zvcache_value *  pnext  = NULL;
    unsigned int     ticks  = GetTickCount();

    dprintverbose("start find_zvcache_entry");

    _ASSERT(pcache  != NULL);
    _ASSERT(key     != NULL);

    header = pcache->zvheader;
    pvalue = ZVCACHE_VALUE(pcache->zvalloc, header->values[index]);

    while(pvalue != NULL)
    {
        pnext = ZVCACHE_VALUE(pcache->zvalloc, pvalue->next_value);

        /* ttl is counted from the time the entry was stored; ttlive 0 means it never expires */
        if(entry_expired(pvalue, ticks))
        {
            if(prune)
            {
                remove_zvcache_entry(pcache, index, pvalue);
            }
        }
        else if(strcmp(pcache->zvmemaddr + pvalue->keystr, key) == 0)
        {
            break;
        }

        pvalue = pnext;
    }

    dprintverbose("end find_zvcache_entry");

    return pvalue;
}

/*
 * Copy-in, phase 2: builds the new entry in shared memory from what copyin_prepare() collected.
 * Runs no user code and allocates no request memory (see copyin_prepare).  The entry is not
 * reachable by anyone before zvcache_set() adds it to the cache, so no lock is needed.
 */
static int create_zvcache_data(zvcache_context * pcache, const char * key, zvcopy_prepared * pprep, unsigned int ttl, zvcache_value ** ppvalue)
{
    int              result  = NONFATAL;
    zvcache_value *  pvalue  = NULL;
    size_t           keylen  = 0;
    zval *           pcopied = NULL;
    zvcopy_context * pcopy   = NULL;
    zvcopy_context   incopy;
    zvcopy_stack     stack;
    HashTable        xlate;
    unsigned int     cticks  = 0;
    volatile unsigned char bailout = 0;

    dprintverbose("start create_zvcache_data");

    _ASSERT(pcache   != NULL);
    _ASSERT(key      != NULL);
    _ASSERT(pprep    != NULL);
    _ASSERT(ppvalue  != NULL);
    _ASSERT(*ppvalue == NULL);

    *ppvalue = NULL;

    /* Private copy of the context: it records what this copy allocates (see copyin_rollback),
     * and a nested ucache_set() must not reset and mix up allocsize */
    incopy = *pcache->incopy;
    pcopy = &incopy;
    pcopy->pstack    = &stack;
    pcopy->pprepared = pprep;
    pcopy->pnewvalue = NULL;
    pcopy->pnewzval  = NULL;
    pcopy->ptracker  = NULL;
    pcopy->mpool     = 0;

    /*
     * One translation table per top-level copy, on the C stack.  Persistent memory: phase 2
     * must not allocate request memory, and a persistent table never bails out (it ends the
     * process when malloc fails).
     */
    zend_hash_init(&xlate, 8, NULL, NULL, 1);

    keylen = strlen(key) + 1;
    _ASSERT(keylen < 4098);  /* NOTE: No clue why 4098 */
    if (keylen > UCACHE_MAX_ZVKEY_LENGTH)
    {
        result = FATAL_ZVCACHE_INVALID_KEY_LENGTH;
        goto Finished;
    }

    pvalue = (zvcache_value *)ZMALLOC(pcopy, sizeof(zvcache_value) + keylen);
    if(pvalue == NULL)
    {
        result = pcopy->oomcode;
        goto Finished;
    }

    pcopy->pnewvalue = pvalue;

    /* Only set zvcache_value to 0. Set keyname after zvcache_value */
    memset(pvalue, 0, sizeof(zvcache_value));
    memcpy_s((char *)pvalue + sizeof(zvcache_value), keylen, key, keylen);

    /* Reset allocsize before calling copyin */
    pcopy->allocsize = 0;

    /* The top-level zval, allocated here so that copyin_rollback() can free it */
    pcopied = (zval *)ZMALLOC(pcopy, sizeof(zval));
    if(pcopied == NULL)
    {
        result = pcopy->oomcode;
        goto Finished;
    }

    pcopy->pnewzval = pcopied;
    pcopy->allocsize += sizeof(zval);

    ucache_stack_start(&stack);

    /*
     * Safety net (see copyin_prepare): if a fatal error interrupts the copy anyway, give back
     * what the entry took and the serializations before propagating the error.  No goto or
     * return may leave the zend_try block.
     */
    zend_try
    {
        result = copyin_zval(pcache, pcopy, &xlate, &pprep->value, &pcopied);
    }
    zend_catch
    {
        bailout = 1;
    }
    zend_end_try();

    if(bailout)
    {
        copyin_rollback(pcache, pcopy, &xlate);
        copyin_free_serialized(pprep);
        zend_bailout();
    }

    if(FAILED(result))
    {
        goto Finished;
    }

    pvalue->keystr     = ZOFFSET(pcopy, pvalue) + sizeof(zvcache_value);
    pvalue->keylen     = (unsigned short)(keylen - 1);
    pvalue->sizeb      = pcopy->allocsize;
    pvalue->zvalue     = ZOFFSET(pcopy, pcopied);

    cticks = GetTickCount();
    pvalue->add_ticks  = cticks;
    pvalue->use_ticks  = cticks;

    pvalue->ttlive     = ttl;
    pvalue->hitcount   = 0;
    pvalue->next_value = 0;
    pvalue->prev_value = 0;

    *ppvalue = pvalue;
    _ASSERT(SUCCEEDED(result));

Finished:

    zend_hash_destroy(&xlate);

    if(FAILED(result))
    {
        dprintimportant("failure %d in create_zvcache_data", result);

        /* copyin_zval() freed what it allocated for the value itself */
        if(pcopied != NULL)
        {
            ZFREE(pcopy, pcopied);
            pcopied = NULL;
        }

        if(pvalue != NULL)
        {
            ZFREE(pcopy, pvalue);
            pvalue = NULL;
        }
    }

    dprintverbose("end create_zvcache_data");

    return result;
}

/* Free what a top-level cached zval points to.  Everything was allocated with
 * pcache->incopy, except the contents of arrays, which live in one memory pool. */
static void destroy_cached_zval(zvcache_context * pcache, zval * pzval)
{
    zvcache_hashtable_pool_tracker * table_tracker = NULL;
    zend_reference *                 pzref         = NULL;

    switch(Z_TYPE_P(pzval))
    {
        case IS_UNDEF:
        case IS_NULL:
        case IS_TRUE:
        case IS_FALSE:
        case IS_LONG:
        case IS_DOUBLE:
            break;

        case IS_STRING:
            ZFREE(pcache->incopy, ZVALUE(pcache->incopy, (size_t)Z_STR_P(pzval)));
            Z_STR_P(pzval) = NULL;
            break;

        case IS_ARRAY:
            table_tracker = (zvcache_hashtable_pool_tracker *)ZVALUE(pcache->incopy, (size_t)Z_PTR_P(pzval));
            if(table_tracker->hoff != 0)
            {
                alloc_free_mpool(pcache->zvalloc, table_tracker->hoff);
            }
            table_tracker->hoff = 0;
            table_tracker->val = 0;
            ZFREE(pcache->incopy, table_tracker);
            Z_PTR_P(pzval) = NULL;
            table_tracker = NULL;
            break;

        case IS_REFERENCE:
            /* Top-level references are dereferenced by zvcache_set(); free the referenced
             * value too, in case an entry holds one anyway. */
            pzref = (zend_reference *)ZVALUE(pcache->incopy, (size_t)Z_REF_P(pzval));
            destroy_cached_zval(pcache, &pzref->val);
            ZFREE(pcache->incopy, pzref);
            Z_REF_P(pzval) = NULL;
            break;

        case IS_OBJECT:
            ZFREE(pcache->incopy, ZVALUE(pcache->incopy, (size_t)Z_PTR_P(pzval)));
            Z_PTR_P(pzval) = NULL;
            break;

        default:
            dprintimportant("destroy_cached_zval: Can't free unsupported type %d", Z_TYPE_P(pzval));
    }
}

static void destroy_zvcache_data(zvcache_context * pcache, zvcache_value * pvalue)
{
    zval * pzval;

    dprintverbose("start destroy_zvcache_data");

    if(pvalue != NULL)
    {
        if(pvalue->zvalue != 0)
        {
            pzval = (zval *)ZVALUE(pcache->incopy, pvalue->zvalue);

            destroy_cached_zval(pcache, pzval);

            ZFREE(pcache->incopy, pzval);
            pvalue->zvalue = 0;
            pzval = NULL;
        }

        /* No need to call free for keystr as value and keystr are allocated together */
        ZFREE(pcache->incopy, pvalue);
        pvalue = NULL;
    }

    dprintverbose("end destroy_zvcache_data");

    return;
}

static void add_zvcache_entry(zvcache_context * pcache, unsigned int index, zvcache_value * pvalue)
{
    zvcache_header * header  = NULL;
    zvcache_value *  pcheck  = NULL;

    dprintverbose("start add_zvcache_entry");

    _ASSERT(pcache         != NULL);
    _ASSERT(pvalue         != NULL);
    _ASSERT(pvalue->keystr != 0);

    header = pcache->zvheader;
    pcheck = ZVCACHE_VALUE(pcache->zvalloc, header->values[index]);

    while(pcheck != NULL)
    {
        if(pcheck->next_value == 0)
        {
            break;
        }

        pcheck = ZVCACHE_VALUE(pcache->zvalloc, pcheck->next_value);
    }

    if(pcheck != NULL)
    {
        pcheck->next_value = alloc_get_valueoffset(pcache->zvalloc, pvalue);
        pvalue->next_value = 0;
        pvalue->prev_value = alloc_get_valueoffset(pcache->zvalloc, pcheck);
    }
    else
    {
        header->values[index] = alloc_get_valueoffset(pcache->zvalloc, pvalue);
        pvalue->next_value = 0;
        pvalue->prev_value = 0;
    }

    header->itemcount++;

    dprintverbose("end add_zvcache_entry");

    return;
}

/*
 * Call this method under write lock.  It unlinks the entry, which no other process can reach from
 * then on, and queues it in the process's free list: free_pending_entries() frees it once the lock
 * is released, so that freeing a big entry does not hold up the other processes.  A process killed
 * in between (an IIS FastCGI timeout) leaves the memory of the queued entries lost until every
 * process of the pool has exited, so the list holds at most FREELIST_MAXIMUM entries: beyond that
 * (ucache_clear(), a step that removes many entries), the entry is freed at once, under the lock,
 * where a kill loses at most the entry being freed.
 */
static void remove_zvcache_entry(zvcache_context * pcache, unsigned int index, zvcache_value * pvalue)
{
    alloc_context *   apalloc = NULL;
    zvcache_header *  header  = NULL;
    zvcache_value *   ptemp   = NULL;

    dprintverbose("start remove_zvcache_entry");

    _ASSERT(pcache         != NULL);
    _ASSERT(pvalue         != NULL);
    _ASSERT(pvalue->keystr != 0);

    apalloc = pcache->zvalloc;
    header  = pcache->zvheader;

    /* Decrement itemcount and remove entry from hashtable */
    header->itemcount--;

    if(pvalue->prev_value == 0)
    {
        header->values[index] = pvalue->next_value;
        if(pvalue->next_value != 0)
        {
            ptemp = ZVCACHE_VALUE(apalloc, pvalue->next_value);
            ptemp->prev_value = 0;
        }
    }
    else
    {
        ptemp = ZVCACHE_VALUE(apalloc, pvalue->prev_value);
        ptemp->next_value = pvalue->next_value;

        if(pvalue->next_value != 0)
        {
            ptemp = ZVCACHE_VALUE(apalloc, pvalue->next_value);
            ptemp->prev_value = pvalue->prev_value;
        }
    }

    if(pcache->freecount < FREELIST_MAXIMUM)
    {
        /* Freed by free_pending_entries(), chained through next_value */
        pvalue->prev_value = 0;
        pvalue->next_value = pcache->freelist;
        pcache->freelist   = alloc_get_valueoffset(apalloc, pvalue);
        pcache->freecount++;
    }
    else
    {
        destroy_zvcache_data(pcache, pvalue);
    }
    pvalue = NULL;

    dprintverbose("end remove_zvcache_entry");
    return;
}

/*
 * Frees the entries that remove_zvcache_entry() queued.  Call it right after releasing the cache
 * lock, before any user code can run: the frees take only the allocator's lock, and allocate no
 * request memory, so nothing in here can bail out.
 */
static void free_pending_entries(zvcache_context * pcache)
{
    zvcache_value * pvalue = NULL;

    while(pcache->freelist != 0)
    {
        pvalue = ZVCACHE_VALUE(pcache->zvalloc, pcache->freelist);
        pcache->freelist = pvalue->next_value;

        destroy_zvcache_data(pcache, pvalue);
    }

    pcache->freecount = 0;
}

/* Whether an entry has expired, by the rule of the reads: more than ttl whole seconds since it was stored */
static zend_bool entry_expired(zvcache_value * pvalue, unsigned int ticks)
{
    return (pvalue->ttlive > 0 && utils_ticksdiff(ticks, pvalue->add_ticks) / 1000 > pvalue->ttlive) ? 1 : 0;
}

/* Public functions */

/*
 * Since PHP 8.3.36, 8.4.27 and 8.5.12 (security fix GHSA-w8hw-w9fx-j79w, also in 8.2.35),
 * reaching memory_limit inside internal code no longer raises the fatal error at once ("delayed
 * OOM"): the allocations go on up to a hard limit (twice memory_limit), where the process ends
 * with _exit(137), and the VM raises the pending error when the internal function returns.  The
 * long loops of ucache (phase 1 of the copy-in, the copy-out, the key listing, the arrays
 * ucache_get()/ucache_info() build) check the usage themselves and stop with a failure, so that
 * the function returns and the VM gets that chance.  Runtimes without delayed OOM bail out on the
 * allocation that would pass the limit (which is never below one chunk), so this never fires
 * there.  ZEND_HAS_OOM() is no alternative: it reads EG(delayed_oom), which the runtimes older
 * than these headers, that this DLL also loads into, do not have.  See also
 * UCACHE_MEMORY_LIMIT_CHECK in ucache_compat.h.
 */
zend_bool zvcache_over_memory_limit(void)
{
#if UCACHE_MEMORY_LIMIT_CHECK
    zend_long limit = PG(memory_limit);

    if(limit <= 0)
    {
        return 0;
    }

    return (zend_memory_usage(1) > MAX((size_t)limit, (size_t)ZEND_MM_CHUNK_SIZE)) ? 1 : 0;
#else
    return 0;
#endif
}

/*
 * With delayed OOM (see zvcache_over_memory_limit), memory_limit can be passed for a moment only:
 * a table that grows allocates its new buffer while the old one is still in use, which leaves the
 * fatal error pending, and the usage falls back below the limit once the old buffer is freed, so
 * the checks that look at the current usage never see it.  A ucache_set() that went past the limit
 * must fail all the same, like one that those checks stop: PHP raises the fatal error as soon as
 * it returns, so the script never learns whether the value was stored.  So zvcache_set() also
 * compares the peak with memory_limit.  Only a rise during the call counts: peak0 is the peak when
 * the call started, which can be above memory_limit if the script lowered it.  Without delayed
 * OOM, the allocation that would pass the limit bails out, so the peak never passes it.  The
 * array forms of ucache_set()/ucache_add() also use it, with the peak when the whole call started,
 * to store nothing more once one value has passed the limit.
 */
zend_bool zvcache_peak_over_memory_limit(size_t peak0)
{
#if UCACHE_MEMORY_LIMIT_CHECK
    zend_long limit = PG(memory_limit);

    if(limit <= 0)
    {
        return 0;
    }

    return (zend_memory_peak_usage(1) > MAX((size_t)limit, peak0)) ? 1 : 0;
#else
    return 0;
#endif
}

/*
 * ucache.gc.interval in milliseconds, for an interval > 0.  A difference of GetTickCount() values
 * stays below 2^32 ms (49.7 days), so a longer interval never passes.
 */
static unsigned int scavenger_interval_ms(void)
{
    zend_long interval = UCG(ucgc_interval);

    return (interval >= (zend_long)(UINT_MAX / 1000)) ? UINT_MAX : (unsigned int)interval * 1000;
}

/* ucache.gc.step 0: a whole sweep of the table in about this many seconds of steps at the end of */
/* requests, and never fewer buckets than WinCache's steps */
#define SCAVENGER_SWEEP_SECONDS     300
#define SCAVENGER_STEP_MINIMUM      16

/* A step stops after about this much work under the cache lock, and the next one goes on from */
/* there; the clock is read every SCAVENGER_CLOCK_EVERY buckets, which hold a few hundred entries */
/* on average in a full cache (see ZVCACHE_BUCKET_BYTES). Most buckets are empty otherwise: read */
/* every 16 buckets, the clock took about half the time of a step */
#define SCAVENGER_STEP_BUDGET_US    1000
#define SCAVENGER_CLOCK_EVERY       256

/* Eviction (ucache.gc.evict) by groups of idle time: group g holds the entries unused for */
/* [2^(g-1), 2^g) seconds, group 0 those used in the last second */
#define EVICTION_GROUPS             33

/* After a full pass for a full cache that removed nothing, the processes skip the pass for at least */
/* this long (make_room); and a write runs at most this many rounds of passes */
#define RECLAIM_FUTILE_MS           1000
#define RECLAIM_TRIES_MAXIMUM       16

/* Buckets a step examines: ucache.gc.step, or with 0 or less the automatic size */
static unsigned int scavenger_step(zvcache_header * pheader)
{
    zend_long step     = UCG(ucgc_step);
    zend_long interval = UCG(ucgc_interval);

    if(step <= 0)
    {
        interval = (interval < 0) ? 0 : MIN(interval, SCAVENGER_SWEEP_SECONDS);
        step = ((zend_long)pheader->valuecount * interval + SCAVENGER_SWEEP_SECONDS - 1) / SCAVENGER_SWEEP_SECONDS;
        step = MAX(step, SCAVENGER_STEP_MINIMUM);
    }

    return (unsigned int)MIN(step, (zend_long)pheader->valuecount);
}

/* Microseconds since start, on the performance counter */
static LONGLONG scavenger_elapsed_us(const LARGE_INTEGER * pstart)
{
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER        now;

    if(frequency.QuadPart == 0)
    {
        QueryPerformanceFrequency(&frequency);
    }

    QueryPerformanceCounter(&now);

    return (now.QuadPart - pstart->QuadPart) * 1000000 / frequency.QuadPart;
}

/* Seconds since an entry was last used: read with ucache_get(), stored, or changed or compared by inc/dec/cas */
static unsigned int entry_idle(zvcache_value * pvalue, unsigned int ticks)
{
    return utils_ticksdiff(ticks, pvalue->use_ticks) / 1000;
}

/* Whether the scavenger removes an entry: expired (by the rule of the reads), or, with */
/* ucache.gc.ttl > 0, not used for that many seconds */
static zend_bool scavenger_removes(zvcache_value * pvalue, unsigned int ticks, zend_long idlettl)
{
    if(entry_expired(pvalue, ticks))
    {
        return 1;
    }

    return (idlettl > 0 && (zend_long)entry_idle(pvalue, ticks) >= idlettl) ? 1 : 0;
}

/* The idle group of an entry for eviction (see EVICTION_GROUPS) */
static unsigned int eviction_group(unsigned int idle)
{
    unsigned int group = 0;

    while(idle != 0)
    {
        group++;
        idle >>= 1;
    }

    return group;
}

/*
 * Call this method under lock.  A write ran out of shared memory: remove what a scavenger step
 * would remove, over the whole table and without its time limit, then, with percent > 0, evict the
 * entries unused for the longest time, one by one, until percent percent of the cache is free
 * (zvcache_set() raises it while the value still does not fit: free memory in many small pieces may
 * not hold it).  Unlike everywhere else, the memory is freed here, under the lock: the write that waits
 * for it, and the next process that runs out, must see what is really free (an estimate of what
 * entries take, or frees still pending in another process, would make processes evict again and
 * again).  A full cache is rare, so the time under the lock does not matter there.  Counts as a
 * scavenger step in ucache_info().  Returns the number of entries removed.
 */
static unsigned int reclaim_entries(zvcache_context * pcache, zend_long percent)
{
    zvcache_header * pheader = pcache->zvheader;
    alloc_context *  palloc  = pcache->zvalloc;
    zvcache_value *  pvalue  = NULL;
    zvcache_value *  ptemp   = NULL;
    unsigned int     ticks   = GetTickCount();
    zend_long        idlettl = UCG(ucgc_ttl);
    unsigned int     index   = 0;
    unsigned int     count   = 0;
    unsigned int     evicted = 0;
    unsigned int     group   = 0;
    unsigned char    enough  = 0;
    size_t           target  = 0;
    unsigned int     counts[EVICTION_GROUPS];
    alloc_info       info;

    dprintverbose("start reclaim_entries");

    for(index = 0; index < pheader->valuecount; index++)
    {
        pvalue = ZVCACHE_VALUE(palloc, pheader->values[index]);
        while(pvalue != NULL)
        {
            ptemp  = pvalue;
            pvalue = ZVCACHE_VALUE(palloc, pvalue->next_value);

            if(scavenger_removes(ptemp, ticks, idlettl))
            {
                count++;
                remove_zvcache_entry(pcache, index, ptemp);
            }
        }
    }

    free_pending_entries(pcache);

    if(percent > 0)
    {
        /* On the stack: see zvcache_run_scavenger() */
        alloc_getinfo(palloc, &info);
        target = info.total_size / 100 * (size_t)MIN(percent, 100);

        if(info.free_size < target)
        {
            /* How many entries each idle group holds, to go over the table once per group that has some */
            memset(counts, 0, sizeof(counts));
            for(index = 0; index < pheader->valuecount; index++)
            {
                for(pvalue = ZVCACHE_VALUE(palloc, pheader->values[index]); pvalue != NULL;
                    pvalue = ZVCACHE_VALUE(palloc, pvalue->next_value))
                {
                    counts[eviction_group(entry_idle(pvalue, ticks))]++;
                }
            }

            /* The groups unused the longest first, each entry freed at once, until enough is free */
            for(group = EVICTION_GROUPS; group > 0 && !enough; )
            {
                group--;
                if(counts[group] == 0)
                {
                    continue;
                }

                for(index = 0; index < pheader->valuecount && !enough; index++)
                {
                    pvalue = ZVCACHE_VALUE(palloc, pheader->values[index]);
                    while(pvalue != NULL)
                    {
                        ptemp  = pvalue;
                        pvalue = ZVCACHE_VALUE(palloc, pvalue->next_value);

                        if(eviction_group(entry_idle(ptemp, ticks)) != group)
                        {
                            continue;
                        }

                        evicted++;
                        count++;
                        remove_zvcache_entry(pcache, index, ptemp);
                        free_pending_entries(pcache);

                        alloc_getinfo(palloc, &info);
                        if(info.free_size >= target)
                        {
                            enough = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    pheader->lscavenge  = ticks;
    pheader->scticks    = ticks;
    pheader->sccount    = count;
    pheader->scruns    += 1;
    pheader->scremoved += count;
    pheader->scevicted += evicted;

    dprintimportant("reclaim_entries: removed %u entries, %u of them evicted", count, evicted);
    dprintverbose("end reclaim_entries");

    return count;
}

/*
 * Call this method under lock.  Like everything done under the cache lock outside the zend_try
 * blocks of zvcache_get()/zvcache_list(), it must not allocate request memory or run user code:
 * a fatal error here would leave the cross-process lock owned by a FastCGI process that survives
 * the error, and every other process of the pool would block on it.
 *
 * force 0: a write, which runs a step while ucache.gc.threshold is set and free memory is at or
 * below it.  force 1: the end of a request (zvcache_scavenge_periodic), which runs a step once
 * ucache.gc.interval seconds have passed since the last one, while ucache.gc.threshold allows it.
 * A step examines scavenger_step() buckets, or fewer when it reaches SCAVENGER_STEP_BUDGET_US,
 * and removes what scavenger_removes() says; the memory comes back in free_pending_entries().
 */
int zvcache_run_scavenger(zvcache_context * pcache, unsigned int force)
{
    int count = 0;

    alloc_info   info;

    unsigned int threshold = 0;
    unsigned int sindex    = 0;
    unsigned int eindex    = 0;
    unsigned int scanned   = 0;
    unsigned int ticks     = GetTickCount();
    unsigned int step      = 0;
    zend_long    ttl       = UCG(ucgc_ttl);

    zvcache_header * pheader = NULL;
    alloc_context *  palloc  = NULL;
    zvcache_value *  pvalue  = NULL;
    zvcache_value *  ptemp   = NULL;
    LARGE_INTEGER    started;

    _ASSERT(pcache != NULL);

    dprintverbose("start zvcache_run_scavenger");

    pheader = pcache->zvheader;
    palloc  = pcache->zvalloc;

    if ((!force && (UCG(ucgc_threshold) == 0)) || (force && utils_ticksdiff(ticks, pheader->lscavenge) < scavenger_interval_ms()))
    {
	goto Finished;
    }

    if (UCG(ucgc_threshold) > 0) {
        /* On the stack: see above */
        alloc_getinfo(palloc, &info);

        threshold = (unsigned int) ((100*info.free_size)/info.total_size);

        if (threshold > UCG(ucgc_threshold))
        {
            /* A run at the end of a request counts as done all the same: otherwise every request */
            /* that ends would take the cache lock to look at the free memory, until the next run */
            if (force)
            {
                pheader->lscavenge = ticks;
            }
            goto Finished;
        }
    }
    
    pheader->lscavenge = ticks;

    /* Run scavenger starting from where the last step stopped */

    step   = scavenger_step(pheader);
    sindex = (pheader->scstart < pheader->valuecount) ? pheader->scstart : 0;
    eindex = MIN(sindex + step, pheader->valuecount);

    dprintimportant("scavenger: sindex = %u, eindex = %u, count = %u, step = %u, threshold = %u", sindex, eindex, pheader->valuecount, step, threshold);

    QueryPerformanceCounter(&started);

    for( ;sindex < eindex; sindex++)
    {
        /* About SCAVENGER_STEP_BUDGET_US of work under the lock: the next step goes on from here */
        if(scanned != 0 && (scanned % SCAVENGER_CLOCK_EVERY) == 0 && scavenger_elapsed_us(&started) > SCAVENGER_STEP_BUDGET_US)
        {
            break;
        }

        scanned++;

        if(pheader->values[sindex] == 0)
        {
            continue;
        }

        pvalue = ZVCACHE_VALUE(palloc, pheader->values[sindex]);
        while(pvalue != NULL)
        {
            ptemp = pvalue;
            pvalue = ZVCACHE_VALUE(palloc, pvalue->next_value);

            if(scavenger_removes(ptemp, ticks, ttl))
            {
                count++;
                remove_zvcache_entry(pcache, sindex, ptemp);
                ptemp = NULL;
            }
        }
    }

    pheader->scstart = (sindex < pheader->valuecount) ? sindex : 0;

    /* For ucache_info(): the steps that ran, not the ones skipped above */
    pheader->scticks    = ticks;
    pheader->sccount    = (unsigned int)count;
    pheader->scruns    += 1;
    pheader->scremoved += (size_t)count;

Finished:

    dprintverbose("end zvcache_run_scavenger");

    return count;
}

/*
 * Called at the end of every request while ucache.gc.interval > 0: the first request to end once
 * that many seconds have passed since the last scavenger run, in any of the processes that share
 * the cache, runs one step.  The time of the last run is read without the lock first, so that the
 * requests that end in between do not take it; zvcache_run_scavenger() checks it again under the
 * lock, since another process may have run the scavenger meanwhile.
 */
void zvcache_scavenge_periodic(zvcache_context * pcache)
{
    _ASSERT(pcache != NULL);

    if(utils_ticksdiff(GetTickCount(), pcache->zvheader->lscavenge) < scavenger_interval_ms())
    {
        return;
    }

    lock_lock(pcache->zvlock);
    zvcache_run_scavenger(pcache, 1);
    lock_unlock(pcache->zvlock);

    free_pending_entries(pcache);
}

int zvcache_create(zvcache_context ** ppcache)
{
    int               result = NONFATAL;
    zvcache_context * pcache = NULL;

    dprintverbose("start zvcache_create");

    _ASSERT(ppcache != NULL);
    *ppcache = NULL;

    pcache = (zvcache_context *)alloc_pemalloc(sizeof(zvcache_context));
    if(pcache == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    pcache->id          = gzvcacheid++;
    pcache->cachekey    = 0;
    pcache->hinitdone   = NULL;

    pcache->incopy      = NULL;
    pcache->outcopy     = NULL;

    pcache->zvmemaddr   = NULL;
    pcache->zvheader    = NULL;
    pcache->zvfilemap   = NULL;
    pcache->zvlock      = NULL;
    pcache->zvalloc     = NULL;
    pcache->freelist    = 0;
    pcache->freecount   = 0;

    *ppcache = pcache;

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_create", result);
    }

    dprintverbose("end zvcache_create %p", pcache);

    return result;
}

void zvcache_destroy(zvcache_context * pcache)
{
    dprintverbose("start zvcache_destroy %p", pcache);

    if(pcache != NULL)
    {
        alloc_pefree(pcache);
        pcache = NULL;
    }

    dprintverbose("end zvcache_destroy");

    return;
}

int zvcache_initialize(zvcache_context * pcache, unsigned short cachekey, unsigned int cachesize)
{
    int              result    = NONFATAL;
    size_t           segsize   = 0;
    zvcache_header * header    = NULL;
    unsigned int     msize     = 0;

    unsigned int    zvcount    = 256;
    unsigned int    cticks     = 0;
    unsigned char   isfirst    = 0;
    unsigned char   islocked   = 0;
    unsigned char   initmemory = 0;
    char *          lockname   = "USERZVALS_CACHE";
    char *          prefix     = NULL;
    size_t          cchprefix  = 0;

    dprintverbose("start zvcache_initialize %p", pcache);

    _ASSERT(pcache    != NULL);
    _ASSERT(cachesize >= 2   && cachesize <= 1024);

    /* Initialize memory map to store the user cache */
    result = filemap_create(&pcache->zvfilemap);
    if(FAILED(result))
    {
        goto Finished;
    }

    pcache->cachekey = cachekey;

    /* get object name prefix */
    result = lock_get_nameprefix(lockname, cachekey, LOCK_TYPE_SHARED, &prefix, &cchprefix);
    if (FAILED(result))
    {
        goto Finished;
    }

    result = utils_create_init_event(prefix, "ZVCACHE_INIT", &pcache->hinitdone, &isfirst);
    if (FAILED(result))
    {
        result = FATAL_ZVCACHE_INIT_EVENT;
        goto Finished;
    }

    /* Owned until Finished: whether the cache is initialized or opened is decided once, under this mutex */
    islocked = 1;

    /* isfirst (no other process had the init mutex) only lets a file-backed filemap delete the file of */
    /* an earlier cache: it does not tell whether the segment is initialized. A process that starts while */
    /* the last one of the pool closes the cache (zvcache_terminate closes the mutex last), or after a */
    /* process died owning the mutex, finds the mutex with a new segment, or with the segment of the */
    /* process that is closing it, or of the one that died */
    result = filemap_initialize(pcache->zvfilemap, FILEMAP_TYPE_USERZVALS, cachekey, cachesize, isfirst);
    if(FAILED(result))
    {
        goto Finished;
    }

    pcache->zvmemaddr = (char *)pcache->zvfilemap->mapaddr;
    segsize = filemap_getsize(pcache->zvfilemap);
    _ASSERT(segsize != 0);

    /* A new segment is initialized; one that existed is too, unless a process completed its */
    /* initialization (alloc_initialize decides, and sets initmemory then) */
    initmemory = (pcache->zvfilemap->existing == 0);

    /* Create allocator for zvcache segment */
    result = alloc_create(&pcache->zvalloc);
    if(FAILED(result))
    {
        goto Finished;
    }

    result = alloc_initialize(pcache->zvalloc, "USERZVALS_SEGMENT", cachekey, pcache->zvfilemap->mapaddr, segsize, &initmemory);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Get memory for cache header */
    zvcount = utils_get_prime_less_than(segsize / ZVCACHE_BUCKET_BYTES);
    msize = sizeof(zvcache_header) + ((zvcount - 1) * sizeof(size_t));
    pcache->zvheader = (zvcache_header *)alloc_get_cacheheader(pcache->zvalloc, msize, CACHE_TYPE_USERZVALS);
    if(pcache->zvheader == NULL)
    {
        result = FATAL_ZVCACHE_INITIALIZE;
        goto Finished;
    }

    header = pcache->zvheader;

    /* Create reader writer locks for the zvcache */
    result = lock_create(&pcache->zvlock);
    if(FAILED(result))
    {
        goto Finished;
    }

    result = lock_initialize(pcache->zvlock, lockname, cachekey, LOCK_TYPE_SHARED, &header->last_owner);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Initialize the zvcache_header if alloc_initialize initialized the segment */
    if(initmemory)
    {
        cticks = GetTickCount();

        /* No need to get a write lock: the other processes wait for */
        /* the init mutex, and none uses a segment that is not marked */
        header->hitcount     = 0;
        header->misscount    = 0;
        header->last_owner   = 0;

        header->lscavenge    = cticks;
        header->scstart      = 0;
        header->itemcount    = 0;
        header->valuecount   = zvcount;
        header->scticks      = 0;
        header->sccount      = 0;
        header->scruns       = 0;
        header->scremoved    = 0;
        header->scevicted    = 0;
        header->rcticks      = 0;
        header->rcfutile     = 0;
        header->rcwait       = 0;
        header->rcgen        = 0;

        memset((void *)header->values, 0, sizeof(size_t) * zvcount);

        header->init_ticks = cticks;
        header->mapcount   = 1;

        /* Every field of zvcache_header, fields added later included, must be set above: the mark */
        /* comes last, and the processes that open the segment from then on use it as it is */
        alloc_initdone(pcache->zvalloc);
    }
    else
    {
        /* Increment the mapcount */
        InterlockedIncrement(&header->mapcount);
    }

    /* Create incopy and outcopy zvcopy contexts */
    pcache->incopy = (zvcopy_context *)alloc_pemalloc(sizeof(zvcopy_context));
    if(pcache->incopy == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    /* The copy-in fields (pprepared, ...) are only set in create_zvcache_data's private copy */
    memset(pcache->incopy, 0, sizeof(zvcopy_context));
    pcache->incopy->oomcode   = FATAL_OUT_OF_SMEMORY;
    pcache->incopy->palloc    = pcache->zvalloc;
    pcache->incopy->pbaseadr  = pcache->zvmemaddr;
    pcache->incopy->hoffset   = 0;
    pcache->incopy->allocsize = 0;
    pcache->incopy->fnmalloc  = alloc_osmalloc;
    pcache->incopy->fnfree    = alloc_osfree;

    pcache->outcopy = (zvcopy_context *)alloc_pemalloc(sizeof(zvcopy_context));
    if(pcache->outcopy == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    memset(pcache->outcopy, 0, sizeof(zvcopy_context));
    pcache->outcopy->oomcode   = FATAL_OUT_OF_LMEMORY;
    pcache->outcopy->palloc    = NULL;
    pcache->outcopy->pbaseadr  = pcache->zvmemaddr;
    pcache->outcopy->hoffset   = 0;
    pcache->outcopy->allocsize = 0;
    pcache->outcopy->fnmalloc  = alloc_oemalloc;
    pcache->outcopy->fnfree    = alloc_oefree;

Finished:

    if (islocked)
    {
        ReleaseMutex(pcache->hinitdone);
        islocked = 0;
    }

    if (prefix != NULL)
    {
        alloc_pefree(prefix);
        prefix = NULL;
    }

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_initialize", result);

        if(pcache->zvalloc != NULL)
        {
            alloc_terminate(pcache->zvalloc);
            alloc_destroy(pcache->zvalloc);

            pcache->zvalloc = NULL;
        }

        if(pcache->zvfilemap != NULL)
        {
            filemap_terminate(pcache->zvfilemap);
            filemap_destroy(pcache->zvfilemap);

            pcache->zvfilemap = NULL;
        }

        if(pcache->zvlock != NULL)
        {
            lock_terminate(pcache->zvlock);
            lock_destroy(pcache->zvlock);

            pcache->zvlock = NULL;
        }

        if(pcache->hinitdone != NULL)
        {
            CloseHandle(pcache->hinitdone);
            pcache->hinitdone = NULL;
        }

        pcache->zvheader = NULL;
    }

    dprintverbose("end zvcache_initialize");

    return result;
}

void zvcache_terminate(zvcache_context * pcache)
{
    dprintverbose("start zvcache_terminate %p", pcache);

    if(pcache != NULL)
    {
        /* Every call frees what it removed before it returns, so this finds nothing to free */
        _ASSERT(pcache->freelist == 0);
        if(pcache->zvalloc != NULL && pcache->incopy != NULL)
        {
            free_pending_entries(pcache);
        }

        /* After free_pending_entries(), which frees through incopy */
        if(pcache->incopy != NULL)
        {
            alloc_pefree(pcache->incopy);
            pcache->incopy = NULL;
        }

        if(pcache->outcopy != NULL)
        {
            alloc_pefree(pcache->outcopy);
            pcache->outcopy = NULL;
        }

        if(pcache->zvheader != NULL)
        {
            InterlockedDecrement(&pcache->zvheader->mapcount);
            pcache->zvheader = NULL;
        }

        if(pcache->zvalloc != NULL)
        {
            alloc_terminate(pcache->zvalloc);
            alloc_destroy(pcache->zvalloc);

            pcache->zvalloc = NULL;
        }

        if(pcache->zvfilemap != NULL)
        {
            filemap_terminate(pcache->zvfilemap);
            filemap_destroy(pcache->zvfilemap);

            pcache->zvfilemap = NULL;
        }

        if(pcache->zvlock != NULL)
        {
            lock_terminate(pcache->zvlock);
            lock_destroy(pcache->zvlock);

            pcache->zvlock = NULL;
        }

        if(pcache->hinitdone != NULL)
        {
            CloseHandle(pcache->hinitdone);
            pcache->hinitdone = NULL;
        }
    }

    dprintverbose("end zvcache_terminate");

    return;
}

int zvcache_get(zvcache_context * pcache, const char * key, zval ** pvalue)
{
    int              result  = NONFATAL;
    unsigned int     index   = 0;
    unsigned char    flock   = 0;
    zvcache_value *  pentry  = NULL;
    zvcache_header * header  = NULL;
    zval *           pcached = NULL;
    zend_llist       pending;
    zvcopy_context   outcopy;
    zvcopy_stack     stack;
    zval             released;
    volatile unsigned char bailout = 0;

    dprintverbose("start zvcache_get");

    _ASSERT(pcache != NULL);
    _ASSERT(key    != NULL);
    _ASSERT(pvalue != NULL);

    /* NULL until the copy fills it: whatever happens, *pvalue can be released below */
    ZVAL_NULL(*pvalue);

    /* objects found by the copyout, unserialized after the lock is released */
    zend_llist_init(&pending, sizeof(zvcache_pending_object), NULL, 0);

    header = pcache->zvheader;
    index = utils_getindex(key, pcache->zvheader->valuecount);

    lock_lock(pcache->zvlock);
    flock = 1;

    pentry = find_zvcache_entry(pcache, key, index, 1);

    /* Adjust overall cache hitcount/misscount and entry hitcount */
    if(pentry == NULL)
    {
        InterlockedIncrement(&header->misscount);

        result = WARNING_ZVCACHE_EMISSING;
        goto Finished;
    }
    else
    {
        InterlockedIncrement(&pentry->hitcount);
        InterlockedIncrement(&header->hitcount);
    }

    /* Entry found. copyout to local memory */
    _ASSERT(pentry->zvalue  != 0);
    _ASSERT(pcache->outcopy != NULL);

    InterlockedExchange(&pentry->use_ticks, GetTickCount());
    pcached = (zval *)ZVALUE(pcache->incopy, pentry->zvalue);

    /* Private copy of the context, with the C stack of this copy (see ucache_stack_exhausted) */
    outcopy = *pcache->outcopy;
    outcopy.pstack = &stack;
    ucache_stack_start(&stack);

    /*
     * A fatal error while copying out (memory_limit reached by a big value) longjmps out
     * of here.  Release the cache lock before propagating it: a FastCGI process survives
     * the fatal error and would keep the mutex, blocking every other process forever.
     * What the copy had built is left to request shutdown, unreleased: no failure path made
     * holes of its unconverted elements, which still hold cache offsets.
     */
    zend_try
    {
        result = copyout_zval(pcache, &outcopy, NULL, &pending, pcached, pvalue);
    }
    zend_catch
    {
        bailout = 1;
    }
    zend_end_try();

    if(bailout)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
        free_pending_entries(pcache);
        zend_bailout();
    }

    if(FAILED(result))
    {
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }

    /* The expired entries the lookup removed */
    free_pending_entries(pcache);

    /*
     * The shared memory is not touched any more: user code may run now.  After a failed copy
     * the placeholders stay NULL: only the serialized strings are released.
     */
    if(!unserialize_pending_objects(&pending, SUCCEEDED(result)) && SUCCEEDED(result))
    {
        /* An object of the value could not be unserialized: return nothing rather than a NULL in
         * its place, as unserialize() returns false (the entry stays, for processes that can) */
        result = FATAL_ZVCACHE_INVALID_ZVAL;
    }
    zend_llist_destroy(&pending);

    /*
     * A value that is not returned is released here, without the cache lock: releasing can start
     * a GC run, which runs destructors.  After a failed copy it is what the copy had built, with
     * holes for the parts it had not converted (see copyout_zval).  *pvalue is NULL before the
     * release starts, so that it never points into a value being destroyed, even if a destructor
     * raises a fatal error.
     */
    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_get", result);

        ZVAL_COPY_VALUE(&released, *pvalue);
        ZVAL_NULL(*pvalue);
        zval_ptr_dtor(&released);
    }

    dprintverbose("end zvcache_get");

    return result;
}

/*
 * zvcache_set(): phase 2 ran out of shared memory, the cache is full.  Each round takes the cache
 * lock, runs a full pass (reclaim_entries) and runs phase 2 again, until the value fits.  Without
 * ucache.gc.evict, one round removes what the scavenger would, and that is all.  With it, the first
 * round evicts until ucache.gc.evict percent of the cache is free, and each next one until twice as
 * much, up to the whole cache: free memory in many small pieces may not hold the value.  If another
 * process ran a full pass since phase 2 began (rcgen), a round only runs phase 2 again.  A pass that
 * removed nothing, with the value still not fitting, makes every process skip the pass for a while
 * (rcfutile, rcticks, rcwait: hints, read and written without the lock), 20 times as long as the
 * pass took and at least RECLAIM_FUTILE_MS, so that such passes keep the cache lock for a small
 * share of the time at most.  Phase 2 can run again: a failure gave back what it had taken, and the
 * value and its serialized objects are still held by pprep.
 */
static int make_room(zvcache_context * pcache, const char * key, zvcopy_prepared * pprep, unsigned int ttl,
                     unsigned int gen, zvcache_value ** ppnewval)
{
    zvcache_header * header  = pcache->zvheader;
    zend_long        evict   = MIN(MAX(UCG(ucgc_evict), 0), 100);
    zend_long        percent = 0;
    int              result  = FATAL_OUT_OF_SMEMORY;
    unsigned int     removed = 0;
    unsigned int     rounds  = 0;
    unsigned int     tries   = 0;
    unsigned char    ran     = 0;
    LONGLONG         spent   = 0;
    LARGE_INTEGER    started;

    while(result == FATAL_OUT_OF_SMEMORY && tries++ < RECLAIM_TRIES_MAXIMUM)
    {
        if(header->rcfutile && utils_ticksdiff(GetTickCount(), header->rcticks) < header->rcwait)
        {
            break;
        }

        lock_lock(pcache->zvlock);

        if(header->rcgen != gen)
        {
            ran = 0;
        }
        else
        {
            rounds++;
            percent = (evict > 0) ? MIN(evict << MIN(rounds - 1, 7), 100) : 0;

            QueryPerformanceCounter(&started);
            removed = reclaim_entries(pcache, percent);
            spent   = scavenger_elapsed_us(&started) / 1000;

            header->rcgen++;
            ran = 1;
        }

        gen = header->rcgen;
        lock_unlock(pcache->zvlock);

        free_pending_entries(pcache);

        result = create_zvcache_data(pcache, key, pprep, ttl, ppnewval);
        if(result != FATAL_OUT_OF_SMEMORY)
        {
            header->rcfutile = 0;
            break;
        }

        if(ran && removed == 0)
        {
            header->rcwait   = (unsigned int)MAX(RECLAIM_FUTILE_MS, MIN(spent * 20, 60000));
            header->rcticks  = GetTickCount();
            header->rcfutile = 1;
            break;
        }

        if(ran && percent >= 100)
        {
            /* Nothing more can be evicted: the value does not fit in the cache */
            break;
        }

        if(ran && evict == 0)
        {
            break;
        }
    }

    return result;
}

int zvcache_set(zvcache_context * pcache, const char * key, zval * pzval, unsigned int ttl, unsigned char isadd)
{
    int             result   = NONFATAL;
    unsigned int    index    = 0;
    unsigned char   flock    = 0;
    unsigned char   prepared = 0;
    size_t          peak0    = 0;
    unsigned int    gen      = 0;
    zvcache_value * pentry   = NULL;
    zvcache_value * pnewval  = NULL;
    zvcopy_prepared prep;

    dprintverbose("start zvcache_set");

    _ASSERT(pcache != NULL);
    _ASSERT(key    != NULL);
    _ASSERT(pzval  != NULL);

    /* For zvcache_peak_over_memory_limit() */
    peak0 = zend_memory_peak_usage(1);

    /* Store the value, not the reference: the array form of ucache_set()/ucache_add()
     * passes array elements as they are, and they can be references. */
    ZVAL_DEREF(pzval);

    index = utils_getindex(key, pcache->zvheader->valuecount);

    lock_lock(pcache->zvlock);
    flock = 1;

    pentry = find_zvcache_entry(pcache, key, index, 1);

    /* If key already exists, throw error for add.  A set is a use of the entry only once it has */
    /* stored the new value (a new entry); a set that fails leaves the old entry as it was */
    if(pentry != NULL && isadd == 1)
    {
        _ASSERT(pentry->zvalue != 0);

        result = WARNING_ZVCACHE_EXISTS;
        goto Finished;
    }

    /*
     * Copy the value in without holding the cache lock: serializing objects runs user code
     * (__sleep(), __serialize(), Serializable::serialize()), which must not run under the
     * cross-process lock and may call ucache functions itself.  The shared memory allocator
     * has its own lock, and the new data is not reachable before add_zvcache_entry().
     * pentry must not be used past this point; it is looked up again below.
     */
    lock_unlock(pcache->zvlock);
    flock = 0;
    pentry = NULL;

    /* The expired entries the lookup removed, before user code runs */
    free_pending_entries(pcache);

    /* Phase 1: hold the value and serialize its objects, before any shared memory is taken */
    prepared = 1;
    result = copyin_prepare(&prep, pzval);
    if(FAILED(result))
    {
        goto Finished;
    }

    if(zvcache_peak_over_memory_limit(peak0))
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    /* Phase 2: if entry wasn't found or set was called, create new data */
    gen    = pcache->zvheader->rcgen;
    result = create_zvcache_data(pcache, key, &prep, ttl, &pnewval);
    if(result == FATAL_OUT_OF_SMEMORY)
    {
        /* The cache is full */
        result = make_room(pcache, key, &prep, ttl, gen, &pnewval);
    }
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Once more before the entry becomes visible, so that the check covers the whole call */
    if(zvcache_peak_over_memory_limit(peak0))
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    lock_lock(pcache->zvlock);
    flock = 1;

    zvcache_run_scavenger(pcache,0);

    /* Check again after scavenging */
    pentry = find_zvcache_entry(pcache, key, index, 1);

    /* some other process already added this entry, destroy cache data */
    if(pentry != NULL)
    {
        if(isadd == 1)
        {
            result = WARNING_ZVCACHE_EXISTS;
            goto Finished;
        }
        else
        {
            /* Delete the existing entry */
            remove_zvcache_entry(pcache, index, pentry);
            pentry = NULL;
        }
    }

    pentry = pnewval;
    pnewval = NULL;

    add_zvcache_entry(pcache, index, pentry);
    _ASSERT(pentry != NULL);

Finished:

    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }

    /* The entries removed under the lock: a replaced one, expired ones, a scavenger step's */
    free_pending_entries(pcache);

    if(pnewval != NULL)
    {
        destroy_zvcache_data(pcache, pnewval);
        pnewval = NULL;
    }

    /* Last: this can run destructors (see copyin_release) */
    if(prepared)
    {
        copyin_release(&prep);
        prepared = 0;
    }

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_set", result);
    }

    dprintverbose("end zvcache_set");

    return result;
}

int zvcache_delete(zvcache_context * pcache, const char * key)
{
    int             result = NONFATAL;
    unsigned int    index     = 0;
    unsigned char   flock     = 0;
    zvcache_value * pentry    = NULL;

    dprintverbose("start zvcache_delete");

    _ASSERT(pcache != NULL);
    _ASSERT(key    != NULL);

    index = utils_getindex(key, pcache->zvheader->valuecount);

    lock_lock(pcache->zvlock);
    flock = 1;

    zvcache_run_scavenger(pcache,0);

    /* An expired entry of key is missing for the caller, and removed on the way */
    pentry = find_zvcache_entry(pcache, key, index, 1);

    if(pentry == NULL)
    {
        result = WARNING_ZVCACHE_EMISSING;
        goto Finished;
    }

    remove_zvcache_entry(pcache, index, pentry);

    _ASSERT(SUCCEEDED(result));

Finished:

    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }

    free_pending_entries(pcache);

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_delete", result);
    }

    dprintverbose("end zvcache_delete");

    return result;
}

int zvcache_clear(zvcache_context * pcache)
{
    int              result = NONFATAL;
    unsigned int     index  = 0;
    zvcache_header * header = NULL;
    alloc_context *  palloc = NULL;
    zvcache_value *  pvalue = NULL;
    zvcache_value *  ptemp  = NULL;

    dprintverbose("start zvcache_clear %p", pcache);

    _ASSERT(pcache != NULL);

    lock_lock(pcache->zvlock);

    /* No point running the scavenger on call to clear */

    header = pcache->zvheader;
    palloc = pcache->zvalloc;

    for(index = 0; index < header->valuecount; index++)
    {
        if(header->values[index] == 0)
        {
            continue;
        }

        pvalue = ZVCACHE_VALUE(palloc, header->values[index]);

        while(pvalue != NULL)
        {
            ptemp = ZVCACHE_VALUE(palloc, pvalue->next_value);
            remove_zvcache_entry(pcache, index, pvalue);
            pvalue = ptemp;
        }

        ptemp = NULL;
    }

    _ASSERT(SUCCEEDED(result));

    lock_unlock(pcache->zvlock);

    /* The other processes see the empty cache already; the memory comes back now */
    free_pending_entries(pcache);

    dprintverbose("end zvcache_clear");

    return result;
}

int zvcache_exists(zvcache_context * pcache, const char * key, unsigned char * pexists)
{
    int             result = NONFATAL;
    unsigned int    index  = 0;
    unsigned char   flock  = 0;
    zvcache_value * pentry = NULL;

    dprintverbose("start zvcache_exists");

    _ASSERT(pcache  != NULL);
    _ASSERT(key     != NULL);
    _ASSERT(pexists != NULL);

    *pexists = 0;
    index = utils_getindex(key, pcache->zvheader->valuecount);

    lock_lock(pcache->zvlock);
    flock = 1;

    pentry = find_zvcache_entry(pcache, key, index, 1);

    if(pentry == NULL)
    {
        *pexists = 0;
    }
    else
    {
        *pexists = 1;
    }

    _ASSERT(SUCCEEDED(result));

    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }

    free_pending_entries(pcache);

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_exists", result);
    }

    dprintverbose("end zvcache_exists");

    return result;
}

static void zvcache_info_entry_dtor(void * pvoid)
{
    zvcache_info_entry * pinfo = NULL;

    _ASSERT(pvoid != NULL);
    pinfo = (zvcache_info_entry *)pvoid;

    if(pinfo->key != NULL)
    {
        alloc_efree(pinfo->key);
        pinfo->key = NULL;
    }

    return;
}

/*
 * Adds the entries zvcache_list() reports to plist. Call this under the cache lock. Without a
 * key, the entries in the order of the table: the first offset ones are skipped without copying
 * their keys, and at most limit ones are added (all of them with limit 0).
 */
static int list_zvcache_entries(zvcache_context * pcache, zend_bool summaryonly, char * pkey, zend_ulong offset, zend_ulong limit, unsigned int cticks, zend_llist * plist)
{
    int                  result   = NONFATAL;
    zvcache_header *     header   = pcache->zvheader;
    alloc_context *      palloc   = pcache->zvalloc;
    zvcache_value *      pvalue   = NULL;
    unsigned int         index    = 0;
    zvcache_info_entry   pinfo    = {0};
    unsigned int         count    = 0;
    zend_ulong           position = 0;
    zend_ulong           listed   = 0;

    if(pkey != NULL)
    {
        index = utils_getindex(pkey, header->valuecount);

        /* ucache_info() removes nothing */
        pvalue = find_zvcache_entry(pcache, pkey, index, 0);

        if(pvalue != NULL)
        {
            pinfo.key = alloc_estrdup(ZVALUE(pcache->incopy, pvalue->keystr));
            if(pinfo.key == NULL)
            {
                result = FATAL_OUT_OF_LMEMORY;
                goto Finished;
            }

            pinfo.ttl      = pvalue->ttlive;
            pinfo.age      = utils_ticksdiff(cticks, pvalue->add_ticks) / 1000;
            pinfo.type     = Z_TYPE_P((zval *)ZVALUE(pcache->incopy, pvalue->zvalue));
            pinfo.sizeb    = pvalue->sizeb;
            pinfo.hitcount = pvalue->hitcount;

            zend_llist_add_element(plist, &pinfo);
        }
    }
    else
    {
        /* Leave count to 0 if only summary is needed */
        if(!summaryonly)
        {
            count = header->valuecount;
        }

        for(index = 0; index < count; index++)
        {
            if(header->values[index] == 0)
            {
                continue;
            }

            pvalue = ZVCACHE_VALUE(palloc, header->values[index]);
            while(pvalue != NULL)
            {
                if(position++ < offset)
                {
                    pvalue = ZVCACHE_VALUE(palloc, pvalue->next_value);
                    continue;
                }

                pinfo.key = alloc_estrdup(ZVALUE(pcache->incopy, pvalue->keystr));
                if(pinfo.key == NULL)
                {
                    result = FATAL_OUT_OF_LMEMORY;
                    goto Finished;
                }

                pinfo.ttl      = pvalue->ttlive;
                pinfo.age      = utils_ticksdiff(cticks, pvalue->add_ticks) / 1000;
                pinfo.type     = Z_TYPE_P((zval *)ZVALUE(pcache->incopy, pvalue->zvalue));
                pinfo.sizeb    = pvalue->sizeb;
                pinfo.hitcount = pvalue->hitcount;

                zend_llist_add_element(plist, &pinfo);
                pvalue = ZVCACHE_VALUE(palloc, pvalue->next_value);

                /* Many long keys can pass memory_limit (see zvcache_over_memory_limit) */
                if(zvcache_over_memory_limit())
                {
                    result = FATAL_OUT_OF_LMEMORY;
                    goto Finished;
                }

                if(limit != 0 && ++listed == limit)
                {
                    goto Finished;
                }
            }
        }
    }

Finished:

    return result;
}

int zvcache_list(zvcache_context * pcache, zend_bool summaryonly, char * pkey, zend_ulong offset, zend_ulong limit, zvcache_info * pcinfo, zend_llist * plist)
{
    int                  result   = NONFATAL;
    unsigned char        flock    = 0;
    zvcache_header *     header   = NULL;
    unsigned int         cticks   = 0;
    volatile unsigned char bailout = 0;

    dprintverbose("start zvcache_list");

    _ASSERT(pcache != NULL);
    _ASSERT(pcinfo != NULL);
    _ASSERT(plist  != NULL);

    header = pcache->zvheader;

    lock_lock(pcache->zvlock);
    flock = 1;

    /* Under the lock: an entry added or a scavenger step run before it was taken would be */
    /* newer than ticks read earlier, and the difference would wrap to 49.7 days */
    cticks  = GetTickCount();

    pcinfo->hitcount  = header->hitcount;
    pcinfo->misscount = header->misscount;
    pcinfo->itemcount = header->itemcount;

    pcinfo->scavenged = (header->scruns != 0);
    pcinfo->scage     = pcinfo->scavenged ? utils_ticksdiff(cticks, header->scticks) / 1000 : 0;
    pcinfo->sccount   = header->sccount;
    pcinfo->scremoved = header->scremoved;
    pcinfo->scevicted = header->scevicted;

    zend_llist_init(plist, sizeof(zvcache_info_entry), zvcache_info_entry_dtor, 0);

    pcinfo->initage = utils_ticksdiff(cticks, header->init_ticks) / 1000;

    /*
     * Copying the keys can hit memory_limit, whose fatal error longjmps out of here.
     * Release the cache lock before propagating it (see zvcache_get). No goto may leave
     * the zend_try block, so the work is done in list_zvcache_entries().
     */
    zend_try
    {
        result = list_zvcache_entries(pcache, summaryonly, pkey, offset, limit, cticks, plist);
    }
    zend_catch
    {
        bailout = 1;
    }
    zend_end_try();

    if(bailout)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
        zend_bailout();
    }

    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_list", result);

        zend_llist_destroy(plist);
    }

    dprintverbose("end zvcache_list");

    return result;
}

int zvcache_change(zvcache_context * pcache, const char * key, zend_long delta, zend_long * newvalue)
{
    int             result = NONFATAL;
    unsigned char   flock  = 0;
    unsigned int    index  = 0;
    zvcache_value * pvalue = NULL;
    zval *          pzval  = NULL;

    dprintverbose("start zvcache_change");

    _ASSERT(pcache   != NULL);
    _ASSERT(key      != NULL);
    _ASSERT(newvalue != NULL);

    *newvalue = 0;
    index = utils_getindex(key, pcache->zvheader->valuecount);

    lock_lock(pcache->zvlock);
    flock = 1;

    zvcache_run_scavenger(pcache,0);

    pvalue = find_zvcache_entry(pcache, key, index, 1);

    if(pvalue == NULL)
    {
        result = WARNING_ZVCACHE_EMISSING;
        goto Finished;
    }

    /* A use, for ucache.gc.ttl */
    InterlockedExchange(&pvalue->use_ticks, GetTickCount());

    _ASSERT(pvalue->zvalue != 0);
    pzval = (zval *)ZVALUE(pcache->incopy, pvalue->zvalue);

    if(Z_TYPE_P(pzval) != IS_LONG)
    {
        result = WARNING_ZVCACHE_NOTLONG;
        goto Finished;
    }

    /* wraps around like before, without signed overflow (undefined behaviour) */
    Z_LVAL_P(pzval) = (zend_long)((zend_ulong)Z_LVAL_P(pzval) + (zend_ulong)delta);
    *newvalue = Z_LVAL_P(pzval);

Finished:

    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }

    free_pending_entries(pcache);

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_change", result);
    }

    dprintverbose("end zvcache_change");

    return result;
}

int zvcache_compswitch(zvcache_context * pcache, const char * key, zend_long oldvalue, zend_long newvalue)
{
    int             result = NONFATAL;
    unsigned char   flock  = 0;
    unsigned int    index  = 0;
    zvcache_value * pentry = NULL;
    zval *          pzval  = NULL;

    dprintverbose("start zvcache_compswitch");

    _ASSERT(pcache != NULL);
    _ASSERT(key    != NULL);

    index = utils_getindex(key, pcache->zvheader->valuecount);

    lock_lock(pcache->zvlock);
    flock = 1;

    zvcache_run_scavenger(pcache,0);

    pentry = find_zvcache_entry(pcache, key, index, 1);

    if(pentry == NULL)
    {
        result = WARNING_ZVCACHE_EMISSING;
        goto Finished;
    }

    /* A use, for ucache.gc.ttl */
    InterlockedExchange(&pentry->use_ticks, GetTickCount());

    _ASSERT(pentry->zvalue != 0);
    pzval = (zval *)ZVALUE(pcache->incopy, pentry->zvalue);

    if(Z_TYPE_P(pzval) != IS_LONG)
    {
        result = WARNING_ZVCACHE_NOTLONG;
        goto Finished;
    }

    if(Z_LVAL_P(pzval) == oldvalue)
    {
        Z_LVAL_P(pzval) = newvalue;
    }
    else
    {
        result = WARNING_ZVCACHE_CASNEQ;
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(flock)
    {
        lock_unlock(pcache->zvlock);
        flock = 0;
    }

    free_pending_entries(pcache);

    if(FAILED(result))
    {
        dprintimportant("failure %d in zvcache_compswitch", result);
    }

    dprintverbose("end zvcache_compswitch");

    return result;
}
