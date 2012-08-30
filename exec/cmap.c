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

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <assert.h>

#include <qb/qbloop.h>
#include <qb/qbipc_common.h>

#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/list.h>
#include <corosync/mar_gen.h>
#include <corosync/ipc_cmap.h>
#include <corosync/logsys.h>
#include <corosync/coroapi.h>
#include <corosync/icmap.h>

#include "service.h"

LOGSYS_DECLARE_SUBSYS ("CMAP");

struct cmap_conn_info {
	struct hdb_handle_database iter_db;
	struct hdb_handle_database track_db;
};

typedef uint64_t cmap_iter_handle_t;
typedef uint64_t cmap_track_handle_t;

struct cmap_track_user_data {
	void *conn;
	cmap_track_handle_t track_handle;
	uint64_t track_inst_handle;
};

static struct corosync_api_v1 *api;

static char *cmap_exec_init_fn (struct corosync_api_v1 *corosync_api);
static int cmap_exec_exit_fn(void);

static int cmap_lib_init_fn (void *conn);
static int cmap_lib_exit_fn (void *conn);

static void message_handler_req_lib_cmap_set(void *conn, const void *message);
static void message_handler_req_lib_cmap_delete(void *conn, const void *message);
static void message_handler_req_lib_cmap_get(void *conn, const void *message);
static void message_handler_req_lib_cmap_adjust_int(void *conn, const void *message);
static void message_handler_req_lib_cmap_iter_init(void *conn, const void *message);
static void message_handler_req_lib_cmap_iter_next(void *conn, const void *message);
static void message_handler_req_lib_cmap_iter_finalize(void *conn, const void *message);
static void message_handler_req_lib_cmap_track_add(void *conn, const void *message);
static void message_handler_req_lib_cmap_track_delete(void *conn, const void *message);

static void cmap_notify_fn(int32_t event,
		const char *key_name,
		struct icmap_notify_value new_val,
		struct icmap_notify_value old_val,
		void *user_data);

/*
 * Library Handler Definition
 */
static struct corosync_lib_handler cmap_lib_engine[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_cmap_set,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_cmap_delete,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_cmap_get,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_cmap_adjust_int,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_cmap_iter_init,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn				= message_handler_req_lib_cmap_iter_next,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn				= message_handler_req_lib_cmap_iter_finalize,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn				= message_handler_req_lib_cmap_track_add,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn				= message_handler_req_lib_cmap_track_delete,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
};


struct corosync_service_engine cmap_service_engine = {
	.name				        = "corosync configuration map access",
	.id					= CMAP_SERVICE,
	.priority				= 1,
	.private_data_size			= sizeof(struct cmap_conn_info),
	.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
	.lib_init_fn				= cmap_lib_init_fn,
	.lib_exit_fn				= cmap_lib_exit_fn,
	.lib_engine				= cmap_lib_engine,
	.lib_engine_count			= sizeof (cmap_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn				= cmap_exec_init_fn,
	.exec_exit_fn				= cmap_exec_exit_fn,
};

struct corosync_service_engine *cmap_get_service_engine_ver0 (void)
{
	return (&cmap_service_engine);
}

static int cmap_exec_exit_fn(void)
{
	return 0;
}

static char *cmap_exec_init_fn (
	struct corosync_api_v1 *corosync_api)
{
	api = corosync_api;

	return (NULL);
}

static int cmap_lib_init_fn (void *conn)
{
	struct cmap_conn_info *conn_info = (struct cmap_conn_info *)api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "lib_init_fn: conn=%p", conn);

	api->ipc_refcnt_inc(conn);

	memset(conn_info, 0, sizeof(*conn_info));
	hdb_create(&conn_info->iter_db);
	hdb_create(&conn_info->track_db);

	return (0);
}

static int cmap_lib_exit_fn (void *conn)
{
	struct cmap_conn_info *conn_info = (struct cmap_conn_info *)api->ipc_private_data_get (conn);
	hdb_handle_t iter_handle = 0;
	icmap_iter_t *iter;
	hdb_handle_t track_handle = 0;
	icmap_track_t *track;

	log_printf(LOGSYS_LEVEL_DEBUG, "exit_fn for conn=%p", conn);

	hdb_iterator_reset(&conn_info->iter_db);
        while (hdb_iterator_next(&conn_info->iter_db,
                (void*)&iter, &iter_handle) == 0) {

		icmap_iter_finalize(*iter);

		(void)hdb_handle_put (&conn_info->iter_db, iter_handle);
        }

	hdb_destroy(&conn_info->iter_db);

	hdb_iterator_reset(&conn_info->track_db);
        while (hdb_iterator_next(&conn_info->track_db,
                (void*)&track, &track_handle) == 0) {

		free(icmap_track_get_user_data(*track));

		icmap_track_delete(*track);

		(void)hdb_handle_put (&conn_info->track_db, track_handle);
        }
	hdb_destroy(&conn_info->track_db);

	api->ipc_refcnt_dec(conn);

	return (0);
}

static void message_handler_req_lib_cmap_set(void *conn, const void *message)
{
	const struct req_lib_cmap_set *req_lib_cmap_set = message;
	struct res_lib_cmap_set res_lib_cmap_set;
	cs_error_t ret;

	if (icmap_is_key_ro((char *)req_lib_cmap_set->key_name.value)) {
		ret = CS_ERR_ACCESS;
	} else {
		ret = icmap_set((char *)req_lib_cmap_set->key_name.value, &req_lib_cmap_set->value,
				req_lib_cmap_set->value_len, req_lib_cmap_set->type);
	}

	memset(&res_lib_cmap_set, 0, sizeof(res_lib_cmap_set));
	res_lib_cmap_set.header.size = sizeof(res_lib_cmap_set);
	res_lib_cmap_set.header.id = MESSAGE_RES_CMAP_SET;
	res_lib_cmap_set.header.error = ret;

	api->ipc_response_send(conn, &res_lib_cmap_set, sizeof(res_lib_cmap_set));
}

static void message_handler_req_lib_cmap_delete(void *conn, const void *message)
{
	const struct req_lib_cmap_set *req_lib_cmap_set = message;
	struct res_lib_cmap_delete res_lib_cmap_delete;
	cs_error_t ret;

	if (icmap_is_key_ro((char *)req_lib_cmap_set->key_name.value)) {
		ret = CS_ERR_ACCESS;
	} else {
		ret = icmap_delete((char *)req_lib_cmap_set->key_name.value);
	}

	memset(&res_lib_cmap_delete, 0, sizeof(res_lib_cmap_delete));
	res_lib_cmap_delete.header.size = sizeof(res_lib_cmap_delete);
	res_lib_cmap_delete.header.id = MESSAGE_RES_CMAP_DELETE;
	res_lib_cmap_delete.header.error = ret;

	api->ipc_response_send(conn, &res_lib_cmap_delete, sizeof(res_lib_cmap_delete));
}

static void message_handler_req_lib_cmap_get(void *conn, const void *message)
{
	const struct req_lib_cmap_get *req_lib_cmap_get = message;
	struct res_lib_cmap_get *res_lib_cmap_get;
	struct res_lib_cmap_get error_res_lib_cmap_get;
	cs_error_t ret;
	size_t value_len;
	size_t res_lib_cmap_get_size;
	icmap_value_types_t type;
	void *value;

	value_len = req_lib_cmap_get->value_len;

	res_lib_cmap_get_size = sizeof(*res_lib_cmap_get) + value_len;
	res_lib_cmap_get = malloc(res_lib_cmap_get_size);
	if (res_lib_cmap_get == NULL) {
		ret = CS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset(res_lib_cmap_get, 0, res_lib_cmap_get_size);

	if (value_len > 0) {
		value = res_lib_cmap_get->value;
	} else {
		value = NULL;
	}

	ret = icmap_get((char *)req_lib_cmap_get->key_name.value,
			value,
			&value_len,
			&type);

	if (ret != CS_OK) {
		free(res_lib_cmap_get);
		goto error_exit;
	}

	res_lib_cmap_get->header.size = res_lib_cmap_get_size;
	res_lib_cmap_get->header.id = MESSAGE_RES_CMAP_GET;
	res_lib_cmap_get->header.error = ret;
	res_lib_cmap_get->type = type;
	res_lib_cmap_get->value_len = value_len;

	api->ipc_response_send(conn, res_lib_cmap_get, res_lib_cmap_get_size);
	free(res_lib_cmap_get);

	return ;

error_exit:
	memset(&error_res_lib_cmap_get, 0, sizeof(error_res_lib_cmap_get));
	error_res_lib_cmap_get.header.size = sizeof(error_res_lib_cmap_get);
	error_res_lib_cmap_get.header.id = MESSAGE_RES_CMAP_GET;
	error_res_lib_cmap_get.header.error = ret;

	api->ipc_response_send(conn, &error_res_lib_cmap_get, sizeof(error_res_lib_cmap_get));
}

static void message_handler_req_lib_cmap_adjust_int(void *conn, const void *message)
{
	const struct req_lib_cmap_adjust_int *req_lib_cmap_adjust_int = message;
	struct res_lib_cmap_adjust_int res_lib_cmap_adjust_int;
	cs_error_t ret;

	if (icmap_is_key_ro((char *)req_lib_cmap_adjust_int->key_name.value)) {
		ret = CS_ERR_ACCESS;
	} else {
		ret = icmap_adjust_int((char *)req_lib_cmap_adjust_int->key_name.value,
		    req_lib_cmap_adjust_int->step);
	}

	memset(&res_lib_cmap_adjust_int, 0, sizeof(res_lib_cmap_adjust_int));
	res_lib_cmap_adjust_int.header.size = sizeof(res_lib_cmap_adjust_int);
	res_lib_cmap_adjust_int.header.id = MESSAGE_RES_CMAP_ADJUST_INT;
	res_lib_cmap_adjust_int.header.error = ret;

	api->ipc_response_send(conn, &res_lib_cmap_adjust_int, sizeof(res_lib_cmap_adjust_int));
}

static void message_handler_req_lib_cmap_iter_init(void *conn, const void *message)
{
	const struct req_lib_cmap_iter_init *req_lib_cmap_iter_init = message;
	struct res_lib_cmap_iter_init res_lib_cmap_iter_init;
	cs_error_t ret;
	icmap_iter_t iter;
	icmap_iter_t *hdb_iter;
	cmap_iter_handle_t handle = 0ULL;
	const char *prefix;
	struct cmap_conn_info *conn_info = (struct cmap_conn_info *)api->ipc_private_data_get (conn);

	if (req_lib_cmap_iter_init->prefix.length > 0) {
		prefix = (char *)req_lib_cmap_iter_init->prefix.value;
	} else {
		prefix = NULL;
	}

	iter = icmap_iter_init(prefix);
	if (iter == NULL) {
		ret = CS_ERR_NO_SECTIONS;
		goto reply_send;
	}

	ret = hdb_error_to_cs(hdb_handle_create(&conn_info->iter_db, sizeof(iter), &handle));
	if (ret != CS_OK) {
		goto reply_send;
	}

	ret = hdb_error_to_cs(hdb_handle_get(&conn_info->iter_db, handle, (void *)&hdb_iter));
	if (ret != CS_OK) {
		goto reply_send;
	}

	*hdb_iter = iter;

	(void)hdb_handle_put (&conn_info->iter_db, handle);

reply_send:
	memset(&res_lib_cmap_iter_init, 0, sizeof(res_lib_cmap_iter_init));
	res_lib_cmap_iter_init.header.size = sizeof(res_lib_cmap_iter_init);
	res_lib_cmap_iter_init.header.id = MESSAGE_RES_CMAP_ITER_INIT;
	res_lib_cmap_iter_init.header.error = ret;
	res_lib_cmap_iter_init.iter_handle = handle;

	api->ipc_response_send(conn, &res_lib_cmap_iter_init, sizeof(res_lib_cmap_iter_init));
}

static void message_handler_req_lib_cmap_iter_next(void *conn, const void *message)
{
	const struct req_lib_cmap_iter_next *req_lib_cmap_iter_next = message;
	struct res_lib_cmap_iter_next res_lib_cmap_iter_next;
	cs_error_t ret;
	icmap_iter_t *iter;
	size_t value_len;
	icmap_value_types_t type;
	const char *res = NULL;
	struct cmap_conn_info *conn_info = (struct cmap_conn_info *)api->ipc_private_data_get (conn);

	ret = hdb_error_to_cs(hdb_handle_get(&conn_info->iter_db,
				req_lib_cmap_iter_next->iter_handle, (void *)&iter));
	if (ret != CS_OK) {
		goto reply_send;
	}

	res = icmap_iter_next(*iter, &value_len, &type);
	if (res == NULL) {
		ret = CS_ERR_NO_SECTIONS;
	}

	(void)hdb_handle_put (&conn_info->iter_db, req_lib_cmap_iter_next->iter_handle);

reply_send:
	memset(&res_lib_cmap_iter_next, 0, sizeof(res_lib_cmap_iter_next));
	res_lib_cmap_iter_next.header.size = sizeof(res_lib_cmap_iter_next);
	res_lib_cmap_iter_next.header.id = MESSAGE_RES_CMAP_ITER_NEXT;
	res_lib_cmap_iter_next.header.error = ret;

	if (res != NULL) {
		res_lib_cmap_iter_next.value_len = value_len;
		res_lib_cmap_iter_next.type = type;

		memcpy(res_lib_cmap_iter_next.key_name.value, res, strlen(res));
	        res_lib_cmap_iter_next.key_name.length = strlen(res);
	}

	api->ipc_response_send(conn, &res_lib_cmap_iter_next, sizeof(res_lib_cmap_iter_next));
}

static void message_handler_req_lib_cmap_iter_finalize(void *conn, const void *message)
{
	const struct req_lib_cmap_iter_finalize *req_lib_cmap_iter_finalize = message;
	struct res_lib_cmap_iter_finalize res_lib_cmap_iter_finalize;
	cs_error_t ret;
	icmap_iter_t *iter;
	struct cmap_conn_info *conn_info = (struct cmap_conn_info *)api->ipc_private_data_get (conn);

	ret = hdb_error_to_cs(hdb_handle_get(&conn_info->iter_db,
				req_lib_cmap_iter_finalize->iter_handle, (void *)&iter));
	if (ret != CS_OK) {
		goto reply_send;
	}

	icmap_iter_finalize(*iter);

	(void)hdb_handle_destroy(&conn_info->iter_db, req_lib_cmap_iter_finalize->iter_handle);

	(void)hdb_handle_put (&conn_info->iter_db, req_lib_cmap_iter_finalize->iter_handle);

reply_send:
	memset(&res_lib_cmap_iter_finalize, 0, sizeof(res_lib_cmap_iter_finalize));
	res_lib_cmap_iter_finalize.header.size = sizeof(res_lib_cmap_iter_finalize);
	res_lib_cmap_iter_finalize.header.id = MESSAGE_RES_CMAP_ITER_FINALIZE;
	res_lib_cmap_iter_finalize.header.error = ret;

	api->ipc_response_send(conn, &res_lib_cmap_iter_finalize, sizeof(res_lib_cmap_iter_finalize));
}

static void cmap_notify_fn(int32_t event,
		const char *key_name,
		struct icmap_notify_value new_val,
		struct icmap_notify_value old_val,
		void *user_data)
{
	struct cmap_track_user_data *cmap_track_user_data = (struct cmap_track_user_data *)user_data;
	struct res_lib_cmap_notify_callback res_lib_cmap_notify_callback;
	struct iovec iov[3];

	memset(&res_lib_cmap_notify_callback, 0, sizeof(res_lib_cmap_notify_callback));

	res_lib_cmap_notify_callback.header.size = sizeof(res_lib_cmap_notify_callback) + new_val.len + old_val.len;
	res_lib_cmap_notify_callback.header.id = MESSAGE_RES_CMAP_NOTIFY_CALLBACK;
	res_lib_cmap_notify_callback.header.error = CS_OK;

	res_lib_cmap_notify_callback.new_value_type = new_val.type;
	res_lib_cmap_notify_callback.old_value_type = old_val.type;
	res_lib_cmap_notify_callback.new_value_len = new_val.len;
	res_lib_cmap_notify_callback.old_value_len = old_val.len;
	res_lib_cmap_notify_callback.event = event;
	res_lib_cmap_notify_callback.key_name.length = strlen(key_name);
	res_lib_cmap_notify_callback.track_inst_handle = cmap_track_user_data->track_inst_handle;

	memcpy(res_lib_cmap_notify_callback.key_name.value, key_name, strlen(key_name));

	iov[0].iov_base = (char *)&res_lib_cmap_notify_callback;
	iov[0].iov_len = sizeof(res_lib_cmap_notify_callback);
	iov[1].iov_base = (char *)new_val.data;
	iov[1].iov_len = new_val.len;
	iov[2].iov_base = (char *)old_val.data;
	iov[2].iov_len = old_val.len;

	api->ipc_dispatch_iov_send(cmap_track_user_data->conn, iov, 3);
}

static void message_handler_req_lib_cmap_track_add(void *conn, const void *message)
{
	const struct req_lib_cmap_track_add *req_lib_cmap_track_add = message;
	struct res_lib_cmap_track_add res_lib_cmap_track_add;
	cs_error_t ret;
	cmap_track_handle_t handle = 0;
	icmap_track_t track = NULL;
	icmap_track_t *hdb_track;
	struct cmap_track_user_data *cmap_track_user_data;
	const char *key_name;

	struct cmap_conn_info *conn_info = (struct cmap_conn_info *)api->ipc_private_data_get (conn);

	cmap_track_user_data = malloc(sizeof(*cmap_track_user_data));
	if (cmap_track_user_data == NULL) {
		ret = CS_ERR_NO_MEMORY;

		goto reply_send;
	}
	memset(cmap_track_user_data, 0, sizeof(*cmap_track_user_data));

	if (req_lib_cmap_track_add->key_name.length > 0) {
		key_name = (char *)req_lib_cmap_track_add->key_name.value;
	} else {
		key_name = NULL;
	}

	ret = icmap_track_add(key_name,
			req_lib_cmap_track_add->track_type,
			cmap_notify_fn,
			cmap_track_user_data,
			&track);
	if (ret != CS_OK) {
		free(cmap_track_user_data);

		goto reply_send;
	}

	ret = hdb_error_to_cs(hdb_handle_create(&conn_info->track_db, sizeof(track), &handle));
	if (ret != CS_OK) {
		free(cmap_track_user_data);

		goto reply_send;
	}

	ret = hdb_error_to_cs(hdb_handle_get(&conn_info->track_db, handle, (void *)&hdb_track));
	if (ret != CS_OK) {
		free(cmap_track_user_data);

		goto reply_send;
	}

	*hdb_track = track;
	cmap_track_user_data->conn = conn;
	cmap_track_user_data->track_handle = handle;
	cmap_track_user_data->track_inst_handle = req_lib_cmap_track_add->track_inst_handle;

	(void)hdb_handle_put (&conn_info->track_db, handle);

reply_send:
	memset(&res_lib_cmap_track_add, 0, sizeof(res_lib_cmap_track_add));
	res_lib_cmap_track_add.header.size = sizeof(res_lib_cmap_track_add);
	res_lib_cmap_track_add.header.id = MESSAGE_RES_CMAP_TRACK_ADD;
	res_lib_cmap_track_add.header.error = ret;
	res_lib_cmap_track_add.track_handle = handle;

	api->ipc_response_send(conn, &res_lib_cmap_track_add, sizeof(res_lib_cmap_track_add));
}

static void message_handler_req_lib_cmap_track_delete(void *conn, const void *message)
{
	const struct req_lib_cmap_track_delete *req_lib_cmap_track_delete = message;
	struct res_lib_cmap_track_delete res_lib_cmap_track_delete;
	cs_error_t ret;
	icmap_track_t *track;
	struct cmap_conn_info *conn_info = (struct cmap_conn_info *)api->ipc_private_data_get (conn);
	uint64_t track_inst_handle = 0;

	ret = hdb_error_to_cs(hdb_handle_get(&conn_info->track_db,
				req_lib_cmap_track_delete->track_handle, (void *)&track));
	if (ret != CS_OK) {
		goto reply_send;
	}

	track_inst_handle = ((struct cmap_track_user_data *)icmap_track_get_user_data(*track))->track_inst_handle;

	free(icmap_track_get_user_data(*track));

	ret = icmap_track_delete(*track);

	(void)hdb_handle_put (&conn_info->track_db, req_lib_cmap_track_delete->track_handle);
	(void)hdb_handle_destroy(&conn_info->track_db, req_lib_cmap_track_delete->track_handle);

reply_send:
	memset(&res_lib_cmap_track_delete, 0, sizeof(res_lib_cmap_track_delete));
	res_lib_cmap_track_delete.header.size = sizeof(res_lib_cmap_track_delete);
	res_lib_cmap_track_delete.header.id = MESSAGE_RES_CMAP_TRACK_DELETE;
	res_lib_cmap_track_delete.header.error = ret;
	res_lib_cmap_track_delete.track_inst_handle = track_inst_handle;

	api->ipc_response_send(conn, &res_lib_cmap_track_delete, sizeof(res_lib_cmap_track_delete));
}
