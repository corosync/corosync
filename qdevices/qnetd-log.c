/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#include <syslog.h>
#include <stdio.h>
#include <time.h>

#include "qnet-config.h"
#include "qnetd-log.h"

static int qnetd_log_config_target = 0;
static int qnetd_log_config_debug = 0;
static int qnetd_log_config_priority_bump = 0;

static const char qnetd_log_month_str[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

struct qnetd_log_syslog_prio_to_str_item {
	int priority;
	const char *priority_str;
};

static struct qnetd_log_syslog_prio_to_str_item qnetd_syslog_prio_to_str_array[] = {
    {LOG_EMERG,		"emerg"},
    {LOG_ALERT,		"alert"},
    {LOG_CRIT,		"crit"},
    {LOG_ERR,		"error"},
    {LOG_WARNING,	"warning"},
    {LOG_NOTICE,	"notice"},
    {LOG_INFO,		"info"},
    {LOG_DEBUG,		"debug"},
    {-1, NULL}};

void
qnetd_log_init(int target)
{

	qnetd_log_config_target = target;

	if (qnetd_log_config_target & QNETD_LOG_TARGET_SYSLOG) {
		openlog(QNETD_PROGRAM_NAME, LOG_PID, LOG_DAEMON);
	}
}

static const char *
qnetd_log_syslog_prio_to_str(int priority)
{

	if (priority >= LOG_EMERG && priority <= LOG_DEBUG) {
		return (qnetd_syslog_prio_to_str_array[priority].priority_str);
	} else {
		return ("none");
	}
}

void
qnetd_log_vprintf(int priority, const char *format, va_list ap)
{
	time_t current_time;
	struct tm tm_res;
	int final_priority;
	va_list ap_copy;

	if (priority != LOG_DEBUG || (qnetd_log_config_debug)) {
		if (qnetd_log_config_target & QNETD_LOG_TARGET_STDERR) {
			current_time = time(NULL);
			localtime_r(&current_time, &tm_res);

			fprintf(stderr, "%s %02d %02d:%02d:%02d ",
			    qnetd_log_month_str[tm_res.tm_mon], tm_res.tm_mday, tm_res.tm_hour,
			    tm_res.tm_min, tm_res.tm_sec);

			fprintf(stderr, "%-7s ", qnetd_log_syslog_prio_to_str(priority));

			va_copy(ap_copy, ap);
			vfprintf(stderr, format, ap_copy);
			va_end(ap_copy);
			fprintf(stderr, "\n");
		}

		if (qnetd_log_config_target & QNETD_LOG_TARGET_SYSLOG) {
			final_priority = priority;
			if (qnetd_log_config_priority_bump && priority > LOG_INFO) {
				final_priority = LOG_INFO;
			}

			va_copy(ap_copy, ap);
			vsyslog(final_priority, format, ap);
			va_end(ap_copy);
		}
	}
}

void
qnetd_log_printf(int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);

	qnetd_log_vprintf(priority, format, ap);

	va_end(ap);
}

void
qnetd_log_close(void)
{

	if (qnetd_log_config_target & QNETD_LOG_TARGET_SYSLOG) {
		closelog();
	}
}

void
qnetd_log_set_debug(int enabled)
{

	qnetd_log_config_debug = enabled;
}

void
qnetd_log_set_priority_bump(int enabled)
{

	qnetd_log_config_priority_bump = enabled;
}

void
qnetd_log_msg_decode_error(int ret)
{

	switch (ret) {
	case -1:
		qnetd_log(LOG_WARNING, "Received message with option with invalid length");
		break;
	case -2:
		qnetd_log(LOG_CRIT, "Can't allocate memory");
		break;
	case -3:
		qnetd_log(LOG_WARNING, "Received inconsistent msg (tlv len > msg size)");
		break;
	case -4:
		qnetd_log(LOG_WARNING, "Received message with option with invalid value");
		break;
	default:
		qnetd_log(LOG_ERR, "Unknown error occured when decoding message");
		break;
	}
}
