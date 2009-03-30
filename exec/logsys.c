/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
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

#include <config.h>

#include <assert.h>
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
#define SYSLOG_NAMES
#include <syslog.h>
#include <stdlib.h>
#include <pthread.h>

#include <corosync/engine/logsys.h>

/* similar to syslog facilities/priorities tables,
 * make a tag table for internal use
 */

#ifdef SYSLOG_NAMES
struct syslog_names {
	const char *c_name;
	int c_val;
};

struct syslog_names tagnames[] =
  {
    { "log", LOGSYS_TAG_LOG },
    { "enter", LOGSYS_TAG_ENTER },
    { "leave", LOGSYS_TAG_LEAVE },
    { "trace1", LOGSYS_TAG_TRACE1 },
    { "trace2", LOGSYS_TAG_TRACE2 },
    { "trace3", LOGSYS_TAG_TRACE3 },
    { "trace4", LOGSYS_TAG_TRACE4 },
    { "trace5", LOGSYS_TAG_TRACE5 },
    { "trace6", LOGSYS_TAG_TRACE6 },
    { "trace7", LOGSYS_TAG_TRACE7 },
    { "trace8", LOGSYS_TAG_TRACE8 },
    { NULL, -1 }
  };
#endif

/*
 * These are not static so they can be read from the core file
 */
int *flt_data;

int flt_data_size;

#define SUBSYS_MAX 32

#define COMBINE_BUFFER_SIZE 2048

struct logsys_logger {
	char subsys[64];
	unsigned int priority;
	unsigned int tags;
	unsigned int mode;
};

/*
 * Configuration parameters for logging system
 */
static const char *logsys_name = NULL;

static unsigned int logsys_mode = LOG_MODE_NOSUBSYS;

static const char *logsys_file = NULL;

static FILE *logsys_file_fp = NULL;

static int logsys_facility = LOG_DAEMON;

/*
 * operating global variables
 */
static struct logsys_logger logsys_loggers[SUBSYS_MAX];

static int wthread_active = 0;

static int wthread_should_exit = 0;

static pthread_mutex_t logsys_config_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int records_written = 1;

static pthread_t logsys_thread_id;

static pthread_cond_t logsys_cond;

static pthread_mutex_t logsys_cond_mutex;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
static pthread_spinlock_t logsys_idx_spinlock;
#else
static pthread_mutex_t logsys_idx_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static unsigned int log_rec_idx;

static int logsys_buffer_full = 0;

static char *format_buffer=NULL;

static int log_requests_pending = 0;

static int log_requests_lost = 0;

void *logsys_rec_end;

#define FDHEAD_INDEX	(flt_data_size)

#define FDTAIL_INDEX 	(flt_data_size + 1)

struct log_data {
	unsigned int syslog_pos;
	unsigned int priority;
	char *log_string;
};

static void logsys_atexit (void);

/*
 * Helpers for _logsys_log_rec functionality
 */
static inline void my_memcpy_32bit (int *dest, const int *src, unsigned int words)
{
	unsigned int word_idx;
	for (word_idx = 0; word_idx < words; word_idx++) {
		dest[word_idx] = src[word_idx];
	}
}

static inline void my_memcpy_8bit (char *dest, const char *src,
				   unsigned int bytes)
{
	unsigned int byte_idx;

	for (byte_idx = 0; byte_idx < bytes; byte_idx++) {
		dest[byte_idx] = src[byte_idx];
	}
}

#if defined(HAVE_PTHREAD_SPIN_LOCK)
static void logsys_lock (void)
{
	pthread_spin_lock (&logsys_idx_spinlock);
}
static void logsys_unlock (void)
{
	pthread_spin_unlock (&logsys_idx_spinlock);
}
#else
static void logsys_lock (void)
{
	pthread_mutex_lock (&logsys_idx_mutex);
}
static void logsys_unlock (void)
{
	pthread_mutex_unlock (&logsys_idx_mutex);
}
#endif


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

	logsys_lock();
	if (flt_data[FDTAIL_INDEX] > flt_data[FDHEAD_INDEX]) {
		if (idx + words >= flt_data[FDTAIL_INDEX]) {
			should_reclaim = 1;
		}
	} else {
		if ((idx + words) >= (flt_data[FDTAIL_INDEX] + flt_data_size)) {
			should_reclaim = 1;
		}
	}

	if (should_reclaim) {
		int words_needed = 0;

		words_needed = words + 1;
		do {
			unsigned int old_tail;

			words_needed -= flt_data[flt_data[FDTAIL_INDEX]];
			old_tail = flt_data[FDTAIL_INDEX];
			flt_data[FDTAIL_INDEX] = 
				(flt_data[FDTAIL_INDEX] + 
				flt_data[flt_data[FDTAIL_INDEX]]) % (flt_data_size);
			if (log_rec_idx == old_tail) {
				log_requests_lost += 1;
				log_rec_idx = flt_data[FDTAIL_INDEX];
			}
		} while (words_needed > 0);
	}
	logsys_unlock();
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
static inline int strcpy_cutoff (char *dest, const char *src, int cutoff)
{
	unsigned int len;

	if (cutoff == -1) {
		strcpy (dest, src);
		return (strlen (dest));
	} else {
		assert (cutoff > 0);
		strncpy (dest, src, cutoff);
		dest[cutoff] = '\0';
		len = strlen (dest);
		if (len != cutoff) {
			memset (&dest[len], ' ', cutoff - len);
		}
	}
	return (cutoff);
}

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
	const char *subsys,
	const char *function_name,
	const char *file_name,
	int file_line,
	unsigned int level,
	char *buffer)
{
	char output_buffer[COMBINE_BUFFER_SIZE];
	char char_time[128];
	char line_no[30];
	unsigned int format_buffer_idx = 0;
	unsigned int output_buffer_idx = 0;
	struct timeval tv;
	int cutoff;
	unsigned int len;
	
	while (format_buffer[format_buffer_idx]) {
		cutoff = -1;
		if (format_buffer[format_buffer_idx] == '%') {
			format_buffer_idx += 1;
			if (isdigit (format_buffer[format_buffer_idx])) {
				cutoff = atoi (&format_buffer[format_buffer_idx]);
			}
			while (isdigit (format_buffer[format_buffer_idx])) {
				format_buffer_idx += 1;
			}
			
			switch (format_buffer[format_buffer_idx]) {
				case 's':
					len = strcpy_cutoff (&output_buffer[output_buffer_idx], subsys, cutoff);
					output_buffer_idx += len;
					break;

				case 'n':
					len = strcpy_cutoff (&output_buffer[output_buffer_idx], function_name, cutoff);
					output_buffer_idx += len;
					break;

				case 'l':
					sprintf (line_no, "%d", file_line);
					len = strcpy_cutoff (&output_buffer[output_buffer_idx], line_no, cutoff);
					output_buffer_idx += len;
					break;

				case 'p':
					break;

				case 't':
					gettimeofday (&tv, NULL);
					(void)strftime (char_time, sizeof (char_time), "%b %e %k:%M:%S", localtime ((time_t *)&tv.tv_sec));
					len = strcpy_cutoff (&output_buffer[output_buffer_idx], char_time, cutoff);
					output_buffer_idx += len;
					break;

				case 'b':
					len = strcpy_cutoff (&output_buffer[output_buffer_idx], buffer, cutoff);
					output_buffer_idx += len;
					break;
			}
			format_buffer_idx += 1;
		} else {
			output_buffer[output_buffer_idx++] = format_buffer[format_buffer_idx++];
		}
	}

	output_buffer[output_buffer_idx] = '\0';

	/*
	 * Output to syslog
	 */	
	if (logsys_mode & LOG_MODE_OUTPUT_SYSLOG) {
		syslog (level, "%s", output_buffer);
	}

	/*
	 * Terminate string with \n \0
	 */
	if (logsys_mode & (LOG_MODE_OUTPUT_FILE|LOG_MODE_OUTPUT_STDERR)) {
		output_buffer[output_buffer_idx++] = '\n';
		output_buffer[output_buffer_idx] = '\0';
	}

	/*
	 * Output to configured file
	 */	
	if ((logsys_mode & LOG_MODE_OUTPUT_FILE) && logsys_file_fp) {
		/*
		 * Output to a file
		 */
		(void)fwrite (output_buffer, strlen (output_buffer), 1, logsys_file_fp);
		fflush (logsys_file_fp);
	}

	/*
	 * Output to stderr
	 */	
	if (logsys_mode & LOG_MODE_OUTPUT_STDERR) {
		(void)write (STDERR_FILENO, output_buffer, strlen (output_buffer));
	}
}

static void record_print (char *buf)
{
	int *buf_uint32t = (int *)buf;
	unsigned int rec_size = buf_uint32t[0];
	unsigned int rec_ident = buf_uint32t[1];
	unsigned int file_line = buf_uint32t[2];
	unsigned int level = rec_ident >> 28;
	unsigned int i;
	unsigned int words_processed;
	unsigned int arg_size_idx;
	void *arguments[64];
	unsigned int arg_count;

	arg_size_idx = 4;
	words_processed = 4;
	arg_count = 0;

	for (i = 0; words_processed < rec_size; i++) {
		arguments[arg_count++] = &buf_uint32t[arg_size_idx + 1];
		arg_size_idx += buf_uint32t[arg_size_idx] + 1;
		words_processed += buf_uint32t[arg_size_idx] + 1;
	}
	log_printf_to_logs (
		(char *)arguments[0],
		(char *)arguments[1],
		(char *)arguments[2],
		file_line,
		level,
		(char *)arguments[3]);
}
	
static int record_read (char *buf, int rec_idx, int *log_msg) {
        unsigned int rec_size;
        unsigned int rec_ident;
        int firstcopy, secondcopy;

	rec_size = flt_data[rec_idx];
	rec_ident = flt_data[(rec_idx + 1) % flt_data_size];

	/*
	 * Not a log record
	 */
	if ((rec_ident & LOGSYS_TAG_LOG) == 0) {
		*log_msg = 0;
        	return ((rec_idx + rec_size) % flt_data_size);
	}

	/*
	 * A log record
	 */
	*log_msg = 1;

        firstcopy = rec_size;
        secondcopy = 0;
        if (firstcopy + rec_idx > flt_data_size) {
                firstcopy = flt_data_size - rec_idx;
                secondcopy -= firstcopy - rec_size;
        }
        memcpy (&buf[0], &flt_data[rec_idx], firstcopy << 2);
        if (secondcopy) {
                memcpy (&buf[(firstcopy << 2)], &flt_data[0], secondcopy << 2);
        }
        return ((rec_idx + rec_size) % flt_data_size);
}

static inline void wthread_signal (void)
{
	if (wthread_active == 0) {
		return;
	}
	pthread_mutex_lock (&logsys_cond_mutex);
	pthread_cond_signal (&logsys_cond);
	pthread_mutex_unlock (&logsys_cond_mutex);
}

static inline void wthread_wait (void)
{
	pthread_mutex_lock (&logsys_cond_mutex);
	pthread_cond_wait (&logsys_cond, &logsys_cond_mutex);
	pthread_mutex_unlock (&logsys_cond_mutex);
}

static inline void wthread_wait_locked (void)
{
	pthread_cond_wait (&logsys_cond, &logsys_cond_mutex);
	pthread_mutex_unlock (&logsys_cond_mutex);
}

static void *logsys_worker_thread (void *data) __attribute__((__noreturn__));
static void *logsys_worker_thread (void *data)
{
	int log_msg;
	char buf[COMBINE_BUFFER_SIZE];

	/*
	 * Signal wthread_create that the initialization process may continue
	 */
	wthread_signal ();
	logsys_lock();
	log_rec_idx = flt_data[FDTAIL_INDEX];
	logsys_unlock();

	for (;;) {
		wthread_wait ();
		/*
		 * Read and copy the logging record index position
		 * It may have been updated by records_reclaim if
		 * messages were lost or or log_rec on the first new
		 * logging record available
		 */
		/*
		 * Process any pending log messages here
		 */
		for (;;) {
			logsys_lock();
			if (log_requests_lost > 0) {
				printf ("lost %d log requests\n", log_requests_lost);
				log_requests_pending -= log_requests_lost;
				log_requests_lost = 0;
			}
			if (log_requests_pending == 0) {
				logsys_unlock();
				break;
			}
			log_rec_idx = record_read (buf, log_rec_idx, &log_msg);
			if (log_msg) {
				log_requests_pending -= 1;
			}
			logsys_unlock();

			/*
			 * print the stored buffer
			 */
			if (log_msg) {
				record_print (buf);
			}
		}

		if (wthread_should_exit) {
			pthread_exit (NULL);
		}
	}
}

static void wthread_create (void)
{
	int res;

	if (wthread_active) {
		return;
	}

	wthread_active = 1;

	pthread_mutex_init (&logsys_cond_mutex, NULL);
	pthread_cond_init (&logsys_cond, NULL);
	pthread_mutex_lock (&logsys_cond_mutex);
	res = pthread_create (&logsys_thread_id, NULL,
		logsys_worker_thread, NULL);


	/*
	 * Wait for thread to be started
	 */
	wthread_wait_locked ();
}

/*
 * Internal API - exported
 */
void _logsys_nosubsys_set (void)
{
	logsys_mode |= LOG_MODE_NOSUBSYS;
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

int _logsys_wthread_create (void)
{
	if ((logsys_mode & LOG_MODE_FORK) == 0) {
		if (logsys_name != NULL) {
			openlog (logsys_name, LOG_CONS|LOG_PID, logsys_facility);
		}
		wthread_create();
		atexit (logsys_atexit);
	}
	return (0);
}

int _logsys_rec_init (unsigned int size)
{
	/*
	 * First record starts at zero
	 * Last record ends at zero
	 */
	flt_data = malloc ((size + 2) * sizeof (unsigned int));
	assert (flt_data != NULL);
	flt_data_size = size;
	assert (flt_data != NULL);
	flt_data[FDHEAD_INDEX] = 0;
	flt_data[FDTAIL_INDEX] = 0;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spin_init (&logsys_idx_spinlock, 0);
#endif

	return (0);
}


/*
 * u32 RECORD SIZE
 * u32 record ident
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
	int subsys,
	const char *function_name,
	const char *file_name,
	int file_line,
	unsigned int rec_ident,
	...)
{
	va_list ap;
	const void *buf_args[64];
	unsigned int buf_len[64];
	unsigned int i;
	unsigned int idx;
	unsigned int arguments = 0;
	unsigned int record_reclaim_size;
	unsigned int index_start;
	int words_written;

	record_reclaim_size = 0;
		
	/*
	 * Decode VA Args
	 */
	va_start (ap, rec_ident);
	arguments = 3;
	for (;;) {
		assert (arguments < 64);
		buf_args[arguments] = va_arg (ap, void *);
		if (buf_args[arguments] == LOG_REC_END) {
			break;
		}
		buf_len[arguments] = va_arg (ap, int);
		record_reclaim_size += ((buf_len[arguments] + 3) >> 2) + 1;
		arguments++;
	}
	va_end (ap);

	/*
	 * Encode logsys subsystem identity, filename, and function
	 */
	buf_args[0] = logsys_loggers[subsys].subsys;
	buf_len[0] = strlen (logsys_loggers[subsys].subsys) + 1;
	buf_args[1] = file_name;
	buf_len[1] = strlen (file_name) + 1;
	buf_args[2] = function_name;
	buf_len[2] = strlen (function_name) + 1;
	for (i = 0; i < 3; i++) {
		record_reclaim_size += ((buf_len[i] + 3) >> 2) + 1;
	}

	idx = flt_data[FDHEAD_INDEX];
	index_start = idx;

	/*
	 * Reclaim data needed for record including 4 words for the header
	 */
	records_reclaim (idx, record_reclaim_size + 4);

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
	/*
	 * Encode all of the arguments into the log message
	 */
	for (i = 0; i < arguments; i++) {
		unsigned int bytes;
		unsigned int full_words;
		unsigned int total_words;

		bytes = buf_len[i];
		full_words = bytes >> 2;
		total_words = (bytes + 3) >> 2;

		flt_data[idx++] = total_words;
		idx_word_step(idx);

		/*
		 * determine if this is a wrapped write or normal write
		 */
		if (idx + total_words < flt_data_size) {
			/*
			 * dont need to wrap buffer
			 */
			my_memcpy_32bit (&flt_data[idx], buf_args[i], full_words);
			if (bytes % 4) {
				my_memcpy_8bit ((char *)&flt_data[idx + full_words],
					((const char *)buf_args[i]) + (full_words << 2), bytes % 4);
			}
		} else {
			/*
			 * need to wrap buffer
			 */
			unsigned int first;
			unsigned int second;

			first = flt_data_size - idx;
			if (first > full_words) {
				first = full_words;
			}
			second = full_words - first;
			my_memcpy_32bit (&flt_data[idx],
					 (const int *)buf_args[i], first);
			my_memcpy_32bit (&flt_data[0],
				(const int *)(((const unsigned char *)buf_args[i]) + (first << 2)),
				second);
			if (bytes % 4) {
				my_memcpy_8bit ((char *)&flt_data[0 + second],
					((const char *)buf_args[i]) + (full_words << 2), bytes % 4);
			}
		}
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

	/*
	 * If the index of the current head equals the current log_rec_idx, 
	 * and this is not a log_printf operation, set the log_rec_idx to
	 * the new head position and commit the new head.
	 */
	logsys_lock();
	if (rec_ident & LOGSYS_TAG_LOG) {
		log_requests_pending += 1;
	}
	if (log_requests_pending == 0) {
		log_rec_idx = idx;
	}
	flt_data[FDHEAD_INDEX] = idx;
	logsys_unlock();
	records_written++;
}

void _logsys_log_printf (
        int subsys,
        const char *function_name,
        const char *file_name,
        int file_line,
        unsigned int level,
        const char *format,
        ...)
{
	char logsys_print_buffer[COMBINE_BUFFER_SIZE];
	unsigned int len;
	va_list ap;

	if (logsys_mode & LOG_MODE_NOSUBSYS) {
		subsys = 0;
	}
	if (level > logsys_loggers[subsys].priority) {
		return;
	}
	va_start (ap, format);
	len = vsprintf (logsys_print_buffer, format, ap);
	va_end (ap);
	if (logsys_print_buffer[len - 1] == '\n') {
		logsys_print_buffer[len - 1] = '\0';
		len -= 1;
	}

	/*
	 * Create a log record
	 */
	_logsys_log_rec (subsys,
		function_name,
		file_name,
		file_line,
		(level+1) << 28,
		logsys_print_buffer, len + 1,
		LOG_REC_END);

	if ((logsys_mode & LOG_MODE_THREADED) == 0) {
		/*
		 * Output (and block) if the log mode is not threaded otherwise
		 * expect the worker thread to output the log data once signaled
		 */
		log_printf_to_logs (logsys_loggers[subsys].subsys,
			function_name, file_name, file_line, level,
			logsys_print_buffer);
	} else {
		/*
		 * Signal worker thread to display logging output
		 */
		wthread_signal ();
	}
}

/*
 * External Configuration and Initialization API
 */
void logsys_fork_completed (void)
{
	logsys_mode &= ~LOG_MODE_FORK;
	_logsys_wthread_create ();
}

void logsys_config_mode_set (unsigned int mode)
{
	pthread_mutex_lock (&logsys_config_mutex);
	logsys_mode = mode;
	pthread_mutex_unlock (&logsys_config_mutex);
}

unsigned int logsys_config_mode_get (void)
{
	return logsys_mode;
}

static void logsys_close_logfile()
{
	if (logsys_file_fp != NULL) {
		fclose (logsys_file_fp);
		logsys_file_fp = NULL;
	}
}

int logsys_config_file_set (const char **error_string, const char *file)
{
	static char error_string_response[512];

	if (file == NULL) {
		logsys_close_logfile();
		return (0);
	}

	pthread_mutex_lock (&logsys_config_mutex);

	if (logsys_mode & LOG_MODE_OUTPUT_FILE) {
		logsys_file = file;
		logsys_close_logfile();
		logsys_file_fp = fopen (file, "a+");
		if (logsys_file_fp == 0) {
			sprintf (error_string_response,
				"Can't open logfile '%s' for reason (%s).\n",
					 file, strerror (errno));
			*error_string = error_string_response;
			pthread_mutex_unlock (&logsys_config_mutex);
			return (-1);
		}
	} else
		logsys_close_logfile();

	pthread_mutex_unlock (&logsys_config_mutex);
	return (0);
}

void logsys_format_set (char *format)
{
	pthread_mutex_lock (&logsys_config_mutex);

	if (format_buffer) {
		free(format_buffer);
		format_buffer = NULL;
	}

	if (format) {
		format_buffer = strdup(format);
	} else {
		format_buffer = strdup("[%6s] %b");
	}

	pthread_mutex_unlock (&logsys_config_mutex);
}

char *logsys_format_get (void)
{
	return format_buffer;
}

void logsys_config_facility_set (const char *name, unsigned int facility)
{
	pthread_mutex_lock (&logsys_config_mutex);

	logsys_name = name;
	logsys_facility = facility;

	pthread_mutex_unlock (&logsys_config_mutex);
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

int logsys_tag_id_get (const char *name)
{
	unsigned int i;

	for (i = 0; tagnames[i].c_name != NULL; i++) {
		if (strcasecmp(name, tagnames[i].c_name) == 0) {
			return (tagnames[i].c_val);
		}
	}
	return (-1);
}

const char *logsys_tag_name_get (unsigned int tag)
{
	unsigned int i;

	for (i = 0; tagnames[i].c_name != NULL; i++) {
		if (tag == tagnames[i].c_val) {
			return (tagnames[i].c_name);
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
 	for (i = 0; i < SUBSYS_MAX; i++) {
		if (strcmp (logsys_loggers[i].subsys, subsys) == 0) {
			logsys_loggers[i].tags = tags;
			logsys_loggers[i].priority = priority;

			break;
		}
	}

	if (i == SUBSYS_MAX) {
		for (i = 0; i < SUBSYS_MAX; i++) {
			if (strcmp (logsys_loggers[i].subsys, "") == 0) {
				strncpy (logsys_loggers[i].subsys, subsys,
					sizeof(logsys_loggers[i].subsys));
				logsys_loggers[i].tags = tags;
				logsys_loggers[i].priority = priority;
				break;
			}
		}
	}
	assert(i < SUBSYS_MAX);

	pthread_mutex_unlock (&logsys_config_mutex);
	return i;
}

int logsys_config_subsys_get (
	const char *subsys,
	unsigned int *tags,
	unsigned int *priority)
{
	unsigned int i;

	pthread_mutex_lock (&logsys_config_mutex);

 	for (i = 0; i < SUBSYS_MAX; i++) {
		if (strcmp (logsys_loggers[i].subsys, subsys) == 0) {
			*tags = logsys_loggers[i].tags;
			*priority = logsys_loggers[i].priority;
			pthread_mutex_unlock (&logsys_config_mutex);
			return i;
		}
	}

	pthread_mutex_unlock (&logsys_config_mutex);

	return (-1);
}

int logsys_log_rec_store (const char *filename)
{
	int fd;
	ssize_t written_size;
	size_t size_to_write = (flt_data_size + 2) * sizeof (unsigned int);

	fd = open (filename, O_CREAT|O_RDWR, 0700);
	if (fd == -1) {
		return (-1);
	}

	written_size = write (fd, flt_data, size_to_write);
	if (close (fd) != 0)
		return (-1);
	if (written_size < 0) {
		return (-1);
	} else if ((size_t)written_size != size_to_write) {
		return (-1);
	}

	return (0);
}

static void logsys_atexit (void)
{
	if (wthread_active) {
		wthread_should_exit = 1;
		wthread_signal ();
		pthread_join (logsys_thread_id, NULL);
	}
}

void logsys_atsegv (void)
{
	if (wthread_active) {
		wthread_should_exit = 1;
		wthread_signal ();
		pthread_join (logsys_thread_id, NULL);
	}
}

int logsys_init (
	const char *name,
	int mode,
	int facility,
	int priority,
	const char *file,
	char *format,
	int rec_size)
{
	const char *errstr;

	_logsys_nosubsys_set ();
	_logsys_subsys_create (name, priority);
	strncpy (logsys_loggers[0].subsys, name,
		 sizeof (logsys_loggers[0].subsys));
	logsys_config_mode_set (mode);
	logsys_config_facility_set (name, facility);
	logsys_config_file_set (&errstr, file);
	logsys_format_set (format);
	_logsys_rec_init (rec_size);
	_logsys_wthread_create ();
	return (0);
}

int logsys_conf (
	char *name,
	int mode,
	int facility,
	int priority,
	char *file)
{
	const char *errstr;

	_logsys_rec_init (100000);
	strncpy (logsys_loggers[0].subsys, name,
		sizeof (logsys_loggers[0].subsys));
	logsys_config_mode_set (mode);
	logsys_config_facility_set (name, facility);
	logsys_config_file_set (&errstr, file);
	return (0);
}

void logsys_exit (void)
{
}
