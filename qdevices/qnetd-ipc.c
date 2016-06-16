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

#include "qnet-config.h"
#include "qnetd-ipc.h"
#include "qnetd-ipc-cmd.h"
#include "qnetd-log.h"
#include "unix-socket-ipc.h"
#include "dynar-simple-lex.h"
#include "dynar-str.h"

/*
 * Needed for creating nspr handle from unix fd
 */
#include <private/pprio.h>

int
qnetd_ipc_init(struct qnetd_instance *instance)
{

	if (unix_socket_ipc_init(&instance->local_ipc,
	    instance->advanced_settings->local_socket_file,
	    instance->advanced_settings->local_socket_backlog,
	    instance->advanced_settings->ipc_max_clients,
	    instance->advanced_settings->ipc_max_receive_size,
	    instance->advanced_settings->ipc_max_send_size) != 0) {
		qnetd_log_err(LOG_ERR, "Can't create unix socket");

		return (-1);
	}

	if ((instance->ipc_socket_poll_fd = PR_CreateSocketPollFd(instance->local_ipc.socket)) == NULL) {
		qnetd_log_nss(LOG_CRIT, "Can't create NSPR IPC socket poll fd");

		return (-1);
	}

	return (0);
}

int
qnetd_ipc_close(struct qnetd_instance *instance)
{
	int res;

	res = unix_socket_ipc_close(&instance->local_ipc);
	if (res != 0) {
		qnetd_log_err(LOG_WARNING, "Can't close local IPC");
	}

	return (res);
}

int
qnetd_ipc_is_closed(struct qnetd_instance *instance)
{

	return (unix_socket_ipc_is_closed(&instance->local_ipc));
}

int
qnetd_ipc_destroy(struct qnetd_instance *instance)
{
	int res;
	struct unix_socket_client *client;
	const struct unix_socket_client_list *ipc_client_list;

	ipc_client_list = &instance->local_ipc.clients;

	TAILQ_FOREACH(client, ipc_client_list, entries) {
		free(client->user_data);
	}

	if (PR_DestroySocketPollFd(instance->ipc_socket_poll_fd) != PR_SUCCESS) {
		qnetd_log_nss(LOG_WARNING, "Unable to destroy IPC poll socket fd");
	}

	res = unix_socket_ipc_destroy(&instance->local_ipc);
	if (res != 0) {
		qnetd_log_err(LOG_WARNING, "Can't destroy local IPC");
	}

	return (res);
}

int
qnetd_ipc_accept(struct qnetd_instance *instance, struct unix_socket_client **res_client)
{
	int res;
	int accept_res;
	PRFileDesc *prfd;

	accept_res = unix_socket_ipc_accept(&instance->local_ipc, res_client);

	switch (accept_res) {
	case -1:
		qnetd_log_err(LOG_ERR, "Can't accept local IPC connection");
		res = -1;
		goto return_res;
		break;
	case -2:
		qnetd_log(LOG_ERR, "Maximum IPC clients reached. Not accepting connection");
		res = -1;
		goto return_res;
		break;
	case -3:
		qnetd_log(LOG_ERR, "Can't add client to list");
		res = -1;
		goto return_res;
		break;
	default:
		unix_socket_client_read_line(*res_client, 1);
		res = 0;
		break;
	}

	(*res_client)->user_data = malloc(sizeof(struct qnetd_ipc_user_data));
	if ((*res_client)->user_data == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc IPC client user data");
		res = -1;
		qnetd_ipc_client_disconnect(instance, *res_client);

		goto return_res;
	}
	memset((*res_client)->user_data, 0, sizeof(struct qnetd_ipc_user_data));

	prfd = PR_CreateSocketPollFd((*res_client)->socket);
	if (prfd == NULL) {
		qnetd_log_nss(LOG_CRIT, "Can't create NSPR poll fd for IPC client. Disconnecting client");
		qnetd_ipc_client_disconnect(instance, *res_client);
		res = -1;

		goto return_res;
	}

	((struct qnetd_ipc_user_data *)(*res_client)->user_data)->nspr_poll_fd = prfd;

return_res:
	return (res);
}

void
qnetd_ipc_client_disconnect(struct qnetd_instance *instance, struct unix_socket_client *client)
{

	if (PR_DestroySocketPollFd(
	    ((struct qnetd_ipc_user_data *)(client)->user_data)->nspr_poll_fd) != PR_SUCCESS) {
		qnetd_log_nss(LOG_WARNING, "Unable to destroy client IPC poll socket fd");
	}

	free(client->user_data);
	unix_socket_ipc_client_disconnect(&instance->local_ipc, client);
}

int
qnetd_ipc_send_error(struct qnetd_instance *instance, struct unix_socket_client *client,
    const char *error_fmt, ...)
{
	va_list ap;
	int res;

	va_start(ap, error_fmt);
	res = ((dynar_str_cpy(&client->send_buffer, "Error\n") == 0) &&
	    (dynar_str_vcatf(&client->send_buffer, error_fmt, ap) > 0) &&
	    (dynar_str_cat(&client->send_buffer, "\n") == 0));

	va_end(ap);

	if (res) {
		unix_socket_client_write_buffer(client, 1);
	} else {
		qnetd_log(LOG_ERR, "Can't send ipc error to client (buffer too small)");
	}

	return (res ? 0 : -1);
}

int
qnetd_ipc_send_buffer(struct qnetd_instance *instance, struct unix_socket_client *client)
{

	if (dynar_str_prepend(&client->send_buffer, "OK\n") != 0) {
		qnetd_log(LOG_ERR, "Can't send ipc message to client (buffer too small)");

		if (qnetd_ipc_send_error(instance, client, "Internal IPC buffer too small") != 0) {
			return (-1);
		}

		return (0);
	}

	unix_socket_client_write_buffer(client, 1);

	return (0);
}

static void
qnetd_ipc_parse_line(struct qnetd_instance *instance, struct unix_socket_client *client)
{
	struct dynar_simple_lex lex;
	struct dynar *token;
	char *str;
	struct qnetd_ipc_user_data *ipc_user_data;
	int verbose;
	char *cluster_name;

	ipc_user_data = (struct qnetd_ipc_user_data *)client->user_data;

	dynar_simple_lex_init(&lex, &client->receive_buffer, DYNAR_SIMPLE_LEX_TYPE_QUOTE);
	token = dynar_simple_lex_token_next(&lex);

	verbose = 0;
	cluster_name = NULL;

	if (token == NULL) {
		goto exit_err_low_mem;
	}

	str = dynar_data(token);
	if (strcasecmp(str, "") == 0) {
		qnetd_log(LOG_DEBUG, "IPC client doesn't send command");
		if (qnetd_ipc_send_error(instance, client, "No command specified") != 0) {
			client->schedule_disconnect = 1;
		}
	} else if (strcasecmp(str, "shutdown") == 0) {
		qnetd_log(LOG_DEBUG, "IPC client requested shutdown");

		ipc_user_data->shutdown_requested = 1;

		if (qnetd_ipc_send_buffer(instance, client) != 0) {
			client->schedule_disconnect = 1;
		}
	} else if (strcasecmp(str, "status") == 0) {
		token = dynar_simple_lex_token_next(&lex);
		if (token == NULL) {
			goto exit_err_low_mem;
		}

		str = dynar_data(token);

		if (token != NULL && strcmp(str, "") != 0) {
			if (strcasecmp(str, "verbose") == 0) {
				verbose = 1;
			}
		}

		if (qnetd_ipc_cmd_status(instance, &client->send_buffer, verbose) != 0) {
			if (qnetd_ipc_send_error(instance, client, "Can't get QNetd status") != 0) {
				client->schedule_disconnect = 1;
			}
		} else {
			if (qnetd_ipc_send_buffer(instance, client) != 0) {
				client->schedule_disconnect = 1;
			}
		}
	} else if (strcasecmp(str, "list") == 0) {
		while (((token = dynar_simple_lex_token_next(&lex)) != NULL) &&
		    (str = dynar_data(token), strcmp(str, "") != 0)) {
			if (strcasecmp(str, "verbose") == 0) {
				verbose = 1;
			} else if (strcasecmp(str, "cluster") == 0) {
				token = dynar_simple_lex_token_next(&lex);
				if (token == NULL) {
					goto exit_err_low_mem;
				}

				free(cluster_name); cluster_name = NULL;
				if ((cluster_name = strdup(dynar_data(token))) == NULL) {
					goto exit_err_low_mem;
				}
			} else {
				break;
			}
		}

		if (qnetd_ipc_cmd_list(instance, &client->send_buffer, verbose, cluster_name) != 0) {
			if (qnetd_ipc_send_error(instance, client, "Can't get QNetd cluster list") != 0) {
				client->schedule_disconnect = 1;
			}
		} else {
			if (qnetd_ipc_send_buffer(instance, client) != 0) {
				client->schedule_disconnect = 1;
			}
		}

		free(cluster_name); cluster_name = NULL;
	} else {
		qnetd_log(LOG_DEBUG, "IPC client sent unknown command");
		if (qnetd_ipc_send_error(instance, client, "Unknown command '%s'", str) != 0) {
			client->schedule_disconnect = 1;
		}
	}

	dynar_simple_lex_destroy(&lex);

	return ;

exit_err_low_mem:
	free(cluster_name); cluster_name = NULL;

	qnetd_log(LOG_ERR, "Can't alloc memory for simple lex");

	if (qnetd_ipc_send_error(instance, client, "Command too long") != 0) {
		client->schedule_disconnect = 1;
	}
}

void
qnetd_ipc_io_read(struct qnetd_instance *instance, struct unix_socket_client *client)
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
		qnetd_log(LOG_DEBUG, "IPC client closed connection");
		client->schedule_disconnect = 1;
		break;
	case -2:
		qnetd_log(LOG_ERR, "Can't store message from IPC client. Disconnecting client.");
		client->schedule_disconnect = 1;
		break;
	case -3:
		qnetd_log_err(LOG_ERR, "Can't receive message from IPC client. Disconnecting client.");
		client->schedule_disconnect = 1;
		break;
	case 1:
		/*
		 * Full message received
		 */
		unix_socket_client_read_line(client, 0);

		qnetd_ipc_parse_line(instance, client);
		break;
	}
}

void
qnetd_ipc_io_write(struct qnetd_instance *instance, struct unix_socket_client *client)
{
	int res;
	struct qnetd_ipc_user_data *ipc_user_data;

	ipc_user_data = (struct qnetd_ipc_user_data *)client->user_data;

	res = unix_socket_client_io_write(client);

	switch (res) {
	case 0:
		/*
		 * Partial send
		 */
		break;
	case -1:
		qnetd_log(LOG_DEBUG, "IPC client closed connection");
		client->schedule_disconnect = 1;
		break;
	case -2:
		qnetd_log_err(LOG_ERR, "Can't send message to IPC client. Disconnecting client");
		client->schedule_disconnect = 1;
		break;
	case 1:
		/*
		 * Full message sent
		 */
		unix_socket_client_write_buffer(client, 0);
		client->schedule_disconnect = 1;

		if (ipc_user_data->shutdown_requested) {
			qnetd_ipc_close(instance);
		}

		break;
	}
}
