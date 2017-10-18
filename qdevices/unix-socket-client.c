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

#include <errno.h>
#include <string.h>

#include "unix-socket-client.h"
#include "unix-socket.h"

#define UNIX_SOCKET_CLIENT_BUFFER	256

void
unix_socket_client_init(struct unix_socket_client *client, int sock, size_t max_receive_size,
    size_t max_send_size, void *user_data)
{

	memset(client, 0, sizeof(*client));
	client->socket = sock;
	client->user_data = user_data;
	dynar_init(&client->receive_buffer, max_receive_size);
	dynar_init(&client->send_buffer, max_send_size);
}

void
unix_socket_client_destroy(struct unix_socket_client *client)
{

	dynar_destroy(&client->send_buffer);
	dynar_destroy(&client->receive_buffer);
}

void
unix_socket_client_read_line(struct unix_socket_client *client, int enabled)
{

	client->reading_line = enabled;
}

void
unix_socket_client_write_buffer(struct unix_socket_client *client, int enabled)
{

	client->writing_buffer = enabled;
}

/*
 *  1 Full line readed
 *  0 Partial read (no error)
 * -1 End of connection
 * -2 Buffer too long
 * -3 Unhandled error
 */
int
unix_socket_client_io_read(struct unix_socket_client *client)
{
	char buf[UNIX_SOCKET_CLIENT_BUFFER];
	ssize_t readed;
	int res;
	size_t zi;

	res = 0;
	readed = unix_socket_read(client->socket, buf, sizeof(buf));
	if (readed > 0) {
		client->msg_already_received_bytes += readed;
		if (dynar_cat(&client->receive_buffer, buf, readed) == -1) {
			res = -2;
			goto exit_err;
		}

		for (zi = 0; zi < (size_t)readed; zi++) {
			if (buf[zi] == '\n') {
				res = 1;
			}
		}
	}

	if (readed == 0) {
		res = -1;
	}

	if (readed < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		res = -3;
	}

exit_err:
	return (res);
}

/*
 *  1 All data succesfully sent
 *  0 Partial send (no error)
 * -1 End of connection
 * -2 Unhandled error
 */
int
unix_socket_client_io_write(struct unix_socket_client *client)
{
	ssize_t sent;
	size_t to_send;
	int res;

	res = 0;

	to_send = dynar_size(&client->send_buffer) - client->msg_already_sent_bytes;
	if (to_send > UNIX_SOCKET_CLIENT_BUFFER) {
		to_send = UNIX_SOCKET_CLIENT_BUFFER;
	}

	sent = unix_socket_write(client->socket,
	    dynar_data(&client->send_buffer) + client->msg_already_sent_bytes,
	    to_send);

	if (sent > 0) {
		client->msg_already_sent_bytes += sent;

		if (client->msg_already_sent_bytes == dynar_size(&client->send_buffer)) {
			return (1);
		}
	}

	if (sent == 0) {
		res = -1;
	}

	if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		res = -2;
	}

	return (res);
}
