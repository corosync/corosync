/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *         Jan Friesse (jfriesse@redhat.com)
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

#include "config.h"

#include <corosync/totem/totem.h>
#include <corosync/logsys.h>
#ifdef LOGCONFIG_USE_ICMAP
#include <corosync/icmap.h>
#define MAP_KEYNAME_MAXLEN	ICMAP_KEYNAME_MAXLEN
#define map_get_string(key_name, value)	icmap_get_string(key_name, value)
#else
#include <corosync/cmap.h>
static cmap_handle_t cmap_handle;
static const char *main_logfile;
#define MAP_KEYNAME_MAXLEN	CMAP_KEYNAME_MAXLEN
#define map_get_string(key_name, value) cmap_get_string(cmap_handle, key_name, value)
#endif

#include "util.h"
#include "logconfig.h"

static char error_string_response[512];

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
	const char **error_string)
{
	const char *error_reason;
	char new_format_buffer[PATH_MAX];
	char *value = NULL;
	int err = 0;

	if (map_get_string("logging.fileline", &value) == CS_OK) {
		if (strcmp (value, "on") == 0) {
			if (!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					" %f:%l", "g]")) {
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
			free(value);
			goto parse_error;
		}

		free(value);
	}

	if (err) {
		error_reason = "not enough memory to set logging format buffer";
		goto parse_error;
	}

	if (map_get_string("logging.function_name", &value) == CS_OK) {
		if (strcmp (value, "on") == 0) {
			if (!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					"%n:", "f:")) {
				err = logsys_format_set(new_format_buffer);
			} else
			if (!insert_into_buffer(new_format_buffer,
					sizeof(new_format_buffer),
					" %n", "g]")) {
				err = logsys_format_set(new_format_buffer);
			}
		} else
		if (strcmp (value, "off") == 0) {
			/* nothing to do here */
		} else {
			error_reason = "unknown value for function_name";
			free(value);
			goto parse_error;
		}

		free(value);
	}

	if (err) {
		error_reason = "not enough memory to set logging format buffer";
		goto parse_error;
	}

	if (map_get_string("logging.timestamp", &value) == CS_OK) {
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
			free(value);
			goto parse_error;
		}

		free(value);
	}

	if (err) {
		error_reason = "not enough memory to set logging format buffer";
		goto parse_error;
	}

	return (0);

parse_error:
	*error_string = error_reason;

	return (-1);
}

static int corosync_main_config_log_destination_set (
	const char *path,
	const char *key,
	const char *subsys,
	const char **error_string,
	unsigned int mode_mask,
	char deprecated,
	char default_value,
	const char *replacement)
{
	static char formatted_error_reason[128];
	char *value = NULL;
	unsigned int mode;
	char key_name[MAP_KEYNAME_MAXLEN];

	snprintf(key_name, MAP_KEYNAME_MAXLEN, "%s.%s", path, key);
	if (map_get_string(key_name, &value) == CS_OK) {
		if (deprecated) {
			log_printf(LOGSYS_LEVEL_WARNING,
			 "Warning: the %s config parameter has been obsoleted."
			 " See corosync.conf man page %s directive.",
			 key, replacement);
		}

		mode = logsys_config_mode_get (subsys);

		if (strcmp (value, "yes") == 0 || strcmp (value, "on") == 0) {
			mode |= mode_mask;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				sprintf (formatted_error_reason, "unable to set mode %s", key);
				goto parse_error;
			}
		} else
		if (strcmp (value, "no") == 0 || strcmp (value, "off") == 0) {
			mode &= ~mode_mask;
			if (logsys_config_mode_set(subsys, mode) < 0) {
				sprintf (formatted_error_reason, "unable to unset mode %s", key);
				goto parse_error;
			}
		} else {
			sprintf (formatted_error_reason, "unknown value for %s", key);
			goto parse_error;
		}
	}
	/* Set to default if we are the top-level logger */
	else if (!subsys && !deprecated) {

		mode = logsys_config_mode_get (subsys);
		if (default_value) {
			mode |= mode_mask;
		}
		else {
			mode &= ~mode_mask;
		}
		if (logsys_config_mode_set(subsys, mode) < 0) {
			sprintf (formatted_error_reason, "unable to change mode %s", key);
			goto parse_error;
		}
	}

	free(value);
	return (0);

parse_error:
	*error_string = formatted_error_reason;
	free(value);
	return (-1);
}

static int corosync_main_config_set (
	const char *path,
	const char *subsys,
	const char **error_string)
{
	const char *error_reason = error_string_response;
	char *value = NULL;
	int mode;
	char key_name[MAP_KEYNAME_MAXLEN];

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
		if (_logsys_subsys_create(subsys, NULL) < 0) {
			error_reason = "unable to create new logging subsystem";
			goto parse_error;
		}
	}

	mode = logsys_config_mode_get(subsys);
	if (mode < 0) {
		error_reason = "unable to get mode";
		goto parse_error;
	}

	if (corosync_main_config_log_destination_set (path, "to_stderr", subsys, &error_reason,
	    LOGSYS_MODE_OUTPUT_STDERR, 0, 1, NULL) != 0)
		goto parse_error;

	if (corosync_main_config_log_destination_set (path, "to_syslog", subsys, &error_reason,
	    LOGSYS_MODE_OUTPUT_SYSLOG, 0, 1, NULL) != 0)
		goto parse_error;

	snprintf(key_name, MAP_KEYNAME_MAXLEN, "%s.%s", path, "syslog_facility");
	if (map_get_string(key_name, &value) == CS_OK) {
		int syslog_facility;

		syslog_facility = qb_log_facility2int(value);
		if (syslog_facility < 0) {
			error_reason = "unknown syslog facility specified";
			goto parse_error;
		}
		if (logsys_config_syslog_facility_set(subsys,
						syslog_facility) < 0) {
			error_reason = "unable to set syslog facility";
			goto parse_error;
		}

		free(value);
	}
	else {
		/* Set default here in case of a reload */
		if (logsys_config_syslog_facility_set(subsys,
						      qb_log_facility2int("daemon")) < 0) {
			error_reason = "unable to set syslog facility";
			goto parse_error;
		}
	}

	snprintf(key_name, MAP_KEYNAME_MAXLEN, "%s.%s", path, "syslog_level");
	if (map_get_string(key_name, &value) == CS_OK) {
		int syslog_priority;

		log_printf(LOGSYS_LEVEL_WARNING,
		 "Warning: the syslog_level config parameter has been obsoleted."
		 " See corosync.conf man page syslog_priority directive.");

		syslog_priority = logsys_priority_id_get(value);
		free(value);

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

	snprintf(key_name, MAP_KEYNAME_MAXLEN, "%s.%s", path, "syslog_priority");
	if (map_get_string(key_name, &value) == CS_OK) {
		int syslog_priority;

		syslog_priority = logsys_priority_id_get(value);
		free(value);
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
	else if(strcmp(key_name, "logging.syslog_priority") == 0){
		if (logsys_config_syslog_priority_set(subsys,
						      logsys_priority_id_get("info")) < 0) {
			error_reason = "unable to set syslog level";
			goto parse_error;
		}
	}

#ifdef LOGCONFIG_USE_ICMAP
	snprintf(key_name, MAP_KEYNAME_MAXLEN, "%s.%s", path, "logfile");
	if (map_get_string(key_name, &value) == CS_OK) {
		if (logsys_config_file_set (subsys, &error_reason, value) < 0) {
			goto parse_error;
		}
		free(value);
	}
#else
	if (!subsys) {
		if (logsys_config_file_set (subsys, &error_reason, main_logfile) < 0) {
			goto parse_error;
		}
	}
#endif

	if (corosync_main_config_log_destination_set (path, "to_file", subsys, &error_reason,
	   LOGSYS_MODE_OUTPUT_FILE, 1, 0, "to_logfile") != 0)
		goto parse_error;

	if (corosync_main_config_log_destination_set (path, "to_logfile", subsys, &error_reason,
	   LOGSYS_MODE_OUTPUT_FILE, 0, 0, NULL) != 0)
		goto parse_error;

	snprintf(key_name, MAP_KEYNAME_MAXLEN, "%s.%s", path, "logfile_priority");
	if (map_get_string(key_name, &value) == CS_OK) {
		int logfile_priority;

		logfile_priority = logsys_priority_id_get(value);
		free(value);
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
	else if(strcmp(key_name,"logging.logfile_priority") == 0){
		if (logsys_config_logfile_priority_set(subsys,
						      logsys_priority_id_get("info")) < 0) {
			error_reason = "unable to set syslog level";
			goto parse_error;
		}
	}

	snprintf(key_name, MAP_KEYNAME_MAXLEN, "%s.%s", path, "debug");
	if (map_get_string(key_name, &value) == CS_OK) {
		if (strcmp (value, "trace") == 0) {
			if (logsys_config_debug_set (subsys, LOGSYS_DEBUG_TRACE) < 0) {
				error_reason = "unable to set debug trace";
				free(value);
				goto parse_error;
			}
		} else
		if (strcmp (value, "on") == 0) {
			if (logsys_config_debug_set (subsys, LOGSYS_DEBUG_ON) < 0) {
				error_reason = "unable to set debug on";
				free(value);
				goto parse_error;
			}
		} else
		if (strcmp (value, "off") == 0) {
			if (logsys_config_debug_set (subsys, LOGSYS_DEBUG_OFF) < 0) {
				error_reason = "unable to set debug off";
				free(value);
				goto parse_error;
			}
		} else {
			error_reason = "unknown value for debug";
			free(value);
			goto parse_error;
		}
		free(value);
	}
	else {
		if (logsys_config_debug_set (subsys, LOGSYS_DEBUG_OFF) < 0) {
			error_reason = "unable to set debug off";
			free(value);
			goto parse_error;
		}
	}

	return (0);

parse_error:
	*error_string = error_reason;

	return (-1);
}

static int corosync_main_config_read_logging (
	const char **error_string)
{
	const char *error_reason;
#ifdef LOGCONFIG_USE_ICMAP
	icmap_iter_t iter;
	const char *key_name;
#else
	cmap_iter_handle_t iter;
	char key_name[CMAP_KEYNAME_MAXLEN];
#endif
	char key_subsys[MAP_KEYNAME_MAXLEN];
	char key_item[MAP_KEYNAME_MAXLEN];
	int res;

	/* format set is supported only for toplevel */
	if (corosync_main_config_format_set(&error_reason) < 0) {
		goto parse_error;
	}

	if (corosync_main_config_set ("logging", NULL, &error_reason) < 0) {
		goto parse_error;
	}

	/*
	 * we will need 2 of these to compensate for new logging
	 * config format
	 */
#ifdef LOGCONFIG_USE_ICMAP
	iter = icmap_iter_init("logging.logger_subsys.");
	while ((key_name = icmap_iter_next(iter, NULL, NULL)) != NULL) {
#else
	cmap_iter_init(cmap_handle, "logging.logger_subsys.", &iter);
	while ((cmap_iter_next(cmap_handle, iter, key_name, NULL, NULL)) == CS_OK) {
#endif
		res = sscanf(key_name, "logging.logger_subsys.%[^.].%s", key_subsys, key_item);

		if (res != 2) {
			continue ;
		}

		if (strcmp(key_item, "subsys") != 0) {
			continue ;
		}

		snprintf(key_item, MAP_KEYNAME_MAXLEN, "logging.logger_subsys.%s", key_subsys);

		if (corosync_main_config_set(key_item, key_subsys, &error_reason) < 0) {
			goto parse_error;
		}
	}
#ifdef LOGCONFIG_USE_ICMAP
	icmap_iter_finalize(iter);
#else
	cmap_iter_finalize(cmap_handle, iter);
#endif

	logsys_config_apply();
	return 0;

parse_error:
	*error_string = error_reason;

	return (-1);
}

#ifdef LOGCONFIG_USE_ICMAP 
static void main_logging_notify(
		int32_t event,
		const char *key_name,
		struct icmap_notify_value new_val,
		struct icmap_notify_value old_val,
		void *user_data)
#else
static void main_logging_notify(
		cmap_handle_t cmap_handle_unused,
		cmap_handle_t cmap_track_handle_unused,
		int32_t event,
		const char *key_name,
		struct cmap_notify_value new_val,
		struct cmap_notify_value old_val,
		void *user_data)
#endif
{
	const char *error_string;
	static int reload_in_progress = 0;

	/* If a full reload happens then suspend updates for individual keys until
	 * it's all completed
	 */
	if (strcmp(key_name, "config.reload_in_progress") == 0) {
		if (*(uint8_t *)new_val.data == 1) {
			reload_in_progress = 1;
		} else {
			reload_in_progress = 0;
		}
	}
	if (reload_in_progress) {
		log_printf(LOGSYS_LEVEL_DEBUG, "Ignoring key change, reload in progress. %s\n", key_name);
		return;
	}

	/*
	 * Reload the logsys configuration
	 */
	if (logsys_format_set(NULL) == -1) {
		fprintf (stderr, "Unable to setup logging format.\n");
	}
	corosync_main_config_read_logging(&error_string);
}

#ifdef LOGCONFIG_USE_ICMAP
static void add_logsys_config_notification(void)
{
	icmap_track_t icmap_track = NULL;

	icmap_track_add("logging.",
			ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
			main_logging_notify,
			NULL,
			&icmap_track);

	icmap_track_add("config.reload_in_progress",
			ICMAP_TRACK_ADD | ICMAP_TRACK_MODIFY,
			main_logging_notify,
			NULL,
			&icmap_track);
}
#else
static void add_logsys_config_notification(void)
{
	cmap_track_handle_t cmap_track;

	cmap_track_add(cmap_handle, "logging.",
			CMAP_TRACK_ADD | CMAP_TRACK_DELETE | CMAP_TRACK_MODIFY | CMAP_TRACK_PREFIX,
			main_logging_notify,
			NULL,
			&cmap_track);

	cmap_track_add(cmap_handle, "config.reload_in_progress",
			CMAP_TRACK_ADD | CMAP_TRACK_MODIFY,
			main_logging_notify,
			NULL,
			&cmap_track);
}
#endif

int corosync_log_config_read (
#ifndef LOGCONFIG_USE_ICMAP
	cmap_handle_t cmap_h,
	const char *default_logfile,
#endif
	const char **error_string)
{
	const char *error_reason = error_string_response;

#ifndef LOGCONFIG_USE_ICMAP
	if (!cmap_h) {
		error_reason = "No cmap handle";
		return (-1);
	}
	if (!default_logfile) {
		error_reason = "No default logfile";
		return (-1);
	}
	cmap_handle = cmap_h;
	main_logfile = default_logfile;
#endif

	if (corosync_main_config_read_logging(error_string) < 0) {
		error_reason = *error_string;
		goto parse_error;
	}

	add_logsys_config_notification();

	return 0;

parse_error:
	snprintf (error_string_response, sizeof(error_string_response),
		 "parse error in config: %s.\n",
		 error_reason);

	*error_string = error_string_response;
	return (-1);
}
