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

#ifndef COROIPCC_H_DEFINED
#define COROIPCC_H_DEFINED

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <corosync/corotypes.h>
#include <corosync/hdb.h>

#ifdef __cplusplus
extern "C" {
#endif

extern cs_error_t
coroipcc_service_connect (
	const char *socket_name,
	unsigned int service,
	size_t request_size,
	size_t respnse__size,
	size_t dispatch_size,
	hdb_handle_t *handle);

extern cs_error_t
coroipcc_service_disconnect (
	hdb_handle_t handle);

extern cs_error_t
coroipcc_fd_get (
	hdb_handle_t handle,
	int *fd);

extern cs_error_t
coroipcc_dispatch_get (
	hdb_handle_t handle,
	void **buf,
	int timeout);

extern cs_error_t
coroipcc_dispatch_put (
	hdb_handle_t handle);

extern cs_error_t
coroipcc_dispatch_flow_control_get (
	hdb_handle_t handle,
	unsigned int *flow_control_state);

extern cs_error_t
coroipcc_msg_send_reply_receive (
	hdb_handle_t handle,
	const struct iovec *iov,
	unsigned int iov_len,
	void *res_msg,
	size_t res_len);

extern cs_error_t
coroipcc_msg_send_reply_receive_in_buf_get (
	hdb_handle_t handle,
	const struct iovec *iov,
	unsigned int iov_len,
	void **res_msg);

extern cs_error_t
coroipcc_msg_send_reply_receive_in_buf_put (
	hdb_handle_t handle);

extern cs_error_t
coroipcc_zcb_alloc (
	hdb_handle_t handle,
	void **buffer,
	size_t size,
        size_t header_size);

extern cs_error_t
coroipcc_zcb_free (
	hdb_handle_t handle,
	void *buffer);

extern cs_error_t
coroipcc_zcb_msg_send_reply_receive (
	hdb_handle_t handle,
	void *msg,
	void *res_msg,
	size_t res_len);

#ifdef __cplusplus
}
#endif

#endif /* COROIPCC_H_DEFINED */
