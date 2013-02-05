/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :

 * Copyright (c) 2004-2005 MontaVista Software, Inc.
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
/*
 * Provides an extended virtual synchrony API using the corosync executive
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>


#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>

#include <corosync/evs.h>
#include <corosync/ipc_evs.h>

#include "util.h"

#undef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))

struct evs_inst {
	hdb_handle_t handle;
	int finalize;
	evs_callbacks_t callbacks;
	void *context;
};

DECLARE_HDB_DATABASE (evs_handle_t_db,NULL);

/*
 * Clean up function for an evt instance (saEvtInitialize) handle
 */


/**
 * @defgroup evs_coroipcc The extended virtual synchrony passthrough API
 * @ingroup coroipcc
 *
 * @{
 */
/**
 * test
 * @param handle The handle of evs initialize
 * @param callbacks The callbacks for evs_initialize
 * @returns EVS_OK
 */
evs_error_t evs_initialize (
	evs_handle_t *handle,
	evs_callbacks_t *callbacks)
{
	cs_error_t error;
	struct evs_inst *evs_inst;

	error = hdb_error_to_cs(hdb_handle_create (&evs_handle_t_db, sizeof (struct evs_inst), handle));
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, *handle, (void *)&evs_inst));
	if (error != CS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		EVS_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&evs_inst->handle);
	if (error != EVS_OK) {
		goto error_put_destroy;
	}

	if (callbacks) {
		memcpy (&evs_inst->callbacks, callbacks, sizeof (evs_callbacks_t));
	}

	hdb_handle_put (&evs_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	hdb_handle_put (&evs_handle_t_db, *handle);
error_destroy:
	hdb_handle_destroy (&evs_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

evs_error_t evs_finalize (
	evs_handle_t handle)
{
	struct evs_inst *evs_inst;
	cs_error_t error;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (evs_inst->finalize) {
		hdb_handle_put (&evs_handle_t_db, handle);
		return (EVS_ERR_BAD_HANDLE);
	}

	evs_inst->finalize = 1;

	coroipcc_service_disconnect (evs_inst->handle);

	hdb_handle_destroy (&evs_handle_t_db, handle);

	hdb_handle_put (&evs_handle_t_db, handle);

	return (EVS_OK);
}

evs_error_t evs_fd_get (
	evs_handle_t handle,
	int *fd)
{
	cs_error_t error;
	struct evs_inst *evs_inst;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	coroipcc_fd_get (evs_inst->handle, fd);

	hdb_handle_put (&evs_handle_t_db, handle);

	return (CS_OK);
}

evs_error_t evs_context_get (
	evs_handle_t handle,
	void **context)
{
	cs_error_t error;
	struct evs_inst *evs_inst;

	error = hdb_error_to_cs (hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	*context = evs_inst->context;

	hdb_handle_put (&evs_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t evs_context_set (
	evs_handle_t handle,
	void *context)
{
	cs_error_t error;
	struct evs_inst *evs_inst;

	error = hdb_error_to_cs (hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	evs_inst->context = context;

	hdb_handle_put (&evs_handle_t_db, handle);

	return (CS_OK);
}

evs_error_t evs_dispatch (
	evs_handle_t handle,
	cs_dispatch_flags_t dispatch_types)
{
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct evs_inst *evs_inst;
	struct res_evs_confchg_callback *res_evs_confchg_callback;
	struct res_evs_deliver_callback *res_evs_deliver_callback;
	evs_callbacks_t callbacks;
	coroipc_response_header_t *dispatch_data;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for CS_DISPATCH_ONE or CS_DISPATCH_ALL and
	 * wait indefinately for CS_DISPATCH_BLOCKING
	 */
	if (dispatch_types == EVS_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		error = coroipcc_dispatch_get (
			evs_inst->handle,
			(void **)&dispatch_data,
			timeout);
		if (error == CS_ERR_BAD_HANDLE) {
			error = CS_OK;
			goto error_put;
		}
		if (error == CS_ERR_TRY_AGAIN) {
			error = CS_OK;
			if (dispatch_types == CPG_DISPATCH_ALL) {
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
		 * operate at the same time that evsFinalize has been called.
		*/
		memcpy (&callbacks, &evs_inst->callbacks, sizeof (evs_callbacks_t));

		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data->id) {
		case MESSAGE_RES_EVS_DELIVER_CALLBACK:
			if (callbacks.evs_deliver_fn == NULL) {
				break;
			}

			res_evs_deliver_callback = (struct res_evs_deliver_callback *)dispatch_data;
			callbacks.evs_deliver_fn (
				handle,
				res_evs_deliver_callback->local_nodeid,
				&res_evs_deliver_callback->msg,
				res_evs_deliver_callback->msglen);
			break;

		case MESSAGE_RES_EVS_CONFCHG_CALLBACK:
			if (callbacks.evs_confchg_fn == NULL) {
				break;
			}

			res_evs_confchg_callback = (struct res_evs_confchg_callback *)dispatch_data;
			callbacks.evs_confchg_fn (
				handle,
				res_evs_confchg_callback->member_list,
				res_evs_confchg_callback->member_list_entries,
				res_evs_confchg_callback->left_list,
				res_evs_confchg_callback->left_list_entries,
				res_evs_confchg_callback->joined_list,
				res_evs_confchg_callback->joined_list_entries,
				NULL);
			break;

		default:
			error = coroipcc_dispatch_put (evs_inst->handle);
			if (error == CS_OK) {
				error = CS_ERR_LIBRARY;
			}
			goto error_put;
			break;
		}
		error = coroipcc_dispatch_put (evs_inst->handle);
		if (error != CS_OK) {
			goto error_put;
		}

		/*
		 * Determine if more messages should be processed
		 */
		if (dispatch_types == CS_DISPATCH_ONE) {
			cont = 0;
		}
	} while (cont);

error_put:
	hdb_handle_put (&evs_handle_t_db, handle);
	return (error);
}

evs_error_t evs_join (
    evs_handle_t handle,
    const struct evs_group *groups,
    size_t group_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[2];
	struct req_lib_evs_join req_lib_evs_join;
	struct res_lib_evs_join res_lib_evs_join;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != EVS_OK) {
		return (error);
	}

	req_lib_evs_join.header.size = sizeof (struct req_lib_evs_join) +
		(group_entries * sizeof (struct evs_group));
	req_lib_evs_join.header.id = MESSAGE_REQ_EVS_JOIN;
	req_lib_evs_join.group_entries = group_entries;

	iov[0].iov_base = (void *)&req_lib_evs_join;
	iov[0].iov_len = sizeof (struct req_lib_evs_join);
	iov[1].iov_base = (void*) groups; /* cast away const */
	iov[1].iov_len = (group_entries * sizeof (struct evs_group));

	error = coroipcc_msg_send_reply_receive (evs_inst->handle, iov, 2,
		&res_lib_evs_join, sizeof (struct res_lib_evs_join));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_join.header.error;

error_exit:
	hdb_handle_put (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_leave (
    evs_handle_t handle,
    const struct evs_group *groups,
    size_t group_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[2];
	struct req_lib_evs_leave req_lib_evs_leave;
	struct res_lib_evs_leave res_lib_evs_leave;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_evs_leave.header.size = sizeof (struct req_lib_evs_leave) +
		(group_entries * sizeof (struct evs_group));
	req_lib_evs_leave.header.id = MESSAGE_REQ_EVS_LEAVE;
	req_lib_evs_leave.group_entries = group_entries;

	iov[0].iov_base = (void *)&req_lib_evs_leave;
	iov[0].iov_len = sizeof (struct req_lib_evs_leave);
	iov[1].iov_base = (void *) groups; /* cast away const */
	iov[1].iov_len = (group_entries * sizeof (struct evs_group));

	error = coroipcc_msg_send_reply_receive (evs_inst->handle, iov, 2,
		&res_lib_evs_leave, sizeof (struct res_lib_evs_leave));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_leave.header.error;

error_exit:
	hdb_handle_put (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_mcast_joined (
	evs_handle_t handle,
	evs_guarantee_t guarantee,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	int i;
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[64];
	struct req_lib_evs_mcast_joined req_lib_evs_mcast_joined;
	struct res_lib_evs_mcast_joined res_lib_evs_mcast_joined;
	size_t msg_len = 0;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	for (i = 0; i < iov_len; i++ ) {
		msg_len += iovec[i].iov_len;
	}

	req_lib_evs_mcast_joined.header.size = sizeof (struct req_lib_evs_mcast_joined) +
		msg_len;

	req_lib_evs_mcast_joined.header.id = MESSAGE_REQ_EVS_MCAST_JOINED;
	req_lib_evs_mcast_joined.guarantee = guarantee;
	req_lib_evs_mcast_joined.msg_len = msg_len;

	iov[0].iov_base = (void *)&req_lib_evs_mcast_joined;
	iov[0].iov_len = sizeof (struct req_lib_evs_mcast_joined);
	memcpy (&iov[1], iovec, iov_len * sizeof (struct iovec));

	error = coroipcc_msg_send_reply_receive (evs_inst->handle, iov,
		iov_len + 1,
		&res_lib_evs_mcast_joined,
		sizeof (struct res_lib_evs_mcast_joined));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_mcast_joined.header.error;

error_exit:
	hdb_handle_put (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_mcast_groups (
	evs_handle_t handle,
	evs_guarantee_t guarantee,
	const struct evs_group *groups,
	size_t group_entries,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	int i;
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[64]; /* FIXME: what if iov_len > 62 ?  use malloc */
	struct req_lib_evs_mcast_groups req_lib_evs_mcast_groups;
	struct res_lib_evs_mcast_groups res_lib_evs_mcast_groups;
	size_t msg_len = 0;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}
	for (i = 0; i < iov_len; i++) {
		msg_len += iovec[i].iov_len;
	}
	req_lib_evs_mcast_groups.header.size = sizeof (struct req_lib_evs_mcast_groups) +
		(group_entries * sizeof (struct evs_group)) + msg_len;
	req_lib_evs_mcast_groups.header.id = MESSAGE_REQ_EVS_MCAST_GROUPS;
	req_lib_evs_mcast_groups.guarantee = guarantee;
	req_lib_evs_mcast_groups.msg_len = msg_len;
	req_lib_evs_mcast_groups.group_entries = group_entries;

	iov[0].iov_base = (void *)&req_lib_evs_mcast_groups;
	iov[0].iov_len = sizeof (struct req_lib_evs_mcast_groups);
	iov[1].iov_base = (void *) groups; /* cast away const */
	iov[1].iov_len = (group_entries * sizeof (struct evs_group));
	memcpy (&iov[2], iovec, iov_len * sizeof (struct iovec));

	error = coroipcc_msg_send_reply_receive (evs_inst->handle, iov,
		iov_len + 2,
		&res_lib_evs_mcast_groups,
		sizeof (struct res_lib_evs_mcast_groups));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_mcast_groups.header.error;

error_exit:
	hdb_handle_put (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_membership_get (
	evs_handle_t handle,
	unsigned int *local_nodeid,
	unsigned int *member_list,
	size_t *member_list_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov;
	struct req_lib_evs_membership_get req_lib_evs_membership_get;
	struct res_lib_evs_membership_get res_lib_evs_membership_get;

	error = hdb_error_to_cs(hdb_handle_get (&evs_handle_t_db, handle, (void *)&evs_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_evs_membership_get.header.size = sizeof (struct req_lib_evs_membership_get);
	req_lib_evs_membership_get.header.id = MESSAGE_REQ_EVS_MEMBERSHIP_GET;

	iov.iov_base = (void *)&req_lib_evs_membership_get;
	iov.iov_len = sizeof (struct req_lib_evs_membership_get);

	error = coroipcc_msg_send_reply_receive (evs_inst->handle,
		&iov,
		1,
		&res_lib_evs_membership_get,
		sizeof (struct res_lib_evs_membership_get));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_membership_get.header.error;

	/*
	 * Copy results to caller
	 */
	if (local_nodeid) {
		*local_nodeid = res_lib_evs_membership_get.local_nodeid;
 	}
	*member_list_entries = MIN (*member_list_entries,
				    res_lib_evs_membership_get.member_list_entries);
	if (member_list) {
		memcpy (member_list, &res_lib_evs_membership_get.member_list,
			*member_list_entries * sizeof (struct in_addr));
	}

error_exit:
	hdb_handle_put (&evs_handle_t_db, handle);

	return (error);
}

/** @} */
