/*
 * Copyright (c) 2008-2020 Red Hat, Inc.
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

#include "quorum.h"
#include <corosync/corotypes.h>
#include <qb/qbipc_common.h>
#include <corosync/corodefs.h>
#include <corosync/swab.h>
#include <qb/qblist.h>
#include <corosync/mar_gen.h>
#include <corosync/ipc_quorum.h>
#include <corosync/coroapi.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>

#include "service.h"
#include "votequorum.h"
#include "vsf_ykd.h"

LOGSYS_DECLARE_SUBSYS ("QUORUM");

struct quorum_pd {
	unsigned char track_flags;
	int tracking_enabled;
	struct qb_list_head list;
	void *conn;
	enum lib_quorum_model model;
};

struct internal_callback_pd {
	struct qb_list_head list;
	quorum_callback_fn_t callback;
	void *context;
};

static void quorum_sync_init (
	const unsigned int *trans_list,
	size_t trans_list_entries,
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int quorum_sync_process (void);

static void quorum_sync_activate (void);

static void quorum_sync_abort (void);

static void message_handler_req_lib_quorum_getquorate (void *conn,
						       const void *msg);
static void message_handler_req_lib_quorum_trackstart (void *conn,
						       const void *msg);
static void message_handler_req_lib_quorum_trackstop (void *conn,
						      const void *msg);
static void message_handler_req_lib_quorum_gettype (void *conn,
						       const void *msg);
static void message_handler_req_lib_quorum_model_gettype (void *conn,
						       const void *msg);
static void send_library_notification(void *conn);
static void send_internal_notification(void);
static void send_nodelist_library_notification(void *conn, int send_joined_left_list);
static char *quorum_exec_init_fn (struct corosync_api_v1 *api);
static int quorum_lib_init_fn (void *conn);
static int quorum_lib_exit_fn (void *conn);

static int primary_designated = 0;
static int quorum_type = 0;
static struct corosync_api_v1 *corosync_api;
static struct qb_list_head lib_trackers_list;
static struct qb_list_head internal_trackers_list;
static struct memb_ring_id quorum_ring_id;
static struct memb_ring_id last_sync_ring_id;
static size_t quorum_view_list_entries = 0;
static int quorum_view_list[PROCESSOR_COUNT_MAX];
struct quorum_services_api_ver1 *quorum_iface = NULL;

static char view_buf[64];

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];
static size_t my_member_list_entries;
static unsigned int my_old_member_list[PROCESSOR_COUNT_MAX];
static size_t my_old_member_list_entries = 0;
static unsigned int my_left_list[PROCESSOR_COUNT_MAX];
static size_t my_left_list_entries;
static unsigned int my_joined_list[PROCESSOR_COUNT_MAX];
static size_t my_joined_list_entries;

static void log_view_list(const unsigned int *view_list, size_t view_list_entries,
    const char *view_list_type_str)
{
	int total = (int)view_list_entries;
	int len, pos, ret;
	int i = 0;

	while (1) {
		len = sizeof(view_buf);
		pos = 0;
		memset(view_buf, 0, len);

		for (; i < total; i++) {
			ret = snprintf(view_buf + pos, len - pos, " " CS_PRI_NODE_ID, view_list[i]);
			if (ret >= len - pos)
				break;
			pos += ret;
		}
		log_printf (LOGSYS_LEVEL_NOTICE, "%s[%d]:%s%s",
			    view_list_type_str, total, view_buf, i < total ? "\\" : "");

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
		log_printf (LOGSYS_LEVEL_NOTICE, "This node is within the primary component and will provide service.");
	} else if (!primary_designated && old_quorum) {
		log_printf (LOGSYS_LEVEL_NOTICE, "This node is within the non-primary component and will NOT provide any services.");
	}

	quorum_view_list_entries = view_list_entries;
	memcpy(&quorum_ring_id, ring_id, sizeof (quorum_ring_id));
	memcpy(quorum_view_list, view_list, sizeof(unsigned int)*view_list_entries);

	log_view_list(view_list, view_list_entries, "Members");

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
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_quorum_gettype,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_quorum_model_gettype,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_service_engine quorum_service_handler = {
	.name				        = "corosync cluster quorum service v0.1",
	.id					= QUORUM_SERVICE,
	.priority				= 1,
	.private_data_size			= sizeof (struct quorum_pd),
	.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
	.lib_init_fn				= quorum_lib_init_fn,
	.lib_exit_fn				= quorum_lib_exit_fn,
	.lib_engine				= quorum_lib_service,
	.exec_init_fn				= quorum_exec_init_fn,
	.sync_init				= quorum_sync_init,
	.sync_process				= quorum_sync_process,
	.sync_activate				= quorum_sync_activate,
	.sync_abort				= quorum_sync_abort,
	.lib_engine_count			= sizeof (quorum_lib_service) / sizeof (struct corosync_lib_handler)
};

struct corosync_service_engine *vsf_quorum_get_service_engine_ver0 (void)
{
	return (&quorum_service_handler);
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
	qb_list_add (&pd->list, &internal_trackers_list);

	return 0;
}

static int quorum_unregister_callback(quorum_callback_fn_t function, void *context)
{
	struct internal_callback_pd *pd;
	struct qb_list_head *tmp, *tmp_iter;

	qb_list_for_each_safe(tmp, tmp_iter, &internal_trackers_list) {
		pd = qb_list_entry(tmp, struct internal_callback_pd, list);
		if (pd->callback == function && pd->context == context) {
			qb_list_del(&pd->list);
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

static void quorum_sync_init (
	const unsigned int *trans_list,
	size_t trans_list_entries,
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	int found;
	int i, j;
	int entries;
	int node_joined;

	memcpy (my_member_list, member_list, member_list_entries *
	    sizeof (unsigned int));
	my_member_list_entries = member_list_entries;

	last_sync_ring_id = *ring_id;

	/*
	 * Determine left list of nodeids
	 */
	entries = 0;
	for (i = 0; i < my_old_member_list_entries; i++) {
		found = 0;
		for (j = 0; j < trans_list_entries; j++) {
			if (my_old_member_list[i] == trans_list[j]) {
				found = 1;
				break;
			}
		}

		if (found == 0) {
			my_left_list[entries++] = my_old_member_list[i];
		} else {
			/*
			 * Check it is really in new membership
			 */
			found = 0;

			for (j = 0; j < my_member_list_entries; j++) {
				if (my_old_member_list[i] == my_member_list[j]) {
					found = 1;
					break;
				}
			}

			/*
			 * Node is in both old_member_list and trans list but not in my_member_list.
			 * (This shouldn't really happen).
			 */
			if (!found) {
				my_left_list[entries++] = my_old_member_list[i];
			}
		}
	}
	my_left_list_entries = entries;

	/*
	 * Determine joined list of nodeids
	 */
	entries = 0;
	for (i = 0; i < my_member_list_entries; i++) {
		node_joined = 1;
		for (j = 0; j < my_old_member_list_entries; j++) {
			if (my_member_list[i] == my_old_member_list[j]) {
				/*
				 * Node is in member list and also in my_old_member list -> check
				 * if it is in left_list.
				 */
				node_joined = 0;
				break;
			}
		}

		if (!node_joined) {
			/*
			 * Check if node is in left list.
			 */
			for (j = 0; j < my_left_list_entries; j++) {
				if (my_member_list[i] == my_left_list[j]) {
					/*
					 * Node is both in left and also in member list -> joined
					 */
					node_joined = 1;
					break;
				}
			}
		}

		if (node_joined) {
			my_joined_list[entries++] = my_member_list[i];
		}
	}
	my_joined_list_entries = entries;

	log_view_list(my_member_list, my_member_list_entries, "Sync members");

	if (my_joined_list_entries > 0) {
		log_view_list(my_joined_list, my_joined_list_entries, "Sync joined");
	}

	if (my_left_list_entries > 0) {
		log_view_list(my_left_list, my_left_list_entries, "Sync left");
	}
}

static int quorum_sync_process (void)
{

	return (0);
}

static void quorum_sync_activate (void)
{

	memcpy (my_old_member_list, my_member_list,
	    my_member_list_entries * sizeof (unsigned int));
	my_old_member_list_entries = my_member_list_entries;

	/* Tell IPC listeners */
	send_nodelist_library_notification(NULL, 1);
}

static void quorum_sync_abort (void)
{

}

static char *quorum_exec_init_fn (struct corosync_api_v1 *api)
{
	char *quorum_module = NULL;
	char *error;

	corosync_api = api;
	qb_list_init (&lib_trackers_list);
	qb_list_init (&internal_trackers_list);

	/*
	 * Tell corosync we have a quorum engine.
	 */
	api->quorum_initialize(&callins);

	/*
	 * Look for a quorum provider
	 */
	if (icmap_get_string("quorum.provider", &quorum_module) == CS_OK) {
		log_printf (LOGSYS_LEVEL_NOTICE,
			    "Using quorum provider %s", quorum_module);

		error = (char *)"Invalid quorum provider";

		if (strcmp (quorum_module, "corosync_votequorum") == 0) {
			error = votequorum_init (api, quorum_api_set_quorum);
			quorum_type = 1;
		}
		if (strcmp (quorum_module, "corosync_ykd") == 0) {
			error = ykd_init (api, quorum_api_set_quorum);
			quorum_type = 1;
		}
		if (error) {
			log_printf (LOGSYS_LEVEL_CRIT, 
				"Quorum provider: %s failed to initialize.",
				 quorum_module);
			free(quorum_module);
			return (error);
		}
	}

	if (quorum_module) {
		free(quorum_module);
		quorum_module = NULL;
	}

	/*
	 * setting quorum_type and primary_designated in the right order is important
	 * always try to lookup/init a quorum module, then revert back to be quorate
	 */

	if (quorum_type == 0) {
		primary_designated = 1;
	}

	return (NULL);
}

static int quorum_lib_init_fn (void *conn)
{
	struct quorum_pd *pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "lib_init_fn: conn=%p", conn);

	qb_list_init (&pd->list);
	pd->conn = conn;
	pd->model = LIB_QUORUM_MODEL_V0;

	return (0);
}

static int quorum_lib_exit_fn (void *conn)
{
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "lib_exit_fn: conn=%p", conn);

	if (quorum_pd->tracking_enabled) {
		qb_list_del (&quorum_pd->list);
		qb_list_init (&quorum_pd->list);
	}
	return (0);
}


static void send_internal_notification(void)
{
	struct qb_list_head *tmp;
	struct internal_callback_pd *pd;

	qb_list_for_each(tmp, &internal_trackers_list) {
		pd = qb_list_entry(tmp, struct internal_callback_pd, list);

		pd->callback(primary_designated, pd->context);
	}
}

static void prepare_library_notification_v0(char *buf, size_t size)
{
	struct res_lib_quorum_notification *res_lib_quorum_notification = (struct res_lib_quorum_notification *)buf;
	int i;

	res_lib_quorum_notification->quorate = primary_designated;
	res_lib_quorum_notification->ring_seq = quorum_ring_id.seq;
	res_lib_quorum_notification->view_list_entries = quorum_view_list_entries;
	for (i=0; i<quorum_view_list_entries; i++) {
		res_lib_quorum_notification->view_list[i] = quorum_view_list[i];
	}

	res_lib_quorum_notification->header.id = MESSAGE_RES_QUORUM_NOTIFICATION;
	res_lib_quorum_notification->header.size = size;
	res_lib_quorum_notification->header.error = CS_OK;
}

static void prepare_library_notification_v1(char *buf, size_t size)
{
	struct res_lib_quorum_v1_quorum_notification *res_lib_quorum_v1_quorum_notification =
	    (struct res_lib_quorum_v1_quorum_notification *)buf;
	int i;

	res_lib_quorum_v1_quorum_notification->quorate = primary_designated;
	res_lib_quorum_v1_quorum_notification->ring_id.nodeid = quorum_ring_id.nodeid;
	res_lib_quorum_v1_quorum_notification->ring_id.seq = quorum_ring_id.seq;
	res_lib_quorum_v1_quorum_notification->view_list_entries = quorum_view_list_entries;
	for (i=0; i<quorum_view_list_entries; i++) {
		res_lib_quorum_v1_quorum_notification->view_list[i] = quorum_view_list[i];
	}

	res_lib_quorum_v1_quorum_notification->header.id = MESSAGE_RES_QUORUM_V1_QUORUM_NOTIFICATION;
	res_lib_quorum_v1_quorum_notification->header.size = size;
	res_lib_quorum_v1_quorum_notification->header.error = CS_OK;
}

static void send_library_notification(void *conn)
{
	int size_v0 = sizeof(struct res_lib_quorum_notification) +
	    sizeof(mar_uint32_t) * quorum_view_list_entries;
	int size_v1 = sizeof(struct res_lib_quorum_v1_quorum_notification) +
	    sizeof(mar_uint32_t)*quorum_view_list_entries;

	char buf_v0[size_v0];
	char buf_v1[size_v1];

	struct res_lib_quorum_notification *res_lib_quorum_notification =
	    (struct res_lib_quorum_notification *)buf_v0;
	struct res_lib_quorum_v1_quorum_notification *res_lib_quorum_v1_quorum_notification =
	    (struct res_lib_quorum_v1_quorum_notification *)buf_v1;

	struct quorum_pd *qpd;
	struct qb_list_head *tmp;

	log_printf(LOGSYS_LEVEL_DEBUG, "sending quorum notification to %p, length = %u/%u", conn, size_v0, size_v1);

	prepare_library_notification_v0(buf_v0, size_v0);
	prepare_library_notification_v1(buf_v1, size_v1);

	/* Send it to all interested parties */
	if (conn) {
		qpd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

		if (qpd->model == LIB_QUORUM_MODEL_V0) {
			corosync_api->ipc_dispatch_send(conn, res_lib_quorum_notification, size_v0);
		} else if (qpd->model == LIB_QUORUM_MODEL_V1) {
			corosync_api->ipc_dispatch_send(conn, res_lib_quorum_v1_quorum_notification, size_v1);
		}
	}
	else {
		qb_list_for_each(tmp, &lib_trackers_list) {
			qpd = qb_list_entry(tmp, struct quorum_pd, list);

			if (qpd->model == LIB_QUORUM_MODEL_V0) {
				corosync_api->ipc_dispatch_send(qpd->conn,
				     res_lib_quorum_notification, size_v0);
			} else if (qpd->model == LIB_QUORUM_MODEL_V1) {
				corosync_api->ipc_dispatch_send(qpd->conn,
				     res_lib_quorum_v1_quorum_notification, size_v1);
			}
		}
	}
	return;
}

static void send_nodelist_library_notification(void *conn, int send_joined_left_list)
{
	int size = sizeof(struct res_lib_quorum_v1_nodelist_notification) +
	    sizeof(mar_uint32_t) * my_member_list_entries;
	char *buf;
	struct res_lib_quorum_v1_nodelist_notification *res_lib_quorum_v1_nodelist_notification;
	struct quorum_pd *qpd;
	struct qb_list_head *tmp;
	mar_uint32_t *ptr;
	int i;

	if (send_joined_left_list) {
		size += sizeof(mar_uint32_t) * my_joined_list_entries;
		size += sizeof(mar_uint32_t) * my_left_list_entries;
	}

	buf = alloca(size);
	memset(buf, 0, size);

	res_lib_quorum_v1_nodelist_notification = (struct res_lib_quorum_v1_nodelist_notification *)buf;

	res_lib_quorum_v1_nodelist_notification->ring_id.nodeid = last_sync_ring_id.nodeid;
	res_lib_quorum_v1_nodelist_notification->ring_id.seq = last_sync_ring_id.seq;
	res_lib_quorum_v1_nodelist_notification->member_list_entries = my_member_list_entries;

	if (send_joined_left_list) {
		res_lib_quorum_v1_nodelist_notification->joined_list_entries = my_joined_list_entries;
		res_lib_quorum_v1_nodelist_notification->left_list_entries = my_left_list_entries;
	}

	ptr = res_lib_quorum_v1_nodelist_notification->member_list;

	for (i=0; i<my_member_list_entries; i++, ptr++) {
		*ptr = my_member_list[i];
	}

	if (send_joined_left_list) {
		for (i=0; i<my_joined_list_entries; i++, ptr++) {
			*ptr = my_joined_list[i];
		}

		for (i=0; i<my_left_list_entries; i++, ptr++) {
			*ptr = my_left_list[i];
		}
	}

	res_lib_quorum_v1_nodelist_notification->header.id = MESSAGE_RES_QUORUM_V1_NODELIST_NOTIFICATION;
	res_lib_quorum_v1_nodelist_notification->header.size = size;
	res_lib_quorum_v1_nodelist_notification->header.error = CS_OK;

	log_printf(LOGSYS_LEVEL_DEBUG, "sending nodelist notification to %p, length = %u", conn, size);

	/* Send it to all interested parties */
	if (conn) {
		qpd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

		if (qpd->model == LIB_QUORUM_MODEL_V1) {
			corosync_api->ipc_dispatch_send(conn, res_lib_quorum_v1_nodelist_notification, size);
		}
	}
	else {
		qb_list_for_each(tmp, &lib_trackers_list) {
			qpd = qb_list_entry(tmp, struct quorum_pd, list);

			if (qpd->model == LIB_QUORUM_MODEL_V1) {
				corosync_api->ipc_dispatch_send(qpd->conn,
				     res_lib_quorum_v1_nodelist_notification, size);
			}
		}
	}

	return;
}

static void message_handler_req_lib_quorum_getquorate (void *conn,
						       const void *msg)
{
	struct res_lib_quorum_getquorate res_lib_quorum_getquorate;

	log_printf(LOGSYS_LEVEL_DEBUG, "got quorate request on %p", conn);

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
	struct qb_ipc_response_header res;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);
	cs_error_t error = CS_OK;

	log_printf(LOGSYS_LEVEL_DEBUG, "got trackstart request on %p", conn);

	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_quorum_trackstart->track_flags & CS_TRACK_CURRENT ||
	    req_lib_quorum_trackstart->track_flags & CS_TRACK_CHANGES) {
		log_printf(LOGSYS_LEVEL_DEBUG, "sending initial status to %p", conn);
		send_nodelist_library_notification(conn, 0);
		send_library_notification(conn);
	}

	if (quorum_pd->tracking_enabled) {
		error = CS_ERR_EXIST;
		goto response_send;
	}

	/*
	 * Record requests for tracking
	 */
	if (req_lib_quorum_trackstart->track_flags & CS_TRACK_CHANGES ||
	    req_lib_quorum_trackstart->track_flags & CS_TRACK_CHANGES_ONLY) {

		quorum_pd->track_flags = req_lib_quorum_trackstart->track_flags;
		quorum_pd->tracking_enabled = 1;

		qb_list_add (&quorum_pd->list, &lib_trackers_list);
	}

response_send:
	/* send status */
	res.size = sizeof(res);
	res.id = MESSAGE_RES_QUORUM_TRACKSTART;
	res.error = error;
	corosync_api->ipc_response_send(conn, &res, sizeof(struct qb_ipc_response_header));
}

static void message_handler_req_lib_quorum_trackstop (void *conn, const void *msg)
{
	struct qb_ipc_response_header res;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "got trackstop request on %p", conn);

	if (quorum_pd->tracking_enabled) {
		res.error = CS_OK;
		quorum_pd->tracking_enabled = 0;
		qb_list_del (&quorum_pd->list);
		qb_list_init (&quorum_pd->list);
	} else {
		res.error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res.size = sizeof(res);
	res.id = MESSAGE_RES_QUORUM_TRACKSTOP;
	res.error = CS_OK;
	corosync_api->ipc_response_send(conn, &res, sizeof(struct qb_ipc_response_header));
}

static void message_handler_req_lib_quorum_gettype (void *conn,
						       const void *msg)
{
	struct res_lib_quorum_gettype res_lib_quorum_gettype;

	log_printf(LOGSYS_LEVEL_DEBUG, "got quorum_type request on %p", conn);

	/* send status */
	res_lib_quorum_gettype.quorum_type = quorum_type;
	res_lib_quorum_gettype.header.size = sizeof(res_lib_quorum_gettype);
	res_lib_quorum_gettype.header.id = MESSAGE_RES_QUORUM_GETTYPE;
	res_lib_quorum_gettype.header.error = CS_OK;
	corosync_api->ipc_response_send(conn, &res_lib_quorum_gettype, sizeof(res_lib_quorum_gettype));
}

static void message_handler_req_lib_quorum_model_gettype (void *conn,
						       const void *msg)
{
	const struct req_lib_quorum_model_gettype *req_lib_quorum_model_gettype = msg;
	struct res_lib_quorum_model_gettype res_lib_quorum_model_gettype;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);
	cs_error_t ret_err;

	log_printf(LOGSYS_LEVEL_DEBUG, "got quorum_model_type request on %p", conn);

	ret_err = CS_OK;

	if (req_lib_quorum_model_gettype->model != LIB_QUORUM_MODEL_V0 &&
	    req_lib_quorum_model_gettype->model != LIB_QUORUM_MODEL_V1) {
		log_printf(LOGSYS_LEVEL_ERROR, "quorum_model_type request for unsupported model %u",
		    req_lib_quorum_model_gettype->model);

		ret_err = CS_ERR_INVALID_PARAM;
	} else {
		quorum_pd->model = req_lib_quorum_model_gettype->model;
	}

	/* send status */
	res_lib_quorum_model_gettype.quorum_type = quorum_type;
	res_lib_quorum_model_gettype.header.size = sizeof(res_lib_quorum_model_gettype);
	res_lib_quorum_model_gettype.header.id = MESSAGE_RES_QUORUM_MODEL_GETTYPE;
	res_lib_quorum_model_gettype.header.error = ret_err;
	corosync_api->ipc_response_send(conn, &res_lib_quorum_model_gettype, sizeof(res_lib_quorum_model_gettype));
}
