/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 *
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 *		Author: Hans Feldt
 *      Description: Added support for runtime installed loggers, tags tracing,
 *                   and file & line printing.
 *
 * All rights reserved.
 *
 * This software licensed under BSD license, the text of which follows:
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef PRINT_H_DEFINED
#define PRINT_H_DEFINED

#include <stdarg.h>
#include <syslog.h>
#include "mainconfig.h"

#define LOG_MODE_DEBUG		1
#define LOG_MODE_TIMESTAMP	2
#define LOG_MODE_FILE		4
#define LOG_MODE_SYSLOG		8
#define LOG_MODE_STDERR		16
#define LOG_MODE_FILELINE   32

/*
 * Log levels, compliant with syslog and SA Forum Log spec.
 */
#define LOG_LEVEL_EMERG	    LOG_EMERG
#define LOG_LEVEL_ALERT		LOG_ALERT
#define LOG_LEVEL_CRIT		LOG_CRIT
#define LOG_LEVEL_ERROR		LOG_ERR
#define LOG_LEVEL_WARNING	LOG_WARNING
#define LOG_LEVEL_SECURITY	LOG_WARNING // openais specific
#define LOG_LEVEL_NOTICE	LOG_NOTICE
#define LOG_LEVEL_INFO	    LOG_INFO
#define LOG_LEVEL_DEBUG		LOG_DEBUG

/*
** Log tags, used by trace macros, uses 32 bits => 32 different tags
*/	
#define TAG_LOG	    1<<0
#define TAG_ENTER	1<<1
#define TAG_LEAVE	1<<2
#define TAG_TRACE1	1<<3
#define TAG_TRACE2	1<<4
#define TAG_TRACE3	1<<5
#define TAG_TRACE4	1<<6
#define TAG_TRACE5	1<<7
#define TAG_TRACE6	1<<8
#define TAG_TRACE7	1<<9
#define TAG_TRACE8	1<<10

struct logger {
	char ident[6];
	int level;
	int tags;
	int mode;
};

extern struct logger loggers[];

/*
** The logger_identifier variable holds the numerical identifier for a logger
** obtained with log_init() and hides it from the logger.
*/
static int logger_identifier __attribute__((unused));

extern void internal_log_printf (char *file, int line, int priority, char *format, ...);
extern void internal_log_printf2 (char *file, int line, int priority, char *format, ...);

#define LEVELMASK 0x07                 /* 3 bits */
#define LOG_LEVEL(p) ((p) & LEVELMASK)
#define IDMASK (0x3f << 3)             /* 6 bits */
#define LOG_ID(p)  (((p) & IDMASK) >> 3)

#define _mkpri(lvl, id) (((id) << 3) | (lvl))

static inline int mkpri (int level, int id)
{
	return _mkpri (level, id);
}

int log_setup (char **error_string, struct main_config *config);

extern int _log_init (const char *ident);
static inline void log_init (const char *ident)
{
	logger_identifier = _log_init (ident);
}

#define log_printf(lvl, format, args...) do { \
    if ((lvl) <= loggers[logger_identifier].level)	{ \
		internal_log_printf2 (__FILE__, __LINE__, _mkpri ((lvl), logger_identifier), format, ##args);  \
    } \
} while(0)

#define dprintf(format, args...) do { \
    if (LOG_LEVEL_DEBUG <= loggers[logger_identifier].level)	{ \
		internal_log_printf2 (__FILE__, __LINE__, _mkpri (LOG_LEVEL_DEBUG, logger_identifier), format, ##args);  \
    } \
} while(0)

#define ENTER() do { \
    if ((LOG_LEVEL_DEBUG <= loggers[logger_identifier].level) && (TAG_ENTER & loggers[logger_identifier].tags))	{ \
        internal_log_printf2 (__FILE__, __LINE__, _mkpri (LOG_LEVEL_DEBUG, logger_identifier), ">%s\n", __FUNCTION__); \
    } \
} while(0)

#define ENTER_ARGS(format, args...) do { \
    if ((LOG_LEVEL_DEBUG <= loggers[logger_identifier].level) && (TAG_ENTER & loggers[logger_identifier].tags))	{ \
        internal_log_printf2 (__FILE__, __LINE__, _mkpri (LOG_LEVEL_DEBUG, logger_identifier), ">%s: " format, __FUNCTION__, ##args); \
    } \
} while(0)

#define LEAVE() do { \
    if ((LOG_LEVEL_DEBUG <= loggers[logger_identifier].level) && (TAG_LEAVE & loggers[logger_identifier].tags))	{ \
        internal_log_printf2 (__FILE__, __LINE__, _mkpri (LOG_LEVEL_DEBUG, logger_identifier), "<%s\n", __FUNCTION__); \
    } \
} while(0)

#define TRACE8(format, args...) do { \
    if ((LOG_LEVEL_DEBUG <= loggers[logger_identifier].level) && (TAG_TRACE8 & loggers[logger_identifier].tags)) { \
		internal_log_printf2 (__FILE__, __LINE__, _mkpri (LOG_LEVEL_DEBUG, logger_identifier), format, ##args);  \
    } \
} while(0)

#endif /* PRINT_H_DEFINED */
