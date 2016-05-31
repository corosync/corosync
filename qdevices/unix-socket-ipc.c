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

#include "unix-socket.h"
#include "unix-socket-ipc.h"

int
unix_socket_ipc_init(struct unix_socket_ipc *ipc, const char *socket_file_name, int backlog,
    size_t max_clients, size_t max_receive_size, size_t max_send_size)
{

	memset(ipc, 0, sizeof(*ipc));

	ipc->socket_file_name = strdup(socket_file_name);
	if (ipc->socket_file_name == NULL) {
		return (-1);
	}

	unix_socket_client_list_init(&ipc->clients);

	ipc->backlog = backlog;
	ipc->socket = unix_socket_server_create(ipc->socket_file_name, 1,
		backlog);
	if (ipc->socket < 0) {
		free(ipc->socket_file_name);
		return (-1);
	}

	ipc->max_clients = max_clients;
	ipc->max_receive_size = max_receive_size;
	ipc->max_send_size = max_send_size;

	return (0);
}

int
unix_socket_ipc_close(struct unix_socket_ipc *ipc)
{
	int res;

	res = 0;

	if (ipc->socket < 0) {
		return (0);
	}

	if (unix_socket_server_destroy(ipc->socket, ipc->socket_file_name) != 0) {
		res = -1;
	}

	free(ipc->socket_file_name);
	ipc->socket_file_name = NULL;

	ipc->socket = -1;

	return (res);
}

int
unix_socket_ipc_is_closed(struct unix_socket_ipc *ipc)
{

	return (ipc->socket < 0);
}

int
unix_socket_ipc_destroy(struct unix_socket_ipc *ipc)
{

	if (unix_socket_ipc_close(ipc) != 0) {
		return (-1);
	}

	unix_socket_client_list_free(&ipc->clients);

	return (0);
}

/*
 *  0 = No error
 * -1 = Can't accept connection (errno set)
 * -2 = Too much clients
 * -3 = Can't add client to list
 */
int
unix_socket_ipc_accept(struct unix_socket_ipc *ipc, struct unix_socket_client **res_client)
{
	int client_sock;
	struct unix_socket_client *client;

	if ((client_sock = unix_socket_server_accept(ipc->socket, 1)) < 0) {
		return (-1);
	}

	if (ipc->max_clients != 0 &&
	    unix_socket_client_list_no_clients(&ipc->clients) >= ipc->max_clients) {
		unix_socket_close(client_sock);

		return (-2);
	}

	client = unix_socket_client_list_add(&ipc->clients, client_sock, ipc->max_receive_size,
	    ipc->max_send_size, NULL);
	if (client == NULL) {
		unix_socket_close(client_sock);

		return (-3);
	}

	*res_client = client;

	return (0);
}

void
unix_socket_ipc_client_disconnect(struct unix_socket_ipc *ipc, struct unix_socket_client *client)
{

	unix_socket_close(client->socket);
	unix_socket_client_list_del(&ipc->clients, client);
}
