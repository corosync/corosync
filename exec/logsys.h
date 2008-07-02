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
#ifndef LOGSYS_H_DEFINED
#define LOGSYS_H_DEFINED

#include <stdarg.h>
#include <syslog.h>
#include <assert.h>

/*
 * MODE_OUTPUT_SYSLOG_* modes are mutually exclusive
 */
#define LOG_MODE_OUTPUT_FILE		(1<<0)
#define LOG_MODE_OUTPUT_STDERR		(1<<1)
#define LOG_MODE_OUTPUT_SYSLOG_THREADED	(1<<2)
#define LOG_MODE_OUTPUT_SYSLOG_LOSSY	(1<<3)
#define LOG_MODE_OUTPUT_SYSLOG_BLOCKING	(1<<4)
#define LOG_MODE_DISPLAY_PRIORITY	(1<<5)
#define LOG_MODE_DISPLAY_FILELINE	(1<<6)
#define LOG_MODE_DISPLAY_TIMESTAMP	(1<<7)
#define LOG_MODE_BUFFER_BEFORE_CONFIG	(1<<8)
#define LOG_MODE_FLUSH_AFTER_CONFIG	(1<<9)
#define LOG_MODE_SHORT_FILELINE		(1<<10)
#define LOG_MODE_NOSUBSYS		(1<<11)

/*
 * Log priorities, compliant with syslog and SA Forum Log spec.
 */
#define LOG_LEVEL_EMERG	    		LOG_EMERG
#define LOG_LEVEL_ALERT			LOG_ALERT
#define LOG_LEVEL_CRIT			LOG_CRIT
#define LOG_LEVEL_ERROR			LOG_ERR
#define LOG_LEVEL_WARNING		LOG_WARNING
#define LOG_LEVEL_SECURITY		LOG_WARNING // openais specific
#define LOG_LEVEL_NOTICE		LOG_NOTICE
#define LOG_LEVEL_INFO	    		LOG_INFO
#define LOG_LEVEL_DEBUG			LOG_DEBUG

/*
** Log tags, used by _logsys_trace macros, uses 32 bits => 32 different tags
*/	
#define LOGSYS_TAG_LOG	    		(1<<0)
#define LOGSYS_TAG_ENTER		(1<<1)
#define LOGSYS_TAG_LEAVE		(1<<2)
#define LOGSYS_TAG_TRACE1		(1<<3)
#define LOGSYS_TAG_TRACE2		(1<<4)
#define LOGSYS_TAG_TRACE3		(1<<5)
#define LOGSYS_TAG_TRACE4		(1<<6)
#define LOGSYS_TAG_TRACE5		(1<<7)
#define LOGSYS_TAG_TRACE6		(1<<8)
#define LOGSYS_TAG_TRACE7		(1<<9)
#define LOGSYS_TAG_TRACE8		(1<<10)

/*
 * External API
 */

struct logsys_logger {
	char subsys[6];
	unsigned int priority;
	unsigned int tags;
	unsigned int mode;
};

extern struct logsys_logger logsys_loggers[];

extern inline int logsys_mkpri (int priority, int id);

extern void logsys_config_mode_set (
	unsigned int mode);

extern unsigned int logsys_config_mode_get (void);

extern int logsys_config_file_set (
	char **error_string,
	char *file);

extern void logsys_config_facility_set (
	char *name,
	unsigned int facility);

extern unsigned int logsys_config_subsys_set (
	const char *subsys,
	unsigned int tags,
	unsigned int priority);

extern int logsys_config_subsys_get (
	const char *subsys,
	unsigned int *tags,
	unsigned int *priority);

extern int logsys_facility_id_get (
	const char *name);

extern char *logsys_facility_name_get (
	unsigned int facility);

extern int logsys_priority_id_get (
	const char *name);

extern char *logsys_priority_name_get (
	unsigned int priority);

extern void logsys_flush (void);

extern void logsys_atsegv (void);

/*
 * Internal APIs that must be globally exported
 */
extern unsigned int _logsys_subsys_create (const char *ident,
	unsigned int priority);

extern void _logsys_nosubsys_set (void);

extern int _logsys_wthread_create (void);

extern void logsys_log_printf (char *file, int line, int priority,
	char *format, ...) __attribute__((format(printf, 4, 5)));

extern void _logsys_log_printf2 (char *file, int line, int priority,
	int id, char *format, ...) __attribute__((format(printf, 5, 6)));

extern void _logsys_trace (char *file, int line, int tag, int id,
	char *format, ...) __attribute__((format(printf, 5, 6)));

/*
 * External definitions
 */
#define LOGSYS_DECLARE_SYSTEM(name,mode,file,facility)			\
__attribute__ ((constructor)) static void logsys_system_init (void)	\
{									\
	char *error_string;						\
									\
	logsys_config_mode_set (mode);					\
	logsys_config_file_set (&error_string, (file));			\
	logsys_config_facility_set (name, (facility));			\
        if (((mode) & LOG_MODE_BUFFER_BEFORE_CONFIG) == 0) {		\
		_logsys_wthread_create ();				\
	}								\
}

static unsigned int logsys_subsys_id __attribute__((unused)) = -1;	\

#define LOGSYS_DECLARE_NOSUBSYS(priority)				\
__attribute__ ((constructor)) static void logsys_nosubsys_init (void)	\
{									\
	_logsys_nosubsys_set();						\
	logsys_subsys_id =						\
		_logsys_subsys_create ("MAIN", (priority));		\
}

#define LOGSYS_DECLARE_SUBSYS(subsys,priority)				\
__attribute__ ((constructor)) static void logsys_subsys_init (void)	\
{									\
	logsys_subsys_id =						\
		_logsys_subsys_create ((subsys), (priority));		\
}

#define log_printf(lvl, format, args...) do {				\
	assert (logsys_subsys_id != -1);				\
	if ((lvl) <= logsys_loggers[logsys_subsys_id].priority)	{	\
		_logsys_log_printf2 (__FILE__, __LINE__, lvl,		\
			logsys_subsys_id, (format), ##args);		\
	}								\
} while(0)

#define dprintf(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_log_printf2 (__FILE__, __LINE__, LOG_DEBUG,	\
			logsys_subsys_id, (format), ##args);		\
	}								\
} while(0)

#define ENTER_VOID() do {						\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_ENTER,	\
			logsys_subsys_id, ">%s\n", __FUNCTION__);	\
	}								\
} while(0)

#define ENTER(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_ENTER,	\
			logsys_subsys_id, ">%s: " format, __FUNCTION__,	\
			##args);					\
	}								\
} while(0)

#define LEAVE_VOID() do {						\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_LEAVE,	\
			logsys_subsys_id, "<%s\n", __FUNCTION__);	\
	}								\
} while(0)

#define LEAVE(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_LEAVE,	\
			logsys_subsys_id, "<%s: " format,		\
			__FUNCTION__, ##args);				\
	}								\
} while(0)

#define TRACE1(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE1,	\
			logsys_subsys_id, (format), ##args);		\
	}								\
} while(0)

#define TRACE2(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE2,	\
			logsys_subsys_id, (format), ##args);		\
	}								\
} while(0)

#define TRACE3(format, args...) do { \
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE3,	\
			logsys_subsys_id, (format), ##args);		\
    }									\
} while(0)

#define TRACE4(format, args...) do { \
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE4,	\
			logsys_subsys_id, (format), ##args);		\
	}								\
} while(0)

#define TRACE5(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE5,	\
		logsys_subsys_id, (format), ##args);			\
	}								\
} while(0)

#define TRACE6(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE6,	\
			logsys_subsys_id, (format), ##args);		\
	}								\
} while(0)

#define TRACE7(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
		_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE7,	\
			 logsys_subsys_id, (format), ##args);		\
	}								\
} while(0)

#define TRACE8(format, args...) do {					\
	assert (logsys_subsys_id != -1);				\
	if (LOG_LEVEL_DEBUG <= logsys_loggers[logsys_subsys_id].priority) { \
	_logsys_trace (__FILE__, __LINE__, LOGSYS_TAG_TRACE8,		\
	 logsys_subsys_id, (format), ##args);				\
	}								\
} while(0)

extern void _logsys_config_priority_set (unsigned int id, unsigned int priority);

#define logsys_config_priority_set(priority) do {		        \
	_logsys_config_priority_set (logsys_subsys_id, priority);       \
} while(0)

/* simple, function-based api */

int logsys_init (char *name, int mode, int facility, int priority, char *file);
int logsys_conf (char *name, int mode, int facility, int priority, char *file);
void logsys_exit (void);

#endif /* LOGSYS_H_DEFINED */
