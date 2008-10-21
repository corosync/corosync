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

#include <assert.h>
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
#include <signal.h>
#include <sched.h>
#include <time.h>

#include <corosync/engine/logsys.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/ipc_gen.h>
#include <corosync/ipc_quorum.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/coroapi.h>

#include "vsf.h"
#include "quorum.h"

LOGSYS_DECLARE_SUBSYS ("QUORUM", LOG_INFO);

struct quorum_pd {
	unsigned char track_flags;
	int tracking_enabled;
	struct list_head list;
	void *conn;
};

static void message_handler_req_lib_quorum_getquorate (void *conn, void *msg);
static void message_handler_req_lib_quorum_trackstart (void *conn, void *msg);
static void message_handler_req_lib_quorum_trackstop (void *conn, void *msg);
static int send_quorum_notification(void *conn);
static int quorum_exec_init_fn (struct corosync_api_v1 *api);
static int quorum_lib_init_fn (void *conn);
static int quorum_lib_exit_fn (void *conn);

static int primary_designated = 0;
static struct corosync_api_v1 *corosync_api;
static struct list_head trackers_list;
static struct memb_ring_id quorum_ring_id;
static int quorum_view_list_entries = 0;
static int quorum_view_list[PROCESSOR_COUNT_MAX];

static void (*quorum_primary_callback_fn) (
	unsigned int *view_list,
	int view_list_entries,
	int primary_designated,
	struct memb_ring_id *ring_id);

/* Internal quorum API function */
static void quorum_api_set_quorum(unsigned int *view_list,
				  int view_list_entries,
				  int quorum, struct memb_ring_id *ring_id)
{
	primary_designated = quorum;
	memcpy(&quorum_ring_id, &ring_id, sizeof (quorum_ring_id));

	quorum_view_list_entries = view_list_entries;
	memcpy(quorum_view_list, view_list, sizeof(unsigned int)*view_list_entries);

	/* Tell sync() */
	quorum_primary_callback_fn(view_list, view_list_entries,
				   primary_designated, &quorum_ring_id);

	/* Tell IPC listeners */
	send_quorum_notification(NULL);
}

static int quorum_init (
	void (*primary_callback_fn) (
		unsigned int *view_list,
		int view_list_entries,
		int primary_designated,
		struct memb_ring_id *ring_id))
{
	quorum_primary_callback_fn = primary_callback_fn;

	return (0);
}

/*
 * Returns 1 if this processor is in the primary (has quorum)
 */
static int quorum_primary (void)
{
	return (primary_designated);
}

/*
 * lcrso object definition
 */
static struct corosync_vsf_iface_ver0 vsf_quorum_iface_ver0 = {
	.init				= quorum_init,
	.primary			= quorum_primary
};

static struct quorum_services_api_ver1 quorum_service_api_v1 = {
	.quorum_api_set_quorum = quorum_api_set_quorum
};

static struct corosync_lib_handler quorum_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_quorum_getquorate,
		.response_size				= sizeof (struct res_lib_quorum_getquorate),
		.response_id				= MESSAGE_RES_QUORUM_GETQUORATE,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_quorum_trackstart,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_QUORUM_NOTIFICATION,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_quorum_trackstop,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_QUORUM_TRACKSTOP,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_service_engine quorum_service_handler = {
	.name				        = "corosync cluster quorum service v0.1",
	.id					= QUORUM_SERVICE,
	.private_data_size			= sizeof (struct quorum_pd),
	.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.allow_inquorate			= COROSYNC_LIB_ALLOW_INQUORATE,
	.lib_init_fn				= quorum_lib_init_fn,
	.lib_exit_fn				= quorum_lib_exit_fn,
	.lib_engine				= quorum_lib_service,
	.exec_init_fn				= quorum_exec_init_fn,
	.lib_engine_count			= sizeof (quorum_lib_service) / sizeof (struct corosync_lib_handler),
};

static struct lcr_iface corosync_vsf_quorum_ver0[3] = {
	{ /* the VSF handler */
		.name			= "corosync_vsf_quorum",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count	= 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= (void **)(void *)&vsf_quorum_iface_ver0,
	},
	{ /* API for quorum users to call */
		.name                   = "corosync_quorum_api",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL
	},
	{ /* Library calls */
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

static struct lcr_comp vsf_quorum_comp_ver0 = {
	.iface_count			= 3,
	.ifaces				= corosync_vsf_quorum_ver0
};

static struct corosync_service_engine_iface_ver0 quorum_service_handler_iface = {
	.corosync_get_service_engine_ver0 = quorum_get_service_handler_ver0
};

__attribute__ ((constructor)) static void vsf_quorum_comp_register (void) {
	lcr_component_register (&vsf_quorum_comp_ver0);
	lcr_interfaces_set (&corosync_vsf_quorum_ver0[0], &vsf_quorum_iface_ver0);
	lcr_interfaces_set (&corosync_vsf_quorum_ver0[1], &quorum_service_api_v1);
	lcr_interfaces_set (&corosync_vsf_quorum_ver0[2], &quorum_service_handler_iface);
}

/* -------------------------------------------------- */

static int quorum_exec_init_fn (struct corosync_api_v1 *api)
{
	corosync_api = api;
	list_init (&trackers_list);
	return (0);
}

static int quorum_lib_init_fn (void *conn)
{
	struct quorum_pd *pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOG_LEVEL_DEBUG, "lib_init_fn: conn=%p\n", conn);

	list_init (&pd->list);
	pd->conn = conn;

	return (0);
}

static int quorum_lib_exit_fn (void *conn)
{
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOG_LEVEL_DEBUG, "lib_exit_fn: conn=%p\n", conn);

	if (quorum_pd->tracking_enabled) {
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	}
	return (0);
}

static int send_quorum_notification(void *conn)
{
	int size = sizeof(struct res_lib_quorum_notification) + sizeof(unsigned int)*quorum_view_list_entries;
	char buf[size];
	struct res_lib_quorum_notification *res_lib_quorum_notification = (struct res_lib_quorum_notification *)buf;
	struct list_head *tmp;
	int i;

	log_printf(LOG_LEVEL_DEBUG, "sending quorum notification to %p, length = %d\n", conn, size);

	res_lib_quorum_notification->quorate = primary_designated;
	res_lib_quorum_notification->ring_seq = quorum_ring_id.seq;
	res_lib_quorum_notification->view_list_entries = quorum_view_list_entries;
	for (i=0; i<quorum_view_list_entries; i++) {
		res_lib_quorum_notification->view_list[i] = quorum_view_list[i];
	}

	res_lib_quorum_notification->header.id = MESSAGE_RES_QUORUM_NOTIFICATION;
	res_lib_quorum_notification->header.size = size;
	res_lib_quorum_notification->header.error = SA_AIS_OK;

	/* Send it to all interested parties */
	if (conn) {
		return corosync_api->ipc_conn_send_response(conn, res_lib_quorum_notification, size);
	}
	else {
		struct quorum_pd *qpd;

		for (tmp = trackers_list.next; tmp != &trackers_list; tmp = tmp->next) {

			qpd = list_entry(tmp, struct quorum_pd, list);

			corosync_api->ipc_conn_send_response(corosync_api->ipc_conn_partner_get(qpd->conn),
							     res_lib_quorum_notification, size);
		}
	}
	return (0);
}

static void message_handler_req_lib_quorum_getquorate (void *conn, void *msg)
{
	struct res_lib_quorum_getquorate res_lib_quorum_getquorate;

	log_printf(LOG_LEVEL_DEBUG, "got quorate request on %p\n", conn);

	/* send status */
	res_lib_quorum_getquorate.quorate = primary_designated;
	res_lib_quorum_getquorate.header.size = sizeof(res_lib_quorum_getquorate);
	res_lib_quorum_getquorate.header.id = MESSAGE_RES_QUORUM_GETQUORATE;
	res_lib_quorum_getquorate.header.error = SA_AIS_OK;
	corosync_api->ipc_conn_send_response(conn, &res_lib_quorum_getquorate, sizeof(res_lib_quorum_getquorate));
}


static void message_handler_req_lib_quorum_trackstart (void *conn, void *msg)
{
	struct req_lib_quorum_trackstart *req_lib_quorum_trackstart = (struct req_lib_quorum_trackstart *)msg;
	mar_res_header_t res;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOG_LEVEL_DEBUG, "got trackstart request on %p\n", conn);

	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_quorum_trackstart->track_flags & SA_TRACK_CURRENT ||
	    req_lib_quorum_trackstart->track_flags & SA_TRACK_CHANGES) {
		log_printf(LOG_LEVEL_DEBUG, "sending initial status to %p\n", conn);
		send_quorum_notification(corosync_api->ipc_conn_partner_get (conn));
	}

	/*
	 * Record requests for tracking
	 */
	if (req_lib_quorum_trackstart->track_flags & SA_TRACK_CHANGES ||
	    req_lib_quorum_trackstart->track_flags & SA_TRACK_CHANGES_ONLY) {

		quorum_pd->track_flags = req_lib_quorum_trackstart->track_flags;
		quorum_pd->tracking_enabled = 1;

		list_add (&quorum_pd->list, &trackers_list);
	}

	/* send status */
	res.size = sizeof(res);
	res.id = MESSAGE_RES_QUORUM_TRACKSTART;
	res.error = SA_AIS_OK;
	corosync_api->ipc_conn_send_response(conn, &res, sizeof(mar_res_header_t));
}

static void message_handler_req_lib_quorum_trackstop (void *conn, void *msg)
{
	mar_res_header_t res;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOG_LEVEL_DEBUG, "got trackstop request on %p\n", conn);

	if (quorum_pd->tracking_enabled) {
		res.error = SA_AIS_OK;
		quorum_pd->tracking_enabled = 0;
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	} else {
		res.error = SA_AIS_ERR_NOT_EXIST;
	}

	/* send status */
	res.size = sizeof(res);
	res.id = MESSAGE_RES_QUORUM_TRACKSTOP;
	res.error = SA_AIS_OK;
	corosync_api->ipc_conn_send_response(conn, &res, sizeof(mar_res_header_t));
}

