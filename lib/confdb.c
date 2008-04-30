/*
 * Copyright (c) 2008 Red Hat, Inc.
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
 * Provides access to data in the openais object database
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>

#include <saAis.h>
#include <confdb.h>
#include <ipc_confdb.h>
#include <mar_gen.h>
#include <ais_util.h>
#include <list.h>

#include "sa-confdb.h"

/* Hold the information for iterators so that
   callers can do recursive tree traversals.
   each object_handle can have its own iterator */
struct iter_context {
	struct list_head list;
	uint32_t parent_object_handle;
	uint32_t context;
};

struct confdb_inst {
	int response_fd;
	int dispatch_fd;
	int finalize;
	int standalone;
	confdb_callbacks_t callbacks;
	void *context;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;

	struct list_head object_find_head;
	struct list_head object_iter_head;
	struct list_head key_iter_head;
};

static void confdb_instance_destructor (void *instance);

static struct saHandleDatabase confdb_handle_t_db = {
	.handleCount		        = 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= confdb_instance_destructor
};

/* Safely tidy one iterator context list */
static void free_context_list(struct list_head *list)
{
	struct iter_context *context;
	struct list_head *iter, *tmp;

	for (iter = list->next, tmp = iter->next;
	     iter != list; iter = tmp, tmp = iter->next) {

		context = list_entry (iter, struct iter_context, list);
		free(context);
	}
}

/*
 * Clean up function for a confdb instance (confdb_initialize) handle
 */
static void confdb_instance_destructor (void *instance)
{
	struct confdb_inst *confdb_inst = instance;

	pthread_mutex_destroy (&confdb_inst->response_mutex);
	pthread_mutex_destroy (&confdb_inst->dispatch_mutex);
}

static struct iter_context *find_iter_context(struct list_head *list, unsigned int object_handle)
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
 * @defgroup confdb_openais
 * @ingroup openais
 *
 * @{
 */

confdb_error_t confdb_initialize (
	confdb_handle_t *handle,
	confdb_callbacks_t *callbacks)
{
	SaAisErrorT error;
	struct confdb_inst *confdb_inst;

	error = saHandleCreate (&confdb_handle_t_db, sizeof (struct confdb_inst), handle);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&confdb_handle_t_db, *handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	if (getenv("OPENAIS_DEFAULT_CONFIG_IFACE")) {
		error = confdb_sa_init();
		confdb_inst->standalone = 1;
	}
	else {
		error = saServiceConnect (&confdb_inst->dispatch_fd,
					  &confdb_inst->response_fd,
					  CONFDB_SERVICE);
	}
	if (error != SA_AIS_OK)
		goto error_put_destroy;

	memcpy (&confdb_inst->callbacks, callbacks, sizeof (confdb_callbacks_t));

	pthread_mutex_init (&confdb_inst->response_mutex, NULL);
	pthread_mutex_init (&confdb_inst->dispatch_mutex, NULL);

	list_init (&confdb_inst->object_find_head);
	list_init (&confdb_inst->object_iter_head);
	list_init (&confdb_inst->key_iter_head);

	saHandleInstancePut (&confdb_handle_t_db, *handle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&confdb_handle_t_db, *handle);
error_destroy:
	saHandleDestroy (&confdb_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

confdb_error_t confdb_finalize (
	confdb_handle_t handle)
{
	struct confdb_inst *confdb_inst;
	SaAisErrorT error;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&confdb_inst->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (confdb_inst->finalize) {
		pthread_mutex_unlock (&confdb_inst->response_mutex);
		saHandleInstancePut (&confdb_handle_t_db, handle);
		return (CONFDB_ERR_BAD_HANDLE);
	}

	confdb_inst->finalize = 1;

	pthread_mutex_unlock (&confdb_inst->response_mutex);

	saHandleDestroy (&confdb_handle_t_db, handle);

	/* Free saved context handles */
	free_context_list(&confdb_inst->object_find_head);
	free_context_list(&confdb_inst->object_iter_head);
	free_context_list(&confdb_inst->key_iter_head);

	if (!confdb_inst->standalone) {
		/*
		 * Disconnect from the server
		 */
		if (confdb_inst->response_fd != -1) {
			shutdown(confdb_inst->response_fd, 0);
			close(confdb_inst->response_fd);
		}
		if (confdb_inst->dispatch_fd != -1) {
			shutdown(confdb_inst->dispatch_fd, 0);
			close(confdb_inst->dispatch_fd);
		}
	}
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (CONFDB_OK);
}

confdb_error_t confdb_fd_get (
	confdb_handle_t handle,
	int *fd)
{
	SaAisErrorT error;
	struct confdb_inst *confdb_inst;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	*fd = confdb_inst->dispatch_fd;

	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (SA_AIS_OK);
}

confdb_error_t confdb_context_get (
	confdb_handle_t handle,
	void **context)
{
	SaAisErrorT error;
	struct confdb_inst *confdb_inst;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	*context = confdb_inst->context;

	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (SA_AIS_OK);
}

confdb_error_t confdb_context_set (
	confdb_handle_t handle,
	void *context)
{
	SaAisErrorT error;
	struct confdb_inst *confdb_inst;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	confdb_inst->context = context;

	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (SA_AIS_OK);
}

struct res_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char data[512000];
};

confdb_error_t confdb_dispatch (
	confdb_handle_t handle,
	confdb_dispatch_t dispatch_types)
{
	struct pollfd ufds;
	int timeout = -1;
	SaAisErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct confdb_inst *confdb_inst;
	struct res_lib_confdb_change_callback *res_confdb_change_callback;
	confdb_callbacks_t callbacks;
	struct res_overlay dispatch_data;
	int ignore_dispatch = 0;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = CONFDB_ERR_NOT_SUPPORTED;
		goto error_unlock;
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ONE or SA_DISPATCH_ALL and
	 * wait indefinately for SA_DISPATCH_BLOCKING
	 */
	if (dispatch_types == CONFDB_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		ufds.fd = confdb_inst->dispatch_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto error_nounlock;
		}

		pthread_mutex_lock (&confdb_inst->dispatch_mutex);

		/*
		 * Regather poll data in case ufds has changed since taking lock
		 */
		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto error_nounlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (confdb_inst->finalize == 1) {
			error = CONFDB_OK;
			pthread_mutex_unlock (&confdb_inst->dispatch_mutex);
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatch_types == CONFDB_DISPATCH_ALL) {
			pthread_mutex_unlock (&confdb_inst->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&confdb_inst->dispatch_mutex);
			continue; /* next poll */
		}

		if (ufds.revents & POLLIN) {
			/*
			 * Queue empty, read response from socket
			 */
			error = saRecvRetry (confdb_inst->dispatch_fd, &dispatch_data.header,
				sizeof (mar_res_header_t));
			if (error != SA_AIS_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
				error = saRecvRetry (confdb_inst->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (mar_res_header_t));

				if (error != SA_AIS_OK) {
					goto error_unlock;
				}
			}
		} else {
			pthread_mutex_unlock (&confdb_inst->dispatch_mutex);
			continue;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that confdbFinalize has been called.
		*/
		memcpy (&callbacks, &confdb_inst->callbacks, sizeof (confdb_callbacks_t));

		pthread_mutex_unlock (&confdb_inst->dispatch_mutex);
		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data.header.id) {
		case MESSAGE_RES_CONFDB_CHANGE_CALLBACK:
			res_confdb_change_callback = (struct res_lib_confdb_change_callback *)&dispatch_data;

			callbacks.confdb_change_notify_fn (handle,
							   res_confdb_change_callback->parent_object_handle,
							   res_confdb_change_callback->object_handle,
							   res_confdb_change_callback->object_name.value,
							   res_confdb_change_callback->object_name.length,
							   res_confdb_change_callback->key_name.value,
							   res_confdb_change_callback->key_name.length,
							   res_confdb_change_callback->key_value.value,
							   res_confdb_change_callback->key_value.length);
			break;

		default:
			error = SA_AIS_ERR_LIBRARY;
			goto error_nounlock;
			break;
		}

		/*
		 * Determine if more messages should be processed
		 * */
		switch (dispatch_types) {
		case CONFDB_DISPATCH_ONE:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			} else {
				cont = 0;
			}
			break;
		case CONFDB_DISPATCH_ALL:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			}
			break;
		case CONFDB_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

error_unlock:
	saHandleInstancePut (&confdb_handle_t_db, handle);
error_nounlock:
	return (error);
}

confdb_error_t confdb_object_create (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *object_name,
	int object_name_len,
	unsigned int *object_handle)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_object_create req_lib_confdb_object_create;
	struct res_lib_confdb_object_create res_lib_confdb_object_create;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_object_create(parent_object_handle,
					    object_name, object_name_len,
					    object_handle))
			error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_create.header.size = sizeof (struct req_lib_confdb_object_create);
	req_lib_confdb_object_create.header.id = MESSAGE_REQ_CONFDB_OBJECT_CREATE;
	req_lib_confdb_object_create.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_object_create.object_name.value, object_name, object_name_len);
	req_lib_confdb_object_create.object_name.length = object_name_len;

	iov[0].iov_base = (char *)&req_lib_confdb_object_create;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_create);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res_lib_confdb_object_create, sizeof (struct res_lib_confdb_object_create));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_create.header.error;
	*object_handle = res_lib_confdb_object_create.object_handle;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_object_destroy (
	confdb_handle_t handle,
	unsigned int object_handle)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_object_destroy req_lib_confdb_object_destroy;
	mar_res_header_t res;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_object_destroy(object_handle))
			error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_destroy.header.size = sizeof (struct req_lib_confdb_object_destroy);
	req_lib_confdb_object_destroy.header.id = MESSAGE_REQ_CONFDB_OBJECT_DESTROY;
	req_lib_confdb_object_destroy.object_handle = object_handle;

	iov[0].iov_base = (char *)&req_lib_confdb_object_destroy;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_destroy);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res, sizeof ( mar_res_header_t));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_object_parent_get (
	confdb_handle_t handle,
	unsigned int object_handle,
	unsigned int *parent_object_handle)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_object_parent_get req_lib_confdb_object_parent_get;
	struct res_lib_confdb_object_parent_get res_lib_confdb_object_parent_get;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_object_parent_get(object_handle, parent_object_handle))
			error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_parent_get.header.size = sizeof (struct req_lib_confdb_object_parent_get);
	req_lib_confdb_object_parent_get.header.id = MESSAGE_REQ_CONFDB_OBJECT_PARENT_GET;
	req_lib_confdb_object_parent_get.object_handle = object_handle;

	iov[0].iov_base = (char *)&req_lib_confdb_object_parent_get;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_parent_get);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res_lib_confdb_object_parent_get, sizeof (struct res_lib_confdb_object_parent_get));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_parent_get.header.error;
	*parent_object_handle = res_lib_confdb_object_parent_get.parent_object_handle;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_key_create (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *value,
	int value_len)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_key_create req_lib_confdb_key_create;
	mar_res_header_t res;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_key_create(parent_object_handle,
					 key_name, key_name_len,
					 value, value_len))
			error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_create.header.size = sizeof (struct req_lib_confdb_key_create);
	req_lib_confdb_key_create.header.id = MESSAGE_REQ_CONFDB_KEY_CREATE;
	req_lib_confdb_key_create.object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_create.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_create.key_name.length = key_name_len;
	memcpy(req_lib_confdb_key_create.value.value, value, value_len);
	req_lib_confdb_key_create.value.length = value_len;

	iov[0].iov_base = (char *)&req_lib_confdb_key_create;
	iov[0].iov_len = sizeof (struct req_lib_confdb_key_create);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res, sizeof (res));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_key_get (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *value,
	int *value_len)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_key_get req_lib_confdb_key_get;
	struct res_lib_confdb_key_get res_lib_confdb_key_get;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_key_get(parent_object_handle,
				      key_name, key_name_len,
				      value, value_len))
			error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_key_get.header.size = sizeof (struct req_lib_confdb_key_get);
	req_lib_confdb_key_get.header.id = MESSAGE_REQ_CONFDB_KEY_GET;
	req_lib_confdb_key_get.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_get.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_get.key_name.length = key_name_len;

	iov[0].iov_base = (char *)&req_lib_confdb_key_get;
	iov[0].iov_len = sizeof (struct req_lib_confdb_key_get);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res_lib_confdb_key_get, sizeof (struct res_lib_confdb_key_get));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_key_get.header.error;
	if (error == SA_AIS_OK) {
		*value_len = res_lib_confdb_key_get.value.length;
		memcpy(value, res_lib_confdb_key_get.value.value, *value_len);
	}

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}


confdb_error_t confdb_key_replace (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *old_value,
	int old_value_len,
	void *new_value,
	int new_value_len)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_key_replace req_lib_confdb_key_replace;
	mar_res_header_t res;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_key_replace(parent_object_handle,
					  key_name, key_name_len,
					  old_value, old_value_len,
					  new_value, new_value_len))
			error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}
	req_lib_confdb_key_replace.header.size = sizeof (struct req_lib_confdb_key_replace);
	req_lib_confdb_key_replace.header.id = MESSAGE_REQ_CONFDB_KEY_REPLACE;
	req_lib_confdb_key_replace.object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_replace.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_replace.key_name.length = key_name_len;
	memcpy(req_lib_confdb_key_replace.old_value.value, old_value, old_value_len);
	req_lib_confdb_key_replace.old_value.length = old_value_len;
	memcpy(req_lib_confdb_key_replace.new_value.value, new_value, new_value_len);
	req_lib_confdb_key_replace.new_value.length = new_value_len;

	iov[0].iov_base = (char *)&req_lib_confdb_key_replace;
	iov[0].iov_len = sizeof (struct req_lib_confdb_key_replace);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res, sizeof (res));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_object_iter_start (
	confdb_handle_t handle,
	unsigned int object_handle)
{
	struct confdb_inst *confdb_inst;
	confdb_error_t error = SA_AIS_OK;
	struct iter_context *context;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->object_iter_head, object_handle);
	if (!context) {
		context = malloc(sizeof(struct iter_context));
		if (!context) {
			error = CONFDB_ERR_NO_MEMORY;
			goto ret;
		}
		context->parent_object_handle = object_handle;
		list_add(&context->list, &confdb_inst->object_iter_head);
	}

	context->context = 0;

	saHandleInstancePut (&confdb_handle_t_db, handle);

ret:
	return error;
}

confdb_error_t confdb_key_iter_start (
	confdb_handle_t handle,
	unsigned int object_handle)
{
	struct confdb_inst *confdb_inst;
	confdb_error_t error = SA_AIS_OK;
	struct iter_context *context;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->key_iter_head, object_handle);
	if (!context) {
		context = malloc(sizeof(struct iter_context));
		if (!context) {
			error = CONFDB_ERR_NO_MEMORY;
			goto ret;
		}
		context->parent_object_handle = object_handle;
		list_add(&context->list, &confdb_inst->key_iter_head);
	}

	context->context = 0;

	saHandleInstancePut (&confdb_handle_t_db, handle);

ret:
	return error;
}

confdb_error_t confdb_object_find_start (
	confdb_handle_t handle,
	unsigned int parent_object_handle)
{
	struct confdb_inst *confdb_inst;
	confdb_error_t error = SA_AIS_OK;
	struct iter_context *context;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	context = find_iter_context(&confdb_inst->object_find_head, parent_object_handle);
	if (!context) {
		context = malloc(sizeof(struct iter_context));
		if (!context) {
			error = CONFDB_ERR_NO_MEMORY;
			goto ret;
		}
		context->parent_object_handle = parent_object_handle;
		list_add(&context->list, &confdb_inst->object_find_head);
	}

	context->context = 0;

	saHandleInstancePut (&confdb_handle_t_db, handle);

ret:
	return error;
}

confdb_error_t confdb_object_find (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *object_name,
	int object_name_len,
	unsigned int *object_handle)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct iter_context *context;
	struct req_lib_confdb_object_find req_lib_confdb_object_find;
	struct res_lib_confdb_object_find res_lib_confdb_object_find;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* You MUST call confdb_object_find_start first */
	context = find_iter_context(&confdb_inst->object_find_head, parent_object_handle);
	if (!context) {
		error =	CONFDB_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_object_find(parent_object_handle,
					  context->context,
					  object_name, object_name_len,
					  object_handle,
					  &context->context))
			error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_confdb_object_find.header.size = sizeof (struct req_lib_confdb_object_find);
	req_lib_confdb_object_find.header.id = MESSAGE_REQ_CONFDB_OBJECT_FIND;
	req_lib_confdb_object_find.parent_object_handle = parent_object_handle;
	req_lib_confdb_object_find.next_entry = context->context;
	memcpy(req_lib_confdb_object_find.object_name.value, object_name, object_name_len);
	req_lib_confdb_object_find.object_name.length = object_name_len;

	iov[0].iov_base = (char *)&req_lib_confdb_object_find;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_find);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res_lib_confdb_object_find, sizeof (struct res_lib_confdb_object_find));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_find.header.error;
	*object_handle = res_lib_confdb_object_find.object_handle;
	context->context = res_lib_confdb_object_find.next_entry;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}


confdb_error_t confdb_object_iter (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	unsigned int *object_handle,
	void *object_name,
	int *object_name_len)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct iter_context *context;
	struct req_lib_confdb_object_iter req_lib_confdb_object_iter;
	struct res_lib_confdb_object_iter res_lib_confdb_object_iter;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* You MUST call confdb_object_iter_start first */
	context = find_iter_context(&confdb_inst->object_iter_head, parent_object_handle);
	if (!context) {
		error =	CONFDB_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_object_iter(parent_object_handle,
					  context->context,
					  object_handle,
					  object_name, object_name_len))
			error = SA_AIS_ERR_ACCESS;
		goto sa_exit;
	}

	req_lib_confdb_object_iter.header.size = sizeof (struct req_lib_confdb_object_iter);
	req_lib_confdb_object_iter.header.id = MESSAGE_REQ_CONFDB_OBJECT_ITER;
	req_lib_confdb_object_iter.parent_object_handle = parent_object_handle;
	req_lib_confdb_object_iter.next_entry = context->context;

	iov[0].iov_base = (char *)&req_lib_confdb_object_iter;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_iter);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res_lib_confdb_object_iter, sizeof (struct res_lib_confdb_object_iter));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_iter.header.error;
	if (error == SA_AIS_OK) {
		*object_name_len = res_lib_confdb_object_iter.object_name.length;
		memcpy(object_name, res_lib_confdb_object_iter.object_name.value, *object_name_len);
		*object_handle = res_lib_confdb_object_iter.object_handle;
	}
sa_exit:
	context->context++;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_key_iter (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int *key_name_len,
	void *value,
	int *value_len)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct iter_context *context;
	struct req_lib_confdb_key_iter req_lib_confdb_key_iter;
	struct res_lib_confdb_key_iter res_lib_confdb_key_iter;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* You MUST call confdb_key_iter_start first */
	context = find_iter_context(&confdb_inst->key_iter_head, parent_object_handle);
	if (!context) {
		error =	CONFDB_ERR_CONTEXT_NOT_FOUND;
		goto error_exit;
	}

	if (confdb_inst->standalone) {
		error = SA_AIS_OK;

		if (confdb_sa_key_iter(parent_object_handle,
				       context->context,
				       key_name, key_name_len,
				       value, value_len))
			error = SA_AIS_ERR_ACCESS;
		goto sa_exit;
	}

	req_lib_confdb_key_iter.header.size = sizeof (struct req_lib_confdb_key_iter);
	req_lib_confdb_key_iter.header.id = MESSAGE_REQ_CONFDB_KEY_ITER;
	req_lib_confdb_key_iter.parent_object_handle = parent_object_handle;
	req_lib_confdb_key_iter.next_entry = context->context;

	iov[0].iov_base = (char *)&req_lib_confdb_key_iter;
	iov[0].iov_len = sizeof (struct req_lib_confdb_key_iter);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = saSendMsgReceiveReply (confdb_inst->response_fd, iov, 1,
		&res_lib_confdb_key_iter, sizeof (struct res_lib_confdb_key_iter));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_key_iter.header.error;
	if (error == SA_AIS_OK) {
		*key_name_len = res_lib_confdb_key_iter.key_name.length;
		memcpy(key_name, res_lib_confdb_key_iter.key_name.value, *key_name_len);
		*value_len = res_lib_confdb_key_iter.value.length;
		memcpy(value, res_lib_confdb_key_iter.value.value, *value_len);
	}

sa_exit:
	context->context++;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}
