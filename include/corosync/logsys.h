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
#ifndef LOGSYS_H_DEFINED
#define LOGSYS_H_DEFINED

#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <limits.h>

#include <qb/qbconfig.h>
#include <qb/qblog.h>

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
#define LOGSYS_LEVEL_TRACE		LOG_TRACE

/*
 * logsys_logger bits
 *
 * SUBSYS_COUNT defines the maximum number of subsystems
 * SUBSYS_NAMELEN defines the maximum len of a subsystem name
 */
#define LOGSYS_MAX_SUBSYS_COUNT		32
#define LOGSYS_MAX_SUBSYS_NAMELEN	64
#define LOGSYS_MAX_PERROR_MSG_LEN	128

/*
 * Debug levels
 */
#define LOGSYS_DEBUG_OFF		0
#define LOGSYS_DEBUG_ON			1
#define LOGSYS_DEBUG_TRACE		2

#ifndef LOGSYS_UTILS_ONLY

/**
 * @brief configuration bits that can only be done for the whole system
 * @param format
 * @return
 */
extern int logsys_format_set (
	const char *format);

/**
 * @brief logsys_format_get
 * @return
 */
extern char *logsys_format_get (void);

/**
 * @brief per system/subsystem settings.
 *
 * NOTE: once a subsystem is created and configured, changing
 * the default does NOT affect the subsystems.
 *
 * Pass a NULL subsystem to change them all
 *
 * @param subsys
 * @param facility
 * @return
 */
extern int logsys_config_syslog_facility_set (
	const char *subsys,
	unsigned int facility);

/**
 * @brief logsys_config_syslog_priority_set
 * @param subsys
 * @param priority
 * @return
 */
extern int logsys_config_syslog_priority_set (
	const char *subsys,
	unsigned int priority);

/**
 * @brief logsys_config_mode_set
 * @param subsys
 * @param mode
 * @return
 */
extern int logsys_config_mode_set (
	const char *subsys,
	unsigned int mode);

/**
 * @brief logsys_config_mode_get
 * @param subsys
 * @return
 */
extern unsigned int logsys_config_mode_get (
	const char *subsys);

/**
 * @brief logsys_config_apply
 */
void logsys_config_apply(void);

/**
 * @brief to close a logfile, just invoke this function with a NULL
 * file or if you want to change logfile, the old one will
 * be closed for you.
 *
 * @param subsys
 * @param error_string
 * @param file
 * @return
 */
extern int logsys_config_file_set (
	const char *subsys,
	const char **error_string,
	const char *file);

/**
 * @brief logsys_config_logfile_priority_set
 * @param subsys
 * @param priority
 * @return
 */
extern int logsys_config_logfile_priority_set (
	const char *subsys,
	unsigned int priority);

/**
 * @brief enabling debug, disable message priority filtering.
 * everything is sent everywhere. priority values
 * for file and syslog are not overwritten.
 *
 * @param subsys
 * @param value
 * @return
 */
extern int logsys_config_debug_set (
	const char *subsys,
	unsigned int value);

/*
 * External API - helpers
 *
 * convert facility/priority to/from name/values
 */
/**
 * @brief logsys_priority_id_get
 * @param name
 * @return
 */
extern int logsys_priority_id_get (
	const char *name);

/**
 * @brief logsys_priority_name_get
 * @param priority
 * @return
 */
extern const char *logsys_priority_name_get (
	unsigned int priority);

/**
 * @brief _logsys_system_setup
 * @param mainsystem
 * @param mode
 * @param syslog_facility
 * @param syslog_priority
 * @return
 */
extern int _logsys_system_setup(
	const char *mainsystem,
	unsigned int mode,
	int syslog_facility,
	int syslog_priority);

/**
 * @brief logsys_system_fini
 */
extern void logsys_system_fini (void);

/**
 * @brief _logsys_config_subsys_get
 * @param subsys
 * @return
 */
extern int _logsys_config_subsys_get (
	const char *subsys);

/**
 * @brief _logsys_subsys_create
 * @param subsys
 * @param filename
 * @return
 */
extern int _logsys_subsys_create (const char *subsys, const char *filename);

/**
 * @brief logsys_thread_start
 * @return
 */
extern int logsys_thread_start (void);

/**
 * @brief logsys_subsys_id
 */
static int logsys_subsys_id __attribute__((unused)) = LOGSYS_MAX_SUBSYS_COUNT;

/**
 * @brief The LOGSYS_DECLARE_SYSTEM macro
 * @param name
 * @param mode
 * @param syslog_facility
 * @param syslog_priority
 */
#define LOGSYS_DECLARE_SYSTEM(name,mode,syslog_facility,syslog_priority)\
__attribute__ ((constructor))						\
static void logsys_system_init (void)					\
{									\
	if (_logsys_system_setup (name,mode,syslog_facility,syslog_priority) < 0) { \
		fprintf (stderr,					\
			"Unable to setup logging system: %s.\n", name);	\
		exit (-1);						\
	}								\
}

#ifdef QB_HAVE_ATTRIBUTE_SECTION
#define LOGSYS_DECLARE_SECTION assert(__start___verbose != __stop___verbose)
#else
#define LOGSYS_DECLARE_SECTION
#endif

/**
 * @brief The LOGSYS_DECLARE_SUBSYS macro
 * @param subsys
 */
#define LOGSYS_DECLARE_SUBSYS(subsys)					\
__attribute__ ((constructor))						\
static void logsys_subsys_init (void)					\
{									\
	LOGSYS_DECLARE_SECTION;						\
	logsys_subsys_id =						\
		_logsys_subsys_create ((subsys), __FILE__);		\
	if (logsys_subsys_id == -1) {					\
		fprintf (stderr,					\
		"Unable to create logging subsystem: %s.\n", subsys);	\
		exit (-1);						\
	}								\
}

/**
 * @brief The LOGSYS_PERROR macro
 * @param err_num
 * @param level
 * @param fmt
 * @param args
 */
#define LOGSYS_PERROR(err_num, level, fmt, args...) do {						\
		char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];						\
		const char *_error_ptr = qb_strerror_r(err_num, _error_str, sizeof(_error_str));	\
		qb_log(level, fmt ": %s (%d)", ##args, _error_ptr, err_num);				\
	} while(0)

#define log_printf(level, format, args...) qb_log(level, format, ##args)
#define ENTER qb_enter
#define LEAVE qb_leave
#define TRACE1(format, args...) qb_log(LOG_TRACE, "TRACE1:" #format, ##args)
#define TRACE2(format, args...) qb_log(LOG_TRACE, "TRACE2:" #format, ##args)
#define TRACE3(format, args...) qb_log(LOG_TRACE, "TRACE3:" #format, ##args)
#define TRACE4(format, args...) qb_log(LOG_TRACE, "TRACE4:" #format, ##args)
#define TRACE5(format, args...) qb_log(LOG_TRACE, "TRACE5:" #format, ##args)
#define TRACE6(format, args...) qb_log(LOG_TRACE, "TRACE6:" #format, ##args)
#define TRACE7(format, args...) qb_log(LOG_TRACE, "TRACE7:" #format, ##args)
#define TRACE8(format, args...) qb_log(LOG_TRACE, "TRACE8:" #format, ##args)

#endif /* LOGSYS_UTILS_ONLY */

#ifdef __cplusplus
}
#endif

#endif /* LOGSYS_H_DEFINED */
