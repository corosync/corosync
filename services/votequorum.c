/*
 * Copyright (c) 2009 Red Hat, Inc.
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
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/corodefs.h>
#include <corosync/cfg.h>
#include <corosync/list.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/logsys.h>
#include <corosync/mar_gen.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/quorum.h>
#include <corosync/ipc_votequorum.h>
#include <corosync/list.h>

#include "../exec/tlist.h"

#define VOTEQUORUM_MAJOR_VERSION 7
#define VOTEQUORUM_MINOR_VERSION 0
#define VOTEQUORUM_PATCH_VERSION 0

 /* Silly default to prevent accidents! */
#define DEFAULT_EXPECTED   1024
#define DEFAULT_QDEV_POLL 10000
#define DEFAULT_LEAVE_TMO 10000

LOGSYS_DECLARE_SUBSYS ("VOTEQ");

enum quorum_message_req_types {
	MESSAGE_REQ_EXEC_VOTEQUORUM_NODEINFO  = 0,
	MESSAGE_REQ_EXEC_VOTEQUORUM_RECONFIGURE = 1,
	MESSAGE_REQ_EXEC_VOTEQUORUM_KILLNODE = 2,
};

#define NODE_FLAGS_BEENDOWN         1
#define NODE_FLAGS_SEESDISALLOWED   8
#define NODE_FLAGS_HASSTATE        16
#define NODE_FLAGS_QDISK           32
#define NODE_FLAGS_REMOVED         64
#define NODE_FLAGS_US             128

#define NODEID_US 0
#define NODEID_QDEVICE -1

typedef enum { NODESTATE_JOINING=1, NODESTATE_MEMBER,
	       NODESTATE_DEAD, NODESTATE_LEAVING, NODESTATE_DISALLOWED } nodestate_t;

struct cluster_node {
	int flags;
	int node_id;
	unsigned int expected_votes;
	unsigned int votes;
	time_t join_time;

	nodestate_t state;

	unsigned long long int last_hello; /* Only used for quorum devices */

	struct list_head list;
};

static int quorum_flags;
#define VOTEQUORUM_FLAG_FEATURE_DISALLOWED 1
#define VOTEQUORUM_FLAG_FEATURE_TWONODE 1

static int quorum;
static int cluster_is_quorate;
static int first_trans = 1;
static unsigned int quorumdev_poll = DEFAULT_QDEV_POLL;
static unsigned int leaving_timeout = DEFAULT_LEAVE_TMO;

static struct cluster_node *us;
static struct cluster_node *quorum_device = NULL;
static char quorum_device_name[VOTEQUORUM_MAX_QDISK_NAME_LEN];
static corosync_timer_handle_t quorum_device_timer;
static corosync_timer_handle_t leaving_timer;
static struct list_head cluster_members_list;
static struct corosync_api_v1 *corosync_api;
static struct list_head trackers_list;
static unsigned int quorum_members[PROCESSOR_COUNT_MAX+1];
static int quorum_members_entries = 0;
static struct memb_ring_id quorum_ringid;

#define max(a,b) (((a) > (b)) ? (a) : (b))
static struct cluster_node *find_node_by_nodeid(int nodeid);
static struct cluster_node *allocate_node(int nodeid);
static const char *kill_reason(int reason);

#define list_iterate(v, head) \
        for (v = (head)->next; v != head; v = v->next)

struct quorum_pd {
	unsigned char track_flags;
	int tracking_enabled;
	uint64_t tracking_context;
	struct list_head list;
	void *conn;
};

/*
 * Service Interfaces required by service_message_handler struct
 */

static void votequorum_init(struct corosync_api_v1 *api,
			    quorum_set_quorate_fn_t report);

static void quorum_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

static int votequorum_exec_init_fn (struct corosync_api_v1 *api);

static int quorum_lib_init_fn (void *conn);

static int quorum_lib_exit_fn (void *conn);

static void message_handler_req_exec_votequorum_nodeinfo (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_votequorum_reconfigure (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_votequorum_killnode (
	const void *message,
	unsigned int nodeid);


static void message_handler_req_lib_votequorum_getinfo (void *conn,
							const void *message);

static void message_handler_req_lib_votequorum_setexpected (void *conn,
							    const void *message);

static void message_handler_req_lib_votequorum_setvotes (void *conn,
							 const void *message);

static void message_handler_req_lib_votequorum_qdisk_register (void *conn,
							       const void *message);

static void message_handler_req_lib_votequorum_qdisk_unregister (void *conn,
								 const void *message);

static void message_handler_req_lib_votequorum_qdisk_poll (void *conn,
							   const void *message);

static void message_handler_req_lib_votequorum_qdisk_getinfo (void *conn,
							      const void *message);

static void message_handler_req_lib_votequorum_setstate (void *conn,
							 const void *message);

static void message_handler_req_lib_votequorum_leaving (void *conn,
							const void *message);
static void message_handler_req_lib_votequorum_trackstart (void *conn,
							   const void *msg);
static void message_handler_req_lib_votequorum_trackstop (void *conn,
							  const void *msg);

static int quorum_exec_send_nodeinfo(void);
static int quorum_exec_send_reconfigure(int param, int nodeid, int value);
static int quorum_exec_send_killnode(int nodeid, unsigned int reason);

static void exec_votequorum_nodeinfo_endian_convert (void *msg);
static void exec_votequorum_reconfigure_endian_convert (void *msg);
static void exec_votequorum_killnode_endian_convert (void *msg);

static void add_votequorum_config_notification(hdb_handle_t quorum_object_handle);

static void recalculate_quorum(int allow_decrease, int by_current_nodes);
static void votequorum_objdb_reload_notify(
	objdb_reload_notify_type_t type, int flush,
	void *priv_data_pt);

/*
 * Library Handler Definition
 */
static struct corosync_lib_handler quorum_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_getinfo,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_setexpected,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_setvotes,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_qdisk_register,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_qdisk_unregister,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_qdisk_poll,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_qdisk_getinfo,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_setstate,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_leaving,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_trackstart,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn				= message_handler_req_lib_votequorum_trackstop,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_exec_handler votequorum_exec_engine[] =
{
	{ /* 0 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_nodeinfo,
		.exec_endian_convert_fn	= exec_votequorum_nodeinfo_endian_convert
	},
	{ /* 1 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_reconfigure,
		.exec_endian_convert_fn	= exec_votequorum_reconfigure_endian_convert
	},
	{ /* 2 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_killnode,
		.exec_endian_convert_fn	= exec_votequorum_killnode_endian_convert
	},
};


static quorum_set_quorate_fn_t set_quorum;
/*
 * lcrso object definition
 */
static struct quorum_services_api_ver1 votequorum_iface_ver0 = {
	.init				= votequorum_init
};

static struct corosync_service_engine quorum_service_handler = {
	.name				        = "corosync votes quorum service v0.91",
	.id					= VOTEQUORUM_SERVICE,
	.private_data_size			= sizeof (struct quorum_pd),
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
	.flow_control				= COROSYNC_LIB_FLOW_CONTROL_REQUIRED,
	.lib_init_fn				= quorum_lib_init_fn,
	.lib_exit_fn				= quorum_lib_exit_fn,
	.lib_engine				= quorum_lib_service,
	.lib_engine_count			= sizeof (quorum_lib_service) / sizeof (struct corosync_lib_handler),
	.exec_init_fn				= votequorum_exec_init_fn,
	.exec_engine				= votequorum_exec_engine,
	.exec_engine_count		        = sizeof (votequorum_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn                             = quorum_confchg_fn,
	.sync_mode				= CS_SYNC_V1
};

/*
 * Dynamic loader definition
 */
static struct corosync_service_engine *quorum_get_service_handler_ver0 (void);

static struct corosync_service_engine_iface_ver0 quorum_service_handler_iface = {
	.corosync_get_service_engine_ver0 = quorum_get_service_handler_ver0
};

static struct lcr_iface corosync_quorum_ver0[2] = {
	{
		.name				= "corosync_votequorum",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count         = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= (void **)(void *)&votequorum_iface_ver0
	},
	{
		.name				= "corosync_votequorum_iface",
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

static struct lcr_comp quorum_comp_ver0 = {
	.iface_count			= 2,
	.ifaces			        = corosync_quorum_ver0
};


static struct corosync_service_engine *quorum_get_service_handler_ver0 (void)
{
	return (&quorum_service_handler);
}

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
        lcr_interfaces_set (&corosync_quorum_ver0[0], &votequorum_iface_ver0);
	lcr_interfaces_set (&corosync_quorum_ver0[1], &quorum_service_handler_iface);
	lcr_component_register (&quorum_comp_ver0);
}

static void votequorum_init(struct corosync_api_v1 *api,
			    quorum_set_quorate_fn_t report)
{
	ENTER();
	set_quorum = report;

	/* Load the library-servicing part of this module */
	api->service_link_and_init(api, "corosync_votequorum_iface", 0);

	LEAVE();
}

struct req_exec_quorum_nodeinfo {
	coroipc_request_header_t header __attribute__((aligned(8)));
	unsigned int first_trans;
	unsigned int votes;
	unsigned int expected_votes;

	unsigned int major_version;	/* Not backwards compatible */
	unsigned int minor_version;	/* Backwards compatible */
	unsigned int patch_version;	/* Backwards/forwards compatible */
	unsigned int config_version;
	unsigned int flags;

} __attribute__((packed));

/* Parameters for RECONFIG command */
#define RECONFIG_PARAM_EXPECTED_VOTES 1
#define RECONFIG_PARAM_NODE_VOTES     2
#define RECONFIG_PARAM_LEAVING        3

struct req_exec_quorum_reconfigure {
	coroipc_request_header_t header __attribute__((aligned(8)));
	unsigned int param;
	unsigned int nodeid;
	unsigned int value;
};

struct req_exec_quorum_killnode {
	coroipc_request_header_t header __attribute__((aligned(8)));
	unsigned int reason;
	unsigned int nodeid;
};

/* These just make the access a little neater */
static inline int objdb_get_string(const struct corosync_api_v1 *corosync,
				   hdb_handle_t object_service_handle,
				   char *key, char **value)
{
	int res;

	*value = NULL;
	if ( !(res = corosync_api->object_key_get(object_service_handle,
					      key,
					      strlen(key),
					      (void *)value,
					      NULL))) {
		if (*value)
			return 0;
	}
	return -1;
}

static inline void objdb_get_int(const struct corosync_api_v1 *corosync,
				 hdb_handle_t object_service_handle,
				 const char *key, unsigned int *intvalue,
				 unsigned int default_value)
{
	char *value = NULL;

	*intvalue = default_value;

	if (!corosync_api->object_key_get(object_service_handle, key, strlen(key),
				 (void *)&value, NULL)) {
		if (value) {
			*intvalue = atoi(value);
		}
	}
}

static void read_quorum_config(hdb_handle_t quorum_handle)
{
	unsigned int value = 0;
	int cluster_members = 0;
	struct list_head *tmp;
	struct cluster_node *node;

	log_printf(LOGSYS_LEVEL_INFO, "Reading configuration\n");

	objdb_get_int(corosync_api, quorum_handle, "expected_votes", &us->expected_votes, DEFAULT_EXPECTED);
	objdb_get_int(corosync_api, quorum_handle, "votes", &us->votes, 1);
	objdb_get_int(corosync_api, quorum_handle, "quorumdev_poll", &quorumdev_poll, DEFAULT_QDEV_POLL);
	objdb_get_int(corosync_api, quorum_handle, "leaving_timeout", &leaving_timeout, DEFAULT_LEAVE_TMO);
	objdb_get_int(corosync_api, quorum_handle, "disallowed", &value, 0);
	if (value)
		quorum_flags |= VOTEQUORUM_FLAG_FEATURE_DISALLOWED;
	else
		quorum_flags &= ~VOTEQUORUM_FLAG_FEATURE_DISALLOWED;

	objdb_get_int(corosync_api, quorum_handle, "two_node", &value, 0);
	if (value)
		quorum_flags |= VOTEQUORUM_FLAG_FEATURE_TWONODE;
	else
		quorum_flags &= ~VOTEQUORUM_FLAG_FEATURE_TWONODE;

	/*
	 * two_node mode is invalid if there are more than 2 nodes in the cluster!
	 */
	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		cluster_members++;
        }

	if (quorum_flags & VOTEQUORUM_FLAG_FEATURE_TWONODE && cluster_members > 2) {
		log_printf(LOGSYS_LEVEL_WARNING, "quorum.two_node was set but there are more than 2 nodes in the cluster. It will be ignored.");
		quorum_flags &= ~VOTEQUORUM_FLAG_FEATURE_TWONODE;
	}
}

static int votequorum_exec_init_fn (struct corosync_api_v1 *api)
{
	hdb_handle_t object_handle;
	hdb_handle_t find_handle;

#ifdef COROSYNC_SOLARIS
	logsys_subsys_init();
#endif
	ENTER();

	corosync_api = api;

	list_init(&cluster_members_list);
	list_init(&trackers_list);

	/* Allocate a cluster_node for us */
	us = allocate_node(corosync_api->totem_nodeid_get());
	if (!us)
		return (1);

	us->flags |= NODE_FLAGS_US;
	us->state = NODESTATE_MEMBER;
	us->expected_votes = DEFAULT_EXPECTED;
	us->votes = 1;
	time(&us->join_time);

	/* Get configuration variables */
	corosync_api->object_find_create(OBJECT_PARENT_HANDLE, "quorum", strlen("quorum"), &find_handle);

	if (corosync_api->object_find_next(find_handle, &object_handle) == 0) {
		read_quorum_config(object_handle);
	}
	recalculate_quorum(0, 0);

	/* Listen for changes */
	add_votequorum_config_notification(object_handle);
	/*
	 * Reload notify must be on the parent object
	 */
	corosync_api->object_track_start(OBJECT_PARENT_HANDLE,
					 1,
					 NULL,
					 NULL,
					 NULL,
					 votequorum_objdb_reload_notify,
					 NULL);
	corosync_api->object_find_destroy(find_handle);

	/* Start us off with one node */
	quorum_exec_send_nodeinfo();

	LEAVE();
	return (0);
}

static int quorum_lib_exit_fn (void *conn)
{
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();
	if (quorum_pd->tracking_enabled) {
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	}
	LEAVE();
	return (0);
}


static int send_quorum_notification(void *conn, uint64_t context)
{
	struct res_lib_votequorum_notification *res_lib_votequorum_notification;
	struct list_head *tmp;
	struct cluster_node *node;
	int cluster_members = 0;
	int i = 0;
	int size;
	char *buf;

	ENTER();
	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		cluster_members++;
        }
	if (quorum_device)
		cluster_members++;

	size = sizeof(struct res_lib_votequorum_notification) + sizeof(struct votequorum_node) * cluster_members;
	buf = alloca(size);
	if (!buf) {
		LEAVE();
		return -1;
	}

	res_lib_votequorum_notification = (struct res_lib_votequorum_notification *)buf;
	res_lib_votequorum_notification->quorate = cluster_is_quorate;
	res_lib_votequorum_notification->node_list_entries = cluster_members;
	res_lib_votequorum_notification->context = context;
	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		res_lib_votequorum_notification->node_list[i].nodeid = node->node_id;
		res_lib_votequorum_notification->node_list[i++].state = node->state;
        }
	if (quorum_device) {
		res_lib_votequorum_notification->node_list[i].nodeid = 0;
		res_lib_votequorum_notification->node_list[i++].state = quorum_device->state | 0x80;
	}
	res_lib_votequorum_notification->header.id = MESSAGE_RES_VOTEQUORUM_NOTIFICATION;
	res_lib_votequorum_notification->header.size = size;
	res_lib_votequorum_notification->header.error = CS_OK;

	/* Send it to all interested parties */
	if (conn) {
		int ret = corosync_api->ipc_dispatch_send(conn, buf, size);
		LEAVE();
		return ret;
	}
	else {
		struct quorum_pd *qpd;

		list_iterate(tmp, &trackers_list) {
			qpd = list_entry(tmp, struct quorum_pd, list);
			res_lib_votequorum_notification->context = qpd->tracking_context;
			corosync_api->ipc_dispatch_send(qpd->conn, buf, size);
		}
	}
	LEAVE();
	return 0;
}

static void send_expectedvotes_notification(void)
{
	struct res_lib_votequorum_expectedvotes_notification res_lib_votequorum_expectedvotes_notification;
	struct quorum_pd *qpd;
	struct list_head *tmp;

	log_printf(LOGSYS_LEVEL_DEBUG, "Sending expected votes callback\n");

	res_lib_votequorum_expectedvotes_notification.header.id = MESSAGE_RES_VOTEQUORUM_EXPECTEDVOTES_NOTIFICATION;
	res_lib_votequorum_expectedvotes_notification.header.size = sizeof(res_lib_votequorum_expectedvotes_notification);
	res_lib_votequorum_expectedvotes_notification.header.error = CS_OK;
	res_lib_votequorum_expectedvotes_notification.expected_votes = us->expected_votes;

	list_iterate(tmp, &trackers_list) {
		qpd = list_entry(tmp, struct quorum_pd, list);
		res_lib_votequorum_expectedvotes_notification.context = qpd->tracking_context;
		corosync_api->ipc_dispatch_send(qpd->conn, &res_lib_votequorum_expectedvotes_notification,
						sizeof(struct res_lib_votequorum_expectedvotes_notification));
	}
}

static void set_quorate(int total_votes)
{
	int quorate;

	ENTER();
	if (quorum > total_votes) {
		quorate = 0;
	}
	else {
		quorate = 1;
	}

	if (cluster_is_quorate && !quorate)
		log_printf(LOGSYS_LEVEL_INFO, "quorum lost, blocking activity\n");
	if (!cluster_is_quorate && quorate)
		log_printf(LOGSYS_LEVEL_INFO, "quorum regained, resuming activity\n");

	/* If we are newly quorate, then kill any DISALLOWED nodes */
	if (!cluster_is_quorate && quorate) {
		struct cluster_node *node = NULL;
		struct list_head *tmp;

		list_iterate(tmp, &cluster_members_list) {
			node = list_entry(tmp, struct cluster_node, list);
			if (node->state == NODESTATE_DISALLOWED)
				quorum_exec_send_killnode(node->node_id, VOTEQUORUM_REASON_KILL_REJOIN);
		}
	}

	cluster_is_quorate = quorate;
	set_quorum(quorum_members, quorum_members_entries, quorate, &quorum_ringid);
	ENTER();
}

static int calculate_quorum(int allow_decrease, int max_expected, unsigned int *ret_total_votes)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	unsigned int total_votes = 0;
	unsigned int highest_expected = 0;
	unsigned int newquorum, q1, q2;
	unsigned int total_nodes = 0;

	ENTER();

	list_iterate(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		log_printf(LOGSYS_LEVEL_DEBUG, "node %x state=%d, votes=%d, expected=%d\n",
			   node->node_id, node->state, node->votes, node->expected_votes);

		if (node->state == NODESTATE_MEMBER) {
			if (max_expected)
				node->expected_votes = max_expected;
			else
				highest_expected = max(highest_expected, node->expected_votes);
			total_votes += node->votes;
			total_nodes++;
		}
	}

	if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
		total_votes += quorum_device->votes;

	if (max_expected > 0)
		highest_expected = max_expected;

	/* This quorum calculation is taken from the OpenVMS Cluster Systems
	 * manual, but, then, you guessed that didn't you */
	q1 = (highest_expected + 2) / 2;
	q2 = (total_votes + 2) / 2;
	newquorum = max(q1, q2);

	/* Normally quorum never decreases but the system administrator can
	 * force it down by setting expected votes to a maximum value */
	if (!allow_decrease)
		newquorum = max(quorum, newquorum);

	/* The special two_node mode allows each of the two nodes to retain
	 * quorum if the other fails.  Only one of the two should live past
	 * fencing (as both nodes try to fence each other in split-brain.)
	 * Also: if there are more than two nodes, force us inquorate to avoid
	 * any damage or confusion.
	 */
	if ((quorum_flags & VOTEQUORUM_FLAG_FEATURE_TWONODE) && total_nodes <= 2)
		newquorum = 1;

	if (ret_total_votes)
		*ret_total_votes = total_votes;

	LEAVE();
	return newquorum;
}

/* Recalculate cluster quorum, set quorate and notify changes */
static void recalculate_quorum(int allow_decrease, int by_current_nodes)
{
	unsigned int total_votes = 0;
	int cluster_members = 0;
	struct list_head *nodelist;
	struct cluster_node *node;

	ENTER();

	list_iterate(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		if (node->state == NODESTATE_MEMBER) {
			if (by_current_nodes)
				cluster_members++;
			total_votes += node->votes;
		}
	}

	/* Keep expected_votes at the highest number of votes in the cluster */
	log_printf(LOGSYS_LEVEL_DEBUG, "total_votes=%d, expected_votes=%d\n", total_votes, us->expected_votes);
	if (total_votes > us->expected_votes) {
		us->expected_votes = total_votes;
		send_expectedvotes_notification();
	}

	quorum = calculate_quorum(allow_decrease, cluster_members, &total_votes);
	set_quorate(total_votes);

	send_quorum_notification(NULL, 0L);
	LEAVE();
}

static int have_disallowed(void)
{
	struct cluster_node *node;
	struct list_head *tmp;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (node->state == NODESTATE_DISALLOWED)
			return 1;
	}

	return 0;
}

static void node_add_ordered(struct cluster_node *newnode)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &newnode->list;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);

                if (newnode->node_id < node->node_id)
                        break;
        }

        if (!node)
		list_add(&newnode->list, &cluster_members_list);
        else {
                newlist->prev = tmp->prev;
                newlist->next = tmp;
                tmp->prev->next = newlist;
                tmp->prev = newlist;
        }
}

static struct cluster_node *allocate_node(int nodeid)
{
	struct cluster_node *cl;

	cl = malloc(sizeof(struct cluster_node));
	if (cl) {
		memset(cl, 0, sizeof(struct cluster_node));
		cl->node_id = nodeid;
		if (nodeid)
			node_add_ordered(cl);
	}
	return cl;
}

static struct cluster_node *find_node_by_nodeid(int nodeid)
{
	struct cluster_node *node;
	struct list_head *tmp;

	if (nodeid == NODEID_US)
		return us;

	if (nodeid == NODEID_QDEVICE)
		return quorum_device;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (node->node_id == nodeid)
			return node;
	}
	return NULL;
}


static int quorum_exec_send_nodeinfo()
{
	struct req_exec_quorum_nodeinfo req_exec_quorum_nodeinfo;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_nodeinfo.expected_votes = us->expected_votes;
	req_exec_quorum_nodeinfo.votes = us->votes;
	req_exec_quorum_nodeinfo.major_version = VOTEQUORUM_MAJOR_VERSION;
	req_exec_quorum_nodeinfo.minor_version = VOTEQUORUM_MINOR_VERSION;
	req_exec_quorum_nodeinfo.patch_version = VOTEQUORUM_PATCH_VERSION;
	req_exec_quorum_nodeinfo.flags = us->flags;
	req_exec_quorum_nodeinfo.first_trans = first_trans;
	if (have_disallowed())
		req_exec_quorum_nodeinfo.flags |= NODE_FLAGS_SEESDISALLOWED;

	req_exec_quorum_nodeinfo.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_NODEINFO);
	req_exec_quorum_nodeinfo.header.size = sizeof(req_exec_quorum_nodeinfo);

	iov[0].iov_base = (void *)&req_exec_quorum_nodeinfo;
	iov[0].iov_len = sizeof(req_exec_quorum_nodeinfo);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}


static int quorum_exec_send_reconfigure(int param, int nodeid, int value)
{
	struct req_exec_quorum_reconfigure req_exec_quorum_reconfigure;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_reconfigure.param = param;
	req_exec_quorum_reconfigure.nodeid = nodeid;
	req_exec_quorum_reconfigure.value = value;

	req_exec_quorum_reconfigure.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_RECONFIGURE);
	req_exec_quorum_reconfigure.header.size = sizeof(req_exec_quorum_reconfigure);

	iov[0].iov_base = (void *)&req_exec_quorum_reconfigure;
	iov[0].iov_len = sizeof(req_exec_quorum_reconfigure);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int quorum_exec_send_killnode(int nodeid, unsigned int reason)
{
	struct req_exec_quorum_killnode req_exec_quorum_killnode;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_killnode.nodeid = nodeid;
	req_exec_quorum_killnode.reason = reason;

	req_exec_quorum_killnode.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_KILLNODE);
	req_exec_quorum_killnode.header.size = sizeof(req_exec_quorum_killnode);

	iov[0].iov_base = (void *)&req_exec_quorum_killnode;
	iov[0].iov_len = sizeof(req_exec_quorum_killnode);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static void quorum_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;
	int leaving = 0;
	struct cluster_node *node;

	ENTER();
	if (member_list_entries > 1)
		first_trans = 0;

	if (left_list_entries) {
		for (i = 0; i< left_list_entries; i++) {
			node = find_node_by_nodeid(left_list[i]);
			if (node) {
				if (node->state == NODESTATE_LEAVING)
					leaving = 1;
				node->state = NODESTATE_DEAD;
				node->flags |= NODE_FLAGS_BEENDOWN;
			}
		}
	}

	if (member_list_entries) {
		memcpy(quorum_members, member_list, sizeof(unsigned int) * member_list_entries);
		quorum_members_entries = member_list_entries;
		if (quorum_device) {
			quorum_members[quorum_members_entries++] = 0;
		}
		quorum_exec_send_nodeinfo();
	}

	if (left_list_entries)
		recalculate_quorum(leaving, leaving);

	memcpy(&quorum_ringid, ring_id, sizeof(*ring_id));
	LEAVE();
}

static void exec_votequorum_nodeinfo_endian_convert (void *msg)
{
	struct req_exec_quorum_nodeinfo *nodeinfo = msg;

	nodeinfo->votes = swab32(nodeinfo->votes);
	nodeinfo->expected_votes = swab32(nodeinfo->expected_votes);
	nodeinfo->major_version = swab32(nodeinfo->major_version);
	nodeinfo->minor_version = swab32(nodeinfo->minor_version);
	nodeinfo->patch_version = swab32(nodeinfo->patch_version);
	nodeinfo->config_version = swab32(nodeinfo->config_version);
	nodeinfo->flags = swab32(nodeinfo->flags);
}

static void exec_votequorum_reconfigure_endian_convert (void *msg)
{
	struct req_exec_quorum_reconfigure *reconfigure = msg;
	reconfigure->nodeid = swab32(reconfigure->nodeid);
	reconfigure->value = swab32(reconfigure->value);
}

static void exec_votequorum_killnode_endian_convert (void *msg)
{
	struct req_exec_quorum_killnode *killnode = msg;
	killnode->reason = swab16(killnode->reason);
	killnode->nodeid = swab32(killnode->nodeid);
}

static void message_handler_req_exec_votequorum_nodeinfo (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_nodeinfo *req_exec_quorum_nodeinfo = message;
	struct cluster_node *node;
	int old_votes;
	int old_expected;
	nodestate_t old_state;
	int new_node = 0;

	ENTER();
	log_printf(LOGSYS_LEVEL_DEBUG, "got nodeinfo message from cluster node %d\n", nodeid);

	node = find_node_by_nodeid(nodeid);
	if (!node) {
		node = allocate_node(nodeid);
		new_node = 1;
	}
	if (!node) {
		corosync_api->error_memory_failure();
		return;
	}

	/*
	 * If the node sending the message sees disallowed nodes and we don't, then
	 * we have to leave
	 */
	if (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_SEESDISALLOWED && !have_disallowed()) {
		/* Must use syslog directly here or the message will never arrive */
		syslog(LOGSYS_LEVEL_CRIT, "[VOTEQ]: Joined a cluster with disallowed nodes. must die");
		corosync_api->fatal_error(2, __FILE__, __LINE__);
		exit(2);
	}
	old_votes = node->votes;
	old_expected = node->expected_votes;
	old_state = node->state;

	/* Update node state */
	node->votes = req_exec_quorum_nodeinfo->votes;
	node->expected_votes = req_exec_quorum_nodeinfo->expected_votes;
	node->state = NODESTATE_MEMBER;

	log_printf(LOGSYS_LEVEL_DEBUG, "nodeinfo message: votes: %d, expected:%d\n", req_exec_quorum_nodeinfo->votes, req_exec_quorum_nodeinfo->expected_votes);

	/* Check flags for disallowed (if enabled) */
	if (quorum_flags & VOTEQUORUM_FLAG_FEATURE_DISALLOWED) {
		if ((req_exec_quorum_nodeinfo->flags & NODE_FLAGS_HASSTATE && node->flags & NODE_FLAGS_BEENDOWN) ||
		    (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_HASSTATE && req_exec_quorum_nodeinfo->first_trans && !(node->flags & NODE_FLAGS_US) && (us->flags & NODE_FLAGS_HASSTATE))) {
			if (node->state != NODESTATE_DISALLOWED) {
				if (cluster_is_quorate) {
					log_printf(LOGSYS_LEVEL_CRIT, "Killing node %d because it has rejoined the cluster with existing state", node->node_id);
					node->state = NODESTATE_DISALLOWED;
					quorum_exec_send_killnode(nodeid, VOTEQUORUM_REASON_KILL_REJOIN);
				}
				else {
					log_printf(LOGSYS_LEVEL_CRIT, "Node %d not joined to quorum because it has existing state", node->node_id);
					node->state = NODESTATE_DISALLOWED;
				}
			}
		}
	}
	node->flags &= ~NODE_FLAGS_BEENDOWN;

	if (new_node || req_exec_quorum_nodeinfo->first_trans || 
	    old_votes != node->votes || old_expected != node->expected_votes || old_state != node->state)
		recalculate_quorum(0, 0);

	if (!nodeid) {
		free(node);
	}

	LEAVE();
}

static void message_handler_req_exec_votequorum_killnode (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_killnode *req_exec_quorum_killnode = message;

	if (req_exec_quorum_killnode->nodeid == corosync_api->totem_nodeid_get()) {
		log_printf(LOGSYS_LEVEL_CRIT, "Killed by node %d: %s\n", nodeid, kill_reason(req_exec_quorum_killnode->reason));

		corosync_api->fatal_error(1, __FILE__, __LINE__);
		exit(1);
	}
}

static void message_handler_req_exec_votequorum_reconfigure (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_reconfigure *req_exec_quorum_reconfigure = message;
	struct cluster_node *node;
	struct list_head *nodelist;

	log_printf(LOGSYS_LEVEL_DEBUG, "got reconfigure message from cluster node %d\n", nodeid);

	node = find_node_by_nodeid(req_exec_quorum_reconfigure->nodeid);
	if (!node)
		return;

	switch(req_exec_quorum_reconfigure->param)
	{
	case RECONFIG_PARAM_EXPECTED_VOTES:
		list_iterate(nodelist, &cluster_members_list) {
			node = list_entry(nodelist, struct cluster_node, list);
			if (node->state == NODESTATE_MEMBER &&
			    node->expected_votes > req_exec_quorum_reconfigure->value) {
				node->expected_votes = req_exec_quorum_reconfigure->value;
			}
		}
		send_expectedvotes_notification();
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_NODE_VOTES:
		node->votes = req_exec_quorum_reconfigure->value;
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_LEAVING:
		if (req_exec_quorum_reconfigure->value == 1 && node->state == NODESTATE_MEMBER)
			node->state = NODESTATE_LEAVING;
		if (req_exec_quorum_reconfigure->value == 0 && node->state == NODESTATE_LEAVING)
			node->state = NODESTATE_MEMBER;
		break;
	}
}

static int quorum_lib_init_fn (void *conn)
{
	struct quorum_pd *pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();

	list_init (&pd->list);
	pd->conn = conn;

	LEAVE();
	return (0);
}

/*
 * Someone called votequorum_leave AGES ago!
 * Assume they forgot to shut down the node.
 */
static void leaving_timer_fn(void *arg)
{
	ENTER();

	if (us->state == NODESTATE_LEAVING)
		us->state = NODESTATE_MEMBER;

	/* Tell everyone else we made a mistake */
	quorum_exec_send_reconfigure(RECONFIG_PARAM_LEAVING, us->node_id, 0);
	LEAVE();
}

/* Message from the library */
static void message_handler_req_lib_votequorum_getinfo (void *conn, const void *message)
{
	const struct req_lib_votequorum_getinfo *req_lib_votequorum_getinfo = message;
	struct res_lib_votequorum_getinfo res_lib_votequorum_getinfo;
	struct cluster_node *node;
	unsigned int highest_expected = 0;
	unsigned int total_votes = 0;
	cs_error_t error = CS_OK;

	log_printf(LOGSYS_LEVEL_DEBUG, "got getinfo request on %p for node %d\n", conn, req_lib_votequorum_getinfo->nodeid);

	node = find_node_by_nodeid(req_lib_votequorum_getinfo->nodeid);
	if (node) {
		struct cluster_node *iternode;
		struct list_head *nodelist;

		list_iterate(nodelist, &cluster_members_list) {
			iternode = list_entry(nodelist, struct cluster_node, list);

			if (iternode->state == NODESTATE_MEMBER) {
				highest_expected =
					max(highest_expected, iternode->expected_votes);
				total_votes += iternode->votes;
			}
		}

		if (quorum_device && quorum_device->state == NODESTATE_MEMBER) {
			total_votes += quorum_device->votes;
		}

		res_lib_votequorum_getinfo.votes = us->votes;
		res_lib_votequorum_getinfo.expected_votes = us->expected_votes;
		res_lib_votequorum_getinfo.highest_expected = highest_expected;

		res_lib_votequorum_getinfo.quorum = quorum;
		res_lib_votequorum_getinfo.total_votes = total_votes;
		res_lib_votequorum_getinfo.flags = 0;
		res_lib_votequorum_getinfo.nodeid = node->node_id;

		if (us->flags & NODE_FLAGS_HASSTATE)
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_FLAG_HASSTATE;
		if (quorum_flags & VOTEQUORUM_FLAG_FEATURE_TWONODE)
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_FLAG_TWONODE;
		if (cluster_is_quorate)
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_FLAG_QUORATE;
		if (us->flags & NODE_FLAGS_SEESDISALLOWED)
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_FLAG_DISALLOWED;
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	res_lib_votequorum_getinfo.header.size = sizeof(res_lib_votequorum_getinfo);
	res_lib_votequorum_getinfo.header.id = MESSAGE_RES_VOTEQUORUM_GETINFO;
	res_lib_votequorum_getinfo.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_getinfo, sizeof(res_lib_votequorum_getinfo));
	log_printf(LOGSYS_LEVEL_DEBUG, "getinfo response error: %d\n", error);
}

/* Message from the library */
static void message_handler_req_lib_votequorum_setexpected (void *conn, const void *message)
{
	const struct req_lib_votequorum_setexpected *req_lib_votequorum_setexpected = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;
	unsigned int newquorum;
	unsigned int total_votes;

	ENTER();

	/*
	 * If there are disallowed nodes, then we can't allow the user
	 * to bypass them by fiddling with expected votes.
	 */
	if (quorum_flags & VOTEQUORUM_FLAG_FEATURE_DISALLOWED && have_disallowed()) {
		error = CS_ERR_EXIST;
		goto error_exit;
	}

	/* Validate new expected votes */
	newquorum = calculate_quorum(1, req_lib_votequorum_setexpected->expected_votes, &total_votes);
	if (newquorum < total_votes / 2
	    || newquorum > total_votes) {
		error = CS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	quorum_exec_send_reconfigure(RECONFIG_PARAM_EXPECTED_VOTES, us->node_id, req_lib_votequorum_setexpected->expected_votes);

	/* send status */
error_exit:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));
	LEAVE();
}

/* Message from the library */
static void message_handler_req_lib_votequorum_setvotes (void *conn, const void *message)
{
	const struct req_lib_votequorum_setvotes *req_lib_votequorum_setvotes = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	struct cluster_node *node;
	unsigned int newquorum;
	unsigned int total_votes;
	unsigned int saved_votes;
	cs_error_t error = CS_OK;
	unsigned int nodeid;

	ENTER();

	nodeid = req_lib_votequorum_setvotes->nodeid;
	node = find_node_by_nodeid(nodeid);
	if (!node) {
		error = CS_ERR_NAME_NOT_FOUND;
		goto error_exit;
	}

	/* Check votes is valid */
	saved_votes = node->votes;
	node->votes = req_lib_votequorum_setvotes->votes;

	newquorum = calculate_quorum(1, 0, &total_votes);

	if (newquorum < total_votes / 2 || newquorum > total_votes) {
		node->votes = saved_votes;
		error = CS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (!nodeid)
		nodeid = corosync_api->totem_nodeid_get();

	quorum_exec_send_reconfigure(RECONFIG_PARAM_NODE_VOTES, nodeid,
				     req_lib_votequorum_setvotes->votes);

error_exit:
	/* send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));
	LEAVE();
}

static void message_handler_req_lib_votequorum_leaving (void *conn, const void *message)
{
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	quorum_exec_send_reconfigure(RECONFIG_PARAM_LEAVING, us->node_id, 1);

	/*
	 * If we don't shut down in a sensible amount of time then cancel the
	 * leave status.
	 */
	if (leaving_timeout)
		corosync_api->timer_add_duration((unsigned long long)leaving_timeout*1000000, NULL,
						 leaving_timer_fn, &leaving_timer);


	/* send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));
	LEAVE();
}

static void quorum_device_timer_fn(void *arg)
{
	ENTER();
	if (!quorum_device || quorum_device->state == NODESTATE_DEAD)
		return;

	if ( (quorum_device->last_hello / TIMERLIST_NS_IN_SEC) + quorumdev_poll/1000 <
		(timerlist_nano_current_get () / TIMERLIST_NS_IN_SEC)) {

		quorum_device->state = NODESTATE_DEAD;
		log_printf(LOGSYS_LEVEL_INFO, "lost contact with quorum device\n");
		recalculate_quorum(0, 0);
	}
	else {
		corosync_api->timer_add_duration((unsigned long long)quorumdev_poll*1000000, quorum_device,
						 quorum_device_timer_fn, &quorum_device_timer);
	}
	LEAVE();
}


static void message_handler_req_lib_votequorum_qdisk_register (void *conn,
							       const void *message)
{
	const struct req_lib_votequorum_qdisk_register
	  *req_lib_votequorum_qdisk_register = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		error = CS_ERR_EXIST;
	}
	else {
		quorum_device = allocate_node(0);
		quorum_device->state = NODESTATE_DEAD;
		quorum_device->votes = req_lib_votequorum_qdisk_register->votes;
		strcpy(quorum_device_name, req_lib_votequorum_qdisk_register->name);
		list_add(&quorum_device->list, &cluster_members_list);
	}

	/* send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));
	LEAVE();
}

static void message_handler_req_lib_votequorum_qdisk_unregister (void *conn,
								 const void *message)
{
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		struct cluster_node *node = quorum_device;

		quorum_device = NULL;
		list_del(&node->list);
		free(node);
		recalculate_quorum(0, 0);
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));
	LEAVE();
}

static void message_handler_req_lib_votequorum_qdisk_poll (void *conn,
							   const void *message)
{
	const struct req_lib_votequorum_qdisk_poll
	  *req_lib_votequorum_qdisk_poll = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		if (req_lib_votequorum_qdisk_poll->state) {
			quorum_device->last_hello = timerlist_nano_current_get ();
			if (quorum_device->state == NODESTATE_DEAD) {
				quorum_device->state = NODESTATE_MEMBER;
				recalculate_quorum(0, 0);

				corosync_api->timer_add_duration((unsigned long long)quorumdev_poll*1000000, quorum_device,
								 quorum_device_timer_fn, &quorum_device_timer);
			}
		}
		else {
			if (quorum_device->state == NODESTATE_MEMBER) {
				quorum_device->state = NODESTATE_DEAD;
				recalculate_quorum(0, 0);
				corosync_api->timer_delete(quorum_device_timer);
			}
		}
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdisk_getinfo (void *conn,
							      const void *message)
{
	struct res_lib_votequorum_qdisk_getinfo res_lib_votequorum_qdisk_getinfo;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		log_printf(LOGSYS_LEVEL_DEBUG, "got qdisk_getinfo state %d\n", quorum_device->state);
		res_lib_votequorum_qdisk_getinfo.votes = quorum_device->votes;
		if (quorum_device->state == NODESTATE_MEMBER)
			res_lib_votequorum_qdisk_getinfo.state = 1;
		else
			res_lib_votequorum_qdisk_getinfo.state = 0;
		strcpy(res_lib_votequorum_qdisk_getinfo.name, quorum_device_name);
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_votequorum_qdisk_getinfo.header.size = sizeof(res_lib_votequorum_qdisk_getinfo);
	res_lib_votequorum_qdisk_getinfo.header.id = MESSAGE_RES_VOTEQUORUM_GETINFO;
	res_lib_votequorum_qdisk_getinfo.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_qdisk_getinfo, sizeof(res_lib_votequorum_qdisk_getinfo));

	LEAVE();
}

static void message_handler_req_lib_votequorum_setstate (void *conn,
							 const void *message)
{
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	us->flags |= NODE_FLAGS_HASSTATE;

	/* send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_trackstart (void *conn,
							   const void *msg)
{
	const struct req_lib_votequorum_trackstart
	  *req_lib_votequorum_trackstart = msg;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();
	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_votequorum_trackstart->track_flags & CS_TRACK_CURRENT ||
	    req_lib_votequorum_trackstart->track_flags & CS_TRACK_CHANGES) {
		log_printf(LOGSYS_LEVEL_DEBUG, "sending initial status to %p\n", conn);
		send_quorum_notification(conn, req_lib_votequorum_trackstart->context);
	}

	/*
	 * Record requests for tracking
	 */
	if (req_lib_votequorum_trackstart->track_flags & CS_TRACK_CHANGES ||
	    req_lib_votequorum_trackstart->track_flags & CS_TRACK_CHANGES_ONLY) {

		quorum_pd->track_flags = req_lib_votequorum_trackstart->track_flags;
		quorum_pd->tracking_enabled = 1;
		quorum_pd->tracking_context = req_lib_votequorum_trackstart->context;

		list_add (&quorum_pd->list, &trackers_list);
	}

	/* Send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = CS_OK;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_trackstop (void *conn,
							  const void *msg)
{
	struct res_lib_votequorum_status res_lib_votequorum_status;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);
	int error = CS_OK;

	ENTER();

	if (quorum_pd->tracking_enabled) {
		error = CS_OK;
		quorum_pd->tracking_enabled = 0;
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	} else {
		error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}


static const char *kill_reason(int reason)
{
	static char msg[1024];

	switch (reason)
	{
	case VOTEQUORUM_REASON_KILL_REJECTED:
		return "our membership application was rejected";

	case VOTEQUORUM_REASON_KILL_APPLICATION:
		return "we were killed by an application request";

	case VOTEQUORUM_REASON_KILL_REJOIN:
		return "we rejoined the cluster without a full restart";

	default:
		sprintf(msg, "we got kill message number %d", reason);
		return msg;
	}
}

static void reread_config(hdb_handle_t object_handle)
{
	unsigned int old_votes;
	unsigned int old_expected;

	old_votes = us->votes;
	old_expected = us->expected_votes;

	/*
	 * Reload the configuration
	 */
	read_quorum_config(object_handle);

	/*
	 * Check for fundamental changes that we need to propogate
	 */
	if (old_votes != us->votes) {
		quorum_exec_send_reconfigure(RECONFIG_PARAM_NODE_VOTES, us->node_id, us->votes);
	}
	if (old_expected != us->expected_votes) {
		quorum_exec_send_reconfigure(RECONFIG_PARAM_EXPECTED_VOTES, us->node_id, us->expected_votes);
	}
}

static void quorum_key_change_notify(object_change_type_t change_type,
				     hdb_handle_t parent_object_handle,
				     hdb_handle_t object_handle,
				     const void *object_name_pt,
				     size_t object_name_len,
				     const void *key_name_pt, size_t key_len,
				     const void *key_value_pt, size_t key_value_len,
				     void *priv_data_pt)
{
	if (memcmp(object_name_pt, "quorum", object_name_len) == 0)
		reread_config(object_handle);
}


/* Called when the objdb is reloaded */
static void votequorum_objdb_reload_notify(
	objdb_reload_notify_type_t type, int flush,
	void *priv_data_pt)
{
	/*
	 * A new quorum {} key might exist, cancel the
	 * existing notification at the start of reload,
	 * and start a new one on the new object when
	 * it's all settled.
	 */

	if (type == OBJDB_RELOAD_NOTIFY_START) {
		corosync_api->object_track_stop(
			quorum_key_change_notify,
			NULL,
			NULL,
			NULL,
			NULL);
	}

	if (type == OBJDB_RELOAD_NOTIFY_END ||
	    type == OBJDB_RELOAD_NOTIFY_FAILED) {
		hdb_handle_t find_handle;
		hdb_handle_t object_handle;

		corosync_api->object_find_create(OBJECT_PARENT_HANDLE, "quorum", strlen("quorum"), &find_handle);
		if (corosync_api->object_find_next(find_handle, &object_handle) == 0) {
			add_votequorum_config_notification(object_handle);

			reread_config(object_handle);
		}
		else {
			log_printf(LOGSYS_LEVEL_ERROR, "votequorum objdb tracking stopped, cannot find quorum{} handle in objdb\n");
		}
		corosync_api->object_find_destroy(find_handle);
	}
}


static void add_votequorum_config_notification(
	hdb_handle_t quorum_object_handle)
{

	corosync_api->object_track_start(quorum_object_handle,
					 1,
					 quorum_key_change_notify,
					 NULL,
					 NULL,
					 NULL,
					 NULL);

}
