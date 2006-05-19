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
#include <stdlib.h>

#include "print.h"
#include "totemip.h"
#include "../include/saAis.h"

static unsigned int logmode = LOG_MODE_BUFFER | LOG_MODE_STDERR | LOG_MODE_SYSLOG;
static char *logfile = 0;
static int log_setup_called;

#ifndef MAX_LOGGERS
#define MAX_LOGGERS 32
#endif
struct logger loggers[MAX_LOGGERS];

static FILE *log_file_fp = 0;

struct log_entry
{
	char *file;
	int line;
	int level;
	char str[128];
	struct log_entry *next;
};

static struct log_entry *head;
static struct log_entry *tail;

static int logger_init (const char *ident, int tags, int level, int mode)
{
	int i;

 	for (i = 0; i < MAX_LOGGERS; i++) {
		if (strcmp (loggers[i].ident, ident) == 0) {
			loggers[i].tags |= tags;
			if (level > loggers[i].level) {
				loggers[i].level = level;
			}
			break;
		}
	}

	if (i == MAX_LOGGERS) {
		for (i = 0; i < MAX_LOGGERS; i++) {
			if (strcmp (loggers[i].ident, "") == 0) {
				strncpy (loggers[i].ident, ident, sizeof(loggers[i].ident));
				loggers[i].tags = tags;
				loggers[i].level = level;
				loggers[i].mode = mode;
				break;
			}
		}
	}

	assert(i < MAX_LOGGERS);

	return i;
}

static void buffered_log_printf (char *file, int line, int level,
								 char *format, va_list ap)
{
	struct log_entry *entry = malloc(sizeof(struct log_entry));

	entry->file = file;
	entry->line = line;
	entry->level = level;
	entry->next = NULL;
	if (head == NULL) {
		head = tail = entry;
	} else {
		tail->next = entry;
		tail = entry;
	}
	vsnprintf(entry->str, sizeof(entry->str), format, ap);
}

static void _log_printf (char *file, int line,
						 int level, int id,
						 char *format, va_list ap)
{
	char newstring[4096];
	char log_string[4096];
	char char_time[512];
	struct timeval tv;
	int i = 0;
	int len;

	assert (id < MAX_LOGGERS);

	/*
	** Buffer before log_setup() has been called.
	*/
	if (logmode & LOG_MODE_BUFFER) {
		buffered_log_printf(file, line, level, format, ap);
		return;
	}

	if (((logmode & LOG_MODE_FILE) || (logmode & LOG_MODE_STDERR)) &&
		(logmode & LOG_MODE_TIMESTAMP)) {
		gettimeofday (&tv, NULL);
		strftime (char_time, sizeof (char_time), "%b %e %k:%M:%S",
				  localtime (&tv.tv_sec));
		i = sprintf (newstring, "%s.%06ld ", char_time, (long)tv.tv_usec);
	}

	if ((level == LOG_LEVEL_DEBUG) || (logmode & LOG_MODE_FILELINE)) {
		sprintf (&newstring[i], "[%s:%u] %s", file, line, format);
	} else {	
		sprintf (&newstring[i], "[%-5s] %s", loggers[id].ident, format);
	}
	len = vsprintf (log_string, newstring, ap);

	/*
	** add line feed if not done yet
	*/
	if (log_string[len - 1] != '\n') {
		log_string[len] = '\n';
		log_string[len + 1] = '\0';
	}

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

int _log_init (const char *ident)
{
	assert (ident != NULL);

	/*
	** do different things before and after log_setup() has been called
	*/
	if (log_setup_called) {
		return logger_init (ident, TAG_LOG, LOG_LEVEL_INFO, 0);
	} else {
		return logger_init (ident, ~0, LOG_LEVEL_DEBUG, 0);
	}
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

	if (config->logmode & LOG_MODE_SYSLOG) {
		openlog("openais", LOG_CONS|LOG_PID, config->syslog_facility);
	}

	/*
	** reinit all loggers that has initialised before log_setup() was called.
	*/
	for (i = 0; i < MAX_LOGGERS; i++) {
		loggers[i].tags = TAG_LOG;
		if (config->logmode & LOG_MODE_DEBUG) {
			loggers[i].level = LOG_LEVEL_DEBUG;
		} else {
			loggers[i].level = LOG_LEVEL_INFO;
		}
	}

	/*
	** init all loggers that has configured level and tags
	*/
	for (i = 0; i < config->loggers; i++) {
		if (config->logger[i].level == 0)
			config->logger[i].level = LOG_LEVEL_INFO;
		config->logger[i].tags |= TAG_LOG;
		logger_init (config->logger[i].ident,
					 config->logger[i].tags,
					 config->logger[i].level,
					 config->logger[i].mode);
	}

	log_setup_called = 1;

	/*
	** Flush what we have buffered
	*/
	log_flush();

	internal_log_printf(__FILE__, __LINE__, LOG_LEVEL_DEBUG, "log setup\n");

	return (0);
}

void internal_log_printf (char *file, int line, int priority,
						  char *format, ...)
{
	int id = LOG_ID(priority);
	int level = LOG_LEVEL(priority);
	va_list ap;

	assert (id < MAX_LOGGERS);

	if (LOG_LEVEL(priority) > loggers[id].level) {
		return;
	}

	va_start (ap, format);
	_log_printf (file, line, level, id, format, ap);
	va_end(ap);
}

void internal_log_printf2 (char *file, int line, int level, int id,
						   char *format, ...)
{
	va_list ap;

	assert (id < MAX_LOGGERS);

	va_start (ap, format);
	_log_printf (file, line, level, id, format, ap);
	va_end(ap);
}

void trace (char *file, int line, int tag, int id, char *format, ...)
{
	assert (id < MAX_LOGGERS);

	if (tag & loggers[id].tags) {
		va_list ap;

		va_start (ap, format);
		_log_printf (file, line, LOG_LEVEL_DEBUG, id, format, ap);
		va_end(ap);
	}
}

void log_flush(void)
{
	struct log_entry *entry = head;
	struct log_entry *tmp;

	/* do not buffer these printouts */
	logmode &= ~LOG_MODE_BUFFER;

	while (entry) {
		internal_log_printf(entry->file, entry->line,
							entry->level, entry->str);
		tmp = entry;
		entry = entry->next;
		free(tmp);
	}

	head = tail = NULL;
}
