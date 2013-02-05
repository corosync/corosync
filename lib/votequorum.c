/*
 * Copyright (c) 2009 Red Hat, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
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
#include <errno.h>


#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>

#include <corosync/votequorum.h>
#include <corosync/ipc_votequorum.h>

#include "util.h"

struct votequorum_inst {
	hdb_handle_t handle;
	int finalize;
	void *context;
	votequorum_callbacks_t callbacks;
};

DECLARE_HDB_DATABASE(votequorum_handle_t_db,NULL);

cs_error_t votequorum_initialize (
	votequorum_handle_t *handle,
	votequorum_callbacks_t *callbacks)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;

	error = hdb_error_to_cs(hdb_handle_create (&votequorum_handle_t_db, sizeof (struct votequorum_inst), handle));
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, *handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		VOTEQUORUM_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		 &votequorum_inst->handle);
	if (error != CS_OK) {
		goto error_put_destroy;
	}

	if (callbacks)
		memcpy(&votequorum_inst->callbacks, callbacks, sizeof (*callbacks));
	else
		memset(&votequorum_inst->callbacks, 0, sizeof (*callbacks));

	hdb_handle_put (&votequorum_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	hdb_handle_put (&votequorum_handle_t_db, *handle);
error_destroy:
	hdb_handle_destroy (&votequorum_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

cs_error_t votequorum_finalize (
	votequorum_handle_t handle)
{
	struct votequorum_inst *votequorum_inst;
	cs_error_t error;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (votequorum_inst->finalize) {
		hdb_handle_put (&votequorum_handle_t_db, handle);
		return (CS_ERR_BAD_HANDLE);
	}

	votequorum_inst->finalize = 1;

	coroipcc_service_disconnect (votequorum_inst->handle);

	hdb_handle_destroy (&votequorum_handle_t_db, handle);

	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (CS_OK);
}


cs_error_t votequorum_getinfo (
	votequorum_handle_t handle,
	unsigned int nodeid,
	struct votequorum_info *info)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_getinfo req_lib_votequorum_getinfo;
	struct res_lib_votequorum_getinfo res_lib_votequorum_getinfo;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_votequorum_getinfo.header.size = sizeof (struct req_lib_votequorum_getinfo);
	req_lib_votequorum_getinfo.header.id = MESSAGE_REQ_VOTEQUORUM_GETINFO;
	req_lib_votequorum_getinfo.nodeid = nodeid;

	iov.iov_base = (char *)&req_lib_votequorum_getinfo;
	iov.iov_len = sizeof (struct req_lib_votequorum_getinfo);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_getinfo,
		sizeof (struct res_lib_votequorum_getinfo));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_getinfo.header.error;

	info->node_id = res_lib_votequorum_getinfo.nodeid;
	info->node_votes = res_lib_votequorum_getinfo.votes;
	info->node_expected_votes = res_lib_votequorum_getinfo.expected_votes;
	info->highest_expected = res_lib_votequorum_getinfo.highest_expected;
	info->total_votes = res_lib_votequorum_getinfo.total_votes;
	info->quorum = res_lib_votequorum_getinfo.quorum;
	info->flags = res_lib_votequorum_getinfo.flags;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_setexpected (
	votequorum_handle_t handle,
	unsigned int expected_votes)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_setexpected req_lib_votequorum_setexpected;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}


	req_lib_votequorum_setexpected.header.size = sizeof (struct req_lib_votequorum_setexpected);
	req_lib_votequorum_setexpected.header.id = MESSAGE_REQ_VOTEQUORUM_SETEXPECTED;
	req_lib_votequorum_setexpected.expected_votes = expected_votes;

	iov.iov_base = (char *)&req_lib_votequorum_setexpected;
	iov.iov_len = sizeof (struct req_lib_votequorum_setexpected);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_setvotes (
	votequorum_handle_t handle,
	unsigned int nodeid,
	unsigned int votes)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_setvotes req_lib_votequorum_setvotes;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_votequorum_setvotes.header.size = sizeof (struct req_lib_votequorum_setvotes);
	req_lib_votequorum_setvotes.header.id = MESSAGE_REQ_VOTEQUORUM_SETVOTES;
	req_lib_votequorum_setvotes.nodeid = nodeid;
	req_lib_votequorum_setvotes.votes = votes;

	iov.iov_base = (char *)&req_lib_votequorum_setvotes;
	iov.iov_len = sizeof (struct req_lib_votequorum_setvotes);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_qdisk_register (
	votequorum_handle_t handle,
	const char *name,
	unsigned int votes)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_qdisk_register req_lib_votequorum_qdisk_register;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	if (strlen(name) > VOTEQUORUM_MAX_QDISK_NAME_LEN)
		return CS_ERR_INVALID_PARAM;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}


	req_lib_votequorum_qdisk_register.header.size = sizeof (struct req_lib_votequorum_qdisk_register);
	req_lib_votequorum_qdisk_register.header.id = MESSAGE_REQ_VOTEQUORUM_QDISK_REGISTER;
	strcpy(req_lib_votequorum_qdisk_register.name, name);
	req_lib_votequorum_qdisk_register.votes = votes;

	iov.iov_base = (char *)&req_lib_votequorum_qdisk_register;
	iov.iov_len = sizeof (struct req_lib_votequorum_qdisk_register);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_qdisk_poll (
	votequorum_handle_t handle,
	unsigned int state)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_qdisk_poll req_lib_votequorum_qdisk_poll;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}


	req_lib_votequorum_qdisk_poll.header.size = sizeof (struct req_lib_votequorum_qdisk_poll);
	req_lib_votequorum_qdisk_poll.header.id = MESSAGE_REQ_VOTEQUORUM_QDISK_POLL;
	req_lib_votequorum_qdisk_poll.state = state;

	iov.iov_base = (char *)&req_lib_votequorum_qdisk_poll;
	iov.iov_len = sizeof (struct req_lib_votequorum_qdisk_poll);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_qdisk_unregister (
	votequorum_handle_t handle)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_general req_lib_votequorum_general;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_votequorum_general.header.size = sizeof (struct req_lib_votequorum_general);
	req_lib_votequorum_general.header.id = MESSAGE_REQ_VOTEQUORUM_QDISK_UNREGISTER;

	iov.iov_base = (char *)&req_lib_votequorum_general;
	iov.iov_len = sizeof (struct req_lib_votequorum_general);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}



cs_error_t votequorum_qdisk_getinfo (
	votequorum_handle_t handle,
	struct votequorum_qdisk_info *qinfo)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_general req_lib_votequorum_general;
	struct res_lib_votequorum_qdisk_getinfo res_lib_votequorum_qdisk_getinfo;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}


	req_lib_votequorum_general.header.size = sizeof (struct req_lib_votequorum_general);
	req_lib_votequorum_general.header.id = MESSAGE_REQ_VOTEQUORUM_QDISK_GETINFO;

	iov.iov_base = (char *)&req_lib_votequorum_general;
	iov.iov_len = sizeof (struct req_lib_votequorum_general);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_qdisk_getinfo,
		sizeof (struct res_lib_votequorum_qdisk_getinfo));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_qdisk_getinfo.header.error;

	qinfo->votes = res_lib_votequorum_qdisk_getinfo.votes;
	qinfo->state = res_lib_votequorum_qdisk_getinfo.state;
	strcpy(qinfo->name, res_lib_votequorum_qdisk_getinfo.name);


error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_setstate (
	votequorum_handle_t handle)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_general req_lib_votequorum_general;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_votequorum_general.header.size = sizeof (struct req_lib_votequorum_general);
	req_lib_votequorum_general.header.id = MESSAGE_REQ_VOTEQUORUM_SETSTATE;

	iov.iov_base = (char *)&req_lib_votequorum_general;
	iov.iov_len = sizeof (struct req_lib_votequorum_general);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_leaving (
	votequorum_handle_t handle)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_general req_lib_votequorum_general;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}


	req_lib_votequorum_general.header.size = sizeof (struct req_lib_votequorum_general);
	req_lib_votequorum_general.header.id = MESSAGE_REQ_VOTEQUORUM_LEAVING;

	iov.iov_base = (char *)&req_lib_votequorum_general;
	iov.iov_len = sizeof (struct req_lib_votequorum_general);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_trackstart (
	votequorum_handle_t handle,
	uint64_t context,
	unsigned int flags)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_trackstart req_lib_votequorum_trackstart;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_votequorum_trackstart.header.size = sizeof (struct req_lib_votequorum_trackstart);
	req_lib_votequorum_trackstart.header.id = MESSAGE_REQ_VOTEQUORUM_TRACKSTART;
	req_lib_votequorum_trackstart.track_flags = flags;
	req_lib_votequorum_trackstart.context = context;

	iov.iov_base = (char *)&req_lib_votequorum_trackstart;
	iov.iov_len = sizeof (struct req_lib_votequorum_trackstart);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_trackstop (
	votequorum_handle_t handle)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;
	struct iovec iov;
	struct req_lib_votequorum_general req_lib_votequorum_general;
	struct res_lib_votequorum_status res_lib_votequorum_status;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_votequorum_general.header.size = sizeof (struct req_lib_votequorum_general);
	req_lib_votequorum_general.header.id = MESSAGE_REQ_VOTEQUORUM_TRACKSTOP;

	iov.iov_base = (char *)&req_lib_votequorum_general;
	iov.iov_len = sizeof (struct req_lib_votequorum_general);

        error = coroipcc_msg_send_reply_receive (
		votequorum_inst->handle,
		&iov,
		1,
                &res_lib_votequorum_status,
		sizeof (struct res_lib_votequorum_status));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_votequorum_status.header.error;

error_exit:
	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}


cs_error_t votequorum_context_get (
	votequorum_handle_t handle,
	void **context)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	*context = votequorum_inst->context;

	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t votequorum_context_set (
	votequorum_handle_t handle,
	void *context)
{
	cs_error_t error;
	struct votequorum_inst *votequorum_inst;

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	votequorum_inst->context = context;

	hdb_handle_put (&votequorum_handle_t_db, handle);

	return (CS_OK);
}


cs_error_t votequorum_fd_get (
        votequorum_handle_t handle,
        int *fd)
{
	cs_error_t error;
        struct votequorum_inst *votequorum_inst;

        error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle, (void *)&votequorum_inst));
        if (error != CS_OK) {
                return (error);
        }

	error = coroipcc_fd_get (votequorum_inst->handle, fd);

	(void)hdb_handle_put (&votequorum_handle_t_db, handle);

	return (error);
}

cs_error_t votequorum_dispatch (
	votequorum_handle_t handle,
	cs_dispatch_flags_t dispatch_types)
{
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct votequorum_inst *votequorum_inst;
	votequorum_callbacks_t callbacks;
	coroipc_response_header_t *dispatch_data;
	struct res_lib_votequorum_notification *res_lib_votequorum_notification;
	struct res_lib_votequorum_expectedvotes_notification *res_lib_votequorum_expectedvotes_notification;

	if (dispatch_types != CS_DISPATCH_ONE &&
		dispatch_types != CS_DISPATCH_ALL &&
		dispatch_types != CS_DISPATCH_BLOCKING) {

		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs(hdb_handle_get (&votequorum_handle_t_db, handle,
		(void *)&votequorum_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for CS_DISPATCH_ONE or CS_DISPATCH_ALL and
	 * wait indefinitely for CS_DISPATCH_BLOCKING
	 */
	if (dispatch_types == CS_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		error = coroipcc_dispatch_get (
			votequorum_inst->handle,
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
		 * operate at the same time that votequorum_finalize has been called in another thread.
		 */
		memcpy (&callbacks, &votequorum_inst->callbacks, sizeof (votequorum_callbacks_t));

		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data->id) {

		case MESSAGE_RES_VOTEQUORUM_NOTIFICATION:
			if (callbacks.votequorum_notify_fn == NULL) {
				break;
			}
			res_lib_votequorum_notification = (struct res_lib_votequorum_notification *)dispatch_data;

			callbacks.votequorum_notify_fn ( handle,
							 res_lib_votequorum_notification->context,
							 res_lib_votequorum_notification->quorate,
							 res_lib_votequorum_notification->node_list_entries,
							 (votequorum_node_t *)res_lib_votequorum_notification->node_list );
				;
			break;

		case MESSAGE_RES_VOTEQUORUM_EXPECTEDVOTES_NOTIFICATION:
			if (callbacks.votequorum_expectedvotes_notify_fn == NULL) {
				break;
			}
			res_lib_votequorum_expectedvotes_notification = (struct res_lib_votequorum_expectedvotes_notification *)dispatch_data;

			callbacks.votequorum_expectedvotes_notify_fn ( handle,
								       res_lib_votequorum_expectedvotes_notification->context,
								       res_lib_votequorum_expectedvotes_notification->expected_votes);
			break;

		default:
			error = coroipcc_dispatch_put (votequorum_inst->handle);
			if (error == CS_OK) {
				error = CS_ERR_LIBRARY;
			}
			goto error_put;
			break;
		}
		error = coroipcc_dispatch_put (votequorum_inst->handle);
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

	goto error_put;

error_put:
	hdb_handle_put (&votequorum_handle_t_db, handle);
	return (error);
}
