/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#endif
#if defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
#include <sys/sysctl.h>
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

#include "../include/saAis.h"
#include "../include/saClm.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_clm.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "aispoll.h"
#include "totempg.h"
#include "main.h"
#include "mempool.h"
#include "service.h"

#define LOG_SERVICE LOG_SERVICE_CLM
#include "print.h"


enum clm_message_req_types {
	MESSAGE_REQ_EXEC_CLM_NODEJOIN = 0
};

SaClmClusterChangesT thisClusterNodeLastChange = SA_CLM_NODE_JOINED;
SaClmClusterNodeT thisClusterNode;

#define NODE_MAX 16

static SaClmClusterNodeT clusterNodes[NODE_MAX];

static int clusterNodeEntries = 0;

static unsigned long long view_current = 0;

static unsigned long long view_initial = 0;

static DECLARE_LIST_INIT (library_notification_send_listhead);

SaClmClusterNodeT *clm_get_by_nodeid (unsigned int node_id)
{
	SaClmClusterNodeT *ret = NULL;
	int i;

	if (node_id == SA_CLM_LOCAL_NODE_ID) {
		return (&clusterNodes[0]);
	}
	for (i = 0; i < clusterNodeEntries; i++) {
		if (clusterNodes[i].nodeId == node_id) {
			ret = &clusterNodes[i];
			break;
		}
	}
	return (ret);
}

/*
 * Service Interfaces required by service_message_handler struct
 */
static void clm_confchg_fn (
	enum totem_configuration_type configuration_type,
    struct totem_ip_address *member_list, int member_list_entries,
    struct totem_ip_address *left_list, int left_list_entries,
    struct totem_ip_address *joined_list, int joined_list_entries,
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
	struct totem_ip_address *source_addr);

static void exec_clm_nodejoin_endian_convert (void *msg);

static void message_handler_req_lib_clm_clustertrack (void *conn, void *message);

static void message_handler_req_lib_clm_trackstop (void *conn, void *message);

static void message_handler_req_lib_clm_nodeget (void *conn, void *message);

static void message_handler_req_lib_clm_nodegetasync (void *conn, void *message);

struct clm_pd {
	SaUint8T trackFlags;
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
		.exec_handler_fn		= message_handler_req_exec_clm_nodejoin,
		.exec_endian_convert_fn	= exec_clm_nodejoin_endian_convert
	}
};
	
struct openais_service_handler clm_service_handler = {
	.name						= (unsigned char*)"openais cluster membership service B.01.01",
	.id							= CLM_SERVICE,
	.private_data_size			= sizeof (struct clm_pd),
	.lib_init_fn				= clm_lib_init_fn,
	.lib_exit_fn				= clm_lib_exit_fn,
	.lib_service				= clm_lib_service,
	.lib_service_count			= sizeof (clm_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn				= clm_exec_init_fn,
	.exec_dump_fn				= NULL,
	.exec_service				= clm_exec_service,
	.exec_service_count		= sizeof (clm_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn					= clm_confchg_fn,
	.sync_init					= clm_sync_init,
	.sync_process				= clm_sync_process,
	.sync_activate				= clm_sync_activate,
	.sync_abort					= clm_sync_abort,
};

/*
 * Dynamic loader definition
 */
static struct openais_service_handler *clm_get_service_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 clm_service_handler_iface = {
	.openais_get_service_handler_ver0		= clm_get_service_handler_ver0
};

static struct lcr_iface openais_clm_ver0[1] = {
	{
		.name					= "openais_clm",
		.version				= 0,
		.versions_replace		= 0,
		.versions_replace_count = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor				= NULL,
		.interfaces				= NULL
	}
};

static struct lcr_comp clm_comp_ver0 = {
	.iface_count			= 1,
	.ifaces					= openais_clm_ver0
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
	struct req_header header;
	SaClmClusterNodeT clusterNode;
};

static int clm_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	memset (clusterNodes, 0, sizeof (SaClmClusterNodeT) * NODE_MAX);

	/*
	 * Build local cluster node data structure
	 */
	sprintf ((char *)thisClusterNode.nodeAddress.value, "%s", totemip_print (this_ip));
	thisClusterNode.nodeAddress.length = strlen ((char *)thisClusterNode.nodeAddress.value);
	if (this_ip->family == AF_INET) {
		thisClusterNode.nodeAddress.family = SA_CLM_AF_INET;
	} else
	if (this_ip->family == AF_INET6) {
		thisClusterNode.nodeAddress.family = SA_CLM_AF_INET6;
	} else {
		assert (0);
	}

	strcpy ((char *)thisClusterNode.nodeName.value, (char *)thisClusterNode.nodeAddress.value);
	thisClusterNode.nodeName.length = thisClusterNode.nodeAddress.length;
	thisClusterNode.nodeId = this_ip->nodeid;
	thisClusterNode.member = 1;
	{
#if defined(OPENAIS_LINUX)
		struct sysinfo s_info;
		time_t current_time;
		sysinfo (&s_info);
		current_time = time (NULL);
		 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
		thisClusterNode.bootTimestamp = ((SaTimeT)(current_time - s_info.uptime)) * 1000000000;
#elif defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
		int mib[2] = { CTL_KERN, KERN_BOOTTIME };
		struct timeval boot_time;
		size_t size = sizeof(boot_time);
		
		if ( sysctl(mib, 2, &boot_time, &size, NULL, 0) == -1 )
			boot_time.tv_sec = time (NULL);
		 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
		thisClusterNode.bootTimestamp = ((SaTimeT)boot_time.tv_sec) * 1000000000;
#else /* defined(CTL_KERN) && defined(KERN_BOOTTIME) */
	#warning "no bootime support"
#endif
	}

	memcpy (&clusterNodes[0], &thisClusterNode, sizeof (SaClmClusterNodeT));
	clusterNodeEntries = 1;

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

static void library_notification_send (SaClmClusterNotificationT *cluster_notification_entries,
	int notify_entries)
{
	struct res_lib_clm_clustertrack res_lib_clm_clustertrack;
	struct clm_pd *clm_pd;
	struct list_head *list;
	int i;

	if (notify_entries == 0) {
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
		if (clm_pd->trackFlags & SA_TRACK_CHANGES) {
			/*
			 * Copy all cluster nodes
			 */
			for (i = 0; i < clusterNodeEntries; i++) {
				memcpy (&res_lib_clm_clustertrack.notification[i].clusterNode,
					&clusterNodes[i], sizeof (SaClmClusterNodeT));
				res_lib_clm_clustertrack.notification[i].clusterChange = SA_CLM_NODE_NO_CHANGE;
				res_lib_clm_clustertrack.notification[i].clusterNode.member = 1;
			}
			/*
			 * Copy change_only notificaiton
			 */
			res_lib_clm_clustertrack.numberOfItems = notify_entries + i;
			memcpy (&res_lib_clm_clustertrack.notification[i],
				cluster_notification_entries,
				sizeof (SaClmClusterNotificationT) * notify_entries);
		} else


		/*
		 * Track only changes
		 */
		if (clm_pd->trackFlags & SA_TRACK_CHANGES_ONLY) {
			res_lib_clm_clustertrack.numberOfItems = notify_entries;
			memcpy (&res_lib_clm_clustertrack.notification,
				cluster_notification_entries,
				sizeof (SaClmClusterNotificationT) * notify_entries);
		}

		/*
		 * Send notifications to all CLM listeners
		 */
		openais_conn_send_response (clm_pd->conn,
			&res_lib_clm_clustertrack,
			sizeof (struct res_lib_clm_clustertrack));
    }
}

static void notification_join (SaClmClusterNodeT *cluster_node)
{
	SaClmClusterNotificationT notification;

	/*
	 * Generate notification element
	 */
	notification.clusterChange = SA_CLM_NODE_JOINED;
	notification.clusterNode.member = 1;
	memcpy (&notification.clusterNode, cluster_node,
		sizeof (SaClmClusterNodeT)); 
	library_notification_send (&notification, 1);
}

static void libraryNotificationLeave (SaClmNodeIdT *nodes, int nodes_entries)
{
	SaClmClusterNotificationT clusterNotification[NODE_MAX];
	int i, j;
	int notifyEntries;

	/*
	 * Determine notification list
	 */
	for (notifyEntries = 0, i = 0; i < clusterNodeEntries; i++) {
		for (j = 0; j < nodes_entries; j++) {
			if (clusterNodes[i].nodeId == nodes[j]) {
				memcpy (&clusterNotification[notifyEntries].clusterNode, 
					&clusterNodes[i],
					sizeof (SaClmClusterNodeT));
				clusterNotification[notifyEntries].clusterChange = SA_CLM_NODE_LEFT;
				clusterNotification[notifyEntries].clusterNode.member = 0;
				notifyEntries += 1;
				break;
			}
		}
	}

	/*
	 * Remove entries from clusterNodes array
	 */
	for (i = 0; i < nodes_entries; i++) {
		for (j = 0; j < clusterNodeEntries;) {
			if (nodes[i] == clusterNodes[j].nodeId) {
				clusterNodeEntries -= 1;
				memmove (&clusterNodes[j], &clusterNodes[j + 1],
					(clusterNodeEntries - j) * sizeof (SaClmClusterNodeT));
			} else {
				/*
				 * next clusterNode entry
				 */
				j++;
			}
		}
	}

	library_notification_send (clusterNotification, notifyEntries);
}

static int clm_nodejoin_send (void)
{
	struct req_exec_clm_nodejoin req_exec_clm_nodejoin;
	struct iovec req_exec_clm_iovec;
	int result;

	req_exec_clm_nodejoin.header.size = sizeof (struct req_exec_clm_nodejoin);
	req_exec_clm_nodejoin.header.id = 
		SERVICE_ID_MAKE (CLM_SERVICE, MESSAGE_REQ_EXEC_CLM_NODEJOIN);
// TODO dont use memcpy, use iovecs !!

	thisClusterNode.initialViewNumber = view_initial;

	memcpy (&req_exec_clm_nodejoin.clusterNode, &thisClusterNode,
		sizeof (SaClmClusterNodeT));
	
	req_exec_clm_iovec.iov_base = (char *)&req_exec_clm_nodejoin;
	req_exec_clm_iovec.iov_len = sizeof (req_exec_clm_nodejoin);

	result = totempg_groups_mcast_joined (openais_group_handle, &req_exec_clm_iovec, 1, TOTEMPG_AGREED);

	return (result);
}

static void clm_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{

	int i;
	SaClmNodeIdT nodes[NODE_MAX];

	view_current = ring_id->seq / 4;
	if (view_initial == 0) {
		view_initial = ring_id->seq / 4;
	}

	log_printf (LOG_LEVEL_NOTICE, "CLM CONFIGURATION CHANGE\n");
	log_printf (LOG_LEVEL_NOTICE, "New Configuration:\n");
	for (i = 0; i < member_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", totemip_print (&member_list[i]));
	}
	log_printf (LOG_LEVEL_NOTICE, "Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", totemip_print (&left_list[i]));
	}

	log_printf (LOG_LEVEL_NOTICE, "Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", totemip_print (&joined_list[i]));
	}

	for (i = 0; i < left_list_entries; i++) {
		nodes[i] = left_list[i].nodeid;
	}

	libraryNotificationLeave (nodes, i);

	/*
	 * Load the thisClusterNode data structure in case we are
	 * transitioning to network interface up or down
	 */
	sprintf ((char *)thisClusterNode.nodeAddress.value, "%s", totemip_print (this_ip));
	thisClusterNode.nodeAddress.length = strlen ((char *)thisClusterNode.nodeAddress.value);
	if (this_ip->family == AF_INET) {
	thisClusterNode.nodeAddress.family = SA_CLM_AF_INET;
	} else
	if (this_ip->family == AF_INET6) {
	thisClusterNode.nodeAddress.family = SA_CLM_AF_INET6;
	} else {
		assert (0);
	}
	strcpy ((char *)thisClusterNode.nodeName.value,
		(char *)thisClusterNode.nodeAddress.value);
	thisClusterNode.nodeName.length = thisClusterNode.nodeAddress.length;
	thisClusterNode.nodeId = this_ip->nodeid;
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
}

static void message_handler_req_exec_clm_nodejoin (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_clm_nodejoin *req_exec_clm_nodejoin = (struct req_exec_clm_nodejoin *)message;
	int found = 0;
	int i;

	log_printf (LOG_LEVEL_NOTICE, "got nodejoin message %s\n", req_exec_clm_nodejoin->clusterNode.nodeName.value);
	
	/*
	 * Determine if nodejoin already received
	 */
	for (found = 0, i = 0; i < clusterNodeEntries; i++) {
		if (clusterNodes[i].nodeId == req_exec_clm_nodejoin->clusterNode.nodeId) {
			found = 1;
		}
	}

	/*
	 * If not received, add to internal list
	 */
	if (found == 0) {
		notification_join (&req_exec_clm_nodejoin->clusterNode);
		memcpy (&clusterNodes[clusterNodeEntries],
			&req_exec_clm_nodejoin->clusterNode,
			sizeof (SaClmClusterNodeT));

		clusterNodeEntries += 1;
	}
}

static int clm_lib_init_fn (void *conn)
{
	log_printf (LOG_LEVEL_DEBUG, "Got request to initalize cluster membership service.\n");
	struct clm_pd *clm_pd = (struct clm_pd *)openais_conn_private_data_get (conn);

	list_init (&clm_pd->list);

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
	res_lib_clm_clustertrack.numberOfItems = 0;

	if (req_lib_clm_clustertrack->trackFlags & SA_TRACK_CURRENT) {
		for (i = 0; i < clusterNodeEntries; i++) {
			res_lib_clm_clustertrack.notification[i].clusterChange = SA_CLM_NODE_NO_CHANGE;

			memcpy (&res_lib_clm_clustertrack.notification[i].clusterNode,
				&clusterNodes[i], sizeof (SaClmClusterNodeT));
		}
		res_lib_clm_clustertrack.numberOfItems = clusterNodeEntries;
	}
	
	/*
	 * Record requests for cluster tracking
	 */
	if (req_lib_clm_clustertrack->trackFlags & SA_TRACK_CHANGES ||
		req_lib_clm_clustertrack->trackFlags & SA_TRACK_CHANGES_ONLY) {

		clm_pd->trackFlags = req_lib_clm_clustertrack->trackFlags;
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

	openais_conn_send_response (conn, &res_lib_clm_trackstop,
		sizeof (struct res_lib_clm_trackstop));
}

static void message_handler_req_lib_clm_nodeget (void *conn, void *msg)
{
	struct req_lib_clm_nodeget *req_lib_clm_nodeget = (struct req_lib_clm_nodeget *)msg;
	struct res_clm_nodeget res_clm_nodeget;
	SaClmClusterNodeT *clusterNode = 0;
	int valid = 0;
	int i;

	log_printf (LOG_LEVEL_NOTICE, "nodeget: trying to find node %x\n", (int)req_lib_clm_nodeget->nodeId);

	if (req_lib_clm_nodeget->nodeId == SA_CLM_LOCAL_NODE_ID) {
		clusterNode = &clusterNodes[0];
		valid = 1;
	} else 
	for (i = 0; i < clusterNodeEntries; i++) {
		if (clusterNodes[i].nodeId == req_lib_clm_nodeget->nodeId) {
			log_printf (LOG_LEVEL_DEBUG, "found host that matches one desired in nodeget.\n");
			clusterNode = &clusterNodes[i];
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
		memcpy (&res_clm_nodeget.clusterNode, clusterNode, sizeof (SaClmClusterNodeT));
	}
	openais_conn_send_response (conn, &res_clm_nodeget, sizeof (struct res_clm_nodeget));
}

static void message_handler_req_lib_clm_nodegetasync (void *conn, void *msg)
{
	struct req_lib_clm_nodegetasync *req_lib_clm_nodegetasync = (struct req_lib_clm_nodegetasync *)msg;
	struct res_clm_nodegetasync res_clm_nodegetasync;
	struct res_clm_nodegetcallback res_clm_nodegetcallback;
	SaClmClusterNodeT *clusterNode = 0;
	int error = SA_AIS_ERR_INVALID_PARAM;
	int i;

	log_printf (LOG_LEVEL_DEBUG, "nodeget: trying to find node %x\n", (int)req_lib_clm_nodegetasync->nodeId);

	if (req_lib_clm_nodegetasync->nodeId == SA_CLM_LOCAL_NODE_ID) {
		clusterNode = &clusterNodes[0];
		error = SA_AIS_OK;
	} else 
	for (i = 0; i < clusterNodeEntries; i++) {
		if (clusterNodes[i].nodeId == req_lib_clm_nodegetasync->nodeId) {
			log_printf (LOG_LEVEL_DEBUG, "found host that matches one desired in nodeget.\n");
			clusterNode = &clusterNodes[i];
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
		memcpy (&res_clm_nodegetcallback.clusterNode, clusterNode,
			sizeof (SaClmClusterNodeT));
	}
	openais_conn_send_response (openais_conn_partner_get (conn),
		&res_clm_nodegetcallback,
		sizeof (struct res_clm_nodegetcallback));
}
