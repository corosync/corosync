/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/saAis.h"
#include "../include/list.h"
#include "util.h"
#include "mainconfig.h"
#include "mempool.h"
#include "logsys.h"
#include "totem.h"
#include "service.h"

static char error_string_response[512];

/* This just makes the code below a little neater */
static inline int objdb_get_string (
	struct objdb_iface_ver0 *objdb,
	unsigned int object_service_handle,
	char *key, char **value)
{
	int res;

	*value = NULL;
	if ( !(res = objdb->object_key_get (object_service_handle,
		key,
		strlen (key),
		(void *)value,
		NULL))) {

		if (*value) {
			return 0;
		}
	}
	return -1;
}

static inline void objdb_get_int (
	struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
	char *key, unsigned int *intvalue)
{
	char *value = NULL;

	if (!objdb->object_key_get (object_service_handle,
		key,
		strlen (key),
		(void *)&value,
		NULL)) {

		if (value) {
			*intvalue = atoi(value);
		}
	}
}

static struct logsys_config_struct {
	char subsys[6];
	unsigned int priority;
	unsigned int tags;
} logsys_logger;

int openais_main_config_read (
	struct objdb_iface_ver0 *objdb,
	char **error_string,
	struct main_config *main_config)
{
	unsigned int object_service_handle;
	unsigned int object_logger_subsys_handle;
	char *value;
	char *error_reason = error_string_response;

	memset (main_config, 0, sizeof (struct main_config));

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);

	main_config->logmode = LOG_MODE_FLUSH_AFTER_CONFIG;
	if (objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "logging",
		    strlen ("logging"),
		    &object_service_handle) == 0) {

		if (!objdb_get_string (objdb,object_service_handle, "to_file", &value)) {
			if (strcmp (value, "yes") == 0) {
				main_config->logmode |= LOG_MODE_OUTPUT_FILE;
			} else
			if (strcmp (value, "no") == 0) {
				main_config->logmode &= ~LOG_MODE_OUTPUT_FILE;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "to_syslog", &value)) {
			if (strcmp (value, "yes") == 0) {
				main_config->logmode |= LOG_MODE_OUTPUT_SYSLOG_THREADED;
			} else
			if (strcmp (value, "no") == 0) {
				main_config->logmode &= ~LOG_MODE_OUTPUT_SYSLOG_THREADED;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "to_stderr", &value)) {
			if (strcmp (value, "yes") == 0) {
				main_config->logmode |= LOG_MODE_OUTPUT_STDERR;
			} else
			if (strcmp (value, "no") == 0) {
				main_config->logmode &= ~LOG_MODE_OUTPUT_STDERR;
			}
		}

		if (!objdb_get_string (objdb,object_service_handle, "debug", &value)) {
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_DISPLAY_DEBUG;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_DISPLAY_DEBUG;
			} else {
				goto parse_error;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "timestamp", &value)) {
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_DISPLAY_TIMESTAMP;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_DISPLAY_TIMESTAMP;
			} else {
				goto parse_error;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "logfile", &value)) {
			main_config->logfile = strdup (value);
		}

		if (!objdb_get_string (objdb,object_service_handle, "fileline", &value)) {
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_DISPLAY_FILELINE;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_DISPLAY_FILELINE;
			} else {
				goto parse_error;
			}
		}

		if (!objdb_get_string (objdb,object_service_handle, "syslog_facility", &value)) {
			if (strcmp (value, "daemon") == 0) {
				main_config->syslog_facility = LOG_DAEMON;
			} else
			if (strcmp (value, "local0") == 0) {
				main_config->syslog_facility = LOG_LOCAL0;
			} else
			if (strcmp (value, "local1") == 0) {
				main_config->syslog_facility = LOG_LOCAL1;
			} else
			if (strcmp (value, "local2") == 0) {
				main_config->syslog_facility = LOG_LOCAL2;
			} else
			if (strcmp (value, "local3") == 0) {
				main_config->syslog_facility = LOG_LOCAL3;
			} else
			if (strcmp (value, "local4") == 0) {
				main_config->syslog_facility = LOG_LOCAL4;
			} else
			if (strcmp (value, "local5") == 0) {
				main_config->syslog_facility = LOG_LOCAL5;
			} else
			if (strcmp (value, "local6") == 0) {
				main_config->syslog_facility = LOG_LOCAL6;
			} else
			if (strcmp (value, "local7") == 0) {
				main_config->syslog_facility = LOG_LOCAL7;
			} else {
				error_reason = "unknown syslog facility specified";
				goto parse_error;
			}
		}

		while (objdb->object_find (object_service_handle,
			"logger_subsys",
			strlen ("logger_subsys"),
			&object_logger_subsys_handle) == 0) {

			if (!objdb_get_string (objdb,
				object_logger_subsys_handle,
				"subsys", &value)) {

				strncpy (logsys_logger.subsys, value,
					sizeof (logsys_logger.subsys));
			}
			else {
				error_reason = "subsys required for logger directive";
				goto parse_error;
			}
			if (!objdb_get_string (objdb, object_logger_subsys_handle, "debug", &value)) {
				if (strcmp (value, "on") == 0) {
					logsys_logger.priority = LOG_LEVEL_DEBUG;
				} else
				if (strcmp (value, "off") == 0) {
					logsys_logger.priority &= ~LOG_LEVEL_DEBUG;
				} else {
					goto parse_error;
				}
			}
			if (!objdb_get_string (objdb, object_logger_subsys_handle, "tags", &value)) {
				char *token = strtok (value, "|");

				while (token != NULL) {
					if (strcmp (token, "enter") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_ENTER;
					} else if (strcmp (token, "leave") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_LEAVE;
					} else if (strcmp (token, "trace1") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE1;
					} else if (strcmp (token, "trace2") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE2;
					} else if (strcmp (token, "trace3") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE3;
					} else if (strcmp (token, "trace4") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE4;
					} else if (strcmp (token, "trace5") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE5;
					} else if (strcmp (token, "trace6") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE6;
					} else if (strcmp (token, "trace7") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE7;
					} else if (strcmp (token, "trace8") == 0) {
						logsys_logger.tags |= LOGSYS_TAG_TRACE8;
					} else {
						error_reason = "bad tags value";
						goto parse_error;
					}

					token = strtok(NULL, "|");
				}
			}
			/*
			 * set individual logger configurations
			 */
			logsys_config_subsys_set (
				logsys_logger.subsys,
				logsys_logger.tags,
				logsys_logger.priority);
			
		}
	}

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);
	if (objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "aisexec",
		    strlen ("aisexec"),
		    &object_service_handle) == 0) {

		if (!objdb_get_string (objdb,object_service_handle, "user", &value)) {
			main_config->user = strdup(value);
		}
		if (!objdb_get_string (objdb,object_service_handle, "group", &value)) {
			main_config->group = strdup(value);
		}
	}

	/* Default user/group */
	if (!main_config->user)
		main_config->user = "ais";

	if (!main_config->group)
		main_config->group = "ais";

	if ((main_config->logmode & LOG_MODE_OUTPUT_FILE) &&
		(main_config->logfile == NULL)) {
		error_reason = "logmode set to 'file' but no logfile specified";
		goto parse_error;
	}

	if (main_config->syslog_facility == 0)
		main_config->syslog_facility = LOG_DAEMON;

	return 0;

parse_error:
	sprintf (error_string_response,
		 "parse error in config: %s.\n",
		 error_reason);

	*error_string = error_string_response;
	return (-1);
}
