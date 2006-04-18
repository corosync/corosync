/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#include "print.h"
#include "totem.h"
#include "service.h"

static char error_string_response[512];

/* This just makes the code below a little neater */
static inline int objdb_get_string(struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
				   char *key, char **value)
{
	int res;

	*value = NULL;
	if ( !(res = objdb->object_key_get (object_service_handle,
					    key,
					    strlen (key),
					    (void *)value,
					    NULL))) {
		if (*value)
			return 0;
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

int openais_main_config_read (
	struct objdb_iface_ver0 *objdb,
	char **error_string,
	struct main_config *main_config)
{
	unsigned int object_service_handle;
	unsigned int object_logger_handle;
	char *value;
	char *error_reason = error_string_response;
	int i;

	memset (main_config, 0, sizeof (struct main_config));

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);

	if (objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "logging",
		    strlen ("logging"),
		    &object_service_handle) == 0) {

		if (!objdb_get_string (objdb,object_service_handle, "to_file", &value)) {
			if (strcmp (value, "yes") == 0) {
				main_config->logmode |= LOG_MODE_FILE;
			} else
			if (strcmp (value, "no") == 0) {
				main_config->logmode &= ~LOG_MODE_FILE;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "to_syslog", &value)) {
			if (strcmp (value, "yes") == 0) {
				main_config->logmode |= LOG_MODE_SYSLOG;
			} else
			if (strcmp (value, "no") == 0) {
				main_config->logmode &= ~LOG_MODE_SYSLOG;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "to_stderr", &value)) {
			if (strcmp (value, "yes") == 0) {
				main_config->logmode |= LOG_MODE_STDERR;
			} else
			if (strcmp (value, "no") == 0) {
				main_config->logmode &= ~LOG_MODE_STDERR;
			}
		}

		if (!objdb_get_string (objdb,object_service_handle, "debug", &value)) {
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_DEBUG;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_DEBUG;
			} else {
				goto parse_error;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "timestamp", &value)) {
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_TIMESTAMP;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_TIMESTAMP;
			} else {
				goto parse_error;
			}
		}
		if (!objdb_get_string (objdb,object_service_handle, "logfile", &value)) {
			main_config->logfile = strdup (value);
		}

		if (!objdb_get_string (objdb,object_service_handle, "fileline", &value)) {
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_FILELINE;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_FILELINE;
			} else {
				goto parse_error;
			}
		}

		while (	objdb->object_find (object_service_handle,
									"logger",
									strlen ("logger"),
									&object_logger_handle) == 0) {
			main_config->logger =
				realloc(main_config->logger,
						sizeof(struct logger_config) *
						(main_config->loggers + 1));
			i = main_config->loggers;
			main_config->loggers++;
			memset(&main_config->logger[i], 0, sizeof(struct logger_config));

			if (!objdb_get_string (objdb, object_logger_handle, "ident", &value)) {
				main_config->logger[i].ident = value;
			}
			else {
				error_reason = "ident required for logger directive";
				goto parse_error;
			}
			if (!objdb_get_string (objdb, object_logger_handle, "debug", &value)) {
				if (strcmp (value, "on") == 0) {
					main_config->logger[i].level = LOG_LEVEL_DEBUG;
				} else
				if (strcmp (value, "off") == 0) {
					main_config->logger[i].level &= ~LOG_LEVEL_DEBUG;
				} else {
					goto parse_error;
				}
			}
			if (!objdb_get_string (objdb, object_logger_handle, "tags", &value)) {
				char *token = strtok (value, "|");

				while (token != NULL) {
					if (strcmp (token, "enter") == 0) {
						main_config->logger[i].tags |= TAG_ENTER;
					} else
					if (strcmp (token, "leave") == 0) {
						main_config->logger[i].tags |= TAG_LEAVE;
					} else
					if (strcmp (token, "trace1") == 0) {
						main_config->logger[i].tags |= TAG_TRACE1;
					} else
					if (strcmp (token, "trace2") == 0) {
						main_config->logger[i].tags |= TAG_TRACE2;
					} else
					if (strcmp (token, "trace3") == 0) {
						main_config->logger[i].tags |= TAG_TRACE3;
					}
					if (strcmp (token, "trace4") == 0) {
						main_config->logger[i].tags |= TAG_TRACE3;
					}
					if (strcmp (token, "trace5") == 0) {
						main_config->logger[i].tags |= TAG_TRACE3;
					}
					if (strcmp (token, "trace6") == 0) {
						main_config->logger[i].tags |= TAG_TRACE3;
					}
					if (strcmp (token, "trace7") == 0) {
						main_config->logger[i].tags |= TAG_TRACE3;
					}
					if (strcmp (token, "trace8") == 0) {
						main_config->logger[i].tags |= TAG_TRACE3;
					}

					token = strtok(NULL, "|");
				}
			}
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
		main_config->user = OPENAIS_USER;

	if (!main_config->group)
		main_config->group = OPENAIS_GROUP;

	if ((main_config->logmode & LOG_MODE_FILE) && main_config->logfile == 0) {
		error_reason = "logmode set to 'file' but no logfile specified";
		goto parse_error;
	}

	return 0;

parse_error:
	sprintf (error_string_response,
		 "parse error in config: %s.\n",
		 error_reason);

	*error_string = error_string_response;
	return (-1);
}
