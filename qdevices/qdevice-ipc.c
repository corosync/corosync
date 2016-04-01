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

#include "qdevice-config.h"
#include "qdevice-ipc.h"
#include "qdevice-log.h"
#include "unix-socket-ipc.h"
#include "dynar-simple-lex.h"

int
qdevice_ipc_init(struct qdevice_instance *instance)
{
	if (unix_socket_ipc_init(&instance->local_ipc, QDEVICE_LOCAL_SOCKET_FILE,
	    QDEVICE_LOCAL_SOCKET_BACKLOG, QDEVICE_IPC_MAX_CLIENTS, QDEVICE_IPC_MAX_RECEIVE_SIZE,
	    QDEVICE_IPC_MAX_SEND_SIZE) != 0) {
		qdevice_log_err(LOG_ERR, "Can't create unix socket");

		return (-1);
	}

	return (0);
}

int
qdevice_ipc_destroy(struct qdevice_instance *instance)
{
	int res;

	res = unix_socket_ipc_destroy(&instance->local_ipc);
	if (res != 0) {
		qdevice_log_err(LOG_WARNING, "Can't destroy local IPC");
	}

	return (res);
}

int
qdevice_ipc_accept(struct qdevice_instance *instance, struct unix_socket_client **res_client)
{
	int res;
	int accept_res;

	accept_res = unix_socket_ipc_accept(&instance->local_ipc, res_client);

	switch (accept_res) {
	case -1:
		qdevice_log_err(LOG_ERR, "Can't accept local IPC connection");
		res = -1;
		break;
	case -2:
		qdevice_log(LOG_ERR, "Maximum IPC clients reached. Not accepting connection");
		res = -1;
		break;
	case -3:
		qdevice_log(LOG_ERR, "Can't add client to list");
		res = -1;
		break;
	default:
		unix_socket_client_read_line(*res_client, 1);
		res = 0;
		break;
	}


	return (res);
}

void
qdevice_ipc_client_disconnect(struct qdevice_instance *instance, struct unix_socket_client *client)
{

	unix_socket_ipc_client_disconnect(&instance->local_ipc, client);
}

static void
qdevice_ipc_parse_line(struct qdevice_instance *instance, struct unix_socket_client *client)
{
	struct dynar_simple_lex lex;
	struct dynar *token;
	char *str;

	dynar_simple_lex_init(&lex, &client->receive_buffer);
	token = dynar_simple_lex_token_next(&lex);

	if (token == NULL) {
		qdevice_log(LOG_ERR, "Can't alloc memory for simple lex");

		return;
	}

	str = dynar_data(token);
	if (strcasecmp(str, "") == 0) {
		qdevice_log(LOG_DEBUG, "IPC client error: No command specified");
		// SEND ERROR
	} else if (strcasecmp(str, "shutdown") == 0) {
		qdevice_log(LOG_DEBUG, "IPC client requested shutdown");
		// Send output?
	} else if (strcasecmp(str, "status") == 0) {
		qdevice_log(LOG_DEBUG, "IPC client requested status display");
		// Send output
	} else {
		qdevice_log(LOG_DEBUG, "IPC client sent unknown command");
		// Send output
	}

	dynar_simple_lex_destroy(&lex);
}

void
qdevice_ipc_io_read(struct qdevice_instance *instance, struct unix_socket_client *client)
{
	int res;

	res = unix_socket_client_io_read(client);

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		qdevice_log(LOG_DEBUG, "IPC client closed connection");
		client->schedule_disconnect = 1;
		break;
	case -2:
		qdevice_log(LOG_ERR, "Can't store message from IPC client. Disconnecting client");
		client->schedule_disconnect = 1;
		break;
	case 1:
		/*
		 * Full message received
		 */
		qdevice_ipc_parse_line(instance, client);
		break;
	}
}
