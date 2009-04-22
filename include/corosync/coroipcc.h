/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
 * Copyright (c) 2006-2007, 2009 Red Hat, Inc.
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

#include <config.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <corosync/corotypes.h>
#include <corosync/ipc_gen.h>

extern cs_error_t
coroipcc_service_connect (
	const char *socket_name,
	unsigned int service,
	size_t request_size,
	size_t respnse__size,
	size_t dispatch_size,
	void **ipc_context);

extern cs_error_t
coroipcc_service_disconnect (
	void *ipc_context);

extern int
coroipcc_fd_get (
	void *ipc_context);

extern int
coroipcc_dispatch_get (
	void *ipc_context,
	void **buf,
	int timeout);

extern int
coroipcc_dispatch_put (
	void *ipc_context);

extern int
coroipcc_dispatch_flow_control_get (
	void *ipc_context);

extern cs_error_t
coroipcc_msg_send_reply_receive (
	void *ipc_context,
	const struct iovec *iov,
	unsigned int iov_len,
	void *res_msg,
	size_t res_len);

extern cs_error_t
coroipcc_msg_send_reply_receive_in_buf (
	void *ipc_context,
	const struct iovec *iov,
	unsigned int iov_len,
	void **res_msg);

extern cs_error_t
coroipcc_zcb_alloc (
	void *ipc_context,
	void **buffer,
	size_t size,
        size_t header_size);

extern cs_error_t
coroipcc_zcb_free (
	void *ipc_context,
	void *buffer);

extern cs_error_t
coroipcc_zcb_msg_send_reply_receive (
	void *ipc_context,
	void *msg,
	void *res_msg,
	size_t res_len);

/*
 * TODO This needs to be removed
 */
struct saHandleDatabase {
	unsigned int handleCount;
	struct saHandle *handles;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spinlock_t lock;
#else
	pthread_mutex_t lock;
#endif
	void (*handleInstanceDestructor) (void *);
};

extern void saHandleDatabaseLock_init (struct saHandleDatabase *hdb);

#define DECLARE_SAHDB_DATABASE(database_name,destructor)		\
static struct saHandleDatabase (database_name) = {			\
	.handleInstanceDestructor	= destructor,			\
	.handleCount			= 0,				\
	.handles			= NULL,				\
};									\
static void database_name##_init(void)__attribute__((constructor));	\
static void database_name##_init(void)					\
{									\
        saHandleDatabaseLock_init (&(database_name));			\
}

extern cs_error_t
saHandleCreate (
	struct saHandleDatabase *handleDatabase,
	int instanceSize,
	uint64_t *handleOut);

extern cs_error_t
saHandleDestroy (
	struct saHandleDatabase *handleDatabase,
	uint64_t handle);

extern cs_error_t
saHandleInstanceGet (
	struct saHandleDatabase *handleDatabase,
	uint64_t handle,
	void **instance);

extern cs_error_t
saHandleInstancePut (
	struct saHandleDatabase *handleDatabase,
	uint64_t handle);

#endif /* COROIPC_H_DEFINED */
