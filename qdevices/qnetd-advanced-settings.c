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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dynar.h"
#include "dynar-getopt-lex.h"
#include "dynar-str.h"
#include "qnet-config.h"
#include "qnetd-advanced-settings.h"
#include "utils.h"

int
qnetd_advanced_settings_init(struct qnetd_advanced_settings *settings)
{

	memset(settings, 0, sizeof(*settings));
	settings->listen_backlog = QNETD_DEFAULT_LISTEN_BACKLOG;
	settings->max_client_send_buffers = QNETD_DEFAULT_MAX_CLIENT_SEND_BUFFERS;
	settings->max_client_send_size = QNETD_DEFAULT_MAX_CLIENT_SEND_SIZE;
	settings->max_client_receive_size = QNETD_DEFAULT_MAX_CLIENT_RECEIVE_SIZE;
	if ((settings->nss_db_dir = strdup(QNETD_DEFAULT_NSS_DB_DIR)) == NULL) {
		return (-1);
	}
	if ((settings->cert_nickname = strdup(QNETD_DEFAULT_CERT_NICKNAME)) == NULL) {
		return (-1);
	}
	settings->heartbeat_interval_min = QNETD_DEFAULT_HEARTBEAT_INTERVAL_MIN;
	settings->heartbeat_interval_max = QNETD_DEFAULT_HEARTBEAT_INTERVAL_MAX;
	settings->dpd_enabled = QNETD_DEFAULT_DPD_ENABLED;
	settings->dpd_interval = QNETD_DEFAULT_DPD_INTERVAL;
	if ((settings->lock_file = strdup(QNETD_DEFAULT_LOCK_FILE)) == NULL) {
		return (-1);
	}
	if ((settings->local_socket_file = strdup(QNETD_DEFAULT_LOCAL_SOCKET_FILE)) == NULL) {
		return (-1);
	}
	settings->local_socket_backlog = QNETD_DEFAULT_LOCAL_SOCKET_BACKLOG;
	settings->ipc_max_clients = QNETD_DEFAULT_IPC_MAX_CLIENTS;
	settings->ipc_max_receive_size = QNETD_DEFAULT_IPC_MAX_RECEIVE_SIZE;
	settings->ipc_max_send_size = QNETD_DEFAULT_IPC_MAX_SEND_SIZE;

	return (0);
}

void
qnetd_advanced_settings_destroy(struct qnetd_advanced_settings *settings)
{

	free(settings->nss_db_dir);
	free(settings->cert_nickname);
	free(settings->lock_file);
	free(settings->local_socket_file);
}

/*
 * 0 - No error
 * -1 - Unknown option
 * -2 - Incorrect value
 */
int
qnetd_advanced_settings_set(struct qnetd_advanced_settings *settings,
    const char *option, const char *value)
{
	long long int tmpll;
	char *ep;

	if (strcasecmp(option, "listen_backlog") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_LISTEN_BACKLOG || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->listen_backlog = (int)tmpll;
	} else if (strcasecmp(option, "max_client_send_buffers") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_CLIENT_SEND_BUFFERS || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->max_client_send_buffers = (size_t)tmpll;
	} else if (strcasecmp(option, "max_client_send_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_CLIENT_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->max_client_send_size = (size_t)tmpll;
	} else if (strcasecmp(option, "max_client_receive_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_CLIENT_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->max_client_receive_size = (size_t)tmpll;
	} else if (strcasecmp(option, "nss_db_dir") == 0) {
		free(settings->nss_db_dir);

		if ((settings->nss_db_dir = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "cert_nickname") == 0) {
		free(settings->cert_nickname);

		if ((settings->cert_nickname = strdup(value)) == NULL) {
			return (-1);
		}
	} else if (strcasecmp(option, "heartbeat_interval_min") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_HEARTBEAT_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heartbeat_interval_min = (uint32_t)tmpll;
	} else if (strcasecmp(option, "heartbeat_interval_max") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_HEARTBEAT_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->heartbeat_interval_max = (uint32_t)tmpll;
	} else if (strcasecmp(option, "dpd_enabled") == 0) {
		if ((tmpll = utils_parse_bool_str(value)) == -1) {
			return (-2);
		}

		settings->dpd_enabled = (uint8_t)tmpll;
	} else if (strcasecmp(option, "dpd_interval") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_DPD_INTERVAL || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->dpd_interval = (uint32_t)tmpll;
	} else if (strcasecmp(option, "lock_file") == 0) {
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
		if (tmpll < QNETD_MIN_LOCAL_SOCKET_BACKLOG || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->local_socket_backlog = (int)tmpll;
	} else if (strcasecmp(option, "ipc_max_clients") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_IPC_MAX_CLIENTS || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->ipc_max_clients = (size_t)tmpll;
	} else if (strcasecmp(option, "ipc_max_receive_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_IPC_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->ipc_max_receive_size = (size_t)tmpll;
	} else if (strcasecmp(option, "ipc_max_send_size") == 0) {
		tmpll = strtoll(value, &ep, 10);
		if (tmpll < QNETD_MIN_IPC_RECEIVE_SEND_SIZE || errno != 0 || *ep != '\0') {
			return (-2);
		}

		settings->ipc_max_send_size = (size_t)tmpll;
	} else {
		return (-1);
	}

	return (0);
}
