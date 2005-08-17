/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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
#include <sys/sysinfo.h>
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
#include "aispoll.h"
#include "totempg.h"
#include "main.h"
#include "mempool.h"
#include "handlers.h"

#define LOG_SERVICE LOG_SERVICE_CLM
#include "print.h"

SaClmClusterChangesT thisClusterNodeLastChange = SA_CLM_NODE_JOINED;
SaClmClusterNodeT thisClusterNode;

#define NODE_MAX 16

SaClmClusterNodeT clusterNodes[NODE_MAX];

int clusterNodeEntries = 0;

static DECLARE_LIST_INIT (library_notification_send_listhead);

SaClmClusterNodeT *clm_get_by_nodeid (struct in_addr node_id)
{
	SaClmClusterNodeT *ret = NULL;
	int i;

	if (node_id.s_addr == SA_CLM_LOCAL_NODE_ID) {
		return (&clusterNodes[0]);
	}
	for (i = 0; i < clusterNodeEntries; i++) {
		if (clusterNodes[i].nodeId == node_id.s_addr) {
			ret = &clusterNodes[i];
			break;
		}
	}
	return (ret);
}

/*
 * Service Interfaces required by service_message_handler struct
 */
static int clm_confchg_fn (
	enum totem_configuration_type configuration_type,
    struct in_addr *member_list, int member_list_entries,
    struct in_addr *left_list, int left_list_entries,
    struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static void clm_sync_init (void);

static int clm_sync_process (void);

static void clm_sync_activate (void);

static void clm_sync_abort (void);

static int clm_exec_init_fn (struct openais_config *);

static int clm_init_two_fn (struct conn_info *conn_info);

static int clm_exit_fn (struct conn_info *conn_info);

static int message_handler_req_exec_clm_nodejoin (void *message, struct in_addr source_addr,
	int endian_conversion_required);

static int message_handler_req_lib_clm_clustertrack (struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_clm_trackstop (struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_clm_nodeget (struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_clm_nodegetasync (struct conn_info *conn_info,
	void *message);

static int clm_exit_fn (struct conn_info *conn_info);

/*
 * Executive Handler Definition
 */
struct libais_handler clm_libais_handlers[] =
{
	{ /* 0 */
		.libais_handler_fn			= message_handler_req_lib_clm_clustertrack,
		.response_size				= sizeof (struct res_lib_clm_clustertrack),
		.response_id				= MESSAGE_RES_CLM_TRACKSTART, // TODO RESPONSE
		.flow_control				= FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.libais_handler_fn			= message_handler_req_lib_clm_trackstop,
		.response_size				= sizeof (struct res_lib_clm_trackstop),
		.response_id				= MESSAGE_RES_CLM_TRACKSTOP, // TODO RESPONSE
		.flow_control				= FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.libais_handler_fn			= message_handler_req_lib_clm_nodeget,
		.response_size				= sizeof (struct res_clm_nodeget),
		.response_id				= MESSAGE_RES_CLM_NODEGET, // TODO RESPONSE
		.flow_control				= FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.libais_handler_fn			= message_handler_req_lib_clm_nodegetasync,
		.response_size				= sizeof (struct res_clm_nodegetasync),
		.response_id				= MESSAGE_RES_CLM_NODEGETCALLBACK, // TODO RESPONSE
		.flow_control				= FLOW_CONTROL_NOT_REQUIRED
	}
};

static int (*clm_aisexec_handler_fns[]) (void *, struct in_addr source_addr, int endian_conversion_required) = {
	message_handler_req_exec_clm_nodejoin
};
	
struct service_handler clm_service_handler = {
	.libais_handlers			= clm_libais_handlers,
	.libais_handlers_count		= sizeof (clm_libais_handlers) / sizeof (struct libais_handler),
	.aisexec_handler_fns		= clm_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (clm_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn					= clm_confchg_fn,
	.libais_init_two_fn			= clm_init_two_fn,
	.libais_exit_fn				= clm_exit_fn,
	.exec_init_fn				= clm_exec_init_fn,
	.exec_dump_fn				= 0,
	.sync_init					= clm_sync_init,
	.sync_process				= clm_sync_process,
	.sync_activate				= clm_sync_activate,
	.sync_abort					= clm_sync_abort,
};

struct req_exec_clm_nodejoin {
	struct req_header header;
	SaClmClusterNodeT clusterNode;
};

static int clm_exec_init_fn (struct openais_config *openais_config)
{
	memset (clusterNodes, 0, sizeof (SaClmClusterNodeT) * NODE_MAX);

	/*
	 * Build local cluster node data structure
	 */
	thisClusterNode.nodeId = this_ip->sin_addr.s_addr;
	memcpy (&thisClusterNode.nodeAddress.value, &this_ip->sin_addr,
		sizeof (struct in_addr));
	thisClusterNode.nodeAddress.length = sizeof (struct in_addr);
	strcpy ((char *)thisClusterNode.nodeName.value, (char *)inet_ntoa (this_ip->sin_addr));
	thisClusterNode.nodeName.length = strlen ((char *)thisClusterNode.nodeName.value);
	thisClusterNode.member = 1;
	{
		struct sysinfo s_info;
		time_t current_time;
		sysinfo (&s_info);
		current_time = time (NULL);
		 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
		thisClusterNode.bootTimestamp = ((SaTimeT)(current_time - s_info.uptime)) * 1000000000;
	}

#ifdef DEBUG
    printSaClmClusterNodeT ("this cluster node", &thisClusterNode);
#endif

	memcpy (&clusterNodes[0], &thisClusterNode, sizeof (SaClmClusterNodeT));
	clusterNodeEntries = 1;

	return (0);
}

static int clm_exit_fn (struct conn_info *conn_info)
{
	/*
	 * Delete track entry if there is one
	 */
	list_del (&conn_info->conn_list);

	return (0);
}

void library_notification_send (SaClmClusterNotificationT *cluster_notification_entries,
	int notify_entries)
{
	struct res_lib_clm_clustertrack res_lib_clm_clustertrack;
	struct conn_info *conn_info;
	struct list_head *list;

    for (list = library_notification_send_listhead.next;
        list != &library_notification_send_listhead;
        list = list->next) {

        conn_info = list_entry (list, struct conn_info, conn_list);

		/*
		 * Send notifications to all CLM listeners
		 */
		if (notify_entries) {
			res_lib_clm_clustertrack.header.size = sizeof (struct res_lib_clm_clustertrack) +
				(notify_entries * sizeof (SaClmClusterNotificationT));
			res_lib_clm_clustertrack.header.id = MESSAGE_RES_CLM_TRACKCALLBACK;
			res_lib_clm_clustertrack.header.error = SA_OK;
			res_lib_clm_clustertrack.viewNumber = 0;
			res_lib_clm_clustertrack.numberOfItems = notify_entries;
			libais_send_response (conn_info, &res_lib_clm_clustertrack,
				sizeof (struct res_lib_clm_clustertrack));
			libais_send_response (conn_info, cluster_notification_entries,
				sizeof (SaClmClusterNotificationT) * notify_entries);
        }
    }
}

static void libraryNotificationJoin (SaClmNodeIdT node)
{
	SaClmClusterNotificationT clusterNotification;
	int i;

	/*
	 * Generate notification element
	 */
	clusterNotification.clusterChange = SA_CLM_NODE_JOINED;
	clusterNotification.clusterNode.member = 1;
	for (i = 0; i < clusterNodeEntries; i++) {
		if (node == clusterNodes[i].nodeId) {
			memcpy (&clusterNotification.clusterNode, &clusterNodes[i],
				sizeof (SaClmClusterNodeT));
		}
	}

	library_notification_send (&clusterNotification, 1);
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

	library_notification_send (clusterNotification, notifyEntries);

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
}

static int clm_nodejoin_send (void)
{
	struct req_exec_clm_nodejoin req_exec_clm_nodejoin;
	struct iovec req_exec_clm_iovec;
	int result;

	req_exec_clm_nodejoin.header.size = sizeof (struct req_exec_clm_nodejoin);
	req_exec_clm_nodejoin.header.id = MESSAGE_REQ_EXEC_CLM_NODEJOIN;
// TODO dont use memcpy, use iovecs !!
	memcpy (&req_exec_clm_nodejoin.clusterNode, &thisClusterNode,
		sizeof (SaClmClusterNodeT));
	
	req_exec_clm_iovec.iov_base = (char *)&req_exec_clm_nodejoin;
	req_exec_clm_iovec.iov_len = sizeof (req_exec_clm_nodejoin);

	result = totempg_mcast (&req_exec_clm_iovec, 1, TOTEMPG_AGREED);

	return (result);
}

static int clm_confchg_fn (
	enum totem_configuration_type configuration_type,
    struct in_addr *member_list, int member_list_entries,
    struct in_addr *left_list, int left_list_entries,
    struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{

	int i;
	SaClmNodeIdT nodes[NODE_MAX];

	log_printf (LOG_LEVEL_NOTICE, "CLM CONFIGURATION CHANGE\n");
	log_printf (LOG_LEVEL_NOTICE, "New Configuration:\n");
	for (i = 0; i < member_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", inet_ntoa (member_list[i]));
	}
	log_printf (LOG_LEVEL_NOTICE, "Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", inet_ntoa (left_list[i]));
	}

	log_printf (LOG_LEVEL_NOTICE, "Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", inet_ntoa (joined_list[i]));
	}

	for (i = 0; i < left_list_entries; i++) {
		nodes[i] = left_list[i].s_addr;
	}

	libraryNotificationLeave (nodes, i);
	/*
	 * Load the thisClusterNode data structure in case we are
	 * transitioning to network interface up or down
	 */
	thisClusterNode.nodeId = this_ip->sin_addr.s_addr;
	memcpy (&thisClusterNode.nodeAddress.value, &this_ip->sin_addr,
		sizeof (struct in_addr));
	thisClusterNode.nodeAddress.length = sizeof (struct in_addr);
	strcpy ((char *)thisClusterNode.nodeName.value, (char *)inet_ntoa (this_ip->sin_addr));
	thisClusterNode.nodeName.length = strlen ((char *)thisClusterNode.nodeName.value);

	return (0);
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


static void exec_clm_nodejoin_endian_conversion (struct req_exec_clm_nodejoin *in,
	struct req_exec_clm_nodejoin *out)
{
	// TODO this isn't complete yet
	// TODO SaNameT needs to be packed and aligned
	memmove (&out->clusterNode.nodeName.value[0],
		&in->clusterNode.nodeName.value[2],
		SA_MAX_NAME_LENGTH);
}

static int message_handler_req_exec_clm_nodejoin (void *message, struct in_addr source_addr,
	int endian_conversion_required)
{
	struct req_exec_clm_nodejoin *req_exec_clm_nodejoin = (struct req_exec_clm_nodejoin *)message;
	struct req_exec_clm_nodejoin req_exec_clm_nodejoin_storage;
	int found;
	int i;

	if (endian_conversion_required) {
		exec_clm_nodejoin_endian_conversion (message, &req_exec_clm_nodejoin_storage);
		req_exec_clm_nodejoin = &req_exec_clm_nodejoin_storage;
	} else {
		req_exec_clm_nodejoin = (struct req_exec_clm_nodejoin *)message;
	}

		
	log_printf (LOG_LEVEL_NOTICE, "got nodejoin message %s\n", req_exec_clm_nodejoin->clusterNode.nodeName.value);
	
	/*
	 * Determine if nodejoin already received
	 */
	for (found = 0, i = 0; i < clusterNodeEntries; i++) {
		if (memcmp (&clusterNodes[i], &req_exec_clm_nodejoin->clusterNode, 
			sizeof (SaClmClusterNodeT)) == 0) {

			found = 1;
		}
	}

	/*
	 * If not received, add to internal list
	 */
	if (found == 0) {
		memcpy (&clusterNodes[clusterNodeEntries],
			&req_exec_clm_nodejoin->clusterNode,
			sizeof (SaClmClusterNodeT));

		clusterNodeEntries += 1;
		libraryNotificationJoin (req_exec_clm_nodejoin->clusterNode.nodeId);
	}

	return (0);
}

static int clm_init_two_fn (struct conn_info *conn_info)
{
	log_printf (LOG_LEVEL_DEBUG, "Got request to initalize cluster membership service.\n");

	list_init (&conn_info->conn_list);

	return (0);
}

int message_handler_req_lib_clm_clustertrack (struct conn_info *conn_info, void *message)
{
	struct req_lib_clm_clustertrack *req_lib_clm_clustertrack = (struct req_lib_clm_clustertrack *)message;
	struct res_lib_clm_clustertrack res_lib_clm_clustertrack;
	int i;

	conn_info->conn_info_partner->ais_ci.u.libclm_ci.trackFlags = req_lib_clm_clustertrack->trackFlags;

	list_add (&conn_info->conn_info_partner->conn_list, &library_notification_send_listhead);

	res_lib_clm_clustertrack.header.size = sizeof (struct res_lib_clm_clustertrack);
	res_lib_clm_clustertrack.header.id = MESSAGE_RES_CLM_TRACKSTART;
	res_lib_clm_clustertrack.header.error = SA_AIS_OK;
	res_lib_clm_clustertrack.viewNumber = 0;
		res_lib_clm_clustertrack.numberOfItems = 0;

	if (req_lib_clm_clustertrack->trackFlags & SA_TRACK_CURRENT) {
		for (i = 0; i < clusterNodeEntries; i++) {
			res_lib_clm_clustertrack.notification[i].clusterChange = SA_CLM_NODE_NO_CHANGE;

			memcpy (&res_lib_clm_clustertrack.notification[i].clusterNode,
				&clusterNodes[i], sizeof (SaClmClusterNodeT));
		}
		res_lib_clm_clustertrack.numberOfItems = clusterNodeEntries;
	} else {
		conn_info->conn_info_partner->ais_ci.u.libclm_ci.tracking_enabled = 1;
	}

	libais_send_response (conn_info, &res_lib_clm_clustertrack,
		sizeof (struct res_lib_clm_clustertrack));

	if (req_lib_clm_clustertrack->return_in_callback) {
		res_lib_clm_clustertrack.header.id = MESSAGE_RES_CLM_TRACKCALLBACK;

		libais_send_response (conn_info->conn_info_partner,
			&res_lib_clm_clustertrack,
			sizeof (struct res_lib_clm_clustertrack));
	}
	return (0);
}


static int message_handler_req_lib_clm_trackstop (struct conn_info *conn_info, void *message)
{
	struct res_lib_clm_trackstop res_lib_clm_trackstop;

	res_lib_clm_trackstop.header.size = sizeof (struct res_lib_clm_trackstop);
	res_lib_clm_trackstop.header.id = MESSAGE_RES_CLM_TRACKSTOP;

	if (conn_info->conn_info_partner->ais_ci.u.libclm_ci.tracking_enabled) {
		res_lib_clm_trackstop.header.error = SA_OK;
	} else {
		res_lib_clm_trackstop.header.error = SA_AIS_ERR_NOT_EXIST;
	}

	conn_info->conn_info_partner->ais_ci.u.libclm_ci.trackFlags = 0;
	conn_info->conn_info_partner->ais_ci.u.libclm_ci.tracking_enabled = 0;

	list_del (&conn_info->conn_info_partner->conn_list);

	libais_send_response (conn_info, &res_lib_clm_trackstop,
		sizeof (struct res_lib_clm_trackstop));

	return (0);
}

static int message_handler_req_lib_clm_nodeget (struct conn_info *conn_info, void *message)
{
	struct req_lib_clm_nodeget *req_lib_clm_nodeget = (struct req_lib_clm_nodeget *)message;
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
	res_clm_nodeget.header.error = SA_OK;
	res_clm_nodeget.invocation = req_lib_clm_nodeget->invocation;
	res_clm_nodeget.valid = valid;
	if (valid) {
		memcpy (&res_clm_nodeget.clusterNode, clusterNode, sizeof (SaClmClusterNodeT));
	}
	libais_send_response (conn_info, &res_clm_nodeget, sizeof (struct res_clm_nodeget));

	return (0);
}

static int message_handler_req_lib_clm_nodegetasync (struct conn_info *conn_info, void *message)
{
	struct req_lib_clm_nodegetasync *req_lib_clm_nodegetasync = (struct req_lib_clm_nodegetasync *)message;
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
	res_clm_nodegetasync.header.error = SA_OK;
	libais_send_response (conn_info, &res_clm_nodegetasync,
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
	libais_send_response (conn_info->conn_info_partner, &res_clm_nodegetcallback,
		sizeof (struct res_clm_nodegetcallback));

	return (0);
}
