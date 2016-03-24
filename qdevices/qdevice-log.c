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

#include "qdevice-log.h"
#include "qdevice-config.h"
#include "utils.h"

static int qdevice_log_global_force_debug;

struct qdevice_log_syslog_names {
	const char *prio_name;
	int priority;
};

static struct qdevice_log_syslog_names qdevice_log_priority_names[] = {
	{ "alert", LOG_ALERT },
	{ "crit", LOG_CRIT },
	{ "debug", LOG_DEBUG },
	{ "emerg", LOG_EMERG },
	{ "err", LOG_ERR },
	{ "error", LOG_ERR },
	{ "info", LOG_INFO },
	{ "notice", LOG_NOTICE },
	{ "warning", LOG_WARNING },
	{ NULL, -1 }};

static int
qdevice_log_priority_str_to_int(const char *priority_str)
{
	unsigned int i;

	for (i = 0; qdevice_log_priority_names[i].prio_name != NULL; i++) {
		if (strcasecmp(priority_str, qdevice_log_priority_names[i].prio_name) == 0) {
			return (qdevice_log_priority_names[i].priority);
		}
	}

	return (-1);
}

void
qdevice_log_configure(struct qdevice_instance *instance)
{
	int to_stderr;
	int to_syslog;
	int syslog_facility;
	int syslog_priority;
	int logfile_priority;
	int debug;
	char *str;
	int i;
	int fileline;
	int timestamp;
	int function_name;
	char log_format_syslog[64];
	char log_format_stderr[64];

	to_stderr = QDEVICE_LOG_DEFAULT_TO_STDERR;
	if (cmap_get_string(instance->cmap_handle, "logging.to_stderr", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			qdevice_log(LOG_WARNING, "logging.to_stderr value is not valid");
		} else {
			to_stderr = i;
		}
		free(str);
	}

	if (cmap_get_string(instance->cmap_handle,
	    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".to_stderr", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			qdevice_log(LOG_WARNING,
			    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".to_stderr value is not valid.");
		} else {
			to_stderr = i;
		}
		free(str);
	}

	to_syslog = QDEVICE_LOG_DEFAULT_TO_SYSLOG;
	if (cmap_get_string(instance->cmap_handle, "logging.to_syslog", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			qdevice_log(LOG_WARNING, "logging.to_syslog value is not valid");
		} else {
			to_syslog = i;
		}
		free(str);
	}

	if (cmap_get_string(instance->cmap_handle,
	    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".to_syslog", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			qdevice_log(LOG_WARNING,
			    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".to_syslog value is not valid.");
		} else {
			to_syslog = i;
		}
		free(str);
	}

	syslog_facility = QDEVICE_LOG_DEFAULT_SYSLOG_FACILITY;
	if (cmap_get_string(instance->cmap_handle, "logging.syslog_facility", &str) == CS_OK) {
		if ((i = qb_log_facility2int(str)) < 0) {
			qdevice_log(LOG_WARNING, "logging.syslog_facility value is not valid");
		} else {
			syslog_facility = i;
		}

		free(str);
	}

	if (cmap_get_string(instance->cmap_handle,
	    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".syslog_facility", &str) == CS_OK) {
		if ((i = qb_log_facility2int(str)) < 0) {
			qdevice_log(LOG_WARNING,
			    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".syslog_facility value is not valid.");
		} else {
			syslog_facility = i;
		}
		free(str);
	}

	syslog_priority = QDEVICE_LOG_DEFAULT_SYSLOG_PRIORITY;
	if (cmap_get_string(instance->cmap_handle, "logging.syslog_priority", &str) == CS_OK) {
		if ((i = qdevice_log_priority_str_to_int(str)) < 0) {
			qdevice_log(LOG_WARNING, "logging.syslog_priority value is not valid");
		} else {
			syslog_priority = i;
		}

		free(str);
	}

	if (cmap_get_string(instance->cmap_handle,
	    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".syslog_priority", &str) == CS_OK) {
		if ((i = qdevice_log_priority_str_to_int(str)) < 0) {
			qdevice_log(LOG_WARNING,
			    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".syslog_priority value is not valid.");
		} else {
			syslog_priority = i;
		}
		free(str);
	}

	logfile_priority = QDEVICE_LOG_DEFAULT_SYSLOG_PRIORITY;
	if (cmap_get_string(instance->cmap_handle, "logging.logfile_priority", &str) == CS_OK) {
		if ((i = qdevice_log_priority_str_to_int(str)) < 0) {
			qdevice_log(LOG_WARNING, "logging.logfile_priority value is not valid");
		} else {
			logfile_priority = i;
		}

		free(str);
	}

	if (cmap_get_string(instance->cmap_handle,
	    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".logfile_priority", &str) == CS_OK) {
		if ((i = qdevice_log_priority_str_to_int(str)) < 0) {
			qdevice_log(LOG_WARNING,
			    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".logfile_priority value is not valid.");
		} else {
			logfile_priority = i;
		}
		free(str);
	}

	debug = QDEVICE_LOG_DEFAULT_DEBUG;
	if (cmap_get_string(instance->cmap_handle, "logging.debug", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			if (strcasecmp(str, "trace") == 0) {
				debug = 1;
			} else {
				qdevice_log(LOG_WARNING, "logging.debug value is not valid");
			}
		} else {
			debug = i;
		}
		free(str);
	}

	if (cmap_get_string(instance->cmap_handle,
	    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".debug", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			if (strcasecmp(str, "trace") == 0) {
				debug = 1;
			} else {
				qdevice_log(LOG_WARNING,
				    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".debug value is not valid.");
			}
		} else {
			debug = i;
		}
		free(str);
	}

	fileline = QDEVICE_LOG_DEFAULT_FILELINE;
	if (cmap_get_string(instance->cmap_handle, "logging.fileline", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			qdevice_log(LOG_WARNING, "logging.fileline value is not valid");
		} else {
			fileline = i;
		}
		free(str);
	}

	timestamp = QDEVICE_LOG_DEFAULT_TIMESTAMP;
	if (cmap_get_string(instance->cmap_handle, "logging.timestamp", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			qdevice_log(LOG_WARNING, "logging.timestamp value is not valid");
		} else {
			timestamp = i;
		}
		free(str);
	}

	function_name = QDEVICE_LOG_DEFAULT_FUNCTION_NAME;
	if (cmap_get_string(instance->cmap_handle, "logging.function_name", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			qdevice_log(LOG_WARNING, "logging.function_name value is not valid");
		} else {
			function_name = i;
		}
		free(str);
	}

	strcpy(log_format_syslog, "");

	if (fileline) {
		strcat(log_format_syslog, "%f:");

		if (function_name) {
			strcat(log_format_syslog, "%n:");
		}

		strcat(log_format_syslog, "%l ");
	}

	strcat(log_format_syslog, "%b");

	strcpy(log_format_stderr, "");
	if (timestamp) {
		strcpy(log_format_stderr, "%t %7p ");
	}
	strcat(log_format_stderr, log_format_syslog);

	if (qdevice_log_global_force_debug) {
		debug = 1;
	}

	/*
	 * Finally reconfigure log system
	 */
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, to_stderr);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, to_syslog);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_FACILITY, syslog_facility);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_CLEAR_ALL, QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*",
	    (debug ? LOG_DEBUG : syslog_priority));
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_CLEAR_ALL, QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*",
	    (debug ? LOG_DEBUG : logfile_priority));

	qb_log_format_set(QB_LOG_STDERR, log_format_stderr);
	qb_log_format_set(QB_LOG_SYSLOG, log_format_syslog);
}

void
qdevice_log_init(struct qdevice_instance *instance, int force_debug)
{
	qdevice_log_global_force_debug = force_debug;

	qb_log_init(QDEVICE_PROGRAM_NAME, QDEVICE_LOG_DEFAULT_SYSLOG_FACILITY,
	    QDEVICE_LOG_DEFAULT_SYSLOG_PRIORITY);

	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_ctl(QB_LOG_STDOUT, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, LOG_INFO - LOG_DEBUG);
	qb_log_format_set(QB_LOG_STDERR, "%t %7p %b");

	qdevice_log_configure(instance);
}

void
qdevice_log_close(struct qdevice_instance *instance)
{

	qb_log_fini();
}
