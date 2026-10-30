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
   | Module: ucache_filemap.c                                                                     |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#define FILEMAP_INFO_HEADER_SIZE ALIGNQWORD(sizeof(filemap_information_header))
#define FILEMAP_INFO_ENTRY_SIZE  ALIGNQWORD(sizeof(filemap_information_entry))

static unsigned int getppid();
static int          create_rwlock(char* lockname, lock_context** pplock);
static void         destroy_rwlock(lock_context* plock);
static int          create_file_mapping(char* name, char* shmfilepath, unsigned char isfirst, size_t size, HANDLE* pshmfile, unsigned int* pexisting, HANDLE* pmap);
static void* map_viewof_file(HANDLE handle);
static int          create_information_filemap(filemap_information** ppinfo);
static void         destroy_information_filemap(filemap_information* pinfo);

#ifdef _WIN64
#define FILEMAP_PREFIX_FORMAT           "%s_%u_%s_x64"
#define FILEMAP_NAMESALT_PREFIX_FORMAT  "%s_%u_%s_%s_x64"
#else  // not _WIN64
#define FILEMAP_PREFIX_FORMAT           "%s_%u_%s"
#define FILEMAP_NAMESALT_PREFIX_FORMAT  "%s_%u_%s_%s"
#endif // _WIN64

// Global information containing information
// about all the memory maps which got created
unsigned short gfilemapid = 1;

/**
 * private method to get parent process id
 *
 * Computed once and kept in UCG(parentpid). If a debugger is present, it is the parent of the parent.
 *
 * @return the parent process id, or 0 if it could not be found (the process snapshot failed, or no entry
 *         matched)
 */
static unsigned int getppid() {
    int            result = NONFATAL;
    unsigned int   pid = 0;
    HANDLE         hSnapShot = INVALID_HANDLE_VALUE;
    PROCESSENTRY32 pe = { 0 };

    dprintverbose("start getppid");

    // Parent process ID will remain constant
    // Just return what was calculated last time
    if (UCG(parentpid) != 0) {
        goto Finished;
    }

    pid = GetCurrentProcessId();

    // Get the current snapshot and look for
    // current process by matching PIDs
    hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapShot == INVALID_HANDLE_VALUE) {
        error_setlasterror();
        result = FATAL_FILEMAP_CREATE_SNAPSHOT;

        goto Finished;
    }

    pe.dwSize = sizeof(PROCESSENTRY32);

    // Go through all the entries in the snapshot
    // looking for current process
    if (Process32First(hSnapShot, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                // If a debugger is present, find parent to parent processId
                if (IsDebuggerPresent()) {
                    dprintimportant("Debugger present. Finding parent to parent");
                    pid = pe.th32ParentProcessID;

                    if (Process32First(hSnapShot, &pe)) {
                        do {
                            if (pe.th32ProcessID == pid) {
                                UCG(parentpid) = pe.th32ParentProcessID;
                                break;
                            }
                        } while (Process32Next(hSnapShot, &pe));
                    }
                } else {
                    UCG(parentpid) = pe.th32ParentProcessID;
                }

                break;
            }
        } while (Process32Next(hSnapShot, &pe));
    }

Finished:

    if (hSnapShot != INVALID_HANDLE_VALUE) {
        CloseHandle(hSnapShot);
        hSnapShot = INVALID_HANDLE_VALUE;
    }

    if (FAILED(result)) {
        dprintimportant("failure %d in getppid", result);
    }

    dprintverbose("end getppid");
    return UCG(parentpid);
}

/**
 * Returns the prefix of the names of the file mappings of a filemap type.
 *
 * @param fmaptype  the filemap type (FILEMAP_TYPE_USERZVALS is the only valid one)
 * @return FILEMAP_USERZVALS_PREFIX for FILEMAP_TYPE_USERZVALS, else "UCACHE_FILEMAP_INVALID" (a static string,
 *         not to be freed)
 */
static char* get_filemap_prefix(unsigned short fmaptype
) {
    // FILEMAP_TYPE_USERZVALS is the only valid filemap type
    if (fmaptype != FILEMAP_TYPE_USERZVALS) {
        return "UCACHE_FILEMAP_INVALID";
    }
    return FILEMAP_USERZVALS_PREFIX;
}

/**
 * Writes the name of the file mapping object of a filemap into dest: the prefix of its type, the cache key,
 * the name salt if there is one, and the group of the processes that share it (plus "_x64" on x64).
 *
 * @param dest       out: buffer that receives the name
 * @param dest_size  size of dest in bytes
 * @param namesalt   name salt (ucache.namesalt), may be NULL
 * @param fmaptype   the filemap type (see get_filemap_prefix)
 * @param cachekey   key of the cache the filemap belongs to
 * @param group      the group of the processes that share the cache (see filemap_global_initialize)
 * @return what _snprintf_s returns: the length of the name, or -1 if it did not fit (truncated)
 */
static int build_filemap_name(
    char* dest,
    size_t dest_size,
    char* namesalt,
    unsigned short fmaptype,
    unsigned short cachekey,
    const char* group
) {
    int ret;
    if (namesalt == NULL) {
        ret = _snprintf_s(dest, dest_size, dest_size - 1, FILEMAP_PREFIX_FORMAT, get_filemap_prefix(fmaptype), cachekey, group);
    } else {
        ret = _snprintf_s(dest, dest_size, dest_size - 1, FILEMAP_NAMESALT_PREFIX_FORMAT, get_filemap_prefix(fmaptype), cachekey, namesalt, group);
    }

    return ret;
}

/**
 * Creates and initializes a lock shared by the processes of the group (LOCK_TYPE_SHARED, cache key 1,
 * no last owner).
 *
 * @param lockname  base name of the lock (see lock_get_nameprefix)
 * @param pplock    out: the new lock, NULL on failure; free it with destroy_rwlock
 * @return NONFATAL on success, else the FATAL_* code of lock_create or lock_initialize (FATAL_OUT_OF_LMEMORY,
 *         FATAL_LOCK_LONGNAME, FATAL_LOCK_INIT_CREATEMUTEX...)
 */
static int create_rwlock(char* lockname, lock_context** pplock) {
    int            result = NONFATAL;
    lock_context* plock = NULL;

    dprintverbose("start create_rwlock");

    _ASSERT(lockname != NULL);
    _ASSERT(pplock != NULL);

    *pplock = NULL;

    // Create a shared read exclusive write lock
    result = lock_create(&plock);
    if (FAILED(result)) {
        goto Finished;
    }

    result = lock_initialize(plock, lockname, 1, LOCK_TYPE_SHARED, NULL);
    if (FAILED(result)) {
        goto Finished;
    }

    *pplock = plock;

    _ASSERT(SUCCEEDED(result));

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in create_rwlock", result);

        if (plock != NULL) {
            lock_terminate(plock);
            lock_destroy(plock);

            plock = NULL;
        }
    }

    dprintverbose("end create_rwlock");

    return result;
}

/**
 * Terminates and frees a lock that create_rwlock created.
 *
 * @param plock  the lock, may be NULL
 */
static void destroy_rwlock(lock_context* plock) {
    dprintverbose("start destroy_rwlock");

    if (plock != NULL) {
        lock_terminate(plock);
        lock_destroy(plock);

        plock = NULL;
    }

    dprintverbose("end destroy_rwlock");

    return;
}

/**
 * Creates the named file mapping object, or opens it if it exists. With shmfilepath, the object is backed by
 * that file (created if needed and given the ACL of the application pool when new); if the ACL cannot be set,
 * the file is closed and deleted and the object goes in the paging file. Without shmfilepath, it is in the
 * paging file.
 *
 * @param name         name of the file mapping object
 * @param shmfilepath  path of the backing file, may be NULL (paging file)
 * @param isfirst      1 if the caller created the init mutex of the cache: the file of an earlier cache is
 *                     deleted first (used only with shmfilepath)
 * @param size         size of the object in bytes (an object that exists keeps its own size)
 * @param pshmfile     out: handle of the backing file, INVALID_HANDLE_VALUE if the ACL could not be set; set
 *                     only with shmfilepath (may be NULL without it)
 * @param pexisting    out: 1 if the file mapping object already existed (another process had it open), else
 *                     0; may be NULL
 * @param pmap         out: handle of the file mapping object
 * @return NONFATAL on success, FATAL_FILEMAP_CREATEFILE if the backing file cannot be created or opened,
 *         FATAL_FILEMAP_CREATEFILEMAP if CreateFileMapping fails (the out parameters are then not set)
 */
static int create_file_mapping(
    char* name,
    char* shmfilepath,
    unsigned char isfirst,
    size_t size,
    HANDLE* pshmfile,
    unsigned int* pexisting,
    HANDLE* pmap
) {
    int             result = NONFATAL;
    HANDLE          filehandle = INVALID_HANDLE_VALUE;
    HANDLE          maphandle = NULL;
    unsigned int    isexisting = 0;
    unsigned int    attributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
    unsigned int    sharemode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    unsigned int    access = GENERIC_READ | GENERIC_WRITE;
    ULARGE_INTEGER  li = { 0 };

    dprintverbose("start create_file_mapping");

    _ASSERT(name != NULL);
    _ASSERT(size > 0);
    _ASSERT(pmap != NULL);

    li.QuadPart = (ULONGLONG)size; // safely handle size_t on x64

    // If a shmfilepath is passed, map the file pointed by path
    if (shmfilepath != NULL) {
        _ASSERT(pshmfile != NULL);
        _ASSERT(*shmfilepath != '\0');

        if (isfirst) {
            // Delete the file of an earlier cache: the caller created the init mutex of the cache,
            // so no process has the cache open (they keep the mutex open until they close it)
            (void)DeleteFile(shmfilepath);
        }

        // Create a new file or open existing
        filehandle = CreateFile(shmfilepath, access, sharemode, NULL, OPEN_ALWAYS, attributes, NULL);
        if (filehandle == INVALID_HANDLE_VALUE) {
            error_setlasterror();
            result = FATAL_FILEMAP_CREATEFILE;

            goto Finished;
        }

        // A new file gets the ACL of the application pool. What an existing file holds does not matter:
        // a new file mapping object over it is initialized (see isexisting below), and one that exists
        // holds the memory of the processes that have it open
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            int aclRet = utils_set_apppool_acl(shmfilepath);

            if (FAILED(aclRet)) {
                // TODO: If the ACL'ing fails, we should close the file and fall
                // back to using the system page file.
                CloseHandle(filehandle);
                (void)DeleteFile(shmfilepath);
                filehandle = INVALID_HANDLE_VALUE;

                dprintimportant("create_file_mapping[%d]: failed to set acl on %s (%d).",
                    GetCurrentProcessId(),
                    shmfilepath,
                    aclRet);
            }
        }
    }

    // Call CreateFileMapping to create new or open existing file mapping object. The last error is
    // cleared first, so that the ERROR_ALREADY_EXISTS of CreateFile cannot be taken for its own
    SetLastError(ERROR_SUCCESS);
    maphandle = CreateFileMapping(filehandle, NULL, PAGE_READWRITE, li.HighPart, li.LowPart, name);

    // handle value null means a fatal error
    if (maphandle == NULL) {
        error_setlasterror();
        result = FATAL_FILEMAP_CREATEFILEMAP;

        goto Finished;
    }

    // Whether the object already existed: only then can its memory hold what another process initialized,
    // with the size that process gave it. A new object is to be initialized, whatever its file holds
    isexisting = (GetLastError() == ERROR_ALREADY_EXISTS);

    if (shmfilepath != NULL) {
        // INVALID_HANDLE_VALUE if the ACL could not be set: the object is then in the paging file
        *pshmfile = filehandle;
        filehandle = INVALID_HANDLE_VALUE;
    }

    if (pexisting != NULL) {
        *pexisting = isexisting;
    }

    *pmap = maphandle;
    maphandle = NULL;

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in create_file_mapping", result);

        if (maphandle != NULL) {
            CloseHandle(maphandle);
            maphandle = NULL;
        }

        if (filehandle != INVALID_HANDLE_VALUE) {
            CloseHandle(filehandle);
            filehandle = INVALID_HANDLE_VALUE;
        }
    }

    dprintverbose("end create_file_mapping");

    return result;
}

/**
 * Maps a whole view of a file mapping object, read-write, at an address the system picks.
 *
 * @param handle  handle of the file mapping object
 * @return the address of the view, or NULL on failure (the Windows error is recorded with error_setlasterror)
 */
static void* map_viewof_file(HANDLE handle) {
    void* retaddr = NULL;

    dprintverbose("Mapping file map at any random address");

    retaddr = MapViewOfFileEx(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0, NULL);
    if (retaddr == NULL) {
        error_setlasterror();
        goto Finished;
    }

    _ASSERT(retaddr != NULL);

Finished:

    return retaddr;
}

/**
 * Creates or opens the information filemap of the group (in the paging file), the table of the filemaps that
 * its processes share, with its lock and its init mutex (_FILEMAP_INIT), and initializes the table unless a
 * process already completed its initialization (FILEMAP_INFO_INITDONE).
 *
 * @param ppinfo  out: the new filemap_information, NULL on failure; free it with destroy_information_filemap
 * @return NONFATAL on success, else FATAL_OUT_OF_LMEMORY, FATAL_INVALID_DATA (the name could not be built),
 *         FATAL_FILEMAP_INIT_EVENT (the init mutex could not be created or taken), FATAL_FILEMAP_INFOMAP (the
 *         view could not be mapped), or the code of create_rwlock or create_file_mapping
 */
static int create_information_filemap(filemap_information** ppinfo) {
    int                         result = NONFATAL;
    int                         index = 0;
    int                         infonamelen = 0;
    size_t                      size = 0;
    size_t                      namelen = 0;
    filemap_information* pinfo = NULL;
    filemap_information_entry* pentry = NULL;
    unsigned char               isfirst = 1;
    unsigned char               islocked = 0;
    unsigned int                isexisting = 0;

    dprintverbose("start create_information_filemap");

    _ASSERT(ppinfo != NULL);
    *ppinfo = NULL;

    // Allocate memory for filemap_information
    pinfo = (filemap_information*)alloc_pemalloc(sizeof(filemap_information));
    if (pinfo == NULL) {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    // Initialize structure values
    pinfo->hinfomap = NULL;
    pinfo->infoname = NULL;
    pinfo->infonlen = 0;
    pinfo->header = NULL;
    pinfo->hinitdone = NULL;
    pinfo->hlock = NULL;

    // First thing to do is create the lock
    // As the lock is xread_xwrite, doing this before mapping is fine
    result = create_rwlock("FILEMAP_INFO_HRWLOCK", &pinfo->hlock);
    if (FAILED(result)) {
        goto Finished;
    }

    // Use the group in the information filemap so that the processes
    // of a group share filemap information. Add the group and 2 more
    // for underscore and terminating NULL
    namelen = FILEMAP_INFORMATION_PREFIX_LENGTH + GROUP_MAX_LENGTH + 2;

    // If a name salt is specified, use _<namesalt> in name
    if (UCG(namesalt) != NULL) {
        namelen += strlen(UCG(namesalt)) + 1;
    }

    // Allocate memory to keep name of the information filemap
    pinfo->infoname = (char*)alloc_pemalloc(namelen);
    if (pinfo->infoname == NULL) {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    ZeroMemory(pinfo->infoname, namelen);

    // Create name as FILE_INFORMATION_PREFIX_<group>
    if (UCG(namesalt) == NULL) {
        infonamelen = _snprintf_s(pinfo->infoname, namelen, namelen - 1, "%s_%s", FILEMAP_INFORMATION_PREFIX, UCG(fmapgdata)->group);
    } else {
        infonamelen = _snprintf_s(pinfo->infoname, namelen, namelen - 1, "%s_%s_%s", FILEMAP_INFORMATION_PREFIX, UCG(namesalt), UCG(fmapgdata)->group);
    }

    if (infonamelen < 0) {
        result = FATAL_INVALID_DATA;
        goto Finished;
    }

    pinfo->infonlen = infonamelen;

    result = utils_create_init_event(pinfo->infoname, "_FILEMAP_INIT", &pinfo->hinitdone, &isfirst);
    if (FAILED(result)) {
        result = FATAL_FILEMAP_INIT_EVENT;
        goto Finished;
    }

    islocked = 1;

    // Calculate size and try to get the filemap handle
    // Adding two aligned qwords sizes will produce qword
    size = FILEMAP_INFO_HEADER_SIZE + (FILEMAP_MAX_COUNT * FILEMAP_INFO_ENTRY_SIZE);

    // In the paging file: shmfilepath = NULL, pshmfile = NULL
    result = create_file_mapping(pinfo->infoname, NULL, isfirst, size, NULL, &isexisting, &pinfo->hinfomap);
    if (FAILED(result)) {
        goto Finished;
    }

    // We have the handle to information file mapping object
    // Map file mapping object in this process's virtual memory
    pinfo->header = (filemap_information_header*)map_viewof_file(pinfo->hinfomap);
    if (pinfo->header == NULL) {
        result = FATAL_FILEMAP_INFOMAP;
        goto Finished;
    }

    // Initialize it unless it existed and a process completed its initialization; the init mutex, which this
    // process owns until Finished, keeps the other processes waiting meanwhile. That this process created the
    // mutex (isfirst) does not tell: a process that starts while the last one closes the filemap (which
    // destroy_information_filemap does before it closes the mutex), or after a process died owning the mutex,
    // finds the mutex and a new filemap; and a process killed while it initialized the filemap leaves one that
    // exists without the mark. No process uses a filemap without the mark
    if (!isexisting || pinfo->header->initdone != FILEMAP_INFO_INITDONE) {
        if (isexisting) {
            dprintimportant("create_information_filemap: the initialization of %s was not completed: initializing it again", pinfo->infoname);
        }

        lock_lock(pinfo->hlock);

        pinfo->header->size = size;
        pinfo->header->maxcount = FILEMAP_MAX_COUNT;
        pinfo->header->entry_count = 0;

        // Initialize all entries to type UNUSED
        pentry = (filemap_information_entry*)((char*)pinfo->header + FILEMAP_INFO_HEADER_SIZE);
        for (index = 0; index < pinfo->header->maxcount; index++) {
            pentry->fmaptype = FILEMAP_TYPE_UNUSED;
            pentry->cachekey = 0;
            pentry = (filemap_information_entry*)((char*)pentry + FILEMAP_INFO_ENTRY_SIZE);
        }

        // Last, interlocked so that it follows every write above
        InterlockedExchange((volatile LONG*)&pinfo->header->initdone, FILEMAP_INFO_INITDONE);

        lock_unlock(pinfo->hlock);
    }

    *ppinfo = pinfo;
    _ASSERT(SUCCEEDED(result));

Finished:

    if (islocked) {
        ReleaseMutex(pinfo->hinitdone);
        islocked = 0;
    }

    if (FAILED(result)) {
        dprintimportant("failure %d in create_information_filemap", result);

        if (pinfo != NULL) {
            if (pinfo->header != NULL) {
                UnmapViewOfFile((void*)pinfo->header);
                pinfo->header = NULL;
            }

            if (pinfo->hinfomap != NULL) {
                CloseHandle(pinfo->hinfomap);
                pinfo->hinfomap = NULL;
            }

            if (pinfo->hlock != NULL) {
                lock_terminate(pinfo->hlock);
                lock_destroy(pinfo->hlock);

                pinfo->hlock = NULL;
            }

            if (pinfo->infoname != NULL) {
                alloc_pefree(pinfo->infoname);
                pinfo->infoname = NULL;

                pinfo->infonlen = 0;
            }

            if (pinfo->hinitdone != NULL) {
                CloseHandle(pinfo->hinitdone);
                pinfo->hinitdone = NULL;
            }

            alloc_pefree(pinfo);
            pinfo = NULL;
        }
    }

    dprintverbose("end create_information_filemap");

    return result;
}

/**
 * destroy the information filemap structure we created
 *
 * Unmaps the view and closes the filemap before the init mutex, then frees pinfo.
 *
 * @param pinfo  the filemap_information that create_information_filemap returned, may be NULL
 */
static void destroy_information_filemap(filemap_information* pinfo) {
    dprintverbose("start destroy_information_filemap");

    if (pinfo != NULL) {
        if (pinfo->header != NULL) {
            UnmapViewOfFile((void*)pinfo->header);

            pinfo->header = NULL;
        }

        if (pinfo->hinfomap != NULL) {
            CloseHandle(pinfo->hinfomap);
            pinfo->hinfomap = NULL;
        }

        if (pinfo->hlock != NULL) {
            lock_terminate(pinfo->hlock);
            lock_destroy(pinfo->hlock);

            pinfo->hlock = NULL;
        }

        if (pinfo->infoname != NULL) {
            alloc_pefree(pinfo->infoname);
            pinfo->infoname = NULL;

            pinfo->infonlen = 0;
        }

        if (pinfo->hinitdone != NULL) {
            CloseHandle(pinfo->hinitdone);
            pinfo->hinitdone = NULL;
        }

        alloc_pefree(pinfo);
        pinfo = NULL;
    }

    dprintverbose("end destroy_information_filemap");

    return;
}

/**
 * Global initializer which should be called once per process
 *
 * Sets UCG(fmapgdata): the process id, the group of the processes that share the cache (application pool
 * hash or parent process id), and the information filemap. Does nothing if UCG(fmapgdata) is already set.
 *
 * @return NONFATAL on success (or if already initialized), else FATAL_OUT_OF_LMEMORY or the code of
 *         create_information_filemap (UCG(fmapgdata) is then NULL)
 */
int filemap_global_initialize() {
    int                      result = NONFATAL;
    filemap_global_context* fgcontext = NULL;
    uint64_t                 poolhash = 0;

    dprintverbose("start filemap_global_initialize");

    // If global_initialize has already been called, just return
    if (UCG(fmapgdata) != NULL) {
        goto Finished;
    }

    // allocate persistent memory for fgcontext
    fgcontext = (filemap_global_context*)alloc_pemalloc(sizeof(filemap_global_context));
    if (fgcontext == NULL) {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    // Set default values of structure members
    fgcontext->pid = GetCurrentProcessId();
    fgcontext->info = NULL;

    // The group of the processes that share the cache, as the object names hold it: "P" and 16 hex digits of the
    // hash of the application pool id (see utils_apoolhash), or else the parent process id. The "P" keeps a pool
    // apart from a parent process, whatever their numbers
    if (utils_apoolhash(&poolhash)) {
        fgcontext->ispool = 1;
        _snprintf_s(fgcontext->group, sizeof(fgcontext->group), _TRUNCATE, "P%016llx", poolhash);
    } else {
        fgcontext->ispool = 0;
        _snprintf_s(fgcontext->group, sizeof(fgcontext->group), _TRUNCATE, "%u", getppid());
    }

    dprintverbose("process group %s", fgcontext->group);

    // Set global as soon as pid and group are set
    UCG(fmapgdata) = fgcontext;

    result = create_information_filemap(&fgcontext->info);
    if (FAILED(result)) {
        goto Finished;
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in filemap_global_initialize", result);

        if (fgcontext != NULL) {
            UCG(fmapgdata) = NULL;
            if (fgcontext->info != NULL) {
                destroy_information_filemap(fgcontext->info);
                fgcontext->info = NULL;
            }

            alloc_pefree(fgcontext);
            fgcontext = NULL;
        }
    }

    dprintverbose("end filemap_global_initialize");

    return result;
}

/**
 * Terminate global information including information filemap
 *
 * Frees UCG(fmapgdata) and sets it to NULL; does nothing if it is NULL.
 */
void filemap_global_terminate() {
    dprintverbose("start filemap_global_terminate");

    if (UCG(fmapgdata) != NULL) {
        if (UCG(fmapgdata)->info != NULL) {
            destroy_information_filemap(UCG(fmapgdata)->info);
            UCG(fmapgdata)->info = NULL;
        }

        alloc_pefree(UCG(fmapgdata));
        UCG(fmapgdata) = NULL;
    }

    dprintverbose("end filemap_global_terminate");

    return;
}

/**
 * API to get current process ID
 *
 * Requires filemap_global_initialize to have succeeded.
 *
 * @return the process id recorded by filemap_global_initialize
 */
unsigned int filemap_getpid() {
    _ASSERT(UCG(fmapgdata) != NULL);
    return UCG(fmapgdata)->pid;
}

/**
 * create new filemap context
 *
 * Allocates the context only; filemap_initialize creates or opens the file mapping.
 *
 * @param ppfilemap  out: the new context, NULL on failure; free it with filemap_destroy
 * @return NONFATAL on success, FATAL_OUT_OF_LMEMORY if the allocation fails
 */
int filemap_create(filemap_context** ppfilemap) {
    int               result = NONFATAL;
    filemap_context* pfilemap = NULL;

    dprintverbose("start filemap_create");

    _ASSERT(ppfilemap != NULL);
    *ppfilemap = NULL;

    pfilemap = (filemap_context*)alloc_pemalloc(sizeof(filemap_context));
    if (pfilemap == NULL) {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    pfilemap->id = gfilemapid++;
    pfilemap->infoentry = NULL;
    pfilemap->hfilemap = NULL;
    pfilemap->hshmfile = INVALID_HANDLE_VALUE;
    pfilemap->existing = 0;
    pfilemap->mapaddr = NULL;

    *ppfilemap = pfilemap;

Finished:

    if (FAILED(result)) {
        dprintimportant("failure %d in filemap_create", result);
    }

    dprintverbose("end filemap_create");

    return result;
}

/**
 * Frees a filemap context that filemap_create allocated. It does not unmap or close anything: call
 * filemap_terminate first.
 *
 * @param pfilemap  the context, may be NULL
 */
void filemap_destroy(filemap_context* pfilemap) {
    dprintverbose("start filemap_destroy");

    if (pfilemap != NULL) {
        alloc_pefree(pfilemap);
        pfilemap = NULL;
    }

    dprintverbose("end filemap_destroy");

    return;
}

/**
 * Creates or opens the shared filemap of a cache and maps it in this process. Under the lock of the
 * information filemap, it finds the entry of (fmaptype, cachekey), or takes a free one and publishes it,
 * creates or opens the file mapping object (backed by a file in ucache.filemapdir only for an application
 * pool, else in the paging file; reverting the thread's impersonation meanwhile), maps a view, and counts
 * this process in the entry's mapcount. If the object existed with another size, the entry takes the size of
 * the object.
 *
 * @param pfilemap  the context from filemap_create; receives the handles, the view (mapaddr), the entry and
 *                  whether the object existed
 * @param fmaptype  the filemap type (FILEMAP_TYPE_USERZVALS)
 * @param cachekey  key of the cache, not 0
 * @param size_mb   size of a new filemap in MB, more than 0
 * @param isfirst   1 if the caller created the init mutex of the cache (see create_file_mapping)
 * @return NONFATAL on success, else FATAL_FILEMAP_NOFREE (no free entry in the information filemap),
 *         FATAL_FILEMAP_REVERT_FAILED, FATAL_FILEMAP_CREATEFILE or FATAL_FILEMAP_CREATEFILEMAP (see
 *         create_file_mapping), FATAL_FILEMAP_MAPVIEW (the view could not be mapped); on failure the handles
 *         and the view are closed and an entry taken by this call is released
 */
int filemap_initialize(filemap_context* pfilemap, unsigned short fmaptype, unsigned short cachekey, unsigned int size_mb, unsigned char isfirst) {
    int           result = NONFATAL;
    unsigned int  ffree = 0;
    unsigned int  size = 0;
    unsigned int  index = 0;
    unsigned int  found = 0;
    unsigned char flock = 0;
    unsigned int  fcreate_file_for_sm = 1;
    char* sm_file_path = NULL;
    HANDLE        hOriginalToken = NULL;

    filemap_information* pinfo = NULL;
    filemap_information_header* pinfoh = NULL;
    filemap_information_entry* pentry = NULL;
    MEMORY_BASIC_INFORMATION     meminfo;

    dprintverbose("start filemap_initialize");

    _ASSERT(UCG(fmapgdata) != NULL);
    _ASSERT(cachekey != 0);
    _ASSERT(pfilemap != NULL);
    _ASSERT(size_mb > 0);

    size = size_mb * 1024 * 1024;

    // Only the cache of an application pool is backed by a file in ucache.filemapdir: the name of the
    // file stays the same from one start of the pool to the next, so a start replaces a file that killed
    // processes left, where a file named after a parent process would stay forever
    if (!UCG(fmapgdata)->ispool) {
        fcreate_file_for_sm = 0;
    }

    // See if this is already there in the list of filemaps
    // If not create a new filemap and add to the list
    pinfo = UCG(fmapgdata)->info;
    pinfoh = pinfo->header;

    _ASSERT(pinfoh != NULL);

    // Check if fmaptype is already there in the list of filemaps available
    lock_lock(pinfo->hlock);
    flock = 1;

    pentry = (filemap_information_entry*)((char*)pinfoh + FILEMAP_INFO_HEADER_SIZE);
    found = 0;

    for (index = 0; index < pinfoh->maxcount; index++) {
        // An entry of this filemap without a name or a size cannot be mapped: create it again. Entries are
        // published whole (see below), so only damage, or a process killed while it wrote one with code
        // that published the type first, leaves one; it would keep every process of the pool without a cache
        if (pentry->fmaptype == fmaptype && pentry->cachekey == cachekey && (pentry->size == 0 || pentry->name[0] == '\0')) {
            dprintimportant("filemap_initialize: entry %u was left half written: creating it again", index);
            pentry->fmaptype = FILEMAP_TYPE_UNUSED;
            pentry->cachekey = 0;
        }

        if (pentry->fmaptype == FILEMAP_TYPE_UNUSED && ffree == 0) {
            ffree = index;
        }

        if (pentry->fmaptype == fmaptype && pentry->cachekey == cachekey) {
            found = 1;
            break;
        }

        pentry = (filemap_information_entry*)((char*)pentry + FILEMAP_INFO_ENTRY_SIZE);
    }

    // Not created by another process yet: take the first free entry
    if (found == 0) {
        // If no free slots found, throw nofree error
        if (ffree == 0) {
            result = FATAL_FILEMAP_NOFREE;
            goto Finished;
        }

        // Get the pointer to first free entry in the table
        pentry = (filemap_information_entry*)((char*)pinfoh + FILEMAP_INFO_HEADER_SIZE);
        pentry = (filemap_information_entry*)((char*)pentry + (ffree * FILEMAP_INFO_ENTRY_SIZE));

        // Create name with the group in it
        ZeroMemory(pentry->name, MAX_PATH);

        build_filemap_name(pentry->name, MAX_PATH, UCG(namesalt), fmaptype, cachekey, UCG(fmapgdata)->group);

        dprintverbose("Creating a shared filemap with name %s", pentry->name);

        pentry->size = size;
        pentry->mapcount = 0;
        pentry->cpid = filemap_getpid();
        pentry->opid = filemap_getpid();
        pentry->mapaddr = NULL;

        // The key and the type last, after a barrier: a process killed before then (IIS can end one at
        // any instruction, which abandons the info lock) leaves the entry unused, where the other
        // processes would otherwise find an entry without a name or a size
        MemoryBarrier();

        pentry->cachekey = cachekey;
        pentry->fmaptype = fmaptype;
    }

    // If we should have a temp file for shared memory, build its name.
    if (fcreate_file_for_sm) {
        sm_file_path = utils_build_temp_filename(pentry->name);
    }

    // RevertToSelf if needed
    result = utils_revert_if_necessary(&hOriginalToken);
    if (FAILED(result)) {
        goto Finished;
    }

    result = create_file_mapping(
        pentry->name,
        sm_file_path,
        isfirst,
        pentry->size,
        &pfilemap->hshmfile,
        &pfilemap->existing,
        &pfilemap->hfilemap);
    if (FAILED(result)) {
        // OK to goto Finished. mapcount is not incremented yet
        goto Finished;
    }

    // At an address the system picks: the cache holds offsets, not pointers,
    // so each process can map it anywhere
    pfilemap->mapaddr = map_viewof_file(pfilemap->hfilemap);
    if (pfilemap->mapaddr == NULL) {
        result = FATAL_FILEMAP_MAPVIEW;
        goto Finished;
    }

    // A file mapping object that existed keeps the size it was created with, which the view has. It differs
    // from the entry's only if this process created the entry while the processes that had created the object,
    // with another ucache.size, were ending (their entry goes before their object): use the size of the object,
    // as the processes that find the entry of a running cache do. The views of a section are committed whole
    // and nothing changes their protection, so the region is the whole view
    if (pfilemap->existing && VirtualQuery(pfilemap->mapaddr, &meminfo, sizeof(meminfo)) != 0 && meminfo.RegionSize != pentry->size) {
        dprintimportant("filemap_initialize: %s has %Iu bytes, not %Iu", pentry->name, meminfo.RegionSize, pentry->size);
        pentry->size = meminfo.RegionSize;
    }

    pfilemap->infoentry = pentry;

    // All went fine. Increment entry_count for debugging
    // purposes and keep mapaddr of creator
    if (found == 0) {
        pentry->mapaddr = pfilemap->mapaddr;
        pinfoh->entry_count++;
    }

    pentry->mapcount++;

Finished:

    utils_reimpersonate_if_necessary(hOriginalToken);

    if (flock) {
        if (FAILED(result)) {
            // If we alloc'd a filemap_information_entry in the shared map,
            // we MUST clean it up while still under the lock!
            if (found == 0 && ffree != 0) {
                _ASSERT(pentry);
                pentry->fmaptype = FILEMAP_TYPE_UNUSED;
            }
        }

        lock_unlock(pinfo->hlock);
        flock = 0;
    }

    if (sm_file_path) {
        alloc_pefree(sm_file_path);
        sm_file_path = NULL;
    }

    if (FAILED(result)) {
        dprintimportant("failure %d in filemap_initialize", result);

        // mapcount and entry_count are updated in the last
        // so no need to worry about that getting messed up
        if (pfilemap->mapaddr != NULL) {
            UnmapViewOfFile(pfilemap->mapaddr);
            pfilemap->mapaddr = NULL;
        }

        if (pfilemap->hshmfile != INVALID_HANDLE_VALUE) {
            CloseHandle(pfilemap->hshmfile);
            pfilemap->hshmfile = INVALID_HANDLE_VALUE;
        }

        if (pfilemap->hfilemap != NULL) {
            CloseHandle(pfilemap->hfilemap);
            pfilemap->hfilemap = NULL;
        }
    }

    dprintverbose("end filemap_initialize");

    return result;
}

/**
 * Unmaps and closes the filemap of a context. Under the lock of the information filemap, it takes this
 * process out of the entry's mapcount; the last process releases the entry and deletes the backing file, if
 * any, once its handle is closed. The context itself is freed by filemap_destroy.
 *
 * @param pfilemap  the context, may be NULL; its entry may be NULL (filemap_initialize did not succeed)
 */
void filemap_terminate(filemap_context* pfilemap) {
    char* sm_file_path = NULL;

    dprintverbose("start filemap_terminate");

    if (pfilemap != NULL) {
        if (pfilemap->infoentry != NULL) {
            lock_lock(UCG(fmapgdata)->info->hlock);

            // Decrement the mapcount
            pfilemap->infoentry->mapcount--;

            // If all the processes unmapped the filemap,
            // remove the entry from information list
            if (pfilemap->infoentry->mapcount == 0) {
                pfilemap->infoentry->fmaptype = FILEMAP_TYPE_UNUSED;
                pfilemap->infoentry->cachekey = 0;
                UCG(fmapgdata)->info->header->entry_count--;

                // Get the name of the backing file, so we can delete it
                // after we close the handle.
                if (pfilemap->hshmfile != INVALID_HANDLE_VALUE) {
                    sm_file_path = utils_build_temp_filename(pfilemap->infoentry->name);
                }
            }

            lock_unlock(UCG(fmapgdata)->info->hlock);
        }

        if (pfilemap->mapaddr != NULL) {
            UnmapViewOfFile(pfilemap->mapaddr);
            pfilemap->mapaddr = NULL;
        }

        if (pfilemap->hfilemap != NULL) {
            CloseHandle(pfilemap->hfilemap);
            pfilemap->hfilemap = NULL;
        }

        if (pfilemap->hshmfile != INVALID_HANDLE_VALUE) {
            CloseHandle(pfilemap->hshmfile);
            pfilemap->hshmfile = INVALID_HANDLE_VALUE;

            // if we are the last one out, delete the file
            if (sm_file_path) {
                (void)DeleteFile(sm_file_path);
                alloc_pefree(sm_file_path);
                sm_file_path = NULL;
            }
        }
    }

    dprintverbose("end filemap_terminate");
    return;
}

/**
 * Reads the size of a filemap from its entry, under the lock of the information filemap.
 *
 * @param pfilemap  a context that filemap_initialize initialized
 * @return the size of the filemap in bytes
 */
size_t filemap_getsize(filemap_context* pfilemap) {
    size_t size = 0;

    dprintverbose("start filemap_getsize");

    _ASSERT(pfilemap != NULL);

    lock_lock(UCG(fmapgdata)->info->hlock);

    size = pfilemap->infoentry->size;

    lock_unlock(UCG(fmapgdata)->info->hlock);

    dprintverbose("end filemap_getsize");
    return size;
}

/**
 * Reads the id of the process that created the entry of a filemap, under the lock of the information
 * filemap.
 *
 * @param pfilemap  a context that filemap_initialize initialized
 * @return the process id of the creator (cpid of the entry)
 */
unsigned int filemap_getcpid(filemap_context* pfilemap) {
    unsigned int cpid = 0;

    dprintverbose("start filemap_getcpid");

    _ASSERT(pfilemap != NULL);

    lock_lock(UCG(fmapgdata)->info->hlock);

    cpid = pfilemap->infoentry->cpid;

    lock_unlock(UCG(fmapgdata)->info->hlock);

    dprintverbose("end filemap_getcpid");

    return cpid;
}
