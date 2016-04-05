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

#ifndef _UNIX_SOCKET_CLIENT_H_
#define _UNIX_SOCKET_CLIENT_H_

#include <sys/types.h>

#include <sys/queue.h>
#include <inttypes.h>

#include "dynar.h"

#ifdef __cplusplus
extern "C" {
#endif

struct unix_socket_client {
	int socket;
	struct dynar receive_buffer;
	struct dynar send_buffer;
	size_t msg_already_received_bytes;
	size_t msg_already_sent_bytes;
	int reading_line;
	int writing_buffer;
	int schedule_disconnect;
	void *user_data;
	TAILQ_ENTRY(unix_socket_client) entries;
};

extern void		unix_socket_client_init(struct unix_socket_client *client, int sock,
    size_t max_receive_size, size_t max_send_size, void *user_data);

extern void		unix_socket_client_destroy(struct unix_socket_client *client);

extern void		unix_socket_client_read_line(struct unix_socket_client *client, int enabled);

extern void		unix_socket_client_write_buffer(struct unix_socket_client *client, int enabled);

extern int		unix_socket_client_io_read(struct unix_socket_client *client);

extern int		unix_socket_client_io_write(struct unix_socket_client *client);

#ifdef __cplusplus
}
#endif

#endif /* _UNIX_SOCKET_CLIENT_H_ */
