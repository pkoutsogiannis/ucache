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
#define MIN_FREE_BLOCK_SIZE            (ALLOC_FREE_BLOCK_OVERHEAD + 4)

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

/* Globals */
unsigned short gallocid = 1;

/* Private methods */
static int    allocate_memory(alloc_context * palloc, size_t size, void ** ppaddr);
static alloc_free_header * find_next_free(alloc_context * palloc, alloc_used_header * usedh);
static void   free_memory(alloc_context * palloc, void * paddr);

static void * alloc_malloc(alloc_context * palloc, unsigned int type, size_t size);
static char * alloc_strdup(alloc_context * palloc, unsigned int type, const char * str);
static void   alloc_free(alloc_context * palloc, unsigned int type, void * addr);

static int allocate_memory(alloc_context * palloc, size_t size, void ** ppaddr)
{
    int                    result  = NONFATAL;
    void *                 paddr   = NULL;
    void *                 segaddr = NULL;
    unsigned char          flock   = 0;

    size_t                 oldsize = 0;
    size_t                 oldprev = 0;
    size_t                 oldnext = 0;

    alloc_segment_header * header  = NULL;
    alloc_free_header *    freeh   = NULL;
    alloc_free_header *    pfree   = NULL;
    alloc_used_header *    usedh   = NULL;

    dprintdecorate("start allocate_memory");

    _ASSERT(palloc         != NULL);
    _ASSERT(palloc->lock   != NULL);
    _ASSERT(palloc->header != NULL);
    _ASSERT(size           >= 0);

    /* Allocate memory from memory segment shared with other processes */
    segaddr = palloc->memaddr;
    header  = palloc->header;

    /* Allocate in sizes which are multiples of size_t */
#ifdef _WIN64
    size = ALIGNQWORD(size);
#else /* _WIN64 */
    size = ALIGNDWORD(size);
#endif /* _WIN64 */

    /* Acquire lock to segment before doing anything */
    lock_lock(palloc->lock);
    flock = 1;

    /* If available size is less than requested size, return NULL */
    if(header->free_size < size)
    {
        result = FATAL_ALLOC_NO_MEMORY;
        goto Finished;
    }

    /* Get pointer to start block and move to next block */
    freeh = (alloc_free_header *)(header + 1);
    _ASSERT(freeh->is_free == BLOCK_ISFREE_FIRST);
    if (freeh->is_free != BLOCK_ISFREE_FIRST)
    {
        dprintcritical("allocate_memory: First block is not BLOCK_ISFREE_FIRST");
        result = FATAL_ALLOC_SEGMENT_CORRUPT;

        goto Finished;
    }

    freeh = FREE_HEADER(segaddr, freeh->next_free);
    _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE || freeh->is_free == BLOCK_ISFREE_LAST);
    if (freeh->is_free != BLOCK_ISFREE_FREE && freeh->is_free != BLOCK_ISFREE_LAST)
    {
        dprintcritical("allocate_memory: Free block is not BLOCK_ISFREE_FREE or BLOCK_ISFREE_LAST");
        result = FATAL_ALLOC_SEGMENT_CORRUPT;

        goto Finished;
    }

    /* Find a free block which is large enough to satisfy the request */
    while(freeh->is_free != BLOCK_ISFREE_LAST)
    {
        _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE);
        if (freeh->is_free != BLOCK_ISFREE_FREE)
        {
            dprintcritical("allocate_memory: Free block is not BLOCK_ISFREE_FREE");
            result = FATAL_ALLOC_SEGMENT_CORRUPT;

            goto Finished;
        }

        if(freeh->size >= size)
        {
            break;
        }

        freeh = FREE_HEADER(segaddr, freeh->next_free);
    }

    /* If we didn't find a block big enough, return error */
    if(freeh->is_free == BLOCK_ISFREE_LAST)
    {
        result = FATAL_ALLOC_NO_MEMORY;
        goto Finished;
    }

    _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE);

    /* Got a free block with sufficient free memory. Now */
    /* see if block size is big enough to be broken into two */
    if(freeh->size >= size + MIN_FREE_BLOCK_SIZE)
    {
        oldsize = freeh->size;
        oldprev = freeh->prev_free;
        oldnext = freeh->next_free;

        usedh = (alloc_used_header *)freeh;
        usedh->size    = size;
        usedh->is_free = BLOCK_ISFREE_USED;
        usedh->dummy1  = 0;
        usedh->dummy2  = 0;

        /* Return pointer right after the used header */
        paddr = (void *)(usedh + 1);

        /* Put the size of allocated block in the end */
        *((size_t *)((char *)paddr + size)) = size;

        /* Create a new free header after the allocated block */
        freeh = (alloc_free_header *)((char *)usedh + size + ALLOC_USED_BLOCK_OVERHEAD);
        freeh->size      = oldsize - size - ALLOC_USED_BLOCK_OVERHEAD;
        freeh->is_free   = BLOCK_ISFREE_FREE;
        freeh->prev_free = oldprev;
        freeh->next_free = oldnext;
        *((size_t *)(((char *)(freeh + 1))+ freeh->size)) = freeh->size;

        pfree = FREE_HEADER(segaddr, freeh->prev_free);
        pfree->next_free = POINTER_OFFSET(segaddr, freeh);
        pfree = FREE_HEADER(segaddr, freeh->next_free);
        pfree->prev_free = POINTER_OFFSET(segaddr, freeh);

        /* Update free_size in segment header */
        header->free_size  -= (size + ALLOC_USED_BLOCK_OVERHEAD);

        /* Number of free blocks are not decreasing in this case */
        header->usedcount++;
    }
    else
    {
        /* Block is very small and should be used completely */
        pfree = FREE_HEADER(segaddr, freeh->prev_free);
        pfree->next_free = freeh->next_free;

        pfree = FREE_HEADER(segaddr, freeh->next_free);
        pfree->prev_free = freeh->prev_free;

        /* Update size to indicate number of bytes we are actually allocating */
        size = freeh->size;

        /* Set used header and last two size_t bytes */
        usedh = (alloc_used_header *)freeh;
        usedh->size    = size;
        usedh->is_free = BLOCK_ISFREE_USED;
        usedh->dummy1  = 0;
        usedh->dummy2  = 0;

        /* Return pointer right after the used header */
        paddr = (void *)(usedh + 1);

        /* Put the size of allocated block in the end */
        *((size_t *)((char *)paddr + size)) = size;

        header->free_size  -= size;

        /* One free block got converted to used block */
        header->freecount--;
        header->usedcount++;
    }

    *ppaddr = paddr;

Finished:

    if(flock)
    {
        lock_unlock(palloc->lock);
        flock = 0;
    }

    if(FAILED(result))
    {
        _ASSERT(*ppaddr == NULL);

        dprintverbose("failure %d in allocate_memory", result);
    }

    dprintdecorate("end allocate_memory");

    return result;
}

/*
 * The free list is kept in address order (free_memory relies on it to combine a block with the
 * free blocks on both sides of it, and so do the other processes of the pool, which may run an
 * older build).  So a block that is freed between two used blocks goes in before the first free
 * block that follows it, the last block if there is none.  Three walks find that block, one step
 * each in turn, and the first to get there answers: forward over the used blocks after it, which
 * is short where free blocks are many (they are then close together), and along the free list from
 * the first block or back from the last block, which is short where free blocks are few or the
 * freed block is near an end of the segment.  WinCache walked forward only, which took time in
 * proportion to the number of entries: a delete took 134 us with 10000 entries and deleting
 * 100000 one by one took minutes.  The last block is found from total_size, as alloc_initialize
 * placed it; if it does not carry its marker, the walk back is left out.
 */
static alloc_free_header * find_next_free(alloc_context * palloc, alloc_used_header * usedh)
{
    void *              segaddr = palloc->memaddr;
    size_t              offset  = (size_t)POINTER_OFFSET(segaddr, usedh);
    alloc_free_header * pnext   = NULL;
    alloc_free_header * pahead  = NULL;
    alloc_free_header * pbehind = NULL;

    /* Forward over the used blocks after it */
    pnext = (alloc_free_header *)((char *)usedh + usedh->size + ALLOC_USED_BLOCK_OVERHEAD);

    /* Along the free list from the first block, and back from the last block */
    pahead  = FREE_HEADER(segaddr, ((alloc_free_header *)(palloc->header + 1))->next_free);
    pbehind = FREE_HEADER(segaddr, palloc->header->total_size - ALLOC_FREE_BLOCK_OVERHEAD);
    pbehind = (pbehind->is_free == BLOCK_ISFREE_LAST) ? FREE_HEADER(segaddr, pbehind->prev_free) : NULL;

    while(1)
    {
        if(pnext->is_free != BLOCK_ISFREE_USED)
        {
            return pnext;
        }
        pnext = (alloc_free_header *)((char *)pnext + pnext->size + ALLOC_USED_BLOCK_OVERHEAD);

        /* The first free block after it, or the last block */
        if(pahead->is_free != BLOCK_ISFREE_FREE || (size_t)POINTER_OFFSET(segaddr, pahead) > offset)
        {
            return pahead;
        }
        pahead = FREE_HEADER(segaddr, pahead->next_free);

        /* The last free block before it, or the first block: the block after that one */
        if(pbehind != NULL)
        {
            if(pbehind->is_free != BLOCK_ISFREE_FREE || (size_t)POINTER_OFFSET(segaddr, pbehind) < offset)
            {
                return FREE_HEADER(segaddr, pbehind->next_free);
            }
            pbehind = FREE_HEADER(segaddr, pbehind->prev_free);
        }
    }
}

static void free_memory(alloc_context * palloc, void * paddr)
{
    int             combined = 0;
    size_t          blocksiz = 0;
    size_t          lastsize = 0;
    size_t          coffset  = 0;
    void *          segaddr  = NULL;
    unsigned char   flock    = 0;

    alloc_segment_header * header = NULL;
    alloc_used_header *    usedh  = NULL;
    alloc_free_header *    freeh  = NULL;
    alloc_free_header *    pfree  = NULL;

    dprintdecorate("start free_memory");

    _ASSERT(palloc != NULL);
    _ASSERT(paddr  != NULL);

    /* Free the block from shared segment */
    lock_lock(palloc->lock);
    flock = 1;

    header  = palloc->header;
    segaddr = palloc->memaddr;

    /* Get pointer to used_header for the block which is getting freed */
    usedh = (alloc_used_header *)((char *)paddr - sizeof(alloc_used_header));

    /* Validate the memory being freed is in the heap */
    if ((void *)usedh < segaddr || (char *)usedh > ((char *)segaddr + palloc->size))
    {
        _ASSERT(0);
        dprintcritical("free_memory: Address %p not in segment %p (size %d)",
            paddr, segaddr, palloc->size);

        goto Finished;
    }

    /* Block is in our heap.  Validate it is in use */
    _ASSERT(usedh->is_free == BLOCK_ISFREE_USED);
    if (usedh->is_free != BLOCK_ISFREE_USED)
    {
        dprintcritical("free_memory: Address %p not in use in segment %p (size %d)",
            paddr, segaddr, palloc->size);

        goto Finished;
    }

    /* Increment freecount and decrement usedcount right away */
    header->usedcount--;
    header->freecount++;

    blocksiz = usedh->size;
    header->free_size += blocksiz;

    /* Check if next block is free */
    freeh = (alloc_free_header *)((char *)usedh + blocksiz + ALLOC_USED_BLOCK_OVERHEAD);
    if(freeh->is_free == BLOCK_ISFREE_FREE)
    {
        /* Block after this block is free. Need to combine */
        pfree = (alloc_free_header *)usedh;

        pfree->size      = freeh->size + ALLOC_FREE_BLOCK_OVERHEAD + blocksiz;
        pfree->is_free   = BLOCK_ISFREE_FREE;
        pfree->prev_free = freeh->prev_free;
        pfree->next_free = freeh->next_free;

        coffset = POINTER_OFFSET(segaddr, pfree);
        (FREE_HEADER(segaddr, pfree->prev_free))->next_free = coffset;
        (FREE_HEADER(segaddr, pfree->next_free))->prev_free = coffset;
        *((size_t *)(((char *)(pfree + 1)) + pfree->size)) = pfree->size;

        header->freecount--;
        header->free_size  += ALLOC_FREE_BLOCK_OVERHEAD;

        combined = 1;
    }

    /* Check if previous block is free */
    lastsize = *((size_t *)((char *)paddr - sizeof(alloc_used_header) - sizeof(size_t)));
    freeh    = (alloc_free_header *)((char *)paddr - sizeof(alloc_used_header) - ALLOC_FREE_BLOCK_OVERHEAD - lastsize);

    _ASSERT(freeh->size == lastsize);
    if(freeh->is_free == BLOCK_ISFREE_FREE)
    {
        /* Block before block getting freed is free. Combine with that */
        if(combined == 0)
        {
            /* Just need to expand the size of this block */
            freeh->size += usedh->size + ALLOC_USED_BLOCK_OVERHEAD;
        }
        else
        {
            /* We already combined next block with block getting freed */
            pfree = (alloc_free_header *)usedh;
            freeh->size += pfree->size + ALLOC_FREE_BLOCK_OVERHEAD;

            freeh->next_free = pfree->next_free;
            pfree = FREE_HEADER(segaddr, pfree->next_free);
            pfree->prev_free = POINTER_OFFSET(segaddr, freeh);
        }

        *((size_t *)(((char *)(freeh + 1)) + freeh->size)) = freeh->size;

        header->freecount--;
        header->free_size  += ALLOC_FREE_BLOCK_OVERHEAD;

        combined = 1;
    }

    if(combined == 0)
    {
        /* New free block around two used blocks or around */
        /* used and first block or around used and last block: */
        /* it goes in before the free block that follows it (see find_next_free) */
        usedh = (alloc_used_header *)((char *)paddr - sizeof(alloc_used_header));
        freeh = find_next_free(palloc, usedh);

        _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE || freeh->is_free == BLOCK_ISFREE_LAST);
        if (freeh->is_free != BLOCK_ISFREE_FREE && freeh->is_free != BLOCK_ISFREE_LAST)
        {
            dprintcritical("free_memory: Free block %p is not free in segment %p (size %d) - CORRUPTION IMMINENT",
                freeh, segaddr, palloc->size);
        }

        pfree = (alloc_free_header *)usedh;
        pfree->is_free = BLOCK_ISFREE_FREE;
        pfree->next_free = POINTER_OFFSET(segaddr, freeh);
        pfree->prev_free = freeh->prev_free;

        freeh = FREE_HEADER(segaddr, pfree->prev_free);
        freeh->next_free = POINTER_OFFSET(segaddr, pfree);

        freeh = FREE_HEADER(segaddr, pfree->next_free);
        freeh->prev_free = POINTER_OFFSET(segaddr, pfree);
    }

Finished:

    if(flock)
    {
        lock_unlock(palloc->lock);
        flock = 0;
    }

    dprintdecorate("end free_memory");

    return;
}

static void * alloc_malloc(alloc_context * palloc, unsigned int type, size_t size)
{
    void * result = NULL;

    _ASSERT(size >= 0);

    switch(type)
    {
        case ALLOC_TYPE_SHAREDMEM:
            if(FAILED(allocate_memory(palloc, size, &result)))
            {
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

static char * alloc_strdup(alloc_context * palloc, unsigned int type, const char * str)
{
    char * result = NULL;
    size_t strl   = 0;

    _ASSERT(str != NULL);
    strl = strlen(str) + 1;

    switch(type)
    {
        case ALLOC_TYPE_SHAREDMEM:
            if(FAILED(allocate_memory(palloc, strl, &result)))
            {
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

static void alloc_free(alloc_context * palloc, unsigned int type, void * addr)
{
    _ASSERT(addr != NULL);

    switch(type)
    {
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

int alloc_create(alloc_context ** ppalloc)
{
    int             result = NONFATAL;
    alloc_context * palloc = NULL;

    dprintverbose("start alloc_create");
    _ASSERT(ppalloc != NULL);

    *ppalloc = NULL;

    /* Allocate memory for context */
    palloc = (alloc_context *)alloc_pemalloc(sizeof(alloc_context));
    if(palloc == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    /* Initialize member variables */
    palloc->id        = gallocid++;
    palloc->memaddr   = NULL;
    palloc->size      = 0;
    palloc->lock      = NULL;
    palloc->header    = NULL;

    *ppalloc = palloc;

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in alloc_create", result);
    }

    dprintverbose("end alloc_create");
    return result;
}

void alloc_destroy(alloc_context * palloc)
{
    dprintverbose("start alloc_destroy");

    if(palloc != NULL)
    {
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
int alloc_initialize(alloc_context * palloc, char * name, unsigned short cachekey, void * staddr, size_t size, unsigned char * pinitmemory)
{
    int                    result   = NONFATAL;
    alloc_segment_header * header   = NULL;
    alloc_free_header *    frestart = NULL;
    alloc_free_header *    fremid   = NULL;
    alloc_free_header *    freend   = NULL;

    dprintverbose("start alloc_initialize");

    _ASSERT(palloc      != NULL);
    _ASSERT(cachekey    != 0);
    _ASSERT(staddr      != NULL);
    _ASSERT(size        >  0);
    _ASSERT(pinitmemory != NULL);

    palloc->memaddr   = staddr;
    palloc->size      = size;
    palloc->header    = (alloc_segment_header *)staddr;
    header            = palloc->header;

    result = lock_create(&palloc->lock);
    if(FAILED(result))
    {
        goto Finished;
    }

    result = lock_initialize(palloc->lock, name, cachekey, LOCK_TYPE_SHARED, &header->last_owner);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Opened, but the process that initialized it died before it marked it */
    if(*pinitmemory == 0 && header->initdone != ALLOC_SEGMENT_INITDONE)
    {
        dprintimportant("alloc_initialize: the initialization of segment %s was not completed: initializing it again", palloc->lock->nameprefix);
        *pinitmemory = 1;
    }

    /* A safety net: the caller maps a segment it opens at the size it has (filemap_initialize), which */
    /* the process that initialized it recorded. No cache is better than a free list past the view */
    if(*pinitmemory == 0 && header->total_size != size)
    {
        dprintimportant("alloc_initialize: segment %s records %Iu bytes, not %Iu", palloc->lock->nameprefix, header->total_size, size);
        result = FATAL_ALLOC_SEGMENT_CORRUPT;
        goto Finished;
    }

    if(*pinitmemory)
    {
        /* No lock needed: the other processes wait for the init mutex of the cache. The segment is */
        /* not initialized until the caller marks it (alloc_initdone): clear a mark that the file of */
        /* a file-backed segment can hold from an earlier cache */
        InterlockedExchange((volatile LONG *)&header->initdone, 0);

        /* Put the start header of size 0 */
        /* followed by the first free block of size free_size */
        frestart = (alloc_free_header *)(header + 1);
        fremid   = (alloc_free_header *)((char *)frestart + ALLOC_FREE_BLOCK_OVERHEAD);

        /* Set start of doubly circular linked list */
        frestart->size      = 0;
        frestart->is_free   = BLOCK_ISFREE_FIRST;
        frestart->prev_free = 0;
        frestart->next_free = POINTER_OFFSET(staddr, fremid);
        *((size_t *)((char *)(frestart + 1))) = frestart->size;

        /* Set mid of doubly linked list */
        fremid->size      = size - ALLOC_SEGMENT_HEADER_SIZE - ALLOC_FREE_BLOCK_OVERHEAD - ALLOC_FREE_BLOCK_OVERHEAD - ALLOC_FREE_BLOCK_OVERHEAD;
        freend   = (alloc_free_header *)((char *)fremid + ALLOC_FREE_BLOCK_OVERHEAD + fremid->size);

        fremid->is_free   = BLOCK_ISFREE_FREE;
        fremid->prev_free = POINTER_OFFSET(staddr, frestart);
        fremid->next_free = POINTER_OFFSET(staddr, freend);
        *((size_t *)(((char *)(fremid + 1)) + fremid->size)) = fremid->size;

        freend->size      = 0;
        freend->is_free   = BLOCK_ISFREE_LAST;
        freend->next_free = 0;
        freend->prev_free = POINTER_OFFSET(staddr, fremid);
        *((size_t *)((char *)(freend + 1))) = freend->size;

        /* Set segment header. Every field of alloc_segment_header, fields added later included, must */
        /* be set in this branch: the caller marks the segment after it (alloc_initdone), and the */
        /* processes that open it from then on use it as it is */
        header->total_size = size;
        header->free_size  = fremid->size;

        header->cacheheader1 = 0;
        header->usedcount    = 0;
        header->freecount    = 1;
        header->last_owner   = 0;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in alloc_initialize", result);

        if(palloc != NULL)
        {
            if(palloc->lock != NULL)
            {
                lock_terminate(palloc->lock);
                lock_destroy(palloc->lock);

                palloc->lock = NULL;
            }

            palloc->memaddr = NULL;
            palloc->size    = 0;
            palloc->header  = NULL;
        }
    }

    dprintverbose("end alloc_initialize");

    return result;
}

/* Marks the segment initialized, once the caller has also initialized what it keeps in it (the cache */
/* header): the processes that open the segment from then on use it as it is (see alloc_initialize). The */
/* interlocked write keeps the mark after every write of the initialization */
void alloc_initdone(alloc_context * palloc)
{
    _ASSERT(palloc         != NULL);
    _ASSERT(palloc->header != NULL);

    InterlockedExchange((volatile LONG *)&palloc->header->initdone, ALLOC_SEGMENT_INITDONE);
}

void alloc_terminate(alloc_context * palloc)
{
    dprintverbose("start alloc_terminate");

    if(palloc != NULL)
    {
        palloc->header = NULL;

        if(palloc->lock != NULL)
        {
            lock_terminate(palloc->lock);
            lock_destroy(palloc->lock);

            palloc->lock = NULL;
        }

        palloc->memaddr = NULL;
        palloc->size    = 0;
    }

    dprintverbose("end alloc_terminate");

    return;
}

int alloc_create_mpool(alloc_context * palloc, size_t * phoffset)
{
    int                   result = NONFATAL;
    alloc_mpool_header *  header = NULL;

    dprintverbose("start alloc_create_mpool");

    _ASSERT(palloc   != NULL);
    _ASSERT(phoffset != NULL);

    *phoffset = 0;

    header = (alloc_mpool_header *)alloc_smalloc(palloc, sizeof(alloc_mpool_header));
    if(header == NULL)
    {
        result = FATAL_OUT_OF_MEMORY;
        goto Finished;
    }

    header->foffset = 0;
    header->loffset = 0;
    header->socurr  = 0;
    header->mocurr  = 0;
    header->locurr  = 0;

    *phoffset = POINTER_OFFSET(palloc->memaddr, header);

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in alloc_create_mpool", result);
    }

    dprintverbose("end alloc_create_mpool");

    return result;
}

void alloc_free_mpool(alloc_context * palloc, size_t hoffset)
{
    void *                pvoid  = NULL;
    alloc_mpool_header *  header = NULL;
    alloc_mpool_segment * pchunk = NULL;
    alloc_mpool_segment * ptemp  = NULL;
    size_t                offset = 0;
    size_t                prev   = 0;
    size_t                next   = 0;

    dprintverbose("start alloc_free_mpool");

    _ASSERT(palloc  != NULL);
    _ASSERT(hoffset >  0);

    header = (alloc_mpool_header *)((char *)palloc->memaddr + hoffset);
    if(header->foffset > 0)
    {
        /*
         * free_memory() merges a freed block with a free neighbour in O(1), but a block freed
         * between two used blocks makes it look for its place in the free list (find_next_free,
         * which can walk the used blocks after it).  alloc_ommalloc() allocates the chunks one after
         * the other, each segment header before its data block, so a pool's blocks mostly lie in
         * increasing order of address: side by side ([header 1][data 1][header 2][data 2]...), or,
         * when the free list is fragmented, the small headers in holes and the data blocks one
         * after another.  So free the chunks from the last to the first, each data block before
         * its header: the block after the one being freed is then usually free already, and only
         * a block followed by something else makes free_memory() walk.  In the order of the list,
         * with WinCache's forward walk, every data block walked over the rest of the pool, which
         * made freeing a pool quadratic in its number of chunks (minutes for a large entry, while
         * the cache lock was held).
         * Freeing each header before its data block, in the order of the list, is linear only
         * while every data block directly follows its header: a header in a hole of its own walks
         * past its data block, still in use, to the next free block, which slowed the frees of
         * small entries down in a fragmented segment.  The pool is private and about to be
         * freed: reverse its list in place first.  A chunk must not be touched once it is freed.
         */
        offset = header->foffset;
        while(offset != 0)
        {
            pchunk       = (alloc_mpool_segment *)((char *)palloc->memaddr + offset);
            next         = pchunk->next;
            pchunk->next = prev;
            prev         = offset;
            offset       = next;
        }

        pchunk = (alloc_mpool_segment *)((char *)palloc->memaddr + prev);
        while(pchunk != NULL)
        {
            ptemp  = pchunk;
            pchunk = ((ptemp->next == 0) ? NULL : (alloc_mpool_segment *)((char *)palloc->memaddr + ptemp->next));

            if(ptemp->aoffset != 0)
            {
                pvoid = (void *)((char *)palloc->memaddr + ptemp->aoffset);

                alloc_sfree(palloc, pvoid);
                pvoid = NULL;
            }

            alloc_sfree(palloc, ptemp);
            ptemp = NULL;
        }
    }

    alloc_sfree(palloc, header);
    header = NULL;

    dprintverbose("end alloc_free_mpool");
    return;
}

void * alloc_get_cacheheader(alloc_context * palloc, unsigned int msize, unsigned int type)
{
    void *                 pvoid   = NULL;
    alloc_segment_header * header  = NULL;
    size_t *               pcvalue = NULL;

    dprintverbose("start alloc_get_cacheheader");

    _ASSERT(palloc != NULL);
    _ASSERT(msize  >  0);

    header = palloc->header;

    switch(type)
    {
        case CACHE_TYPE_USERZVALS:
            pcvalue = &header->cacheheader1;
            break;

        default:
            _ASSERT(FALSE);
            break;
    }

    if(*pcvalue != 0)
    {
        pvoid = (void *)((char *)palloc->memaddr + (*pcvalue));
        goto Finished;
    }

    if(FAILED(allocate_memory(palloc, msize, &pvoid)))
    {
        pvoid = NULL;
        goto Finished;
    }

    lock_lock(palloc->lock);
    if(*pcvalue != 0)
    {
        /* Some other process allocated before this process could do */
        lock_unlock(palloc->lock);
        free_memory(palloc, pvoid);
        pvoid = (void *)((char *)palloc->memaddr + (*pcvalue));
    }
    else
    {
        *pcvalue = POINTER_OFFSET(palloc->memaddr, pvoid);
        lock_unlock(palloc->lock);
    }

Finished:

    dprintverbose("end alloc_get_cacheheader");
    return pvoid;
}

__inline
void * alloc_get_cachevalue(alloc_context * palloc, size_t offset)
{
    _ASSERT(palloc != NULL);
    _ASSERT(palloc->memaddr != NULL);

    if(offset != 0 && offset < palloc->size)
    {
        return (void *)((char *)palloc->memaddr + offset);
    }
    else
    {
        return NULL;
    }
}

__inline
size_t alloc_get_valueoffset(alloc_context * palloc, void * pvalue)
{
    _ASSERT(palloc != NULL);
    _ASSERT(palloc->memaddr != NULL);

    if(pvalue != NULL && pvalue >= palloc->memaddr)
    {
        return POINTER_OFFSET(palloc->memaddr, pvalue);
    }
    else
    {
        return 0;
    }
}

/*
 * Fills the caller's alloc_info (on its stack).  It must not allocate request memory: the
 * scavenger calls it while it holds the cross-process cache lock, and a memory_limit fatal error
 * there would leave the lock owned by a FastCGI process that survives the error, blocking every
 * other process of the pool.
 */
void alloc_getinfo(alloc_context * palloc, alloc_info * pinfo)
{
    dprintverbose("start alloc_getinfo");

    _ASSERT(palloc != NULL);
    _ASSERT(pinfo  != NULL);

    lock_lock(palloc->lock);

    pinfo->total_size   = palloc->header->total_size;
    pinfo->free_size    = palloc->header->free_size;
    pinfo->usedcount    = palloc->header->usedcount;
    pinfo->freecount    = palloc->header->freecount;
    pinfo->mem_overhead = (palloc->header->usedcount + palloc->header->freecount + 2) * sizeof(alloc_free_header);

    lock_unlock(palloc->lock);

    dprintverbose("end alloc_getinfo");

    return;
}

__inline
void * alloc_emalloc(size_t size)
{
    return alloc_malloc(NULL, ALLOC_TYPE_PROCESS, size);
}

__inline
void * alloc_pemalloc(size_t size)
{
    return alloc_malloc(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, size);
}

__inline
void * alloc_smalloc(alloc_context * palloc, size_t size)
{
    _ASSERT(palloc != NULL);
    return alloc_malloc(palloc, ALLOC_TYPE_SHAREDMEM, size);
}

__inline
char * alloc_estrdup(const char * str)
{
    return alloc_strdup(NULL, ALLOC_TYPE_PROCESS, str);
}

__inline
void alloc_efree(void * addr)
{
    alloc_free(NULL, ALLOC_TYPE_PROCESS, addr);
    return;
}

__inline
void alloc_pefree(void * addr)
{
    alloc_free(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, addr);
    return;
}

__inline
void alloc_sfree(alloc_context * palloc, void * addr)
{
    _ASSERT(palloc != NULL);
    alloc_free(palloc, ALLOC_TYPE_SHAREDMEM, addr);
    return;
}

void * alloc_oemalloc(alloc_context * palloc, size_t hoffset, size_t size)
{
    _ASSERT(palloc  == NULL);
    _ASSERT(hoffset == 0);

    return alloc_emalloc(size);
}

void * alloc_osmalloc(alloc_context * palloc, size_t hoffset, size_t size)
{
    _ASSERT(palloc  != NULL);
    _ASSERT(hoffset == 0);

    return alloc_smalloc(palloc, size);
}

void * alloc_ommalloc(alloc_context * palloc, size_t hoffset, size_t size)
{
    alloc_mpool_header *  header = NULL;
    alloc_mpool_segment * pchunk = NULL;
    alloc_mpool_segment * ptemp  = NULL;

    void *                addr   = NULL;
    void *                pvoid  = NULL;
    size_t                offset = 0;
    size_t                rsize  = 0;
    size_t                bsize  = 0;
    unsigned char         ctype  = 0;

    dprintdecorate("start alloc_ommalloc");

    _ASSERT(palloc  != NULL);
    _ASSERT(hoffset >  0);
    _ASSERT(size    >  0);

#ifdef _WIN64
    rsize  = ALIGNQWORD(size);
#else /* _WIN64 */
    rsize  = ALIGNDWORD(size);
#endif /* _WIN64 */
    header = (alloc_mpool_header *)((char *)palloc->memaddr + hoffset);

    if(rsize <= POOL_ALLOCATION_SMALL_MAX)
    {
        offset = header->socurr;
        bsize  = POOL_ALLOCATION_SMALL_BSIZE;
        ctype  = POOL_ALLOCATION_TYPE_SMALL;
    }
    else if(rsize <= POOL_ALLOCATION_MEDIUM_MAX)
    {
        offset = header->mocurr;
        bsize  = POOL_ALLOCATION_MEDIUM_BSIZE;
        ctype  = POOL_ALLOCATION_TYPE_MEDIUM;
    }
    else if(rsize <= POOL_ALLOCATION_LARGE_MAX)
    {
        offset = header->locurr;
        bsize  = POOL_ALLOCATION_LARGE_BSIZE;
        ctype  = POOL_ALLOCATION_TYPE_LARGE;
    }
    else
    {
        offset = 0;
        bsize  = rsize;
        ctype  = POOL_ALLOCATION_TYPE_HUGE;
    }

    /* Check if chunk has enough space for this allocation */
    /* Else we need to allocate a new chunk and use that */
    if(offset > 0)
    {
        pchunk = (alloc_mpool_segment *)((char *)palloc->memaddr + offset);
        if(pchunk->size - pchunk->position < rsize)
        {
            offset = 0;
            pchunk = NULL;
        }
    }

    if(offset == 0)
    {
        pchunk = (alloc_mpool_segment *)alloc_smalloc(palloc, sizeof(alloc_mpool_segment));
        if(pchunk == NULL)
        {
            goto Finished;
        }

        /* Initialize pchunk */
        pchunk->aoffset  = 0;
        pchunk->size     = 0;
        pchunk->position = 0;
        pchunk->next     = 0;

        pvoid = alloc_smalloc(palloc, bsize);
        if(pvoid == NULL)
        {
            goto Finished;
        }

        /* Update aoffset to offset of allocated memory */
        /* Also update size after allocating block */
        pchunk->aoffset = POINTER_OFFSET(palloc->memaddr, pvoid);
        pchunk->size = bsize;

        offset = POINTER_OFFSET(palloc->memaddr, pchunk);
        if(header->foffset == 0)
        {
            _ASSERT(header->loffset == 0);

            header->foffset = offset;
            header->loffset = offset;
        }
        else
        {
            ptemp = (alloc_mpool_segment *)((char *)palloc->memaddr + header->loffset);
            ptemp->next     = offset;
            header->loffset = offset;
        }

        switch(ctype)
        {
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
    }
    else
    {
        pchunk = (alloc_mpool_segment *)((char *)palloc->memaddr + offset);
    }

    _ASSERT(pchunk != NULL);
    _ASSERT(pchunk->size - pchunk->position >= rsize);

    addr = (void *)((char *)palloc->memaddr + pchunk->aoffset + pchunk->position);
    pchunk->position += rsize;

    pchunk = NULL;

Finished:

    if(pchunk != NULL)
    {
        if(pchunk->aoffset != 0)
        {
            ptemp = (alloc_mpool_segment *)((char *)palloc->memaddr + pchunk->aoffset);
            alloc_sfree(palloc, ptemp);
            pchunk->aoffset = 0;
        }

        alloc_sfree(palloc, pchunk);
        pchunk = NULL;
    }

    dprintdecorate("end alloc_ommalloc");
    return addr;
}

void alloc_oefree(alloc_context * palloc, size_t hoffset, void * addr)
{
    _ASSERT(palloc == NULL);
    _ASSERT(hoffset == 0);

    alloc_efree(addr);
    return;
}

void alloc_osfree(alloc_context * palloc, size_t hoffset, void * addr)
{
    _ASSERT(palloc  != NULL);
    _ASSERT(hoffset == 0);

    alloc_sfree(palloc, addr);
    return;
}

void alloc_omfree(alloc_context * palloc, size_t hoffset, void * addr)
{
    /* Nothing to do. We will free memory in the pool in one go */
    return;
}

void alloc_runtest()
{
    int             result  = NONFATAL;
    void *          memaddr = NULL;
    unsigned char   initmem = 1;

    void *          mem1    = NULL;
    void *          mem2    = NULL;
    void *          mem3    = NULL;
    void *          mem4    = NULL;

    size_t          freesiz = 0;

    alloc_context *         palloc = NULL;
    alloc_segment_header *  header = NULL;
    alloc_free_header *     freeh  = NULL;
    alloc_used_header *     usedh  = NULL;

    dprintverbose("*** STARTING ALLOC TESTS ***");

    memaddr = malloc(4096);
    if(memaddr == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    result = alloc_create(&palloc);
    if(FAILED(result))
    {
        goto Finished;
    }

    result = alloc_initialize(palloc, "ALLOC_TEST", 1, memaddr, 4096, &initmem);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Verify alloc_context, alloc_segment_header and free blocks */
    _ASSERT(palloc->lock    != NULL);
    _ASSERT(palloc->size    == 4096);
    _ASSERT(palloc->header  != NULL);
    _ASSERT(palloc->memaddr != NULL);

    freesiz = palloc->header->free_size;

    /* Allocate 128 bytes 4 times and verify segment header, */
    /* free blocks and used blocks */
    mem1 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem1 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem1) < 4096);
    _ASSERT(palloc->header->usedcount == 1);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    mem2 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem2 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem2) < 4096);
    _ASSERT(palloc->header->usedcount == 2);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    mem3 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem3 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem3) < 4096);
    _ASSERT(palloc->header->usedcount == 3);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    mem4 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem4 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem4) < 4096);
    _ASSERT(palloc->header->usedcount == 4);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    /* Verify that free and allocating again reuse the same address */
    alloc_sfree(palloc, mem4);
    _ASSERT(mem4 == alloc_smalloc(palloc, 128));

    /* Free 3rd memory block and verify everything */
    /* Free 4th memory block and verify everything */
    /* Free 1st memory block and verify everything */
    /* Free 2nd memory block and verify everything */
    alloc_sfree(palloc, mem3);
    _ASSERT(palloc->header->usedcount == 3);
    _ASSERT(palloc->header->freecount == 2);

    alloc_sfree(palloc, mem4);
    _ASSERT(palloc->header->usedcount == 2);
    _ASSERT(palloc->header->freecount == 1);

    alloc_sfree(palloc, mem1);
    _ASSERT(palloc->header->usedcount == 1);
    _ASSERT(palloc->header->freecount == 2);

    alloc_sfree(palloc, mem2);
    _ASSERT(palloc->header->usedcount == 0);
    _ASSERT(palloc->header->freecount == 1);

    /* Allocate free_size number of bytes so that memory is full */
    /* Try allocating another block and verify error */
    /* Free block and allocate 1024 bytes twice and verify */
    /* Free one block. Verify. Free other block and verify */
    mem1 = alloc_smalloc(palloc, palloc->header->free_size);
    mem2 = alloc_smalloc(palloc, 128);

    _ASSERT(mem2 == NULL);
    _ASSERT(palloc->header->usedcount == 1);

    alloc_sfree(palloc, mem1);
    mem2 = alloc_smalloc(palloc, 1024);
    mem3 = alloc_smalloc(palloc, 1024);

    alloc_sfree(palloc, mem2);
    _ASSERT(palloc->header->usedcount == 1);

    alloc_sfree(palloc, mem3);

    _ASSERT(palloc->header->usedcount == 0);
    _ASSERT(palloc->header->freecount == 1);

    _ASSERT(SUCCEEDED(result));

Finished:

    if(palloc != NULL)
    {
        alloc_terminate(palloc);
        alloc_destroy(palloc);

        palloc =  NULL;
    }

    if(memaddr != NULL)
    {
        free(memaddr);
        memaddr = NULL;
    }

    dprintverbose("*** ENDING ALLOC TESTS ***");

    return;
}
