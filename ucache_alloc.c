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
   | Module: ucache_alloc.c                                                                       |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#define BLOCK_ISFREE_FIRST             (unsigned int)'iiii'
#define BLOCK_ISFREE_FREE              (unsigned int)'ffff'
#define BLOCK_ISFREE_USED              (unsigned int)'uuuu'
#define BLOCK_ISFREE_LAST              (unsigned int)'llll'

#define ALLOC_SEGMENT_HEADER_SIZE      (sizeof(alloc_segment_header))
#define ALLOC_FREE_BLOCK_OVERHEAD      (sizeof(alloc_free_header) + sizeof(size_t))
#define ALLOC_USED_BLOCK_OVERHEAD      (sizeof(alloc_used_header) + sizeof(size_t))

#define POINTER_OFFSET(baseaddr, p)    ((char *)p - (char *)baseaddr)
#define FREE_HEADER(baseaddr, offset)  (alloc_free_header *)((char *)baseaddr + offset)
#define USED_HEADER(baseaddr, offset)  (alloc_used_header *)((char *)baseaddr + offset)
#define FREE_BLOCK(baseaddr, offset)   ((offset == 0) ? NULL : FREE_HEADER(baseaddr, offset))

/* The smallest block the cache asks for: a zval (16 bytes, the top-level zval of an entry, or a pool tracker). */
/* A block is split only when the rest can hold that much: a smaller free block would never be used, and */
/* every search would go past it (a hole of 152 bytes split for an entry of 104 left 8 bytes) */
#define ALLOC_SMALLEST_REQUEST         16
#define MIN_FREE_BLOCK_SIZE            (ALLOC_FREE_BLOCK_OVERHEAD + ALLOC_SMALLEST_REQUEST)

/* An allocation looks at no more than this many blocks of its own class before the classes above */
#define ALLOC_CLASS_WALK               16

/* With the lock held for a series of frees (alloc_hold), it is released and taken again after this */
/* many, so that the allocations of the other processes wait a few microseconds at most */
#define ALLOC_HOLD_FREES               64

#define POOL_ALLOCATION_TYPE_UNKNOWN   0
#define POOL_ALLOCATION_TYPE_SMALL     1
#define POOL_ALLOCATION_TYPE_MEDIUM    2
#define POOL_ALLOCATION_TYPE_LARGE     3
#define POOL_ALLOCATION_TYPE_HUGE      4

#define POOL_ALLOCATION_SMALL_MAX      128
#define POOL_ALLOCATION_MEDIUM_MAX     256
#define POOL_ALLOCATION_LARGE_MAX      1024

#define POOL_ALLOCATION_SMALL_BSIZE    512
#define POOL_ALLOCATION_MEDIUM_BSIZE   1024
#define POOL_ALLOCATION_LARGE_BSIZE    4096

/*
 * Writes a field of the segment in an update under the allocator lock, after logging what it held (see
 * lock_undo_write): every write that a process could see once the update is undone goes through it, the
 * update ends with lock_undo_commit(), and the lock is taken with alloc_lock().  Not logged, because no
 * process reads them afterwards: what goes inside a block (the end of a used block, the header of the
 * rest of a block split, a block taken whole), the links of a block that was not free when the update
 * began (link_free_block), and what does not change.  An update logs 13 fields at most: see free_block()
 * and LOCK_UNDO_MAXIMUM.
 */
#define ALLOC_SET(palloc, field, value)  LOCK_UNDO_SET(&(palloc)->undo, field, value)

/* The count of the undo log on the first cache line of the segment, with last_owner (see alloc_lock) */
C_ASSERT(FIELD_OFFSET(alloc_segment_header, undocount) < 64 && FIELD_OFFSET(alloc_segment_header, last_owner) < 64);

/* Globals */
unsigned short gallocid = 1;

/* Private methods */
static int                alloc_lock(alloc_context* palloc);
static unsigned int       alloc_class_of(size_t size);
static void               link_free_block(alloc_context* palloc, alloc_free_header* freeh, unsigned char wasfree);
static void               unlink_free_block(alloc_context* palloc, alloc_free_header* freeh);
static void               drop_free_class(alloc_context* palloc, unsigned int cls);
static alloc_free_header* find_free_block(alloc_context* palloc, size_t size);
static int                allocate_memory(alloc_context* palloc, size_t size, void** ppaddr);
static void               free_block(alloc_context* palloc, alloc_used_header* usedh, unsigned char counted);
static void               free_memory(alloc_context* palloc, void* paddr);
static void               shrink_memory(alloc_context* palloc, void* paddr, size_t size);

static void*  alloc_malloc(alloc_context* palloc, unsigned int type, size_t size);
static char*  alloc_strdup(alloc_context* palloc, unsigned int type, const char* str);
static void   alloc_free(alloc_context* palloc, unsigned int type, void* addr);

/*
 * Takes the allocator lock, the only way to take it.  A process that ended in the middle of an update of
 * the allocator (see lock_undo_write) left its log in the segment header: the update is undone first, so
 * that the block being taken stays free, and the block being freed stays in use (lost with the rest of
 * its entry).  The log is replayed whenever it is not empty, not only when the mutex was abandoned: the
 * mutex is created again if every process closed it (a process that starts while the last one of the
 * pool exits, which closes its handles before its views), and the segment can outlive it.  Reading the
 * count costs nothing: it is on the cache line of last_owner, which lock_lock() has just written.  The
 * abandoned mutexes are counted (header->abandoned, for ucache_info()); a mutex created again is not.
 * Returns NONFATAL with the lock held; with a failure the lock is not held, and the caller must neither
 * use the segment nor release the lock: FATAL_LOCK_WAIT if the wait failed, FATAL_LOCK_UNDO if the log
 * cannot be replayed (see lock_undo_replay), which stays so for every process until the segment is
 * created again.  Call it only once alloc_initialize() has set the segment up.
 */
static int alloc_lock(alloc_context* palloc) {
    int                    result = NONFATAL;
    int                    undone = 0;
    alloc_segment_header*  header = palloc->header;

    result = lock_lock(palloc->lock);
    if (result == WARNING_LOCK_ABANDONED) {
        header->abandoned++;
        result = NONFATAL;
    }

    if (FAILED(result)) {
        return result;
    }

    if (header->undocount != 0) {
        undone = lock_undo_replay(&palloc->undo);
        if (undone < 0) {
            lock_unlock(palloc->lock);
            return FATAL_LOCK_UNDO;
        }

        dprintcritical("alloc_lock: put back %d fields of an update of the allocator that a process left half done", undone);
    }

    return NONFATAL;
}

/*
 * Free blocks are kept in one doubly linked list per size class, and a bitmap in the segment header
 * tells which lists hold blocks, so that an allocation and a free take the same time whatever the
 * number of free blocks.  Below ALLOC_CLASS_LINEAR_BYTES a class holds one size; above, a class
 * holds the sizes of an eighth of a power of two.  A free block is still combined with the free
 * blocks on both sides of it, which the sizes at both ends of every block find.  With every free
 * block in one list in address order, as before, an allocation went through the list from the start
 * for the first block large enough, which took time in proportion to the free blocks before that one
 * (1.6 ms per ucache_set() of a 1000-byte string with 250000 smaller free blocks), and a free looked
 * for the place of the block in it.
 */
static unsigned int alloc_class_of(size_t size) {
    unsigned long msb = 0;
    size_t        cls = 0;

    if (size < ALLOC_CLASS_LINEAR_BYTES) {
        return (unsigned int)(size / ALLOC_ALIGNMENT);
    }

#ifdef _WIN64
    _BitScanReverse64(&msb, (unsigned __int64)size);
#else  /* _WIN64 */
    _BitScanReverse(&msb, (unsigned long)size);
#endif /* _WIN64 */

    cls = ALLOC_CLASS_LINEAR_COUNT + ((size_t)(msb - ALLOC_CLASS_LINEAR_LOG2) << ALLOC_CLASS_SPLIT_LOG2)
        + ((size >> (msb - ALLOC_CLASS_SPLIT_LOG2)) & ((1 << ALLOC_CLASS_SPLIT_LOG2) - 1));

    return (unsigned int)MIN(cls, ALLOC_CLASS_COUNT - 1);
}

/*
 * Call under the allocator lock, in an update (see ALLOC_SET): puts a free block, its size set, first in
 * the list of its class (2 fields logged, 4 if wasfree).  wasfree: the block was free when the update
 * began (the free block before a block being freed, which grows), so an undo must give it its links
 * back; otherwise its links are written without a log: they become the inside of a used block again, or
 * of the free block that a split cut.
 */
static void link_free_block(alloc_context* palloc, alloc_free_header* freeh, unsigned char wasfree) {
    alloc_segment_header*  header = palloc->header;
    void*                  segaddr = palloc->memaddr;
    unsigned int           cls = alloc_class_of(freeh->size);
    size_t                 offset = POINTER_OFFSET(segaddr, freeh);
    size_t                 next = header->freelists[cls];

    if (wasfree) {
        ALLOC_SET(palloc, freeh->prev_free, 0);
        ALLOC_SET(palloc, freeh->next_free, next);
    } else {
        freeh->prev_free = 0;
        freeh->next_free = next;
    }

    if (next != 0) {
        ALLOC_SET(palloc, (FREE_HEADER(segaddr, next))->prev_free, offset);
    } else {
        ALLOC_SET(palloc, header->freemap[cls / 32], header->freemap[cls / 32] | (1u << (cls % 32)));
    }

    ALLOC_SET(palloc, header->freelists[cls], offset);
}

/*
 * Call under the allocator lock, in an update (see ALLOC_SET): takes a free block out of the list of its
 * class, its size as when it was linked (2 fields logged at most).  Its own links are left as they are:
 * the block becomes a used block, which never reads them, or part of a larger free block, whose links
 * link_free_block() sets; and after an undo they are what its neighbours in the list point back to.
 */
static void unlink_free_block(alloc_context* palloc, alloc_free_header* freeh) {
    alloc_segment_header*  header = palloc->header;
    void*                  segaddr = palloc->memaddr;
    unsigned int           cls = 0;

    if (freeh->prev_free != 0) {
        ALLOC_SET(palloc, (FREE_HEADER(segaddr, freeh->prev_free))->next_free, freeh->next_free);
    } else {
        cls = alloc_class_of(freeh->size);
        ALLOC_SET(palloc, header->freelists[cls], freeh->next_free);

        if (freeh->next_free == 0) {
            ALLOC_SET(palloc, header->freemap[cls / 32], header->freemap[cls / 32] & ~(1u << (cls % 32)));
        }
    }

    if (freeh->next_free != 0) {
        ALLOC_SET(palloc, (FREE_HEADER(segaddr, freeh->next_free))->prev_free, freeh->prev_free);
    }
}

/*
 * Call under the allocator lock, before an update writes anything: the list of a class leads to a block
 * that is not free, or its bit is set while it is empty.  No process leaves that, since every update is
 * undone if it is cut off (see alloc_lock); memory damaged otherwise could.  The class is emptied, so
 * that this list does not make every allocation of a smaller size fail: its blocks stay unused until the
 * processes of the pool have exited (free_size and freecount still count them).  Not logged: a process
 * ended between its two writes leaves an empty list with its bit set, which is dropped again.
 */
static void drop_free_class(alloc_context* palloc, unsigned int cls) {
    dprintcritical("allocate_memory: the free list of class %u is damaged: its blocks are left unused", cls);

    palloc->header->freelists[cls] = 0;
    palloc->header->freemap[cls / 32] &= ~(1u << (cls % 32));
}

/*
 * Call under the allocator lock: a free block of at least size bytes, NULL if there is none, the
 * smallest that the lists find at once (a best fit, by class).  The class of the size first: below
 * ALLOC_CLASS_LINEAR_BYTES its first block fits, above it the first of its ALLOC_CLASS_WALK first
 * blocks that is large enough.  Then the smallest of the first ALLOC_CLASS_WALK blocks of the smallest
 * class above (its first block below ALLOC_CLASS_LINEAR_BYTES; used whole if the rest could not be a
 * free block, split otherwise), then the rest of the class of the size, so that the allocation fails
 * only if no free block is large enough, as before.  Preferring the blocks that a split leaves a free
 * block of, before the smaller ones that fit, let the zvals of new entries split the holes of old
 * entries and left the small pieces of other splits unused: in a cache kept full by eviction, writes
 * then failed with up to 5% of the memory free in such pieces, where this fails, as the single list in
 * address order of before did, with less than 0.3% free.  A list that leads to a block that is not
 * free is dropped (see drop_free_class), never followed.
 */
static alloc_free_header* find_free_block(alloc_context* palloc, size_t size) {
    alloc_segment_header*  header = palloc->header;
    void*                  segaddr = palloc->memaddr;
    alloc_free_header*     freeh = NULL;
    alloc_free_header*     pfirst = NULL;
    alloc_free_header*     pnext = NULL;
    unsigned int           cls = alloc_class_of(size);
    unsigned int           word = 0;
    unsigned int           bits = 0;
    unsigned int           walked = 0;
    unsigned int           seen = 0;
    unsigned long          bit = 0;

    for (freeh = FREE_BLOCK(segaddr, header->freelists[cls]); freeh != NULL && walked < ALLOC_CLASS_WALK; walked++) {
        if (freeh->is_free != BLOCK_ISFREE_FREE) {
            drop_free_class(palloc, cls);
            freeh = NULL;
            break;
        }

        if (freeh->size >= size) {
            return freeh;
        }

        freeh = FREE_BLOCK(segaddr, freeh->next_free);
    }

    if (cls + 1 < ALLOC_CLASS_COUNT) {
        word = (cls + 1) / 32;
        bits = header->freemap[word] & (~0u << ((cls + 1) % 32));

        while (1) {
            if (bits != 0) {
                _BitScanForward(&bit, bits);

                pfirst = FREE_BLOCK(segaddr, header->freelists[word * 32 + bit]);
                if (pfirst != NULL && pfirst->is_free == BLOCK_ISFREE_FREE) {
                    /* Above ALLOC_CLASS_LINEAR_BYTES the blocks of a class differ in size: the smallest of its first */
                    /* ALLOC_CLASS_WALK, so that a small part of a value (its entry or its zval) does not take the */
                    /* only block of the class that a larger part fits (the block freed last is first in the list) */
                    if (word * 32 + bit >= ALLOC_CLASS_LINEAR_COUNT) {
                        pnext = FREE_BLOCK(segaddr, pfirst->next_free);
                        for (seen = 1; pnext != NULL && seen < ALLOC_CLASS_WALK && pnext->is_free == BLOCK_ISFREE_FREE; seen++) {
                            if (pnext->size < pfirst->size) {
                                pfirst = pnext;
                            }
                            pnext = FREE_BLOCK(segaddr, pnext->next_free);
                        }
                    }
                    return pfirst;
                }

                drop_free_class(palloc, word * 32 + bit);
                bits &= ~(1u << bit);
                continue;
            }

            if (++word == ALLOC_CLASS_WORDS) {
                break;
            }

            bits = header->freemap[word];
        }
    }

    for (; freeh != NULL; freeh = FREE_BLOCK(segaddr, freeh->next_free)) {
        if (freeh->is_free != BLOCK_ISFREE_FREE) {
            drop_free_class(palloc, cls);
            return NULL;
        }

        if (freeh->size >= size) {
            return freeh;
        }
    }

    return NULL;
}

/*
 * One update of the allocator (see ALLOC_SET), which logs 9 fields at most: a block split whose rest goes
 * in another class (unlinked 2, header 2, the end of the block 1, the rest linked 2, counts 2); 7 if the
 * rest takes its place in the list; 6 for a block taken whole.
 */
static int allocate_memory(alloc_context* palloc, size_t size, void** ppaddr) {
    int                    result = NONFATAL;
    void*                  paddr = NULL;
    void*                  segaddr = NULL;
    unsigned char          flock = 0;
    unsigned char          samecls = 0;

    size_t                 oldsize = 0;
    size_t                 oldprev = 0;
    size_t                 oldnext = 0;
    size_t                 restsiz = 0;
    size_t                 restoff = 0;

    alloc_segment_header* header = NULL;
    alloc_free_header*    freeh = NULL;
    alloc_used_header*    usedh = NULL;

    dprintdecorate("start allocate_memory");

    _ASSERT(palloc != NULL);
    _ASSERT(palloc->lock != NULL);
    _ASSERT(palloc->header != NULL);
    _ASSERT(size >= 0);

    /* Allocate memory from memory segment shared with other processes */
    segaddr = palloc->memaddr;
    header = palloc->header;

    /* Allocate in sizes which are multiples of size_t */
#ifdef _WIN64
    size = ALIGNQWORD(size);
#else /* _WIN64 */
    size = ALIGNDWORD(size);
#endif /* _WIN64 */

    /* Acquire lock to segment before doing anything, unless the process holds it (alloc_hold) */
    if (palloc->held == 0) {
        result = alloc_lock(palloc);
        if (FAILED(result)) {
            goto Finished;
        }

        flock = 1;
    }

    /* If available size is less than requested size, return NULL */
    if (header->free_size < size) {
        result = FATAL_ALLOC_NO_MEMORY;
        goto Finished;
    }

    freeh = find_free_block(palloc, size);
    if (freeh == NULL) {
        result = FATAL_ALLOC_NO_MEMORY;
        goto Finished;
    }

    _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE);

    /* Got a free block with sufficient free memory. Now */
    /* see if block size is big enough to be broken into two */
    if (freeh->size >= size + MIN_FREE_BLOCK_SIZE) {
        oldsize = freeh->size;
        oldprev = freeh->prev_free;
        oldnext = freeh->next_free;
        restsiz = oldsize - size - ALLOC_USED_BLOCK_OVERHEAD;

        /* The rest of a block of the same class takes its place in the list (the large free block */
        /* that an empty cache is, mostly); else the rest goes in the list of its own class */
        samecls = (alloc_class_of(restsiz) == alloc_class_of(oldsize));
        if (!samecls) {
            unlink_free_block(palloc, freeh);
        }

        /* The header of the free block becomes that of the used block */
        usedh = (alloc_used_header*)freeh;
        ALLOC_SET(palloc, usedh->size, size);
        ALLOC_SET(palloc, usedh->is_free, BLOCK_ISFREE_USED);

        /* Return pointer right after the used header */
        paddr = (void*)(usedh + 1);

        /* The size of the used block at its end, and the header of the rest after it, inside the free */
        /* block: not logged (see ALLOC_SET). The size at the end of the free block is now the rest's */
        *((size_t*)((char*)paddr + size)) = size;

        freeh = (alloc_free_header*)((char*)usedh + size + ALLOC_USED_BLOCK_OVERHEAD);
        restoff = POINTER_OFFSET(segaddr, freeh);
        freeh->size = restsiz;
        freeh->is_free = BLOCK_ISFREE_FREE;
        ALLOC_SET(palloc, *((size_t*)(((char*)(freeh + 1)) + restsiz)), restsiz);

        if (samecls) {
            freeh->prev_free = oldprev;
            freeh->next_free = oldnext;

            if (oldprev != 0) {
                ALLOC_SET(palloc, (FREE_HEADER(segaddr, oldprev))->next_free, restoff);
            } else {
                ALLOC_SET(palloc, header->freelists[alloc_class_of(restsiz)], restoff);
            }

            if (oldnext != 0) {
                ALLOC_SET(palloc, (FREE_HEADER(segaddr, oldnext))->prev_free, restoff);
            }
        } else {
            link_free_block(palloc, freeh, 0);
        }

        /* The number of free blocks does not change */
        ALLOC_SET(palloc, header->free_size, header->free_size - (size + ALLOC_USED_BLOCK_OVERHEAD));
        ALLOC_SET(palloc, header->usedcount, header->usedcount + 1);
    } else {
        /* Block is very small and should be used completely */
        unlink_free_block(palloc, freeh);

        /* Update size to indicate number of bytes we are actually allocating: the size in the */
        /* header and at the end of the block stay as they are */
        size = freeh->size;

        usedh = (alloc_used_header*)freeh;
        ALLOC_SET(palloc, usedh->is_free, BLOCK_ISFREE_USED);

        /* Return pointer right after the used header */
        paddr = (void*)(usedh + 1);

        /* One free block got converted to used block */
        ALLOC_SET(palloc, header->free_size, header->free_size - size);
        ALLOC_SET(palloc, header->freecount, header->freecount - 1);
        ALLOC_SET(palloc, header->usedcount, header->usedcount + 1);
    }

    lock_undo_commit(&palloc->undo);

    *ppaddr = paddr;

Finished:

    if (flock) {
        lock_unlock(palloc->lock);
        flock = 0;
    }

    if (FAILED(result)) {
        _ASSERT(*ppaddr == NULL);

        dprintverbose("failure %d in allocate_memory", result);
    }

    dprintdecorate("end allocate_memory");

    return result;
}

/*
 * Call under the allocator lock, in an update (see ALLOC_SET): makes a used block free, combined with the
 * free blocks on both sides of it, and puts the result in the list of its class.  counted: the block
 * counts as a used block (shrink_memory frees the end of a block, which did not).  Logs 13 fields at most,
 * the most an update logs: with free blocks on both sides, unlinked (2 + 2), the size in the header and at
 * the end of the block (2), the block before, which grows, linked with its own links (4), and the counts
 * (3), each written once.
 */
static void free_block(alloc_context* palloc, alloc_used_header* usedh, unsigned char counted) {
    size_t                 blocksiz = usedh->size;
    size_t                 lastsize = 0;
    alloc_segment_header*  header = palloc->header;
    alloc_free_header*     freeh = NULL;
    alloc_free_header*     pfree = NULL;
    size_t*                pend = NULL;
    size_t                 freesize = header->free_size + blocksiz;
    unsigned int           freecnt = header->freecount + 1;
    unsigned char          wasfree = 0;

    /* The block becomes a free block, combined below with the free blocks on both sides of it */
    freeh = (alloc_free_header*)usedh;

    /* Check if next block is free */
    pfree = (alloc_free_header*)((char*)usedh + blocksiz + ALLOC_USED_BLOCK_OVERHEAD);
    if (pfree->is_free == BLOCK_ISFREE_FREE) {
        unlink_free_block(palloc, pfree);
        blocksiz += pfree->size + ALLOC_FREE_BLOCK_OVERHEAD;

        freecnt--;
        freesize += ALLOC_FREE_BLOCK_OVERHEAD;
    }

    /* Check if previous block is free */
    lastsize = *((size_t*)((char*)usedh - sizeof(size_t)));
    pfree = (alloc_free_header*)((char*)usedh - ALLOC_FREE_BLOCK_OVERHEAD - lastsize);

    _ASSERT(pfree->size == lastsize);
    if (pfree->is_free == BLOCK_ISFREE_FREE) {
        unlink_free_block(palloc, pfree);
        blocksiz += pfree->size + ALLOC_FREE_BLOCK_OVERHEAD;
        freeh = pfree;
        wasfree = 1;

        freecnt--;
        freesize += ALLOC_FREE_BLOCK_OVERHEAD;
    }

    /* The header and the end of the free block, where they change: the block before was free already, */
    /* and a block that joins nothing keeps its size */
    if (freeh->size != blocksiz) {
        ALLOC_SET(palloc, freeh->size, blocksiz);
    }

    if (!wasfree) {
        ALLOC_SET(palloc, freeh->is_free, BLOCK_ISFREE_FREE);
    }

    pend = (size_t*)(((char*)(freeh + 1)) + blocksiz);
    if (*pend != blocksiz) {
        ALLOC_SET(palloc, *pend, blocksiz);
    }

    link_free_block(palloc, freeh, wasfree);

    if (counted) {
        ALLOC_SET(palloc, header->usedcount, header->usedcount - 1);
    }

    ALLOC_SET(palloc, header->freecount, freecnt);
    ALLOC_SET(palloc, header->free_size, freesize);
}

/* Whether paddr is the address that allocate_memory() returned for a block in use: logged when it is not */
static zend_bool used_block(alloc_context* palloc, void* paddr) {
    void*              segaddr = palloc->memaddr;
    alloc_used_header* usedh = (alloc_used_header*)((char*)paddr - sizeof(alloc_used_header));

    /* Validate the memory being freed is in the heap */
    if ((void*)usedh < segaddr || (char*)usedh >((char*)segaddr + palloc->size)) {
        _ASSERT(0);
        dprintcritical("free_memory: Address %p not in segment %p (size %d)",
            paddr, segaddr, palloc->size);

        return 0;
    }

    /* Block is in our heap.  Validate it is in use */
    _ASSERT(usedh->is_free == BLOCK_ISFREE_USED);
    if (usedh->is_free != BLOCK_ISFREE_USED) {
        dprintcritical("free_memory: Address %p not in use in segment %p (size %d)",
            paddr, segaddr, palloc->size);

        return 0;
    }

    return 1;
}

static void free_memory(alloc_context* palloc, void* paddr) {
    dprintdecorate("start free_memory");

    _ASSERT(palloc != NULL);
    _ASSERT(paddr != NULL);

    /* Free the block from shared segment, under the lock, unless the process holds it (alloc_hold). */
    /* Without the lock (see alloc_lock), the block stays in use */
    if (palloc->held == 0 && FAILED(alloc_lock(palloc))) {
        goto Finished;
    }

    if (used_block(palloc, paddr)) {
        free_block(palloc, (alloc_used_header*)((char*)paddr - sizeof(alloc_used_header)), 1);
        lock_undo_commit(&palloc->undo);
    }

    if (palloc->held == 0) {
        lock_unlock(palloc->lock);
    } else if (++palloc->heldfrees >= ALLOC_HOLD_FREES) {
        /* Let the other processes allocate (a released mutex goes to a process that waits for it). If */
        /* the lock cannot be taken again, it is not held any more: the next frees take it each */
        lock_unlock(palloc->lock);
        if (FAILED(alloc_lock(palloc))) {
            palloc->held = 0;
        }
        palloc->heldfrees = 0;
    }

Finished:

    dprintdecorate("end free_memory");

    return;
}

/*
 * Gives back the end of a used block past its first size bytes, when that is large enough to be a free
 * block: the rest becomes a used block of its own, which is freed at once (combined with the free
 * block after it, if any).  Nothing moves: the block keeps its address and its first size bytes.  One
 * update (see ALLOC_SET), which logs 11 fields at most: the size in the header and at the end of the
 * block (2), and the free of the rest, which never counted as a used block and has a used block before
 * it (9).
 */
static void shrink_memory(alloc_context* palloc, void* paddr, size_t size) {
    alloc_used_header*  usedh = NULL;
    alloc_used_header*  resth = NULL;
    size_t              restsiz = 0;

    _ASSERT(palloc != NULL);
    _ASSERT(paddr != NULL);

#ifdef _WIN64
    size = ALIGNQWORD(size);
#else /* _WIN64 */
    size = ALIGNDWORD(size);
#endif /* _WIN64 */

    /* Without the lock (see alloc_lock), the block keeps its size */
    if (palloc->held == 0 && FAILED(alloc_lock(palloc))) {
        return;
    }

    if (!used_block(palloc, paddr)) {
        goto Finished;
    }

    usedh = (alloc_used_header*)((char*)paddr - sizeof(alloc_used_header));
    if (usedh->size < size + MIN_FREE_BLOCK_SIZE) {
        goto Finished;
    }

    /* The rest: its header after the new end of the block, and that new end, inside the block, are not */
    /* logged (see ALLOC_SET); its size at the end, where the block's was, and the size of the block are */
    restsiz = usedh->size - size - ALLOC_USED_BLOCK_OVERHEAD;
    resth = (alloc_used_header*)((char*)paddr + size + sizeof(size_t));
    resth->size = restsiz;
    resth->is_free = BLOCK_ISFREE_USED;
    *((size_t*)((char*)paddr + size)) = size;

    ALLOC_SET(palloc, *((size_t*)((char*)(resth + 1) + restsiz)), restsiz);
    ALLOC_SET(palloc, usedh->size, size);

    free_block(palloc, resth, 0);
    lock_undo_commit(&palloc->undo);

Finished:

    if (palloc->held == 0) {
        lock_unlock(palloc->lock);
    }

    return;
}

/*
 * The process takes the allocator lock once for a series of frees (the batches of ucache_clear() and of
 * a full pass), instead of once for each block: allocate_memory(), free_memory() and alloc_getinfo() do
 * not take it while it is held.  Every ALLOC_HOLD_FREES frees, free_memory() releases it and takes it
 * again, so that the allocations of the other processes are not held up.  Hold it only for work in shared
 * memory: nothing in between may run user code, allocate request memory, or take the cache lock (which
 * is taken before this one everywhere else).  Returns NONFATAL with the lock held; otherwise (see
 * alloc_lock) it is not held, and the caller must not call alloc_release(): its frees take the lock each.
 */
int alloc_hold(alloc_context* palloc) {
    int result = NONFATAL;

    _ASSERT(palloc != NULL);
    _ASSERT(palloc->lock != NULL);

    if (palloc->held == 0) {
        result = alloc_lock(palloc);
        if (FAILED(result)) {
            return result;
        }

        palloc->heldfrees = 0;
    }

    palloc->held++;

    return NONFATAL;
}

void alloc_release(alloc_context* palloc) {
    _ASSERT(palloc != NULL);

    /* held is 0 if free_memory() could not take the lock again: nothing is held then */
    if (palloc->held != 0 && --palloc->held == 0) {
        lock_unlock(palloc->lock);
    }
}

static void* alloc_malloc(alloc_context* palloc, unsigned int type, size_t size) {
    void* result = NULL;

    _ASSERT(size >= 0);

    switch (type) {
        case ALLOC_TYPE_SHAREDMEM:
            if (FAILED(allocate_memory(palloc, size, &result))) {
                return NULL;
            }
            break;
        case ALLOC_TYPE_PROCESS:
            result = pemalloc(size, 0);
            break;
        case ALLOC_TYPE_PROCESS_PERSISTENT:
            result = pemalloc(size, 1);
            break;
        default:
            _ASSERT(FALSE);
            break;
    }

    return result;
}

static char* alloc_strdup(alloc_context* palloc, unsigned int type, const char* str) {
    char*  result = NULL;
    size_t strl = 0;

    _ASSERT(str != NULL);
    strl = strlen(str) + 1;

    switch (type) {
        case ALLOC_TYPE_SHAREDMEM:
            if (FAILED(allocate_memory(palloc, strl, &result))) {
                return NULL;
            }
            memcpy_s(result, strl, str, strl);
            break;
        case ALLOC_TYPE_PROCESS:
            result = pestrdup(str, 0);
            break;
        case ALLOC_TYPE_PROCESS_PERSISTENT:
            result = pestrdup(str, 1);
            break;
        default:
            _ASSERT(FALSE);
            break;
    }

    return result;
}

static void alloc_free(alloc_context* palloc, unsigned int type, void* addr) {
    _ASSERT(addr != NULL);

    switch (type) {
        case ALLOC_TYPE_SHAREDMEM:
            free_memory(palloc, addr);
            break;
        case ALLOC_TYPE_PROCESS:
            pefree(addr, 0);
            break;
        case ALLOC_TYPE_PROCESS_PERSISTENT:
            pefree(addr, 1);
            break;
        default:
            _ASSERT(FALSE);
            break;
    }

    return;
}

int alloc_create(alloc_context** ppalloc) {
    int             result = NONFATAL;
    alloc_context*  palloc = NULL;

    dprintverbose("start alloc_create");
    _ASSERT(ppalloc != NULL);

    *ppalloc = NULL;

    /* Allocate memory for context */
    palloc = (alloc_context*)alloc_pemalloc(sizeof(alloc_context));
    if (palloc == NULL) {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    /* Initialize member variables */
    palloc->id = gallocid++;
    palloc->memaddr = NULL;
    palloc->size = 0;
    palloc->lock = NULL;
    palloc->header = NULL;
    palloc->held = 0;
    palloc->heldfrees = 0;
    memset(&palloc->undo, 0, sizeof(palloc->undo));

    *ppalloc = palloc;

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in alloc_create", result);
    }

    dprintverbose("end alloc_create");
    return result;
}

void alloc_destroy(alloc_context* palloc) {
    dprintverbose("start alloc_destroy");

    if (palloc != NULL) {
        alloc_pefree(palloc);
        palloc = NULL;
    }

    dprintverbose("end alloc_destroy");

    return;
}

/*
 * *pinitmemory is 1 for a segment in a file mapping object that the caller has just created, 0 for one that it
 * opened. An opened segment is used as it is only once a process has completed its initialization (alloc_initdone):
 * otherwise the process that was initializing it died before it was done, and the segment is initialized again,
 * which *pinitmemory then tells the caller. The caller holds the init mutex of the cache throughout (ZVCACHE_INIT),
 * so no other process initializes or opens the segment meanwhile, and no process uses a segment that is not marked
 */
int alloc_initialize(alloc_context* palloc, char* name, unsigned short cachekey, void* staddr, size_t size, unsigned char* pinitmemory) {
    int                    result = NONFATAL;
    alloc_segment_header*  header = NULL;
    alloc_free_header*     frestart = NULL;
    alloc_free_header*     fremid = NULL;
    alloc_free_header*     freend = NULL;

    dprintverbose("start alloc_initialize");

    _ASSERT(palloc != NULL);
    _ASSERT(cachekey != 0);
    _ASSERT(staddr != NULL);
    _ASSERT(size > 0);
    _ASSERT(pinitmemory != NULL);

    palloc->memaddr = staddr;
    palloc->size = size;
    palloc->header = (alloc_segment_header*)staddr;
    header = palloc->header;

    /* The undo log of the allocator, in the segment header (see alloc_lock) */
    lock_undo_initialize(&palloc->undo, staddr, size, &header->undocount, header->undo);

    result = lock_create(&palloc->lock);
    if (FAILED(result)) {
        goto Finished;
    }

    result = lock_initialize(palloc->lock, name, cachekey, LOCK_TYPE_SHARED, &header->last_owner);
    if (FAILED(result)) {
        goto Finished;
    }

    /* Opened, but the process that initialized it died before it marked it */
    if (*pinitmemory == 0 && header->initdone != ALLOC_SEGMENT_INITDONE) {
        dprintimportant("alloc_initialize: the initialization of segment %s was not completed: initializing it again", palloc->lock->nameprefix);
        *pinitmemory = 1;
    }

    /* A safety net: the caller maps a segment it opens at the size it has (filemap_initialize), which */
    /* the process that initialized it recorded. No cache is better than a free list past the view */
    if (*pinitmemory == 0 && header->total_size != size) {
        dprintimportant("alloc_initialize: segment %s records %Iu bytes, not %Iu", palloc->lock->nameprefix, header->total_size, size);
        result = FATAL_ALLOC_SEGMENT_CORRUPT;
        goto Finished;
    }

    if (*pinitmemory) {
        /* No lock needed: the other processes wait for the init mutex of the cache. The segment is */
        /* not initialized until the caller marks it (alloc_initdone): clear a mark that the file of */
        /* a file-backed segment can hold from an earlier cache */
        InterlockedExchange((volatile LONG*)&header->initdone, 0);

        /* Put the start block of size 0, then one free block of size free_size, */
        /* then the end block of size 0: the start and end blocks are never free, */
        /* so a free block is never combined past them */
        frestart = (alloc_free_header*)(header + 1);
        fremid = (alloc_free_header*)((char*)frestart + ALLOC_FREE_BLOCK_OVERHEAD);

        frestart->size = 0;
        frestart->is_free = BLOCK_ISFREE_FIRST;
        frestart->prev_free = 0;
        frestart->next_free = 0;
        *((size_t*)((char*)(frestart + 1))) = frestart->size;

        fremid->size = size - ALLOC_SEGMENT_HEADER_SIZE - ALLOC_FREE_BLOCK_OVERHEAD - ALLOC_FREE_BLOCK_OVERHEAD - ALLOC_FREE_BLOCK_OVERHEAD;
        freend = (alloc_free_header*)((char*)fremid + ALLOC_FREE_BLOCK_OVERHEAD + fremid->size);

        fremid->is_free = BLOCK_ISFREE_FREE;
        *((size_t*)(((char*)(fremid + 1)) + fremid->size)) = fremid->size;

        freend->size = 0;
        freend->is_free = BLOCK_ISFREE_LAST;
        freend->next_free = 0;
        freend->prev_free = 0;
        *((size_t*)((char*)(freend + 1))) = freend->size;

        /* Set segment header. Every field of alloc_segment_header, fields added later included, must */
        /* be set in this branch: the caller marks the segment after it (alloc_initdone), and the */
        /* processes that open it from then on use it as it is */
        header->total_size = size;
        header->free_size = fremid->size;

        header->cacheheader1 = 0;
        header->usedcount = 0;
        header->freecount = 1;
        header->last_owner = 0;
        header->undocount = 0;
        header->abandoned = 0;

        /* Empty free lists, then the free block in the list of its class (logged as in any update, */
        /* and committed: the log is empty when the segment is marked) */
        memset(header->freemap, 0, sizeof(header->freemap));
        memset(header->freelists, 0, sizeof(header->freelists));
        link_free_block(palloc, fremid, 0);
        lock_undo_commit(&palloc->undo);
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in alloc_initialize", result);

        if (palloc != NULL) {
            if (palloc->lock != NULL) {
                lock_terminate(palloc->lock);
                lock_destroy(palloc->lock);

                palloc->lock = NULL;
            }

            palloc->memaddr = NULL;
            palloc->size = 0;
            palloc->header = NULL;
        }
    }

    dprintverbose("end alloc_initialize");

    return result;
}

/* Marks the segment initialized, once the caller has also initialized what it keeps in it (the cache */
/* header): the processes that open the segment from then on use it as it is (see alloc_initialize). The */
/* interlocked write keeps the mark after every write of the initialization */
void alloc_initdone(alloc_context* palloc) {
    _ASSERT(palloc != NULL);
    _ASSERT(palloc->header != NULL);

    InterlockedExchange((volatile LONG*)&palloc->header->initdone, ALLOC_SEGMENT_INITDONE);
}

void alloc_terminate(alloc_context* palloc) {
    dprintverbose("start alloc_terminate");

    if (palloc != NULL) {
        palloc->header = NULL;

        if (palloc->lock != NULL) {
            lock_terminate(palloc->lock);
            lock_destroy(palloc->lock);

            palloc->lock = NULL;
        }

        palloc->memaddr = NULL;
        palloc->size = 0;
    }

    dprintverbose("end alloc_terminate");

    return;
}

/*
 * A memory pool, which holds every part of a stored array (see copyin_zval in ucache_zvcache.c) and is
 * freed whole (alloc_free_mpool).  With size > 0, the pool gets one chunk of that size at once, in the
 * block of its header: the caller counts what the copy will take, so that the parts of one array lie in
 * one block, without the rest of fixed-size chunks.  If no free block is that large, or size is 0, the
 * chunks come as the copy needs them (alloc_ommalloc), and alloc_trim_mpool() gives back what they do
 * not use.
 */
int alloc_create_mpool(alloc_context* palloc, size_t* phoffset, size_t size) {
    int                   result = NONFATAL;
    alloc_mpool_header*   header = NULL;
    alloc_mpool_segment*  pchunk = NULL;

    dprintverbose("start alloc_create_mpool");

    _ASSERT(palloc != NULL);
    _ASSERT(phoffset != NULL);

    *phoffset = 0;

#ifdef _WIN64
    size = ALIGNQWORD(size);
#else /* _WIN64 */
    size = ALIGNDWORD(size);
#endif /* _WIN64 */

    if (size > 0) {
        header = (alloc_mpool_header*)alloc_smalloc(palloc, sizeof(alloc_mpool_header) + sizeof(alloc_mpool_segment) + size);
        if (header == NULL) {
            size = 0;
        }
    }

    if (header == NULL) {
        header = (alloc_mpool_header*)alloc_smalloc(palloc, sizeof(alloc_mpool_header));
        if (header == NULL) {
            result = FATAL_OUT_OF_MEMORY;
            goto Finished;
        }
    }

    header->foffset = 0;
    header->loffset = 0;
    header->socurr = 0;
    header->mocurr = 0;
    header->locurr = 0;
    header->gocurr = 0;

    if (size > 0) {
        /* The chunk right after the header, its memory right after it, in the same block */
        pchunk = (alloc_mpool_segment*)(header + 1);
        pchunk->aoffset = POINTER_OFFSET(palloc->memaddr, (pchunk + 1));
        pchunk->size = size;
        pchunk->position = 0;
        pchunk->next = 0;

        header->foffset = POINTER_OFFSET(palloc->memaddr, pchunk);
        header->loffset = header->foffset;
        header->gocurr = header->foffset;
    }

    *phoffset = POINTER_OFFSET(palloc->memaddr, header);

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in alloc_create_mpool", result);
    }

    dprintverbose("end alloc_create_mpool");

    return result;
}

/*
 * Once the copy into a pool is done, nothing is allocated from it any more: gives back the end of every
 * chunk that the copy did not use (of the chunk in the header's block, if the size the caller counted was
 * too large, and of the last chunks of fixed size), when it is large enough to be a free block.
 */
void alloc_trim_mpool(alloc_context* palloc, size_t hoffset) {
    alloc_mpool_header*   header = NULL;
    alloc_mpool_segment*  pchunk = NULL;
    size_t                inner = 0;
    size_t                offset = 0;

    _ASSERT(palloc != NULL);
    _ASSERT(hoffset > 0);

    header = (alloc_mpool_header*)((char*)palloc->memaddr + hoffset);
    inner = hoffset + sizeof(alloc_mpool_header);

    for (offset = header->foffset; offset != 0; offset = pchunk->next) {
        pchunk = (alloc_mpool_segment*)((char*)palloc->memaddr + offset);
        if (pchunk->size - pchunk->position < MIN_FREE_BLOCK_SIZE) {
            continue;
        }

        if (offset == inner) {
            dprintimportant("alloc_trim_mpool: %Iu bytes of the %Iu counted were not used", pchunk->size - pchunk->position, pchunk->size);
            shrink_memory(palloc, header, sizeof(alloc_mpool_header) + sizeof(alloc_mpool_segment) + pchunk->position);
        } else {
            shrink_memory(palloc, (char*)palloc->memaddr + pchunk->aoffset, pchunk->position);
        }

        pchunk->size = pchunk->position;
    }

    header->socurr = 0;
    header->mocurr = 0;
    header->locurr = 0;
    header->gocurr = 0;
}

/*
 * Frees every chunk of the pool, then its header (with the chunk in its block, see alloc_create_mpool).
 * The pool is private: nothing else refers to it.  A chunk must not be touched once it is freed.
 */
void alloc_free_mpool(alloc_context* palloc, size_t hoffset) {
    alloc_mpool_header*   header = NULL;
    alloc_mpool_segment*  pchunk = NULL;
    size_t                inner = 0;
    size_t                offset = 0;
    size_t                next = 0;

    dprintverbose("start alloc_free_mpool");

    _ASSERT(palloc != NULL);
    _ASSERT(hoffset > 0);

    header = (alloc_mpool_header*)((char*)palloc->memaddr + hoffset);
    inner = hoffset + sizeof(alloc_mpool_header);

    for (offset = header->foffset; offset != 0; offset = next) {
        pchunk = (alloc_mpool_segment*)((char*)palloc->memaddr + offset);
        next = pchunk->next;

        if (offset == inner) {
            continue;
        }

        if (pchunk->aoffset != 0) {
            alloc_sfree(palloc, (char*)palloc->memaddr + pchunk->aoffset);
        }

        alloc_sfree(palloc, pchunk);
    }

    alloc_sfree(palloc, header);
    header = NULL;

    dprintverbose("end alloc_free_mpool");
    return;
}

void* alloc_get_cacheheader(alloc_context* palloc, unsigned int msize, unsigned int type) {
    void*                 pvoid = NULL;
    alloc_segment_header* header = NULL;
    size_t*               pcvalue = NULL;

    dprintverbose("start alloc_get_cacheheader");

    _ASSERT(palloc != NULL);
    _ASSERT(msize > 0);

    header = palloc->header;

    switch (type) {
        case CACHE_TYPE_USERZVALS:
            pcvalue = &header->cacheheader1;
            break;

        default:
            _ASSERT(FALSE);
            break;
    }

    if (*pcvalue != 0) {
        pvoid = (void*)((char*)palloc->memaddr + (*pcvalue));
        goto Finished;
    }

    if (FAILED(allocate_memory(palloc, msize, &pvoid))) {
        pvoid = NULL;
        goto Finished;
    }

    /* One write of an aligned offset: nothing to log. Without the lock (see alloc_lock), the block is lost */
    if (FAILED(alloc_lock(palloc))) {
        pvoid = NULL;
        goto Finished;
    }

    if (*pcvalue != 0) {
        /* Some other process allocated before this process could do */
        lock_unlock(palloc->lock);
        free_memory(palloc, pvoid);
        pvoid = (void*)((char*)palloc->memaddr + (*pcvalue));
    } else {
        *pcvalue = POINTER_OFFSET(palloc->memaddr, pvoid);
        lock_unlock(palloc->lock);
    }

Finished:

    dprintverbose("end alloc_get_cacheheader");
    return pvoid;
}

__inline
void* alloc_get_cachevalue(alloc_context* palloc, size_t offset) {
    _ASSERT(palloc != NULL);
    _ASSERT(palloc->memaddr != NULL);

    if (offset != 0 && offset < palloc->size) {
        return (void*)((char*)palloc->memaddr + offset);
    } else {
        return NULL;
    }
}

__inline
size_t alloc_get_valueoffset(alloc_context* palloc, void* pvalue) {
    _ASSERT(palloc != NULL);
    _ASSERT(palloc->memaddr != NULL);

    if (pvalue != NULL && pvalue >= palloc->memaddr) {
        return POINTER_OFFSET(palloc->memaddr, pvalue);
    } else {
        return 0;
    }
}

/*
 * Fills the caller's alloc_info (on its stack).  It must not allocate request memory: the
 * scavenger calls it while it holds the cross-process cache lock, and a memory_limit fatal error
 * there would leave the lock owned by a FastCGI process that survives the error, blocking every
 * other process of the pool.
 */
void alloc_getinfo(alloc_context* palloc, alloc_info* pinfo) {
    dprintverbose("start alloc_getinfo");

    _ASSERT(palloc != NULL);
    _ASSERT(pinfo != NULL);

    /* Without the lock (see alloc_lock), only the size, which never changes, and the count of the */
    /* abandoned locks (one aligned field) are known. Reported all free: no scavenger step or full pass */
    /* then removes entries for memory that the allocator could not take back */
    if (palloc->held == 0 && FAILED(alloc_lock(palloc))) {
        pinfo->total_size = palloc->size;
        pinfo->free_size = palloc->size;
        pinfo->usedcount = 0;
        pinfo->freecount = 0;
        pinfo->mem_overhead = 0;
        pinfo->abandoned = palloc->header->abandoned;
        goto Finished;
    }

    pinfo->total_size = palloc->header->total_size;
    pinfo->free_size = palloc->header->free_size;
    pinfo->usedcount = palloc->header->usedcount;
    pinfo->freecount = palloc->header->freecount;
    pinfo->mem_overhead = (palloc->header->usedcount + palloc->header->freecount + 2) * sizeof(alloc_free_header);
    pinfo->abandoned = palloc->header->abandoned;

    if (palloc->held == 0) {
        lock_unlock(palloc->lock);
    }

Finished:

    dprintverbose("end alloc_getinfo");

    return;
}

/*
 * The most the segment can hold: the free memory once every block but the cache header is freed.  The
 * segment is then one free block between the first and the last block (see alloc_initialize), the cache
 * header being the first block that allocate_memory() took (alloc_get_cacheheader), at the start of
 * that block.  No allocation can be larger, nor all of them together.  0 before the cache header exists.
 */
size_t alloc_capacity(alloc_context* palloc) {
    size_t              capacity = 0;
    alloc_used_header*  usedh = NULL;

    _ASSERT(palloc != NULL);

    /* Without the lock (see alloc_lock): not known */
    if (palloc->held == 0 && FAILED(alloc_lock(palloc))) {
        return 0;
    }

    if (palloc->header->cacheheader1 != 0) {
        usedh = USED_HEADER(palloc->memaddr, palloc->header->cacheheader1 - sizeof(alloc_used_header));
        capacity = palloc->header->total_size - ALLOC_SEGMENT_HEADER_SIZE - 3 * ALLOC_FREE_BLOCK_OVERHEAD
            - (usedh->size + ALLOC_USED_BLOCK_OVERHEAD);
    }

    if (palloc->held == 0) {
        lock_unlock(palloc->lock);
    }

    return capacity;
}

__inline
void* alloc_emalloc(size_t size) {
    return alloc_malloc(NULL, ALLOC_TYPE_PROCESS, size);
}

__inline
void* alloc_pemalloc(size_t size) {
    return alloc_malloc(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, size);
}

__inline
void* alloc_smalloc(alloc_context* palloc, size_t size) {
    _ASSERT(palloc != NULL);
    return alloc_malloc(palloc, ALLOC_TYPE_SHAREDMEM, size);
}

__inline
char* alloc_estrdup(const char* str) {
    return alloc_strdup(NULL, ALLOC_TYPE_PROCESS, str);
}

__inline
void alloc_efree(void* addr) {
    alloc_free(NULL, ALLOC_TYPE_PROCESS, addr);
    return;
}

__inline
void alloc_pefree(void* addr) {
    alloc_free(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, addr);
    return;
}

__inline
void alloc_sfree(alloc_context* palloc, void* addr) {
    _ASSERT(palloc != NULL);
    alloc_free(palloc, ALLOC_TYPE_SHAREDMEM, addr);
    return;
}

void* alloc_oemalloc(alloc_context* palloc, size_t hoffset, size_t size) {
    _ASSERT(palloc == NULL);
    _ASSERT(hoffset == 0);

    return alloc_emalloc(size);
}

void* alloc_osmalloc(alloc_context* palloc, size_t hoffset, size_t size) {
    _ASSERT(palloc != NULL);
    _ASSERT(hoffset == 0);

    return alloc_smalloc(palloc, size);
}

void* alloc_ommalloc(alloc_context* palloc, size_t hoffset, size_t size) {
    alloc_mpool_header*  header = NULL;
    alloc_mpool_segment* pchunk = NULL;
    alloc_mpool_segment* ptemp = NULL;

    void*                 addr = NULL;
    void*                 pvoid = NULL;
    size_t                offset = 0;
    size_t                rsize = 0;
    size_t                bsize = 0;
    unsigned char         ctype = 0;

    dprintdecorate("start alloc_ommalloc");

    _ASSERT(palloc != NULL);
    _ASSERT(hoffset > 0);
    _ASSERT(size > 0);

#ifdef _WIN64
    rsize = ALIGNQWORD(size);
#else /* _WIN64 */
    rsize = ALIGNDWORD(size);
#endif /* _WIN64 */
    header = (alloc_mpool_header*)((char*)palloc->memaddr + hoffset);

    /* The chunk of the size the caller counted first (see alloc_create_mpool), while it has room */
    if (header->gocurr != 0) {
        ptemp = (alloc_mpool_segment*)((char*)palloc->memaddr + header->gocurr);
        if (ptemp->size - ptemp->position >= rsize) {
            addr = (void*)((char*)palloc->memaddr + ptemp->aoffset + ptemp->position);
            ptemp->position += rsize;
            goto Finished;
        }

        dprintimportant("alloc_ommalloc: %Iu bytes do not fit in the %Iu counted (%Iu used)", rsize, ptemp->size, ptemp->position);
        ptemp = NULL;
    }

    if (rsize <= POOL_ALLOCATION_SMALL_MAX) {
        offset = header->socurr;
        bsize = POOL_ALLOCATION_SMALL_BSIZE;
        ctype = POOL_ALLOCATION_TYPE_SMALL;
    } else if (rsize <= POOL_ALLOCATION_MEDIUM_MAX) {
        offset = header->mocurr;
        bsize = POOL_ALLOCATION_MEDIUM_BSIZE;
        ctype = POOL_ALLOCATION_TYPE_MEDIUM;
    } else if (rsize <= POOL_ALLOCATION_LARGE_MAX) {
        offset = header->locurr;
        bsize = POOL_ALLOCATION_LARGE_BSIZE;
        ctype = POOL_ALLOCATION_TYPE_LARGE;
    } else {
        offset = 0;
        bsize = rsize;
        ctype = POOL_ALLOCATION_TYPE_HUGE;
    }

    /* Check if chunk has enough space for this allocation */
    /* Else we need to allocate a new chunk and use that */
    if (offset > 0) {
        pchunk = (alloc_mpool_segment*)((char*)palloc->memaddr + offset);
        if (pchunk->size - pchunk->position < rsize) {
            offset = 0;
            pchunk = NULL;
        }
    }

    if (offset == 0) {
        pchunk = (alloc_mpool_segment*)alloc_smalloc(palloc, sizeof(alloc_mpool_segment));
        if (pchunk == NULL) {
            goto Finished;
        }

        /* Initialize pchunk */
        pchunk->aoffset = 0;
        pchunk->size = 0;
        pchunk->position = 0;
        pchunk->next = 0;

        pvoid = alloc_smalloc(palloc, bsize);
        if (pvoid == NULL) {
            goto Finished;
        }

        /* Update aoffset to offset of allocated memory */
        /* Also update size after allocating block */
        pchunk->aoffset = POINTER_OFFSET(palloc->memaddr, pvoid);
        pchunk->size = bsize;

        offset = POINTER_OFFSET(palloc->memaddr, pchunk);
        if (header->foffset == 0) {
            _ASSERT(header->loffset == 0);

            header->foffset = offset;
            header->loffset = offset;
        } else {
            ptemp = (alloc_mpool_segment*)((char*)palloc->memaddr + header->loffset);
            ptemp->next = offset;
            header->loffset = offset;
        }

        switch (ctype) {
            case POOL_ALLOCATION_TYPE_SMALL:
                header->socurr = offset;
                break;
            case POOL_ALLOCATION_TYPE_MEDIUM:
                header->mocurr = offset;
                break;
            case POOL_ALLOCATION_TYPE_LARGE:
                header->locurr = offset;
                break;
        }
    } else {
        pchunk = (alloc_mpool_segment*)((char*)palloc->memaddr + offset);
    }

    _ASSERT(pchunk != NULL);
    _ASSERT(pchunk->size - pchunk->position >= rsize);

    addr = (void*)((char*)palloc->memaddr + pchunk->aoffset + pchunk->position);
    pchunk->position += rsize;

    pchunk = NULL;

Finished:

    if (pchunk != NULL) {
        if (pchunk->aoffset != 0) {
            ptemp = (alloc_mpool_segment*)((char*)palloc->memaddr + pchunk->aoffset);
            alloc_sfree(palloc, ptemp);
            pchunk->aoffset = 0;
        }

        alloc_sfree(palloc, pchunk);
        pchunk = NULL;
    }

    dprintdecorate("end alloc_ommalloc");
    return addr;
}

void alloc_oefree(alloc_context* palloc, size_t hoffset, void* addr) {
    _ASSERT(palloc == NULL);
    _ASSERT(hoffset == 0);

    alloc_efree(addr);
    return;
}

void alloc_osfree(alloc_context* palloc, size_t hoffset, void* addr) {
    _ASSERT(palloc != NULL);
    _ASSERT(hoffset == 0);

    alloc_sfree(palloc, addr);
    return;
}

void alloc_omfree(alloc_context* palloc, size_t hoffset, void* addr) {
    /* Nothing to do. We will free memory in the pool in one go */
    return;
}
