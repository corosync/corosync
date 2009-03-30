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
#ifndef LOGSYS_H_DEFINED
#define LOGSYS_H_DEFINED

#include <stdarg.h>
#include <syslog.h>
#include <assert.h>

/*
 * All of the LOG_MODE's can be ORed together for combined behavior
 */
#define LOG_MODE_OUTPUT_FILE		(1<<0)
#define LOG_MODE_OUTPUT_STDERR		(1<<1)
#define LOG_MODE_OUTPUT_SYSLOG		(1<<3)
#define LOG_MODE_NOSUBSYS		(1<<4)
#define LOG_MODE_FORK			(1<<5)
#define LOG_MODE_THREADED		(1<<6)

/*
 * Log priorities, compliant with syslog and SA Forum Log spec.
 */
#define LOG_LEVEL_EMERG	    		LOG_EMERG
#define LOG_LEVEL_ALERT			LOG_ALERT
#define LOG_LEVEL_CRIT			LOG_CRIT
#define LOG_LEVEL_ERROR			LOG_ERR
#define LOG_LEVEL_WARNING		LOG_WARNING
#define LOG_LEVEL_SECURITY		LOG_WARNING // corosync specific
#define LOG_LEVEL_NOTICE		LOG_NOTICE
#define LOG_LEVEL_INFO	    		LOG_INFO
#define LOG_LEVEL_DEBUG			LOG_DEBUG

/*
 * The tag masks are all mutually exclusive
 */	
#define LOGSYS_TAG_LOG			(0xff<<28)
#define LOGSYS_TAG_ENTER		(1<<27)
#define LOGSYS_TAG_LEAVE		(1<<26)
#define LOGSYS_TAG_TRACE1		(1<<25)
#define LOGSYS_TAG_TRACE2		(1<<24)
#define LOGSYS_TAG_TRACE3		(1<<23)
#define LOGSYS_TAG_TRACE4		(1<<22)
#define LOGSYS_TAG_TRACE5		(1<<21)
#define LOGSYS_TAG_TRACE6		(1<<20)
#define LOGSYS_TAG_TRACE7		(1<<19)
#define LOGSYS_TAG_TRACE8		(1<<18)

/*
 * External API
 */
extern void logsys_config_mode_set (
	unsigned int mode);

extern unsigned int logsys_config_mode_get (void);

extern int logsys_config_file_set (
	char **error_string,
	char *file);

extern void logsys_config_facility_set (
	const char *name,
	unsigned int facility);

extern void logsys_format_set (
	char *format);

extern char *logsys_format_get (void);

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

extern const char *logsys_facility_name_get (
	unsigned int facility);

extern int logsys_priority_id_get (
	const char *name);

extern const char *logsys_priority_name_get (
	unsigned int priority);

extern int logsys_tag_id_get (
	const char *name);

extern const char *logsys_tag_name_get (
	unsigned int tag);

extern void logsys_fork_completed (void);

extern void logsys_flush (void);

extern void logsys_atsegv (void);

extern int logsys_log_rec_store (char *filename);

/*
 * Internal APIs that must be globally exported
 */
extern unsigned int _logsys_subsys_create (
	const char *ident,
	unsigned int priority);

extern void _logsys_nosubsys_set (void);

extern int _logsys_rec_init (unsigned int size);

extern void _logsys_log_printf (
	int subsys,
	const char *function_name,
	const char *file_name,
	int file_line,
	unsigned int level,
	const char *format,
	...) __attribute__((format(printf, 6, 7)));

extern void _logsys_log_rec (
	int subsys,
	const char *function_name,
	const char *file_name,
	int file_line,
	unsigned int rec_ident,
	...);

extern int _logsys_wthread_create (void);

static unsigned int logsys_subsys_id __attribute__((unused)) = -1;
									
/*
 * External definitions
 */
extern void *logsys_rec_end;

#define LOG_REC_END (&logsys_rec_end)

#define LOGSYS_DECLARE_SYSTEM(name,mode,file,facility,format,rec_size)	\
__attribute__ ((constructor)) static void logsys_system_init (void)	\
{									\
	char *error_string;						\
									\
	logsys_config_mode_set (mode);					\
	logsys_config_file_set (&error_string, (file));			\
	logsys_config_facility_set (name, (facility));			\
	logsys_format_set (format);					\
	_logsys_rec_init (rec_size);					\
	_logsys_wthread_create();					\
}

#define LOGSYS_DECLARE_NOSUBSYS(priority)				\
__attribute__ ((constructor)) static void logsys_nosubsys_init (void)	\
{									\
	unsigned int pri, tags;						\
									\
	logsys_subsys_id =						\
		logsys_config_subsys_get("MAIN", &tags, &pri);		\
									\
	if (logsys_subsys_id == -1) {					\
		_logsys_nosubsys_set();					\
		logsys_subsys_id =					\
			_logsys_subsys_create ("MAIN", (priority));	\
	}								\
}

#define LOGSYS_DECLARE_SUBSYS(subsys,priority)				\
__attribute__ ((constructor)) static void logsys_subsys_init (void)	\
{									\
	unsigned int pri, tags;						\
									\
	logsys_subsys_id =						\
		logsys_config_subsys_get((subsys), &tags, &pri);	\
									\
	if (logsys_subsys_id == -1)					\
		logsys_subsys_id =					\
			_logsys_subsys_create ((subsys), (priority));	\
}

#define log_rec(rec_ident, args...)					\
do {									\
	_logsys_log_rec (logsys_subsys_id,  __FUNCTION__,		\
		__FILE__,  __LINE__, rec_ident, ##args);		\
} while(0)

#define log_printf(lvl, format, args...)				\
 do {									\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__, __LINE__, lvl, format, ##args);		\
} while(0)

#define ENTER() do {							\
	_logsys_log_rec (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_ENTER, LOG_REC_END);	\
} while(0)

#define LEAVE() do {							\
	_logsys_log_rec (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_LEAVE, LOG_REC_END);	\
} while(0)

#define TRACE1(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE1, format, ##args);\
} while(0)

#define TRACE2(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE2, format, ##args);\
} while(0)

#define TRACE3(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE3, format, ##args);\
} while(0)

#define TRACE4(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE4, format, ##args);\
} while(0)

#define TRACE5(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE5, format, ##args);\
} while(0)

#define TRACE6(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE6, format, ##args);\
} while(0)

#define TRACE7(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE7, format, ##args);\
} while(0)

#define TRACE8(format, args...) do {					\
	_logsys_log_printf (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_TRACE8, format, ##args);\
} while(0)

/*
 * For one-time programmatic initialization and configuration of logsys
 * instead of using the DECLARE macros.  These APIs do not allow subsystems
 */
int logsys_init (
	char *name,
	int mode,
	int facility,
	int priority,
	char *file,
	char *format,
	int rec_size);

int logsys_conf (
	char *name,
	int mode,
	int facility,
	int priority,
	char *file);

void logsys_exit (void);

#endif /* LOGSYS_H_DEFINED */
