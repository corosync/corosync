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
#include "qdevice-log.h"
#include "qdevice-local-socket.h"
#include "unix-socket.h"

int
qdevice_local_socket_init(struct qdevice_instance *instance)
{
	int local_socket;

	local_socket = unix_socket_server_create(QDEVICE_LOCAL_SOCKET_FILE, 1,
	    QDEVICE_LOCAL_SOCKET_BACKLOG);
	if (local_socket < 0) {
		qdevice_log_err(LOG_ERR, "Can't create unix socket");
		return (-1);
	}

	instance->local_socket_fd = local_socket;

	return (0);
}

void
qdevice_local_socket_destroy(struct qdevice_instance *instance)
{

	if (instance->local_socket_fd < 0) {
		return ;
	}

	if (close(instance->local_socket_fd) != 0) {
		qdevice_log_err(LOG_WARNING, "Can't close unix socket");
	}

	instance->local_socket_fd = -1;
	if (unlink(QDEVICE_LOCAL_SOCKET_FILE) != 0) {
		qdevice_log_err(LOG_WARNING, "Can't unlink unix socket");
	}
}
