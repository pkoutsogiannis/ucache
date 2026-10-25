/*
   +----------------------------------------------------------------------------------------------+
   | Windows Cache for PHP                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Copyright (c) 2009, Microsoft Corporation. All rights reserved.                              |
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
   | Module: precomp.h                                                                            |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Updated: Eric Stenson <ericsten@microsoft.com>                                               |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _PRECOMP_H_
#define _PRECOMP_H_

#define PHP_UCACHE_EXTNAME   "ucache"
#define PHP_UCACHE_VERSION    "1.0.0.0"
#define PHP_UCACHE_VERSION_LEN (sizeof(PHP_UCACHE_VERSION)-1)

/* comment following line for release builds */
/* #define UCACHE_DEBUG */

#ifdef PHP_WIN32
 #define PHP_UCACHE_API __declspec(dllexport)
#else
 #define PHP_UCACHE_API
#endif

#ifdef HAVE_CONFIG_H
 #include "config.h"
#endif

#ifdef ZTS
 #include "TSRM.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "sapi.h"
#include "ext/standard/info.h"
#include "zend_smart_str.h"
#include "ext/standard/php_var.h"

#include <tlhelp32.h>

#define ALIGNDWORD(size)   (((size) % 4) ? ((size)+(4-((size) % 4))) : (size))
#define ALIGNQWORD(size)   (((size) % 8) ? ((size)+(8-((size) % 8))) : (size))

#define XSTRVER2(maj, min)             #maj "." #min
#define STRVER2(maj, min)              XSTRVER2(maj, min)

#ifdef _ASSERT
 #undef _ASSERT
#endif

#ifdef UCACHE_DEBUG
# define UCACHE_TEST
# define _ASSERT(x)   if(!(x)) { dprintalways("%s(%d): " #x "\n", __FILE__, __LINE__ ); if(IsDebuggerPresent()) { DebugBreak(); } }
#else
# define _ASSERT(x)
#endif

#if (defined(_MSC_VER) && (_MSC_VER < 1500))
# define IsDebuggerPresent() (0)
# define memcpy_s(dst, size, src, cnt) memcpy(dst, src, cnt)
# define sprintf_s(buffer, size, format) sprintf(buffer, format)
# define _snprintf_s ucache_php_snprintf_s
# define vsprintf_s(buffer, size, format, va_alist) vsprintf(buffer, format, va_alist)
# define strcpy_s(src, size, dst) strcpy(src, dst)
#endif

#define CACHE_TYPE_USERZVALS       5

#define ZCACHE_SIZE_MINIMUM        5
#define ZCACHE_SIZE_MAXIMUM        930

#define MAX_INIT_EVENT_WAIT        30000

#include "ucache_error.h"
#include "ucache_debug.h"
#include "ucache_utils.h"
#include "ucache_lock.h"
#include "ucache_filemap.h"
#include "ucache_alloc.h"
#include "ucache_zvcache.h"

#include "php_ucache.h"

#endif /* _PRECOMP_H_ */
