/*
 * Copyright (c) 2008, 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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
/*
 * Provides a quorum API using the corosync executive
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/mar_gen.h>
#include <corosync/ipc_gen.h>
#include <corosync/coroipcc.h>
#include "corosync/quorum.h"
#include "corosync/ipc_quorum.h"

struct quorum_inst {
	void *ipc_ctx;
	int finalize;
	const void *context;
	quorum_callbacks_t callbacks;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

static void quorum_instance_destructor (void *instance);

static struct saHandleDatabase quorum_handle_t_db = {
	.handleCount		        = 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= quorum_instance_destructor
};

/*
 * Clean up function for a quorum instance (quorum_initialize) handle
 */
static void quorum_instance_destructor (void *instance)
{
	struct quorum_inst *quorum_inst = instance;

	pthread_mutex_destroy (&quorum_inst->response_mutex);
}

cs_error_t quorum_initialize (
	quorum_handle_t *handle,
	quorum_callbacks_t *callbacks)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;

	error = saHandleCreate (&quorum_handle_t_db, sizeof (struct quorum_inst), handle);
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&quorum_handle_t_db, *handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (IPC_SOCKET_NAME, QUORUM_SERVICE, &quorum_inst->ipc_ctx);
	if (error != CS_OK) {
		goto error_put_destroy;
	}

	pthread_mutex_init (&quorum_inst->response_mutex, NULL);
	pthread_mutex_init (&quorum_inst->dispatch_mutex, NULL);
	if (callbacks)
		memcpy(&quorum_inst->callbacks, callbacks, sizeof (callbacks));
	else
		memset(&quorum_inst->callbacks, 0, sizeof (callbacks));

	(void)saHandleInstancePut (&quorum_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	(void)saHandleInstancePut (&quorum_handle_t_db, *handle);
error_destroy:
	(void)saHandleDestroy (&quorum_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

cs_error_t quorum_finalize (
	quorum_handle_t handle)
{
	struct quorum_inst *quorum_inst;
	cs_error_t error;

	error = saHandleInstanceGet (&quorum_handle_t_db, handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&quorum_inst->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (quorum_inst->finalize) {
		pthread_mutex_unlock (&quorum_inst->response_mutex);
		(void)saHandleInstancePut (&quorum_handle_t_db, handle);
		return (CS_ERR_BAD_HANDLE);
	}

	quorum_inst->finalize = 1;

	coroipcc_service_disconnect (quorum_inst->ipc_ctx);

	pthread_mutex_unlock (&quorum_inst->response_mutex);

	(void)saHandleDestroy (&quorum_handle_t_db, handle);

	(void)saHandleInstancePut (&quorum_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t quorum_getquorate (
	quorum_handle_t handle,
	int *quorate)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;
	struct iovec iov;
	mar_req_header_t req;
	struct res_lib_quorum_getquorate res_lib_quorum_getquorate;

	error = saHandleInstanceGet (&quorum_handle_t_db, handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&quorum_inst->response_mutex);

	req.size = sizeof (req);
	req.id = MESSAGE_REQ_QUORUM_GETQUORATE;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (req);

       error = coroipcc_msg_send_reply_receive (
		quorum_inst->ipc_ctx,
		&iov,
		1,
		&res_lib_quorum_getquorate,
		sizeof (struct res_lib_quorum_getquorate));

	pthread_mutex_unlock (&quorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_quorum_getquorate.header.error;

	*quorate = res_lib_quorum_getquorate.quorate;

error_exit:
	(void)saHandleInstancePut (&quorum_handle_t_db, handle);

	return (error);
}

cs_error_t quorum_fd_get (
	quorum_handle_t handle,
	int *fd)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;

	error = saHandleInstanceGet (&quorum_handle_t_db, handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	*fd = coroipcc_fd_get (quorum_inst->ipc_ctx);

	(void)saHandleInstancePut (&quorum_handle_t_db, handle);

	return (CS_OK);
}


cs_error_t quorum_context_get (
	quorum_handle_t handle,
	const void **context)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;

	error = saHandleInstanceGet (&quorum_handle_t_db, handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	*context = quorum_inst->context;

	(void)saHandleInstancePut (&quorum_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t quorum_context_set (
	quorum_handle_t handle,
	const void *context)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;

	error = saHandleInstanceGet (&quorum_handle_t_db, handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	quorum_inst->context = context;

	(void)saHandleInstancePut (&quorum_handle_t_db, handle);

	return (CS_OK);
}


cs_error_t quorum_trackstart (
	quorum_handle_t handle,
	unsigned int flags )
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;
	struct iovec iov;
	struct req_lib_quorum_trackstart req_lib_quorum_trackstart;
	mar_res_header_t res;

	error = saHandleInstanceGet (&quorum_handle_t_db, handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&quorum_inst->response_mutex);

	req_lib_quorum_trackstart.header.size = sizeof (struct req_lib_quorum_trackstart);
	req_lib_quorum_trackstart.header.id = MESSAGE_REQ_QUORUM_TRACKSTART;
	req_lib_quorum_trackstart.track_flags = flags;

	iov.iov_base = (char *)&req_lib_quorum_trackstart;
	iov.iov_len = sizeof (struct req_lib_quorum_trackstart);

       error = coroipcc_msg_send_reply_receive (
		quorum_inst->ipc_ctx,
                &iov,
                1,
                &res,
                sizeof (res));

	pthread_mutex_unlock (&quorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)saHandleInstancePut (&quorum_handle_t_db, handle);

	return (error);
}

cs_error_t quorum_trackstop (
	quorum_handle_t handle)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;
	struct iovec iov;
	mar_req_header_t req;
	mar_res_header_t res;

	error = saHandleInstanceGet (&quorum_handle_t_db, handle, (void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&quorum_inst->response_mutex);

	req.size = sizeof (req);
	req.id = MESSAGE_REQ_QUORUM_TRACKSTOP;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (req);

       error = coroipcc_msg_send_reply_receive (
		quorum_inst->ipc_ctx,
                &iov,
                1,
                &res,
                sizeof (res));

	pthread_mutex_unlock (&quorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)saHandleInstancePut (&quorum_handle_t_db, handle);

	return (error);
}

cs_error_t quorum_dispatch (
	quorum_handle_t handle,
	cs_dispatch_flags_t dispatch_types)
{
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct quorum_inst *quorum_inst;
	quorum_callbacks_t callbacks;
	mar_res_header_t *dispatch_data;
	struct res_lib_quorum_notification *res_lib_quorum_notification;

	if (dispatch_types != CS_DISPATCH_ONE &&
		dispatch_types != CS_DISPATCH_ALL &&
		dispatch_types != CS_DISPATCH_BLOCKING) {

		return (CS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&quorum_handle_t_db, handle,
		(void *)&quorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for CS_DISPATCH_ONE or SA_DISPATCH_ALL and
	 * wait indefinately for CS_DISPATCH_BLOCKING
	 */
	if (dispatch_types == CS_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		pthread_mutex_lock (&quorum_inst->dispatch_mutex);

		dispatch_avail = coroipcc_dispatch_get (
			quorum_inst->ipc_ctx,
			(void **)&dispatch_data,
			timeout);

		/*
		 * Handle has been finalized in another thread
		 */
		if (quorum_inst->finalize == 1) {
			error = CS_OK;
			goto error_unlock;
		}

		if (dispatch_avail == 0 && dispatch_types == CS_DISPATCH_ALL) {
			pthread_mutex_unlock (&quorum_inst->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&quorum_inst->dispatch_mutex);
			continue; /* next poll */
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that quorum_finalize has been called in another thread.
		 */
		memcpy (&callbacks, &quorum_inst->callbacks, sizeof (quorum_callbacks_t));
		pthread_mutex_unlock (&quorum_inst->dispatch_mutex);

		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data->id) {

		case MESSAGE_RES_QUORUM_NOTIFICATION:
			if (callbacks.quorum_notify_fn == NULL) {
				continue;
			}
			res_lib_quorum_notification = (struct res_lib_quorum_notification *)dispatch_data;

			callbacks.quorum_notify_fn ( handle,
				res_lib_quorum_notification->quorate,
				res_lib_quorum_notification->ring_seq,
				res_lib_quorum_notification->view_list_entries,
				res_lib_quorum_notification->view_list);
			break;

		default:
			coroipcc_dispatch_put (quorum_inst->ipc_ctx);
			error = CS_ERR_LIBRARY;
			goto error_put;
			break;
		}
		coroipcc_dispatch_put (quorum_inst->ipc_ctx);

		/*
		 * Determine if more messages should be processed
		 * */
		switch (dispatch_types) {
		case CS_DISPATCH_ONE:
			cont = 0;
			break;
		case CS_DISPATCH_ALL:
			break;
		case CS_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

	goto error_put;

error_unlock:
	pthread_mutex_unlock (&quorum_inst->dispatch_mutex);

error_put:
	(void)saHandleInstancePut (&quorum_handle_t_db, handle);
	return (error);
}
