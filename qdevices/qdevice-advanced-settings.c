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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dynar.h"
#include "dynar-getopt-lex.h"
#include "dynar-str.h"
#include "qdevice-config.h"
#include "qnet-config.h"
#include "qdevice-advanced-settings.h"
#include "utils.h"

int
qdevice_advanced_settings_init(struct qdevice_advanced_settings *settings)
{

	memset(settings, 0, sizeof(*settings));
	if ((settings->lock_file = strdup(QDEVICE_DEFAULT_LOCK_FILE)) == NULL) {
		return (-1);
	}
	if ((settings->local_socket_file = strdup(QDEVICE_DEFAULT_LOCAL_SOCKET_FILE)) == NULL) {
		return (-1);
	}
	settings->local_socket_backlog = QDEVICE_DEFAULT_LOCAL_SOCKET_BACKLOG;
	settings->max_cs_try_again = QDEVICE_DEFAULT_MAX_CS_TRY_AGAIN;
	if ((settings->votequorum_device_name = strdup(QDEVICE_DEFAULT_VOTEQUORUM_DEVICE_NAME)) == NULL) {
		return (-1);
	}
	settings->ipc_max_clients = QDEVICE_DEFAULT_IPC_MAX_CLIENTS;
	settings->ipc_max_receive_size = QDEVICE_DEFAULT_IPC_MAX_RECEIVE_SIZE;
	settings->ipc_max_send_size = QDEVICE_DEFAULT_IPC_MAX_SEND_SIZE;

	settings->heuristics_ipc_max_send_buffers = QDEVICE_DEFAULT_HEURISTICS_IPC_MAX_SEND_BUFFERS;
	settings->heuristics_ipc_max_send_receive_size = QDEVICE_DEFAULT_HEURISTICS_IPC_MAX_SEND_RECEIVE_SIZE;

	settings->heuristics_min_timeout = QDEVICE_DEFAULT_HEURISTICS_MIN_TIMEOUT;
	settings->heuristics_max_timeout = QDEVICE_DEFAULT_HEURISTICS_MAX_TIMEOUT;
	settings->heuristics_min_interval = QDEVICE_DEFAULT_HEURISTICS_MIN_INTERVAL;
	settings->heuristics_max_interval = QDEVICE_DEFAULT_HEURISTICS_MAX_INTERVAL;

	settings->heuristics_max_execs = QDEVICE_DEFAULT_HEURISTICS_MAX_EXECS;

	settings->heuristics_use_execvp = QDEVICE_DEFAULT_HEURISTICS_USE_EXECVP;
	settings->heuristics_max_processes = QDEVICE_DEFAULT_HEURISTICS_MAX_PROCESSES;
	settings->heuristics_kill_list_interval = QDEVICE_DEFAULT_HEURISTICS_KILL_LIST_INTERVAL;

	if ((settings->net_nss_db_dir = strdup(QDEVICE_NET_DEFAULT_NSS_DB_DIR)) == NULL) {
		return (-1);
	}
	settings->net_initial_msg_receive_size = QDEVICE_NET_DEFAULT_INITIAL_MSG_RECEIVE_SIZE;
	settings->net_initial_msg_send_size = QDEVICE_NET_DEFAULT_INITIAL_MSG_SEND_SIZE;
	settings->net_min_msg_send_size = QDEVICE_NET_DEFAULT_MIN_MSG_SEND_SIZE;
	settings->net_max_msg_receive_size = QDEVICE_NET_DEFAULT_MAX_MSG_RECEIVE_SIZE;
	settings->net_max_send_buffers = QDEVICE_NET_DEFAULT_MAX_SEND_BUFFERS;
	if ((settings->net_nss_qnetd_cn = strdup(QDEVICE_NET_DEFAULT_NSS_QNETD_CN)) == NULL) {
		return (-1);
	}
	if ((settings->net_nss_client_cert_nickname =
	    strdup(QDEVICE_NET_DEFAULT_NSS_CLIENT_CERT_NICKNAME)) == NULL) {
		return (-1);
	}
	settings->net_heartbeat_interval_min = QDEVICE_NET_DEFAULT_HEARTBEAT_INTERVAL_MIN;
	settings->net_heartbeat_interval_max = QDEVICE_NET_DEFAULT_HEARTBEAT_INTERVAL_MAX;
	settings->net_min_connect_timeout = QDEVICE_NET_DEFAULT_MIN_CONNECT_TIMEOUT;
	settings->net_max_connect_timeout = QDEVICE_NET_DEFAULT_MAX_CONNECT_TIMEOUT;
	settings->net_test_algorithm_enabled = QDEVICE_NET_DEFAULT_TEST_ALGORITHM_ENABLED;

	settings->master_wins = QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_MODEL;

	return (0);
}

void
qdevice_advanced_settings_destroy(struct qdevice_advanced_settings *settings)
{

	free(settings->local_socket_file);
	free(settings->lock_file);
	free(settings->votequorum_device_name);
	free(settings->net_nss_db_dir);
	free(settings->net_nss_qnetd_cn);
	free(settings->net_nss_client_cert_nickname);
}

/*
 * 0 - No error
 * -1 - Unknown option
 * -2 - Incorrect value
 */
int
qdevice_advanced_settings_set(struct qdevice_advanced_settings *settings,
    const char *option, const char *value)
{
	long long int tmpll;
	char *ep;

	if (strcasecmp(option, "lock_file") == 0) {
		free(settings->lock_file);

		if ((settings->lock_file = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "local_socket_file") == 0) {
		free(settings->local_socket_file);

		if ((settings->local_socket_file = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "local_socket_backlog") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_LOCAL_SOCKET_BACKLOG || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->local_socket_backlog = (int)tmpll;
	} else if (strcasecmp(option, "max_cs_try_again") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_MAX_CS_TRY_AGAIN || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->max_cs_try_again = (int)tmpll;
	} else if (strcasecmp(option, "votequorum_device_name") == 0) {
		free(settings->votequorum_device_name);

		if ((settings->votequorum_device_name = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "ipc_max_clients") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_IPC_MAX_CLIENTS || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->ipc_max_clients = (size_t)tmpll;
	} else if (strcasecmp(option, "ipc_max_receive_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_IPC_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->ipc_max_receive_size = (size_t)tmpll;
	} else if (strcasecmp(option, "ipc_max_send_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_IPC_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->ipc_max_send_size = (size_t)tmpll;
	} else if (strcasecmp(option, "heuristics_ipc_max_send_buffers") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_IPC_MAX_SEND_BUFFERS || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_ipc_max_send_buffers = (size_t)tmpll;
	} else if (strcasecmp(option, "heuristics_ipc_max_send_receive_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_IPC_MAX_SEND_RECEIVE_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_ipc_max_send_receive_size = (size_t)tmpll;
	} else if (strcasecmp(option, "heuristics_min_timeout") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_TIMEOUT || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_min_timeout = (uint32_t)tmpll;
	} else if (strcasecmp(option, "heuristics_max_timeout") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_TIMEOUT || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_max_timeout = (uint32_t)tmpll;
	} else if (strcasecmp(option, "heuristics_min_interval") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_min_interval = (uint32_t)tmpll;
	} else if (strcasecmp(option, "heuristics_max_interval") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_max_interval = (uint32_t)tmpll;
	} else if (strcasecmp(option, "heuristics_max_execs") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_MAX_EXECS || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_max_execs = (size_t)tmpll;
	} else if (strcasecmp(option, "heuristics_use_execvp") == 0) {
		if ((tmpll = utils_parse_bool_str(value)) == -1) {
			return (-2);
		}

		settings->heuristics_use_execvp = (uint8_t)tmpll;
	} else if (strcasecmp(option, "heuristics_max_processes") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_MAX_PROCESSES || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_max_processes = (size_t)tmpll;
	} else if (strcasecmp(option, "heuristics_kill_list_interval") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_MIN_HEURISTICS_KILL_LIST_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heuristics_kill_list_interval = (uint32_t)tmpll;
	} else if (strcasecmp(option, "net_nss_db_dir") == 0) {
		free(settings->net_nss_db_dir);

		if ((settings->net_nss_db_dir = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "net_initial_msg_receive_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_MSG_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_initial_msg_receive_size = (size_t)tmpll;
	} else if (strcasecmp(option, "net_initial_msg_send_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_MSG_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_initial_msg_send_size = (size_t)tmpll;
	} else if (strcasecmp(option, "net_min_msg_send_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_MSG_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_min_msg_send_size = (size_t)tmpll;
	} else if (strcasecmp(option, "net_max_msg_receive_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_MSG_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_max_msg_receive_size = (size_t)tmpll;
	} else if (strcasecmp(option, "net_max_send_buffers") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_MAX_SEND_BUFFERS || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_max_send_buffers = (size_t)tmpll;
	} else if (strcasecmp(option, "net_nss_qnetd_cn") == 0) {
		free(settings->net_nss_qnetd_cn);

		if ((settings->net_nss_qnetd_cn = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "net_nss_client_cert_nickname") == 0) {
		free(settings->net_nss_client_cert_nickname);

		if ((settings->net_nss_client_cert_nickname = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "net_heartbeat_interval_min") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_HEARTBEAT_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_heartbeat_interval_min = (uint32_t)tmpll;
	} else if (strcasecmp(option, "net_heartbeat_interval_max") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_HEARTBEAT_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_heartbeat_interval_max = (uint32_t)tmpll;
	} else if (strcasecmp(option, "net_min_connect_timeout") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_CONNECT_TIMEOUT || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_min_connect_timeout = (uint32_t)tmpll;
	} else if (strcasecmp(option, "net_max_connect_timeout") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QDEVICE_NET_MIN_CONNECT_TIMEOUT || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->net_max_connect_timeout = (uint32_t)tmpll;
	} else if (strcasecmp(option, "net_test_algorithm_enabled") == 0) {
		if ((tmpll = utils_parse_bool_str(value)) == -1) {
			return (-2);
		}

		settings->net_test_algorithm_enabled = (uint8_t)tmpll;
	} else if (strcasecmp(option, "master_wins") == 0) {
		tmpll = utils_parse_bool_str(value);

		if (tmpll == 0) {
			settings->master_wins = QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_FORCE_OFF;
		} else if (tmpll == 1) {
			settings->master_wins = QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_FORCE_ON;
		} else if (strcasecmp(value, "model") == 0) {
			settings->master_wins = QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_MODEL;
		} else {
			return (-2);
		}
	} else {
		return (-1);
	}

	return (0);
}
