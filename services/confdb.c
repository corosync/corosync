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
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "../include/saAis.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_confdb.h"
#include "../include/mar_gen.h"
#include "../lcr/lcr_comp.h"
#include "../exec/logsys.h"
#include "../include/coroapi.h"

LOGSYS_DECLARE_SUBSYS ("CONFDB", LOG_INFO);

static struct corosync_api_v1 *api;

static int confdb_exec_init_fn (
	struct corosync_api_v1 *corosync_api);

static int confdb_lib_init_fn (void *conn);
static int confdb_lib_exit_fn (void *conn);

static void message_handler_req_lib_confdb_object_create (void *conn, void *message);
static void message_handler_req_lib_confdb_object_destroy (void *conn, void *message);

static void message_handler_req_lib_confdb_key_create (void *conn, void *message);
static void message_handler_req_lib_confdb_key_get (void *conn, void *message);
static void message_handler_req_lib_confdb_key_replace (void *conn, void *message);
static void message_handler_req_lib_confdb_key_delete (void *conn, void *message);
static void message_handler_req_lib_confdb_key_iter (void *conn, void *message);

static void message_handler_req_lib_confdb_object_iter (void *conn, void *message);
static void message_handler_req_lib_confdb_object_find (void *conn, void *message);

static void message_handler_req_lib_confdb_object_parent_get (void *conn, void *message);
static void message_handler_req_lib_confdb_write (void *conn, void *message);

static void message_handler_req_lib_confdb_track_start (void *conn, void *message);
static void message_handler_req_lib_confdb_track_stop (void *conn, void *message);


/*
 * Library Handler Definition
 */
static struct corosync_lib_handler confdb_lib_engine[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_create,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_CREATE,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_destroy,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_DESTROY,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_find,
		.response_size				= sizeof (struct res_lib_confdb_object_find),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_FIND,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_create,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_KEY_CREATE,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_get,
		.response_size				= sizeof (struct res_lib_confdb_key_get),
		.response_id				= MESSAGE_RES_CONFDB_KEY_GET,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_replace,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_KEY_REPLACE,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_delete,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_KEY_DELETE,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_iter,
		.response_size				= sizeof (struct res_lib_confdb_object_iter),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_ITER,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_parent_get,
		.response_size				= sizeof (struct res_lib_confdb_object_parent_get),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_PARENT_GET,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_iter,
		.response_size				= sizeof (struct res_lib_confdb_key_iter),
		.response_id				= MESSAGE_RES_CONFDB_KEY_ITER,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn				= message_handler_req_lib_confdb_track_start,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_TRACK_START,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn				= message_handler_req_lib_confdb_track_stop,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_TRACK_START,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn				= message_handler_req_lib_confdb_write,
		.response_size				= sizeof (struct res_lib_confdb_write),
		.response_id				= MESSAGE_RES_CONFDB_WRITE,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
};


struct corosync_service_engine confdb_service_engine = {
	.name				        = "corosync cluster config database access v1.01",
	.id					= CONFDB_SERVICE,
	.private_data_size			= 0,
	.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn				= confdb_lib_init_fn,
	.lib_exit_fn				= confdb_lib_exit_fn,
	.lib_engine				= confdb_lib_engine,
	.lib_engine_count			= sizeof (confdb_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn				= confdb_exec_init_fn,
};

/*
 * Dynamic loader definition
 */
static struct corosync_service_engine *confdb_get_service_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 confdb_service_engine_iface = {
	.corosync_get_service_engine_ver0		= confdb_get_service_engine_ver0
};

static struct lcr_iface corosync_confdb_ver0[1] = {
	{
		.name				= "corosync_confdb",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count         = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL
	}
};

static struct lcr_comp confdb_comp_ver0 = {
	.iface_count			= 1,
	.ifaces			        = corosync_confdb_ver0
};


static struct corosync_service_engine *confdb_get_service_engine_ver0 (void)
{
	return (&confdb_service_engine);
}

__attribute__ ((constructor)) static void confdb_comp_register (void) {
        lcr_interfaces_set (&corosync_confdb_ver0[0], &confdb_service_engine_iface);

	lcr_component_register (&confdb_comp_ver0);
}

static int confdb_exec_init_fn (
	struct corosync_api_v1 *corosync_api)
{
	api = corosync_api;
	return 0;
}

static int confdb_lib_init_fn (void *conn)
{
	log_printf(LOG_LEVEL_DEBUG, "lib_init_fn: conn=%p\n", conn);
	return (0);
}

static int confdb_lib_exit_fn (void *conn)
{

	log_printf(LOG_LEVEL_DEBUG, "exit_fn for conn=%p\n", conn);
	return (0);
}

static void message_handler_req_lib_confdb_object_create (void *conn, void *message)
{
	struct req_lib_confdb_object_create *req_lib_confdb_object_create = (struct req_lib_confdb_object_create *)message;
	struct res_lib_confdb_object_create res_lib_confdb_object_create;
	unsigned int object_handle;
	int ret = SA_AIS_OK;

	if (api->object_create(req_lib_confdb_object_create->parent_object_handle,
					&object_handle,
					req_lib_confdb_object_create->object_name.value,
					req_lib_confdb_object_create->object_name.length))
		ret = SA_AIS_ERR_ACCESS;

	res_lib_confdb_object_create.object_handle = object_handle;
	res_lib_confdb_object_create.header.size = sizeof(res_lib_confdb_object_create);
	res_lib_confdb_object_create.header.id = MESSAGE_RES_CONFDB_OBJECT_CREATE;
	res_lib_confdb_object_create.header.error = ret;
	api->ipc_conn_send_response(conn, &res_lib_confdb_object_create, sizeof(res_lib_confdb_object_create));
}

static void message_handler_req_lib_confdb_object_destroy (void *conn, void *message)
{
	struct req_lib_confdb_object_destroy *req_lib_confdb_object_destroy = (struct req_lib_confdb_object_destroy *)message;
	mar_res_header_t res;
	int ret = SA_AIS_OK;

	if (api->object_destroy(req_lib_confdb_object_destroy->object_handle))
		ret = SA_AIS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_OBJECT_CREATE;
	res.error = ret;
	api->ipc_conn_send_response(conn, &res, sizeof(res));
}


static void message_handler_req_lib_confdb_key_create (void *conn, void *message)
{
	struct req_lib_confdb_key_create *req_lib_confdb_key_create = (struct req_lib_confdb_key_create *)message;
	mar_res_header_t res;
	int ret = SA_AIS_OK;

	if (api->object_key_create(req_lib_confdb_key_create->object_handle,
					    req_lib_confdb_key_create->key_name.value,
					    req_lib_confdb_key_create->key_name.length,
					    req_lib_confdb_key_create->value.value,
					    req_lib_confdb_key_create->value.length))
		ret = SA_AIS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_CREATE;
	res.error = ret;
	api->ipc_conn_send_response(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_key_get (void *conn, void *message)
{
	struct req_lib_confdb_key_get *req_lib_confdb_key_get = (struct req_lib_confdb_key_get *)message;
	struct res_lib_confdb_key_get res_lib_confdb_key_get;
	int value_len;
	void *value;
	int ret = SA_AIS_OK;

	if (api->object_key_get(req_lib_confdb_key_get->parent_object_handle,
					 req_lib_confdb_key_get->key_name.value,
					 req_lib_confdb_key_get->key_name.length,
					 &value,
					 &value_len))
		ret = SA_AIS_ERR_ACCESS;
	else {
		memcpy(res_lib_confdb_key_get.value.value, value, value_len);
		res_lib_confdb_key_get.value.length = value_len;

	}
	res_lib_confdb_key_get.header.size = sizeof(res_lib_confdb_key_get);
	res_lib_confdb_key_get.header.id = MESSAGE_RES_CONFDB_KEY_GET;
	res_lib_confdb_key_get.header.error = ret;
	api->ipc_conn_send_response(conn, &res_lib_confdb_key_get, sizeof(res_lib_confdb_key_get));
}

static void message_handler_req_lib_confdb_key_replace (void *conn, void *message)
{
	struct req_lib_confdb_key_replace *req_lib_confdb_key_replace = (struct req_lib_confdb_key_replace *)message;
	mar_res_header_t res;
	int ret = SA_AIS_OK;

	if (api->object_key_replace(req_lib_confdb_key_replace->object_handle,
					     req_lib_confdb_key_replace->key_name.value,
					     req_lib_confdb_key_replace->key_name.length,
					     req_lib_confdb_key_replace->old_value.value,
					     req_lib_confdb_key_replace->old_value.length,
					     req_lib_confdb_key_replace->new_value.value,
					     req_lib_confdb_key_replace->new_value.length))
		ret = SA_AIS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_REPLACE;
	res.error = ret;
	api->ipc_conn_send_response(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_key_delete (void *conn, void *message)
{
	struct req_lib_confdb_key_delete *req_lib_confdb_key_delete = (struct req_lib_confdb_key_delete *)message;
	mar_res_header_t res;
	int ret = SA_AIS_OK;

	if (api->object_key_delete(req_lib_confdb_key_delete->object_handle,
					    req_lib_confdb_key_delete->key_name.value,
					    req_lib_confdb_key_delete->key_name.length,
					    req_lib_confdb_key_delete->value.value,
					    req_lib_confdb_key_delete->value.length))
		ret = SA_AIS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_DELETE;
	res.error = ret;
	api->ipc_conn_send_response(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_object_parent_get (void *conn, void *message)
{
	struct req_lib_confdb_object_parent_get *req_lib_confdb_object_parent_get = (struct req_lib_confdb_object_parent_get *)message;
	struct res_lib_confdb_object_parent_get res_lib_confdb_object_parent_get;
	unsigned int object_handle;
	int ret = SA_AIS_OK;

	if (api->object_parent_get(req_lib_confdb_object_parent_get->object_handle,
					    &object_handle))
		ret = SA_AIS_ERR_ACCESS;

	res_lib_confdb_object_parent_get.parent_object_handle = object_handle;
	res_lib_confdb_object_parent_get.header.size = sizeof(res_lib_confdb_object_parent_get);
	res_lib_confdb_object_parent_get.header.id = MESSAGE_RES_CONFDB_OBJECT_CREATE;
	res_lib_confdb_object_parent_get.header.error = ret;
	api->ipc_conn_send_response(conn, &res_lib_confdb_object_parent_get, sizeof(res_lib_confdb_object_parent_get));
}


static void message_handler_req_lib_confdb_key_iter (void *conn, void *message)
{
	struct req_lib_confdb_key_iter *req_lib_confdb_key_iter = (struct req_lib_confdb_key_iter *)message;
	struct res_lib_confdb_key_iter res_lib_confdb_key_iter;
	void *key_name;
	int key_name_len;
	void *value;
	int value_len;
	int ret = SA_AIS_OK;

	if (api->object_key_iter_from(req_lib_confdb_key_iter->parent_object_handle,
					       req_lib_confdb_key_iter->next_entry,
					       &key_name,
					       &key_name_len,
					       &value,
					       &value_len))
		ret = SA_AIS_ERR_ACCESS;
	else {
		memcpy(res_lib_confdb_key_iter.key_name.value, key_name, key_name_len);
		memcpy(res_lib_confdb_key_iter.value.value, value, value_len);
		res_lib_confdb_key_iter.key_name.length = key_name_len;
		res_lib_confdb_key_iter.value.length = value_len;
	}
	res_lib_confdb_key_iter.header.size = sizeof(res_lib_confdb_key_iter);
	res_lib_confdb_key_iter.header.id = MESSAGE_RES_CONFDB_KEY_ITER;
	res_lib_confdb_key_iter.header.error = ret;

	api->ipc_conn_send_response(conn, &res_lib_confdb_key_iter, sizeof(res_lib_confdb_key_iter));
}

static void message_handler_req_lib_confdb_object_iter (void *conn, void *message)
{
	struct req_lib_confdb_object_iter *req_lib_confdb_object_iter = (struct req_lib_confdb_object_iter *)message;
	struct res_lib_confdb_object_iter res_lib_confdb_object_iter;
	void *object_name;
	int object_name_len;
	int ret = SA_AIS_OK;

	if (api->object_iter_from(req_lib_confdb_object_iter->parent_object_handle,
					   req_lib_confdb_object_iter->next_entry,
					   &object_name,
					   &object_name_len,
					   &res_lib_confdb_object_iter.object_handle))
		ret = SA_AIS_ERR_ACCESS;
	else {
		res_lib_confdb_object_iter.object_name.length = object_name_len;
		memcpy(res_lib_confdb_object_iter.object_name.value, object_name, object_name_len);
	}
	res_lib_confdb_object_iter.header.size = sizeof(res_lib_confdb_object_iter);
	res_lib_confdb_object_iter.header.id = MESSAGE_RES_CONFDB_OBJECT_ITER;
	res_lib_confdb_object_iter.header.error = ret;

	api->ipc_conn_send_response(conn, &res_lib_confdb_object_iter, sizeof(res_lib_confdb_object_iter));
}

static void message_handler_req_lib_confdb_object_find (void *conn, void *message)
{
	struct req_lib_confdb_object_find *req_lib_confdb_object_find = (struct req_lib_confdb_object_find *)message;
	struct res_lib_confdb_object_find res_lib_confdb_object_find;
	int ret = SA_AIS_OK;

	if (api->object_find_from(req_lib_confdb_object_find->parent_object_handle,
					   req_lib_confdb_object_find->next_entry,
					   req_lib_confdb_object_find->object_name.value,
					   req_lib_confdb_object_find->object_name.length,
					   &res_lib_confdb_object_find.object_handle,
					   &res_lib_confdb_object_find.next_entry))
		ret = SA_AIS_ERR_ACCESS;

	res_lib_confdb_object_find.header.size = sizeof(res_lib_confdb_object_find);
	res_lib_confdb_object_find.header.id = MESSAGE_RES_CONFDB_OBJECT_FIND;
	res_lib_confdb_object_find.header.error = ret;

	api->ipc_conn_send_response(conn, &res_lib_confdb_object_find, sizeof(res_lib_confdb_object_find));
}

static void message_handler_req_lib_confdb_write (void *conn, void *message)
{
	struct res_lib_confdb_write res_lib_confdb_write;
	int ret = SA_AIS_OK;
	char *error_string = NULL;

	if (api->object_write_config(&error_string))
		ret = SA_AIS_ERR_ACCESS;

	res_lib_confdb_write.header.size = sizeof(res_lib_confdb_write);
	res_lib_confdb_write.header.id = MESSAGE_RES_CONFDB_WRITE;
	res_lib_confdb_write.header.error = ret;
	if (error_string) {
		strcpy((char *)res_lib_confdb_write.error.value, error_string);
		res_lib_confdb_write.error.length = strlen(error_string) + 1;
	} else
		res_lib_confdb_write.error.length = 0;

	api->ipc_conn_send_response(conn, &res_lib_confdb_write, sizeof(res_lib_confdb_write));
}

/* TODO: when we have notification in the objdb. */
static void message_handler_req_lib_confdb_track_start (void *conn, void *message)
{
	mar_res_header_t res;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_TRACK_START;
	res.error = SA_AIS_ERR_NOT_SUPPORTED;
	api->ipc_conn_send_response(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_track_stop (void *conn, void *message)
{
	mar_res_header_t res;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_TRACK_STOP;
	res.error = SA_AIS_ERR_NOT_SUPPORTED;
	api->ipc_conn_send_response(conn, &res, sizeof(res));
}


