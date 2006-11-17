/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 * Copyright (C) 2006 Sun Microsystems, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#if defined(OPENAIS_LINUX)
#include <sys/sysinfo.h>
#elif defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
#include <sys/sysctl.h>
#elif defined(OPENAIS_SOLARIS)
#include <utmpx.h>
#endif
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "totem.h"
#include "../include/saAis.h"
#include "../include/saClm.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_clm.h"
#include "../include/mar_gen.h"
#include "../include/mar_clm.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "totempg.h"
#include "main.h"
#include "ipc.h"
#include "mempool.h"
#include "service.h"
#include "print.h"

enum clm_message_req_types {
	MESSAGE_REQ_EXEC_CLM_NODEJOIN = 0
};

mar_clm_cluster_change_t my_cluster_node_last_change = SA_CLM_NODE_JOINED;

mar_clm_cluster_node_t my_cluster_node;

static mar_clm_cluster_node_t cluster_node_entries[PROCESSOR_COUNT_MAX];

static int cluster_node_count = 0;

static unsigned long long view_current = 0;

static unsigned long long view_initial = 0;

static DECLARE_LIST_INIT (library_notification_send_listhead);

SaClmClusterNodeT *clm_get_by_nodeid (unsigned int node_id)
{
	static SaClmClusterNodeT cluster_node;
	int i;

	if (node_id == SA_CLM_LOCAL_NODE_ID) {
		marshall_from_mar_clm_cluster_node_t (
			&cluster_node,
			&cluster_node_entries[0]);
		return (&cluster_node);
	}
	for (i = 0; i < cluster_node_count; i++) {
		if (cluster_node_entries[i].node_id == node_id) {
			marshall_from_mar_clm_cluster_node_t (
				&cluster_node,
				&cluster_node_entries[i]);
			return (&cluster_node);
			break;
		}
	}
	return (NULL);
}

/*
 * Service Interfaces required by service_message_handler struct
 */
static void clm_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static void clm_sync_init (void);

static int clm_sync_process (void);

static void clm_sync_activate (void);

static void clm_sync_abort (void);

static int clm_exec_init_fn (struct objdb_iface_ver0 *objdb);

static int clm_lib_init_fn (void *conn);

static int clm_lib_exit_fn (void *conn);

static void message_handler_req_exec_clm_nodejoin (
	void *message,
	unsigned int nodeid);

static void exec_clm_nodejoin_endian_convert (void *msg);

static void message_handler_req_lib_clm_clustertrack (void *conn, void *message);

static void message_handler_req_lib_clm_trackstop (void *conn, void *message);

static void message_handler_req_lib_clm_nodeget (void *conn, void *message);

static void message_handler_req_lib_clm_nodegetasync (void *conn, void *message);

struct clm_pd {
	unsigned char track_flags;
	int tracking_enabled;
	struct list_head list;
	void *conn;
};

/*
 * Executive Handler Definition
 */
static struct openais_lib_handler clm_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_clm_clustertrack,
		.response_size				= sizeof (struct res_lib_clm_clustertrack),
		.response_id				= MESSAGE_RES_CLM_TRACKSTART, // TODO RESPONSE
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_clm_trackstop,
		.response_size				= sizeof (struct res_lib_clm_trackstop),
		.response_id				= MESSAGE_RES_CLM_TRACKSTOP, // TODO RESPONSE
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_clm_nodeget,
		.response_size				= sizeof (struct res_clm_nodeget),
		.response_id				= MESSAGE_RES_CLM_NODEGET, // TODO RESPONSE
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_clm_nodegetasync,
		.response_size				= sizeof (struct res_clm_nodegetasync),
		.response_id				= MESSAGE_RES_CLM_NODEGETCALLBACK, // TODO RESPONSE
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct openais_exec_handler clm_exec_service[] =
{
	{
		.exec_handler_fn	= message_handler_req_exec_clm_nodejoin,
		.exec_endian_convert_fn	= exec_clm_nodejoin_endian_convert
	}
};
	
struct openais_service_handler clm_service_handler = {
	.name			= "openais cluster membership service B.01.01",
	.id			= CLM_SERVICE,
	.private_data_size	= sizeof (struct clm_pd),
	.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED, 
	.lib_init_fn		= clm_lib_init_fn,
	.lib_exit_fn		= clm_lib_exit_fn,
	.lib_service		= clm_lib_service,
	.lib_service_count	= sizeof (clm_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn		= clm_exec_init_fn,
	.exec_dump_fn		= NULL,
	.exec_service		= clm_exec_service,
	.exec_service_count	= sizeof (clm_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn		= clm_confchg_fn,
	.sync_init		= clm_sync_init,
	.sync_process		= clm_sync_process,
	.sync_activate		= clm_sync_activate,
	.sync_abort		= clm_sync_abort,
};

/*
 * Dynamic loader definition
 */
static struct openais_service_handler *clm_get_service_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 clm_service_handler_iface = {
	.openais_get_service_handler_ver0	= clm_get_service_handler_ver0
};

static struct lcr_iface openais_clm_ver0[1] = {
	{
		.name			= "openais_clm",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL
	}
};

static struct lcr_comp clm_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_clm_ver0
};

static struct openais_service_handler *clm_get_service_handler_ver0 (void)
{
	return (&clm_service_handler);
}

__attribute__ ((constructor)) static void clm_comp_register (void) {
	lcr_interfaces_set (&openais_clm_ver0[0], &clm_service_handler_iface);

	lcr_component_register (&clm_comp_ver0);
}

struct req_exec_clm_nodejoin {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_clm_cluster_node_t cluster_node __attribute__((aligned(8)));
};

static int clm_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	log_init ("CLM");

	memset (cluster_node_entries, 0,
		sizeof (mar_clm_cluster_node_t) * PROCESSOR_COUNT_MAX);

	/*
	 * Build local cluster node data structure
	 */
	sprintf ((char *)my_cluster_node.node_address.value, "%s",
		totemip_print (this_ip));
	my_cluster_node.node_address.length =
		strlen ((char *)my_cluster_node.node_address.value);
	if (this_ip->family == AF_INET) {
		my_cluster_node.node_address.family = SA_CLM_AF_INET;
	} else
	if (this_ip->family == AF_INET6) {
		my_cluster_node.node_address.family = SA_CLM_AF_INET6;
	} else {
		assert (0);
	}

	strcpy ((char *)my_cluster_node.node_name.value,
		(char *)my_cluster_node.node_address.value);
	my_cluster_node.node_name.length =
		my_cluster_node.node_address.length;
	my_cluster_node.node_id = this_ip->nodeid;
	my_cluster_node.member = 1;
	{
#ifndef NANOSEC
#define	NANOSEC	1000000000
#endif
#if defined(OPENAIS_LINUX)
		struct sysinfo s_info;
		time_t current_time;
		sysinfo (&s_info);
		current_time = time (NULL);
		 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
		my_cluster_node.boot_timestamp = ((SaTimeT)(current_time - s_info.uptime)) * NANOSEC;
#elif defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
		int mib[2] = { CTL_KERN, KERN_BOOTTIME };
		struct timeval boot_time;
		size_t size = sizeof(boot_time);
		
		if ( sysctl(mib, 2, &boot_time, &size, NULL, 0) == -1 )
			boot_time.tv_sec = time (NULL);
		 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
		my_cluster_node.boot_timestamp = ((SaTimeT)boot_time.tv_sec) * NANOSEC;
#elif defined(OPENAIS_SOLARIS)
		struct utmpx ut, *utp;
		ut.ut_type = BOOT_TIME;
		setutxent();
		if ((utp = getutxid(&ut)) == NULL) {
			my_cluster_node.boot_timestamp = ((SaTimeT)time(NULL)) * NANOSEC;
		} else {
			my_cluster_node.boot_timestamp = ((SaTimeT)utp->ut_xtime) * NANOSEC;
		}
		endutxent();
#else
	#warning "no bootime support"
#endif
	}

	memcpy (&cluster_node_entries[0], &my_cluster_node, sizeof (mar_clm_cluster_node_t));
	cluster_node_count = 1;

	main_clm_get_by_nodeid = clm_get_by_nodeid;
	return (0);
}

static int clm_lib_exit_fn (void *conn)
{
	struct clm_pd *clm_pd = (struct clm_pd *)openais_conn_private_data_get (conn);
	/*
	 * Delete track entry if there is one
	 */
	list_del (&clm_pd->list);
	clm_pd->conn = conn;

	return (0);
}

static void library_notification_send (
	mar_clm_cluster_notification_t *cluster_notification_entries,
	int notify_count)
{
	struct res_lib_clm_clustertrack res_lib_clm_clustertrack;
	struct clm_pd *clm_pd;
	struct list_head *list;
	int i;

	if (notify_count == 0) {
		return;
	}

	res_lib_clm_clustertrack.header.size = sizeof (struct res_lib_clm_clustertrack);
	res_lib_clm_clustertrack.header.id = MESSAGE_RES_CLM_TRACKCALLBACK;
	res_lib_clm_clustertrack.header.error = SA_AIS_OK;
	res_lib_clm_clustertrack.view = view_current;

	for (list = library_notification_send_listhead.next;
		list != &library_notification_send_listhead;
		list = list->next) {

		clm_pd = list_entry (list, struct clm_pd, list);

		/*
		 * Track current and changes
		 */
		if (clm_pd->track_flags & SA_TRACK_CHANGES) {
			/*
			 * Copy all cluster nodes
			 */
			for (i = 0; i < cluster_node_count; i++) {
				memcpy (&res_lib_clm_clustertrack.notification[i].cluster_node,
					&cluster_node_entries[i], sizeof (mar_clm_cluster_node_t));
				res_lib_clm_clustertrack.notification[i].cluster_change = SA_CLM_NODE_NO_CHANGE;
				res_lib_clm_clustertrack.notification[i].cluster_node.member = 1;
			}
			/*
			 * Copy change_only notificaiton
			 */
			res_lib_clm_clustertrack.number_of_items = notify_count + i;
			memcpy (&res_lib_clm_clustertrack.notification[i],
				cluster_notification_entries,
				sizeof (mar_clm_cluster_notification_t) * notify_count);
		} else

		/*
		 * Track only changes
		 */
		if (clm_pd->track_flags & SA_TRACK_CHANGES_ONLY) {
			res_lib_clm_clustertrack.number_of_items = notify_count;
			memcpy (&res_lib_clm_clustertrack.notification,
				cluster_notification_entries,
				sizeof (mar_clm_cluster_notification_t) * notify_count);
		}

		/*
		 * Send notifications to all CLM listeners
		 */
		openais_conn_send_response (
			clm_pd->conn,
			&res_lib_clm_clustertrack,
			sizeof (struct res_lib_clm_clustertrack));
	}
}

static void notification_join (mar_clm_cluster_node_t *cluster_node)
{
	mar_clm_cluster_notification_t notification;

	/*
	 * Generate notification element
	 */
	notification.cluster_change = SA_CLM_NODE_JOINED;
	notification.cluster_node.member = 1;
	memcpy (&notification.cluster_node, cluster_node,
		sizeof (mar_clm_cluster_node_t)); 
	library_notification_send (&notification, 1);
}

static void lib_notification_leave (unsigned int *nodes, int nodes_entries)
{
	mar_clm_cluster_notification_t cluster_notification[PROCESSOR_COUNT_MAX];
	int i, j;
	int notify_count;

	/*
	 * Determine notification list
	 */
	for (notify_count = 0, i = 0; i < cluster_node_count; i++) {
		for (j = 0; j < nodes_entries; j++) {
			if (cluster_node_entries[i].node_id == nodes[j]) {
				memcpy (&cluster_notification[notify_count].cluster_node, 
					&cluster_node_entries[i],
					sizeof (mar_clm_cluster_node_t));
				cluster_notification[notify_count].cluster_change = SA_CLM_NODE_LEFT;
				cluster_notification[notify_count].cluster_node.member = 0;
				notify_count += 1;
				break;
			}
		}
	}

	/*
	 * Remove entries from cluster_node_entries array
	 */
	for (i = 0; i < nodes_entries; i++) {
		for (j = 0; j < cluster_node_count;) {
			if (nodes[i] == cluster_node_entries[j].node_id) {
				cluster_node_count -= 1;
				memmove (&cluster_node_entries[j], &cluster_node_entries[j + 1],
					(cluster_node_count - j) * sizeof (mar_clm_cluster_node_t));
			} else {
				/*
				 * next cluster_node entry
				 */
				j++;
			}
		}
	}

	library_notification_send (cluster_notification, notify_count);
}

static int clm_nodejoin_send (void)
{
	struct req_exec_clm_nodejoin req_exec_clm_nodejoin;
	struct iovec req_exec_clm_iovec;
	int result;

	req_exec_clm_nodejoin.header.size = sizeof (struct req_exec_clm_nodejoin);
	req_exec_clm_nodejoin.header.id = 
		SERVICE_ID_MAKE (CLM_SERVICE, MESSAGE_REQ_EXEC_CLM_NODEJOIN);

	my_cluster_node.initial_view_number = view_initial;

	memcpy (&req_exec_clm_nodejoin.cluster_node, &my_cluster_node,
		sizeof (mar_clm_cluster_node_t));
	
	req_exec_clm_iovec.iov_base = (char *)&req_exec_clm_nodejoin;
	req_exec_clm_iovec.iov_len = sizeof (req_exec_clm_nodejoin);

	result = totempg_groups_mcast_joined (openais_group_handle, &req_exec_clm_iovec, 1, TOTEMPG_AGREED);

	return (result);
}

static void clm_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;
	unsigned int node_ids[PROCESSOR_COUNT_MAX];

	view_current = ring_id->seq / 4;
	if (view_initial == 0) {
		view_initial = ring_id->seq / 4;
	}

	log_printf (LOG_LEVEL_NOTICE, "CLM CONFIGURATION CHANGE\n");
	log_printf (LOG_LEVEL_NOTICE, "New Configuration:\n");
	for (i = 0; i < member_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", totempg_ifaces_print (member_list[i]));
	}
	log_printf (LOG_LEVEL_NOTICE, "Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", totempg_ifaces_print (left_list[i]));
	}

	log_printf (LOG_LEVEL_NOTICE, "Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", totempg_ifaces_print (joined_list[i]));
	}

	for (i = 0; i < left_list_entries; i++) {
		node_ids[i] = left_list[i];
	}

	lib_notification_leave (node_ids, i);

	/*
	 * Load the my_cluster_node data structure in case we are
	 * transitioning to network interface up or down
	 */
	sprintf ((char *)my_cluster_node.node_address.value, "%s", totemip_print (this_ip));
	my_cluster_node.node_address.length = strlen ((char *)my_cluster_node.node_address.value);
	if (this_ip->family == AF_INET) {
	my_cluster_node.node_address.family = SA_CLM_AF_INET;
	} else
	if (this_ip->family == AF_INET6) {
	my_cluster_node.node_address.family = SA_CLM_AF_INET6;
	} else {
		assert (0);
	}
	strcpy ((char *)my_cluster_node.node_name.value,
		(char *)my_cluster_node.node_address.value);
	my_cluster_node.node_name.length = my_cluster_node.node_address.length;
	my_cluster_node.node_id = this_ip->nodeid;
}

/*
 * This is a noop for this service
 */
static void clm_sync_init (void)
{
	return;
}

/*
 * If a processor joined in the configuration change and clm_sync_activate hasn't
 * yet been called, issue a node join to share CLM specific data about the processor
 */
static int clm_sync_process (void)
{
	/*
	 * Send node information to other nodes
	 */
	return (clm_nodejoin_send ());
}

/*
 * This is a noop for this service
 */
static void clm_sync_activate (void)
{
	return;
}

/*
 * This is a noop for this service
 */
static void clm_sync_abort (void)
{
	return;
}


static void exec_clm_nodejoin_endian_convert (void *msg)
{
	struct req_exec_clm_nodejoin *node_join = msg;

	swab_mar_req_header_t (&node_join->header);
	swab_mar_clm_cluster_node_t (&node_join->cluster_node);
}

static void message_handler_req_exec_clm_nodejoin (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_clm_nodejoin *req_exec_clm_nodejoin = (struct req_exec_clm_nodejoin *)message;
	int found = 0;
	int i;

	log_printf (LOG_LEVEL_NOTICE, "got nodejoin message %s\n",
		req_exec_clm_nodejoin->cluster_node.node_name.value);
	
	/*
	 * Determine if nodejoin already received
	 */
	for (found = 0, i = 0; i < cluster_node_count; i++) {
		if (cluster_node_entries[i].node_id == req_exec_clm_nodejoin->cluster_node.node_id) {
			found = 1;
		}
	}

	/*
	 * If not received, add to internal list
	 */
	if (found == 0) {
		notification_join (&req_exec_clm_nodejoin->cluster_node);
		memcpy (&cluster_node_entries[cluster_node_count],
			&req_exec_clm_nodejoin->cluster_node,
			sizeof (mar_clm_cluster_node_t));

		cluster_node_count += 1;
	}
}

static int clm_lib_init_fn (void *conn)
{
	log_printf (LOG_LEVEL_DEBUG, "Got request to initalize cluster membership service.\n");
	struct clm_pd *clm_pd = (struct clm_pd *)openais_conn_private_data_get (conn);

	list_init (&clm_pd->list);
	clm_pd->conn = conn;

	return (0);
}

static void message_handler_req_lib_clm_clustertrack (void *conn, void *msg)
{
	struct req_lib_clm_clustertrack *req_lib_clm_clustertrack = (struct req_lib_clm_clustertrack *)msg;
	struct res_lib_clm_clustertrack res_lib_clm_clustertrack;
	struct clm_pd *clm_pd = (struct clm_pd *)openais_conn_private_data_get (conn);
	int i;

	res_lib_clm_clustertrack.header.size = sizeof (struct res_lib_clm_clustertrack);
	res_lib_clm_clustertrack.header.id = MESSAGE_RES_CLM_TRACKSTART;
	res_lib_clm_clustertrack.header.error = SA_AIS_OK;
	res_lib_clm_clustertrack.view = view_current;
	res_lib_clm_clustertrack.number_of_items = 0;

	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_clm_clustertrack->track_flags & SA_TRACK_CURRENT ||
		req_lib_clm_clustertrack->track_flags & SA_TRACK_CHANGES) {
		for (i = 0; i < cluster_node_count; i++) {
			res_lib_clm_clustertrack.notification[i].cluster_change = SA_CLM_NODE_NO_CHANGE;

			memcpy (&res_lib_clm_clustertrack.notification[i].cluster_node,
				&cluster_node_entries[i], sizeof (mar_clm_cluster_node_t));
		}
		res_lib_clm_clustertrack.number_of_items = cluster_node_count;
	}
	
	/*
	 * Record requests for cluster tracking
	 */
	if (req_lib_clm_clustertrack->track_flags & SA_TRACK_CHANGES ||
		req_lib_clm_clustertrack->track_flags & SA_TRACK_CHANGES_ONLY) {

		clm_pd->track_flags = req_lib_clm_clustertrack->track_flags;
		clm_pd->tracking_enabled = 1;

		list_add (&clm_pd->list, &library_notification_send_listhead);
	}

	openais_conn_send_response (conn, &res_lib_clm_clustertrack,
		sizeof (struct res_lib_clm_clustertrack));

	if (req_lib_clm_clustertrack->return_in_callback) {
		res_lib_clm_clustertrack.header.id = MESSAGE_RES_CLM_TRACKCALLBACK;

		openais_conn_send_response (
			openais_conn_partner_get (conn),
			&res_lib_clm_clustertrack,
			sizeof (struct res_lib_clm_clustertrack));
	}
}


static void message_handler_req_lib_clm_trackstop (void *conn, void *msg)
{
	struct res_lib_clm_trackstop res_lib_clm_trackstop;
	struct clm_pd *clm_pd = (struct clm_pd *)openais_conn_private_data_get (conn);

	res_lib_clm_trackstop.header.size = sizeof (struct res_lib_clm_trackstop);
	res_lib_clm_trackstop.header.id = MESSAGE_RES_CLM_TRACKSTOP;

	if (clm_pd->tracking_enabled) {
		res_lib_clm_trackstop.header.error = SA_AIS_OK;
		clm_pd->tracking_enabled = 0;
	} else {
		res_lib_clm_trackstop.header.error = SA_AIS_ERR_NOT_EXIST;
	}

	list_del (&clm_pd->list);
	list_init (&clm_pd->list);

	openais_conn_send_response (conn, &res_lib_clm_trackstop,
		sizeof (struct res_lib_clm_trackstop));
}

static void message_handler_req_lib_clm_nodeget (void *conn, void *msg)
{
	struct req_lib_clm_nodeget *req_lib_clm_nodeget = (struct req_lib_clm_nodeget *)msg;
	struct res_clm_nodeget res_clm_nodeget;
	mar_clm_cluster_node_t *cluster_node = 0;
	int valid = 0;
	int i;

	log_printf (LOG_LEVEL_NOTICE, "nodeget: trying to find node %x\n",
		(int)req_lib_clm_nodeget->node_id);

	if (req_lib_clm_nodeget->node_id == SA_CLM_LOCAL_NODE_ID) {
		cluster_node = &cluster_node_entries[0];
		valid = 1;
	} else 
	for (i = 0; i < cluster_node_count; i++) {
		if (cluster_node_entries[i].node_id == req_lib_clm_nodeget->node_id) {
			log_printf (LOG_LEVEL_DEBUG, "found host that matches one desired in nodeget.\n");
			cluster_node = &cluster_node_entries[i];
			valid = 1;
			break;
		}
	}

	res_clm_nodeget.header.size = sizeof (struct res_clm_nodeget);
	res_clm_nodeget.header.id = MESSAGE_RES_CLM_NODEGET;
	res_clm_nodeget.header.error = SA_AIS_OK;
	res_clm_nodeget.invocation = req_lib_clm_nodeget->invocation;
	res_clm_nodeget.valid = valid;
	if (valid) {
		memcpy (&res_clm_nodeget.cluster_node, cluster_node, sizeof (mar_clm_cluster_node_t));
	}
	openais_conn_send_response (conn, &res_clm_nodeget, sizeof (struct res_clm_nodeget));
}

static void message_handler_req_lib_clm_nodegetasync (void *conn, void *msg)
{
	struct req_lib_clm_nodegetasync *req_lib_clm_nodegetasync = (struct req_lib_clm_nodegetasync *)msg;
	struct res_clm_nodegetasync res_clm_nodegetasync;
	struct res_clm_nodegetcallback res_clm_nodegetcallback;
	mar_clm_cluster_node_t *cluster_node = 0;
	int error = SA_AIS_ERR_INVALID_PARAM;
	int i;

	log_printf (LOG_LEVEL_DEBUG, "nodeget: trying to find node %x\n",
		(int)req_lib_clm_nodegetasync->node_id);

	if (req_lib_clm_nodegetasync->node_id == SA_CLM_LOCAL_NODE_ID) {
		cluster_node = &cluster_node_entries[0];
		error = SA_AIS_OK;
	} else 
	for (i = 0; i < cluster_node_count; i++) {
		if (cluster_node_entries[i].node_id == req_lib_clm_nodegetasync->node_id) {
			log_printf (LOG_LEVEL_DEBUG, "found host that matches one desired in nodeget.\n");
			cluster_node = &cluster_node_entries[i];
			error = SA_AIS_OK;
			break;
		}
	}

	/*
	 * Respond to library request
	 */
	res_clm_nodegetasync.header.size = sizeof (struct res_clm_nodegetasync);
	res_clm_nodegetasync.header.id = MESSAGE_RES_CLM_NODEGETASYNC;
	res_clm_nodegetasync.header.error = SA_AIS_OK;

	openais_conn_send_response (conn, &res_clm_nodegetasync,
		sizeof (struct res_clm_nodegetasync));

	/*
	 * Send async response
	 */
	res_clm_nodegetcallback.header.size = sizeof (struct res_clm_nodegetcallback);
	res_clm_nodegetcallback.header.id = MESSAGE_RES_CLM_NODEGETCALLBACK;
	res_clm_nodegetcallback.header.error = error;
	res_clm_nodegetcallback.invocation = req_lib_clm_nodegetasync->invocation;
	if (error == SA_AIS_OK) {
		memcpy (&res_clm_nodegetcallback.cluster_node, cluster_node,
			sizeof (mar_clm_cluster_node_t));
	}
	openais_conn_send_response (openais_conn_partner_get (conn),
		&res_clm_nodegetcallback,
		sizeof (struct res_clm_nodegetcallback));
}
