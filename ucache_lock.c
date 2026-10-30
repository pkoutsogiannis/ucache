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
   | Module: ucache_lock.c                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#ifdef _WIN64
#define LOCK_SUFFIX_FORMAT              "_%u_%u_%s_x64_"
#define LOCK_NAMESALT_SUFFIX_FORMAT     "_%u_%s_%u_%s_x64_"
#else  /* not _WIN64 */
#define LOCK_SUFFIX_FORMAT              "_%u_%u_%s_"
#define LOCK_NAMESALT_SUFFIX_FORMAT     "_%u_%s_%u_%s_"
#endif /* _WIN64 */

/* Prefix of every named lock object, so that lock (and init event) */
/* names never match the ones created by another extension */
#define LOCK_NAME_PREFIX                "UCACHE_"


/* Globals */
unsigned short glockid = 1;

/* Create a new lock context */
/* Call lock_initialize to actually create the lock */
int lock_create(lock_context** pplock) {
    int            result = NONFATAL;
    lock_context*  plock = NULL;

    dprintverbose("start lock_create");

    _ASSERT(pplock != NULL);
    *pplock = NULL;

    plock = (lock_context*)alloc_pemalloc(sizeof(lock_context));
    if (plock == NULL) {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    /* Initialize lock_context structure with default values */
    plock->id = glockid++;
    plock->type = LOCK_TYPE_INVALID;
    plock->unused = 0;
    plock->state = LOCK_STATE_UNLOCKED;
    plock->nameprefix = NULL;
    plock->namelen = 0;
    plock->hxlock = NULL;
    plock->last_owner = NULL;

    /* id is 8 bit field. Keep maximum to 255 */
    if (glockid == 256) {
        glockid = 1;
    }

    *pplock = plock;
    _ASSERT(SUCCEEDED(result));

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in lock_create", result);
    }

    dprintverbose("end lock_create");

    return result;
}

/* Destroy the lock context */
void lock_destroy(lock_context* plock) {
    dprintverbose("start lock_destroy");

    if (plock != NULL) {
        _ASSERT(plock->nameprefix == NULL);
        _ASSERT(plock->hxlock == NULL);

        alloc_pefree(plock);
        plock = NULL;
    }

    dprintverbose("end lock_destroy");
}

/* Caller must free ppnew_prefix using alloc_pefree() */
int lock_get_nameprefix(
    char* name,
    unsigned short cachekey,
    unsigned short type,
    char** ppnew_prefix,
    size_t* pcchnew_prefix
) {
    int          result = NONFATAL;
    char*        objname = 0;
    size_t       namelen = 0;
    int          actual_len = 0;
    int          pid = 0;
    const char*  group = NULL;

    /* Get the initial length of name prefix */
    namelen = strlen(name);
    _ASSERT(namelen > 0);

    /* If a namesalt is specified, put _<salt> in the name */
    if (UCG(namesalt) != NULL) {
        namelen += strlen(UCG(namesalt)) + 1;
    }

    /* A lock shared by the processes of the group has the group in its name (see */
    /* filemap_global_initialize). The pid stays 0 in the name: only the */
    /* process-local lock type, now removed, set it */
    switch (type) {
        case LOCK_TYPE_SHARED:
            group = UCG(fmapgdata)->group;
            namelen += GROUP_MAX_LENGTH;
            break;
        default:
            result = FATAL_LOCK_INVALID_TYPE;
            goto Finished;
    }

    /* Add 6 for underscores, 1 for null termination, */
    /* LOCK_NUMBER_MAX_STRLEN for adding cachekey, 2 for the pid (always 0), */
    /* PHP_UCACHE_VERSION_LEN, plus the length of the PHP Maj.Min version. */
    namelen = namelen + LOCK_NUMBER_MAX_STRLEN + 9;
    namelen += sizeof(STRVER2(PHP_MAJOR_VERSION, PHP_MINOR_VERSION));
    namelen += PHP_UCACHE_VERSION_LEN;

    /* Add the length of LOCK_NAME_PREFIX */
    namelen += sizeof(LOCK_NAME_PREFIX) - 1;

    /* Add 2 for lock-type padding.  The buffer will be used to tack on
     * an extra char for the lock type by the caller. */
    namelen += 2;

    /* Synchronization object names are limited to MAX_PATH characters */
    if (namelen >= MAX_PATH - 1) {
        result = FATAL_LOCK_LONGNAME;
        goto Finished;
    }

    objname = (char*)alloc_pemalloc(namelen + 1);
    if (objname == NULL) {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    ZeroMemory(objname, namelen + 1);

    /* Create nameprefix as name_pid_group_ */
    if (UCG(namesalt) == NULL) {
        actual_len = _snprintf_s(objname, namelen + 1, namelen, LOCK_NAME_PREFIX "%s_" STRVER2(PHP_MAJOR_VERSION, PHP_MINOR_VERSION) "_" PHP_UCACHE_VERSION LOCK_SUFFIX_FORMAT, name, cachekey, pid, group);
    } else {
        actual_len = _snprintf_s(objname, namelen + 1, namelen, LOCK_NAME_PREFIX "%s_" STRVER2(PHP_MAJOR_VERSION, PHP_MINOR_VERSION) "_" PHP_UCACHE_VERSION LOCK_NAMESALT_SUFFIX_FORMAT, name, cachekey, UCG(namesalt), pid, group);
    }

    if (-1 == actual_len) {
        error_setlasterror();
        result = FATAL_LOCK_LONGNAME;
        goto Finished;
    }

    /* Zero out the trailing portion of the buffer, for safety! */
    ZeroMemory(objname + actual_len, namelen - actual_len);

    *ppnew_prefix = objname;
    *pcchnew_prefix = actual_len;

Finished:
    return result;
}

/* Initialize the lock context with valid information */
/* lock is not ready to use unless initialize is called */
/*++

Arguments:

    plock           pointer to lock_context to be initialized
    name            unique name for this lock
    cachekey        ?
    type            scope of this lock (LOCK_TYPE_SHARED, the only one)
    plast_owner     Optional pointer to unsigned int that will receive the PID
                    of the process which last successfully acquired the lock.

Return Value:

    NONFATAL                    Success
    FAIL_LOCK_INIT_CREATMUTEX   Failed to create named mutex.  Call error_getlasterror() for more info.

--*/
int lock_initialize(lock_context* plock, char* name, unsigned short cachekey, unsigned short type, unsigned int* plast_owner) {
    int    result = NONFATAL;
    char*  objname = 0;
    size_t namelen = 0;

    dprintverbose("start lock_initialize");

    _ASSERT(plock != NULL);
    _ASSERT(name != NULL);
    _ASSERT(cachekey != 0);
    _ASSERT(cachekey <= LOCK_NUMBER_MAXIMUM);
    _ASSERT(type <= LOCK_TYPE_MAXIMUM);

    if (cachekey > LOCK_NUMBER_MAXIMUM) {
        result = FATAL_LOCK_NUMBER_LARGE;
        goto Finished;
    }

    result = lock_get_nameprefix(name, cachekey, type, &objname, &namelen);
    if (FAILED(result)) {
        goto Finished;
    }

    /* lock_get_nameprefix validates that 'type' is a valid lock type */
    plock->type = type;

    /* Use the name prefix as the lock name */
    plock->nameprefix = objname;
    _ASSERT(namelen < (MAX_PATH));
    plock->namelen = (unsigned short)namelen;

    /* Create mutex which will be used to synchronize access */
    plock->hxlock = CreateMutex(NULL, FALSE, objname);
    if (plock->hxlock == NULL) {
        dprintimportant("Failed to create lock %s due to error %u", objname, error_setlasterror());
        result = FATAL_LOCK_INIT_CREATEMUTEX;
        goto Finished;
    }

    plock->state = LOCK_STATE_UNLOCKED;
    plock->last_owner = plast_owner;
    _ASSERT(SUCCEEDED(result));

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in lock_initialize", result);

        if (plock->hxlock != NULL) {
            CloseHandle(plock->hxlock);
            plock->hxlock = NULL;
        }

        if (plock->nameprefix != NULL) {
            alloc_pefree(plock->nameprefix);
            plock->nameprefix = NULL;

            plock->namelen = 0;
        }

        plock->type = LOCK_TYPE_INVALID;
        plock->last_owner = NULL;
    }

    dprintverbose("end lock_initialize");

    return result;
}

void lock_terminate(lock_context* plock) {
    dprintverbose("start lock_terminate");

    if (plock != NULL) {
        if (plock->hxlock != NULL) {
            CloseHandle(plock->hxlock);
            plock->hxlock = NULL;
        }

        if (plock->nameprefix != NULL) {
            alloc_pefree(plock->nameprefix);
            plock->nameprefix = NULL;

            plock->namelen = 0;
        }

        plock->type = LOCK_TYPE_INVALID;
        plock->state = LOCK_STATE_UNLOCKED;
        plock->last_owner = NULL;
    }

    dprintverbose("end lock_terminate");
}

/*
 * Acquire write lock.  Returns NONFATAL; WARNING_LOCK_ABANDONED if the process that held the mutex
 * ended without releasing it (killed, for example by an IIS FastCGI timeout, or crashed): the mutex is
 * ours all the same, and what it guards may be half updated (see lock_undo_write); or FATAL_LOCK_WAIT if
 * the wait failed (an invalid handle): the lock is not held then, and the caller must not use what it
 * guards nor release the lock.
 */
int lock_lock(lock_context* plock) {
    int          result = NONFATAL;
    DWORD        ret = 0;
    unsigned int last_owner = 0;

    dprintverbose("start lock_lock   %p", plock);

    _ASSERT(plock != NULL);
    _ASSERT(plock->hxlock != NULL);

    /* Simple case. Just lock hxwrite */
    ret = WaitForSingleObject(plock->hxlock, INFINITE);

    /* PID of the process which got the lock before, for the messages below */
    if (plock->last_owner) {
        last_owner = *plock->last_owner;
    }

    /* The mutex is ours all the same: record it as for WAIT_OBJECT_0 below, so that lock_unlock() finds it locked */
    if (ret == WAIT_ABANDONED) {
        error_setlasterror();
        dprintcritical("lock_lock: acquired abandoned mutex %s (last owner pid %u). Something bad happend in another process!",
            plock->nameprefix, last_owner);
        result = WARNING_LOCK_ABANDONED;
    }

    if (ret == WAIT_FAILED) {
        dprintcritical("lock_lock: Failure waiting on lock %s (%d, last owner pid %u). Something bad happened!",
            plock->nameprefix, error_setlasterror(), last_owner);
        result = FATAL_LOCK_WAIT;
        goto Finished;
    }

    if (plock->last_owner) {
        *plock->last_owner = GetCurrentProcessId();
    }

    _ASSERT(plock->state == LOCK_STATE_UNLOCKED);
    plock->state = LOCK_STATE_LOCKED;

Finished:

    dprintverbose("end lock_lock     %p", plock);
    return result;
}

/*
 * The undo log.  IIS ends a FastCGI process at any instruction (activityTimeout, requestTimeout, a
 * stopped or recycled pool), and a process can crash.  If that happens while it holds a mutex in the
 * middle of an update of shared memory made of several writes (a split or a free of the allocator,
 * which changes block headers, the lists of free blocks and the counts; a hash chain of the cache and
 * its count), the update is left half done, and every process of the pool would go on with it: the
 * same memory given out twice, a crash, every later write failing, a chain that leads to freed memory.
 * So each such update writes every field that another process could see afterwards through
 * lock_undo_write(), which first records in the log, in the same shared memory, what the field held,
 * and ends with lock_undo_commit(), which empties the log.  The log is never empty when the lock is
 * free unless the process before ended inside an update (the mutex was abandoned, or created again
 * while the memory lived on), and the next process to take the lock puts the fields back from it,
 * the last first (lock_undo_replay), so that the update did not happen.  Nothing else is undone: the
 * other processes go on, their own updates complete.
 *
 * Order: an entry is complete before the count includes it, the count includes it before the field
 * changes, and the log is emptied only after the last write of the update, and before any write that
 * follows the update (a removed entry's links are reused for the queue of entries to free).  x64 makes
 * the writes of a process visible to the others in program order, and a process that is ended has
 * made visible every write it executed, so only the compiler could reorder them:
 * WholeProgramOptimization inlines these functions into the updates, and a volatile write (/volatile:ms)
 * only keeps the accesses before it before it, not the ones after.  _ReadWriteBarrier() keeps both in
 * place; it costs no instruction.  A replay is idempotent: a process ended while it replays leaves the
 * log as it was, and the next one replays it again.
 */
void lock_undo_initialize(lock_undo* pundo, void* pbase, size_t size, unsigned int* pcount, lock_undo_entry* pentries) {
    _ASSERT(pundo != NULL);
    _ASSERT(pbase != NULL);
    _ASSERT(pcount != NULL);
    _ASSERT(pentries != NULL);

    pundo->pcount = pcount;
    pundo->pentries = pentries;
    pundo->pbase = (char*)pbase;
    pundo->size = size;
}

/* An update logs more fields than the log holds (see lock_undo_write): out of line, it never happens */
static __declspec(noinline) void lock_undo_overflow(lock_undo* pundo) {
    if (*pundo->pcount != LOCK_UNDO_OVERFLOW) {
        dprintcritical("lock_undo_write: an update writes more than %u fields: it cannot be undone", LOCK_UNDO_MAXIMUM);
        *pundo->pcount = LOCK_UNDO_OVERFLOW;
    }
}

/*
 * Writes value into the field of width bytes (4 or 8) at pfield, after logging what the field held (see
 * above); use LOCK_UNDO_SET.  A log never takes more than LOCK_UNDO_MAXIMUM entries: an update that would
 * log more (no update does, see LOCK_UNDO_MAXIMUM) writes its fields without logging them, and marks the
 * log LOCK_UNDO_OVERFLOW.  If the process completes the update, the log is emptied as usual; if it ends
 * first, the next owner of the lock cannot undo the update whole, and does not use the structures the lock
 * guards (see lock_undo_replay): the calls fail rather than work on half-updated memory.
 */
void lock_undo_write(lock_undo* pundo, void* pfield, size_t width, unsigned __int64 value) {
    unsigned int      count = *pundo->pcount;
    lock_undo_entry*  pentry = NULL;

    _ASSERT(count < LOCK_UNDO_MAXIMUM);
    _ASSERT(width == 4 || width == 8);
    _ASSERT((char*)pfield >= pundo->pbase && (char*)pfield + width <= pundo->pbase + pundo->size);

    if (count < LOCK_UNDO_MAXIMUM) {
        pentry = pundo->pentries + count;
        pentry->where = (size_t)((char*)pfield - pundo->pbase) | ((width == 8) ? 1 : 0);
        pentry->value = (width == 8) ? *(unsigned __int64*)pfield : *(unsigned int*)pfield;

        /* The entry before the count that includes it */
        _ReadWriteBarrier();
        *pundo->pcount = count + 1;
    } else {
        lock_undo_overflow(pundo);
    }

    /* The count before the field */
    _ReadWriteBarrier();

    if (width == 8) {
        *(unsigned __int64*)pfield = value;
    } else {
        *(unsigned int*)pfield = (unsigned int)value;
    }
}

/* The update is complete: a process ended from here on leaves nothing to undo */
void lock_undo_commit(lock_undo* pundo) {
    /* Every write of the update before the log is emptied, and every write after the update after it */
    _ReadWriteBarrier();
    *pundo->pcount = 0;
    _ReadWriteBarrier();
}

/*
 * Call under the lock, when the log is not empty: puts back the fields of the update that the process
 * before left half done, the last first, and empties the log.  Returns how many, or -1 if the log cannot
 * be replayed: an update overflowed it (see lock_undo_write), or the memory is not a log (an entry outside
 * the segment, a count past the log).  It is left as it is then, nothing put back, so that every process
 * finds it so: the caller must not use the structures the lock guards.  Out of line: it runs only after a
 * process ended inside an update.
 */
__declspec(noinline) int lock_undo_replay(lock_undo* pundo) {
    unsigned int      count = *pundo->pcount;
    unsigned int      index = 0;
    size_t            offset = 0;
    size_t            width = 0;
    lock_undo_entry*  pentry = NULL;

    if (count > LOCK_UNDO_MAXIMUM) {
        dprintcritical("lock_undo_replay: the log of an update that was not completed counts %u entries: it cannot be replayed", count);
        return -1;
    }

    for (index = 0; index < count; index++) {
        pentry = pundo->pentries + index;
        width = (pentry->where & 1) ? 8 : 4;
        offset = pentry->where & ~(size_t)1;

        if (offset % width != 0 || pundo->size < width || offset > pundo->size - width) {
            dprintcritical("lock_undo_replay: entry %u of the log of an update that was not completed is outside the segment: it cannot be replayed", index);
            return -1;
        }
    }

    for (index = count; index > 0; index--) {
        pentry = pundo->pentries + index - 1;
        offset = pentry->where & ~(size_t)1;

        if (pentry->where & 1) {
            *(unsigned __int64*)(pundo->pbase + offset) = pentry->value;
        } else {
            *(unsigned int*)(pundo->pbase + offset) = (unsigned int)pentry->value;
        }
    }

    /* Every field back before the log is emptied */
    _ReadWriteBarrier();
    *pundo->pcount = 0;
    _ReadWriteBarrier();

    return (int)count;
}

/* Release write lock */
void lock_unlock(lock_context* plock) {
    dprintverbose("start lock_unlock %p", plock);

    _ASSERT(plock != NULL);
    _ASSERT(plock->hxlock != NULL);
    _ASSERT(plock->state == LOCK_STATE_LOCKED);

    /* Simple case. Just release the mutex */
    plock->state = LOCK_STATE_UNLOCKED;
    ReleaseMutex(plock->hxlock);

    dprintverbose("end lock_unlock   %p", plock);
    return;
}
