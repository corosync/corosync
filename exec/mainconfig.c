/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2008 Red Hat, Inc.
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
#include <pwd.h>
#include <grp.h>

#include <corosync/corotypes.h>
#include <corosync/list.h>
#include <corosync/totem/totem.h>
#include <corosync/engine/logsys.h>

#include "util.h"
#include "mainconfig.h"
#include "mempool.h"

static char error_string_response[512];
static struct objdb_iface_ver0 *global_objdb;

static void add_logsys_config_notification(
	struct objdb_iface_ver0 *objdb,
	struct main_config *main_config);


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




int corosync_main_config_read_logging (
	struct objdb_iface_ver0 *objdb,
	char **error_string,
	struct main_config *main_config)
{
	unsigned int object_service_handle;
	unsigned int object_logger_subsys_handle;
	char *value;
	char *error_reason = error_string_response;
	unsigned int object_find_handle;
	unsigned int object_find_logsys_handle;

	objdb->object_find_create (
		OBJECT_PARENT_HANDLE,
		"logging",
		strlen ("logging"),
		&object_find_handle);

	main_config->logmode = LOG_MODE_THREADED | LOG_MODE_FORK;
	if (objdb->object_find_next (
		object_find_handle,
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
				main_config->logmode |= LOG_MODE_OUTPUT_SYSLOG;
			} else
			if (strcmp (value, "no") == 0) {
				main_config->logmode &= ~LOG_MODE_OUTPUT_SYSLOG;
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
		if (!objdb_get_string (objdb,object_service_handle, "timestamp", &value)) {
/* todo change format string
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_DISPLAY_TIMESTAMP;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_DISPLAY_TIMESTAMP;
			} else {
				goto parse_error;
			}
*/
		}

		/* free old string on reload */
		if (main_config->logfile) {
			free(main_config->logfile);
			main_config->logfile = NULL;
		}
		if (!objdb_get_string (objdb,object_service_handle, "logfile", &value)) {
			main_config->logfile = strdup (value);
		}

		if (!objdb_get_string (objdb,object_service_handle, "fileline", &value)) {
/* TODO
			if (strcmp (value, "on") == 0) {
				main_config->logmode |= LOG_MODE_DISPLAY_FILELINE;
			} else
			if (strcmp (value, "off") == 0) {
				main_config->logmode &= ~LOG_MODE_DISPLAY_FILELINE;
			} else {
				goto parse_error;
			}
*/
		}

		if (!objdb_get_string (objdb,object_service_handle, "syslog_facility", &value)) {
			main_config->syslog_facility = logsys_facility_id_get(value);
			if (main_config->syslog_facility < 0) {
				error_reason = "unknown syslog facility specified";
				goto parse_error;
			}
		}

		logsys_config_facility_set ("corosync", main_config->syslog_facility);
		logsys_config_mode_set (main_config->logmode);
		logsys_config_file_set (error_string, main_config->logfile);

		objdb->object_find_create (
			object_service_handle,
			"logger_subsys",
			strlen ("logger_subsys"),
			&object_find_logsys_handle);

		while (objdb->object_find_next (
			object_find_logsys_handle,
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
			if (!objdb_get_string (objdb, object_logger_subsys_handle, "syslog_level", &value)) {
				logsys_logger.priority = logsys_priority_id_get(value);
				if (logsys_logger.priority < 0) {
					error_reason = "unknown syslog priority specified";
					goto parse_error;
				}
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
					int val;

					val = logsys_tag_id_get(token);
					if (val < 0) {
						error_reason = "bad tags value";
						goto parse_error;
					}
					logsys_logger.tags |= val;
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
		objdb->object_find_destroy (object_find_logsys_handle);
	}

	objdb->object_find_destroy (object_find_handle);

	return 0;

parse_error:
	sprintf (error_string_response,
		 "parse error in config: %s.\n",
		 error_reason);

	*error_string = error_string_response;
	return (-1);
}

static int uid_determine (char *req_user)
{
	struct passwd *passwd;
	int ais_uid = 0;

	passwd = getpwnam(req_user);
	if (passwd == 0) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: The '%s' user is not found in /etc/passwd, please read the documentation.\n", req_user);
		corosync_exit_error (AIS_DONE_UID_DETERMINE);
	}
	ais_uid = passwd->pw_uid;
	endpwent ();
	return ais_uid;
}

static int gid_determine (char *req_group)
{
	struct group *group;
	int ais_gid = 0;

	group = getgrnam (req_group);
	if (group == 0) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: The '%s' group is not found in /etc/group, please read the documentation.\n", req_group);
		corosync_exit_error (AIS_DONE_GID_DETERMINE);
	}
	ais_gid = group->gr_gid;
	endgrent ();
	return ais_gid;
}

int corosync_main_config_read (
	struct objdb_iface_ver0 *objdb,
	char **error_string,
	struct main_config *main_config)
{
	unsigned int object_service_handle;
	char *value;
	char *error_reason = error_string_response;
	unsigned int object_find_handle;

	memset (main_config, 0, sizeof (struct main_config));

	corosync_main_config_read_logging(objdb, error_string, main_config);

	objdb->object_find_create (
		OBJECT_PARENT_HANDLE,
		"aisexec",
		strlen ("aisexec"),
		&object_find_handle);

	if (objdb->object_find_next (
		object_find_handle,
		&object_service_handle) == 0) {

		if (!objdb_get_string (objdb,object_service_handle, "user", &value)) {
			main_config->uid = uid_determine(value);
		} else
			main_config->uid = uid_determine("ais");

		if (!objdb_get_string (objdb,object_service_handle, "group", &value)) {
			main_config->gid = gid_determine(value);
		} else
			main_config->gid = gid_determine("ais");
	}

	objdb->object_find_destroy (object_find_handle);

	if ((main_config->logmode & LOG_MODE_OUTPUT_FILE) &&
		(main_config->logfile == NULL)) {
		error_reason = "logmode set to 'file' but no logfile specified";
		goto parse_error;
	}

	if (main_config->syslog_facility == 0)
		main_config->syslog_facility = LOG_DAEMON;

	add_logsys_config_notification(objdb, main_config);

	logsys_fork_completed ();

	return 0;

parse_error:
	sprintf (error_string_response,
		 "parse error in config: %s.\n",
		 error_reason);

	*error_string = error_string_response;
	return (-1);
}


static void main_objdb_reload_notify(objdb_reload_notify_type_t type, int flush,
				     void *priv_data_pt)
{
	struct main_config *main_config = priv_data_pt;
	char *error_string;

	if (type == OBJDB_RELOAD_NOTIFY_END) {

		/*
		 * Reload the logsys configuration
		 */
		corosync_main_config_read_logging(global_objdb,
						  &error_string,
						  main_config);
	}
}

static void add_logsys_config_notification(
	struct objdb_iface_ver0 *objdb,
	struct main_config *main_config)
{

	global_objdb = objdb;

	objdb->object_track_start(OBJECT_PARENT_HANDLE,
				  1,
				  NULL,
				  NULL,
				  NULL,
				  main_objdb_reload_notify,
				  main_config);

}
