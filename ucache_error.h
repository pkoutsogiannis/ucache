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
   | Module: ucache_error.h                                                                       |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _UCACHE_FATAL_H_
#define _UCACHE_FATAL_H_

/* Basic error codes */
#define NONFATAL                            0
#define FATAL                               1

/* General purpose error codes */
#define FATAL_OUT_OF_MEMORY                 10
#define FATAL_OUT_OF_LMEMORY                10
#define FATAL_OUT_OF_SMEMORY                11
#define FATAL_ACCESS_DENIED                 12
#define FATAL_FILE_NOT_FOUND                13
#define FATAL_INVALID_DATA                  14
#define FATAL_INVALID_ARGUMENT              15
#define FATAL_UNEXPECTED_DATA               17
#define FATAL_UNEXPECTED_FCALL              18
#define FATAL_CRYPTO_FAILED                 19
#define FATAL_ACL_FAILED                    20

#define FATAL_ZEND_BAILOUT                  98
#define FATAL_UNKNOWN_FATAL                 99

/* Error codes used by lock functionality */
#define FATAL_LOCK_BASE                     100
#define FATAL_LOCK_INIT_CREATEMUTEX         FATAL_LOCK_BASE + 1
#define FATAL_LOCK_INIT_CREATEEVENT         FATAL_LOCK_BASE + 2
#define FATAL_LOCK_LONGNAME                 FATAL_LOCK_BASE + 3
#define FATAL_LOCK_SHORT_BUFFER             FATAL_LOCK_BASE + 4
#define FATAL_LOCK_NUMBER_LARGE             FATAL_LOCK_BASE + 5
#define FATAL_LOCK_INVALID_TYPE             FATAL_LOCK_BASE + 6

/* Error codes used in filemap functionality */
#define FATAL_FILEMAP_BASE                  200
#define FATAL_FILEMAP_CREATION              FATAL_FILEMAP_BASE + 1
#define FATAL_FILEMAP_INFOCREATE            FATAL_FILEMAP_BASE + 2
#define FATAL_FILEMAP_INFOMAP               FATAL_FILEMAP_BASE + 3
#define FATAL_FILEMAP_CREATE_SNAPSHOT       FATAL_FILEMAP_BASE + 4
#define FATAL_FILEMAP_INITIALIZE            FATAL_FILEMAP_BASE + 5
#define FATAL_FILEMAP_NOFREE                FATAL_FILEMAP_BASE + 6
#define FATAL_FILEMAP_INIT_EVENT            FATAL_FILEMAP_BASE + 7
#define FATAL_FILEMAP_MAPVIEW               FATAL_FILEMAP_BASE + 8
#define FATAL_FILEMAP_CREATEFILE            FATAL_FILEMAP_BASE + 9
#define FATAL_FILEMAP_GETFILETIME           FATAL_FILEMAP_BASE + 10
#define FATAL_FILEMAP_SYSTOFILE             FATAL_FILEMAP_BASE + 11
#define FATAL_FILEMAP_CREATEFILEMAP         FATAL_FILEMAP_BASE + 12
#define FATAL_FILEMAP_ALLOC_LOCALFILEMAP    FATAL_FILEMAP_BASE + 13
#define FATAL_FILEMAP_REVERT_FAILED         FATAL_FILEMAP_BASE + 14
#define FATAL_FILEMAP_IMPERSONATE_FAILED    FATAL_FILEMAP_BASE + 15

/* Error codes used by shared memory allocator */
#define FATAL_ALLOC_BASE                    300
#define FATAL_ALLOC_NO_MEMORY               FATAL_ALLOC_BASE + 1
#define FATAL_ALLOC_INIT_EVENT              FATAL_ALLOC_BASE + 2
#define FATAL_ALLOC_SEGMENT_CORRUPT         FATAL_ALLOC_BASE + 3

/* Error codes used by zvcache */
#define FATAL_ZVCACHE_BASE                  900
#define FATAL_ZVCACHE_INITIALIZE            FATAL_ZVCACHE_BASE + 1
#define FATAL_ZVCACHE_INIT_EVENT            FATAL_ZVCACHE_BASE + 2
#define FATAL_ZVCACHE_INVALID_ZVAL          FATAL_ZVCACHE_BASE + 3
#define FATAL_ZVCACHE_INVALID_KEY_LENGTH    FATAL_ZVCACHE_BASE + 4

/* Warning codes */
#define WARNING_COMMON_BASE                 5000
#define WARNING_FILEMAP_MAPVIEW             WARNING_COMMON_BASE + 3
#define WARNING_ZVCACHE_EMISSING            WARNING_COMMON_BASE + 4
#define WARNING_ZVCACHE_EXISTS              WARNING_COMMON_BASE + 5
#define WARNING_ZVCACHE_NOTLONG             WARNING_COMMON_BASE + 6
#define WARNING_ZVCACHE_ARGUMENT            WARNING_COMMON_BASE + 7
#define WARNING_ZVCACHE_RESCOPYIN           WARNING_COMMON_BASE + 8
#define WARNING_ZVCACHE_CASNEQ              WARNING_COMMON_BASE + 9
#define WARNING_APPPOOL_NAME_NOT_FOUND      WARNING_COMMON_BASE + 13

/* SUCCEEDED and FAILED macros */
#ifdef SUCCEEDED
#undef SUCCEEDED
#endif
#define SUCCEEDED(r)                        ((r == NONFATAL) ? 1 : 0)
#ifdef FAILED
#undef FAILED
#endif
#define FAILED(r)                           ((r != NONFATAL) ? 1 : 0)

typedef struct error_context error_context;
struct error_context
{
    unsigned int    error_code;
    unsigned char * error_message;
};

extern unsigned int error_getlasterror();
extern unsigned int error_setlasterror();
extern char * error_gethrmessage();
extern char * error_getmessage(unsigned int error_code);

#endif /* _UCACHE_FATAL_H_ */
