/*
 * Copyright (c) 2008-2010 Red Hat, Inc.
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

#include <config.h>

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/corodefs.h>
#include <corosync/cfg.h>
#include <corosync/list.h>
#include <corosync/mar_gen.h>
#include <corosync/ipc_confdb.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/logsys.h>
#include <corosync/engine/coroapi.h>

LOGSYS_DECLARE_SUBSYS ("CONFDB");

static hdb_handle_t *
m2h (mar_uint64_t *m)
{
	/* FIXME enable the following when/if we use gnulib:
	   (it's a compile-time assertion; i.e., zero run-time cost)
	   verify (sizeof (*m) == sizeof (hdb_handle_t)); */
	return (void *) m;
}

static struct corosync_api_v1 *api;

static int confdb_exec_init_fn (
	struct corosync_api_v1 *corosync_api);

static int confdb_lib_init_fn (void *conn);
static int confdb_lib_exit_fn (void *conn);

static void message_handler_req_lib_confdb_object_create (void *conn,
							  const void *message);
static void message_handler_req_lib_confdb_object_destroy (void *conn,
							   const void *message);
static void message_handler_req_lib_confdb_object_find_destroy (void *conn,
								const void *message);

static void message_handler_req_lib_confdb_key_create (void *conn,
								const void *message);
static void message_handler_req_lib_confdb_key_create_typed (void *conn,
								const void *message);
static void message_handler_req_lib_confdb_key_get (void *conn,
								const void *message);
static void message_handler_req_lib_confdb_key_get_typed (void *conn,
						    const void *message);
static void message_handler_req_lib_confdb_key_replace (void *conn,
							const void *message);
static void message_handler_req_lib_confdb_key_delete (void *conn,
						       const void *message);
static void message_handler_req_lib_confdb_key_iter (void *conn,
								const void *message);
static void message_handler_req_lib_confdb_key_iter_typed (void *conn,
						     const void *message);

static void message_handler_req_lib_confdb_key_increment (void *conn,
							  const void *message);
static void message_handler_req_lib_confdb_key_decrement (void *conn,
							  const void *message);

static void message_handler_req_lib_confdb_object_iter (void *conn,
							const void *message);
static void message_handler_req_lib_confdb_object_find (void *conn,
							const void *message);

static void message_handler_req_lib_confdb_object_parent_get (void *conn,
							      const void *message);
static void message_handler_req_lib_confdb_write (void *conn,
						  const void *message);
static void message_handler_req_lib_confdb_reload (void *conn,
						   const void *message);

static void message_handler_req_lib_confdb_track_start (void *conn,
							const void *message);
static void message_handler_req_lib_confdb_track_stop (void *conn,
						       const void *message);

static void confdb_notify_lib_of_key_change(
	object_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t object_name_len,
	const void *key_name_pt, size_t key_name_len,
	const void *key_value_pt, size_t key_value_len,
	void *priv_data_pt);

static void confdb_notify_lib_of_new_object(
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt);

static void confdb_notify_lib_of_destroyed_object(
	hdb_handle_t parent_object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt);

static void confdb_notify_lib_of_reload(
	objdb_reload_notify_type_t notify_type,
	int flush,
	void *priv_data_pt);

/*
 * Library Handler Definition
 */
static struct corosync_lib_handler confdb_lib_engine[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_create,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_destroy,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_find,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_create,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_get,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_replace,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_delete,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_iter,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_parent_get,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_iter,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn				= message_handler_req_lib_confdb_track_start,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn				= message_handler_req_lib_confdb_track_stop,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn				= message_handler_req_lib_confdb_write,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn				= message_handler_req_lib_confdb_reload,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 14 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_find_destroy,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 15 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_increment,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 16 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_decrement,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 17 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_create_typed,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 18 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_get_typed,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 19 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_iter_typed,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
};


struct corosync_service_engine confdb_service_engine = {
	.name				        = "corosync cluster config database access v1.01",
	.id					= CONFDB_SERVICE,
	.priority				= 1,
	.private_data_size			= 0,
	.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
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

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
        lcr_interfaces_set (&corosync_confdb_ver0[0], &confdb_service_engine_iface);

	lcr_component_register (&confdb_comp_ver0);
}

static int confdb_exec_init_fn (
	struct corosync_api_v1 *corosync_api)
{
#ifdef COROSYNC_SOLARIS
	logsys_subsys_init();
#endif
	api = corosync_api;
	return 0;
}

static int confdb_lib_init_fn (void *conn)
{
	log_printf(LOGSYS_LEVEL_DEBUG, "lib_init_fn: conn=%p\n", conn);
	return (0);
}

static int confdb_lib_exit_fn (void *conn)
{
	log_printf(LOGSYS_LEVEL_DEBUG, "exit_fn for conn=%p\n", conn);
	/* cleanup the object trackers for this client. */
	api->object_track_stop(confdb_notify_lib_of_key_change,
		confdb_notify_lib_of_new_object,
		confdb_notify_lib_of_destroyed_object,
		NULL,
		conn);
	return (0);
}

static void message_handler_req_lib_confdb_object_create (void *conn,
							  const void *message)
{
	const struct req_lib_confdb_object_create *req_lib_confdb_object_create
	  = message;
	struct res_lib_confdb_object_create res_lib_confdb_object_create;
	hdb_handle_t object_handle;
	int ret = CS_OK;

	if (api->object_create(req_lib_confdb_object_create->parent_object_handle,
					&object_handle,
					req_lib_confdb_object_create->object_name.value,
					req_lib_confdb_object_create->object_name.length))
		ret = CS_ERR_ACCESS;

	res_lib_confdb_object_create.object_handle = object_handle;
	res_lib_confdb_object_create.header.size = sizeof(res_lib_confdb_object_create);
	res_lib_confdb_object_create.header.id = MESSAGE_RES_CONFDB_OBJECT_CREATE;
	res_lib_confdb_object_create.header.error = ret;
	api->ipc_response_send(conn, &res_lib_confdb_object_create, sizeof(res_lib_confdb_object_create));
}

static void message_handler_req_lib_confdb_object_destroy (void *conn,
							   const void *message)
{
	const struct req_lib_confdb_object_destroy *req_lib_confdb_object_destroy
	  = message;
	coroipc_response_header_t res;
	int ret = CS_OK;

	if (api->object_destroy(req_lib_confdb_object_destroy->object_handle))
		ret = CS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_OBJECT_DESTROY;
	res.error = ret;
	api->ipc_response_send(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_object_find_destroy (void *conn,
								const void *message)
{
	const struct req_lib_confdb_object_find_destroy
	  *req_lib_confdb_object_find_destroy = message;
	coroipc_response_header_t res;
	int ret = CS_OK;

	if (api->object_find_destroy(req_lib_confdb_object_find_destroy->find_handle))
		ret = CS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_OBJECT_FIND_DESTROY;
	res.error = ret;
	api->ipc_response_send(conn, &res, sizeof(res));
}


static void message_handler_req_lib_confdb_key_create (void *conn,
						       const void *message)
{
	const struct req_lib_confdb_key_create *req_lib_confdb_key_create
	  = message;
	coroipc_response_header_t res;
	int ret = CS_OK;

	if (api->object_key_create(req_lib_confdb_key_create->object_handle,
					    req_lib_confdb_key_create->key_name.value,
					    req_lib_confdb_key_create->key_name.length,
					    req_lib_confdb_key_create->value.value,
					    req_lib_confdb_key_create->value.length))
		ret = CS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_CREATE;
	res.error = ret;
	api->ipc_response_send(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_key_create_typed (void *conn,
						       const void *message)
{
	const struct req_lib_confdb_key_create_typed *req_lib_confdb_key_create
	  = message;
	coroipc_response_header_t res;
	int ret = CS_OK;

	if (api->object_key_create_typed(req_lib_confdb_key_create->object_handle,
					    (char*)req_lib_confdb_key_create->key_name.value,
					    req_lib_confdb_key_create->value.value,
					    req_lib_confdb_key_create->value.length,
					    req_lib_confdb_key_create->type))
		ret = CS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_CREATE;
	res.error = ret;
	api->ipc_response_send(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_key_get (void *conn,
						    const void *message)
{
	const struct req_lib_confdb_key_get *req_lib_confdb_key_get = message;
	struct res_lib_confdb_key_get res_lib_confdb_key_get;
	size_t value_len;
	void *value;
	int ret = CS_OK;

	if (api->object_key_get(req_lib_confdb_key_get->parent_object_handle,
					 req_lib_confdb_key_get->key_name.value,
					 req_lib_confdb_key_get->key_name.length,
					 &value,
					 &value_len))
		ret = CS_ERR_ACCESS;
	else {
		memcpy(res_lib_confdb_key_get.value.value, value, value_len);
		res_lib_confdb_key_get.value.length = value_len;

	}
	res_lib_confdb_key_get.header.size = sizeof(res_lib_confdb_key_get);
	res_lib_confdb_key_get.header.id = MESSAGE_RES_CONFDB_KEY_GET;
	res_lib_confdb_key_get.header.error = ret;
	api->ipc_response_send(conn, &res_lib_confdb_key_get, sizeof(res_lib_confdb_key_get));
}

static void message_handler_req_lib_confdb_key_get_typed (void *conn,
						    const void *message)
{
	const struct req_lib_confdb_key_get *req_lib_confdb_key_get = message;
	struct res_lib_confdb_key_get_typed res_lib_confdb_key_get;
	size_t value_len;
	void *value;
	int ret = CS_OK;
	objdb_value_types_t type;
	char * key_name = (char*)req_lib_confdb_key_get->key_name.value;
	key_name[req_lib_confdb_key_get->key_name.length] = '\0';

	if (api->object_key_get_typed(req_lib_confdb_key_get->parent_object_handle,
					 key_name,
					 &value,
					 &value_len, &type))
		ret = CS_ERR_ACCESS;
	else {
		memcpy(res_lib_confdb_key_get.value.value, value, value_len);
		res_lib_confdb_key_get.value.length = value_len;
		res_lib_confdb_key_get.type = type;

	}
	res_lib_confdb_key_get.header.size = sizeof(res_lib_confdb_key_get);
	res_lib_confdb_key_get.header.id = MESSAGE_RES_CONFDB_KEY_GET_TYPED;
	res_lib_confdb_key_get.header.error = ret;
	api->ipc_response_send(conn, &res_lib_confdb_key_get, sizeof(res_lib_confdb_key_get));
}

static void message_handler_req_lib_confdb_key_increment (void *conn,
							  const void *message)
{
	const struct req_lib_confdb_key_get *req_lib_confdb_key_get = message;
	struct res_lib_confdb_key_incdec res_lib_confdb_key_incdec;
	int ret = CS_OK;

	if (api->object_key_increment(req_lib_confdb_key_get->parent_object_handle,
				      req_lib_confdb_key_get->key_name.value,
				      req_lib_confdb_key_get->key_name.length,
				      &res_lib_confdb_key_incdec.value))
		ret = CS_ERR_ACCESS;

	res_lib_confdb_key_incdec.header.size = sizeof(res_lib_confdb_key_incdec);
	res_lib_confdb_key_incdec.header.id = MESSAGE_RES_CONFDB_KEY_INCREMENT;
	res_lib_confdb_key_incdec.header.error = ret;
	api->ipc_response_send(conn, &res_lib_confdb_key_incdec, sizeof(res_lib_confdb_key_incdec));
}

static void message_handler_req_lib_confdb_key_decrement (void *conn,
							  const void *message)
{
	const struct req_lib_confdb_key_get *req_lib_confdb_key_get = message;
	struct res_lib_confdb_key_incdec res_lib_confdb_key_incdec;
	int ret = CS_OK;

	if (api->object_key_decrement(req_lib_confdb_key_get->parent_object_handle,
				      req_lib_confdb_key_get->key_name.value,
				      req_lib_confdb_key_get->key_name.length,
				      &res_lib_confdb_key_incdec.value))
		ret = CS_ERR_ACCESS;

	res_lib_confdb_key_incdec.header.size = sizeof(res_lib_confdb_key_incdec);
	res_lib_confdb_key_incdec.header.id = MESSAGE_RES_CONFDB_KEY_DECREMENT;
	res_lib_confdb_key_incdec.header.error = ret;
	api->ipc_response_send(conn, &res_lib_confdb_key_incdec, sizeof(res_lib_confdb_key_incdec));
}

static void message_handler_req_lib_confdb_key_replace (void *conn,
							const void *message)
{
	const struct req_lib_confdb_key_replace *req_lib_confdb_key_replace
	  = message;
	coroipc_response_header_t res;
	int ret = CS_OK;

	if (api->object_key_replace(req_lib_confdb_key_replace->object_handle,
					     req_lib_confdb_key_replace->key_name.value,
					     req_lib_confdb_key_replace->key_name.length,
					     req_lib_confdb_key_replace->new_value.value,
					     req_lib_confdb_key_replace->new_value.length))
		ret = CS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_REPLACE;
	res.error = ret;
	api->ipc_response_send(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_key_delete (void *conn,
						       const void *message)
{
	const struct req_lib_confdb_key_delete *req_lib_confdb_key_delete
	  = message;
	coroipc_response_header_t res;
	int ret = CS_OK;

	if (api->object_key_delete(req_lib_confdb_key_delete->object_handle,
					    req_lib_confdb_key_delete->key_name.value,
				   req_lib_confdb_key_delete->key_name.length))
		ret = CS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_DELETE;
	res.error = ret;
	api->ipc_response_send(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_object_parent_get (void *conn,
							      const void *message)
{
	const struct req_lib_confdb_object_parent_get
	  *req_lib_confdb_object_parent_get = message;
	struct res_lib_confdb_object_parent_get res_lib_confdb_object_parent_get;
	hdb_handle_t object_handle;
	int ret = CS_OK;

	if (api->object_parent_get(req_lib_confdb_object_parent_get->object_handle,
					    &object_handle))
		ret = CS_ERR_ACCESS;

	res_lib_confdb_object_parent_get.parent_object_handle = object_handle;
	res_lib_confdb_object_parent_get.header.size = sizeof(res_lib_confdb_object_parent_get);
	res_lib_confdb_object_parent_get.header.id = MESSAGE_RES_CONFDB_OBJECT_CREATE;
	res_lib_confdb_object_parent_get.header.error = ret;
	api->ipc_response_send(conn, &res_lib_confdb_object_parent_get, sizeof(res_lib_confdb_object_parent_get));
}


static void message_handler_req_lib_confdb_key_iter (void *conn,
						     const void *message)
{
	const struct req_lib_confdb_key_iter *req_lib_confdb_key_iter = message;
	struct res_lib_confdb_key_iter res_lib_confdb_key_iter;
	void *key_name;
	size_t key_name_len;
	void *value;
	size_t value_len;
	int ret = CS_OK;

	if (api->object_key_iter_from(req_lib_confdb_key_iter->parent_object_handle,
					       req_lib_confdb_key_iter->next_entry,
					       &key_name,
					       &key_name_len,
					       &value,
					       &value_len))
		ret = CS_ERR_ACCESS;
	else {
		memcpy(res_lib_confdb_key_iter.key_name.value, key_name, key_name_len);
		memcpy(res_lib_confdb_key_iter.value.value, value, value_len);
		res_lib_confdb_key_iter.key_name.length = key_name_len;
		res_lib_confdb_key_iter.value.length = value_len;
	}
	res_lib_confdb_key_iter.header.size = sizeof(res_lib_confdb_key_iter);
	res_lib_confdb_key_iter.header.id = MESSAGE_RES_CONFDB_KEY_ITER;
	res_lib_confdb_key_iter.header.error = ret;

	api->ipc_response_send(conn, &res_lib_confdb_key_iter, sizeof(res_lib_confdb_key_iter));
}

static void message_handler_req_lib_confdb_key_iter_typed (void *conn,
						     const void *message)
{
	const struct req_lib_confdb_key_iter *req_lib_confdb_key_iter = message;
	struct res_lib_confdb_key_iter_typed res_lib_confdb_key_iter;
	void *key_name;
	size_t key_name_len;
	void *value;
	size_t value_len;
	int ret = CS_OK;
	objdb_value_types_t my_type;

	if (api->object_key_iter_from(req_lib_confdb_key_iter->parent_object_handle,
					       req_lib_confdb_key_iter->next_entry,
					       &key_name,
					       &key_name_len,
					       &value,
					       &value_len))
		ret = CS_ERR_ACCESS;
	else {
		memcpy(res_lib_confdb_key_iter.key_name.value, key_name, key_name_len);
		memcpy(res_lib_confdb_key_iter.value.value, value, value_len);
		res_lib_confdb_key_iter.key_name.length = key_name_len;
		res_lib_confdb_key_iter.key_name.value[key_name_len] = '\0';
		res_lib_confdb_key_iter.value.length = value_len;
		api->object_key_get_typed(req_lib_confdb_key_iter->parent_object_handle,
								(const char*)res_lib_confdb_key_iter.key_name.value,
								&value,
								&value_len,
								&my_type);
		res_lib_confdb_key_iter.type = my_type;
	}
	res_lib_confdb_key_iter.header.size = sizeof(res_lib_confdb_key_iter);
	res_lib_confdb_key_iter.header.id = MESSAGE_RES_CONFDB_KEY_ITER_TYPED;
	res_lib_confdb_key_iter.header.error = ret;

	api->ipc_response_send(conn, &res_lib_confdb_key_iter, sizeof(res_lib_confdb_key_iter));
}

static void message_handler_req_lib_confdb_object_iter (void *conn,
							const void *message)
{
	const struct req_lib_confdb_object_iter *req_lib_confdb_object_iter
	  = message;
	struct res_lib_confdb_object_iter res_lib_confdb_object_iter;
	size_t object_name_len;
	int ret = CS_OK;

	if (!req_lib_confdb_object_iter->find_handle) {
		api->object_find_create(req_lib_confdb_object_iter->parent_object_handle,
					NULL, 0,
					m2h(&res_lib_confdb_object_iter.find_handle));
	}
	else
		res_lib_confdb_object_iter.find_handle = req_lib_confdb_object_iter->find_handle;

	if (api->object_find_next(res_lib_confdb_object_iter.find_handle,
				  m2h(&res_lib_confdb_object_iter.object_handle))) {
		ret = CS_ERR_ACCESS;
		api->object_find_destroy(res_lib_confdb_object_iter.find_handle);
	}
	else {
		api->object_name_get(res_lib_confdb_object_iter.object_handle,
				     (char *)res_lib_confdb_object_iter.object_name.value,
				     &object_name_len);

		res_lib_confdb_object_iter.object_name.length = object_name_len;
	}
	res_lib_confdb_object_iter.header.size = sizeof(res_lib_confdb_object_iter);
	res_lib_confdb_object_iter.header.id = MESSAGE_RES_CONFDB_OBJECT_ITER;
	res_lib_confdb_object_iter.header.error = ret;

	api->ipc_response_send(conn, &res_lib_confdb_object_iter, sizeof(res_lib_confdb_object_iter));
}

static void message_handler_req_lib_confdb_object_find (void *conn,
							const void *message)
{
	const struct req_lib_confdb_object_find *req_lib_confdb_object_find
	  = message;
	struct res_lib_confdb_object_find res_lib_confdb_object_find;
	int ret = CS_OK;

	if (!req_lib_confdb_object_find->find_handle) {
		api->object_find_create(req_lib_confdb_object_find->parent_object_handle,
					req_lib_confdb_object_find->object_name.value,
					req_lib_confdb_object_find->object_name.length,
					m2h(&res_lib_confdb_object_find.find_handle));
	}
	else
		res_lib_confdb_object_find.find_handle = req_lib_confdb_object_find->find_handle;

	if (api->object_find_next(res_lib_confdb_object_find.find_handle,
				  m2h(&res_lib_confdb_object_find.object_handle))) {
		ret = CS_ERR_ACCESS;
		api->object_find_destroy(res_lib_confdb_object_find.find_handle);
	}

	res_lib_confdb_object_find.header.size = sizeof(res_lib_confdb_object_find);
	res_lib_confdb_object_find.header.id = MESSAGE_RES_CONFDB_OBJECT_FIND;
	res_lib_confdb_object_find.header.error = ret;


	api->ipc_response_send(conn, &res_lib_confdb_object_find, sizeof(res_lib_confdb_object_find));
}

static void message_handler_req_lib_confdb_write (void *conn,
						  const void *message)
{
	struct res_lib_confdb_write res_lib_confdb_write;
	int ret = CS_OK;
	const char *error_string = NULL;

	if (api->object_write_config(&error_string))
		ret = CS_ERR_ACCESS;

	res_lib_confdb_write.header.size = sizeof(res_lib_confdb_write);
	res_lib_confdb_write.header.id = MESSAGE_RES_CONFDB_WRITE;
	res_lib_confdb_write.header.error = ret;
	if (error_string) {
		strcpy((char *)res_lib_confdb_write.error.value, error_string);
		res_lib_confdb_write.error.length = strlen(error_string) + 1;
	} else
		res_lib_confdb_write.error.length = 0;

	api->ipc_response_send(conn, &res_lib_confdb_write, sizeof(res_lib_confdb_write));
}

static void message_handler_req_lib_confdb_reload (void *conn,
						   const void *message)
{
	const struct req_lib_confdb_reload *req_lib_confdb_reload = message;
	struct res_lib_confdb_reload res_lib_confdb_reload;
	int ret = CS_OK;
	const char *error_string = NULL;

	if (api->object_reload_config(req_lib_confdb_reload->flush, &error_string))
		ret = CS_ERR_ACCESS;

	res_lib_confdb_reload.header.size = sizeof(res_lib_confdb_reload);
	res_lib_confdb_reload.header.id = MESSAGE_RES_CONFDB_RELOAD;
	res_lib_confdb_reload.header.error = ret;

	if(error_string) {
		strcpy((char *)res_lib_confdb_reload.error.value, error_string);
		res_lib_confdb_reload.error.length = strlen(error_string) + 1;
	} else
		res_lib_confdb_reload.error.length = 0;

	api->ipc_response_send(conn, &res_lib_confdb_reload, sizeof(res_lib_confdb_reload));
}

static void confdb_notify_lib_of_key_change(object_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t object_name_len,
	const void *key_name_pt, size_t key_name_len,
	const void *key_value_pt, size_t key_value_len,
	void *priv_data_pt)
{
	struct res_lib_confdb_key_change_callback res;

	res.header.size = sizeof(res);
	res.header.id = MESSAGE_RES_CONFDB_KEY_CHANGE_CALLBACK;
	res.header.error = CS_OK;
// handle & type
	res.change_type = change_type;
	res.parent_object_handle = parent_object_handle;
	res.object_handle = object_handle;
//object
	memcpy(res.object_name.value, object_name_pt, object_name_len);
	res.object_name.length = object_name_len;
//key name
	memcpy(res.key_name.value, key_name_pt, key_name_len);
	res.key_name.length = key_name_len;
//key value
	memcpy(res.key_value.value, key_value_pt, key_value_len);
	res.key_value.length = key_value_len;

	api->ipc_dispatch_send(priv_data_pt, &res, sizeof(res));
}

static void confdb_notify_lib_of_new_object(hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt)
{
	struct res_lib_confdb_object_create_callback res;

	res.header.size = sizeof(res);
	res.header.id = MESSAGE_RES_CONFDB_OBJECT_CREATE_CALLBACK;
	res.header.error = CS_OK;
	res.parent_object_handle = parent_object_handle;
	res.object_handle = object_handle;
	memcpy(res.name.value, name_pt, name_len);
	res.name.length = name_len;

	api->ipc_dispatch_send(priv_data_pt, &res, sizeof(res));
}

static void confdb_notify_lib_of_destroyed_object(
	hdb_handle_t parent_object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt)
{
	struct res_lib_confdb_object_destroy_callback res;

	res.header.size = sizeof(res);
	res.header.id = MESSAGE_RES_CONFDB_OBJECT_DESTROY_CALLBACK;
	res.header.error = CS_OK;
	res.parent_object_handle = parent_object_handle;
	memcpy(res.name.value, name_pt, name_len);
	res.name.length = name_len;

	api->ipc_dispatch_send(priv_data_pt, &res, sizeof(res));
}

static void confdb_notify_lib_of_reload(objdb_reload_notify_type_t notify_type,
					int flush,
					void *priv_data_pt)
{
	struct res_lib_confdb_reload_callback res;

	res.header.size = sizeof(res);
	res.header.id = MESSAGE_RES_CONFDB_RELOAD_CALLBACK;
	res.header.error = CS_OK;
	res.type = notify_type;

	api->ipc_dispatch_send(priv_data_pt, &res, sizeof(res));
}


static void message_handler_req_lib_confdb_track_start (void *conn,
							const void *message)
{
	const struct req_lib_confdb_object_track_start *req = message;
	coroipc_response_header_t res;

	api->object_track_start(req->object_handle,
		req->flags,
		confdb_notify_lib_of_key_change,
		confdb_notify_lib_of_new_object,
		confdb_notify_lib_of_destroyed_object,
		confdb_notify_lib_of_reload,
		conn);
	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_TRACK_START;
	res.error = CS_OK;
	api->ipc_response_send(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_track_stop (void *conn,
						       const void *message)
{
	coroipc_response_header_t res;

	api->object_track_stop(confdb_notify_lib_of_key_change,
		confdb_notify_lib_of_new_object,
		confdb_notify_lib_of_destroyed_object,
		confdb_notify_lib_of_reload,
		conn);

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_TRACK_STOP;
	res.error = CS_OK;
	api->ipc_response_send(conn, &res, sizeof(res));
}
