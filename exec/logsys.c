/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2010 Red Hat, Inc.
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
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(COROSYNC_LINUX)
#include <linux/un.h>
#endif
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
#include <sys/un.h>
#endif
#include <syslog.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <sys/mman.h>
#include <semaphore.h>

#include <corosync/list.h>
#include <corosync/engine/logsys.h>

#include "util.h"

#define YIELD_AFTER_LOG_OPS 10

#define MIN(x,y) ((x) < (y) ? (x) : (y))

#define ROUNDUP(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

/*
 * syslog prioritynames, facility names to value mapping
 * Some C libraries build this in to their headers, but it is non-portable
 * so logsys supplies its own version.
 */
struct syslog_names {
	const char *c_name;
	int c_val;
};

struct syslog_names prioritynames[] =
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

struct syslog_names facilitynames[] =
{
	{ "auth", LOG_AUTH },
	{ "cron", LOG_CRON },
	{ "daemon", LOG_DAEMON },
	{ "kern", LOG_KERN },
	{ "lpr", LOG_LPR },
	{ "mail", LOG_MAIL },
	{ "news", LOG_NEWS },
	{ "syslog", LOG_SYSLOG },
	{ "user", LOG_USER },
	{ "uucp", LOG_UUCP },
	{ "local0", LOG_LOCAL0 },
	{ "local1", LOG_LOCAL1 },
	{ "local2", LOG_LOCAL2 },
	{ "local3", LOG_LOCAL3 },
	{ "local4", LOG_LOCAL4 },
	{ "local5", LOG_LOCAL5 },
	{ "local6", LOG_LOCAL6 },
	{ "local7", LOG_LOCAL7 },
	{ NULL, -1 }
};

struct record {
	unsigned int rec_ident;
	const char *file_name;
	const char *function_name;
	int file_line;
	char *buffer;
	struct list_head list;
};
	
/*
 * need unlogical order to preserve 64bit alignment
 */
struct logsys_logger {
	char subsys[LOGSYS_MAX_SUBSYS_NAMELEN];	/* subsystem name */
	char *logfile;				/* log to file */
	FILE *logfile_fp;			/* track file descriptor */
	unsigned int mode;			/* subsystem mode */
	unsigned int debug;			/* debug on|off */
	int syslog_facility;			/* facility */
	int syslog_priority;			/* priority */
	int logfile_priority;			/* priority to file */
	int init_status;			/* internal field to handle init queues
						   for subsystems */
	unsigned int trace1_allowed;
};


/*
 * These are not static so they can be read from the core file
 */
int *flt_data;

uint32_t flt_head;

uint32_t flt_tail;

unsigned int flt_data_size;

#define COMBINE_BUFFER_SIZE 2048

/* values for logsys_logger init_status */
#define LOGSYS_LOGGER_INIT_DONE		0
#define LOGSYS_LOGGER_NEEDS_INIT	1

static int logsys_system_needs_init = LOGSYS_LOGGER_NEEDS_INIT;

static int logsys_memory_used = 0;

static int logsys_sched_param_queued = 0;

static int logsys_sched_policy;

static struct sched_param logsys_sched_param;

static int logsys_after_log_ops_yield = 10;

static struct logsys_logger logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT + 1];

static int wthread_active = 0;

static int wthread_should_exit = 0;

static pthread_mutex_t logsys_config_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int records_written = 1;

static pthread_t logsys_thread_id;

static sem_t logsys_thread_start;

static sem_t logsys_print_finished;

static pthread_mutex_t logsys_flt_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t logsys_wthread_mutex = PTHREAD_MUTEX_INITIALIZER;

static int logsys_buffer_full = 0;

static char *format_buffer=NULL;

static int logsys_dropped_messages = 0;

void *logsys_rec_end;

static pid_t startup_pid = 0;

static DECLARE_LIST_INIT(logsys_print_finished_records);

#define FDMAX_ARGS	64

#define CIRCULAR_BUFFER_WRITE_SIZE	64

/* forward declarations */
static void logsys_close_logfile(int subsysid);

static uint32_t circular_memory_map (void **buf, size_t bytes)
{
	void *addr_orig;
	void *addr;
	int fd;
	int res;
	const char *file = "fdata-XXXXXX";
	char path[PATH_MAX];
	char buffer[CIRCULAR_BUFFER_WRITE_SIZE];
	int i;
	int written;
	int error_return = 0;

	snprintf (path, PATH_MAX, "/dev/shm/%s", file);

	fd = mkstemp (path);
	if (fd == -1) {
		snprintf (path, PATH_MAX, LOCALSTATEDIR "/run/%s", file);
		fd = mkstemp (path);
		if (fd == -1) {
			error_return = -1;
			goto error_exit;
		}
	}

	/*
	 * ftruncate doesn't return ENOSPC
	 * have to use write to determine if shared memory is actually available
	 */
	res = ftruncate (fd, 0);
	if (res == -1) {
		error_return = -1;
		goto unlink_exit;
	}
	memset (buffer, 0, sizeof (buffer));
	for (i = 0; i < (bytes / CIRCULAR_BUFFER_WRITE_SIZE); i++) {
retry_write:
		written = write (fd, buffer, CIRCULAR_BUFFER_WRITE_SIZE);
		if (written == -1 && errno == EINTR) {
			goto retry_write;
		}
		if (written != 64) {
			error_return = -1;
			goto unlink_exit;
		}
	}
	
	addr_orig = mmap (NULL, bytes << 1, PROT_NONE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (addr_orig == MAP_FAILED) {
		error_return = -1;
		goto unlink_exit;
	}

	addr = mmap (addr_orig, bytes, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_FIXED, fd, 0);
	if (addr != addr_orig) {
		error_return = -1;
		goto mmap_exit;
	}
	#if (defined COROSYNC_BSD && defined MADV_NOSYNC)
		madvise(addr_orig, bytes, MADV_NOSYNC);
	#endif

	addr = mmap (((char *)addr_orig) + bytes,
		  bytes, PROT_READ | PROT_WRITE,
		  MAP_SHARED | MAP_FIXED, fd, 0);
	if ((char *)addr != (char *)((char *)addr_orig + bytes)) {
		error_return = -1;
		goto mmap_exit;
	}
#if (defined COROSYNC_BSD && defined MADV_NOSYNC)
	madvise(((char *)addr_orig) + bytes, bytes, MADV_NOSYNC);
#endif

	*buf = addr_orig;
	error_return = 0;
	goto unlink_exit;

mmap_exit:
	munmap (addr_orig, bytes << 1);
unlink_exit:
	unlink (path);
	close (fd);
error_exit:
	return (error_return);
}

static void logsys_flt_lock (void)
{
	pthread_mutex_lock (&logsys_flt_mutex);
}
static void logsys_flt_unlock (void)
{
	pthread_mutex_unlock (&logsys_flt_mutex);
}

static void logsys_wthread_lock (void)
{
	pthread_mutex_lock (&logsys_wthread_mutex);
}
static void logsys_wthread_unlock (void)
{
	pthread_mutex_unlock (&logsys_wthread_mutex);
}

/*
 * Before any write operation, a reclaim on the buffer area must be executed
 */
static inline void records_reclaim (unsigned int idx, unsigned int words)
{
	unsigned int should_reclaim;

	should_reclaim = 0;

	if ((idx + words) >= flt_data_size) {
		logsys_buffer_full = 1;
	}
	if (logsys_buffer_full == 0) {
		return;
	}

	if (flt_tail > flt_head) {
		if (idx + words >= flt_tail) {
			should_reclaim = 1;
		}
	} else {
		if ((idx + words) >= (flt_tail + flt_data_size)) {
			should_reclaim = 1;
		}
	}

	if (should_reclaim) {
		int words_needed = 0;

		words_needed = words + 1;
		do {
			unsigned int old_tail;

			words_needed -= flt_data[flt_tail];
			old_tail = flt_tail;
			flt_tail =
				(flt_tail +
				flt_data[flt_tail]) % (flt_data_size);
		} while (words_needed > 0);
	}
}

#define idx_word_step(idx)						\
do {									\
	if (idx > (flt_data_size - 1)) {				\
		idx = 0;						\
	}								\
} while (0);

#define idx_buffer_step(idx)						\
do {									\
	if (idx > (flt_data_size - 1)) {				\
		idx = ((idx) % (flt_data_size));			\
	}								\
} while (0);

/*
 * Internal threaded logging implementation
 */
static inline int strcpy_cutoff (char *dest, const char *src, size_t cutoff,
				 size_t buf_len)
{
	size_t len = strlen (src);
	if (buf_len <= 1) {
		if (buf_len == 0)
			dest[0] = 0;
		return 0;
	}

	if (cutoff == 0) {
		cutoff = len;
	}

	cutoff = MIN (cutoff, buf_len - 1);
	len = MIN (len, cutoff);
	memcpy (dest, src, len);
	memset (dest + len, ' ', cutoff - len);
	dest[cutoff] = '\0';

	return (cutoff);
}

static const char log_month_name[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/*
 * %s SUBSYSTEM
 * %n FUNCTION NAME
 * %f FILENAME
 * %l FILELINE
 * %p PRIORITY
 * %t TIMESTAMP
 * %b BUFFER
 *
 * any number between % and character specify field length to pad or chop
*/
static void log_printf_to_logs (
	unsigned int rec_ident,
	const char *file_name,
	const char *function_name,
	int file_line,
	const char *buffer)
{
	char normal_output_buffer[COMBINE_BUFFER_SIZE];
	char syslog_output_buffer[COMBINE_BUFFER_SIZE];
	char char_time[128];
	char line_no[30];
	unsigned int format_buffer_idx = 0;
	unsigned int normal_output_buffer_idx = 0;
	unsigned int syslog_output_buffer_idx = 0;
	struct timeval tv;
	size_t cutoff;
	unsigned int normal_len, syslog_len;
	int subsysid;
	unsigned int level;
	int c;
	struct tm tm_res;

	subsysid = LOGSYS_DECODE_SUBSYSID(rec_ident);
	level = LOGSYS_DECODE_LEVEL(rec_ident);

	if (!((LOGSYS_DECODE_RECID(rec_ident) == LOGSYS_RECID_LOG) ||
	      (logsys_loggers[subsysid].trace1_allowed && LOGSYS_DECODE_RECID(rec_ident) == LOGSYS_RECID_TRACE1))) {
		return;
	}

	pthread_mutex_lock (&logsys_config_mutex);

	while ((c = format_buffer[format_buffer_idx])) {
		cutoff = 0;
		if (c != '%') {
			normal_output_buffer[normal_output_buffer_idx++] = c;
			syslog_output_buffer[syslog_output_buffer_idx++] = c;
			format_buffer_idx++;
		} else {
			const char *normal_p, *syslog_p;

			format_buffer_idx += 1;
			if (isdigit (format_buffer[format_buffer_idx])) {
				cutoff = atoi (&format_buffer[format_buffer_idx]);
			}
			while (isdigit (format_buffer[format_buffer_idx])) {
				format_buffer_idx += 1;
			}

			switch (format_buffer[format_buffer_idx]) {
				case 's':
					normal_p = logsys_loggers[subsysid].subsys;
					syslog_p = logsys_loggers[subsysid].subsys;
					break;

				case 'n':
					normal_p = function_name;
					syslog_p = function_name;
					break;

				case 'f':
					normal_p = file_name;
					syslog_p = file_name;
					break;

				case 'l':
					snprintf (line_no, sizeof (line_no), "%d", file_line);
					normal_p = line_no;
					syslog_p = line_no;
					break;

				case 't':
					gettimeofday (&tv, NULL);
					(void)localtime_r ((time_t *)&tv.tv_sec, &tm_res);
					snprintf (char_time, sizeof (char_time), "%s %02d %02d:%02d:%02d",
					    log_month_name[tm_res.tm_mon], tm_res.tm_mday, tm_res.tm_hour,
					    tm_res.tm_min, tm_res.tm_sec);
					normal_p = char_time;

					/*
					 * syslog does timestamping on its own.
					 * also strip extra space in case.
					 */
					syslog_p = "";
					break;

				case 'b':
					normal_p = buffer;
					syslog_p = buffer;
					break;

				case 'p':
					normal_p = logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].subsys;
					syslog_p = "";
					break;

				default:
					normal_p = "";
					syslog_p = "";
					break;
			}
			normal_len = strcpy_cutoff (normal_output_buffer + normal_output_buffer_idx,
					     normal_p, cutoff,
					     (sizeof (normal_output_buffer)
					      - normal_output_buffer_idx));
			normal_output_buffer_idx += normal_len;
			syslog_len = strcpy_cutoff (syslog_output_buffer + syslog_output_buffer_idx,
					     syslog_p, cutoff,
					     (sizeof (syslog_output_buffer)
					      - syslog_output_buffer_idx));
			syslog_output_buffer_idx += syslog_len;
			format_buffer_idx += 1;
		}
		if ((normal_output_buffer_idx >= sizeof (normal_output_buffer) - 2) ||
		    (syslog_output_buffer_idx >= sizeof (syslog_output_buffer) - 1)) {
			/* Note: we make allowance for '\0' at the end of
			 * both of these arrays and normal_output_buffer also
			 * needs a '\n'.
			 */
			break;
		}
	}

	pthread_mutex_unlock (&logsys_config_mutex);

	normal_output_buffer[normal_output_buffer_idx] = '\0';
	syslog_output_buffer[syslog_output_buffer_idx] = '\0';

	/*
	 * Output to syslog
	 */
	if ((logsys_loggers[subsysid].mode & LOGSYS_MODE_OUTPUT_SYSLOG) &&
	     ((level <= logsys_loggers[subsysid].syslog_priority) ||
	     (logsys_loggers[subsysid].debug != 0))) {
		syslog (level | logsys_loggers[subsysid].syslog_facility, "%s", syslog_output_buffer);
	}

	/*
	 * Terminate string with \n \0
	 */
	normal_output_buffer[normal_output_buffer_idx++] = '\n';
	normal_output_buffer[normal_output_buffer_idx] = '\0';

	/*
	 * Output to configured file
	 */
	if (((logsys_loggers[subsysid].mode & LOGSYS_MODE_OUTPUT_FILE) &&
	     (logsys_loggers[subsysid].logfile_fp != NULL)) &&
	    ((level <= logsys_loggers[subsysid].logfile_priority) ||
	     (logsys_loggers[subsysid].debug != 0))) {
		/*
		 * Output to a file
		 */
		if ((fwrite (normal_output_buffer, strlen (normal_output_buffer), 1,
			    logsys_loggers[subsysid].logfile_fp) < 1) ||
		    (fflush (logsys_loggers[subsysid].logfile_fp) == EOF)) {
			char tmpbuffer[1024];
			/*
			 * if we are here, it's bad.. it's really really bad.
			 * Best thing would be to light a candle in a church
			 * and pray.
			 */
			snprintf(tmpbuffer, sizeof(tmpbuffer),
				"LOGSYS EMERGENCY: %s Unable to write to %s.",
				logsys_loggers[subsysid].subsys,
				logsys_loggers[subsysid].logfile);
			pthread_mutex_lock (&logsys_config_mutex);
			logsys_close_logfile(subsysid);
			logsys_loggers[subsysid].mode &= ~LOGSYS_MODE_OUTPUT_FILE;
			pthread_mutex_unlock (&logsys_config_mutex);
			log_printf_to_logs(
					   LOGSYS_ENCODE_RECID(
						LOGSYS_LEVEL_EMERG,
						subsysid,
						LOGSYS_RECID_LOG),
					   __FILE__, __FUNCTION__, __LINE__,
					   tmpbuffer);
		}
	}

	/*
	 * Output to stderr
	 */
	if ((logsys_loggers[subsysid].mode & LOGSYS_MODE_OUTPUT_STDERR) &&
	     ((level <= logsys_loggers[subsysid].logfile_priority) ||
	     (logsys_loggers[subsysid].debug != 0))) {
		if (write (STDERR_FILENO, normal_output_buffer, strlen (normal_output_buffer)) < 0) {
			char tmpbuffer[1024];
			/*
			 * if we are here, it's bad.. it's really really bad.
			 * Best thing would be to light 20 candles for each saint
			 * in the calendar and pray a lot...
			 */
			pthread_mutex_lock (&logsys_config_mutex);
			logsys_loggers[subsysid].mode &= ~LOGSYS_MODE_OUTPUT_STDERR;
			pthread_mutex_unlock (&logsys_config_mutex);
			snprintf(tmpbuffer, sizeof(tmpbuffer),
				"LOGSYS EMERGENCY: %s Unable to write to STDERR.",
				logsys_loggers[subsysid].subsys);
			log_printf_to_logs(
					   LOGSYS_ENCODE_RECID(
						LOGSYS_LEVEL_EMERG,
						subsysid,
						LOGSYS_RECID_LOG),
					   __FILE__, __FUNCTION__, __LINE__,
					   tmpbuffer);
		}
	}
}

static void log_printf_to_logs_wthread (
	unsigned int rec_ident,
	const char *file_name,
	const char *function_name,
	int file_line,
	const char *buffer)
{
	struct record *rec;
	uint32_t length;

	rec = malloc (sizeof (struct record));
	if (rec == NULL) {
		return;
	}
	
	length = strlen (buffer);

	rec->rec_ident = rec_ident;
	rec->file_name = file_name;
	rec->function_name = function_name;
	rec->file_line = file_line;
	rec->buffer = malloc (length + 1);
	if (rec->buffer == NULL) {
		free (rec);
		return;
	}
	memcpy (rec->buffer, buffer, length + 1);
	
	list_init (&rec->list);
	logsys_wthread_lock();
	logsys_memory_used += length + 1 + sizeof (struct record);
	if (logsys_memory_used > 512000) {
		free (rec->buffer);
		free (rec);
		logsys_memory_used = logsys_memory_used - length - 1 - sizeof (struct record);
		logsys_dropped_messages += 1;
		logsys_wthread_unlock();
		return;
		
	} else {
		list_add_tail (&rec->list, &logsys_print_finished_records);
	}
	logsys_wthread_unlock();

	sem_post (&logsys_print_finished);
}

static void *logsys_worker_thread (void *data) __attribute__((noreturn));
static void *logsys_worker_thread (void *data)
{
	struct record *rec;
	int dropped = 0;
	int res;

	/*
	 * Signal wthread_create that the initialization process may continue
	 */
	sem_post (&logsys_thread_start);
	for (;;) {
		dropped = 0;
retry_sem_wait:
		res = sem_wait (&logsys_print_finished);
		if (res == -1 && errno == EINTR) {
			goto retry_sem_wait;
		} else
		if (res == -1) {
			/*
  			 * This case shouldn't happen
  			 */
			pthread_exit (NULL);
		}
		

		logsys_wthread_lock();
		if (wthread_should_exit) {
			int value;

			res = sem_getvalue (&logsys_print_finished, &value);
			if (value == 0) {
				logsys_wthread_unlock();
				pthread_exit (NULL);
			}
		}

		rec = list_entry (logsys_print_finished_records.next, struct record, list);
		list_del (&rec->list);
		logsys_memory_used = logsys_memory_used - strlen (rec->buffer) -
			sizeof (struct record) - 1;
		dropped = logsys_dropped_messages;
		logsys_dropped_messages = 0;
		logsys_wthread_unlock();
		if (dropped) {
			printf ("%d messages lost\n", dropped);
		}
		log_printf_to_logs (
			rec->rec_ident,
			rec->file_name,
			rec->function_name,
			rec->file_line,
			rec->buffer);
		free (rec->buffer);
		free (rec);
	}
}

static void wthread_create (void)
{
	int res;

	if (wthread_active) {
		return;
	}

	wthread_active = 1;


	/*
	 * TODO: propagate pthread_create errors back to the caller
	 */
	res = pthread_create (&logsys_thread_id, NULL,
		logsys_worker_thread, NULL);
	sem_wait (&logsys_thread_start);

	if (res == 0) {
		if (logsys_sched_param_queued == 1) {
			/*
			 * TODO: propagate logsys_thread_priority_set errors back to
			 * the caller
			 */
			res = logsys_thread_priority_set (
				logsys_sched_policy,
				&logsys_sched_param,
				logsys_after_log_ops_yield);
			logsys_sched_param_queued = 0;
		}
	} else {
		wthread_active = 0;
	}
}

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

static void syslog_facility_reconf (void)
{
	closelog();
	openlog(logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].subsys,
		LOG_CONS|LOG_PID,
		logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].syslog_facility);
}

/*
 * this is always invoked within the mutex, so it's safe to parse the
 * whole thing as we need.
 */
static void logsys_close_logfile (
	int subsysid)
{
	int i;

	if ((logsys_loggers[subsysid].logfile_fp == NULL) &&
	    (logsys_loggers[subsysid].logfile == NULL)) {
		return;
	}

	/*
	 * if there is another subsystem or system using the same fp,
	 * then we clean our own structs, but we can't close the file
	 * as it is in use by somebody else.
	 * Only the last users will be allowed to perform the fclose.
	 */
 	for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((logsys_loggers[i].logfile_fp == logsys_loggers[subsysid].logfile_fp) &&
		    (i != subsysid)) {
			logsys_loggers[subsysid].logfile = NULL;
			logsys_loggers[subsysid].logfile_fp = NULL;
			return;
		}
	}

	/*
	 * if we are here, we are the last users of that fp, so we can safely
	 * close it.
	 */
	fclose (logsys_loggers[subsysid].logfile_fp);
	logsys_loggers[subsysid].logfile_fp = NULL;
	free (logsys_loggers[subsysid].logfile);
	logsys_loggers[subsysid].logfile = NULL;
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

	logsys_close_logfile(subsysid);

	if ((file == NULL) ||
	    (strcmp(logsys_loggers[subsysid].subsys, "") == 0)) {
		return (0);
	}

	if (strlen(file) >= PATH_MAX) {
		snprintf (error_string_response,
			sizeof(error_string_response),
			"%s: logfile name exceed maximum system filename lenght\n",
			logsys_loggers[subsysid].subsys);
		*error_string = error_string_response;
		return (-1);
	}

	for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((logsys_loggers[i].logfile != NULL) &&
			(strcmp (logsys_loggers[i].logfile, file) == 0) &&
			(i != subsysid)) {
				logsys_loggers[subsysid].logfile =
					logsys_loggers[i].logfile;
				logsys_loggers[subsysid].logfile_fp =
					logsys_loggers[i].logfile_fp;
				return (0);
		}
	}

	logsys_loggers[subsysid].logfile = strdup(file);
	if (logsys_loggers[subsysid].logfile == NULL) {
		snprintf (error_string_response,
			sizeof(error_string_response),
			"Unable to allocate memory for logfile '%s'\n",
			file);
		*error_string = error_string_response;
		return (-1);
	}

	logsys_loggers[subsysid].logfile_fp = fopen (file, "a+");
	if (logsys_loggers[subsysid].logfile_fp == NULL) {
		int err;
		char error_str[LOGSYS_MAX_PERROR_MSG_LEN];
		const char *error_ptr;

		err = errno;
#ifdef COROSYNC_LINUX
		/* The GNU version of strerror_r returns a (char*) that *must* be used */
		error_ptr = strerror_r(err, error_str, sizeof(error_str));
#else
		/* The XSI-compliant strerror_r() return 0 or -1 (in case the buffer is full) */
		if ( strerror_r(err, error_str, sizeof(error_str)) < 0 )
			error_ptr = "";
		else
			error_ptr = error_str;
#endif

		free(logsys_loggers[subsysid].logfile);
		logsys_loggers[subsysid].logfile = NULL;
		snprintf (error_string_response,
			sizeof(error_string_response),
			"Can't open logfile '%s' for reason: %s (%d).\n",
				 file, error_ptr, err);
		*error_string = error_string_response;
		return (-1);
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
		memcpy(&logsys_loggers[subsysid],
		       &logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT],
		       sizeof(logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT]));
		logsys_loggers[subsysid].init_status =
			LOGSYS_LOGGER_INIT_DONE;
	}
	strncpy (logsys_loggers[subsysid].subsys, subsys,
		sizeof (logsys_loggers[subsysid].subsys));
	logsys_loggers[subsysid].subsys[
		sizeof (logsys_loggers[subsysid].subsys) - 1] = '\0';
}

/*
 * Internal API - exported
 */

int _logsys_system_setup(
	const char *mainsystem,
	unsigned int mode,
	unsigned int debug,
	const char *logfile,
	int logfile_priority,
	int syslog_facility,
	int syslog_priority)
{
	int i;
	const char *errstr;
	char tempsubsys[LOGSYS_MAX_SUBSYS_NAMELEN];

	if ((mainsystem == NULL) ||
	    (strlen(mainsystem) >= LOGSYS_MAX_SUBSYS_NAMELEN)) {
		return -1;
	}

	i = LOGSYS_MAX_SUBSYS_COUNT;

	pthread_mutex_lock (&logsys_config_mutex);

	snprintf(logsys_loggers[i].subsys,
		 LOGSYS_MAX_SUBSYS_NAMELEN,
		"%s", mainsystem);

	logsys_loggers[i].mode = mode;

	logsys_loggers[i].debug = debug;
	logsys_loggers[i].trace1_allowed = 0;

	if (logsys_config_file_set_unlocked (i, &errstr, logfile) < 0) {
		pthread_mutex_unlock (&logsys_config_mutex);
		return (-1);
	}
	logsys_loggers[i].logfile_priority = logfile_priority;

	logsys_loggers[i].syslog_facility = syslog_facility;
	logsys_loggers[i].syslog_priority = syslog_priority;
	syslog_facility_reconf();

	logsys_loggers[i].init_status = LOGSYS_LOGGER_INIT_DONE;

	logsys_system_needs_init = LOGSYS_LOGGER_INIT_DONE;

	for (i = 0; i < LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((strcmp (logsys_loggers[i].subsys, "") != 0) &&
			(logsys_loggers[i].init_status ==
			 LOGSYS_LOGGER_NEEDS_INIT)) {
				strncpy (tempsubsys, logsys_loggers[i].subsys,
					sizeof (tempsubsys));
				tempsubsys[sizeof (tempsubsys) - 1] = '\0';
				logsys_subsys_init(tempsubsys, i);
		}
	}

	pthread_mutex_unlock (&logsys_config_mutex);

	return (0);
}

int _logsys_subsys_create (const char *subsys)
{
	int i;

	if ((subsys == NULL) ||
	    (strlen(subsys) >= LOGSYS_MAX_SUBSYS_NAMELEN)) {
		return -1;
	}

	pthread_mutex_lock (&logsys_config_mutex);

	i = _logsys_config_subsys_get_unlocked (subsys);
	if ((i > -1) && (i < LOGSYS_MAX_SUBSYS_COUNT)) {
		pthread_mutex_unlock (&logsys_config_mutex);
		return i;
	}

	for (i = 0; i < LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (strcmp (logsys_loggers[i].subsys, "") == 0) {
			logsys_subsys_init(subsys, i);
			break;
		}
	}

	if (i >= LOGSYS_MAX_SUBSYS_COUNT) {
		i = -1;
	}

	pthread_mutex_unlock (&logsys_config_mutex);
	return i;
}

int _logsys_wthread_create (void)
{
	if (((logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].mode & LOGSYS_MODE_FORK) == 0) &&
		((logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].mode & LOGSYS_MODE_THREADED) != 0)) {
		wthread_create();
	}
	return (0);
}

int _logsys_rec_init (unsigned int fltsize)
{
	size_t flt_real_size;
	int res;

	sem_init (&logsys_thread_start, 0, 0);

	sem_init (&logsys_print_finished, 0, 0);

	/*
	 * XXX: kill me for 1.1 because I am a dirty hack
	 * temporary workaround that will be replaced by supporting
	 * 0 byte size flight recorder buffer.
	 * 0 byte size buffer will enable direct printing to logs
	 *   without flight recoder.
	 */
	if (fltsize < 64000) {
		fltsize = 64000;
	}

	flt_real_size = ROUNDUP(fltsize, sysconf(_SC_PAGESIZE)) * 4;

	res = circular_memory_map ((void **)&flt_data, flt_real_size);
	if (res == -1) {
		sem_destroy (&logsys_thread_start);
		sem_destroy (&logsys_print_finished);

		return (-1);
	}

	memset (flt_data, 0, flt_real_size * 2);
	/*
	 * flt_data_size tracks data by ints and not bytes/chars.
	 */

	flt_data_size = flt_real_size / sizeof (uint32_t);

	/*
	 * First record starts at zero
	 * Last record ends at zero
	 */
	flt_head = 0;
	flt_tail = 0;

	return (0);
}


/*
 * u32 RECORD SIZE
 * u32 record ident
 * u64 time_t timestamp
 * u32 arg count
 * u32 file line
 * u32 subsys length
 * buffer null terminated subsys
 * u32 filename length
 * buffer null terminated filename
 * u32 filename length
 * buffer null terminated function
 * u32 arg1 length
 * buffer arg1
 * ... repeats length & arg
 */

void _logsys_log_rec (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	...)
{
	va_list ap;
	const void *buf_args[FDMAX_ARGS];
	unsigned int buf_len[FDMAX_ARGS];
	unsigned int i;
	unsigned int idx;
	unsigned int arguments = 0;
	unsigned int record_reclaim_size = 0;
	unsigned int index_start;
	struct timeval the_timeval;
	int words_written;
	int subsysid;

	subsysid = LOGSYS_DECODE_SUBSYSID(rec_ident);

	/*
	 * Decode VA Args
	 */
	va_start (ap, file_line);
	arguments = 3;
	for (;;) {
		buf_args[arguments] = va_arg (ap, void *);
		if (buf_args[arguments] == LOGSYS_REC_END) {
			break;
		}
		buf_len[arguments] = va_arg (ap, int);
		record_reclaim_size += ((buf_len[arguments] + 3) >> 2) + 1;
		arguments++;
		if (arguments >= FDMAX_ARGS) {
			break;
		}
	}
	va_end (ap);

	/*
	 * Encode logsys subsystem identity, filename, and function
	 */
	buf_args[0] = logsys_loggers[subsysid].subsys;
	buf_len[0] = strlen (logsys_loggers[subsysid].subsys) + 1;
	buf_args[1] = file_name;
	buf_len[1] = strlen (file_name) + 1;
	buf_args[2] = function_name;
	buf_len[2] = strlen (function_name) + 1;
	for (i = 0; i < 3; i++) {
		record_reclaim_size += ((buf_len[i] + 3) >> 2) + 1;
	}

	logsys_flt_lock();
	idx = flt_head;
	index_start = idx;

	/*
	 * Reclaim data needed for record including 6 words for the header
	 */
	records_reclaim (idx, record_reclaim_size + 6);

	/*
	 * Write record size of zero and rest of header information
	 */
	flt_data[idx++] = 0;
	idx_word_step(idx);

	flt_data[idx++] = rec_ident;
	idx_word_step(idx);

	flt_data[idx++] = file_line;
	idx_word_step(idx);

	flt_data[idx++] = records_written;
	idx_word_step(idx);

	gettimeofday(&the_timeval, NULL);
	flt_data[idx++] = the_timeval.tv_sec & 0xFFFFFFFF;
	idx_word_step(idx);

	flt_data[idx++] = the_timeval.tv_sec >> 32;
	idx_word_step(idx);

	/*
	 * Encode all of the arguments into the log message
	 */
	for (i = 0; i < arguments; i++) {
		unsigned int bytes;
		unsigned int total_words;

		bytes = buf_len[i];
		total_words = (bytes + 3) >> 2;

		flt_data[idx++] = total_words;
		idx_word_step(idx);

		memcpy (&flt_data[idx], buf_args[i], buf_len[i]);

		idx += total_words;
		idx_buffer_step (idx);

	}
	words_written = idx - index_start;
	if (words_written < 0) {
		words_written += flt_data_size;
	}
	/*
	 * Commit the write of the record size now that the full record
	 * is in the memory buffer
	 */
	flt_data[index_start] = words_written;

	flt_head = idx;
	logsys_flt_unlock();
	records_written++;
}

void _logsys_log_vprintf (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	const char *format,
	va_list ap)
{
	char logsys_print_buffer[COMBINE_BUFFER_SIZE];
	unsigned int len;
	unsigned int level;
	int subsysid;
	const char *short_file_name;

	subsysid = LOGSYS_DECODE_SUBSYSID(rec_ident);
	level = LOGSYS_DECODE_LEVEL(rec_ident);

	len = vsnprintf (logsys_print_buffer, sizeof (logsys_print_buffer), format, ap);
	if (logsys_print_buffer[len - 1] == '\n') {
		logsys_print_buffer[len - 1] = '\0';
		len -= 1;
	}
#ifdef BUILDING_IN_PLACE
	short_file_name = file_name;
#else
	short_file_name = strrchr (file_name, '/');
	if (short_file_name == NULL)
		short_file_name = file_name;
	else
		short_file_name++; /* move past the "/" */
#endif /* BUILDING_IN_PLACE */

	if (startup_pid == 0 || startup_pid == getpid()) {
		/*
		 * Create a log record if we are really true corosync
		 * process (not fork of some service) or if we didn't finished
		 * initialization yet.
		 */
		_logsys_log_rec (
			rec_ident,
			function_name,
			short_file_name,
			file_line,
			logsys_print_buffer, len + 1,
			LOGSYS_REC_END);
	}

	/*
	 * If logsys is not going to print a message to a log target don't
	 * queue one
	 */
	if ((level > logsys_loggers[subsysid].syslog_priority && 
		level > logsys_loggers[subsysid].logfile_priority &&
			logsys_loggers[subsysid].debug == 0) ||

		(level == LOGSYS_LEVEL_DEBUG &&
			logsys_loggers[subsysid].debug == 0)) {

		return;
	}

	if ((logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].mode & LOGSYS_MODE_THREADED) == 0) {
		/*
		 * Output (and block) if the log mode is not threaded otherwise
		 * expect the worker thread to output the log data once signaled
		 */
		log_printf_to_logs (rec_ident,
			short_file_name,
			function_name,
			file_line,
			logsys_print_buffer);
	} else {
		/*
		 * Signal worker thread to display logging output
		 */
		log_printf_to_logs_wthread (rec_ident,
			short_file_name,
			function_name,
			file_line,
			logsys_print_buffer);
	}
}

void _logsys_log_printf (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	const char *format,
	...)
{
	va_list ap;

	va_start (ap, format);
	_logsys_log_vprintf (rec_ident, function_name, file_name, file_line,
		format, ap);
	va_end (ap);
}

int _logsys_config_subsys_get (const char *subsys)
{
	unsigned int i;

	pthread_mutex_lock (&logsys_config_mutex);

	i = _logsys_config_subsys_get_unlocked (subsys);

	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
}

/*
 * External Configuration and Initialization API
 */
void logsys_fork_completed (void)
{
	logsys_loggers[LOGSYS_MAX_SUBSYS_COUNT].mode &= ~LOGSYS_MODE_FORK;
	startup_pid = getpid();

	(void)_logsys_wthread_create ();
}

int logsys_config_mode_set (const char *subsys, unsigned int mode)
{
	int i;

	pthread_mutex_lock (&logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked (subsys);
		if (i >= 0) {
			logsys_loggers[i].mode = mode;
			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].mode = mode;
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

int logsys_format_set (const char *format)
{
	int ret = 0;

	pthread_mutex_lock (&logsys_config_mutex);

	if (format_buffer) {
		free(format_buffer);
		format_buffer = NULL;
	}

	format_buffer = strdup(format ? format : "%p [%6s] %b");
	if (format_buffer == NULL) {
		ret = -1;
	}

	pthread_mutex_unlock (&logsys_config_mutex);
	return ret;
}

char *logsys_format_get (void)
{
	return format_buffer;
}

int logsys_config_syslog_facility_set (
	const char *subsys,
	unsigned int facility)
{
	int i;

	pthread_mutex_lock (&logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked (subsys);
		if (i >= 0) {
			logsys_loggers[i].syslog_facility = facility;
			if (i == LOGSYS_MAX_SUBSYS_COUNT) {
				syslog_facility_reconf();
			}
			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].syslog_facility = facility;
		}
		syslog_facility_reconf();
		i = 0;
	}
	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
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
			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].syslog_priority = priority;
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
			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].logfile_priority = priority;
		}
		i = 0;
	}
	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
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
			switch (debug) {
			case LOGSYS_DEBUG_OFF:
			case LOGSYS_DEBUG_ON:
				logsys_loggers[i].debug = debug;
				logsys_loggers[i].trace1_allowed = 0;
				break;
			case LOGSYS_DEBUG_TRACE:
				logsys_loggers[i].debug = LOGSYS_DEBUG_ON;
				logsys_loggers[i].trace1_allowed = 1;
				break;
			}
			i = 0;
		}
	} else {
		for (i = 0; i <= LOGSYS_MAX_SUBSYS_COUNT; i++) {
			switch (debug) {
			case LOGSYS_DEBUG_OFF:
			case LOGSYS_DEBUG_ON:
				logsys_loggers[i].debug = debug;
				logsys_loggers[i].trace1_allowed = 0;
				break;
			case LOGSYS_DEBUG_TRACE:
				logsys_loggers[i].debug = LOGSYS_DEBUG_ON;
				logsys_loggers[i].trace1_allowed = 1;
				break;
			}
		}
		i = 0;
	}
	pthread_mutex_unlock (&logsys_config_mutex);

	return i;
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

const char *logsys_facility_name_get (unsigned int facility)
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

const char *logsys_priority_name_get (unsigned int priority)
{
	unsigned int i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (priority == prioritynames[i].c_val) {
			return (prioritynames[i].c_name);
		}
	}
	return (NULL);
}

int logsys_thread_priority_set (
	int policy,
	const struct sched_param *param,
        unsigned int after_log_ops_yield)

{
	int res = 0;
	if (param == NULL) {
		return (0);
	}

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && defined(HAVE_SCHED_GET_PRIORITY_MAX)
	if (wthread_active == 0) {
		logsys_sched_policy = policy;
		memcpy(&logsys_sched_param, param, sizeof(struct sched_param));
		logsys_sched_param_queued = 1;
	} else {
		res = pthread_setschedparam (logsys_thread_id, policy, param);
	}
#endif

	if (after_log_ops_yield > 0) {
		logsys_after_log_ops_yield = after_log_ops_yield;
	}

	return (res);
}

int logsys_log_rec_store (const char *filename)
{
	int fd;
	ssize_t written_size = 0;
	size_t this_write_size;
	uint32_t header_magic=0xFFFFDABB; /* Flight Data Black Box */

	fd = open (filename, O_CREAT|O_RDWR, 0700);
	if (fd < 0) {
		return (-1);
	}

	logsys_flt_lock();

	/* write a header that tells corosync-fplay this is a new-format
	 * flightdata file, with timestamps. The header word has the top
	 * bit set which makes it larger than any older fdata file so
	 * that the tool can recognise old or new files.
	 */
	this_write_size = write (fd, &header_magic, sizeof(uint32_t));
	if (this_write_size != sizeof(unsigned int)) {
		goto error_exit;
	}
	written_size += this_write_size;

	this_write_size = write (fd, &flt_data_size, sizeof(uint32_t));
	if (this_write_size != sizeof(unsigned int)) {
		goto error_exit;
	}
	written_size += this_write_size;

	this_write_size = write (fd, flt_data, flt_data_size * sizeof (uint32_t));
	if (this_write_size != (flt_data_size * sizeof(uint32_t))) {
		goto error_exit;
	}
	written_size += this_write_size;

	this_write_size = write (fd, &flt_head, sizeof (uint32_t));
	if (this_write_size != (sizeof(uint32_t))) {
		goto error_exit;
	}
	written_size += this_write_size;
	this_write_size = write (fd, &flt_tail, sizeof (uint32_t));
	if (this_write_size != (sizeof(uint32_t))) {
		goto error_exit;
	}
	written_size += this_write_size;
	if (written_size != ((flt_data_size + 3) * sizeof (uint32_t))) { 
		goto error_exit;
	}

	logsys_flt_unlock();
	close (fd);
	return (0);

error_exit:
	logsys_flt_unlock();
	close (fd);
	return (-1);
}

void logsys_atexit (void)
{
	int res;
	int value;
	struct record *rec;

	if (wthread_active == 0) {
		for (;;) {
			logsys_wthread_lock();

			res = sem_getvalue (&logsys_print_finished, &value);
			if (value == 0) {
				logsys_wthread_unlock();
				return;
			}
			sem_wait (&logsys_print_finished);

			rec = list_entry (logsys_print_finished_records.next, struct record, list);
			list_del (&rec->list);
			logsys_memory_used = logsys_memory_used - strlen (rec->buffer) -
				sizeof (struct record) - 1;
			logsys_wthread_unlock();
			log_printf_to_logs (
				rec->rec_ident,
				rec->file_name,
				rec->function_name,
				rec->file_line,
				rec->buffer);
			free (rec->buffer);
			free (rec);
		}
	} else {
		wthread_should_exit = 1;
		sem_post (&logsys_print_finished);
		pthread_join (logsys_thread_id, NULL);
	}
}

void logsys_flush (void)
{
}
