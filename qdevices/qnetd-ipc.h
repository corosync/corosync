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

#ifndef _QNETD_IPC_H_
#define _QNETD_IPC_H_

#include "qnetd-instance.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qnetd_ipc_user_data {
	int shutdown_requested;
	PRFileDesc *nspr_poll_fd;
};

extern int		qnetd_ipc_init(struct qnetd_instance *instance);

extern int		qnetd_ipc_close(struct qnetd_instance *instance);

extern int		qnetd_ipc_is_closed(struct qnetd_instance *instance);

extern int		qnetd_ipc_destroy(struct qnetd_instance *instance);

extern int		qnetd_ipc_accept(struct qnetd_instance *instance,
    struct unix_socket_client **res_client);

extern void		qnetd_ipc_client_disconnect(struct qnetd_instance *instance,
    struct unix_socket_client *client);

extern void		qnetd_ipc_io_read(struct qnetd_instance *instance,
    struct unix_socket_client *client);

extern void		qnetd_ipc_io_write(struct qnetd_instance *instance,
    struct unix_socket_client *client);

extern int		qnetd_ipc_send_error(struct qnetd_instance *instance,
    struct unix_socket_client *client, const char *error_fmt, ...)
    __attribute__((__format__(__printf__, 3, 4)));

extern int		qnetd_ipc_send_buffer(struct qnetd_instance *instance,
    struct unix_socket_client *client);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_IPC_H_ */
