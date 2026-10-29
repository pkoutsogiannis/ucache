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
   | Module: ucache_filemap.h                                                                     |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _UCACHE_FILEMAP_H_
#define _UCACHE_FILEMAP_H_

/* TBD?? Should we look at commandline arguments of the process to find what */
/* all processes got launched for a single fastcgi process pool. Using ppid */
/* won't work for per site php.ini configuration. We will end up sharing the */
/* cache with all processes which won't be too bad either */

#define PID_MAX_LENGTH                          6   /* 999999 is the max value */
#define READWRITE_LOCK_MAX_COUNT                20  /* NOT USED */

#define FILEMAP_TYPE_INVALID                    0   /* Invalid value */
#define FILEMAP_TYPE_USERZVALS                  5   /* User cache map */
#define FILEMAP_TYPE_UNUSED                     255 /* UNUSED SEGMENT MARKER */
#define FILEMAP_TYPE_MAX                        255

#define FILEMAP_MAX_COUNT                       30
#define FILEMAP_MIN_SIZE                        1048576    /* 1   MB */
#define FILEMAP_MAX_SIZE                        268435456  /* 256 MB */

/* Following prefixes should be less than 254 characters */
#define FILEMAP_INFORMATION_PREFIX              "UCACHE_" STRVER2(PHP_MAJOR_VERSION, PHP_MINOR_VERSION) "_" PHP_UCACHE_VERSION "_FILEMAP_INFORMATION"
#define FILEMAP_INFORMATION_PREFIX_LENGTH       (sizeof(FILEMAP_INFORMATION_PREFIX))

#define FILEMAP_USERZVALS_PREFIX                "UCACHE_" STRVER2(PHP_MAJOR_VERSION, PHP_MINOR_VERSION) "_" PHP_UCACHE_VERSION "_FILEMAP_USERZVALS"
#define FILEMAP_USERZVALS_PREFIX_LENGTH         (sizeof(FILEMAP_USERZVALS_PREFIX))

/* filemap_information_header - SHARED - 16 bytes */
/* filemap_information_entry  - SHARED - 276 bytes */
/* filemap_global_context     - LOCAL -  8 bytes */
/* filemap_information        - LOCAL -  20 bytes */
/* filemap_context            - LOCAL -  14 bytes (not aligned at 4 bytes) */

typedef struct filemap_information_header filemap_information_header;
struct filemap_information_header
{
    size_t                       size;          /* Total size of the memory map */
    unsigned int                 mapcount;      /* Total times this shared area got mapped */
    unsigned short               entry_count;   /* How many other filemap objects are present */
    unsigned short               maxcount;      /* Maximum number of filemaps which can be created */
};

typedef struct filemap_information_entry filemap_information_entry;
struct filemap_information_entry
{
    unsigned short               fmaptype;      /* Type which tells what is the purpose of this filemap */
    unsigned short               cachekey;      /* short key which uniquely identifies fmap of this type */
    char                         name[MAX_PATH];/* name of filemap */
    size_t                       size;          /* Size of this filemap */
    unsigned int                 mapcount;      /* How many processes mapped this filemap */
    unsigned int                 cpid;          /* ProcessID of process which initially created this map */
    unsigned int                 opid;          /* Current ProcessID which is the owner */
    void *                       mapaddr;       /* Map address where owner has this memory mapped */
};

typedef struct filemap_context filemap_context;
struct filemap_context
{
    unsigned short               id;            /* unique id of this filemap context */
    filemap_information_entry *  infoentry;     /* pointer to filemap information entry */
    HANDLE                       hshmfile;      /* file handle for file backed shared memory */
    unsigned int                 existing;      /* If value is 1, skip initialization for this */
    HANDLE                       hfilemap;      /* handle to memory map object */
    void *                       mapaddr;       /* local adddress mapped for this process */
};

typedef struct filemap_information filemap_information;
struct filemap_information
{
    HANDLE                       hinfomap;      /* Handle to filemap object */
    char *                       infoname;      /* Name of memory map to store filemap info */
    size_t                       infonlen;      /* Length of name buffer */
    filemap_information_header * header;        /* Mapped memory address to information segment */
    HANDLE                       hinitdone;     /* event inidicating if memory is initialized */
    lock_context *               hlock;         /* Lock object for info filemap */
};

typedef struct filemap_global_context filemap_global_context;
struct filemap_global_context
{
    unsigned int                 pid;           /* PID of this process */
    unsigned int                 ppid;          /* Parent process id */
    filemap_information *        info;          /* pointer to filemap_information */
};

extern int  filemap_global_initialize();
extern void filemap_global_terminate();
extern unsigned int filemap_getpid();
extern unsigned int filemap_getppid();

extern int    filemap_create(filemap_context ** ppfilemap);
extern void   filemap_destroy(filemap_context * pfilemap);

extern int
filemap_initialize(
    filemap_context * pfilemap,
    unsigned short fmaptype,
    unsigned short cachekey,
    unsigned int size_mb,
    unsigned char isfirst);

extern void   filemap_terminate(filemap_context * pfilemap);

extern size_t filemap_getsize(filemap_context * pfilemap);
extern unsigned int filemap_getcpid(filemap_context * pfilemap);

extern void   filemap_runtest();

#endif /* _UCACHE_FILEMAP_H_ */
