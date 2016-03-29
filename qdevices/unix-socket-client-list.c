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

#include "unix-socket-client-list.h"

void
unix_socket_client_list_init(struct unix_socket_client_list *client_list)
{

	TAILQ_INIT(client_list);
}

struct unix_socket_client *
unix_socket_client_list_add(struct unix_socket_client_list *client_list,
    int sock, size_t max_receive_size, size_t max_send_size, void *user_data)
{
	struct unix_socket_client *client;

	client = (struct unix_socket_client *)malloc(sizeof(*client));
	if (client == NULL) {
		return (NULL);
	}

	unix_socket_client_init(client, sock, max_receive_size, max_send_size, user_data);

	TAILQ_INSERT_TAIL(client_list, client, entries);

	return (client);
}

void
unix_socket_client_list_free(struct unix_socket_client_list *client_list)
{
	struct unix_socket_client *client;
	struct unix_socket_client *client_next;

	client = TAILQ_FIRST(client_list);

	while (client != NULL) {
		client_next = TAILQ_NEXT(client, entries);

		unix_socket_client_destroy(client);
		free(client);

		client = client_next;
	}

	TAILQ_INIT(client_list);
}

void
unix_socket_client_list_del(struct unix_socket_client_list *client_list,
    struct unix_socket_client *client)
{

	TAILQ_REMOVE(client_list, client, entries);
	unix_socket_client_destroy(client);
	free(client);
}

size_t
unix_socket_client_list_no_clients(struct unix_socket_client_list *client_list)
{
	size_t res;
	struct unix_socket_client *client;

	res = 0;

	TAILQ_FOREACH(client, client_list, entries) {
		res++;
	}

	return (res);
}
