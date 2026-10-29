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
   | Module: ucache_alloc.h                                                                       |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _UCACHE_ALLOC_H_
#define _UCACHE_ALLOC_H_

/* Full fledged memory allocator to allocate memory from shared memory segments */
/* Has ability to manage shared memory in memory pools in which case */
/* free is only supported for the pool and not for individual allocation */
/* Also wrappers for emalloc, efree, pemalloc and pefree */

#define ALLOC_TYPE_INVALID              0
#define ALLOC_TYPE_SHAREDMEM            1
#define ALLOC_TYPE_PROCESS              2
#define ALLOC_TYPE_PROCESS_PERSISTENT   3

/* alloc_context        - LOCAL */
/* alloc_info           - LOCAL */
/* alloc_segment_header - SHARED */
/* alloc_free_header    - SHARED */
/* alloc_used_header    - SHARED */
/* alloc_mpool_header   - SHARED */
/* alloc_mpool_segment  - SHARED */

typedef void * (*fn_malloc)(void * palloc, size_t hoffset, size_t size);
typedef void   (*fn_free)(void * palloc, size_t hoffset, void * addr);

/* Free blocks are kept in one list per size class (see alloc_class_of in ucache_alloc.c): one class */
/* per ALLOC_ALIGNMENT bytes below ALLOC_CLASS_LINEAR_BYTES, then 8 per power of two */
#ifdef _WIN64
#define ALLOC_ALIGNMENT                 8
#else  /* _WIN64 */
#define ALLOC_ALIGNMENT                 4
#endif /* _WIN64 */
#define ALLOC_CLASS_LINEAR_BYTES        1024
#define ALLOC_CLASS_LINEAR_LOG2         10
#define ALLOC_CLASS_LINEAR_COUNT        (ALLOC_CLASS_LINEAR_BYTES / ALLOC_ALIGNMENT)
#define ALLOC_CLASS_SPLIT_LOG2          3
#define ALLOC_CLASS_COUNT               (ALLOC_CLASS_LINEAR_COUNT + ((32 - ALLOC_CLASS_LINEAR_LOG2) << ALLOC_CLASS_SPLIT_LOG2))
#define ALLOC_CLASS_WORDS               ((ALLOC_CLASS_COUNT + 31) / 32)

/* alloc_segment_header.initdone once a process has initialized the segment and what its caller keeps in it */
/* ("DONE" in memory). Any other value, 0 in a new segment, means that it is still to be initialized */
#define ALLOC_SEGMENT_INITDONE          0x454E4F44

/* The first 64 bytes, one cache line (a segment starts on a page), hold what every allocation and free */
/* reads or writes, the count of the undo log included */
typedef struct alloc_segment_header alloc_segment_header;
struct alloc_segment_header
{
    unsigned int initdone;    /* ALLOC_SEGMENT_INITDONE once initialized (alloc_initdone), else not */
    unsigned int usedcount;   /* Number of used blocks. Determine fragmentation */
    unsigned int freecount;   /* Number of free blocks Determine when to defrag */
    unsigned int last_owner;  /* PID of last process to acquire the lock for this segment */
    size_t       total_size;  /* Total size of shared memory segment */
    size_t       free_size;   /* Bytes left to be allocated */
    size_t       cacheheader1;/* Offset to memory which contains 1st cache_header */
    unsigned int undocount;   /* Entries in undo: an update of the allocator in progress, 0 between updates (see alloc_lock) */
    unsigned int abandoned;   /* Times a process found the lock of the allocator abandoned since the segment was created */
    unsigned int freemap[ALLOC_CLASS_WORDS];   /* Bit c set: the list of class c is not empty */
    size_t       freelists[ALLOC_CLASS_COUNT]; /* Offset of the first free block of each class, 0 if none */
    lock_undo_entry undo[LOCK_UNDO_MAXIMUM];   /* What the update in progress changed (see lock_undo_write) */
};

/* Each free block will have following information */
/* Each allocation will be appended by size in end as well */
typedef struct alloc_free_header alloc_free_header;
struct alloc_free_header
{
    size_t       size;        /* size of free segment including this header */
    unsigned int is_free;     /* marker which differentiate this block from used block */
    size_t       prev_free;   /* offset to the previous free block of its class, 0 for the first */
    size_t       next_free;   /* offset to the next free block of its class, 0 for the last */
    /* free block will have size written in the end */
};

typedef struct alloc_used_header alloc_used_header;
struct alloc_used_header
{
    size_t       size;        /* size of this used block */
    unsigned int is_free;     /* marker which differentiate this block from free block */
    size_t       dummy1;      /* Dummy variable to make size equal to free header (never written, never read) */
    size_t       dummy2;      /* Dummy variable to make size equal to free header (never written, never read) */
    /* used block will have size written in the end */
};

typedef struct alloc_mpool_header alloc_mpool_header;
struct alloc_mpool_header
{
    size_t       foffset;     /* First memory chunk offset */
    size_t       loffset;     /* Last memory chunk offset */
    size_t       socurr;      /* Current chunk for small allocations */
    size_t       mocurr;      /* Current chunk for medium allocations */
    size_t       locurr;      /* Current chunk for large allocations */
    size_t       gocurr;      /* The chunk of the size the caller asked for, in the block of this header (see alloc_create_mpool), 0 if none */
};

typedef struct alloc_mpool_segment alloc_mpool_segment;
struct alloc_mpool_segment
{
    size_t       aoffset;     /* Address of this memory chunk offset */
    size_t       size;        /* Size of this memory chunk */
    size_t       position;    /* Current position where allocation can be made */
    size_t       next;        /* Next chunk of memory offset */
};

typedef struct alloc_context alloc_context;
struct alloc_context
{
    unsigned short         id;        /* Unique id of this alloc_context */
    void *                 memaddr;   /* Shared memory segment which is manipulated */
    size_t                 size;      /* Size of memory segment to be allocated */
    lock_context *         lock;      /* Lock to acquire before manipulating segment */
    alloc_segment_header * header;    /* Address of header for this alloc_context */
    unsigned int           held;      /* Nesting of alloc_hold(): the process holds the lock between the calls (see alloc_hold) */
    unsigned int           heldfrees; /* Blocks freed since the held lock was last taken */
    lock_undo              undo;      /* The undo log of the segment (see alloc_lock) */
};

typedef struct alloc_info alloc_info;
struct alloc_info
{
    size_t       total_size;  /* total size of memory segment */
    size_t       free_size;   /* free memory */
    unsigned int usedcount;   /* number of used blocks */
    unsigned int freecount;   /* number of free blocks */
    size_t       mem_overhead;/* overhead of used/free headers */
    unsigned int abandoned;   /* times a process found the lock of the allocator abandoned */
};

extern int    alloc_create(alloc_context ** ppalloc);
extern void   alloc_destroy(alloc_context * palloc);
extern int    alloc_initialize(alloc_context * palloc, char * name, unsigned short cachekey, void * staddr, size_t size, unsigned char * pinitmemory);
extern void   alloc_initdone(alloc_context * palloc);
extern void   alloc_terminate(alloc_context * palloc);
extern int    alloc_hold(alloc_context * palloc);
extern void   alloc_release(alloc_context * palloc);
extern int    alloc_create_mpool(alloc_context * palloc, size_t * phoffset, size_t size);
extern void   alloc_trim_mpool(alloc_context * palloc, size_t hoffset);
extern void   alloc_free_mpool(alloc_context * palloc, size_t hoffset);

extern void * alloc_get_cacheheader(alloc_context * palloc, unsigned int msize, unsigned int type);
extern void * alloc_get_cachevalue(alloc_context * palloc, size_t offset);
extern size_t alloc_get_valueoffset(alloc_context * palloc, void * value);
extern void   alloc_getinfo(alloc_context * palloc, alloc_info * pinfo);
extern size_t alloc_capacity(alloc_context * palloc);

extern void * alloc_emalloc(size_t size);
extern void * alloc_pemalloc(size_t size);
extern void * alloc_smalloc(alloc_context * palloc, size_t size);

extern char * alloc_estrdup(const char * str);

extern void   alloc_efree(void * addr);
extern void   alloc_pefree(void * addr);
extern void   alloc_sfree(alloc_context * palloc, void * addr);

extern void * alloc_oemalloc(alloc_context * palloc, size_t hoffset, size_t size);
extern void * alloc_osmalloc(alloc_context * palloc, size_t hoffset, size_t size);
extern void * alloc_ommalloc(alloc_context * palloc, size_t hoffset, size_t size);
extern void   alloc_oefree(alloc_context * palloc, size_t hoffset, void * addr);
extern void   alloc_osfree(alloc_context * palloc, size_t hoffset, void * addr);
extern void   alloc_omfree(alloc_context * palloc, size_t hoffset, void * addr);

#endif /* _UCACHE_ALLOC_H_ */
