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
   | Module: ucache_debug.c                                                                       |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   | Modified: Periklis Koutsogiannis <pkoutsogiannis@datasteam.com>                              |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#define UCACHE_DEBUG_MTYPE_DISABLED  0
#define UCACHE_DEBUG_MTYPE_ALWAYS    101
#define UCACHE_DEBUG_MTYPE_CRITICAL  201
#define UCACHE_DEBUG_MTYPE_IMPORTANT 301
#define UCACHE_DEBUG_MTYPE_VERBOSE   401
#define UCACHE_DEBUG_MTYPE_DECORATE  501

unsigned int gdebuglevel = 0;

/**
 * Sets the debug output level (gdebuglevel) if level is one of the UCACHE_DEBUG_MTYPE_* values (0, 101, 201, 301,
 * 401, 501); any other value leaves it unchanged.
 *
 * @param level  the new level
 */
void dprintsetlevel(unsigned int level) {
    if (level == UCACHE_DEBUG_MTYPE_DISABLED ||
        level == UCACHE_DEBUG_MTYPE_ALWAYS ||
        level == UCACHE_DEBUG_MTYPE_CRITICAL ||
        level == UCACHE_DEBUG_MTYPE_IMPORTANT ||
        level == UCACHE_DEBUG_MTYPE_VERBOSE ||
        level == UCACHE_DEBUG_MTYPE_DECORATE) {
        gdebuglevel = level;
    }

    return;
}

/**
 * Formats a message, prefixed with "UCACHE: ", into a 255-byte buffer (247 bytes for the formatted text with its
 * terminating null) and sends it to the debugger with OutputDebugStringA(), followed by a newline when a debugger
 * is attached. Does not check the debug level: the dprint* functions do.
 *
 * @param format  printf-style format
 * @param args    arguments of the format
 */
void dprintmessage(char* format, va_list args) {
    char debug_message[255];

    sprintf_s(debug_message, 255, "UCACHE: ");
    vsprintf_s(debug_message + 8, 247, format, args);

    OutputDebugStringA(debug_message);
    if (IsDebuggerPresent()) {
        OutputDebugStringA("\n");
    }

    return;
}

/**
 * Writes a debug message (dprintmessage()) when the debug level is at least UCACHE_DEBUG_MTYPE_ALWAYS (101).
 *
 * @param format  printf-style format
 * @param ...     arguments of the format
 */
void dprintalways(char* format, ...) {
    va_list args;
    if (gdebuglevel >= UCACHE_DEBUG_MTYPE_ALWAYS) {
        va_start(args, format);
        dprintmessage(format, args);
        va_end(args);
    }
}

/**
 * Writes a debug message (dprintmessage()) when the debug level is at least UCACHE_DEBUG_MTYPE_CRITICAL (201).
 *
 * @param format  printf-style format
 * @param ...     arguments of the format
 */
void dprintcritical(char* format, ...) {
    va_list args;
    if (gdebuglevel >= UCACHE_DEBUG_MTYPE_CRITICAL) {
        va_start(args, format);
        dprintmessage(format, args);
        va_end(args);
    }
}

/**
 * Writes a debug message (dprintmessage()) when the debug level is at least UCACHE_DEBUG_MTYPE_IMPORTANT (301).
 *
 * @param format  printf-style format
 * @param ...     arguments of the format
 */
void dprintimportant(char* format, ...) {
    va_list args;
    if (gdebuglevel >= UCACHE_DEBUG_MTYPE_IMPORTANT) {
        va_start(args, format);
        dprintmessage(format, args);
        va_end(args);
    }
}

/**
 * Writes a debug message (dprintmessage()) when the debug level is at least UCACHE_DEBUG_MTYPE_VERBOSE (401).
 *
 * @param format  printf-style format
 * @param ...     arguments of the format
 */
void dprintverbose(char* format, ...) {
    va_list args;
    if (gdebuglevel >= UCACHE_DEBUG_MTYPE_VERBOSE) {
        va_start(args, format);
        dprintmessage(format, args);
        va_end(args);
    }
}

/**
 * Writes a debug message (dprintmessage()) when the debug level is at least UCACHE_DEBUG_MTYPE_DECORATE (501).
 *
 * @param format  printf-style format
 * @param ...     arguments of the format
 */
void dprintdecorate(char* format, ...) {
    va_list args;
    if (gdebuglevel >= UCACHE_DEBUG_MTYPE_DECORATE) {
        va_start(args, format);
        dprintmessage(format, args);
        va_end(args);
    }
}

/**
 * The on-modify handler of the ucache.debuglevel setting: an empty value disables debug output, any other value
 * is passed to dprintsetlevel() (through atoi()) and the resulting level, unchanged if the value is not a valid
 * level, is copied to UCG(debuglevel). The parameters are those of ZEND_INI_MH.
 *
 * @param entry      the ini entry (unused)
 * @param new_value  the new value of the setting
 * @param mh_arg1    handler argument (unused)
 * @param mh_arg2    handler argument (unused)
 * @param mh_arg3    handler argument (unused)
 * @param stage      the stage at which the setting changes (unused)
 * @return SUCCESS, always
 */
ZEND_INI_MH(ucache_modify_debuglevel) {
    if (ZSTR_LEN(new_value) == 0) {
        dprintsetlevel(UCACHE_DEBUG_MTYPE_DISABLED);
    } else {
        dprintsetlevel(atoi(ZSTR_VAL(new_value)));
        UCG(debuglevel) = gdebuglevel;
    }
    return SUCCESS;
}
