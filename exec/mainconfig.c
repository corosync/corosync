/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
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

#include <config.h>

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
#include <limits.h>

#include <corosync/corotypes.h>
#include <corosync/list.h>
#include <corosync/totem/totem.h>
#include <corosync/engine/logsys.h>

#include "util.h"
#include "mainconfig.h"
#include "mempool.h"

static char error_string_response[512];
static struct objdb_iface_ver0 *global_objdb;

/* This just makes the code below a little neater */
static inline int objdb_get_string (
	const struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_service_handle,
	const char *key, char **value)
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
	const struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_service_handle,
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

/**
 * insert_into_buffer
 * @target_buffer: a buffer where to write results
 * @bufferlen: tell us the size of the buffer to avoid overflows
 * @entry: entry that needs to be added to the buffer
 * @after: can either be NULL or set to a string.
 *         if NULL, @entry is prependend to logsys_format_get buffer.
 *         if set, @entry is added immediately after @after.
 *
 * Since the function is specific to logsys_format_get handling, it is implicit
 * that source is logsys_format_get();
 *
 * In case of failure, target_buffer could be left dirty. So don't trust
 * any data leftover in it.
 *
 * Searching for "after" assumes that there is only entry of "after"
 * in the source. Afterall we control the string here and for logging format
 * it makes little to no sense to have duplicate format entries.
 *
 * Returns: 0 on success, -1 on failure
 **/
static int insert_into_buffer(
	char *target_buffer,
	size_t bufferlen,
	const char *entry,
	const char *after)
{
	const char *current_format = NULL;

	current_format = logsys_format_get();

	/* if the entry is already in the format we don't add it again */
	if (strstr(current_format, entry) != NULL) {
		return -1;
	}

	/* if there is no "after", simply prepend the requested entry
	 * otherwise go for beautiful string manipulation.... </sarcasm> */
	if (!after) {
		if (snprintf(target_buffer, bufferlen - 1, "%s%s",
				entry,
				current_format) >= bufferlen - 1) {
			return -1;
		}
	} else {
		const char *afterpos;
		size_t afterlen;
		size_t templen;

		/* check if after is contained in the format
		 * and afterlen has a meaning or return an error */
		afterpos = strstr(current_format, after);
		afterlen = strlen(after);
		if ((!afterpos) || (!afterlen)) {
			return -1;
		}

		templen = afterpos - current_format + afterlen;
		if (snprintf(target_buffer, templen + 1, "%s", current_format)
				>= bufferlen - 1) {
			return -1;
		}
		if (snprintf(target_buffer + templen, bufferlen - ( templen + 1 ),
				"%s%s", entry, current_format + templen)
				>= bufferlen - ( templen + 1 )) {
			return -1;
		}
	}
	return 0;
}

/*
 * format set is the only global specific option that
 * doesn't apply at system/subsystem level.
 */
static int corosync_main_config_format_set (
	struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_handle,
	const char **error_string)
{
	const char *error_reason;
	char new_format_buffer[PATH_MAX];
	char *value;
	int err = 0;

	if (!objdb_get_string (objdb,object_handle, "fileline", &value)) {
		if (strcmp (value, "on") == 0) {
			if (!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					" %f:%l", "s]")) {
				err = logsys_format_set(new_format_buffer);
			} else
			if (!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					"%f:%l", NULL)) {
				err = logsys_format_set(new_format_buffer);
			}
		} else
		if (strcmp (value, "off") == 0) {
			/* nothing to do here */
		} else {
			error_reason = "unknown value for fileline";
			goto parse_error;
		}
	}
	if (!objdb_get_string (objdb,object_handle, "function_name", &value)) {
		if (strcmp (value, "on") == 0) {
			if (!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					"%n:", "f:")) {
				err = logsys_format_set(new_format_buffer);
			} else
			if (!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					" %n", "s]")) {
				err = logsys_format_set(new_format_buffer);
			}
		} else
		if (strcmp (value, "off") == 0) {
			/* nothing to do here */
		} else {
			error_reason = "unknown value for function_name";
			goto parse_error;
		}
	}
	if (!objdb_get_string (objdb,object_handle, "timestamp", &value)) {
		if (strcmp (value, "on") == 0) {
			if(!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					"%t ", NULL)) {
				err = logsys_format_set(new_format_buffer);
			}
		} else
		if (strcmp (value, "off") == 0) {
			/* nothing to do here */
		} else {
			error_reason = "unknown value for timestamp";
			goto parse_error;
		}
	}
	if (err) {
		error_reason = "exhausted virtual memory";
		goto parse_error;
	}

	return (0);

parse_error:
	*error_string = error_reason;

	return (-1);
}

static int corosync_main_config_set (
	struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_handle,
	const char *subsys,
	const char **error_string)
{
	const char *error_reason = error_string_response;
	char *value;
	unsigned int mode;

	/*
	 * this bit abuses the internal logsys exported API
	 * to guarantee that all configured subsystems are
	 * initialized too.
	 *
	 * using this approach avoids some headaches caused
	 * by IPC and TOTEM that have a special logging
	 * handling requirements
	 */
	if (subsys != NULL) {
		if (_logsys_subsys_create(subsys) < 0) {
			error_reason = "unable to create new logging subsystem";
			goto parse_error;
		}
	}

	mode = logsys_config_mode_get(subsys);
	if (mode < 0) {
		error_reason = "unable to get mode";
		goto parse_error;
	}

	if (!objdb_get_string (objdb,object_handle, "to_file", &value)) {

		log_printf(LOGSYS_LEVEL_WARNING,
		 "Warning: the to_file config paramater has been obsoleted."
		 " See corosync.conf man page to_logfile directive.");

		if (strcmp (value, "yes") == 0) {
			mode |= LOGSYS_MODE_OUTPUT_FILE;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to set mode to_file";
				goto parse_error;
			}
		} else
		if (strcmp (value, "no") == 0) {
			mode &= ~LOGSYS_MODE_OUTPUT_FILE;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to unset mode to_file";
				goto parse_error;
			}
		} else {
			error_reason = "unknown value for to_file";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "to_logfile", &value)) {
		if (strcmp (value, "yes") == 0) {
			mode |= LOGSYS_MODE_OUTPUT_FILE;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to set mode to_logfile";
				goto parse_error;
			}
		} else
		if (strcmp (value, "no") == 0) {
			mode &= ~LOGSYS_MODE_OUTPUT_FILE;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to unset mode to_logfile";
				goto parse_error;
			}
		} else {
			error_reason = "unknown value for to_logfile";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "to_syslog", &value)) {
		if (strcmp (value, "yes") == 0) {
			mode |= LOGSYS_MODE_OUTPUT_SYSLOG;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to set mode to_syslog";
				goto parse_error;
			}
		} else
		if (strcmp (value, "no") == 0) {
			mode &= ~LOGSYS_MODE_OUTPUT_SYSLOG;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to unset mode to_syslog";
				goto parse_error;
			}
		} else {
			error_reason = "unknown value for to_syslog";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "to_stderr", &value)) {
		if (strcmp (value, "yes") == 0) {
			mode |= LOGSYS_MODE_OUTPUT_STDERR;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to set mode to_stderr";
				goto parse_error;
			}
		} else
		if (strcmp (value, "no") == 0) {
			mode &= ~LOGSYS_MODE_OUTPUT_STDERR;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				error_reason = "unable to unset mode to_stderr";
				goto parse_error;
			}
		} else {
			error_reason = "unknown value for to_syslog";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "syslog_facility", &value)) {
		int syslog_facility;

		syslog_facility = logsys_facility_id_get(value);
		if (syslog_facility < 0) {
			error_reason = "unknown syslog facility specified";
			goto parse_error;
		}
		if (logsys_config_syslog_facility_set(subsys,
						syslog_facility) < 0) {
			error_reason = "unable to set syslog facility";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "syslog_level", &value)) {
		int syslog_priority;

		log_printf(LOGSYS_LEVEL_WARNING,
		 "Warning: the syslog_level config paramater has been obsoleted."
		 " See corosync.conf man page syslog_priority directive.");

		syslog_priority = logsys_priority_id_get(value);
		if (syslog_priority < 0) {
			error_reason = "unknown syslog level specified";
			goto parse_error;
		}
		if (logsys_config_syslog_priority_set(subsys,
						syslog_priority) < 0) {
			error_reason = "unable to set syslog level";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "syslog_priority", &value)) {
		int syslog_priority;

		syslog_priority = logsys_priority_id_get(value);
		if (syslog_priority < 0) {
			error_reason = "unknown syslog priority specified";
			goto parse_error;
		}
		if (logsys_config_syslog_priority_set(subsys,
						syslog_priority) < 0) {
			error_reason = "unable to set syslog priority";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "logfile", &value)) {
		if (logsys_config_file_set (subsys, error_string, value) < 0) {
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb,object_handle, "logfile_priority", &value)) {
		int logfile_priority;

		logfile_priority = logsys_priority_id_get(value);
		if (logfile_priority < 0) {
			error_reason = "unknown logfile priority specified";
			goto parse_error;
		}
		if (logsys_config_logfile_priority_set(subsys,
						logfile_priority) < 0) {
			error_reason = "unable to set logfile priority";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb, object_handle, "debug", &value)) {
		if (strcmp (value, "on") == 0) {
			if (logsys_config_debug_set (subsys, 1) < 0) {
				error_reason = "unable to set debug on";
				goto parse_error;
			}
		} else
		if (strcmp (value, "off") == 0) {
			if (logsys_config_debug_set (subsys, 0) < 0) {
				error_reason = "unable to set debug off";
				goto parse_error;
			}
		} else {
			error_reason = "unknown value for debug";
			goto parse_error;
		}
	}

	if (!objdb_get_string (objdb, object_handle, "tags", &value)) {
		char *temp, *token;
		unsigned int tags = 0;

		temp = strdup(value);
		if (temp == NULL) {
			error_reason = "exhausted virtual memory";
			goto parse_error;
		}

		while ((token = strsep(&temp, "|")) != NULL) {
			int val;

			val = logsys_tag_id_get(token);
			if (val < 0) {
				error_reason = "bad tags value";
				goto parse_error;
			}
			tags |= val;
		}
		free(temp);

		tags |= LOGSYS_TAG_LOG;

		if (logsys_config_tags_set (subsys, tags) < 0) {
			error_reason = "unable to set tags";
			goto parse_error;
		}
	}

	return (0);

parse_error:
	*error_string = error_reason;

	return (-1);
}

static int corosync_main_config_read_logging (
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
{
	hdb_handle_t object_service_handle;
	hdb_handle_t object_logger_subsys_handle;
	hdb_handle_t object_find_handle;
	hdb_handle_t object_find_logsys_handle;
	const char *error_reason;
	char *value;

	objdb->object_find_create (
		OBJECT_PARENT_HANDLE,
		"logging",
		strlen ("logging"),
		&object_find_handle);

	if (objdb->object_find_next (
		object_find_handle,
		&object_service_handle) == 0) {

		/* format set is supported only for toplevel */
		if (corosync_main_config_format_set (objdb,
						       object_service_handle,
						       &error_reason) < 0) {
			goto parse_error;
		}

		if (corosync_main_config_set (objdb,
						object_service_handle,
						NULL,
						&error_reason) < 0) {
			goto parse_error;
		}

		/* we will need 2 of these to compensate for new logging
		 * config format */

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

				if (corosync_main_config_set (objdb,
						object_logger_subsys_handle,
						value,
						&error_reason) < 0) {
					goto parse_error;
				}
			}
			else {
				error_reason = "subsys required for logger directive";
				goto parse_error;
			}
		}
		objdb->object_find_destroy (object_find_logsys_handle);

		objdb->object_find_create (
			object_service_handle,
			"logging_daemon",
			strlen ("logging_daemon"),
			&object_find_logsys_handle);

		while (objdb->object_find_next (
			object_find_logsys_handle,
			&object_logger_subsys_handle) == 0) {

			if (!objdb_get_string (objdb,
				object_logger_subsys_handle,
				"name", &value)) {

				if ((strcmp(value, "corosync") == 0) &&
				   (!objdb_get_string (objdb,
					object_logger_subsys_handle,
					"subsys", &value))) {

					if (corosync_main_config_set (objdb,
							object_logger_subsys_handle,
							value,
							&error_reason) < 0) {
						goto parse_error;
					}
				}
				else {
					error_reason = "subsys required for logging_daemon directive";
					goto parse_error;
				}
			}
			else {
				error_reason = "name required for logging_daemon directive";
				goto parse_error;
			}
		}
		objdb->object_find_destroy (object_find_logsys_handle);
	}
	objdb->object_find_destroy (object_find_handle);

	return 0;

parse_error:
	*error_string = error_reason;

	return (-1);
}

static int uid_determine (const char *req_user)
{
	struct passwd *passwd;
	int ais_uid = 0;

	passwd = getpwnam(req_user);
	if (passwd == 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "ERROR: The '%s' user is not found in /etc/passwd, please read the documentation.\n", req_user);
		corosync_exit_error (AIS_DONE_UID_DETERMINE);
	}
	ais_uid = passwd->pw_uid;
	endpwent ();
	return ais_uid;
}

static int gid_determine (const char *req_group)
{
	struct group *group;
	int ais_gid = 0;

	group = getgrnam (req_group);
	if (group == 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "ERROR: The '%s' group is not found in /etc/group, please read the documentation.\n", req_group);
		corosync_exit_error (AIS_DONE_GID_DETERMINE);
	}
	ais_gid = group->gr_gid;
	endgrent ();
	return ais_gid;
}

static void main_objdb_reload_notify(objdb_reload_notify_type_t type, int flush,
				     void *priv_data_pt)
{
	const char *error_string;

	if (type == OBJDB_RELOAD_NOTIFY_END) {

		/*
		 * Reload the logsys configuration
		 */
		logsys_format_set(NULL);
		corosync_main_config_read_logging(global_objdb,
						  &error_string);
	}
}

static void add_logsys_config_notification(
	struct objdb_iface_ver0 *objdb)
{

	global_objdb = objdb;

	objdb->object_track_start(OBJECT_PARENT_HANDLE,
				  1,
				  NULL,
				  NULL,
				  NULL,
				  main_objdb_reload_notify,
				  NULL);

}

int corosync_main_config_read (
	struct objdb_iface_ver0 *objdb,
	const char **error_string,
	struct ug_config *ug_config)
{
	hdb_handle_t object_service_handle;
	char *value;
	const char *error_reason = error_string_response;
	hdb_handle_t object_find_handle;

	memset (ug_config, 0, sizeof (struct ug_config));

	if (corosync_main_config_read_logging(objdb, error_string) < 0) {
		error_reason = *error_string;
		goto parse_error;
	}

	ug_config->uid = -1;
	ug_config->gid = -1;

	objdb->object_find_create (
		OBJECT_PARENT_HANDLE,
		"aisexec",
		strlen ("aisexec"),
		&object_find_handle);

	if (objdb->object_find_next (
		object_find_handle,
		&object_service_handle) == 0) {

		if (!objdb_get_string (objdb,object_service_handle, "user", &value)) {
			ug_config->uid = uid_determine(value);
		}

		if (!objdb_get_string (objdb,object_service_handle, "group", &value)) {
			ug_config->gid = gid_determine(value);
		}
	}

	objdb->object_find_destroy (object_find_handle);

	if (ug_config->uid < 0) {
		ug_config->uid = uid_determine("ais");
	}
	if (ug_config->gid < 0) {
		ug_config->gid = gid_determine("ais");
	}

	add_logsys_config_notification(objdb);

	logsys_fork_completed ();

	return 0;

parse_error:
	snprintf (error_string_response, sizeof(error_string_response),
		 "parse error in config: %s.\n",
		 error_reason);

	*error_string = error_string_response;
	return (-1);
}
