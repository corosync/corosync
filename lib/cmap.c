/*
 * Copyright (c) 2011-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>
#include <corosync/list.h>
#include <qb/qbipcc.h>

#include <corosync/cmap.h>
#include <corosync/ipc_cmap.h>

#include "util.h"
#include <stdio.h>

struct cmap_inst {
	int finalize;
	qb_ipcc_connection_t *c;
	const void *context;
};

struct cmap_track_inst {
	void *user_data;
	cmap_notify_fn_t notify_fn;
	qb_ipcc_connection_t *c;
	cmap_track_handle_t track_handle;
};

static void cmap_inst_free (void *inst);

DECLARE_HDB_DATABASE(cmap_handle_t_db, cmap_inst_free);
DECLARE_HDB_DATABASE(cmap_track_handle_t_db,NULL);

/*
 * Function prototypes
 */
static cs_error_t cmap_get_int(
	cmap_handle_t handle,
	const char *key_name,
	void *value,
	size_t value_size,
	cmap_value_types_t type);

static cs_error_t cmap_adjust_int(cmap_handle_t handle, const char *key_name, int32_t step);

/*
 * Function implementations
 */
cs_error_t cmap_initialize (cmap_handle_t *handle)
{
	cs_error_t error;
	struct cmap_inst *cmap_inst;

	error = hdb_error_to_cs(hdb_handle_create(&cmap_handle_t_db, sizeof(*cmap_inst), handle));
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_cs(hdb_handle_get(&cmap_handle_t_db, *handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		goto error_destroy;
	}

	error = CS_OK;
	cmap_inst->finalize = 0;
	cmap_inst->c = qb_ipcc_connect("cmap", IPC_REQUEST_SIZE);
	if (cmap_inst->c == NULL) {
		error = qb_to_cs_error(-errno);
		goto error_put_destroy;
	}

	(void)hdb_handle_put(&cmap_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	(void)hdb_handle_put(&cmap_handle_t_db, *handle);
error_destroy:
	(void)hdb_handle_destroy(&cmap_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

static void cmap_inst_free (void *inst)
{
	struct cmap_inst *cmap_inst = (struct cmap_inst *)inst;
	qb_ipcc_disconnect(cmap_inst->c);
}

cs_error_t cmap_finalize(cmap_handle_t handle)
{
	struct cmap_inst *cmap_inst;
	cs_error_t error;
	hdb_handle_t track_inst_handle = 0;
        struct cmap_track_inst *cmap_track_inst;

	error = hdb_error_to_cs(hdb_handle_get(&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	if (cmap_inst->finalize) {
		(void)hdb_handle_put (&cmap_handle_t_db, handle);
		return (CS_ERR_BAD_HANDLE);
	}
	cmap_inst->finalize = 1;

	/*
	 * Destroy all track instances for given connection
	 */
	hdb_iterator_reset(&cmap_track_handle_t_db);
	while (hdb_iterator_next(&cmap_track_handle_t_db,
		(void*)&cmap_track_inst, &track_inst_handle) == 0) {

		if (cmap_track_inst->c == cmap_inst->c) {
			(void)hdb_handle_destroy(&cmap_track_handle_t_db, track_inst_handle);
		}

		(void)hdb_handle_put (&cmap_track_handle_t_db, track_inst_handle);
	}

	(void)hdb_handle_destroy(&cmap_handle_t_db, handle);

	(void)hdb_handle_put(&cmap_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t cmap_fd_get(cmap_handle_t handle, int *fd)
{
	cs_error_t error;
	struct cmap_inst *cmap_inst;

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	error = qb_to_cs_error (qb_ipcc_fd_get (cmap_inst->c, fd));

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_dispatch (
	cmap_handle_t handle,
        cs_dispatch_flags_t dispatch_types)
{
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct cmap_inst *cmap_inst;
	struct qb_ipc_response_header *dispatch_data;
	char dispatch_buf[IPC_DISPATCH_SIZE];
	struct res_lib_cmap_notify_callback *res_lib_cmap_notify_callback;
	struct cmap_track_inst *cmap_track_inst;
	struct cmap_notify_value old_val;
	struct cmap_notify_value new_val;

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
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
		error = qb_to_cs_error(qb_ipcc_event_recv (
			cmap_inst->c,
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
		 * Dispatch incoming message
		 */
		switch (dispatch_data->id) {
		case MESSAGE_RES_CMAP_NOTIFY_CALLBACK:
			res_lib_cmap_notify_callback = (struct res_lib_cmap_notify_callback *)dispatch_data;

			error = hdb_error_to_cs(hdb_handle_get(&cmap_track_handle_t_db,
					res_lib_cmap_notify_callback->track_inst_handle,
					(void *)&cmap_track_inst));
			if (error == CS_ERR_BAD_HANDLE) {
				/*
				 * User deleted tracker -> ignore error
				 */
				 break;
			}
			if (error != CS_OK) {
				goto error_put;
			}

			new_val.type = res_lib_cmap_notify_callback->new_value_type;
			old_val.type = res_lib_cmap_notify_callback->old_value_type;
			new_val.len = res_lib_cmap_notify_callback->new_value_len;
			old_val.len = res_lib_cmap_notify_callback->old_value_len;
			new_val.data = res_lib_cmap_notify_callback->new_value;
			old_val.data = (((const char *)res_lib_cmap_notify_callback->new_value) + new_val.len);

			cmap_track_inst->notify_fn(handle,
					cmap_track_inst->track_handle,
					res_lib_cmap_notify_callback->event,
					(char *)res_lib_cmap_notify_callback->key_name.value,
					new_val,
					old_val,
					cmap_track_inst->user_data);

			(void)hdb_handle_put(&cmap_track_handle_t_db, res_lib_cmap_notify_callback->track_inst_handle);
			break;
		default:
			error = CS_ERR_LIBRARY;
			goto error_put;
			break;
		}
		if (cmap_inst->finalize) {
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
	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_context_get (
	cmap_handle_t handle,
	const void **context)
{
	cs_error_t error;
	struct cmap_inst *cmap_inst;

	error = hdb_error_to_cs(hdb_handle_get(&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	*context = cmap_inst->context;

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t cmap_context_set (
	cmap_handle_t handle,
	const void *context)
{
	cs_error_t error;
	struct cmap_inst *cmap_inst;

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	cmap_inst->context = context;

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t cmap_set (
	cmap_handle_t handle,
	const char *key_name,
	const void *value,
	size_t value_len,
	cmap_value_types_t type)
{
	cs_error_t error;
	struct iovec iov[2];
	struct cmap_inst *cmap_inst;
	struct req_lib_cmap_set req_lib_cmap_set;
	struct res_lib_cmap_set res_lib_cmap_set;

	if (key_name == NULL || value == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	if (strlen(key_name) >= CS_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
	}

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_set, 0, sizeof(req_lib_cmap_set));
	req_lib_cmap_set.header.size = sizeof(req_lib_cmap_set) + value_len;
	req_lib_cmap_set.header.id = MESSAGE_REQ_CMAP_SET;

	memcpy(req_lib_cmap_set.key_name.value, key_name, strlen(key_name));
	req_lib_cmap_set.key_name.length = strlen(key_name);

	req_lib_cmap_set.value_len = value_len;
	req_lib_cmap_set.type = type;

	iov[0].iov_base = (char *)&req_lib_cmap_set;
	iov[0].iov_len = sizeof(req_lib_cmap_set);
	iov[1].iov_base = (void *)value;
	iov[1].iov_len = value_len;

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		iov,
		2,
		&res_lib_cmap_set,
		sizeof (struct res_lib_cmap_set), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_set.header.error;
	}

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_set_int8(cmap_handle_t handle, const char *key_name, int8_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_INT8));
}

cs_error_t cmap_set_uint8(cmap_handle_t handle, const char *key_name, uint8_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_UINT8));
}

cs_error_t cmap_set_int16(cmap_handle_t handle, const char *key_name, int16_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_INT16));
}

cs_error_t cmap_set_uint16(cmap_handle_t handle, const char *key_name, uint16_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_UINT16));
}

cs_error_t cmap_set_int32(cmap_handle_t handle, const char *key_name, int32_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_INT32));
}

cs_error_t cmap_set_uint32(cmap_handle_t handle, const char *key_name, uint32_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_UINT32));
}

cs_error_t cmap_set_int64(cmap_handle_t handle, const char *key_name, int64_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_INT64));
}

cs_error_t cmap_set_uint64(cmap_handle_t handle, const char *key_name, uint64_t value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_UINT64));
}

cs_error_t cmap_set_float(cmap_handle_t handle, const char *key_name, float value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_FLOAT));
}

cs_error_t cmap_set_double(cmap_handle_t handle, const char *key_name, double value)
{
	return (cmap_set(handle, key_name, &value, sizeof(value), CMAP_VALUETYPE_DOUBLE));
}

cs_error_t cmap_set_string(cmap_handle_t handle, const char *key_name, const char *value)
{

	if (value == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	return (cmap_set(handle, key_name, value, strlen(value), CMAP_VALUETYPE_STRING));
}

cs_error_t cmap_delete(cmap_handle_t handle, const char *key_name)
{
	cs_error_t error;
	struct iovec iov;
	struct cmap_inst *cmap_inst;
	struct req_lib_cmap_delete req_lib_cmap_delete;
	struct res_lib_cmap_delete res_lib_cmap_delete;

	if (key_name == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}
	if (strlen(key_name) >= CS_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
	}

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_delete, 0, sizeof(req_lib_cmap_delete));
	req_lib_cmap_delete.header.size = sizeof(req_lib_cmap_delete);
	req_lib_cmap_delete.header.id = MESSAGE_REQ_CMAP_DELETE;

	memcpy(req_lib_cmap_delete.key_name.value, key_name, strlen(key_name));
	req_lib_cmap_delete.key_name.length = strlen(key_name);

	iov.iov_base = (char *)&req_lib_cmap_delete;
	iov.iov_len = sizeof(req_lib_cmap_delete);

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		&res_lib_cmap_delete,
		sizeof (struct res_lib_cmap_delete), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_delete.header.error;
	}

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_get(
		cmap_handle_t handle,
		const char *key_name,
		void *value,
		size_t *value_len,
		cmap_value_types_t *type)
{
	cs_error_t error;
	struct cmap_inst *cmap_inst;
	struct iovec iov;
	struct req_lib_cmap_get req_lib_cmap_get;
	struct res_lib_cmap_get *res_lib_cmap_get;
	size_t res_size;

	if (key_name == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}
	if (strlen(key_name) >= CS_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
	}

	if (value != NULL && value_len == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_get, 0, sizeof(req_lib_cmap_get));
	req_lib_cmap_get.header.size = sizeof(req_lib_cmap_get);
	req_lib_cmap_get.header.id = MESSAGE_REQ_CMAP_GET;

	memcpy(req_lib_cmap_get.key_name.value, key_name, strlen(key_name));
	req_lib_cmap_get.key_name.length = strlen(key_name);

	if (value != NULL && value_len != NULL) {
		req_lib_cmap_get.value_len = *value_len;
	} else {
		req_lib_cmap_get.value_len = 0;
	}

	iov.iov_base = (char *)&req_lib_cmap_get;
	iov.iov_len = sizeof(req_lib_cmap_get);

	res_size = sizeof(struct res_lib_cmap_get) + req_lib_cmap_get.value_len;

	res_lib_cmap_get = malloc(res_size);
	if (res_lib_cmap_get == NULL) {
		return (CS_ERR_NO_MEMORY);
	}

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		res_lib_cmap_get,
		res_size, CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_get->header.error;
	}

	if (error == CS_OK) {
		if (type != NULL) {
			*type = res_lib_cmap_get->type;
		}

		if (value_len != NULL) {
			*value_len = res_lib_cmap_get->value_len;
		}

		if (value != NULL && value_len != NULL) {
			memcpy(value, res_lib_cmap_get->value, res_lib_cmap_get->value_len);
		}
	}

	free(res_lib_cmap_get);

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

static cs_error_t cmap_get_int(
	cmap_handle_t handle,
	const char *key_name,
	void *value,
	size_t value_size,
	cmap_value_types_t type)
{
	char key_value[16];
	size_t key_size;
	cs_error_t err;

	cmap_value_types_t key_type;

	key_size = sizeof(key_value);
	memset(key_value, 0, key_size);

	err = cmap_get(handle, key_name, key_value, &key_size, &key_type);
	if (err != CS_OK)
		return (err);

	if (key_type != type) {
		return (CS_ERR_INVALID_PARAM);
	}

	memcpy(value, key_value, value_size);

	return (CS_OK);
}

cs_error_t cmap_get_int8(cmap_handle_t handle, const char *key_name, int8_t *i8)
{

	return (cmap_get_int(handle, key_name, i8, sizeof(*i8), CMAP_VALUETYPE_INT8));
}

cs_error_t cmap_get_uint8(cmap_handle_t handle, const char *key_name, uint8_t *u8)
{

	return (cmap_get_int(handle, key_name, u8, sizeof(*u8), CMAP_VALUETYPE_UINT8));
}

cs_error_t cmap_get_int16(cmap_handle_t handle, const char *key_name, int16_t *i16)
{

	return (cmap_get_int(handle, key_name, i16, sizeof(*i16), CMAP_VALUETYPE_INT16));
}

cs_error_t cmap_get_uint16(cmap_handle_t handle, const char *key_name, uint16_t *u16)
{

	return (cmap_get_int(handle, key_name, u16, sizeof(*u16), CMAP_VALUETYPE_UINT16));
}

cs_error_t cmap_get_int32(cmap_handle_t handle, const char *key_name, int32_t *i32)
{

	return (cmap_get_int(handle, key_name, i32, sizeof(*i32), CMAP_VALUETYPE_INT32));
}

cs_error_t cmap_get_uint32(cmap_handle_t handle, const char *key_name, uint32_t *u32)
{

	return (cmap_get_int(handle, key_name, u32, sizeof(*u32), CMAP_VALUETYPE_UINT32));
}

cs_error_t cmap_get_int64(cmap_handle_t handle, const char *key_name, int64_t *i64)
{

	return (cmap_get_int(handle, key_name, i64, sizeof(*i64), CMAP_VALUETYPE_INT64));
}

cs_error_t cmap_get_uint64(cmap_handle_t handle, const char *key_name, uint64_t *u64)
{

	return (cmap_get_int(handle, key_name, u64, sizeof(*u64), CMAP_VALUETYPE_UINT64));
}

cs_error_t cmap_get_float(cmap_handle_t handle, const char *key_name, float *flt)
{

	return (cmap_get_int(handle, key_name, flt, sizeof(*flt), CMAP_VALUETYPE_FLOAT));
}

cs_error_t cmap_get_double(cmap_handle_t handle, const char *key_name, double *dbl)
{

	return (cmap_get_int(handle, key_name, dbl, sizeof(*dbl), CMAP_VALUETYPE_DOUBLE));
}

cs_error_t cmap_get_string(cmap_handle_t handle, const char *key_name, char **str)
{
	cs_error_t res;
	size_t str_len;
	cmap_value_types_t type;

	res = cmap_get(handle, key_name, NULL, &str_len, &type);

	if (res != CS_OK || type != CMAP_VALUETYPE_STRING) {
		if (res == CS_OK) {
			res = CS_ERR_INVALID_PARAM;
		}

		goto return_error;
	}

	*str = malloc(str_len);
	if (*str == NULL) {
		res = CS_ERR_NO_MEMORY;

		goto return_error;
	}

	res = cmap_get(handle, key_name, *str, &str_len, &type);
	if (res != CS_OK) {
		free(*str);

		goto return_error;
	}

	return (CS_OK);

return_error:
	return (res);
}

static cs_error_t cmap_adjust_int(cmap_handle_t handle, const char *key_name, int32_t step)
{
	cs_error_t error;
	struct iovec iov;
	struct cmap_inst *cmap_inst;
	struct req_lib_cmap_adjust_int req_lib_cmap_adjust_int;
	struct res_lib_cmap_adjust_int res_lib_cmap_adjust_int;

	if (key_name == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}
	if (strlen(key_name) >= CS_MAX_NAME_LENGTH) {
		return (CS_ERR_NAME_TOO_LONG);
	}

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_adjust_int, 0, sizeof(req_lib_cmap_adjust_int));
	req_lib_cmap_adjust_int.header.size = sizeof(req_lib_cmap_adjust_int);
	req_lib_cmap_adjust_int.header.id = MESSAGE_REQ_CMAP_ADJUST_INT;

	memcpy(req_lib_cmap_adjust_int.key_name.value, key_name, strlen(key_name));
	req_lib_cmap_adjust_int.key_name.length = strlen(key_name);

	req_lib_cmap_adjust_int.step = step;

	iov.iov_base = (char *)&req_lib_cmap_adjust_int;
	iov.iov_len = sizeof(req_lib_cmap_adjust_int);

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		&res_lib_cmap_adjust_int,
		sizeof (struct res_lib_cmap_adjust_int), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_adjust_int.header.error;
	}

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_inc(cmap_handle_t handle, const char *key_name)
{

	return (cmap_adjust_int(handle, key_name, 1));
}

cs_error_t cmap_dec(cmap_handle_t handle, const char *key_name)
{

	return (cmap_adjust_int(handle, key_name, -1));
}

cs_error_t cmap_iter_init(
		cmap_handle_t handle,
		const char *prefix,
		cmap_iter_handle_t *cmap_iter_handle)
{
	cs_error_t error;
	struct iovec iov;
	struct cmap_inst *cmap_inst;
	struct req_lib_cmap_iter_init req_lib_cmap_iter_init;
	struct res_lib_cmap_iter_init res_lib_cmap_iter_init;

	if (cmap_iter_handle == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_iter_init, 0, sizeof(req_lib_cmap_iter_init));
	req_lib_cmap_iter_init.header.size = sizeof(req_lib_cmap_iter_init);
	req_lib_cmap_iter_init.header.id = MESSAGE_REQ_CMAP_ITER_INIT;

	if (prefix) {
		if (strlen(prefix) >= CS_MAX_NAME_LENGTH) {
			return (CS_ERR_NAME_TOO_LONG);
		}
		memcpy(req_lib_cmap_iter_init.prefix.value, prefix, strlen(prefix));
		req_lib_cmap_iter_init.prefix.length = strlen(prefix);
	}

	iov.iov_base = (char *)&req_lib_cmap_iter_init;
	iov.iov_len = sizeof(req_lib_cmap_iter_init);

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		&res_lib_cmap_iter_init,
		sizeof (struct res_lib_cmap_iter_init), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_iter_init.header.error;
	}

	if (error == CS_OK) {
		*cmap_iter_handle = res_lib_cmap_iter_init.iter_handle;
	}

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_iter_next(
		cmap_handle_t handle,
		cmap_iter_handle_t iter_handle,
		char key_name[],
		size_t *value_len,
		cmap_value_types_t *type)
{
	cs_error_t error;
	struct iovec iov;
	struct cmap_inst *cmap_inst;
	struct req_lib_cmap_iter_next req_lib_cmap_iter_next;
	struct res_lib_cmap_iter_next res_lib_cmap_iter_next;

	if (key_name == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_iter_next, 0, sizeof(req_lib_cmap_iter_next));
	req_lib_cmap_iter_next.header.size = sizeof(req_lib_cmap_iter_next);
	req_lib_cmap_iter_next.header.id = MESSAGE_REQ_CMAP_ITER_NEXT;
	req_lib_cmap_iter_next.iter_handle = iter_handle;

	iov.iov_base = (char *)&req_lib_cmap_iter_next;
	iov.iov_len = sizeof(req_lib_cmap_iter_next);

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		&res_lib_cmap_iter_next,
		sizeof (struct res_lib_cmap_iter_next), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_iter_next.header.error;
	}

	if (error == CS_OK) {
		strncpy(key_name, (const char *)res_lib_cmap_iter_next.key_name.value, CMAP_KEYNAME_MAXLEN);

		if (value_len != NULL) {
			*value_len = res_lib_cmap_iter_next.value_len;
		}

		if (type != NULL) {
			*type = res_lib_cmap_iter_next.type;
		}
	}

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_iter_finalize(
		cmap_handle_t handle,
		cmap_iter_handle_t iter_handle)
{
	cs_error_t error;
	struct iovec iov;
	struct cmap_inst *cmap_inst;
	struct req_lib_cmap_iter_finalize req_lib_cmap_iter_finalize;
	struct res_lib_cmap_iter_finalize res_lib_cmap_iter_finalize;

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_iter_finalize, 0, sizeof(req_lib_cmap_iter_finalize));
	req_lib_cmap_iter_finalize.header.size = sizeof(req_lib_cmap_iter_finalize);
	req_lib_cmap_iter_finalize.header.id = MESSAGE_REQ_CMAP_ITER_FINALIZE;
	req_lib_cmap_iter_finalize.iter_handle = iter_handle;

	iov.iov_base = (char *)&req_lib_cmap_iter_finalize;
	iov.iov_len = sizeof(req_lib_cmap_iter_finalize);

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		&res_lib_cmap_iter_finalize,
		sizeof (struct res_lib_cmap_iter_finalize), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_iter_finalize.header.error;
	}

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_track_add(
	cmap_handle_t handle,
	const char *key_name,
	int32_t track_type,
	cmap_notify_fn_t notify_fn,
	void *user_data,
	cmap_track_handle_t *cmap_track_handle)
{
	cs_error_t error;
	struct iovec iov;
	struct cmap_inst *cmap_inst;
	struct req_lib_cmap_track_add req_lib_cmap_track_add;
	struct res_lib_cmap_track_add res_lib_cmap_track_add;
	struct cmap_track_inst *cmap_track_inst;
	cmap_track_handle_t cmap_track_inst_handle;

	if (cmap_track_handle == NULL || notify_fn == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	error = hdb_error_to_cs(hdb_handle_create(&cmap_track_handle_t_db,
				sizeof(*cmap_track_inst), &cmap_track_inst_handle));
	if (error != CS_OK) {
		goto error_put;
	}

	error = hdb_error_to_cs(hdb_handle_get(&cmap_track_handle_t_db,
				cmap_track_inst_handle, (void *)&cmap_track_inst));
	if (error != CS_OK) {
		goto error_put_destroy;
	}

	cmap_track_inst->user_data = user_data;
	cmap_track_inst->notify_fn = notify_fn;
	cmap_track_inst->c = cmap_inst->c;

	memset(&req_lib_cmap_track_add, 0, sizeof(req_lib_cmap_track_add));
	req_lib_cmap_track_add.header.size = sizeof(req_lib_cmap_track_add);
	req_lib_cmap_track_add.header.id = MESSAGE_REQ_CMAP_TRACK_ADD;

	if (key_name) {
		if (strlen(key_name) >= CS_MAX_NAME_LENGTH) {
			return (CS_ERR_NAME_TOO_LONG);
		}
		memcpy(req_lib_cmap_track_add.key_name.value, key_name, strlen(key_name));
		req_lib_cmap_track_add.key_name.length = strlen(key_name);
	}

	req_lib_cmap_track_add.track_type = track_type;
	req_lib_cmap_track_add.track_inst_handle = cmap_track_inst_handle;

	iov.iov_base = (char *)&req_lib_cmap_track_add;
	iov.iov_len = sizeof(req_lib_cmap_track_add);

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		&res_lib_cmap_track_add,
		sizeof (struct res_lib_cmap_track_add), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_track_add.header.error;
	}

	if (error == CS_OK) {
		*cmap_track_handle = res_lib_cmap_track_add.track_handle;
		cmap_track_inst->track_handle = *cmap_track_handle;
	}

	(void)hdb_handle_put (&cmap_track_handle_t_db, cmap_track_inst_handle);

	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);

error_put_destroy:
	(void)hdb_handle_put (&cmap_track_handle_t_db, cmap_track_inst_handle);
	(void)hdb_handle_destroy (&cmap_track_handle_t_db, cmap_track_inst_handle);

error_put:
	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}

cs_error_t cmap_track_delete(
		cmap_handle_t handle,
		cmap_track_handle_t track_handle)
{
	cs_error_t error;
	struct iovec iov;
	struct cmap_inst *cmap_inst;
	struct cmap_track_inst *cmap_track_inst;
	struct req_lib_cmap_track_delete req_lib_cmap_track_delete;
	struct res_lib_cmap_track_delete res_lib_cmap_track_delete;

	error = hdb_error_to_cs(hdb_handle_get (&cmap_handle_t_db, handle, (void *)&cmap_inst));
	if (error != CS_OK) {
		return (error);
	}

	memset(&req_lib_cmap_track_delete, 0, sizeof(req_lib_cmap_track_delete));
	req_lib_cmap_track_delete.header.size = sizeof(req_lib_cmap_track_delete);
	req_lib_cmap_track_delete.header.id = MESSAGE_REQ_CMAP_TRACK_DELETE;
	req_lib_cmap_track_delete.track_handle = track_handle;

	iov.iov_base = (char *)&req_lib_cmap_track_delete;
	iov.iov_len = sizeof(req_lib_cmap_track_delete);

	error = qb_to_cs_error(qb_ipcc_sendv_recv(
		cmap_inst->c,
		&iov,
		1,
		&res_lib_cmap_track_delete,
		sizeof (struct res_lib_cmap_track_delete), CS_IPC_TIMEOUT_MS));

	if (error == CS_OK) {
		error = res_lib_cmap_track_delete.header.error;
	}

	if (error == CS_OK) {
		error = hdb_error_to_cs(hdb_handle_get(&cmap_track_handle_t_db,
					res_lib_cmap_track_delete.track_inst_handle,
					(void *)&cmap_track_inst));
		if (error != CS_OK) {
			goto error_put;
		}

		(void)hdb_handle_put(&cmap_track_handle_t_db, res_lib_cmap_track_delete.track_inst_handle);
		(void)hdb_handle_destroy(&cmap_track_handle_t_db, res_lib_cmap_track_delete.track_inst_handle);
	}

error_put:
	(void)hdb_handle_put (&cmap_handle_t_db, handle);

	return (error);
}
