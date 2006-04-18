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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#if defined(OPENAIS_LINUX)
#include <linux/un.h>
#endif
#if defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
#include <sys/un.h>
#endif
#include <syslog.h>
#include "print.h"
#include "totemip.h"
#include "../include/saAis.h"

static unsigned int logmode = LOG_MODE_STDERR | LOG_MODE_SYSLOG;
static char *logfile = 0;

#define MAX_LOGGERS 32
struct logger loggers[MAX_LOGGERS];

#define LOG_MODE_DEBUG      1
#define LOG_MODE_TIMESTAMP  2
#define LOG_MODE_FILE       4
#define LOG_MODE_SYSLOG     8
#define LOG_MODE_STDERR     16

static FILE *log_file_fp = 0;

struct sockaddr_un syslog_sockaddr = {
	sun_family: AF_UNIX,
	sun_path: "/dev/log"
};

static int logger_init (const char *ident, int tags, int level, int mode)
{
	int i;

	for (i = 0; i < MAX_LOGGERS; i++) {
		if (strcmp (loggers[i].ident, ident) == 0) {
			break;
		}
	}

	if (i == MAX_LOGGERS) {
		for (i = 0; i < MAX_LOGGERS; i++) {
			if (strcmp (loggers[i].ident, "") == 0) {
				strncpy (loggers[i].ident, ident, sizeof(loggers[i].ident));
				loggers[i].tags = tags;
				loggers[i].level = level;
				if (logmode & LOG_MODE_DEBUG)
					loggers[i].level = LOG_LEVEL_DEBUG;
				loggers[i].mode = mode;
				break;
			}
		}
	}

	assert(i < MAX_LOGGERS);

	return i;
}

int _log_init (const char *ident)
{
	assert (ident != NULL);

	return logger_init (ident, TAG_LOG, LOG_LEVEL_INFO, 0);
}

int log_setup (char **error_string, struct main_config *config)
{
	int i;
	static char error_string_response[512];

	logmode = config->logmode;
	logfile = config->logfile;

	if (config->logmode & LOG_MODE_FILE) {
		log_file_fp = fopen (config->logfile, "a+");
		if (log_file_fp == 0) {
			sprintf (error_string_response,
				"Can't open logfile '%s' for reason (%s).\n",
					 config->logfile, strerror (errno));
			*error_string = error_string_response;
			return (-1);
		}
	}

	for (i = 0; i < config->loggers; i++) {
		if (config->logger[i].level == 0)
			config->logger[i].level = LOG_LEVEL_INFO;
		config->logger[i].tags |= TAG_LOG;
		logger_init (config->logger[i].ident,
					 config->logger[i].tags,
					 config->logger[i].level,
					 config->logger[i].mode);
	}
			
	return (0);
}

static void _log_printf (char *file, int line, int priority,
						char *format, va_list ap)
{
	char newstring[4096];
	char log_string[4096];
	char char_time[512];
	struct timeval tv;
	int level = LOG_LEVEL(priority);
	int id = LOG_ID(priority);
	int i = 0;

	assert (id < MAX_LOGGERS);

	if (((logmode & LOG_MODE_FILE) || (logmode & LOG_MODE_STDERR)) &&
		(logmode & LOG_MODE_TIMESTAMP)) {
		gettimeofday (&tv, NULL);
		strftime (char_time, sizeof (char_time), "%b %e %k:%M:%S",
				  localtime (&tv.tv_sec));
		i = sprintf (newstring, "%s.%06ld ", char_time, tv.tv_usec);
	}

	if ((level == LOG_LEVEL_DEBUG) || (logmode & LOG_MODE_FILELINE)) {
		sprintf (&newstring[i], "[%s:%u] %s", file, line, format);
	} else {	
		sprintf (&newstring[i], "[%-5s] %s", loggers[id].ident, format);
	}
	vsprintf (log_string, newstring, ap);

	/*
	 * Output the log data
	 */
	if (logmode & LOG_MODE_FILE && log_file_fp != 0) {
		fprintf (log_file_fp, "%s", log_string);
		fflush (log_file_fp);
	}
	if (logmode & LOG_MODE_STDERR) {
		fprintf (stderr, "%s", log_string);
	}
	fflush (stdout);

	if (logmode & LOG_MODE_SYSLOG) {
		syslog (level, &log_string[i]);
	}
}

void internal_log_printf (char *file, int line, int priority,
						  char *format, ...)
{
	int id = LOG_ID(priority);
	va_list ap;

	assert (id < MAX_LOGGERS);

	if (LOG_LEVEL(priority) > loggers[id].level) {
		return;
	}

	va_start (ap, format);
	_log_printf (file, line, priority, format, ap);
	va_end(ap);
}

void internal_log_printf2 (char *file, int line, int priority,
						  char *format, ...)
{
	va_list ap;
	va_start (ap, format);
	_log_printf (file, line, priority, format, ap);
	va_end(ap);
}
