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

#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "unix-socket.h"
#include "utils.h"

int
unix_socket_server_create(const char *path, int non_blocking, int backlog)
{
	int s;
	struct sockaddr_un sun;

	if (strlen(path) >= sizeof(sun.sun_path)) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		return (-1);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;

	strncpy(sun.sun_path, path, strlen(path));
	unlink(path);
	if (bind(s, (struct sockaddr *)&sun, SUN_LEN(&sun)) != 0) {
		close(s);

		return (-1);
	}

	if (non_blocking) {
		if (utils_fd_set_non_blocking(s) != 0) {
			close(s);

			return (-1);
		}
	}

	if (listen(s, backlog) != 0) {
		close(s);

		return (-1);
	}

	return (s);
}

int
unix_socket_client_create(const char *path, int non_blocking)
{
	int s;
	struct sockaddr_un sun;

	if (strlen(path) >= sizeof(sun.sun_path)) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		return (-1);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;

	strncpy(sun.sun_path, path, strlen(path));

	if (non_blocking) {
		if (utils_fd_set_non_blocking(s) != 0) {
			close(s);

			return (-1);
		}
	}

	if (connect(s, (struct sockaddr *)&sun, SUN_LEN(&sun)) != 0) {
		close(s);

		return (-1);
	}

	return (s);
}



int
unix_socket_server_destroy(int sock, const char *path)
{
	int res;

	res = 0;

	if (close(sock) != 0) {
		res = -1;
	}

	if (unlink(path) != 0) {
		res = -1;
	}

	return (res);
}

int
unix_socket_server_accept(int sock, int non_blocking)
{
	struct sockaddr_un sun;
	socklen_t sun_len;
	int client_sock;

	sun_len = sizeof(sun);
	if ((client_sock = accept(sock, (struct sockaddr *)&sun, &sun_len)) < 0) {
		return (-1);
	}

	if (non_blocking) {
		if (utils_fd_set_non_blocking(client_sock) != 0) {
			close(client_sock);

			return (-1);
		}
	}

	return (client_sock);
}

int
unix_socket_close(int sock)
{

	return (close(sock));
}

ssize_t
unix_socket_read(int sock, void *buf, size_t len)
{

	return (recv(sock, buf, len, 0));
}

ssize_t
unix_socket_write(int sock, void *buf, size_t len)
{

	return (send(sock, buf, len, 0));
}
