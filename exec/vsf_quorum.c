/*
 * Copyright (c) 2008, 2009 Red Hat, Inc.
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
 * - Neither the name of Red Hat Inc. nor the names of its
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

#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/corodefs.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/mar_gen.h>
#include <corosync/ipc_quorum.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/lcr/lcr_ifact.h>
#include <corosync/mar_gen.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/engine/quorum.h>

LOGSYS_DECLARE_SUBSYS ("QUORUM");

struct quorum_pd {
	unsigned char track_flags;
	int tracking_enabled;
	struct list_head list;
	void *conn;
};

struct internal_callback_pd {
	struct list_head list;
	quorum_callback_fn_t callback;
	void *context;
};

static void message_handler_req_lib_quorum_getquorate (void *conn,
						       const void *msg);
static void message_handler_req_lib_quorum_trackstart (void *conn,
						       const void *msg);
static void message_handler_req_lib_quorum_trackstop (void *conn,
						      const void *msg);
static void send_library_notification(void *conn);
static void send_internal_notification(void);
static int quorum_exec_init_fn (struct corosync_api_v1 *api);
static int quorum_lib_init_fn (void *conn);
static int quorum_lib_exit_fn (void *conn);

static int primary_designated = 0;
static struct corosync_api_v1 *corosync_api;
static struct list_head lib_trackers_list;
static struct list_head internal_trackers_list;
static struct memb_ring_id quorum_ring_id;
static size_t quorum_view_list_entries = 0;
static int quorum_view_list[PROCESSOR_COUNT_MAX];
struct quorum_services_api_ver1 *quorum_iface = NULL;
static char view_buf[64];

static void log_view_list(const unsigned int *view_list, size_t view_list_entries)
{
	int total = (int)view_list_entries;
	int len, pos, ret;
	int i = 0;

	while (1) {
		len = sizeof(view_buf);
		pos = 0;
		memset(view_buf, 0, len);

		for (; i < total; i++) {
			ret = snprintf(view_buf + pos, len - pos, " %d", view_list[i]);
			if (ret >= len - pos)
				break;
			pos += ret;
		}
		log_printf (LOGSYS_LEVEL_NOTICE, "Members[%d]:%s%s",
			    total, view_buf, i < total ? "\\" : "");

		if (i == total)
			break;
	}
}

/* Internal quorum API function */
static void quorum_api_set_quorum(const unsigned int *view_list,
				  size_t view_list_entries,
				  int quorum, struct memb_ring_id *ring_id)
{
	int old_quorum = primary_designated;
	primary_designated = quorum;

	if (primary_designated && !old_quorum) {
		log_printf (LOGSYS_LEVEL_NOTICE, "This node is within the primary component and will provide service.\n");
	} else if (!primary_designated && old_quorum) {
		log_printf (LOGSYS_LEVEL_NOTICE, "This node is within the non-primary component and will NOT provide any services.\n");
	}

	quorum_view_list_entries = view_list_entries;
	memcpy(&quorum_ring_id, ring_id, sizeof (quorum_ring_id));
	memcpy(quorum_view_list, view_list, sizeof(unsigned int)*view_list_entries);

	log_view_list(view_list, view_list_entries);

	/* Tell internal listeners */
	send_internal_notification();

	/* Tell IPC listeners */
	send_library_notification(NULL);
}

static struct corosync_lib_handler quorum_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_quorum_getquorate,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_quorum_trackstart,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_quorum_trackstop,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_service_engine quorum_service_handler = {
	.name				        = "corosync cluster quorum service v0.1",
	.id					= QUORUM_SERVICE,
	.private_data_size			= sizeof (struct quorum_pd),
	.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
	.lib_init_fn				= quorum_lib_init_fn,
	.lib_exit_fn				= quorum_lib_exit_fn,
	.lib_engine				= quorum_lib_service,
	.exec_init_fn				= quorum_exec_init_fn,
	.lib_engine_count			= sizeof (quorum_lib_service) / sizeof (struct corosync_lib_handler),
	.sync_mode				= CS_SYNC_V1
};

static struct lcr_iface corosync_quorum_ver0[1] = {
	{
		.name			= "corosync_quorum",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count	= 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL,
	},
};

static struct corosync_service_engine *quorum_get_service_handler_ver0 (void)
{
	return (&quorum_service_handler);
}

static struct lcr_comp quorum_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= corosync_quorum_ver0
};

static struct corosync_service_engine_iface_ver0 quorum_service_handler_iface = {
	.corosync_get_service_engine_ver0 = quorum_get_service_handler_ver0
};

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
	lcr_component_register (&quorum_comp_ver0);
	lcr_interfaces_set (&corosync_quorum_ver0[0], &quorum_service_handler_iface);
}

/* -------------------------------------------------- */


/*
 * Internal API functions for corosync
 */

static int quorum_quorate(void)
{
	return primary_designated;
}


static int quorum_register_callback(quorum_callback_fn_t function, void *context)
{
	struct internal_callback_pd *pd = malloc(sizeof(struct internal_callback_pd));
	if (!pd)
		return -1;

	pd->context  = context;
	pd->callback = function;
	list_add (&pd->list, &internal_trackers_list);

	return 0;
}

static int quorum_unregister_callback(quorum_callback_fn_t function, void *context)
{
	struct internal_callback_pd *pd;
	struct list_head *tmp;

	for (tmp = internal_trackers_list.next; tmp != &internal_trackers_list; tmp = tmp->next) {

		pd = list_entry(tmp, struct internal_callback_pd, list);
		if (pd->callback == function && pd->context == context) {
			list_del(&pd->list);
			free(pd);
			return 0;
		}
	}
	return -1;
}

static struct quorum_callin_functions callins = {
	.quorate = quorum_quorate,
	.register_callback = quorum_register_callback,
	.unregister_callback = quorum_unregister_callback
};

/* --------------------------------------------------------------------- */

static int quorum_exec_init_fn (struct corosync_api_v1 *api)
{
	hdb_handle_t find_handle;
	hdb_handle_t quorum_handle = 0;
	hdb_handle_t q_handle;
	char *quorum_module;
	int res;
	void *quorum_iface_p;

#ifdef COROSYNC_SOLARIS
	logsys_subsys_init();
#endif
	corosync_api = api;
	list_init (&lib_trackers_list);
	list_init (&internal_trackers_list);

	/*
	 * Tell corosync we have a quorum engine.
	 */
	api->quorum_initialize(&callins);

	/*
	 * Look for a quorum provider
	 */
	api->object_find_create(OBJECT_PARENT_HANDLE, "quorum", strlen("quorum"), &find_handle);
        api->object_find_next(find_handle, &quorum_handle);
	api->object_find_destroy(find_handle);

	if (quorum_handle) {
		if ( !(res = api->object_key_get(quorum_handle,
						 "provider",
						 strlen("provider"),
						 (void *)&quorum_module,
						 NULL))) {

			res = lcr_ifact_reference (
				&q_handle,
				quorum_module,
				0,
				&quorum_iface_p,
				0);

			if (res == -1) {
				log_printf (LOGSYS_LEVEL_NOTICE,
					    "Couldn't load quorum provider %s\n",
					    quorum_module);
				return (-1);
			}

			log_printf (LOGSYS_LEVEL_NOTICE,
				    "Using quorum provider %s\n", quorum_module);

			quorum_iface = (struct quorum_services_api_ver1 *)quorum_iface_p;
			quorum_iface->init (api, quorum_api_set_quorum);
		}
	}
	if (!quorum_iface) {
		/*
                 * With no quorum provider, we are always quorate
                 */
		primary_designated = 1;
	}

	return (0);
}

static int quorum_lib_init_fn (void *conn)
{
	struct quorum_pd *pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "lib_init_fn: conn=%p\n", conn);

	list_init (&pd->list);
	pd->conn = conn;

	return (0);
}

static int quorum_lib_exit_fn (void *conn)
{
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "lib_exit_fn: conn=%p\n", conn);

	if (quorum_pd->tracking_enabled) {
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	}
	return (0);
}


static void send_internal_notification(void)
{
	struct list_head *tmp;
	struct internal_callback_pd *pd;

	for (tmp = internal_trackers_list.next; tmp != &internal_trackers_list; tmp = tmp->next) {

		pd = list_entry(tmp, struct internal_callback_pd, list);

		pd->callback(primary_designated, pd->context);
	}
}

static void send_library_notification(void *conn)
{
	int size = sizeof(struct res_lib_quorum_notification) + sizeof(unsigned int)*quorum_view_list_entries;
	char buf[size];
	struct res_lib_quorum_notification *res_lib_quorum_notification = (struct res_lib_quorum_notification *)buf;
	struct list_head *tmp;
	int i;

	log_printf(LOGSYS_LEVEL_DEBUG, "sending quorum notification to %p, length = %d\n", conn, size);

	res_lib_quorum_notification->quorate = primary_designated;
	res_lib_quorum_notification->ring_seq = quorum_ring_id.seq;
	res_lib_quorum_notification->view_list_entries = quorum_view_list_entries;
	for (i=0; i<quorum_view_list_entries; i++) {
		res_lib_quorum_notification->view_list[i] = quorum_view_list[i];
	}

	res_lib_quorum_notification->header.id = MESSAGE_RES_QUORUM_NOTIFICATION;
	res_lib_quorum_notification->header.size = size;
	res_lib_quorum_notification->header.error = CS_OK;

	/* Send it to all interested parties */
	if (conn) {
		corosync_api->ipc_dispatch_send(conn, res_lib_quorum_notification, size);
	}
	else {
		struct quorum_pd *qpd;

		for (tmp = lib_trackers_list.next; tmp != &lib_trackers_list; tmp = tmp->next) {

			qpd = list_entry(tmp, struct quorum_pd, list);

			corosync_api->ipc_dispatch_send(qpd->conn,
			     res_lib_quorum_notification, size);
		}
	}
	return;
}

static void message_handler_req_lib_quorum_getquorate (void *conn,
						       const void *msg)
{
	struct res_lib_quorum_getquorate res_lib_quorum_getquorate;

	log_printf(LOGSYS_LEVEL_DEBUG, "got quorate request on %p\n", conn);

	/* send status */
	res_lib_quorum_getquorate.quorate = primary_designated;
	res_lib_quorum_getquorate.header.size = sizeof(res_lib_quorum_getquorate);
	res_lib_quorum_getquorate.header.id = MESSAGE_RES_QUORUM_GETQUORATE;
	res_lib_quorum_getquorate.header.error = CS_OK;
	corosync_api->ipc_response_send(conn, &res_lib_quorum_getquorate, sizeof(res_lib_quorum_getquorate));
}


static void message_handler_req_lib_quorum_trackstart (void *conn,
						       const void *msg)
{
	const struct req_lib_quorum_trackstart *req_lib_quorum_trackstart = msg;
	coroipc_response_header_t res;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "got trackstart request on %p\n", conn);

	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_quorum_trackstart->track_flags & CS_TRACK_CURRENT ||
	    req_lib_quorum_trackstart->track_flags & CS_TRACK_CHANGES) {
		log_printf(LOGSYS_LEVEL_DEBUG, "sending initial status to %p\n", conn);
		send_library_notification(conn);
	}

	/*
	 * Record requests for tracking
	 */
	if (req_lib_quorum_trackstart->track_flags & CS_TRACK_CHANGES ||
	    req_lib_quorum_trackstart->track_flags & CS_TRACK_CHANGES_ONLY) {

		quorum_pd->track_flags = req_lib_quorum_trackstart->track_flags;
		quorum_pd->tracking_enabled = 1;

		list_add (&quorum_pd->list, &lib_trackers_list);
	}

	/* send status */
	res.size = sizeof(res);
	res.id = MESSAGE_RES_QUORUM_TRACKSTART;
	res.error = CS_OK;
	corosync_api->ipc_response_send(conn, &res, sizeof(coroipc_response_header_t));
}

static void message_handler_req_lib_quorum_trackstop (void *conn, const void *msg)
{
	coroipc_response_header_t res;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "got trackstop request on %p\n", conn);

	if (quorum_pd->tracking_enabled) {
		res.error = CS_OK;
		quorum_pd->tracking_enabled = 0;
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	} else {
		res.error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res.size = sizeof(res);
	res.id = MESSAGE_RES_QUORUM_TRACKSTOP;
	res.error = CS_OK;
	corosync_api->ipc_response_send(conn, &res, sizeof(coroipc_response_header_t));
}
