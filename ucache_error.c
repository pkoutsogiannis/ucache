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
   | Module: ucache_error.c                                                                       |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

error_context ucache_errors[] =
{
    { FATAL_OUT_OF_MEMORY, "Fatal: Out of memory error" },
    { FATAL_OUT_OF_LMEMORY, "Fatal: Out of local memory error" },
    { FATAL_OUT_OF_SMEMORY, "Fatal: Out of shared memor error" },
    { FATAL_ACCESS_DENIED, "Fatal: Access Denied" },
    { FATAL_LOCK_INIT_CREATEMUTEX, "Fatal: CreateMutex operation failed in lock_initialize" },
    { FATAL_LOCK_LONGNAME, "Fatal: Lock prefix is too long" },
    { FATAL_FILEMAP_CREATION, "Fatal: Filemap creation error" },
    { FATAL_FILEMAP_INFOCREATE, "Fatal: CreateFileMapping operation for information filemap failed" },
    { FATAL_FILEMAP_INFOMAP, "Fatal: MapViewOfFileEx operation for information filemap failed" },
    { FATAL_FILEMAP_CREATE_SNAPSHOT, "Fatal: CreateToolhelp32Snapshot call failed unexpectedly" },
    { FATAL_FILEMAP_INITIALIZE, "Fatal: New filemap couldn't be created" },
    { FATAL_FILEMAP_NOFREE, "Fatal: No free filemap_information_entry found in info filemap" },
    { FATAL_ALLOC_NO_MEMORY, "Fatal: Shared memory segment is completely full" }
};

// Windows maintain last error code for each thread
// We are only maintaining for the process
unsigned int error_glerror = 0;

/**
 * The last Windows error that error_setlasterror() recorded for the process.
 *
 * @return the recorded error code (0 if none was recorded)
 */
unsigned int error_getlasterror() {
    return error_glerror;
}

/**
 * Records GetLastError() of the calling thread as the last error of the process (error_glerror).
 *
 * @return the recorded error code
 */
unsigned int error_setlasterror() {
    error_glerror = GetLastError();
    return error_glerror;
}

/**
 * The message of ucache_errors for the recorded last error (error_glerror), through error_getmessage().
 *
 * @return a static string (not to be freed); NULL if ucache_errors has no entry for that code
 */
char* error_gethrmessage() {
    return error_getmessage(error_glerror);
}

/**
 * The message of ucache_errors for an error code. The loop compares the entries with error_glerror, the recorded
 * last error, not with error_code, so it returns the message of the recorded last error whatever error_code is.
 *
 * @param error_code  the error code to look up (not used by the lookup, see above)
 * @return a static string (not to be freed); NULL if ucache_errors has no entry for error_glerror
 */
char* error_getmessage(unsigned int error_code) {
    int    count = 0;
    int    index = 0;
    char*  message = NULL;

    count = sizeof(ucache_errors) / sizeof(ucache_errors[0]);
    for (index = 0; index < count; index++) {
        if (ucache_errors[index].error_code == error_glerror) {
            message = ucache_errors[index].error_message;
            break;
        }
    }

    return message;
}
