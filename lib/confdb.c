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
 * Provides access to data in the corosync object database
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>
#include <corosync/list.h>

#include <corosync/confdb.h>
#include <corosync/ipc_confdb.h>

#include "util.h"

#include "sa-confdb.h"

#undef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))

/* Hold the information for iterators so that
   callers can do recursive tree traversals.
   each object_handle can have its own iterator */
struct iter_context {
	struct list_head list;
	hdb_handle_t parent_object_handle;
	hdb_handle_t find_handle;
	hdb_handle_t next_entry;
};

struct confdb_inst {
	hdb_handle_t handle;
	int finalize;
	int standalone;
	confdb_callbacks_t callbacks;
	const void *context;

	struct list_head object_find_head;
	struct list_head object_iter_head;
	struct list_head key_iter_head;
};

DECLARE_HDB_DATABASE(confdb_handle_t_db,NULL);

static cs_error_t do_find_destroy(struct confdb_inst *confdb_inst, hdb_handle_t find_handle);


/* Safely tidy one iterator context list */
static void free_context_list(struct confdb_inst *confdb_inst, struct list_head *list)
{
	struct iter_context *context;
	struct list_head *iter, *tmp;

	for (iter = list->next, tmp = iter->next;
	     iter != list; iter = tmp, tmp = iter->next) {

		context = list_entry (iter, struct iter_context, list);
		(void)do_find_destroy(confdb_inst, context->find_handle);
		free(context);
	}
}

static struct iter_context *find_iter_context(struct list_head *list, hdb_handle_t object_handle)
{
	struct iter_context *context;
	struct list_head *iter;

	for (iter = list->next;
	     iter != list; iter = iter->next) {

		context = list_entry (iter, struct iter_context, list);
		if (context->parent_object_handle == object_handle)
			return context;
	}
	return NULL;
}

/**
 * @defgroup confdb_corosync
 * @ingroup corosync
 *
 * @{
 */

cs_error_t confdb_initialize (
	confdb_handle_t *handle,
	confdb_callbacks_t *callbacks)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;

	error = hdb_error_to_cs(hdb_handle_create (&confdb_handle_t_db, sizeof (struct confdb_inst), handle));
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, *handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		goto error_destroy;
	}

	if (getenv("COROSYNC_DEFAULT_CONFIG_IFACE")) {
		error = confdb_sa_init();
		confdb_inst->standalone = 1;
	}
	else {
		error = coroipcc_service_connect (
			COROSYNC_SOCKET_NAME,
			CONFDB_SERVICE,
			IPC_REQUEST_SIZE,
			IPC_RESPONSE_SIZE,
			IPC_DISPATCH_SIZE,
			&confdb_inst->handle);
	}
	if (error != CS_OK)
		goto error_put_destroy;

	if (callbacks) {
		memcpy (&confdb_inst->callbacks, callbacks, sizeof (confdb_callbacks_t));
	}

	list_init (&confdb_inst->object_find_head);
	list_init (&confdb_inst->object_iter_head);
	list_init (&confdb_inst->key_iter_head);

	(void)hdb_handle_put (&confdb_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	(void)hdb_handle_put (&confdb_handle_t_db, *handle);
error_destroy:
	(void)hdb_handle_destroy (&confdb_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

cs_error_t confdb_finalize (
	confdb_handle_t handle)
{
	struct confdb_inst *confdb_inst;
	cs_error_t error;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (confdb_inst->finalize) {
		(void)hdb_handle_put (&confdb_handle_t_db, handle);
		return (CS_ERR_BAD_HANDLE);
	}

	confdb_inst->finalize = 1;

	/* Free saved context handles */
	free_context_list(confdb_inst, &confdb_inst->object_find_head);
	free_context_list(confdb_inst, &confdb_inst->object_iter_head);
	free_context_list(confdb_inst, &confdb_inst->key_iter_head);

	if (!confdb_inst->standalone) {
		coroipcc_service_disconnect (confdb_inst->handle);
	}

	(void)hdb_handle_destroy (&confdb_handle_t_db, handle);

	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t confdb_fd_get (
	confdb_handle_t handle,
	int *fd)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	error = coroipcc_fd_get (confdb_inst->handle, fd);

	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_context_get (
	confdb_handle_t handle,
	const void **context)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	*context = confdb_inst->context;

	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t confdb_context_set (
	confdb_handle_t handle,
	const void *context)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	confdb_inst->context = context;

	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t confdb_dispatch (
	confdb_handle_t handle,
	cs_dispatch_flags_t dispatch_types)
{
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct confdb_inst *confdb_inst;
	confdb_callbacks_t callbacks;
	struct res_lib_confdb_key_change_callback *res_key_changed_pt;
	struct res_lib_confdb_object_create_callback *res_object_created_pt;
	struct res_lib_confdb_object_destroy_callback *res_object_destroyed_pt;
	struct res_lib_confdb_reload_callback *res_reload_pt;
	coroipc_response_header_t *dispatch_data;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_ERR_NOT_SUPPORTED;
		goto error_put;
	}

	/*
	 * Timeout instantly for CS_DISPATCH_ONE or CS_DISPATCH_ALL and
	 * wait indefinitely for CS_DISPATCH_BLOCKING
	 */
	if (dispatch_types == CONFDB_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		error = coroipcc_dispatch_get (
			confdb_inst->handle,
			(void **)&dispatch_data,
			timeout);
		if (error == CS_ERR_BAD_HANDLE) {
			error = CS_OK;
			goto error_put;
		}
		if (error == CS_ERR_TRY_AGAIN) {
			error = CS_OK;
			if (dispatch_types == CONFDB_DISPATCH_ALL) {
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
		 * operate at the same time that confdbFinalize has been called.
		*/
		memcpy (&callbacks, &confdb_inst->callbacks, sizeof (confdb_callbacks_t));


		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data->id) {
			case MESSAGE_RES_CONFDB_KEY_CHANGE_CALLBACK:
				if (callbacks.confdb_key_change_notify_fn == NULL) {
					break;
				}

				res_key_changed_pt = (struct res_lib_confdb_key_change_callback *)dispatch_data;

				callbacks.confdb_key_change_notify_fn(handle,
					res_key_changed_pt->change_type,
					res_key_changed_pt->object_handle,
					res_key_changed_pt->parent_object_handle,
					res_key_changed_pt->object_name.value,
					res_key_changed_pt->object_name.length,
					res_key_changed_pt->key_name.value,
					res_key_changed_pt->key_name.length,
					res_key_changed_pt->key_value.value,
					res_key_changed_pt->key_value.length);
				break;

		        case MESSAGE_RES_CONFDB_KEY_CHANGE_CALLBACK2:
				if (callbacks.confdb_key_change_notify_fn == NULL) {
					break;
				}

				res_key_changed_pt = (struct res_lib_confdb_key_change_callback *)dispatch_data;

				callbacks.confdb_key_change_notify_fn(handle,
					res_key_changed_pt->change_type,
					res_key_changed_pt->object_handle,
					res_key_changed_pt->parent_object_handle,
					res_key_changed_pt->object_name.value,
					res_key_changed_pt->object_name.length,
					res_key_changed_pt->key_name.value,
					res_key_changed_pt->key_name.length,
					&res_key_changed_pt->key_value.value,
					res_key_changed_pt->key_value.length);
				break;

			case MESSAGE_RES_CONFDB_OBJECT_CREATE_CALLBACK:
				if (callbacks.confdb_object_create_change_notify_fn == NULL) {
					break;
				}

				res_object_created_pt = (struct res_lib_confdb_object_create_callback *)dispatch_data;

				callbacks.confdb_object_create_change_notify_fn(handle,
					res_object_created_pt->object_handle,
					res_object_created_pt->parent_object_handle,
					res_object_created_pt->name.value,
					res_object_created_pt->name.length);
				break;

			case MESSAGE_RES_CONFDB_OBJECT_DESTROY_CALLBACK:
				if (callbacks.confdb_object_delete_change_notify_fn == NULL) {
					break;
				}

				res_object_destroyed_pt = (struct res_lib_confdb_object_destroy_callback *)dispatch_data;

				callbacks.confdb_object_delete_change_notify_fn(handle,
					res_object_destroyed_pt->parent_object_handle,
					res_object_destroyed_pt->name.value,
					res_object_destroyed_pt->name.length);
				break;

		        case MESSAGE_RES_CONFDB_RELOAD_CALLBACK:
				if (callbacks.confdb_reload_notify_fn == NULL) {
					continue;
				}

				res_reload_pt = (struct res_lib_confdb_reload_callback *)dispatch_data;

				callbacks.confdb_reload_notify_fn(handle,
					res_reload_pt->type);
				break;

			default:
				error = coroipcc_dispatch_put (confdb_inst->handle);
				if (error == CS_OK) {
					error = CS_ERR_LIBRARY;
				}
				goto error_put;
				break;
		}
		error = coroipcc_dispatch_put (confdb_inst->handle);
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
	(void)hdb_handle_put (&confdb_handle_t_db, handle);
	return (error);
}

cs_error_t confdb_object_create (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *object_name,
	size_t object_name_len,
	hdb_handle_t *object_handle)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_object_create req_lib_confdb_object_create;
	struct res_lib_confdb_object_create res_lib_confdb_object_create;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_object_create(parent_object_handle,
					    object_name, object_name_len,
					    object_handle))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_create.header.size = sizeof (struct req_lib_confdb_object_create);
	req_lib_confdb_object_create.header.id = MESSAGE_REQ_CONFDB_OBJECT_CREATE;
	req_lib_confdb_object_create.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_object_create.object_name.value, object_name, object_name_len);
	req_lib_confdb_object_create.object_name.length = object_name_len;

	iov.iov_base = (char *)&req_lib_confdb_object_create;
	iov.iov_len = sizeof (struct req_lib_confdb_object_create);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_object_create,
		sizeof (struct res_lib_confdb_object_create));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_create.header.error;
	*object_handle = res_lib_confdb_object_create.object_handle;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_object_destroy (
	confdb_handle_t handle,
	hdb_handle_t object_handle)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_object_destroy req_lib_confdb_object_destroy;
	coroipc_response_header_t res;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_object_destroy(object_handle))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_destroy.header.size = sizeof (struct req_lib_confdb_object_destroy);
	req_lib_confdb_object_destroy.header.id = MESSAGE_REQ_CONFDB_OBJECT_DESTROY;
	req_lib_confdb_object_destroy.object_handle = object_handle;

	iov.iov_base = (char *)&req_lib_confdb_object_destroy;
	iov.iov_len = sizeof (struct req_lib_confdb_object_destroy);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res,
		sizeof (coroipc_response_header_t));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_object_parent_get (
	confdb_handle_t handle,
	hdb_handle_t object_handle,
	hdb_handle_t *parent_object_handle)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_object_parent_get req_lib_confdb_object_parent_get;
	struct res_lib_confdb_object_parent_get res_lib_confdb_object_parent_get;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_object_parent_get(object_handle, parent_object_handle))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_parent_get.header.size = sizeof (struct req_lib_confdb_object_parent_get);
	req_lib_confdb_object_parent_get.header.id = MESSAGE_REQ_CONFDB_OBJECT_PARENT_GET;
	req_lib_confdb_object_parent_get.object_handle = object_handle;

	iov.iov_base = (char *)&req_lib_confdb_object_parent_get;
	iov.iov_len = sizeof (struct req_lib_confdb_object_parent_get);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_object_parent_get,
		sizeof (struct res_lib_confdb_object_parent_get));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_parent_get.header.error;
	*parent_object_handle = res_lib_confdb_object_parent_get.parent_object_handle;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_object_name_get (
	confdb_handle_t handle,
	hdb_handle_t object_handle,
	char *object_name,
	size_t *object_name_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_object_name_get request;
	struct res_lib_confdb_object_name_get response;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_object_name_get(object_handle, object_name, object_name_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	request.header.size = sizeof (struct req_lib_confdb_object_name_get);
	request.header.id = MESSAGE_REQ_CONFDB_OBJECT_NAME_GET;
	request.object_handle = object_handle;

	iov.iov_base = (char *)&request;
	iov.iov_len = sizeof (struct req_lib_confdb_object_name_get);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &response,
		sizeof (struct res_lib_confdb_object_name_get));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = response.header.error;
	if (error == CS_OK) {
		*object_name_len = response.object_name.length;
		memcpy(object_name, response.object_name.value, *object_name_len);
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

static cs_error_t do_find_destroy(
	struct confdb_inst *confdb_inst,
	hdb_handle_t find_handle)
{
	cs_error_t error;
	struct iovec iov;
	struct req_lib_confdb_object_find_destroy req_lib_confdb_object_find_destroy;
	coroipc_response_header_t res;

	if (!find_handle)
		return CS_OK;

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_find_destroy(find_handle))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_find_destroy.header.size = sizeof (struct req_lib_confdb_object_find_destroy);
	req_lib_confdb_object_find_destroy.header.id = MESSAGE_REQ_CONFDB_OBJECT_FIND_DESTROY;
	req_lib_confdb_object_find_destroy.find_handle = find_handle;

	iov.iov_base = (char *)&req_lib_confdb_object_find_destroy;
	iov.iov_len = sizeof (struct req_lib_confdb_object_find_destroy);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res,
		sizeof (coroipc_response_header_t));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:

	return (error);
}

cs_error_t confdb_object_find_destroy(
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle)
{
	struct iter_context *context;
	cs_error_t error;
	struct confdb_inst *confdb_inst;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->object_find_head, parent_object_handle);
	if (context == NULL) {
		error = CS_ERR_LIBRARY;
		goto error_exit;
	}
	error = do_find_destroy(confdb_inst, context->find_handle);
	if (error == CS_OK) {
		list_del(&context->list);
		free(context);
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);
	return error;
}

cs_error_t confdb_object_iter_destroy(
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle)
{
	struct iter_context *context;
	cs_error_t error;
	struct confdb_inst *confdb_inst;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->object_iter_head, parent_object_handle);
	if (context == NULL) {
		error = CS_ERR_LIBRARY;
		goto error_exit;
	}
	error = do_find_destroy(confdb_inst, context->find_handle);
	if (error == CS_OK) {
		list_del(&context->list);
		free(context);
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);
	return error;
}


cs_error_t confdb_key_create (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	const void *value,
	size_t value_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_create req_lib_confdb_key_create;
	coroipc_response_header_t res;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_create(parent_object_handle,
					 key_name, key_name_len,
					 value, value_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_create.header.size = sizeof (struct req_lib_confdb_key_create);
	req_lib_confdb_key_create.header.id = MESSAGE_REQ_CONFDB_KEY_CREATE;
	req_lib_confdb_key_create.object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_create.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_create.key_name.length = key_name_len;
	memcpy(req_lib_confdb_key_create.value.value, value, value_len);
	req_lib_confdb_key_create.value.length = value_len;

	iov.iov_base = (char *)&req_lib_confdb_key_create;
	iov.iov_len = sizeof (struct req_lib_confdb_key_create);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res,
		sizeof (res));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}


cs_error_t confdb_key_create_typed (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const char *key_name,
	const void *value,
	size_t value_len,
	confdb_value_types_t type)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_create_typed2 *request;
	coroipc_response_header_t res;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_create_typed(parent_object_handle,
					 key_name, value, value_len, type))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	request = malloc(sizeof(struct req_lib_confdb_key_create_typed2)+value_len);
	if (!request) {
		error = CS_ERR_NO_MEMORY;
		goto error_exit;
	}

	request->header.size = sizeof (struct req_lib_confdb_key_create_typed2) + value_len;
	request->header.id = MESSAGE_REQ_CONFDB_KEY_CREATE_TYPED2;
	request->object_handle = parent_object_handle;
	request->key_name.length = strlen(key_name)+1;
	memcpy(request->key_name.value, key_name, request->key_name.length);
	memcpy(&request->value, value, value_len);
	request->value_length = value_len;
	request->type = type;

	iov.iov_base = (char *)request;
	iov.iov_len = request->header.size;

	error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
		&res,
		sizeof (res));

	if (error != CS_OK) {
		goto free_exit;
	}

	error = res.error;

free_exit:
	free(request);

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}



cs_error_t confdb_key_delete (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	const void *value,
	size_t value_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_delete req_lib_confdb_key_delete;
	coroipc_response_header_t res;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_delete(parent_object_handle,
					 key_name, key_name_len,
					 value, value_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_delete.header.size = sizeof (struct req_lib_confdb_key_delete);
	req_lib_confdb_key_delete.header.id = MESSAGE_REQ_CONFDB_KEY_DELETE;
	req_lib_confdb_key_delete.object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_delete.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_delete.key_name.length = key_name_len;
	memcpy(req_lib_confdb_key_delete.value.value, value, value_len);
	req_lib_confdb_key_delete.value.length = value_len;

	iov.iov_base = (char *)&req_lib_confdb_key_delete;
	iov.iov_len = sizeof (struct req_lib_confdb_key_delete);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res,
		sizeof (res));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_key_get (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	void *value,
	size_t *value_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_get req_lib_confdb_key_get;
	struct res_lib_confdb_key_get res_lib_confdb_key_get;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_get(parent_object_handle,
				      key_name, key_name_len,
				      value, value_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_get.header.size = sizeof (struct req_lib_confdb_key_get);
	req_lib_confdb_key_get.header.id = MESSAGE_REQ_CONFDB_KEY_GET;
	req_lib_confdb_key_get.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_get.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_get.key_name.length = key_name_len;

	iov.iov_base = (char *)&req_lib_confdb_key_get;
	iov.iov_len = sizeof (struct req_lib_confdb_key_get);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_key_get,
		sizeof (struct res_lib_confdb_key_get));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_key_get.header.error;
	if (error == CS_OK) {
		*value_len = res_lib_confdb_key_get.value.length;
		memcpy(value, res_lib_confdb_key_get.value.value, *value_len);
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}


cs_error_t confdb_key_get_typed (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const char *key_name,
	void *value,
	size_t *value_len,
	confdb_value_types_t *type)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_get req_lib_confdb_key_get;
	struct res_lib_confdb_key_get_typed response;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_get_typed(parent_object_handle,
				      key_name, &value, value_len, (int*)type))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_get.header.size = sizeof (struct req_lib_confdb_key_get);
	req_lib_confdb_key_get.header.id = MESSAGE_REQ_CONFDB_KEY_GET_TYPED;
	req_lib_confdb_key_get.parent_object_handle = parent_object_handle;
	req_lib_confdb_key_get.key_name.length = strlen(key_name) + 1;
	memcpy(req_lib_confdb_key_get.key_name.value, key_name, req_lib_confdb_key_get.key_name.length);

	iov.iov_base = (char *)&req_lib_confdb_key_get;
	iov.iov_len = sizeof (struct req_lib_confdb_key_get);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
		&response,
		sizeof (struct res_lib_confdb_key_get_typed));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = response.header.error;
	if (error == CS_OK) {
		*value_len = response.value.length;
		*type = response.type;
		memcpy(value, response.value.value, *value_len);
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_key_get_typed2 (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const char *key_name,
	void **value,
	size_t *value_len,
	confdb_value_types_t *type)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_get req_lib_confdb_key_get;
	struct res_lib_confdb_key_get_typed2 *response;
	void *return_address;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_get_typed(parent_object_handle,
				      key_name, value, value_len, (int*)type))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_get.header.size = sizeof (struct req_lib_confdb_key_get);
	req_lib_confdb_key_get.header.id = MESSAGE_REQ_CONFDB_KEY_GET_TYPED2;
	req_lib_confdb_key_get.parent_object_handle = parent_object_handle;
	req_lib_confdb_key_get.key_name.length = strlen(key_name) + 1;
	memcpy(req_lib_confdb_key_get.key_name.value, key_name, req_lib_confdb_key_get.key_name.length);

	iov.iov_base = (char *)&req_lib_confdb_key_get;
	iov.iov_len = sizeof (struct req_lib_confdb_key_get);

	error = coroipcc_msg_send_reply_receive_in_buf_get (
		confdb_inst->handle,
		&iov,
		1,
		&return_address);
	response = return_address;

	if (error != CS_OK) {
		goto error_exit;
	}
	error = response->header.error;

	if (error == CS_OK) {
		if (!*value) {
			/* Allow space for naughty callers to put a NUL for printing */
			*value = malloc(response->value_length+1);
			if (!*value) {
				error = CS_ERR_NO_MEMORY;
				goto error_exit;
			}
		}
		memcpy(*value, &response->value, response->value_length);
		*value_len = response->value_length;
		*type = response->type;
	}
	coroipcc_msg_send_reply_receive_in_buf_put(confdb_inst->handle);

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);

}


cs_error_t confdb_key_increment (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	unsigned int *value)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_get req_lib_confdb_key_get;
	struct res_lib_confdb_key_incdec res_lib_confdb_key_incdec;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_increment(parent_object_handle,
					    key_name, key_name_len,
					    value))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_get.header.size = sizeof (struct req_lib_confdb_key_get);
	req_lib_confdb_key_get.header.id = MESSAGE_REQ_CONFDB_KEY_INCREMENT;
	req_lib_confdb_key_get.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_get.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_get.key_name.length = key_name_len;

	iov.iov_base = (char *)&req_lib_confdb_key_get;
	iov.iov_len = sizeof (struct req_lib_confdb_key_get);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_key_incdec,
		sizeof (struct res_lib_confdb_key_incdec));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_key_incdec.header.error;
	if (error == CS_OK) {
		*value = res_lib_confdb_key_incdec.value;
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_key_decrement (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	unsigned int *value)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_get req_lib_confdb_key_get;
	struct res_lib_confdb_key_incdec res_lib_confdb_key_incdec;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_decrement(parent_object_handle,
					    key_name, key_name_len,
					    value))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_get.header.size = sizeof (struct req_lib_confdb_key_get);
	req_lib_confdb_key_get.header.id = MESSAGE_REQ_CONFDB_KEY_DECREMENT;
	req_lib_confdb_key_get.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_get.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_get.key_name.length = key_name_len;

	iov.iov_base = (char *)&req_lib_confdb_key_get;
	iov.iov_len = sizeof (struct req_lib_confdb_key_get);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_key_incdec,
		sizeof (struct res_lib_confdb_key_incdec));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_key_incdec.header.error;
	if (error == CS_OK) {
		*value = res_lib_confdb_key_incdec.value;
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_key_replace (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	const void *old_value,
	size_t old_value_len,
	const void *new_value,
	size_t new_value_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_key_replace2 *req_lib_confdb_key_replace;
	coroipc_response_header_t res;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_replace(parent_object_handle,
					  key_name, key_name_len,
					  old_value, old_value_len,
					  new_value, new_value_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_replace = malloc(sizeof(struct req_lib_confdb_key_replace2) + new_value_len);

	req_lib_confdb_key_replace->header.size = sizeof(struct req_lib_confdb_key_replace2) + new_value_len;
	req_lib_confdb_key_replace->header.id = MESSAGE_REQ_CONFDB_KEY_REPLACE2;
	req_lib_confdb_key_replace->object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_replace->key_name.value, key_name, key_name_len);
	req_lib_confdb_key_replace->key_name.length = key_name_len;
	memcpy(&req_lib_confdb_key_replace->new_value, new_value, new_value_len);
	req_lib_confdb_key_replace->new_value_length = new_value_len;
	/* Oddly objdb doesn't use the old value, so we don't bother sending it */
	iov.iov_base = (char *)req_lib_confdb_key_replace;
	iov.iov_len = sizeof(struct req_lib_confdb_key_replace2) +  new_value_len;

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res,
		sizeof (res));

	if (error != CS_OK) {
		goto free_exit;
	}

	error = res.error;
free_exit:
	free(req_lib_confdb_key_replace);

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_object_iter_start (
	confdb_handle_t handle,
	hdb_handle_t object_handle)
{
	struct confdb_inst *confdb_inst;
	cs_error_t error = CS_OK;
	struct iter_context *context;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->object_iter_head, object_handle);
	if (!context) {
		context = malloc(sizeof(struct iter_context));
		if (!context) {
			error = CS_ERR_NO_MEMORY;
			goto ret;
		}
		context->parent_object_handle = object_handle;
		context->find_handle = 0;
		list_add(&context->list, &confdb_inst->object_iter_head);
	}

	/* Start a new find context */
	if (context->find_handle) {
		(void)do_find_destroy(confdb_inst, context->find_handle);
		context->find_handle = 0;
	}

	(void)hdb_handle_put (&confdb_handle_t_db, handle);

ret:
	return error;
}

cs_error_t confdb_key_iter_start (
	confdb_handle_t handle,
	hdb_handle_t object_handle)
{
	struct confdb_inst *confdb_inst;
	cs_error_t error = CS_OK;
	struct iter_context *context;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->key_iter_head, object_handle);
	if (!context) {
		context = malloc(sizeof(struct iter_context));
		if (!context) {
			error = CS_ERR_NO_MEMORY;
			goto ret;
		}
		context->parent_object_handle = object_handle;
		list_add(&context->list, &confdb_inst->key_iter_head);
	}

	context->find_handle = 0;
	context->next_entry = 0;

	(void)hdb_handle_put (&confdb_handle_t_db, handle);

ret:
	return error;
}

cs_error_t confdb_object_find_start (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle)
{
	struct confdb_inst *confdb_inst;
	cs_error_t error = CS_OK;
	struct iter_context *context;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->object_find_head, parent_object_handle);
	if (!context) {
		context = malloc(sizeof(struct iter_context));
		if (!context) {
			error = CS_ERR_NO_MEMORY;
			goto ret;
		}
		context->find_handle = 0;
		context->parent_object_handle = parent_object_handle;
		list_add(&context->list, &confdb_inst->object_find_head);
	}
	/* Start a new find context */
	if (context->find_handle) {
		(void)do_find_destroy(confdb_inst, context->find_handle);
		context->find_handle = 0;
	}

	(void)hdb_handle_put (&confdb_handle_t_db, handle);

ret:
	return error;
}

cs_error_t confdb_object_find (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *object_name,
	size_t object_name_len,
	hdb_handle_t *object_handle)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct iter_context *context;
	struct req_lib_confdb_object_find req_lib_confdb_object_find;
	struct res_lib_confdb_object_find res_lib_confdb_object_find;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	/* You MUST call confdb_object_find_start first */
	context = find_iter_context(&confdb_inst->object_find_head, parent_object_handle);
	if (!context) {
		error =	CS_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_object_find(parent_object_handle,
					  &context->find_handle,
					  object_handle,
					  object_name, object_name_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_find.header.size = sizeof (struct req_lib_confdb_object_find);
	req_lib_confdb_object_find.header.id = MESSAGE_REQ_CONFDB_OBJECT_FIND;
	req_lib_confdb_object_find.parent_object_handle = parent_object_handle;
	req_lib_confdb_object_find.find_handle = context->find_handle;
	memcpy(req_lib_confdb_object_find.object_name.value, object_name, object_name_len);
	req_lib_confdb_object_find.object_name.length = object_name_len;

	iov.iov_base = (char *)&req_lib_confdb_object_find;
	iov.iov_len = sizeof (struct req_lib_confdb_object_find);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_object_find,
		sizeof (struct res_lib_confdb_object_find));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_find.header.error;
	*object_handle = res_lib_confdb_object_find.object_handle;
	context->find_handle = res_lib_confdb_object_find.find_handle;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}


cs_error_t confdb_object_iter (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	hdb_handle_t *object_handle,
	void *object_name,
	size_t *object_name_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct iter_context *context;
	struct req_lib_confdb_object_iter req_lib_confdb_object_iter;
	struct res_lib_confdb_object_iter res_lib_confdb_object_iter;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	/* You MUST call confdb_object_iter_start first */
	context = find_iter_context(&confdb_inst->object_iter_head, parent_object_handle);
	if (!context) {
		error =	CS_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		*object_name_len = 0;
		if (confdb_sa_object_iter(parent_object_handle,
					  &context->find_handle,
					  object_handle,
					  NULL, 0,
					  object_name, object_name_len))
			error = CS_ERR_ACCESS;
		goto sa_exit;
	}

	req_lib_confdb_object_iter.header.size = sizeof (struct req_lib_confdb_object_iter);
	req_lib_confdb_object_iter.header.id = MESSAGE_REQ_CONFDB_OBJECT_ITER;
	req_lib_confdb_object_iter.parent_object_handle = parent_object_handle;
	req_lib_confdb_object_iter.find_handle = context->find_handle;

	iov.iov_base = (char *)&req_lib_confdb_object_iter;
	iov.iov_len = sizeof (struct req_lib_confdb_object_iter);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_object_iter,
		sizeof (struct res_lib_confdb_object_iter));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_iter.header.error;
	if (error == CS_OK) {
		*object_name_len = res_lib_confdb_object_iter.object_name.length;
		memcpy(object_name, res_lib_confdb_object_iter.object_name.value, *object_name_len);
		*object_handle = res_lib_confdb_object_iter.object_handle;
		context->find_handle = res_lib_confdb_object_iter.find_handle;
	}
sa_exit:

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_key_iter (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	void *key_name,
	size_t *key_name_len,
	void *value,
	size_t *value_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct iter_context *context;
	struct req_lib_confdb_key_iter req_lib_confdb_key_iter;
	struct res_lib_confdb_key_iter res_lib_confdb_key_iter;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	/* You MUST call confdb_key_iter_start first */
	context = find_iter_context(&confdb_inst->key_iter_head, parent_object_handle);
	if (!context) {
		error =	CS_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_iter(parent_object_handle,
				       context->next_entry,
				       key_name, key_name_len,
				       value, value_len))
			error = CS_ERR_ACCESS;
		goto sa_exit;
	}

	req_lib_confdb_key_iter.header.size = sizeof (struct req_lib_confdb_key_iter);
	req_lib_confdb_key_iter.header.id = MESSAGE_REQ_CONFDB_KEY_ITER;
	req_lib_confdb_key_iter.parent_object_handle = parent_object_handle;
	req_lib_confdb_key_iter.next_entry= context->next_entry;

	iov.iov_base = (char *)&req_lib_confdb_key_iter;
	iov.iov_len = sizeof (struct req_lib_confdb_key_iter);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_key_iter,
		sizeof (struct res_lib_confdb_key_iter));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_key_iter.header.error;
	if (error == CS_OK) {
		char* key_name_str = (char*)key_name;
		*key_name_len = res_lib_confdb_key_iter.key_name.length;
		memcpy(key_name, res_lib_confdb_key_iter.key_name.value, *key_name_len);
		key_name_str[res_lib_confdb_key_iter.key_name.length] = '\0';
		*value_len = res_lib_confdb_key_iter.value.length;
		memcpy(value, res_lib_confdb_key_iter.value.value, *value_len);
	}

sa_exit:
	context->next_entry++;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_key_iter_typed (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	char *key_name,
	void *value,
	size_t *value_len,
	confdb_value_types_t *type)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct iter_context *context;
	struct req_lib_confdb_key_iter req_lib_confdb_key_iter;
	struct res_lib_confdb_key_iter_typed response;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	/* You MUST call confdb_key_iter_start first */
	context = find_iter_context(&confdb_inst->key_iter_head, parent_object_handle);
	if (!context) {
		error =	CS_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_key_iter_typed(parent_object_handle,
				       context->next_entry,
				       key_name,
				       &value, value_len, (int*)type))
			error = CS_ERR_ACCESS;
		goto sa_exit;
	}

	req_lib_confdb_key_iter.header.size = sizeof (struct req_lib_confdb_key_iter);
	req_lib_confdb_key_iter.header.id = MESSAGE_REQ_CONFDB_KEY_ITER_TYPED;
	req_lib_confdb_key_iter.parent_object_handle = parent_object_handle;
	req_lib_confdb_key_iter.next_entry= context->next_entry;

	iov.iov_base = (char *)&req_lib_confdb_key_iter;
	iov.iov_len = sizeof (struct req_lib_confdb_key_iter);

	error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
		&response,
		sizeof (struct res_lib_confdb_key_iter_typed));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = response.header.error;
	if (error == CS_OK) {
		memcpy(key_name, response.key_name.value, response.key_name.length);
		key_name[response.key_name.length] = '\0';
		*value_len = response.value.length;
		memcpy(value, response.value.value, *value_len);
		*type = response.type;
	}

sa_exit:
	context->next_entry++;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_key_iter_typed2 (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	char *key_name,
	void **value,
	size_t *value_len,
	confdb_value_types_t *type)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct iter_context *context;
	struct req_lib_confdb_key_iter req_lib_confdb_key_iter;
	struct res_lib_confdb_key_iter_typed2 *response;
	void *return_address;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	/* You MUST call confdb_key_iter_start first */
	context = find_iter_context(&confdb_inst->key_iter_head, parent_object_handle);
	if (!context) {
		error =	CS_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = CS_OK;
		if (confdb_sa_key_iter_typed(parent_object_handle,
				       context->next_entry,
				       key_name,
				       value, value_len, (int*)type))
			error = CS_ERR_ACCESS;
		goto sa_exit;
	}

	req_lib_confdb_key_iter.header.size = sizeof (struct req_lib_confdb_key_iter);
	req_lib_confdb_key_iter.header.id = MESSAGE_REQ_CONFDB_KEY_ITER_TYPED2;
	req_lib_confdb_key_iter.parent_object_handle = parent_object_handle;
	req_lib_confdb_key_iter.next_entry= context->next_entry;

	iov.iov_base = (char *)&req_lib_confdb_key_iter;
	iov.iov_len = sizeof (struct req_lib_confdb_key_iter);

	error = coroipcc_msg_send_reply_receive_in_buf_get (
		confdb_inst->handle,
		&iov,
		1,
		&return_address);
	response = return_address;

	if (error != CS_OK) {
		goto error_exit;
	}
	error = response->header.error;

	if (error == CS_OK) {
		if (!*value) {
			/* Allow space for naughty callers to put a NUL for printing */
			*value = malloc(response->value_length+1);
			if (!*value) {
				error = CS_ERR_NO_MEMORY;
				goto error_exit;
			}
		}
		memcpy(key_name, response->key_name.value, response->key_name.length);
		key_name[response->key_name.length] = '\0';
		memcpy(*value, &response->value, response->value_length);
		*value_len = response->value_length;
		*type = response->type;
	}
	coroipcc_msg_send_reply_receive_in_buf_put(confdb_inst->handle);

sa_exit:
	context->next_entry++;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_write (
	confdb_handle_t handle,
	char *error_text,
	size_t errbuf_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	coroipc_request_header_t req;
	struct res_lib_confdb_write res_lib_confdb_write;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		/* FIXME: set error_text */
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_write(error_text, errbuf_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req.size = sizeof (coroipc_request_header_t);
	req.id = MESSAGE_REQ_CONFDB_WRITE;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (coroipc_request_header_t);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_write,
		sizeof (struct res_lib_confdb_write));

	if (error != CS_OK) {
		/* FIXME: set error_text */
		goto error_exit;
	}

	error = res_lib_confdb_write.header.error;
	if (res_lib_confdb_write.error.length) {
		memcpy(error_text, res_lib_confdb_write.error.value,
		       MIN(res_lib_confdb_write.error.length,errbuf_len));
		error_text[errbuf_len-1] = '\0';
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_reload (
	confdb_handle_t handle,
	int flush,
	char *error_text,
	size_t errbuf_len)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct res_lib_confdb_reload res_lib_confdb_reload;
	struct req_lib_confdb_reload req_lib_confdb_reload;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		/* FIXME: set error_text */
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_OK;

		if (confdb_sa_reload(flush, error_text, errbuf_len))
			error = CS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_reload.header.size = sizeof (req_lib_confdb_reload);
	req_lib_confdb_reload.header.id = MESSAGE_REQ_CONFDB_RELOAD;
	req_lib_confdb_reload.flush = flush;

	iov.iov_base = (char *)&req_lib_confdb_reload;
	iov.iov_len = sizeof (req_lib_confdb_reload);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res_lib_confdb_reload,
		sizeof (struct res_lib_confdb_reload));

	if (error != CS_OK) {
		/* FIXME: set error_text */
		goto error_exit;
	}

	error = res_lib_confdb_reload.header.error;
	if(res_lib_confdb_reload.error.length) {
		memcpy(error_text, res_lib_confdb_reload.error.value,
		       MIN(res_lib_confdb_reload.error.length,errbuf_len));
		error_text[errbuf_len-1] = '\0';
	}

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_track_changes (
	confdb_handle_t handle,
	hdb_handle_t object_handle,
	unsigned int flags)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	struct req_lib_confdb_object_track_start req;
	coroipc_response_header_t res;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_ERR_NOT_SUPPORTED;
		goto error_exit;
	}

	req.header.size = sizeof (struct req_lib_confdb_object_track_start);
	req.header.id = MESSAGE_REQ_CONFDB_TRACK_START;
	req.object_handle = object_handle;
	req.flags = flags;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (struct req_lib_confdb_object_track_start);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res,
		sizeof (coroipc_response_header_t));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}

cs_error_t confdb_stop_track_changes (confdb_handle_t handle)
{
	cs_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov;
	coroipc_request_header_t req;
	coroipc_response_header_t res;

	error = hdb_error_to_cs(hdb_handle_get (&confdb_handle_t_db, handle, (void *)&confdb_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CS_ERR_NOT_SUPPORTED;
		goto error_exit;
	}

	req.size = sizeof (coroipc_request_header_t);
	req.id = MESSAGE_REQ_CONFDB_TRACK_STOP;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (coroipc_request_header_t);

        error = coroipcc_msg_send_reply_receive (
		confdb_inst->handle,
		&iov,
		1,
                &res,
		sizeof (coroipc_response_header_t));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	(void)hdb_handle_put (&confdb_handle_t_db, handle);

	return (error);
}
