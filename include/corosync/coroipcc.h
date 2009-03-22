/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
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

#ifndef COROIPC_H_DEFINED
#define COROIPC_H_DEFINED

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <corosync/corotypes.h>
#include <corosync/ipc_gen.h>

/* Debug macro
 */
#ifdef DEBUG
	#define DPRINT(s) printf s
#else
	#define DPRINT(s)
#endif
		
#ifdef SO_NOSIGPIPE
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
void socket_nosigpipe(int s);
#else
#define socket_nosigpipe(s)
#endif

struct saHandleDatabase {
	unsigned int handleCount;
	struct saHandle *handles;
	pthread_mutex_t mutex;
	void (*handleInstanceDestructor) (void *);
};


cs_error_t
coroipcc_service_connect (
	const char *socket_name,
	enum service_types service,
	void **ipc_context);

cs_error_t
coroipcc_service_disconnect (
	void *ipc_context);

int
coroipcc_fd_get (
	void *ipc_context);

int
coroipcc_dispatch_recv (
	void *ipc_context,
	void *buf,
	int timeout);

int
coroipcc_dispatch_flow_control_get (
	void *ipc_context);

cs_error_t
coroipcc_msg_send_reply_receive (
	void *ipc_context,
	struct iovec *iov,
	int iov_len,
	void *res_msg,
	int res_len);

cs_error_t
coroipcc_msg_send_reply_receive_in_buf (
	void *ipc_context,
	struct iovec *iov,
	int iov_len,
	void **res_msg);

cs_error_t
saHandleCreate (
	struct saHandleDatabase *handleDatabase,
	int instanceSize,
	uint64_t *handleOut);

cs_error_t
saHandleDestroy (
	struct saHandleDatabase *handleDatabase,
	uint64_t handle);

cs_error_t
saHandleInstanceGet (
	struct saHandleDatabase *handleDatabase,
	uint64_t handle,
	void **instance);

cs_error_t
saHandleInstancePut (
	struct saHandleDatabase *handleDatabase,
	uint64_t handle);

#define offset_of(type,member) (int)(&(((type *)0)->member))

#endif /* COROIPC_H_DEFINED */

