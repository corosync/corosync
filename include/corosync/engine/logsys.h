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
#include <syslog.h>
#include <assert.h>

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
#define LOGSYS_LEVEL_EMERG	    		LOG_EMERG
#define LOGSYS_LEVEL_ALERT			LOG_ALERT
#define LOGSYS_LEVEL_CRIT			LOG_CRIT
#define LOGSYS_LEVEL_ERROR			LOG_ERR
#define LOGSYS_LEVEL_WARNING		LOG_WARNING
#define LOGSYS_LEVEL_SECURITY		LOG_WARNING // corosync specific
#define LOGSYS_LEVEL_NOTICE		LOG_NOTICE
#define LOGSYS_LEVEL_INFO	    		LOG_INFO
#define LOGSYS_LEVEL_DEBUG			LOG_DEBUG

/*
 * The tag masks are all mutually exclusive
 *
 * LOG is mandatory, but enforced, for subsystems.
 * Be careful if/when changing tags at runtime.
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

extern int _logsys_system_setup(
	const char *mainsystem,
	unsigned int mode,
	unsigned int debug,
	const char *logfile,
	int logfile_priority,
	int syslog_facility,
	int syslog_priority,
	unsigned int tags);

extern int _logsys_config_subsys_get (
	const char *subsys);

extern unsigned int _logsys_subsys_create (const char *subsys);

extern int _logsys_rec_init (unsigned int size);

extern void _logsys_log_printf (
	int subsysid,
	const char *function_name,
	const char *file_name,
	int file_line,
	unsigned int level,
	const char *format,
	...) __attribute__((format(printf, 6, 7)));

extern void _logsys_log_rec (
	int subsysid,
	const char *function_name,
	const char *file_name,
	int file_line,
	unsigned int rec_ident,
	...);

extern int _logsys_wthread_create (void);

static unsigned int logsys_subsys_id __attribute__((unused)) = -1;

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
extern unsigned int logsys_config_syslog_facility_set (
	const char *subsys,
	unsigned int facility);

extern unsigned int logsys_config_syslog_priority_set (
	const char *subsys,
	unsigned int priority);

extern unsigned int logsys_config_mode_set (
	const char *subsys,
	unsigned int mode);

extern unsigned int logsys_config_mode_get (
	const char *subsys);

extern unsigned int logsys_config_tags_set (
	const char *subsys,
	unsigned int tags);

extern unsigned int logsys_config_tags_get (
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

extern unsigned int logsys_config_logfile_priority_set (
	const char *subsys,
	unsigned int priority);

/*
 * enabling debug, disable message priority filtering.
 * everything is sent everywhere. priority values
 * for file and syslog are not overwritten.
 */
extern unsigned int logsys_config_debug_set (
	const char *subsys,
	unsigned int value);

/*
 * External API - helpers
 *
 * convert facility/priority/tag to/from name/values
 */
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

/*
 * External definitions
 */
extern void *logsys_rec_end;

#define LOGSYS_REC_END (&logsys_rec_end)

#define LOGSYS_DECLARE_SYSTEM(name,mode,debug,file,file_priority,	\
		syslog_facility,syslog_priority,tags,format,rec_size)	\
__attribute__ ((constructor))						\
static void logsys_system_init (void)					\
{									\
	int err;							\
									\
	err = _logsys_system_setup (name,mode,debug,file,file_priority,	\
				syslog_facility,syslog_priority,tags);	\
	assert (err == 0 && "_logsys_system_setup failed");		\
									\
	err = logsys_format_set (format);				\
	assert (err == 0 && "logsys_format_set failed");		\
									\
	err = _logsys_rec_init (rec_size);				\
	assert (err == 0 && "_logsys_rec_init failed");			\
									\
	err = _logsys_wthread_create();					\
	assert (err == 0 && "_logsys_wthread_create failed");		\
}

#define LOGSYS_DECLARE_SUBSYS(subsys)					\
__attribute__ ((constructor))						\
static void logsys_subsys_init (void)					\
{									\
	logsys_subsys_id =						\
		_logsys_subsys_create ((subsys));			\
	assert (logsys_subsys_id < LOGSYS_MAX_SUBSYS_COUNT && 		\
		"_logsys_subsys_create failed");			\
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
		__FILE__,  __LINE__, LOGSYS_TAG_ENTER, LOGSYS_REC_END);	\
} while(0)

#define LEAVE() do {							\
	_logsys_log_rec (logsys_subsys_id, __FUNCTION__,		\
		__FILE__,  __LINE__, LOGSYS_TAG_LEAVE, LOGSYS_REC_END);	\
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

#endif /* LOGSYS_H_DEFINED */
