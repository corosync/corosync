/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
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

#include "qdevice-config.h"
#include "qdevice-instance.h"
#include "qdevice-heuristics-exec-list.h"
#include "qdevice-log.h"
#include "qdevice-model.h"
#include "utils.h"

int
qdevice_instance_init(struct qdevice_instance *instance,
    const struct qdevice_advanced_settings *advanced_settings)
{

	memset(instance, 0, sizeof(*instance));

	node_list_init(&instance->config_node_list);

	instance->vq_last_poll = ((time_t) -1);
	instance->advanced_settings = advanced_settings;

	return (0);
}

int
qdevice_instance_destroy(struct qdevice_instance *instance)
{

	node_list_free(&instance->config_node_list);

	return (0);
}

int
qdevice_instance_configure_from_cmap_heuristics(struct qdevice_instance *instance)
{
	char *str;
	long int li;
	char *ep;
	int i;
	int res;
	cs_error_t cs_err;
	cmap_iter_handle_t iter_handle;
	char key_name[CMAP_KEYNAME_MAXLEN + 1];
	size_t value_len;
	cmap_value_types_t type;
	struct qdevice_heuristics_exec_list tmp_exec_list;
	struct qdevice_heuristics_exec_list *exec_list;
	char *command;
	char exec_name[CMAP_KEYNAME_MAXLEN + 1];
	char tmp_key[CMAP_KEYNAME_MAXLEN + 1];
	size_t no_execs;
	int send_exec_list;

	instance->heuristics_instance.timeout = instance->heartbeat_interval / 2;
	if (cmap_get_string(instance->cmap_handle,
	    "quorum.device.heuristics.timeout", &str) == CS_OK) {
		li = strtol(str, &ep, 10);
		if (li < instance->advanced_settings->heuristics_min_timeout ||
		    li > instance->advanced_settings->heuristics_max_timeout || *ep != '\0') {
			qdevice_log(LOG_ERR, "heuristics.timeout must be valid number in "
			    "range <%"PRIu32",%"PRIu32">",
			    instance->advanced_settings->heuristics_min_timeout,
			    instance->advanced_settings->heuristics_max_timeout);

			free(str);
			return (-1);
		} else {
			instance->heuristics_instance.timeout = li;
		}

		free(str);
	}

	instance->heuristics_instance.sync_timeout = instance->sync_heartbeat_interval / 2;
	if (cmap_get_string(instance->cmap_handle,
	    "quorum.device.heuristics.sync_timeout", &str) == CS_OK) {
		li = strtol(str, &ep, 10);
		if (li < instance->advanced_settings->heuristics_min_timeout ||
		    li > instance->advanced_settings->heuristics_max_timeout || *ep != '\0') {
			qdevice_log(LOG_ERR, "heuristics.sync_timeout must be valid number in "
			    "range <%"PRIu32",%"PRIu32">",
			    instance->advanced_settings->heuristics_min_timeout,
			    instance->advanced_settings->heuristics_max_timeout);

			free(str);
			return (-1);
		} else {
			instance->heuristics_instance.sync_timeout = li;
		}

		free(str);
	}

	instance->heuristics_instance.interval = instance->heartbeat_interval * 3;
	if (cmap_get_string(instance->cmap_handle,
	    "quorum.device.heuristics.interval", &str) == CS_OK) {
		li = strtol(str, &ep, 10);
		if (li < instance->advanced_settings->heuristics_min_interval ||
		    li > instance->advanced_settings->heuristics_max_interval || *ep != '\0') {
			qdevice_log(LOG_ERR, "heuristics.interval must be valid number in "
			    "range <%"PRIu32",%"PRIu32">",
			    instance->advanced_settings->heuristics_min_interval,
			    instance->advanced_settings->heuristics_max_interval);

			free(str);
			return (-1);
		} else {
			instance->heuristics_instance.interval = li;
		}

		free(str);
	}

	instance->heuristics_instance.mode = QDEVICE_DEFAULT_HEURISTICS_MODE;

	if (cmap_get_string(instance->cmap_handle, "quorum.device.heuristics.mode", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			if (strcasecmp(str, "sync") != 0) {
				qdevice_log(LOG_ERR, "quorum.device.heuristics.mode value is not valid.");

				free(str);
				return (-1);
			} else {
				instance->heuristics_instance.mode = QDEVICE_HEURISTICS_MODE_SYNC;
			}
		} else {
			if (i == 1) {
				instance->heuristics_instance.mode = QDEVICE_HEURISTICS_MODE_ENABLED;
			} else {
				instance->heuristics_instance.mode = QDEVICE_HEURISTICS_MODE_DISABLED;
			}
		}

		free(str);
	}

	send_exec_list = 0;
	exec_list = NULL;
	qdevice_heuristics_exec_list_init(&tmp_exec_list);

	if (instance->heuristics_instance.mode == QDEVICE_HEURISTICS_MODE_DISABLED) {
		exec_list = NULL;
		send_exec_list = 1;
	} else if (instance->heuristics_instance.mode == QDEVICE_HEURISTICS_MODE_ENABLED ||
	    instance->heuristics_instance.mode == QDEVICE_HEURISTICS_MODE_SYNC) {
		/*
		 * Walk thru list of commands to exec
		 */
		cs_err = cmap_iter_init(instance->cmap_handle, "quorum.device.heuristics.exec_", &iter_handle);
		if (cs_err != CS_OK) {
			qdevice_log(LOG_ERR, "Can't iterate quorum.device.heuristics.exec_ keys. "
			    "Error %s", cs_strerror(cs_err));

			return (-1);
		}

		while ((cs_err = cmap_iter_next(instance->cmap_handle, iter_handle, key_name,
		    &value_len, &type)) == CS_OK) {
			if (type != CMAP_VALUETYPE_STRING) {
				qdevice_log(LOG_WARNING, "%s key is not of string type. Ignoring");
				continue ;
			}

			res = sscanf(key_name, "quorum.device.heuristics.exec_%[^.]%s", exec_name, tmp_key);
			if (res != 1) {
				qdevice_log(LOG_WARNING, "%s key is not correct heuristics exec name. Ignoring");
				continue ;
			}

			cs_err = cmap_get_string(instance->cmap_handle, key_name, &command);
			if (cs_err != CS_OK) {
				qdevice_log(LOG_WARNING, "Can't get value of %s key. Ignoring");
				continue ;
			}

			if (qdevice_heuristics_exec_list_add(&tmp_exec_list, exec_name, command) == NULL) {
				qdevice_log(LOG_WARNING, "Can't store value of %s key into list. Ignoring");
			}

			free(command);
		}

		no_execs = qdevice_heuristics_exec_list_size(&tmp_exec_list);

		if (no_execs == 0) {
			qdevice_log(LOG_INFO, "No valid heuristics execs defined. Disabling heuristics.");
			instance->heuristics_instance.mode = QDEVICE_HEURISTICS_MODE_DISABLED;
			exec_list = NULL;
			send_exec_list = 1;
		} else if (no_execs > instance->advanced_settings->heuristics_max_execs) {
			qdevice_log(LOG_ERR, "Too much (%zu) heuristics execs defined (max is %zu)."
			    " Disabling heuristics.", no_execs,
			    instance->advanced_settings->heuristics_max_execs);
			instance->heuristics_instance.mode = QDEVICE_HEURISTICS_MODE_DISABLED;
			exec_list = NULL;
			send_exec_list = 1;
		} else if (qdevice_heuristics_exec_list_eq(&tmp_exec_list,
		    &instance->heuristics_instance.exec_list) == 1) {
			qdevice_log(LOG_DEBUG, "Heuristics list is unchanged");
			send_exec_list = 0;
		} else {
			qdevice_log(LOG_DEBUG, "Heuristics list changed");
			exec_list = &tmp_exec_list;
			send_exec_list = 1;
		}

	} else {
		qdevice_log(LOG_CRIT, "Undefined heuristics mode");
		exit(1);
	}

	if (send_exec_list) {
		if (qdevice_heuristics_change_exec_list(&instance->heuristics_instance,
		    exec_list, instance->sync_in_progress) != 0) {
			return (-1);
		}
	}

	qdevice_heuristics_exec_list_free(&tmp_exec_list);

	return (0);
}

int
qdevice_instance_configure_from_cmap(struct qdevice_instance *instance)
{
	char *str;

	if (cmap_get_string(instance->cmap_handle, "quorum.device.model", &str) != CS_OK) {
		qdevice_log(LOG_ERR, "Can't read quorum.device.model cmap key.");

		return (-1);
	}

	if (qdevice_model_str_to_type(str, &instance->model_type) != 0) {
		qdevice_log(LOG_ERR, "Configured device model %s is not supported.", str);
		free(str);

		return (-1);
	}
	free(str);

	if (cmap_get_uint32(instance->cmap_handle, "runtime.votequorum.this_node_id",
	    &instance->node_id) != CS_OK) {
		qdevice_log(LOG_ERR, "Unable to retrive this node nodeid.");

		return (-1);
	}

	if (cmap_get_uint32(instance->cmap_handle, "quorum.device.timeout", &instance->heartbeat_interval) != CS_OK) {
		instance->heartbeat_interval = VOTEQUORUM_QDEVICE_DEFAULT_TIMEOUT;
	}

	if (cmap_get_uint32(instance->cmap_handle, "quorum.device.sync_timeout",
	    &instance->sync_heartbeat_interval) != CS_OK) {
		instance->sync_heartbeat_interval = VOTEQUORUM_QDEVICE_DEFAULT_SYNC_TIMEOUT;
	}

	if (qdevice_instance_configure_from_cmap_heuristics(instance) != 0) {
		return (-1);
	}

	return (0);
}
