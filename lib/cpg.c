/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
 * Copyright (c) 2006-2015 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfi@redhat.com)
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
 * Provides a closed process group API using the coroipcc executive
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#include <qb/qbdefs.h>
#include <qb/qbipcc.h>
#include <qb/qblog.h>

#include <corosync/hdb.h>
#include <corosync/list.h>
#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/cpg.h>
#include <corosync/ipc_cpg.h>

#include "util.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/*
 * Maximum number of times to retry a send when transmitting
 * a large message fragment
 */
#define MAX_RETRIES 100

/*
 * ZCB files have following umask (umask is same as used in libqb)
 */
#define CPG_MEMORY_MAP_UMASK		077

struct cpg_inst {
	qb_ipcc_connection_t *c;
	int finalize;
	void *context;
	union {
		cpg_model_data_t model_data;
		cpg_model_v1_data_t model_v1_data;
	};
	struct list_head iteration_list_head;
    uint32_t max_msg_size;
    char *assembly_buf;
    uint32_t assembly_buf_ptr;
    int assembling; /* Flag that says we have started assembling a message.
					 * It's here to catch the situation where a node joins
					 * the cluster/group in the middle of a CPG message send
					 * so we don't pass on a partial message to the client.
					 */
};
static void cpg_inst_free (void *inst);

DECLARE_HDB_DATABASE(cpg_handle_t_db, cpg_inst_free);

struct cpg_iteration_instance_t {
	cpg_iteration_handle_t cpg_iteration_handle;
	qb_ipcc_connection_t *conn;
	hdb_handle_t executive_iteration_handle;
	struct list_head list;
};

DECLARE_HDB_DATABASE(cpg_iteration_handle_t_db,NULL);


/*
 * Internal (not visible by API) functions
 */

static cs_error_t
coroipcc_msg_send_reply_receive (
	qb_ipcc_connection_t *c,
	const struct iovec *iov,
	unsigned int iov_len,
	void *res_msg,
	size_t res_len)
{
	return qb_to_cs_error(qb_ipcc_sendv_recv(c, iov, iov_len, res_msg, res_len,
				CS_IPC_TIMEOUT_MS));
}

static void cpg_iteration_instance_finalize (struct cpg_iteration_instance_t *cpg_iteration_instance)
{
	list_del (&cpg_iteration_instance->list);
	hdb_handle_destroy (&cpg_iteration_handle_t_db, cpg_iteration_instance->cpg_iteration_handle);
}

static void cpg_inst_free (void *inst)
{
	struct cpg_inst *cpg_inst = (struct cpg_inst *)inst;
	qb_ipcc_disconnect(cpg_inst->c);
}

static void cpg_inst_finalize (struct cpg_inst *cpg_inst, hdb_handle_t handle)
{
	struct list_head *iter, *iter_next;
	struct cpg_iteration_instance_t *cpg_iteration_instance;

	/*
	 * Traverse thru iteration instances and delete them
	 */
	for (iter = cpg_inst->iteration_list_head.next;	iter != &cpg_inst->iteration_list_head;iter = iter_next) {
		iter_next = iter->next;

		cpg_iteration_instance = list_entry (iter, struct cpg_iteration_instance_t, list);

		cpg_iteration_instance_finalize (cpg_iteration_instance);
	}
	hdb_handle_destroy (&cpg_handle_t_db, handle);
}

/**
 * @defgroup cpg_coroipcc The closed process group API
 * @ingroup coroipcc
 *
 * @{
 */

cs_error_t cpg_initialize (
	cpg_handle_t *handle,
	cpg_callbacks_t *callbacks)
{
	cpg_model_v1_data_t model_v1_data;

	memset (&model_v1_data, 0, sizeof (cpg_model_v1_data_t));

	if (callbacks) {
		model_v1_data.cpg_deliver_fn = callbacks->cpg_deliver_fn;
		model_v1_data.cpg_confchg_fn = callbacks->cpg_confchg_fn;
	}

	return (cpg_model_initialize (handle, CPG_MODEL_V1, (cpg_model_data_t *)&model_v1_data, NULL));
}

cs_error_t cpg_model_initialize (
	cpg_handle_t *handle,
	cpg_model_t model,
	cpg_model_data_t *model_data,
	void *context)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;

	if (model != CPG_MODEL_V1) {
		error = CS_ERR_INVALID_PARAM;
		goto error_no_destroy;
	}

	error = hdb_error_to_cs (hdb_handle_create (&cpg_handle_t_db, sizeof (struct cpg_inst), handle));
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, *handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		goto error_destroy;
	}

	cpg_inst->c = qb_ipcc_connect ("cpg", IPC_REQUEST_SIZE);
	if (cpg_inst->c == NULL) {
		error = qb_to_cs_error(-errno);
		goto error_put_destroy;
	}

	if (model_data != NULL) {
		switch (model) {
		case CPG_MODEL_V1:
			memcpy (&cpg_inst->model_v1_data, model_data, sizeof (cpg_model_v1_data_t));
			if ((cpg_inst->model_v1_data.flags & ~(CPG_MODEL_V1_DELIVER_INITIAL_TOTEM_CONF)) != 0) {
				error = CS_ERR_INVALID_PARAM;

				goto error_destroy;
			}
			break;
		}
	}

	/* Allow space for corosync internal headers */
	cpg_inst->max_msg_size = IPC_REQUEST_SIZE - 1024;
	cpg_inst->model_data.model = model;
	cpg_inst->context = context;

	list_init(&cpg_inst->iteration_list_head);

	hdb_handle_put (&cpg_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	hdb_handle_put (&cpg_handle_t_db, *handle);
error_destroy:
	hdb_handle_destroy (&cpg_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

cs_error_t cpg_finalize (
	cpg_handle_t handle)
{
	struct cpg_inst *cpg_inst;
	struct iovec iov;
	struct req_lib_cpg_finalize req_lib_cpg_finalize;
	struct res_lib_cpg_finalize res_lib_cpg_finalize;
	cs_error_t error;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (cpg_inst->finalize) {
		hdb_handle_put (&cpg_handle_t_db, handle);
		return (CS_ERR_BAD_HANDLE);
	}

	cpg_inst->finalize = 1;

	/*
	 * Send service request
	 */
	req_lib_cpg_finalize.header.size = sizeof (struct req_lib_cpg_finalize);
	req_lib_cpg_finalize.header.id = MESSAGE_REQ_CPG_FINALIZE;

	iov.iov_base = (void *)&req_lib_cpg_finalize;
	iov.iov_len = sizeof (struct req_lib_cpg_finalize);

	error = coroipcc_msg_send_reply_receive (cpg_inst->c,
		&iov,
		1,
		&res_lib_cpg_finalize,
		sizeof (struct res_lib_cpg_finalize));

	cpg_inst_finalize (cpg_inst, handle);
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_fd_get (
	cpg_handle_t handle,
	int *fd)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	error = qb_to_cs_error (qb_ipcc_fd_get (cpg_inst->c, fd));

	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_max_atomic_msgsize_get (
	cpg_handle_t handle,
	uint32_t *size)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	*size = cpg_inst->max_msg_size;

	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_context_get (
	cpg_handle_t handle,
	void **context)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	*context = cpg_inst->context;

	hdb_handle_put (&cpg_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t cpg_context_set (
	cpg_handle_t handle,
	void *context)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	cpg_inst->context = context;

	hdb_handle_put (&cpg_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t cpg_dispatch (
	cpg_handle_t handle,
	cs_dispatch_flags_t dispatch_types)
{
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct cpg_inst *cpg_inst;
	struct res_lib_cpg_confchg_callback *res_cpg_confchg_callback;
	struct res_lib_cpg_deliver_callback *res_cpg_deliver_callback;
	struct res_lib_cpg_partial_deliver_callback *res_cpg_partial_deliver_callback;
	struct res_lib_cpg_totem_confchg_callback *res_cpg_totem_confchg_callback;
	struct cpg_inst cpg_inst_copy;
	struct qb_ipc_response_header *dispatch_data;
	struct cpg_address member_list[CPG_MEMBERS_MAX];
	struct cpg_address left_list[CPG_MEMBERS_MAX];
	struct cpg_address joined_list[CPG_MEMBERS_MAX];
	struct cpg_name group_name;
	mar_cpg_address_t *left_list_start;
	mar_cpg_address_t *joined_list_start;
	unsigned int i;
	struct cpg_ring_id ring_id;
	uint32_t totem_member_list[CPG_MEMBERS_MAX];
	int32_t errno_res;
	char dispatch_buf[IPC_DISPATCH_SIZE];

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for CS_DISPATCH_ONE_NONBLOCKING or CS_DISPATCH_ALL and
	 * wait indefinitely for CS_DISPATCH_ONE or CS_DISPATCH_BLOCKING
	 */
	if (dispatch_types == CS_DISPATCH_ALL || dispatch_types == CS_DISPATCH_ONE_NONBLOCKING) {
		timeout = 0;
	}

	dispatch_data = (struct qb_ipc_response_header *)dispatch_buf;
	do {
		errno_res = qb_ipcc_event_recv (
			cpg_inst->c,
			dispatch_buf,
			IPC_DISPATCH_SIZE,
			timeout);
		error = qb_to_cs_error (errno_res);
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
		 * operate at the same time that cpgFinalize has been called.
		 */
		memcpy (&cpg_inst_copy, cpg_inst, sizeof (struct cpg_inst));
		switch (cpg_inst_copy.model_data.model) {
		case CPG_MODEL_V1:
			/*
			 * Dispatch incoming message
			 */
			switch (dispatch_data->id) {
			case MESSAGE_RES_CPG_DELIVER_CALLBACK:
				if (cpg_inst_copy.model_v1_data.cpg_deliver_fn == NULL) {
					break;
				}

				res_cpg_deliver_callback = (struct res_lib_cpg_deliver_callback *)dispatch_data;

				marshall_from_mar_cpg_name_t (
					&group_name,
					&res_cpg_deliver_callback->group_name);

				cpg_inst_copy.model_v1_data.cpg_deliver_fn (handle,
					&group_name,
					res_cpg_deliver_callback->nodeid,
					res_cpg_deliver_callback->pid,
					&res_cpg_deliver_callback->message,
					res_cpg_deliver_callback->msglen);
				break;

			case MESSAGE_RES_CPG_PARTIAL_DELIVER_CALLBACK:
				res_cpg_partial_deliver_callback = (struct res_lib_cpg_partial_deliver_callback *)dispatch_data;

				marshall_from_mar_cpg_name_t (
					&group_name,
					&res_cpg_partial_deliver_callback->group_name);

				if (res_cpg_partial_deliver_callback->type == LIBCPG_PARTIAL_FIRST) {
					/*
					 * Allocate a buffer to contain a full message.
					 */
					cpg_inst->assembly_buf = malloc(res_cpg_partial_deliver_callback->msglen);
					if (!cpg_inst->assembly_buf) {
						error = CS_ERR_NO_MEMORY;
						goto error_put;
					}
					cpg_inst->assembling = 1;
					cpg_inst->assembly_buf_ptr = 0;
				}
				if (cpg_inst->assembling) {
					memcpy(cpg_inst->assembly_buf + cpg_inst->assembly_buf_ptr,
					       res_cpg_partial_deliver_callback->message, res_cpg_partial_deliver_callback->fraglen);
					cpg_inst->assembly_buf_ptr += res_cpg_partial_deliver_callback->fraglen;

					if (res_cpg_partial_deliver_callback->type == LIBCPG_PARTIAL_LAST) {
						cpg_inst_copy.model_v1_data.cpg_deliver_fn (handle,
							&group_name,
							res_cpg_partial_deliver_callback->nodeid,
							res_cpg_partial_deliver_callback->pid,
							cpg_inst->assembly_buf,
							res_cpg_partial_deliver_callback->msglen);
						free(cpg_inst->assembly_buf);
						cpg_inst->assembling = 0;
					}
				}
				break;

			case MESSAGE_RES_CPG_CONFCHG_CALLBACK:
				if (cpg_inst_copy.model_v1_data.cpg_confchg_fn == NULL) {
					break;
				}

				res_cpg_confchg_callback = (struct res_lib_cpg_confchg_callback *)dispatch_data;

				for (i = 0; i < res_cpg_confchg_callback->member_list_entries; i++) {
					marshall_from_mar_cpg_address_t (&member_list[i],
						&res_cpg_confchg_callback->member_list[i]);
				}
				left_list_start = res_cpg_confchg_callback->member_list +
					res_cpg_confchg_callback->member_list_entries;
				for (i = 0; i < res_cpg_confchg_callback->left_list_entries; i++) {
					marshall_from_mar_cpg_address_t (&left_list[i],
						&left_list_start[i]);
				}
				joined_list_start = res_cpg_confchg_callback->member_list +
					res_cpg_confchg_callback->member_list_entries +
					res_cpg_confchg_callback->left_list_entries;
				for (i = 0; i < res_cpg_confchg_callback->joined_list_entries; i++) {
					marshall_from_mar_cpg_address_t (&joined_list[i],
						&joined_list_start[i]);
				}
				marshall_from_mar_cpg_name_t (
					&group_name,
					&res_cpg_confchg_callback->group_name);

				cpg_inst_copy.model_v1_data.cpg_confchg_fn (handle,
					&group_name,
					member_list,
					res_cpg_confchg_callback->member_list_entries,
					left_list,
					res_cpg_confchg_callback->left_list_entries,
					joined_list,
					res_cpg_confchg_callback->joined_list_entries);

				break;
			case MESSAGE_RES_CPG_TOTEM_CONFCHG_CALLBACK:
				if (cpg_inst_copy.model_v1_data.cpg_totem_confchg_fn == NULL) {
					break;
				}

				res_cpg_totem_confchg_callback = (struct res_lib_cpg_totem_confchg_callback *)dispatch_data;

				marshall_from_mar_cpg_ring_id_t (&ring_id, &res_cpg_totem_confchg_callback->ring_id);
				for (i = 0; i < res_cpg_totem_confchg_callback->member_list_entries; i++) {
					totem_member_list[i] = res_cpg_totem_confchg_callback->member_list[i];
				}

				cpg_inst_copy.model_v1_data.cpg_totem_confchg_fn (handle,
					ring_id,
					res_cpg_totem_confchg_callback->member_list_entries,
					totem_member_list);
				break;
			default:
				error = CS_ERR_LIBRARY;
				goto error_put;
				break;
			} /* - switch (dispatch_data->id) */
			break; /* case CPG_MODEL_V1 */
		} /* - switch (cpg_inst_copy.model_data.model) */

		if (cpg_inst_copy.finalize || cpg_inst->finalize) {
			/*
			 * If the finalize has been called then get out of the dispatch.
			 */
			cpg_inst->finalize = 1;
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
	hdb_handle_put (&cpg_handle_t_db, handle);
	return (error);
}

cs_error_t cpg_join (
    cpg_handle_t handle,
    const struct cpg_name *group)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;
	struct iovec iov[2];
	struct req_lib_cpg_join req_lib_cpg_join;
	struct res_lib_cpg_join response;

	if (group->length > CPG_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
	}

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	/* Now join */
	req_lib_cpg_join.header.size = sizeof (struct req_lib_cpg_join);
	req_lib_cpg_join.header.id = MESSAGE_REQ_CPG_JOIN;
	req_lib_cpg_join.pid = getpid();
	req_lib_cpg_join.flags = 0;

	switch (cpg_inst->model_data.model) {
	case CPG_MODEL_V1:
		req_lib_cpg_join.flags = cpg_inst->model_v1_data.flags;
		break;
	}

	marshall_to_mar_cpg_name_t (&req_lib_cpg_join.group_name,
		group);

	iov[0].iov_base = (void *)&req_lib_cpg_join;
	iov[0].iov_len = sizeof (struct req_lib_cpg_join);

	do {
		error = coroipcc_msg_send_reply_receive (cpg_inst->c, iov, 1,
			&response, sizeof (struct res_lib_cpg_join));

		if (error != CS_OK) {
			goto error_exit;
		}
	} while (response.header.error == CS_ERR_BUSY);

	error = response.header.error;

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_leave (
    cpg_handle_t handle,
    const struct cpg_name *group)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;
	struct iovec iov[2];
	struct req_lib_cpg_leave req_lib_cpg_leave;
	struct res_lib_cpg_leave res_lib_cpg_leave;

        if (group->length > CPG_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
        }

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_cpg_leave.header.size = sizeof (struct req_lib_cpg_leave);
	req_lib_cpg_leave.header.id = MESSAGE_REQ_CPG_LEAVE;
	req_lib_cpg_leave.pid = getpid();
	marshall_to_mar_cpg_name_t (&req_lib_cpg_leave.group_name,
		group);

	iov[0].iov_base = (void *)&req_lib_cpg_leave;
	iov[0].iov_len = sizeof (struct req_lib_cpg_leave);

	do {
		error = coroipcc_msg_send_reply_receive (cpg_inst->c, iov, 1,
			&res_lib_cpg_leave, sizeof (struct res_lib_cpg_leave));

		if (error != CS_OK) {
			goto error_exit;
		}
	} while (res_lib_cpg_leave.header.error == CS_ERR_BUSY);

	error = res_lib_cpg_leave.header.error;

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_membership_get (
	cpg_handle_t handle,
	struct cpg_name *group_name,
	struct cpg_address *member_list,
	int *member_list_entries)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;
	struct iovec iov;
	struct req_lib_cpg_membership_get req_lib_cpg_membership_get;
	struct res_lib_cpg_membership_get res_lib_cpg_membership_get;
	unsigned int i;

	if (group_name->length > CPG_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
	}
	if (member_list == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}
	if (member_list_entries == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_cpg_membership_get.header.size = sizeof (struct req_lib_cpg_membership_get);
	req_lib_cpg_membership_get.header.id = MESSAGE_REQ_CPG_MEMBERSHIP;

	marshall_to_mar_cpg_name_t (&req_lib_cpg_membership_get.group_name,
		group_name);

	iov.iov_base = (void *)&req_lib_cpg_membership_get;
	iov.iov_len = sizeof (struct req_lib_cpg_membership_get);

	error = coroipcc_msg_send_reply_receive (cpg_inst->c, &iov, 1,
			&res_lib_cpg_membership_get, sizeof (res_lib_cpg_membership_get));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cpg_membership_get.header.error;

	/*
	 * Copy results to caller
	 */
	*member_list_entries = res_lib_cpg_membership_get.member_count;
	if (member_list) {
		for (i = 0; i < res_lib_cpg_membership_get.member_count; i++) {
			marshall_from_mar_cpg_address_t (&member_list[i],
				&res_lib_cpg_membership_get.member_list[i]);
		}
	}

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_local_get (
	cpg_handle_t handle,
	unsigned int *local_nodeid)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;
	struct iovec iov;
	struct req_lib_cpg_local_get req_lib_cpg_local_get;
	struct res_lib_cpg_local_get res_lib_cpg_local_get;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_cpg_local_get.header.size = sizeof (struct qb_ipc_request_header);
	req_lib_cpg_local_get.header.id = MESSAGE_REQ_CPG_LOCAL_GET;

	iov.iov_base = (void *)&req_lib_cpg_local_get;
	iov.iov_len = sizeof (struct req_lib_cpg_local_get);

	error = coroipcc_msg_send_reply_receive (cpg_inst->c, &iov, 1,
		&res_lib_cpg_local_get, sizeof (res_lib_cpg_local_get));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cpg_local_get.header.error;

	*local_nodeid = res_lib_cpg_local_get.local_nodeid;

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_flow_control_state_get (
	cpg_handle_t handle,
	cpg_flow_control_state_t *flow_control_state)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}
	*flow_control_state = CPG_FLOW_CONTROL_DISABLED;
	error = CS_OK;

	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

static int
memory_map (char *path, const char *file, void **buf, size_t bytes)
{
	int32_t fd;
	void *addr;
	int32_t res;
	char *buffer;
	int32_t i;
	size_t written;
	size_t page_size; 
	long int sysconf_page_size;
	mode_t old_umask;

	snprintf (path, PATH_MAX, "/dev/shm/%s", file);

	old_umask = umask(CPG_MEMORY_MAP_UMASK);
	fd = mkstemp (path);
	(void)umask(old_umask);
	if (fd == -1) {
		snprintf (path, PATH_MAX, LOCALSTATEDIR "/run/%s", file);
		old_umask = umask(CPG_MEMORY_MAP_UMASK);
		fd = mkstemp (path);
		(void)umask(old_umask);
		if (fd == -1) {
			return (-1);
		}
	}

	res = ftruncate (fd, bytes);
	if (res == -1) {
		goto error_close_unlink;
	}
	sysconf_page_size = sysconf(_SC_PAGESIZE);
	if (sysconf_page_size <= 0) {
		goto error_close_unlink;
	}
	page_size = sysconf_page_size;
	buffer = malloc (page_size);
	if (buffer == NULL) {
		goto error_close_unlink;
	}
	memset (buffer, 0, page_size);
	for (i = 0; i < (bytes / page_size); i++) {
retry_write:
		written = write (fd, buffer, page_size);
		if (written == -1 && errno == EINTR) {
			goto retry_write;
		}
		if (written != page_size) {
			free (buffer);
			goto error_close_unlink;
		}
	}
	free (buffer);

	addr = mmap (NULL, bytes, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);

	if (addr == MAP_FAILED) {
		goto error_close_unlink;
	}
#ifdef MADV_NOSYNC
	madvise(addr, bytes, MADV_NOSYNC);
#endif

	res = close (fd);
	if (res) {
		munmap(addr, bytes);

		return (-1);
	}
	*buf = addr;

	return 0;

error_close_unlink:
	close (fd);
	unlink(path);
	return -1;
}

cs_error_t cpg_zcb_alloc (
	cpg_handle_t handle,
	size_t size,
	void **buffer)
{
	void *buf = NULL;
	char path[PATH_MAX];
	mar_req_coroipcc_zc_alloc_t req_coroipcc_zc_alloc;
	struct qb_ipc_response_header res_coroipcs_zc_alloc;
	size_t map_size;
	struct iovec iovec;
	struct coroipcs_zc_header *hdr;
	cs_error_t error;
	struct cpg_inst *cpg_inst;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	map_size = size + sizeof (struct req_lib_cpg_mcast) + sizeof (struct coroipcs_zc_header);
	assert(memory_map (path, "corosync_zerocopy-XXXXXX", &buf, map_size) != -1);

	if (strlen(path) >= CPG_ZC_PATH_LEN) {
		unlink(path);
		munmap (buf, map_size);
		return (CS_ERR_NAME_TOO_LONG);
	}

	req_coroipcc_zc_alloc.header.size = sizeof (mar_req_coroipcc_zc_alloc_t);
	req_coroipcc_zc_alloc.header.id = MESSAGE_REQ_CPG_ZC_ALLOC;
	req_coroipcc_zc_alloc.map_size = map_size;
	strcpy (req_coroipcc_zc_alloc.path_to_file, path);

	iovec.iov_base = (void *)&req_coroipcc_zc_alloc;
	iovec.iov_len = sizeof (mar_req_coroipcc_zc_alloc_t);

	error = coroipcc_msg_send_reply_receive (
		cpg_inst->c,
		&iovec,
		1,
		&res_coroipcs_zc_alloc,
		sizeof (struct qb_ipc_response_header));

	if (error != CS_OK) {
		goto error_exit;
	}

	hdr = (struct coroipcs_zc_header *)buf;
	hdr->map_size = map_size;
	*buffer = ((char *)buf) + sizeof (struct coroipcs_zc_header) + sizeof (struct req_lib_cpg_mcast);

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);
	return (error);
}

cs_error_t cpg_zcb_free (
	cpg_handle_t handle,
	void *buffer)
{
	cs_error_t error;
	unsigned int res;
	struct cpg_inst *cpg_inst;
	mar_req_coroipcc_zc_free_t req_coroipcc_zc_free;
	struct qb_ipc_response_header res_coroipcs_zc_free;
	struct iovec iovec;
	struct coroipcs_zc_header *header = (struct coroipcs_zc_header *)((char *)buffer - sizeof (struct coroipcs_zc_header) - sizeof (struct req_lib_cpg_mcast));

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_coroipcc_zc_free.header.size = sizeof (mar_req_coroipcc_zc_free_t);
	req_coroipcc_zc_free.header.id = MESSAGE_REQ_CPG_ZC_FREE;
	req_coroipcc_zc_free.map_size = header->map_size;
	req_coroipcc_zc_free.server_address = header->server_address;

	iovec.iov_base = (void *)&req_coroipcc_zc_free;
	iovec.iov_len = sizeof (mar_req_coroipcc_zc_free_t);

	error = coroipcc_msg_send_reply_receive (
		cpg_inst->c,
		&iovec,
		1,
		&res_coroipcs_zc_free,
		sizeof (struct qb_ipc_response_header));

	if (error != CS_OK) {
		goto error_exit;
	}

	res = munmap ((void *)header, header->map_size);
	if (res == -1) {
		error = qb_to_cs_error(-errno);

		goto error_exit;
	}

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_zcb_mcast_joined (
	cpg_handle_t handle,
	cpg_guarantee_t guarantee,
	void *msg,
	size_t msg_len)
{
	cs_error_t error;
	struct cpg_inst *cpg_inst;
	struct req_lib_cpg_mcast *req_lib_cpg_mcast;
	struct res_lib_cpg_mcast res_lib_cpg_mcast;
	mar_req_coroipcc_zc_execute_t req_coroipcc_zc_execute;
	struct coroipcs_zc_header *hdr;
	struct iovec iovec;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (msg_len > IPC_REQUEST_SIZE) {
		error = CS_ERR_TOO_BIG;
		goto error_exit;
	}

	req_lib_cpg_mcast = (struct req_lib_cpg_mcast *)(((char *)msg) - sizeof (struct req_lib_cpg_mcast));
	req_lib_cpg_mcast->header.size = sizeof (struct req_lib_cpg_mcast) +
		msg_len;

	req_lib_cpg_mcast->header.id = MESSAGE_REQ_CPG_MCAST;
	req_lib_cpg_mcast->guarantee = guarantee;
	req_lib_cpg_mcast->msglen = msg_len;

	hdr = (struct coroipcs_zc_header *)(((char *)req_lib_cpg_mcast) - sizeof (struct coroipcs_zc_header));

	req_coroipcc_zc_execute.header.size = sizeof (mar_req_coroipcc_zc_execute_t);
	req_coroipcc_zc_execute.header.id = MESSAGE_REQ_CPG_ZC_EXECUTE;
	req_coroipcc_zc_execute.server_address = hdr->server_address;

	iovec.iov_base = (void *)&req_coroipcc_zc_execute;
	iovec.iov_len = sizeof (mar_req_coroipcc_zc_execute_t);

	error = coroipcc_msg_send_reply_receive (
		cpg_inst->c,
		&iovec,
		1,
		&res_lib_cpg_mcast,
		sizeof(res_lib_cpg_mcast));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cpg_mcast.header.error;

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

static cs_error_t send_fragments (
	struct cpg_inst *cpg_inst,
	cpg_guarantee_t guarantee,
	size_t msg_len,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	int i;
	cs_error_t error = CS_OK;
	struct iovec iov[2];
	struct req_lib_cpg_partial_mcast req_lib_cpg_mcast;
	struct res_lib_cpg_partial_send res_lib_cpg_partial_send;
	size_t sent = 0;
	size_t iov_sent = 0;
	int retry_count;

	req_lib_cpg_mcast.header.id = MESSAGE_REQ_CPG_PARTIAL_MCAST;
	req_lib_cpg_mcast.guarantee = guarantee;
	req_lib_cpg_mcast.msglen = msg_len;

	iov[0].iov_base = (void *)&req_lib_cpg_mcast;
	iov[0].iov_len = sizeof (struct req_lib_cpg_partial_mcast);

	i=0;
	iov_sent = 0 ;
	qb_ipcc_fc_enable_max_set(cpg_inst->c,  2);

	while (error == CS_OK && sent < msg_len) {

		retry_count = 0;
		if ( (iovec[i].iov_len - iov_sent) > cpg_inst->max_msg_size) {
			iov[1].iov_len = cpg_inst->max_msg_size;
		}
		else {
			iov[1].iov_len = iovec[i].iov_len - iov_sent;
		}

		if (sent == 0) {
			req_lib_cpg_mcast.type = LIBCPG_PARTIAL_FIRST;
		}
		else if ((sent + iov[1].iov_len) == msg_len) {
			req_lib_cpg_mcast.type = LIBCPG_PARTIAL_LAST;
		}
		else {
			req_lib_cpg_mcast.type = LIBCPG_PARTIAL_CONTINUED;
		}

		req_lib_cpg_mcast.fraglen = iov[1].iov_len;
		req_lib_cpg_mcast.header.size = sizeof (struct req_lib_cpg_partial_mcast) + iov[1].iov_len;
		iov[1].iov_base = (char *)iovec[i].iov_base + iov_sent;

	resend:
		error = coroipcc_msg_send_reply_receive (cpg_inst->c, iov, 2,
							 &res_lib_cpg_partial_send,
							 sizeof (res_lib_cpg_partial_send));

		if (error == CS_ERR_TRY_AGAIN) {
			fprintf(stderr, "sleep. counter=%d\n", retry_count);
			if (++retry_count > MAX_RETRIES) {
				goto error_exit;
			}
			usleep(10000);
			goto resend;
		}

		iov_sent += iov[1].iov_len;
		sent += iov[1].iov_len;

		/* Next iovec */
		if (iov_sent >= iovec[i].iov_len) {
			i++;
			iov_sent = 0;
		}
		error = res_lib_cpg_partial_send.header.error;
	}
error_exit:
	qb_ipcc_fc_enable_max_set(cpg_inst->c,  1);

	return error;
}


cs_error_t cpg_mcast_joined (
	cpg_handle_t handle,
	cpg_guarantee_t guarantee,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	int i;
	cs_error_t error;
	struct cpg_inst *cpg_inst;
	struct iovec iov[64];
	struct req_lib_cpg_mcast req_lib_cpg_mcast;
	size_t msg_len = 0;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	for (i = 0; i < iov_len; i++ ) {
		msg_len += iovec[i].iov_len;
	}

	if (msg_len > cpg_inst->max_msg_size) {
		error = send_fragments(cpg_inst, guarantee, msg_len, iovec, iov_len);
		goto error_exit;
	}

	req_lib_cpg_mcast.header.size = sizeof (struct req_lib_cpg_mcast) +
		msg_len;

	req_lib_cpg_mcast.header.id = MESSAGE_REQ_CPG_MCAST;
	req_lib_cpg_mcast.guarantee = guarantee;
	req_lib_cpg_mcast.msglen = msg_len;

	iov[0].iov_base = (void *)&req_lib_cpg_mcast;
	iov[0].iov_len = sizeof (struct req_lib_cpg_mcast);
	memcpy (&iov[1], iovec, iov_len * sizeof (struct iovec));

	qb_ipcc_fc_enable_max_set(cpg_inst->c,  2);
	error = qb_to_cs_error(qb_ipcc_sendv(cpg_inst->c, iov, iov_len + 1));
	qb_ipcc_fc_enable_max_set(cpg_inst->c,  1);

error_exit:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_iteration_initialize(
	cpg_handle_t handle,
	cpg_iteration_type_t iteration_type,
	const struct cpg_name *group,
	cpg_iteration_handle_t *cpg_iteration_handle)
{
	cs_error_t error;
	struct iovec iov;
	struct cpg_inst *cpg_inst;
	struct cpg_iteration_instance_t *cpg_iteration_instance;
	struct req_lib_cpg_iterationinitialize req_lib_cpg_iterationinitialize;
	struct res_lib_cpg_iterationinitialize res_lib_cpg_iterationinitialize;

	if (group && group->length > CPG_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
	}
	if (cpg_iteration_handle == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	if ((iteration_type == CPG_ITERATION_ONE_GROUP && group == NULL) ||
		(iteration_type != CPG_ITERATION_ONE_GROUP && group != NULL)) {
		return (CS_ERR_INVALID_PARAM);
	}

	if (iteration_type != CPG_ITERATION_NAME_ONLY && iteration_type != CPG_ITERATION_ONE_GROUP &&
	    iteration_type != CPG_ITERATION_ALL) {

		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs (hdb_handle_get (&cpg_handle_t_db, handle, (void *)&cpg_inst));
	if (error != CS_OK) {
		return (error);
	}

	error = hdb_error_to_cs (hdb_handle_create (&cpg_iteration_handle_t_db,
		sizeof (struct cpg_iteration_instance_t), cpg_iteration_handle));
	if (error != CS_OK) {
		goto error_put_cpg_db;
	}

	error = hdb_error_to_cs (hdb_handle_get (&cpg_iteration_handle_t_db, *cpg_iteration_handle,
		(void *)&cpg_iteration_instance));
	if (error != CS_OK) {
		goto error_destroy;
	}

	cpg_iteration_instance->conn = cpg_inst->c;

	list_init (&cpg_iteration_instance->list);

	req_lib_cpg_iterationinitialize.header.size = sizeof (struct req_lib_cpg_iterationinitialize);
	req_lib_cpg_iterationinitialize.header.id = MESSAGE_REQ_CPG_ITERATIONINITIALIZE;
	req_lib_cpg_iterationinitialize.iteration_type = iteration_type;
	if (group) {
		marshall_to_mar_cpg_name_t (&req_lib_cpg_iterationinitialize.group_name, group);
	}

	iov.iov_base = (void *)&req_lib_cpg_iterationinitialize;
	iov.iov_len = sizeof (struct req_lib_cpg_iterationinitialize);

	error = coroipcc_msg_send_reply_receive (cpg_inst->c,
		&iov,
		1,
		&res_lib_cpg_iterationinitialize,
		sizeof (struct res_lib_cpg_iterationinitialize));

	if (error != CS_OK) {
		goto error_put_destroy;
	}

	cpg_iteration_instance->executive_iteration_handle =
		res_lib_cpg_iterationinitialize.iteration_handle;
	cpg_iteration_instance->cpg_iteration_handle = *cpg_iteration_handle;

	list_add (&cpg_iteration_instance->list, &cpg_inst->iteration_list_head);

	hdb_handle_put (&cpg_iteration_handle_t_db, *cpg_iteration_handle);
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (res_lib_cpg_iterationinitialize.header.error);

error_put_destroy:
	hdb_handle_put (&cpg_iteration_handle_t_db, *cpg_iteration_handle);
error_destroy:
	hdb_handle_destroy (&cpg_iteration_handle_t_db, *cpg_iteration_handle);
error_put_cpg_db:
	hdb_handle_put (&cpg_handle_t_db, handle);

	return (error);
}

cs_error_t cpg_iteration_next(
	cpg_iteration_handle_t handle,
	struct cpg_iteration_description_t *description)
{
	cs_error_t error;
	struct cpg_iteration_instance_t *cpg_iteration_instance;
	struct req_lib_cpg_iterationnext req_lib_cpg_iterationnext;
	struct res_lib_cpg_iterationnext res_lib_cpg_iterationnext;

	if (description == NULL) {
		return CS_ERR_INVALID_PARAM;
	}

	error = hdb_error_to_cs (hdb_handle_get (&cpg_iteration_handle_t_db, handle,
		(void *)&cpg_iteration_instance));
	if (error != CS_OK) {
		goto error_exit;
	}

	req_lib_cpg_iterationnext.header.size = sizeof (struct req_lib_cpg_iterationnext);
	req_lib_cpg_iterationnext.header.id = MESSAGE_REQ_CPG_ITERATIONNEXT;
	req_lib_cpg_iterationnext.iteration_handle = cpg_iteration_instance->executive_iteration_handle;

	error = qb_to_cs_error (qb_ipcc_send (cpg_iteration_instance->conn,
				&req_lib_cpg_iterationnext,
				req_lib_cpg_iterationnext.header.size));
	if (error != CS_OK) {
		goto error_put;
	}

	error = qb_to_cs_error (qb_ipcc_recv (cpg_iteration_instance->conn,
				&res_lib_cpg_iterationnext,
				sizeof(struct res_lib_cpg_iterationnext), -1));
	if (error != CS_OK) {
		goto error_put;
	}

	marshall_from_mar_cpg_iteration_description_t(
			description,
			&res_lib_cpg_iterationnext.description);

	error = res_lib_cpg_iterationnext.header.error;

error_put:
	hdb_handle_put (&cpg_iteration_handle_t_db, handle);

error_exit:
	return (error);
}

cs_error_t cpg_iteration_finalize (
	cpg_iteration_handle_t handle)
{
	cs_error_t error;
	struct iovec iov;
	struct cpg_iteration_instance_t *cpg_iteration_instance;
	struct req_lib_cpg_iterationfinalize req_lib_cpg_iterationfinalize;
	struct res_lib_cpg_iterationfinalize res_lib_cpg_iterationfinalize;

	error = hdb_error_to_cs (hdb_handle_get (&cpg_iteration_handle_t_db, handle,
		(void *)&cpg_iteration_instance));
	if (error != CS_OK) {
		goto error_exit;
	}

	req_lib_cpg_iterationfinalize.header.size = sizeof (struct req_lib_cpg_iterationfinalize);
	req_lib_cpg_iterationfinalize.header.id = MESSAGE_REQ_CPG_ITERATIONFINALIZE;
	req_lib_cpg_iterationfinalize.iteration_handle = cpg_iteration_instance->executive_iteration_handle;

	iov.iov_base = (void *)&req_lib_cpg_iterationfinalize;
	iov.iov_len = sizeof (struct req_lib_cpg_iterationfinalize);

	error = coroipcc_msg_send_reply_receive (cpg_iteration_instance->conn,
		&iov,
		1,
		&res_lib_cpg_iterationfinalize,
		sizeof (struct req_lib_cpg_iterationfinalize));

	if (error != CS_OK) {
		goto error_put;
	}

	cpg_iteration_instance_finalize (cpg_iteration_instance);
	hdb_handle_put (&cpg_iteration_handle_t_db, cpg_iteration_instance->cpg_iteration_handle);

	return (res_lib_cpg_iterationfinalize.header.error);

error_put:
	hdb_handle_put (&cpg_iteration_handle_t_db, handle);
error_exit:
	return (error);
}

/** @} */
