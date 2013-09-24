/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
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
#ifndef LOGSYS_H_DEFINED
#define LOGSYS_H_DEFINED

#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * All of the LOGSYS_MODE's can be ORed together for combined behavior
 *
 * FORK and THREADED are ignored for SUBSYSTEMS
 */
#define LOGSYS_MODE_OUTPUT_FILE		(1<<0)
#define LOGSYS_MODE_OUTPUT_STDERR	(1<<1)
#define LOGSYS_MODE_OUTPUT_SYSLOG	(1<<2)
#define LOGSYS_MODE_FORK		(1<<3)
#define LOGSYS_MODE_THREADED		(1<<4)


/*
 * Log priorities, compliant with syslog and SA Forum Log spec.
 */
#define LOGSYS_LEVEL_EMERG		LOG_EMERG
#define LOGSYS_LEVEL_ALERT		LOG_ALERT
#define LOGSYS_LEVEL_CRIT		LOG_CRIT
#define LOGSYS_LEVEL_ERROR		LOG_ERR
#define LOGSYS_LEVEL_WARNING		LOG_WARNING
#define LOGSYS_LEVEL_NOTICE		LOG_NOTICE
#define LOGSYS_LEVEL_INFO		LOG_INFO
#define LOGSYS_LEVEL_DEBUG		LOG_DEBUG

/*
 * All of the LOGSYS_RECID's are mutually exclusive. Only one RECID at any time
 * can be specified.
 *
 * RECID_LOG indicates a message that should be sent to log. Anything else
 * is stored only in the flight recorder.
 */

#define LOGSYS_RECID_MAX		((UINT_MAX) >> LOGSYS_SUBSYSID_END)

#define LOGSYS_RECID_LOG		(LOGSYS_RECID_MAX - 1)
#define LOGSYS_RECID_ENTER		(LOGSYS_RECID_MAX - 2)
#define LOGSYS_RECID_LEAVE		(LOGSYS_RECID_MAX - 3)
#define LOGSYS_RECID_TRACE1		(LOGSYS_RECID_MAX - 4)
#define LOGSYS_RECID_TRACE2		(LOGSYS_RECID_MAX - 5)
#define LOGSYS_RECID_TRACE3		(LOGSYS_RECID_MAX - 6)
#define LOGSYS_RECID_TRACE4		(LOGSYS_RECID_MAX - 7)
#define LOGSYS_RECID_TRACE5		(LOGSYS_RECID_MAX - 8)
#define LOGSYS_RECID_TRACE6		(LOGSYS_RECID_MAX - 9)
#define LOGSYS_RECID_TRACE7		(LOGSYS_RECID_MAX - 10)
#define LOGSYS_RECID_TRACE8		(LOGSYS_RECID_MAX - 11)

/*
 * Debug levels
 */
#define LOGSYS_DEBUG_OFF		0
#define LOGSYS_DEBUG_ON			1
#define LOGSYS_DEBUG_TRACE		2

/*
 * Internal APIs that must be globally exported
 * (External API below)
 */

/*
 * logsys_logger bits
 *
 * SUBSYS_COUNT defines the maximum number of subsystems
 * SUBSYS_NAMELEN defines the maximum len of a subsystem name
 */
#define LOGSYS_MAX_SUBSYS_COUNT		64
#define LOGSYS_MAX_SUBSYS_NAMELEN	64

/*
 * rec_ident explained:
 *
 * rec_ident is an unsigned int and carries bitfields information
 * on subsys_id, log priority (level) and type of message (RECID).
 *
 * level values are imported from syslog.h.
 * At the time of writing it's a 3 bits value (0 to 7).
 *
 * subsys_id is any value between 0 and 64 (LOGSYS_MAX_SUBSYS_COUNT)
 *
 * RECID identifies the type of message. A set of predefined values
 * are available via logsys, but other custom values can be defined
 * by users.
 *
 * ----
 * bitfields:
 *
 * 0  - 2 level
 * 3  - 9 subsysid
 * 10 - n RECID
 */

#define LOGSYS_LEVEL_END		(3)
#define LOGSYS_SUBSYSID_END		(LOGSYS_LEVEL_END + 7)

#define LOGSYS_RECID_LEVEL_MASK		(LOG_PRIMASK)
#define LOGSYS_RECID_SUBSYSID_MASK	((2 << (LOGSYS_SUBSYSID_END - 1)) - \
					(LOG_PRIMASK + 1))
#define LOGSYS_RECID_RECID_MASK		(UINT_MAX - \
					(LOGSYS_RECID_SUBSYSID_MASK + LOG_PRIMASK))

#define LOGSYS_ENCODE_RECID(level,subsysid,recid) \
	(((recid) << LOGSYS_SUBSYSID_END) | \
	((subsysid) << LOGSYS_LEVEL_END) | \
	(level))

#define LOGSYS_DECODE_LEVEL(rec_ident) \
	((rec_ident) & LOGSYS_RECID_LEVEL_MASK)

#define LOGSYS_DECODE_SUBSYSID(rec_ident) \
	(((rec_ident) & LOGSYS_RECID_SUBSYSID_MASK) >> LOGSYS_LEVEL_END)

#define LOGSYS_DECODE_RECID(rec_ident) \
	(((rec_ident) & LOGSYS_RECID_RECID_MASK) >> LOGSYS_SUBSYSID_END)

#define LOGSYS_MAX_PERROR_MSG_LEN	128

#ifdef COROSYNC_LINUX
/* The GNU version of strerror_r returns a (char*) that *must* be used */
#define LOGSYS_STRERROR_R(out_ptr, err_num, buffer, sizeof_buffer) \
	out_ptr = strerror_r(err_num, buffer, sizeof_buffer);
#else
/* The XSI-compliant strerror_r() return 0 or -1 (in case the buffer is full) */
#define LOGSYS_STRERROR_R(out_ptr, err_num, buffer, sizeof_buffer) do {	\
		if ( strerror_r(err_num, buffer, sizeof_buffer) == 0 ) {		\
			out_ptr = buffer;											\
		} else {														\
			out_ptr = "";												\
		}																\
	} while(0)
#endif

#define LOGSYS_PERROR(err_num, level, fmt, args...) do {							\
		char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];									\
		const char *_error_ptr;														\
		LOGSYS_STRERROR_R(_error_ptr, err_num, _error_str, sizeof(_error_str));		\
		log_printf(level, fmt ": %s (%d)\n", ##args, _error_ptr, err_num);			\
	} while(0)



#ifndef LOGSYS_UTILS_ONLY

extern int _logsys_system_setup(
	const char *mainsystem,
	unsigned int mode,
	unsigned int debug,
	const char *logfile,
	int logfile_priority,
	int syslog_facility,
	int syslog_priority);

extern int _logsys_config_subsys_get (
	const char *subsys);

extern int _logsys_subsys_create (const char *subsys);

extern int _logsys_rec_init (unsigned int size);

extern void _logsys_log_vprintf (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	const char *format,
	va_list ap) __attribute__((format(printf, 5, 0)));

extern void _logsys_log_printf (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	const char *format,
	...) __attribute__((format(printf, 5, 6)));

extern void _logsys_log_rec (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	...);

extern int _logsys_wthread_create (void);

static int logsys_subsys_id __attribute__((unused)) = LOGSYS_MAX_SUBSYS_COUNT;

/*
 * External API - init
 * See below:
 *
 * LOGSYS_DECLARE_SYSTEM
 * LOGSYS_DECLARE_SUBSYS
 *
 */
extern void logsys_fork_completed (void);

extern void logsys_atexit (void);

/*
 * External API - misc
 */
extern void logsys_flush (void);

extern int logsys_log_rec_store (const char *filename);

/*
 * External API - configuration
 */

/*
 * configuration bits that can only be done for the whole system
 */
extern int logsys_format_set (
	const char *format);

extern char *logsys_format_get (void);

/*
 * per system/subsystem settings.
 *
 * NOTE: once a subsystem is created and configured, changing
 * the default does NOT affect the subsystems.
 *
 * Pass a NULL subsystem to change them all
 */
extern int logsys_config_syslog_facility_set (
	const char *subsys,
	unsigned int facility);

extern int logsys_config_syslog_priority_set (
	const char *subsys,
	unsigned int priority);

extern int logsys_config_mode_set (
	const char *subsys,
	unsigned int mode);

extern unsigned int logsys_config_mode_get (
	const char *subsys);

/*
 * to close a logfile, just invoke this function with a NULL
 * file or if you want to change logfile, the old one will
 * be closed for you.
 */
extern int logsys_config_file_set (
	const char *subsys,
	const char **error_string,
	const char *file);

extern int logsys_config_logfile_priority_set (
	const char *subsys,
	unsigned int priority);

/*
 * enabling debug, disable message priority filtering.
 * everything is sent everywhere. priority values
 * for file and syslog are not overwritten.
 */
extern int logsys_config_debug_set (
	const char *subsys,
	unsigned int value);

/*
 * External API - helpers
 *
 * convert facility/priority to/from name/values
 */
extern int logsys_facility_id_get (
	const char *name);

extern const char *logsys_facility_name_get (
	unsigned int facility);

extern int logsys_priority_id_get (
	const char *name);

extern const char *logsys_priority_name_get (
	unsigned int priority);

extern int logsys_thread_priority_set (
	int policy,
	const struct sched_param *param,
	unsigned int after_log_ops_yield);

/*
 * External definitions
 */
extern void *logsys_rec_end;

#define LOGSYS_REC_END (&logsys_rec_end)

#define LOGSYS_DECLARE_SYSTEM(name,mode,debug,file,file_priority,	\
		syslog_facility,syslog_priority,format,fltsize)		\
__attribute__ ((constructor))						\
static void logsys_system_init (void)					\
{									\
	const char *error_str;						\
									\
	if (_logsys_system_setup (name,mode,debug,file,file_priority,	\
			syslog_facility,syslog_priority) < 0) {		\
		fprintf (stderr,					\
			"Unable to setup logging system: %s.\n", name);	\
		syslog (LOG_ERR,					\
			"Unable to setup logging system: %s.\n", name);	\
		exit (EXIT_FAILURE);					\
	}								\
									\
	if (logsys_format_set (format) == -1) {				\
		error_str = "Unable to setup logging format.";		\
									\
		fprintf (stderr, "%s\n", error_str);			\
		syslog (LOG_ERR, "%s\n", error_str);			\
		exit (EXIT_FAILURE);					\
	}								\
									\
	if (_logsys_rec_init (fltsize) < 0) {				\
		error_str = "Unable to initialize log flight recorder. "\
		    "The most common cause of this error is "		\
		    "not enough space on /dev/shm.";			\
									\
		fprintf (stderr, "%s\n", error_str);			\
		syslog (LOG_ERR, "%s\n", error_str);			\
		exit (EXIT_FAILURE);					\
	}								\
									\
	if (_logsys_wthread_create() < 0) {				\
		error_str = "Unable to initialize logging thread.";	\
									\
		fprintf (stderr, "%s\n", error_str);			\
		syslog (LOG_ERR, "%s\n", error_str);			\
		exit (EXIT_FAILURE);					\
	}								\
}

#define LOGSYS_DECLARE_SUBSYS(subsys)					\
__attribute__ ((constructor))						\
static void logsys_subsys_init (void)					\
{									\
	logsys_subsys_id =						\
		_logsys_subsys_create ((subsys));			\
	if (logsys_subsys_id == -1) {					\
		fprintf (stderr,					\
		"Unable to create logging subsystem: %s.\n", subsys);	\
		exit (-1);						\
	}								\
}

#define log_rec(rec_ident, args...)					\
do {									\
	_logsys_log_rec (rec_ident,  __FUNCTION__,			\
		__FILE__,  __LINE__, ##args,				\
		LOGSYS_REC_END);					\
} while(0)

#define log_printf(level, format, args...)				\
 do {									\
	_logsys_log_printf (						\
		LOGSYS_ENCODE_RECID(level,				\
				    logsys_subsys_id,			\
				    LOGSYS_RECID_LOG),			\
		 __FUNCTION__, __FILE__, __LINE__,			\
		format, ##args);					\
} while(0)

#define ENTER() do {							\
	_logsys_log_rec (						\
		LOGSYS_ENCODE_RECID(LOGSYS_LEVEL_DEBUG,			\
				    logsys_subsys_id,			\
				    LOGSYS_RECID_ENTER),		\
		__FUNCTION__, __FILE__,  __LINE__, LOGSYS_REC_END);	\
} while(0)

#define LEAVE() do {							\
	_logsys_log_rec (						\
		LOGSYS_ENCODE_RECID(LOGSYS_LEVEL_DEBUG,			\
				    logsys_subsys_id,			\
				    LOGSYS_RECID_LEAVE),		\
		__FUNCTION__, __FILE__,  __LINE__, LOGSYS_REC_END);	\
} while(0)

#define TRACE(recid, format, args...) do {				\
	_logsys_log_printf (						\
		LOGSYS_ENCODE_RECID(LOGSYS_LEVEL_DEBUG,			\
				    logsys_subsys_id,			\
				    recid),				\
		 __FUNCTION__, __FILE__, __LINE__,			\
		format, ##args);					\
} while(0)

#define TRACE1(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE1, format, ##args);			\
} while(0)

#define TRACE2(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE2, format, ##args);			\
} while(0)

#define TRACE3(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE3, format, ##args);			\
} while(0)

#define TRACE4(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE4, format, ##args);			\
} while(0)

#define TRACE5(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE5, format, ##args);			\
} while(0)

#define TRACE6(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE6, format, ##args);			\
} while(0)

#define TRACE7(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE7, format, ##args);			\
} while(0)

#define TRACE8(format, args...) do {					\
	TRACE(LOGSYS_RECID_TRACE8, format, ##args);			\
} while(0)

#endif /* LOGSYS_UTILS_ONLY */

#ifdef __cplusplus
}
#endif

#endif /* LOGSYS_H_DEFINED */
