/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2012 Red Hat, Inc.
 *
 * Author: Steven Dake (sdake@redhat.com)
 * Author: Lon Hohberger (lhh@redhat.com)
 * Author: Fabio M. Di Nitto (fdinitto@redhat.com)
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

#include <config.h>

#include <stdint.h>
#include <ctype.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

#include <corosync/list.h>
#include <corosync/logsys.h>

/*
 * syslog prioritynames, facility names to value mapping
 * Some C libraries build this in to their headers, but it is non-portable
 * so logsys supplies its own version.
 */
struct syslog_names {
	const char *c_name;
	int c_val;
};

static struct syslog_names prioritynames[] =
{
	{ "alert", LOG_ALERT },
	{ "crit", LOG_CRIT },
	{ "debug", LOG_DEBUG },
	{ "emerg", LOG_EMERG },
	{ "err", LOG_ERR },
	{ "error", LOG_ERR },
	{ "info", LOG_INFO },
	{ "notice", LOG_NOTICE },
	{ "warning", LOG_WARNING },
	{ NULL, -1 }
};

#define MAX_FILES_PER_SUBSYS 32
#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define IPC_LOGSYS_SIZE			8192*64
#else
#define IPC_LOGSYS_SIZE			8192*1024
#endif

/*
 * need unlogical order to preserve 64bit alignment
 */
struct logsys_logger {
	char subsys[LOGSYS_MAX_SUBSYS_NAMELEN];	/* subsystem name */
	char *logfile;				/* log to file */
	unsigned int mode;			/* subsystem mode */
	unsigned int debug;			/* debug on|off|trace */
	int syslog_priority;			/* priority */
	int logfile_priority;			/* priority to file */
	int init_status;			/* internal field to handle init queues
						   for subsystems */
	int32_t target_id;
	char *files[MAX_FILES_PER_SUBSYS];
	int32_t file_idx;
	int32_t dirty;
};

/* values for logsys_logger init_status */
#define LOGSYS_LOGGER_INIT_DONE		0
#define LOGSYS_LOGGER_NEEDS_INIT	1

static int logsys_system_needs_init = LOGSYS_LOGGER_NEEDS_INIT;

static struct logsys_logger logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT + 1];

static pthread_mutex_t logsys_config_mutex = PTHREAD_MUTEX_INITIALIZER;

static int32_t _logsys_config_mode_set_unlocked(int32_t subsysid, uint32_t new_mode);
static void _logsys_config_apply_per_file(int32_t s, const char *filename);
static void _logsys_config_apply_per_subsys(int32_t s);
static void _logsys_subsys_filename_add (int32_t s, const char *filename);
static void logsys_file_format_get(char* file_format, int buf_len);

static char *format_buffer=NULL;

static int logsys_thread_started = 0;

static int _logsys_config_subsys_get_unlocked (const char *subsys)
{
	unsigned int i;

	if (!subsys) {
		return LOGSYS_MAX_SUBSYS_COUNT;
	}

	for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (strcmp (logsys_loggers[i].subsys, subsys) == 0) {
			return i;
		}
	}

	return (-1);
}


/*
 * we need a version that can work when somebody else is already
 * holding a config mutex lock or we will never get out of here
 */
static int logsys_config_file_set_unlocked (
		int subsysid,
		const char **error_string,
		const char *file)
{
	static char error_string_response[512];
	int i;
	char file_format[128];

	if (logsys_loggers[subsysid].target_id > 0) {
		int32_t f;
		for (f = 0; f < logsys_loggers[subsysid].file_idx; f++) {
			qb_log_filter_ctl(logsys_loggers[subsysid].target_id,
				QB_LOG_FILTER_REMOVE,
				QB_LOG_FILTER_FILE,
				logsys_loggers[subsysid].files[f],
				LOG_TRACE);
		}
	}

	logsys_loggers[subsysid].dirty = QB_TRUE;
	if (file == NULL) {
		return (0);
	}

	if (logsys_loggers[subsysid].target_id > 0 &&
	    logsys_loggers[subsysid].logfile != NULL &&
	    strcmp(file, logsys_loggers[subsysid].logfile) == 0) {
		return (0);
	}

	if (strlen(file) >= PATH_MAX) {
		snprintf (error_string_response,
			sizeof(error_string_response),
			"%s: logfile name exceed maximum system filename length",
			logsys_loggers[subsysid].subsys);
		*error_string = error_string_response;
		return (-1);
	}

	if (logsys_loggers[subsysid].logfile != NULL) {
		free(logsys_loggers[subsysid].logfile);
		logsys_loggers[subsysid].logfile = NULL;
	}

	logsys_loggers[subsysid].logfile = strdup(file);

	if (logsys_loggers[subsysid].logfile == NULL) {
		snprintf (error_string_response,
			sizeof(error_string_response),
			"Unable to allocate memory for logfile '%s'",
			file);
		*error_string = error_string_response;
		return (-1);
	}

	for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((logsys_loggers[i].logfile != NULL) &&
		    (strcmp (logsys_loggers[i].logfile, file) == 0) &&
		    (i != subsysid)) {
			/* we have found another subsys with this config file
			 * so add a filter
			 */
			logsys_loggers[subsysid].target_id = logsys_loggers[i].target_id;
			return (0);
		}
	}

	if (logsys_loggers[subsysid].target_id > 0) {
		int num_using_current = 0;
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			if (logsys_loggers[subsysid].target_id ==
				logsys_loggers[i].target_id) {
				num_using_current++;
			}
		}
		if (num_using_current == 1) {
			/* no one else is using this close it */
			qb_log_file_close(logsys_loggers[subsysid].target_id);
		}
	}

	logsys_loggers[subsysid].target_id = qb_log_file_open(file);
	if (logsys_loggers[subsysid].target_id < 0) {
		int err = -logsys_loggers[subsysid].target_id;
		char error_str[LOGSYS_MAX_PERROR_MSG_LEN];
		const char *error_ptr;
		error_ptr = qb_strerror_r(err, error_str, sizeof(error_str));

		free(logsys_loggers[subsysid].logfile);
		logsys_loggers[subsysid].logfile = NULL;
		snprintf (error_string_response,
			sizeof(error_string_response),
			"Can't open logfile '%s' for reason: %s (%d)",
			 file, error_ptr, err);
		*error_string = error_string_response;
		return (-1);
	}
	logsys_file_format_get(file_format, 128);
	qb_log_format_set(logsys_loggers[subsysid].target_id, file_format);

	qb_log_ctl(logsys_loggers[subsysid].target_id,
		   QB_LOG_CONF_ENABLED,
		   (logsys_loggers[subsysid].mode & LOGSYS_MODE_OUTPUT_FILE));
	if (logsys_thread_started) {
		qb_log_ctl(logsys_loggers[subsysid].target_id, QB_LOG_CONF_THREADED, QB_TRUE);
	}

	return (0);
}

static void logsys_subsys_init (
		const char *subsys,
		int subsysid)
{
	if (logsys_system_needs_init == LOGSYS_LOGGER_NEEDS_INIT) {
		logsys_loggers[subsysid].init_status =
			LOGSYS_LOGGER_NEEDS_INIT;
	} else {
		logsys_loggers[subsysid].mode = logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].mode;
		logsys_loggers[subsysid].debug = logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].debug;
		logsys_loggers[subsysid].syslog_priority = logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].syslog_priority;
		logsys_loggers[subsysid].logfile_priority = logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].logfile_priority;
		logsys_loggers[subsysid].init_status = LOGSYS_LOGGER_INIT_DONE;
	}
	strncpy (logsys_loggers[subsysid].subsys, subsys,
		sizeof (logsys_loggers[subsysid].subsys));
	logsys_loggers[subsysid].subsys[
		sizeof (logsys_loggers[subsysid].subsys) - 1] = '\0';
	logsys_loggers[subsysid].file_idx = 0;
}

static const char *_logsys_tags_stringify(uint32_t tags)
{
	if (tags == QB_LOG_TAG_LIBQB_MSG) {
		return "QB";
	} else {
		return logsys_loggers[tags].subsys;
	}
}

void logsys_system_fini (void)
{
	int i;
	int f;
	for (i = 0; i < LOGSYS_MAX_SUBSYS_COUNT; i++) {
		free(logsys_loggers[i].logfile);
		for (f = 0; f < logsys_loggers[i].file_idx; f++) {
			free(logsys_loggers[i].files[f]);
		}
	}

	qb_log_fini ();
}

/*
 * Internal API - exported
 */

int _logsys_system_setup(
	const char *mainsystem,
	unsigned int mode,
	int syslog_facility,
	int syslog_priority)
{
	int i;
	int32_t fidx;
	char tempsubsys[LOGSYS_MAX_SUBSYS_NAMELEN];
	int blackbox_enable_res;

	if ((mainsystem == NULL) ||
	    (strlen(mainsystem) >= LOGSYS_MAX_SUBSYS_NAMELEN)) {
		return -1;
	}

	/*
	 * Setup libqb as a subsys
	 */
	i = _logsys_subsys_create ("QB", "array.c,log.c,log_syslog.c,log_blackbox.c,log_format.c,"
		"log_file.c,log_dcs.c,log_thread.c,ipc_shm.c,ipcs.c,ipc_us.c,loop.c,"
		"loop_poll_epoll.c,loop_job.c,loop_poll_poll.c,loop_poll_kqueue.c,"
		"loop_timerlist.c,loop_poll.c,ringbuffer.c,ringbuffer_helper.c,trie.c,"
		"map.c,skiplist.c,rpl_sem.c,hdb.c,unix.c,hashtable.c,strlcpy.c,ipc_socket.c,"
		"strchrnul.c,ipc_setup.c,strlcat.c");
	if (i < 0) {
		return -1;
	}

	/*
	 * name clash
	 * _logsys_subsys_filename_add (i, "util.c");
	 */

	/*
	 * This file (logsys.c) is not exactly QB. We need tag for logsys.c if flightrecorder init
	 * fails, and QB seems to be closest.
	 */
	_logsys_subsys_filename_add (i, "logsys.c");

	i = LOGSYS_MAX_SUBSYS_COUNT;

	pthread_mutex_lock (&logsys_config_mutex);

	snprintf(logsys_loggers[i].subsys,
		 LOGSYS_MAX_SUBSYS_NAMELEN,
		"%s", mainsystem);

	logsys_loggers[i].mode = mode;
	logsys_loggers[i].debug = LOGSYS_DEBUG_OFF;
	logsys_loggers[i].file_idx = 0;
	logsys_loggers[i].logfile_priority = syslog_priority;
	logsys_loggers[i].syslog_priority = syslog_priority;

	qb_log_init(mainsystem, syslog_facility, syslog_priority);
	if (logsys_loggers[i].mode & LOGSYS_MODE_OUTPUT_STDERR) {
		qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	} else {
		qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_FALSE);
	}
	if (logsys_loggers[i].mode & LOGSYS_MODE_OUTPUT_SYSLOG) {
		qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_TRUE);
	} else {
		qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	}
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, LOG_INFO - LOG_DEBUG);

	qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, IPC_LOGSYS_SIZE);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_THREADED, QB_FALSE);
	blackbox_enable_res = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);

	if (logsys_format_set(NULL) == -1) {
		return -1;
	}

	qb_log_tags_stringify_fn_set(_logsys_tags_stringify);

	logsys_loggers[i].init_status = LOGSYS_LOGGER_INIT_DONE;
	logsys_system_needs_init = LOGSYS_LOGGER_INIT_DONE;

	for (i = 0; i < LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((strcmp (logsys_loggers[i].subsys, "") != 0) &&
		    (logsys_loggers[i].init_status ==
		     LOGSYS_LOGGER_NEEDS_INIT)) {
			fidx = logsys_loggers[i].file_idx;
			strncpy (tempsubsys, logsys_loggers[i].subsys,
				 sizeof (tempsubsys));
			tempsubsys[sizeof (tempsubsys) - 1] = '\0';
			logsys_subsys_init(tempsubsys, i);
			logsys_loggers[i].file_idx = fidx;
			_logsys_config_mode_set_unlocked(i, logsys_loggers[i].mode);
			_logsys_config_apply_per_subsys(i);
		}
	}

	if (blackbox_enable_res < 0) {
		LOGSYS_PERROR (-blackbox_enable_res, LOGSYS_LEVEL_WARNING,
		    "Unable to initialize log flight recorder. "\
		    "The most common cause of this error is " \
		    "not enough space on /dev/shm. Corosync will continue work, " \
		    "but blackbox will not be available");
	}

	pthread_mutex_unlock (&logsys_config_mutex);

	return (0);
}


static void _logsys_subsys_filename_add (int32_t s, const char *filename)
{
	int i;

	if (filename == NULL) {
		return;
	}
	assert(logsys_loggers[s].file_idx < MAX_FILES_PER_SUBSYS);
	assert(logsys_loggers[s].file_idx >= 0);

	for (i = 0; i < logsys_loggers[s].file_idx; i++) {
		if (strcmp(logsys_loggers[s].files[i], filename) == 0) {
			return;
		}
	}
	logsys_loggers[s].files[logsys_loggers[s].file_idx++] = strdup(filename);

	if (logsys_system_needs_init == LOGSYS_LOGGER_INIT_DONE) {
		_logsys_config_apply_per_file(s, filename);
	}
}

int _logsys_subsys_create (const char *subsys, const char *filename)
{
	int i;

	if ((subsys == NULL) ||
	    (strlen(subsys) >= LOGSYS_MAX_SUBSYS_NAMELEN)) {
		return -1;
	}

	pthread_mutex_lock (&logsys_config_mutex);

	i = _logsys_config_subsys_get_unlocked (subsys);
	if ((i > -1) && (i < LOGSYS_MAX_SUBSYS_COUNT)) {
		_logsys_subsys_filename_add(i, filename);
		pthread_mutex_unlock (&logsys_config_mutex);
		return i;
	}

	for (i = 0; i < LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (strcmp (logsys_loggers[i].subsys, "") == 0) {
			logsys_subsys_init(subsys, i);
			_logsys_subsys_filename_add(i, filename);
			break;
		}
	}

	if (i >= LOGSYS_MAX_SUBSYS_COUNT) {
		i = -1;
	}

	pthread_mutex_unlock (&logsys_config_mutex);
	return i;
}

int _logsys_config_subsys_get (const char *subsys)
{
	unsigned int i;

	pthread_mutex_lock (&logsys_config_mutex);

	i = _logsys_config_subsys_get_unlocked (subsys);

	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
}

static int32_t _logsys_config_mode_set_unlocked(int32_t subsysid, uint32_t new_mode)
{
	if ( logsys_loggers[subsysid].mode == new_mode) {
		return 0;
	}
	if (logsys_loggers[subsysid].target_id > 0) {
		qb_log_ctl(logsys_loggers[subsysid].target_id,
			   QB_LOG_CONF_ENABLED,
			   (new_mode & LOGSYS_MODE_OUTPUT_FILE));
	}

	if (subsysid == LOGSYS_MAX_SUBSYS_COUNT) {
		qb_log_ctl(QB_LOG_STDERR,
			   QB_LOG_CONF_ENABLED,
			   (new_mode & LOGSYS_MODE_OUTPUT_STDERR));
		qb_log_ctl(QB_LOG_SYSLOG,
			   QB_LOG_CONF_ENABLED,
			   (new_mode & LOGSYS_MODE_OUTPUT_SYSLOG));
	}
	logsys_loggers[subsysid].mode = new_mode;
	return 0;
}

int logsys_config_mode_set (const char *subsys, unsigned int mode)
{
	int i;

	pthread_mutex_lock (&logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked (subsys);
		if (i >= 0) {
			i = _logsys_config_mode_set_unlocked(i, mode);
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			_logsys_config_mode_set_unlocked(i, mode);
		}
		i = 0;
	}

	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
}

unsigned int logsys_config_mode_get (const char *subsys)
{
	int i;

	i = _logsys_config_subsys_get (subsys);
	if (i < 0) {
		return i;
	}

	return logsys_loggers[i].mode;
}

int logsys_config_file_set (
		const char *subsys,
		const char **error_string,
		const char *file)
{
	int i;
	int res;

	pthread_mutex_lock (&logsys_config_mutex);

	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked (subsys);
		if (i < 0) {
			res = i;
		} else {
			res = logsys_config_file_set_unlocked(i, error_string, file);
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			res = logsys_config_file_set_unlocked(i, error_string, file);
			if (res < 0) {
				break;
			}
		}
	}

	pthread_mutex_unlock (&logsys_config_mutex);
	return res;
}

static void
logsys_file_format_get(char* file_format, int buf_len)
{
	char *per_t;
	file_format[0] = '\0';
	per_t = strstr(format_buffer, "%t");
	if (per_t) {
		strcpy(file_format, "%t [%P] %H %N");
		per_t += 2;
		strncat(file_format, per_t, buf_len - strlen("%t [%P] %H %N"));
	} else {
		strcpy(file_format, "[%P] %H %N");
		strncat(file_format, format_buffer, buf_len - strlen("[%P] %H %N"));
	}
}

int logsys_format_set (const char *format)
{
	int i;
	int c;
	int w;
	int reminder;
	char syslog_format[128];
	char file_format[128];

	if (format_buffer) {
		free(format_buffer);
		format_buffer = NULL;
	}

	format_buffer = strdup(format ? format : "%7p [%6g] %b");
	if (format_buffer == NULL) {
		return -1;
	}

	qb_log_format_set(QB_LOG_STDERR, format_buffer);

	logsys_file_format_get(file_format, 128);
	for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (logsys_loggers[i].target_id > 0) {
			qb_log_format_set(logsys_loggers[i].target_id, file_format);
		}
	}

	/*
	 * This just goes through and remove %t and %p from
	 * the format string for syslog.
	 */
	w = 0;
	memset(syslog_format, '\0', sizeof(syslog_format));
	for (c = 0; c < strlen(format_buffer); c++) {
		if (format_buffer[c] == '%') {
			reminder = c;
			for (c++; c < strlen(format_buffer); c++) {
				if (isdigit(format_buffer[c])) {
					continue;
				}
				if (format_buffer[c] == 't' ||
				    format_buffer[c] == 'p') {
					c++;
				} else {
					c = reminder;
				}
				break;
			}
		}
		syslog_format[w] = format_buffer[c];
		w++;
	}
	qb_log_format_set(QB_LOG_SYSLOG, syslog_format);

	return 0;
}

char *logsys_format_get (void)
{
	return format_buffer;
}

int logsys_config_syslog_facility_set (
	const char *subsys,
	unsigned int facility)
{
	return qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_FACILITY, facility);
}

int logsys_config_syslog_priority_set (
	const char *subsys,
	unsigned int priority)
{
	int i;

	pthread_mutex_lock (&logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked (subsys);
		if (i >= 0) {
			logsys_loggers[i].syslog_priority = priority;
			logsys_loggers[i].dirty = QB_TRUE;

			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].syslog_priority = priority;
			logsys_loggers[i].dirty = QB_TRUE;
		}
		i = 0;
	}
	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
}

int logsys_config_logfile_priority_set (
	const char *subsys,
	unsigned int priority)
{
	int i;

	pthread_mutex_lock (&logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked (subsys);
		if (i >= 0) {
			logsys_loggers[i].logfile_priority = priority;
			logsys_loggers[i].dirty = QB_TRUE;
			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].logfile_priority = priority;
			logsys_loggers[i].dirty = QB_TRUE;
		}
		i = 0;
	}
	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
}


static void _logsys_config_apply_per_file(int32_t s, const char *filename)
{
	uint32_t syslog_priority = logsys_loggers[s].syslog_priority;
	uint32_t logfile_priority = logsys_loggers[s].logfile_priority;

	qb_log_filter_ctl(s, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE,
			  filename, LOG_TRACE);

	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_REMOVE,
			  QB_LOG_FILTER_FILE, filename, LOG_TRACE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_REMOVE,
			  QB_LOG_FILTER_FILE, filename, LOG_TRACE);
	if (logsys_loggers[s].target_id > 0) {
		qb_log_filter_ctl(logsys_loggers[s].target_id,
			QB_LOG_FILTER_REMOVE,
			QB_LOG_FILTER_FILE, filename, LOG_TRACE);
	}

	if (logsys_loggers[s].debug != LOGSYS_DEBUG_OFF) {
		switch (logsys_loggers[s].debug) {
		case LOGSYS_DEBUG_ON:
			syslog_priority = LOG_DEBUG;
			logfile_priority = LOG_DEBUG;
			break;
		case LOGSYS_DEBUG_TRACE:
			syslog_priority = LOG_TRACE;
			logfile_priority = LOG_TRACE;
			break;
		default:
			assert(0);
		}
	}
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
		QB_LOG_FILTER_FILE, filename,
		syslog_priority);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
		QB_LOG_FILTER_FILE, filename,
		logfile_priority);
	if (logsys_loggers[s].target_id > 0) {
		qb_log_filter_ctl(logsys_loggers[s].target_id,
			QB_LOG_FILTER_ADD,
			QB_LOG_FILTER_FILE, filename,
			logfile_priority);
	}
}

static void _logsys_config_apply_per_subsys(int32_t s)
{
	int32_t f;
	for (f = 0; f < logsys_loggers[s].file_idx; f++) {
		_logsys_config_apply_per_file(s, logsys_loggers[s].files[f]);
	}
	if (logsys_loggers[s].target_id > 0) {
		qb_log_ctl(logsys_loggers[s].target_id,
			QB_LOG_CONF_ENABLED,
			(logsys_loggers[s].mode & LOGSYS_MODE_OUTPUT_FILE));
	}
	logsys_loggers[s].dirty = QB_FALSE;
}

void logsys_config_apply(void)
{
	int32_t s;

	for (s = 0; s <= LOGSYS_MAX_SUBSYS_COUNT; s++) {
		if (strcmp(logsys_loggers[s].subsys, "") == 0) {
			continue;
		}
		_logsys_config_apply_per_subsys(s);
	}
}

int logsys_config_debug_set (
	const char *subsys,
	unsigned int debug)
{
	int i;

	pthread_mutex_lock (&logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked (subsys);
		if (i >= 0) {
			logsys_loggers[i].dirty = QB_TRUE;
			logsys_loggers[i].debug = debug;
			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].debug = debug;
			logsys_loggers[i].dirty = QB_TRUE;
		}
		i = 0;
	}
	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
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

int logsys_thread_start (void)
{
	int i;
	int err;

	err = qb_log_thread_start();
	if (err != 0) {
		return (err);
	}

	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_THREADED, QB_TRUE);
	for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (logsys_loggers[i].target_id > 0) {
			qb_log_ctl(logsys_loggers[i].target_id, QB_LOG_CONF_THREADED, QB_TRUE);
		}
	}

	logsys_thread_started = 1;

	return (0);
}
