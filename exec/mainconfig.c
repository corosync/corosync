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

typedef enum {
	MAIN_HEAD,
	MAIN_LOGGING,
	MAIN_EVENT,
	MAIN_AMF,
	MAIN_COMPONENTS
} main_parse_t;

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

extern int openais_main_config_read (
	struct objdb_iface_ver0 *objdb,
	char **error_string,
	struct main_config *main_config,
	int interface_max)
{
	unsigned int object_service_handle;
	char *value;
	char *error_reason = error_string_response;

	memset (main_config, 0, sizeof (struct main_config));

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);

	if (objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "logging",
		    strlen ("logging"),
		    &object_service_handle) == 0) {

		if (!objdb_get_string (objdb,object_service_handle, "logoutput", &value)) {
			if (strcmp (value, "file") == 0) {
				main_config->logmode |= LOG_MODE_FILE;
			} else
			if (strcmp (value, "syslog") == 0) {
				main_config->logmode |= LOG_MODE_SYSLOG;
			} else
			if (strcmp (value, "stderr") == 0) {
				main_config->logmode |= LOG_MODE_STDERR;
			} else {
				goto parse_error;
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
	}

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
