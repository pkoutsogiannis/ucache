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
   | Module: ucache_lock.h                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _UCACHE_LOCK_H_
#define _UCACHE_LOCK_H_

#define LOCK_WAIT_TIMEOUT       200   /* Wait timeout is 200 ms */
#define LOCK_NUMBER_MAXIMUM     65535 /* Maximum number of instances for same type */
#define LOCK_NUMBER_MAX_STRLEN  5     /* 65535 is the maximum cachekey supported */

#define LOCK_TYPE_INVALID       0     /* Invalid type */
#define LOCK_TYPE_SHARED        1     /* Lock shared by the processes of the group (its name has the group) */
#define LOCK_TYPE_MAXIMUM       7     /* type is a 3 bit field. Max value is 7 */

#define LOCK_STATE_UNLOCKED     0     /* Lock is not acquired */
#define LOCK_STATE_LOCKED       1     /* Lock is acquired */
#define LOCK_STATE_MAXIMUM      1     /* state is a 1 bit field. Max value is 1 */

/* lock_context - LOCAL - 32 bytes */
/* last_owner - SHARED */
/* lock_undo - LOCAL */
/* lock_undo_entry - SHARED */

typedef struct lock_context lock_context;
struct lock_context
{
    unsigned       id:8;              /* Unique identifier for the lock */
    unsigned       type:3;            /* Type of lock (LOCK_TYPE_SHARED) */
    unsigned       unused:3;          /* padding */
    unsigned       state:2;           /* Current state of the lock for debugging */
    unsigned       namelen:16;        /* length of name buffers */

    char *         nameprefix;        /* Name prefix to use for named objects */
    HANDLE         hxlock;            /* Handle to mutex to prevent multiple writers */
    unsigned int * last_owner;        /* Pointer to shared memory which has the last owner PID */
};

/*
 * The undo log of the updates that a process makes under a lock to the segment that the lock guards (see
 * lock_undo_write in ucache_lock.c).  Its count and its LOCK_UNDO_MAXIMUM entries are in the header of
 * the segment; lock_undo says where, in each process.  An update logs 13 fields at most (a free that joins
 * the free blocks on both sides of the block, see free_block in ucache_alloc.c; the longest update of the
 * cache, the record of a full pass in reclaim_entries in ucache_zvcache.c, logs 7): the log has room for
 * more.  Count the fields of any update added or changed, and keep the most below LOCK_UNDO_MAXIMUM.
 */
#define LOCK_UNDO_MAXIMUM       24

/* The count of a log that an update overflowed (see lock_undo_write) */
#define LOCK_UNDO_OVERFLOW      0xFFFFFFFF

typedef struct lock_undo_entry lock_undo_entry;
struct lock_undo_entry
{
    size_t                  where;    /* Offset of the field from the start of the segment, plus 1 if it takes 8 bytes (else 4) */
    unsigned __int64        value;    /* What the field held before the update */
};

typedef struct lock_undo lock_undo;
struct lock_undo
{
    volatile unsigned int * pcount;   /* Entries in the log, 0 between updates (shared) */
    lock_undo_entry *       pentries; /* Its LOCK_UNDO_MAXIMUM entries (shared) */
    char *                  pbase;    /* Start of the segment */
    size_t                  size;     /* Size of the segment */
};

/* Writes value into field, a field of 4 or 8 bytes of the segment of pundo (a field of another size does not */
/* compile), after logging what it held. Call it under the lock, for every write of an update that a process */
/* reading the segment after an undo would see */
#define LOCK_UNDO_SET(pundo, field, value) \
    lock_undo_write((pundo), (void *)&(field), sizeof(char[(sizeof(field) == 4 || sizeof(field) == 8) ? 1 : -1]) * sizeof(field), (unsigned __int64)(value))

extern int  lock_create(lock_context ** pplock);
extern void lock_destroy(lock_context * plock);

extern int lock_get_nameprefix(
    char * name,
    unsigned short cachekey,
    unsigned short type,
    char **ppnew_prefix,
    size_t * pcchnew_prefix
    );
extern int  lock_initialize(lock_context * plock, char * name, unsigned short cachekey, unsigned short type, unsigned int * plast_owner);
extern void lock_terminate(lock_context * plock);

extern int  lock_lock(lock_context * plock);
extern void lock_unlock(lock_context * plock);

extern void lock_undo_initialize(lock_undo * pundo, void * pbase, size_t size, unsigned int * pcount, lock_undo_entry * pentries);
extern void lock_undo_write(lock_undo * pundo, void * pfield, size_t width, unsigned __int64 value);
extern void lock_undo_commit(lock_undo * pundo);
extern int  lock_undo_replay(lock_undo * pundo);

#endif /* _UCACHE_LOCK_H_ */
