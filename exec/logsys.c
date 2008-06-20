/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
 *
 * Author: Steven Dake (sdake@redhat.com)
 * Author: Lon Hohberger (lhh@redhat.com)
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
#define SYSLOG_NAMES
#include <syslog.h>
#include <stdlib.h>
#include <pthread.h>

#include "swab.h"
#include "logsys.h"
#include "totemip.h"
//#include "../include/saAis.h"
#include "mainconfig.h"
#include "wthread.h"

/*
 * Configuration parameters for logging system
 */
static char *logsys_name = NULL;

static unsigned int logsys_mode = 0;

static char *logsys_file = NULL;

static FILE *logsys_file_fp = NULL;

static int logsys_facility = LOG_DAEMON;

static int logsys_nosubsys = 0;

static int logsys_wthread_active = 0;

static pthread_mutex_t logsys_config_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t logsys_new_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct worker_thread_group log_thread_group;

static unsigned int dropped_log_entries = 0;

#ifndef MAX_LOGGERS
#define MAX_LOGGERS 32
#endif
struct logsys_logger logsys_loggers[MAX_LOGGERS];


struct log_entry {
	char *file;
	int line;
	int priority;
	char str[128];
	struct log_entry *next;
};

static struct log_entry *head;

static struct log_entry *tail;

struct log_data {
	unsigned int syslog_pos;
	unsigned int priority;
	char *log_string;
};

enum logsys_config_mutex_state {
	LOGSYS_CONFIG_MUTEX_LOCKED,
	LOGSYS_CONFIG_MUTEX_UNLOCKED
};

static void logsys_atexit (void);

#define LEVELMASK 0x07                 /* 3 bits */
#define LOG_LEVEL(p) ((p) & LEVELMASK)
#define LOGSYS_IDMASK (0x3f << 3)             /* 6 bits */
#define LOG_ID(p)  (((p) & LOGSYS_IDMASK) >> 3)

static void logsys_buffer_flush (void);

void _logsys_nosubsys_set (void)
{
	logsys_nosubsys = 1;
}

int logsys_facility_id_get (const char *name)
{
	unsigned int i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, facilitynames[i].c_name) == 0) {
			return (facilitynames[i].c_val);
		}
	}
	return (-1);
}

char *logsys_facility_name_get (unsigned int facility)
{
	unsigned int i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (facility == facilitynames[i].c_val) {
			return (facilitynames[i].c_name);
		}
	}
	return (NULL);
}

int logsys_priority_id_get (const char *name)
{
	unsigned int i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, prioritynames[i].c_name) == 0) {
			return (prioritynames[i].c_val);
		}
	}
	return (-1);
}

char *logsys_priority_name_get (unsigned int priority)
{
	unsigned int i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (priority == prioritynames[i].c_val) {
			return (prioritynames[i].c_name);
		}
	}
	return (NULL);
}

unsigned int logsys_config_subsys_set (
	const char *subsys,
	unsigned int tags,
	unsigned int priority)
{
	int i;

	pthread_mutex_lock (&logsys_config_mutex);
 	for (i = 0; i < MAX_LOGGERS; i++) {
		if (strcmp (logsys_loggers[i].subsys, subsys) == 0) {
			logsys_loggers[i].tags = tags;
			logsys_loggers[i].priority = priority;

			if (priority > logsys_loggers[i].priority) {
				logsys_loggers[i].priority = priority;
			}
			break;
		}
	}

	if (i == MAX_LOGGERS) {
		for (i = 0; i < MAX_LOGGERS; i++) {
			if (strcmp (logsys_loggers[i].subsys, "") == 0) {
				strncpy (logsys_loggers[i].subsys, subsys,
					sizeof(logsys_loggers[i].subsys));
				logsys_loggers[i].tags = tags;
				logsys_loggers[i].priority = priority;
				break;
			}
		}
	}
	assert(i < MAX_LOGGERS);

	pthread_mutex_unlock (&logsys_config_mutex);
	return i;
}

inline int logsys_mkpri (int priority, int id)
{
	return (((id) << 3) | (priority));
}

int logsys_config_subsys_get (
	const char *subsys,
	unsigned int *tags,
	unsigned int *priority)
{
	unsigned int i;

	pthread_mutex_lock (&logsys_config_mutex);

 	for (i = 0; i < MAX_LOGGERS; i++) {
		if (strcmp (logsys_loggers[i].subsys, subsys) == 0) {
			*tags = logsys_loggers[i].tags;
			*priority = logsys_loggers[i].priority;
			pthread_mutex_unlock (&logsys_config_mutex);
			return (0);
		}
	}

	pthread_mutex_unlock (&logsys_config_mutex);

	return (-1);
}

static void buffered_log_printf (
	char *file,
	int line,
	int priority,
	char *format,
	va_list ap)
{
	struct log_entry *entry = malloc(sizeof(struct log_entry));

	entry->file = file;
	entry->line = line;
	entry->priority = priority;
	entry->next = NULL;
	if (head == NULL) {
		head = tail = entry;
	} else {
		tail->next = entry;
		tail = entry;
	}
	vsnprintf(entry->str, sizeof(entry->str), format, ap);
}

static void log_printf_worker_fn (void *thread_data, void *work_item)
{
	struct log_data *log_data = (struct log_data *)work_item;

	if (logsys_wthread_active)
		pthread_mutex_lock (&logsys_config_mutex);
	/*
	 * Output the log data
	 */
	if (logsys_mode & LOG_MODE_OUTPUT_FILE && logsys_file_fp != 0) {
		fprintf (logsys_file_fp, "%s", log_data->log_string);
		fflush (logsys_file_fp);
	}
	if (logsys_mode & LOG_MODE_OUTPUT_STDERR) {
		fprintf (stderr, "%s", log_data->log_string);
		fflush (stdout);
	}

	if (logsys_mode & LOG_MODE_OUTPUT_SYSLOG_THREADED) {
		syslog (log_data->priority,
			&log_data->log_string[log_data->syslog_pos]);
	}
	free (log_data->log_string);
	if (logsys_wthread_active)
		pthread_mutex_unlock (&logsys_config_mutex);
}

static void _log_printf (
	enum logsys_config_mutex_state config_mutex_state,
	char *file,
	int line,
	int priority,
	int id,
	char *format,
	va_list ap)
{
	char newstring[4096];
	char log_string[4096];
	char char_time[512];
	struct timeval tv;
	int i = 0;
	int len;
	struct log_data log_data;
	unsigned int res = 0;

	assert (id < MAX_LOGGERS);

	if (config_mutex_state == LOGSYS_CONFIG_MUTEX_UNLOCKED) {
		pthread_mutex_lock (&logsys_config_mutex);
	}
	pthread_mutex_lock (&logsys_new_log_mutex);
	/*
	** Buffer before log has been configured has been called.
	*/
	if (logsys_mode & LOG_MODE_BUFFER_BEFORE_CONFIG) {
		buffered_log_printf(file, line, logsys_mkpri(priority, id), format, ap);
		pthread_mutex_unlock (&logsys_new_log_mutex);
		if (config_mutex_state == LOGSYS_CONFIG_MUTEX_UNLOCKED) {
			pthread_mutex_unlock (&logsys_config_mutex);
		}
		return;
	}

	if (((logsys_mode & LOG_MODE_OUTPUT_FILE) || (logsys_mode & LOG_MODE_OUTPUT_STDERR)) &&
		(logsys_mode & LOG_MODE_DISPLAY_TIMESTAMP)) {
		gettimeofday (&tv, NULL);
		strftime (char_time, sizeof (char_time), "%b %e %k:%M:%S",
				  localtime (&tv.tv_sec));
		i = sprintf (newstring, "%s.%06ld ", char_time, (long)tv.tv_usec);
	}

	if ((priority == LOG_LEVEL_DEBUG) || (logsys_mode & LOG_MODE_DISPLAY_FILELINE)) {
		sprintf (&newstring[i], "[%s:%04u] %s", file, line, format);
	} else {	
		if (logsys_nosubsys == 1) {
			sprintf (&newstring[i], "%s", format);
		} else {
			sprintf (&newstring[i], "[%-5s] %s", logsys_loggers[id].subsys, format);
		}
	}
	if (dropped_log_entries) {
		/*
		 * Get rid of \n if there is one
		 */
		if (newstring[strlen (newstring) - 1] == '\n') {
			newstring[strlen (newstring) - 1] = '\0';
		}
		len = sprintf (log_string,
			"%s - prior to this log entry, openais logger dropped '%d' messages because of overflow.", newstring, dropped_log_entries + 1);
	} else {
		len = vsprintf (log_string, newstring, ap);
	}

	/*
	** add line feed if not done yet
	*/
	if (log_string[len - 1] != '\n') {
		log_string[len] = '\n';
		log_string[len + 1] = '\0';
	}

	/*
	 * Create work thread data
	 */
	log_data.syslog_pos = i;
	log_data.priority = priority;
	log_data.log_string = strdup (log_string);
	if (log_data.log_string == NULL) {
		goto drop_log_msg;
	}
	
	if (logsys_wthread_active) {
		res = worker_thread_group_work_add (&log_thread_group, &log_data);
		if (res == 0) {
			dropped_log_entries = 0;
		} else {
			dropped_log_entries += 1;
		}
	} else {
		log_printf_worker_fn (NULL, &log_data);	
	}

	pthread_mutex_unlock (&logsys_new_log_mutex);
	if (config_mutex_state == LOGSYS_CONFIG_MUTEX_UNLOCKED) {
		pthread_mutex_unlock (&logsys_config_mutex);
	}
	return;

drop_log_msg:
	dropped_log_entries++;
	pthread_mutex_unlock (&logsys_new_log_mutex);
	if (config_mutex_state == LOGSYS_CONFIG_MUTEX_UNLOCKED) {
		pthread_mutex_unlock (&logsys_config_mutex);
	}
}

unsigned int _logsys_subsys_create (
	const char *subsys,
	unsigned int priority)
{
	assert (subsys != NULL);

	return logsys_config_subsys_set (
		subsys,
		LOGSYS_TAG_LOG,
		priority);
}


void logsys_config_mode_set (unsigned int mode)
{
	pthread_mutex_lock (&logsys_config_mutex);
	logsys_mode = mode;
	if (mode & LOG_MODE_FLUSH_AFTER_CONFIG) {
		_logsys_wthread_create ();
		logsys_buffer_flush ();
	}
	pthread_mutex_unlock (&logsys_config_mutex);
}

unsigned int logsys_config_mode_get (void)
{
	return logsys_mode;
}

int logsys_config_file_set (char **error_string, char *file)
{
	static char error_string_response[512];

	if (file == NULL) {
		return (0);
	}

	pthread_mutex_lock (&logsys_new_log_mutex);
	pthread_mutex_lock (&logsys_config_mutex);

	if (logsys_mode & LOG_MODE_OUTPUT_FILE) {
		logsys_file = file;
		if (logsys_file_fp != NULL) {
			fclose (logsys_file_fp);
		}
		logsys_file_fp = fopen (file, "a+");
		if (logsys_file_fp == 0) {
			sprintf (error_string_response,
				"Can't open logfile '%s' for reason (%s).\n",
					 file, strerror (errno));
			*error_string = error_string_response;
			pthread_mutex_unlock (&logsys_config_mutex);
			pthread_mutex_unlock (&logsys_new_log_mutex);
			return (-1);
		}
	}

	pthread_mutex_unlock (&logsys_config_mutex);
	pthread_mutex_unlock (&logsys_new_log_mutex);
	return (0);
}

void logsys_config_facility_set (char *name, unsigned int facility)
{
	pthread_mutex_lock (&logsys_new_log_mutex);
	pthread_mutex_lock (&logsys_config_mutex);

	logsys_name = name;
	logsys_facility = facility;

	pthread_mutex_unlock (&logsys_config_mutex);
	pthread_mutex_unlock (&logsys_new_log_mutex);
}

void _logsys_config_priority_set (unsigned int id, unsigned int priority)
{
	pthread_mutex_lock (&logsys_new_log_mutex);

	logsys_loggers[id].priority = priority;

	if (priority > logsys_loggers[id].priority) {
		logsys_loggers[id].priority = priority;
	}

	pthread_mutex_unlock (&logsys_new_log_mutex);
}

static void child_cleanup (void)
{
	memset(&log_thread_group, 0, sizeof(log_thread_group));
	logsys_wthread_active = 0;
	pthread_mutex_init(&logsys_config_mutex, NULL);
	pthread_mutex_init(&logsys_new_log_mutex, NULL);
}

int _logsys_wthread_create (void)
{
	worker_thread_group_init (
		&log_thread_group,
		1,
		1024,
		sizeof (struct log_data),
		0,
		NULL,
		log_printf_worker_fn);

	logsys_flush();

	atexit (logsys_atexit);
	pthread_atfork(NULL, NULL, child_cleanup);

	if (logsys_mode & LOG_MODE_OUTPUT_SYSLOG_THREADED && logsys_name != NULL) {
		openlog (logsys_name, LOG_CONS|LOG_PID, logsys_facility);
	}

	logsys_wthread_active = 1;

	return (0);
}

void logsys_log_printf (
	char *file,
	int line,
	int priority,
	char *format,
	...)
{
	int id = LOG_ID(priority);
	int level = LOG_LEVEL(priority);
	va_list ap;

	assert (id < MAX_LOGGERS);

	if (LOG_LEVEL(priority) > logsys_loggers[id].priority) {
		return;
	}

	va_start (ap, format);
	_log_printf (LOGSYS_CONFIG_MUTEX_UNLOCKED, file, line, level, id,
		format, ap);
	va_end(ap);
}

static void logsys_log_printf_locked (
	char *file,
	int line,
	int priority,
	char *format,
	...)
{
	int id = LOG_ID(priority);
	int level = LOG_LEVEL(priority);
	va_list ap;

	assert (id < MAX_LOGGERS);

	if (LOG_LEVEL(priority) > logsys_loggers[id].priority) {
		return;
	}

	va_start (ap, format);
	_log_printf (LOGSYS_CONFIG_MUTEX_LOCKED, file, line, level, id,
		format, ap);
	va_end(ap);
}

void _logsys_log_printf2 (
	char *file,
	int line,
	int priority,
	int id,
	char *format, ...)
{
	va_list ap;

	assert (id < MAX_LOGGERS);

	va_start (ap, format);
	_log_printf (LOGSYS_CONFIG_MUTEX_UNLOCKED, file, line, priority, id,
		format, ap);
	va_end(ap);
}

void _logsys_trace (char *file, int line, int tag, int id, char *format, ...)
{
	assert (id < MAX_LOGGERS);

	pthread_mutex_lock (&logsys_config_mutex);

	if (tag & logsys_loggers[id].tags) {
		va_list ap;

		va_start (ap, format);
		_log_printf (LOGSYS_CONFIG_MUTEX_LOCKED, file, line,
			LOG_LEVEL_DEBUG, id, format, ap);
		va_end(ap);
	}
	pthread_mutex_unlock (&logsys_config_mutex);
}

static void logsys_atexit (void)
{
	if (logsys_wthread_active) {
		worker_thread_group_wait (&log_thread_group);
	}
	if (logsys_mode & LOG_MODE_OUTPUT_SYSLOG_THREADED) {
		closelog ();
	}
}

static void logsys_buffer_flush (void)
{
	struct log_entry *entry = head;
	struct log_entry *tmp;

	if (logsys_mode & LOG_MODE_FLUSH_AFTER_CONFIG) {
		logsys_mode &= ~LOG_MODE_FLUSH_AFTER_CONFIG;

		while (entry) {
			logsys_log_printf_locked (
				entry->file,
				entry->line,
				entry->priority,
				entry->str);
			tmp = entry;
			entry = entry->next;
			free (tmp);
		}
	}

	head = tail = NULL;
}

void logsys_flush (void)
{
	worker_thread_group_wait (&log_thread_group);
}
