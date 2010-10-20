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

static char error_string_response[512];

static struct objdb_iface_ver0 *global_objdb;

DECLARE_LIST_INIT(uidgid_list_head);


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

static int corosync_main_config_log_destination_set (
	struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_handle,
	const char *subsys,
	const char **error_string,
	const char *objdb_key,
	unsigned int mode_mask,
	char deprecated,
	const char *replacement)
{
	static char formatted_error_reason[128];
	char *value;
	unsigned int mode;

	if (!objdb_get_string (objdb, object_handle, objdb_key, &value)) {
		if (deprecated) {
			log_printf(LOGSYS_LEVEL_WARNING,
			 "Warning: the %s config paramater has been obsoleted."
			 " See corosync.conf man page %s directive.",
			 objdb_key, replacement);
		}

		mode = logsys_config_mode_get (subsys);

		if (strcmp (value, "yes") == 0 || strcmp (value, "on") == 0) {
			mode |= mode_mask;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				sprintf (formatted_error_reason, "unable to set mode %s", objdb_key);
				*error_string = formatted_error_reason;
				return -1;
			}
		} else
		if (strcmp (value, "no") == 0 || strcmp (value, "off") == 0) {
			mode &= ~mode_mask;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				sprintf (formatted_error_reason, "unable to unset mode %s", objdb_key);
				*error_string = formatted_error_reason;
				return -1;
			}
		} else {
			sprintf (formatted_error_reason, "unknown value for %s", objdb_key);
			*error_string = formatted_error_reason;
			return -1;
		}
	}

	return 0;
}

static int corosync_main_config_set (
	struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_handle,
	const char *subsys,
	const char **error_string)
{
	const char *error_reason = error_string_response;
	char *value;
	int mode;

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

	if (corosync_main_config_log_destination_set (objdb, object_handle, subsys, &error_reason,
	    "to_logfile", LOGSYS_MODE_OUTPUT_FILE, 0, NULL) != 0)
		goto parse_error;

	if (corosync_main_config_log_destination_set (objdb, object_handle, subsys, &error_reason,
	    "to_stderr", LOGSYS_MODE_OUTPUT_STDERR, 0, NULL) != 0)
		goto parse_error;

	if (corosync_main_config_log_destination_set (objdb, object_handle, subsys, &error_reason,
	    "to_syslog", LOGSYS_MODE_OUTPUT_SYSLOG, 0, NULL) != 0)
		goto parse_error;

	if (corosync_main_config_log_destination_set (objdb, object_handle, subsys, &error_reason,
	    "to_file", LOGSYS_MODE_OUTPUT_FILE, 1, "to_logfile") != 0)
		goto parse_error;

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

				if (strcmp(value, "corosync") == 0) {
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
						if (corosync_main_config_set (objdb,
								object_logger_subsys_handle,
								NULL,
								&error_reason) < 0) {
							goto parse_error;
						}
					}
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
	struct passwd passwd;
	struct passwd* pwdptr = &passwd;
	struct passwd* temp_pwd_pt;
	char pwdbuffer[200];
	int  pwdlinelen = sizeof(pwdbuffer);

	if ((getpwnam_r (req_user, pwdptr, pwdbuffer, pwdlinelen, &temp_pwd_pt)) != 0) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"ERROR: The '%s' user is not found in /etc/passwd, please read the documentation.\n",
			req_user);
		corosync_exit_error (AIS_DONE_UID_DETERMINE);
	}

	return passwd.pw_uid;
}

static int gid_determine (const char *req_group)
{
	int ais_gid = 0;
	struct group group;
	struct group * grpptr = &group;
	struct group * temp_grp_pt;
	char grpbuffer[200];
	int  grplinelen = sizeof(grpbuffer);

	if ((getgrnam_r (req_group, grpptr, grpbuffer, grplinelen, &temp_grp_pt)) != 0) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"ERROR: The '%s' group is not found in /etc/group, please read the documentation.\n",
			req_group);
		corosync_exit_error (AIS_DONE_GID_DETERMINE);
	}
	ais_gid = group.gr_gid;
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

static int corosync_main_config_read_uidgid (
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
{
	hdb_handle_t object_find_handle;
	hdb_handle_t object_service_handle;
	char *value;
	int uid, gid;
	struct uidgid_item *ugi;

	objdb->object_find_create (
		OBJECT_PARENT_HANDLE,
		"uidgid",
		strlen ("uidgid"),
		&object_find_handle);

	while (objdb->object_find_next (
		object_find_handle,
		&object_service_handle) == 0) {
		uid = -1;
		gid = -1;

		if (!objdb_get_string (objdb,object_service_handle, "uid", &value)) {
			uid = uid_determine(value);
		}

		if (!objdb_get_string (objdb,object_service_handle, "gid", &value)) {
			gid = gid_determine(value);
		}

		if (uid > -1 || gid > -1) {
			ugi = malloc (sizeof (*ugi));
			if (ugi == NULL) {
				_corosync_out_of_memory_error();
			}
			ugi->uid = uid;
			ugi->gid = gid;
			list_init (&ugi->list);
			list_add (&ugi->list, &uidgid_list_head);
		}
	}
	objdb->object_find_destroy (object_find_handle);

	return 0;
}

int corosync_main_config_read (
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
{
	const char *error_reason = error_string_response;

	if (corosync_main_config_read_logging(objdb, error_string) < 0) {
		error_reason = *error_string;
		goto parse_error;
	}

	corosync_main_config_read_uidgid (objdb, error_string);

	add_logsys_config_notification(objdb);

	return 0;

parse_error:
	snprintf (error_string_response, sizeof(error_string_response),
		 "parse error in config: %s.\n",
		 error_reason);

	*error_string = error_string_response;
	return (-1);
}

int corosync_main_config_compatibility_read (
        struct objdb_iface_ver0 *objdb,
        enum cs_sync_mode *minimum_sync_mode,
        const char **error_string)
{
	const char *error_reason = error_string_response;
	char *value;

	*minimum_sync_mode = CS_SYNC_V1;
	if (!objdb_get_string (objdb, OBJECT_PARENT_HANDLE, "compatibility", &value)) {

		if (strcmp (value, "whitetank") == 0) {
			*minimum_sync_mode = CS_SYNC_V1;
		} else
		if (strcmp (value, "none") == 0) {
			*minimum_sync_mode = CS_SYNC_V2;
		} else {

			snprintf (error_string_response, sizeof (error_string_response),
				"Invalid compatibility option '%s' specified, must be none or whitetank.\n", value);
			goto parse_error;
		}
	}

	return 0;

parse_error:
	*error_string = error_reason;

	return (-1);
}
