/*
 * Copyright (c) 2008-2012 Red Hat, Inc.
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <errno.h>

#include <qb/qbipcc.h>
#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>

#include <corosync/quorum.h>
#include <corosync/ipc_quorum.h>

#include "util.h"

struct quorum_inst {
	qb_ipcc_connection_t *c;
	int finalize;
	const void *context;
	quorum_callbacks_t callbacks;
};

static void quorum_inst_free (void *inst);

DECLARE_HDB_DATABASE(quorum_handle_t_db, quorum_inst_free);

cs_error_t quorum_initialize (
	quorum_handle_t *handle,
	quorum_callbacks_t *callbacks,
	uint32_t *quorum_type)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;
	struct iovec iov;
	struct qb_ipc_request_header req;
	struct res_lib_quorum_gettype res_lib_quorum_gettype;

	error = hdb_error_to_cs(hdb_handle_create (&quorum_handle_t_db, sizeof (struct quorum_inst), handle));
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, *handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		goto error_destroy;
	}

	error = CS_OK;
	quorum_inst->finalize = 0;
	quorum_inst->c = qb_ipcc_connect ("quorum", IPC_REQUEST_SIZE);
	if (quorum_inst->c == NULL) {
		error = qb_to_cs_error(-errno);
		goto error_put_destroy;
	}

	req.size = sizeof (req);
	req.id = MESSAGE_REQ_QUORUM_GETTYPE;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (req);

	error = qb_to_cs_error(qb_ipcc_sendv_recv (
		quorum_inst->c,
		&iov,
		1,
		&res_lib_quorum_gettype,
		sizeof (struct res_lib_quorum_gettype), -1));

	if (error != CS_OK) {
		goto error_put_destroy;
	}

	error = res_lib_quorum_gettype.header.error;

	*quorum_type = res_lib_quorum_gettype.quorum_type;

	if (callbacks)
		memcpy(&quorum_inst->callbacks, callbacks, sizeof (*callbacks));
	else
		memset(&quorum_inst->callbacks, 0, sizeof (*callbacks));

	(void)hdb_handle_put (&quorum_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	(void)hdb_handle_put (&quorum_handle_t_db, *handle);
error_destroy:
	(void)hdb_handle_destroy (&quorum_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

static void quorum_inst_free (void *inst)
{
	struct quorum_inst *quorum_inst = (struct quorum_inst *)inst;
	qb_ipcc_disconnect(quorum_inst->c);
}

cs_error_t quorum_finalize (
	quorum_handle_t handle)
{
	struct quorum_inst *quorum_inst;
	cs_error_t error;

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (quorum_inst->finalize) {
		(void)hdb_handle_put (&quorum_handle_t_db, handle);
		return (CS_ERR_BAD_HANDLE);
	}

	quorum_inst->finalize = 1;

	(void)hdb_handle_destroy (&quorum_handle_t_db, handle);

	(void)hdb_handle_put (&quorum_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t quorum_getquorate (
	quorum_handle_t handle,
	int *quorate)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;
	struct iovec iov;
	struct qb_ipc_request_header req;
	struct res_lib_quorum_getquorate res_lib_quorum_getquorate;

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req.size = sizeof (req);
	req.id = MESSAGE_REQ_QUORUM_GETQUORATE;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (req);

	error = qb_to_cs_error(qb_ipcc_sendv_recv (
		quorum_inst->c,
		&iov,
		1,
		&res_lib_quorum_getquorate,
		sizeof (struct res_lib_quorum_getquorate), CS_IPC_TIMEOUT_MS));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_quorum_getquorate.header.error;

	*quorate = res_lib_quorum_getquorate.quorate;

error_exit:
	(void)hdb_handle_put (&quorum_handle_t_db, handle);

	return (error);
}

cs_error_t quorum_fd_get (
	quorum_handle_t handle,
	int *fd)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	error = qb_to_cs_error(qb_ipcc_fd_get (quorum_inst->c, fd));

	(void)hdb_handle_put (&quorum_handle_t_db, handle);

	return (error);
}


cs_error_t quorum_context_get (
	quorum_handle_t handle,
	const void **context)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	*context = quorum_inst->context;

	(void)hdb_handle_put (&quorum_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t quorum_context_set (
	quorum_handle_t handle,
	const void *context)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	quorum_inst->context = context;

	(void)hdb_handle_put (&quorum_handle_t_db, handle);

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
	struct qb_ipc_response_header res;

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_quorum_trackstart.header.size = sizeof (struct req_lib_quorum_trackstart);
	req_lib_quorum_trackstart.header.id = MESSAGE_REQ_QUORUM_TRACKSTART;
	req_lib_quorum_trackstart.track_flags = flags;

	iov.iov_base = (char *)&req_lib_quorum_trackstart;
	iov.iov_len = sizeof (struct req_lib_quorum_trackstart);

       error = qb_to_cs_error(qb_ipcc_sendv_recv (
		quorum_inst->c,
                &iov,
                1,
                &res,
                sizeof (res), CS_IPC_TIMEOUT_MS));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)hdb_handle_put (&quorum_handle_t_db, handle);

	return (error);
}

cs_error_t quorum_trackstop (
	quorum_handle_t handle)
{
	cs_error_t error;
	struct quorum_inst *quorum_inst;
	struct iovec iov;
	struct qb_ipc_request_header req;
	struct qb_ipc_response_header res;

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle, (void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req.size = sizeof (req);
	req.id = MESSAGE_REQ_QUORUM_TRACKSTOP;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (req);

       error = qb_to_cs_error(qb_ipcc_sendv_recv (
		quorum_inst->c,
                &iov,
                1,
                &res,
                sizeof (res), CS_IPC_TIMEOUT_MS));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)hdb_handle_put (&quorum_handle_t_db, handle);

	return (error);
}

cs_error_t quorum_dispatch (
	quorum_handle_t handle,
	cs_dispatch_flags_t dispatch_types)
{
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct quorum_inst *quorum_inst;
	quorum_callbacks_t callbacks;
	struct qb_ipc_response_header *dispatch_data;
	char dispatch_buf[IPC_DISPATCH_SIZE];
	struct res_lib_quorum_notification *res_lib_quorum_notification;

	if (dispatch_types != CS_DISPATCH_ONE &&
		dispatch_types != CS_DISPATCH_ALL &&
		dispatch_types != CS_DISPATCH_BLOCKING &&
		dispatch_types != CS_DISPATCH_ONE_NONBLOCKING) {

		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs(hdb_handle_get (&quorum_handle_t_db, handle,
		(void *)&quorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for CS_DISPATCH_ONE_NONBLOCKING or CS_DISPATCH_ALL and
	 * wait indefinately for CS_DISPATCH_ONE or CS_DISPATCH_BLOCKING
	 */
	if (dispatch_types == CS_DISPATCH_ALL || dispatch_types == CS_DISPATCH_ONE_NONBLOCKING) {
		timeout = 0;
	}

	dispatch_data = (struct qb_ipc_response_header *)dispatch_buf;
	do {
		error = qb_to_cs_error (qb_ipcc_event_recv (
			quorum_inst->c,
			dispatch_buf,
			IPC_DISPATCH_SIZE,
			timeout));
		if (error == CS_ERR_BAD_HANDLE) {
			error = CS_OK;
			goto error_put;
		}
		if (error == CS_ERR_TRY_AGAIN) {
			if (dispatch_types == CS_DISPATCH_ONE_NONBLOCKING) {
				/*
				 * Don't mask error
				 */
				goto error_put;
			}
			error = CS_OK;
			if (dispatch_types == CS_DISPATCH_ALL) {
				break; /* exit do while cont is 1 loop */
			} else {
				continue; /* next poll */
			}
		}
		if (error != CS_OK) {
			goto error_put;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that quorum_finalize has been called in another thread.
		 */
		memcpy (&callbacks, &quorum_inst->callbacks, sizeof (quorum_callbacks_t));
		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data->id) {

		case MESSAGE_RES_QUORUM_NOTIFICATION:
			if (callbacks.quorum_notify_fn == NULL) {
				break;
			}
			res_lib_quorum_notification = (struct res_lib_quorum_notification *)dispatch_data;

			callbacks.quorum_notify_fn ( handle,
				res_lib_quorum_notification->quorate,
				res_lib_quorum_notification->ring_seq,
				res_lib_quorum_notification->view_list_entries,
				res_lib_quorum_notification->view_list);
			break;

		default:
			error = CS_ERR_LIBRARY;
			goto error_put;
			break;
		}
		if (quorum_inst->finalize) {
			/*
			 * If the finalize has been called then get out of the dispatch.
			 */
			error = CS_ERR_BAD_HANDLE;
			goto error_put;
		}

		/*
		 * Determine if more messages should be processed
		 */
		if (dispatch_types == CS_DISPATCH_ONE || dispatch_types == CS_DISPATCH_ONE_NONBLOCKING) {
			cont = 0;
		}
	} while (cont);

error_put:
	(void)hdb_handle_put (&quorum_handle_t_db, handle);
	return (error);
}
