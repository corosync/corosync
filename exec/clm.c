/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <time.h>


#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/ais_types.h"
#include "../include/ais_msg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "poll.h"
#include "gmi.h"
#include "parse.h"
#include "main.h"
#include "print.h"
#include "mempool.h"
#include "handlers.h"

SaClmClusterChangesT thisClusterNodeLastChange = SA_CLM_NODE_JOINED;
SaClmClusterNodeT thisClusterNode;

#define NODE_MAX 16

SaClmClusterNodeT clusterNodes[NODE_MAX];

int clusterNodeEntries = 0;

/*
 * Service Interfaces required by service_message_handler struct
 */
static int clmExecutiveInitialize (void);

static int clmConfChg (
    struct sockaddr_in *member_list, int member_list_entries,
    struct sockaddr_in *left_list, int left_list_entries,
    struct sockaddr_in *joined_list, int joined_list_entries);

static int message_handler_req_exec_clm_nodejoin (int fd, void *message);

static int message_handler_req_clm_init (int fd, void *message);

static int message_handler_req_clm_trackstart (int fd, void *message);

static int message_handler_req_clm_trackstop (int fd, void *message);

static int message_handler_req_clm_nodeget (int fd, void *message);

static int (*clm_libais_handler_fns[]) (int fd, void *) = {
	message_handler_req_clm_trackstart,
	message_handler_req_clm_trackstop,
	message_handler_req_clm_nodeget
};

static int (*clm_aisexec_handler_fns[]) (int fd, void *) = {
	message_handler_req_exec_clm_nodejoin
};
	
struct service_handler clm_service_handler = {
	libais_handler_fns:			clm_libais_handler_fns,
	libais_handler_fns_count:	sizeof (clm_libais_handler_fns) / sizeof (int (*)),
	aisexec_handler_fns:		clm_aisexec_handler_fns ,
	aisexec_handler_fns_count:	sizeof (clm_aisexec_handler_fns) / sizeof (int (*)),
	confchg_fn:					clmConfChg,
	libais_init_fn:				message_handler_req_clm_init,
	libais_exit_fn:				0,
	aisexec_init_fn:			clmExecutiveInitialize
};

static int clmExecutiveInitialize (void)
{
	
	memset (clusterNodes, 0, sizeof (SaClmClusterNodeT) * NODE_MAX);

	/*
	 * Build local cluster node data structure
	 */
	thisClusterNode.nodeId = this_ip.sin_addr.s_addr;
	memcpy (&thisClusterNode.nodeAddress.value, &this_ip.sin_addr, sizeof (struct in_addr));
	thisClusterNode.nodeAddress.length = sizeof (struct in_addr);
	strcpy (thisClusterNode.nodeName.value, (char *)inet_ntoa (this_ip.sin_addr));
	thisClusterNode.nodeName.length = strlen (thisClusterNode.nodeName.value);
	strcpy (thisClusterNode.clusterName.value, "mvlcge");
	thisClusterNode.clusterName.length = strlen ("mvlcge");
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

static void libraryNotificationCurrentState (int fd)
{
	struct res_clm_trackcallback res_clm_trackcallback;
	SaClmClusterNotificationT clusterNotification[NODE_MAX];
	int i;

	if ((connections[fd].ais_ci.u.libclm_ci.trackFlags & SA_TRACK_CURRENT) == 0) {
		return;
	}
	/*
	 * Turn off track current
	 */
	connections[fd].ais_ci.u.libclm_ci.trackFlags &= ~SA_TRACK_CURRENT;

	/*
	 * Build notification list
	 */
	for (i = 0; i < clusterNodeEntries; i++) {
		clusterNotification[i].clusterChanges = SA_CLM_NODE_NO_CHANGE;

		memcpy (&clusterNotification[i].clusterNode, &clusterNodes[i],
			sizeof (SaClmClusterNodeT));
	}

	/*
	 * Send track response
	 */
	res_clm_trackcallback.header.magic = MESSAGE_MAGIC;
	res_clm_trackcallback.header.size = sizeof (struct res_clm_trackcallback) +
		sizeof (SaClmClusterNotificationT) * i;
	res_clm_trackcallback.header.id = MESSAGE_RES_CLM_TRACKCALLBACK;
	res_clm_trackcallback.viewNumber = 0;
	res_clm_trackcallback.numberOfItems = i;
	res_clm_trackcallback.numberOfMembers = i;
	res_clm_trackcallback.notificationBufferAddress = 
		connections[fd].ais_ci.u.libclm_ci.notificationBufferAddress;
	libais_send_response (fd, &res_clm_trackcallback, sizeof (struct res_clm_trackcallback));
	libais_send_response (fd, clusterNotification, sizeof (SaClmClusterNotificationT) * i);
}

static void libraryNotificationJoin (SaClmNodeIdT node)
{
	struct res_clm_trackcallback res_clm_trackcallback;
	SaClmClusterNotificationT clusterNotification;
	int fd;
	int i;

	/*
	 * Generate notification element
	 */
	clusterNotification.clusterChanges = SA_CLM_NODE_JOINED;
	for (i = 0; i < clusterNodeEntries; i++) {
		if (node == clusterNodes[i].nodeId) {
			memcpy (&clusterNotification.clusterNode, &clusterNodes[i],
				sizeof (SaClmClusterNodeT));
		}
	}

	/*
	 * Send notifications to all listeners
	 */
	for (fd = 0; fd < connection_entries; fd++) {
		if (connections[fd].service == SOCKET_SERVICE_CLM &&
			connections[fd].active &&
			connections[fd].ais_ci.u.libclm_ci.trackFlags) {

			res_clm_trackcallback.header.magic = MESSAGE_MAGIC;
			res_clm_trackcallback.header.size = sizeof (struct res_clm_trackcallback) +
				sizeof (SaClmClusterNotificationT);
			res_clm_trackcallback.header.id = MESSAGE_RES_CLM_TRACKCALLBACK;
			res_clm_trackcallback.viewNumber = 0;
			res_clm_trackcallback.numberOfItems = 1;
			res_clm_trackcallback.numberOfMembers = 1;
			res_clm_trackcallback.notificationBufferAddress = 
				connections[fd].ais_ci.u.libclm_ci.notificationBufferAddress;
			libais_send_response (fd, &res_clm_trackcallback, sizeof (struct res_clm_trackcallback));
			libais_send_response (fd, &clusterNotification, sizeof (SaClmClusterNotificationT));
		}
	}
}

static void libraryNotificationLeave (SaClmNodeIdT *nodes, int nodes_entries)
{
	struct res_clm_trackcallback res_clm_trackcallback;
	SaClmClusterNotificationT clusterNotification[NODE_MAX];
	int fd;
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
				clusterNotification[notifyEntries].clusterChanges = SA_CLM_NODE_LEFT;
				notifyEntries += 1;
				break;
			}
		}
	}

	/*
	 * Send notifications to all listeners
	 */
	for (fd = 0; fd < connection_entries; fd++) {
		if (connections[fd].service == SOCKET_SERVICE_CLM &&
			connections[fd].active &&
			connections[fd].ais_ci.u.libclm_ci.trackFlags) {

			if (notifyEntries) {
				res_clm_trackcallback.header.magic = MESSAGE_MAGIC;
				res_clm_trackcallback.header.size = sizeof (struct res_clm_trackcallback) +
					(notifyEntries * sizeof (SaClmClusterNotificationT));
				res_clm_trackcallback.header.id = MESSAGE_RES_CLM_TRACKCALLBACK;
				res_clm_trackcallback.viewNumber = 0;
				res_clm_trackcallback.numberOfItems = notifyEntries;
				res_clm_trackcallback.numberOfMembers = notifyEntries;
				res_clm_trackcallback.notificationBufferAddress = 
					connections[fd].ais_ci.u.libclm_ci.notificationBufferAddress;
				libais_send_response (fd, &res_clm_trackcallback, sizeof (struct res_clm_trackcallback));
				libais_send_response (fd, clusterNotification, sizeof (SaClmClusterNotificationT) * notifyEntries);
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
				memcpy (&clusterNodes[j], &clusterNodes[j + 1],
					(clusterNodeEntries - i) * sizeof (SaClmClusterNodeT));
			} else {
				/*
				 * next clusterNode entry
				 */
				j++;
			}
		}
	}
}

static int clmNodeJoinSend (void)
{
	struct req_exec_clm_nodejoin req_exec_clm_nodejoin;
	struct iovec req_exec_clm_iovec;
	int result;
	req_exec_clm_nodejoin.header.magic = MESSAGE_MAGIC;
	req_exec_clm_nodejoin.header.size = sizeof (struct req_exec_clm_nodejoin);
	req_exec_clm_nodejoin.header.id = MESSAGE_REQ_EXEC_CLM_NODEJOIN;
// TODO dont use memcpy, use iovecs !!
	memcpy (&req_exec_clm_nodejoin.clusterNode, &thisClusterNode,
		sizeof (SaClmClusterNodeT));
	
	req_exec_clm_iovec.iov_base = &req_exec_clm_nodejoin;
	req_exec_clm_iovec.iov_len = sizeof (req_exec_clm_nodejoin);

	result = gmi_mcast (&aisexec_groupname, &req_exec_clm_iovec, 1, GMI_PRIO_HIGH);

	return (result);
}

static int clmConfChg (
    struct sockaddr_in *member_list, int member_list_entries,
    struct sockaddr_in *left_list, int left_list_entries,
    struct sockaddr_in *joined_list, int joined_list_entries) {

	int i;
	SaClmNodeIdT nodes[NODE_MAX];

	log_printf (LOG_LEVEL_NOTICE, "CLM CONFIGURATION CHANGE\n");
	log_printf (LOG_LEVEL_NOTICE, "New Configuration:\n");
	for (i = 0; i < member_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", inet_ntoa (member_list[i].sin_addr));
	}
	log_printf (LOG_LEVEL_NOTICE, "Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", inet_ntoa (left_list[i].sin_addr));
	}

	log_printf (LOG_LEVEL_NOTICE, "Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
		log_printf (LOG_LEVEL_NOTICE, "\t%s\n", inet_ntoa (joined_list[i].sin_addr));
	}

	/*
	 * Send node information to other nodes
	 */
	if (joined_list_entries) {
		clmNodeJoinSend ();
	}
	for (i = 0; i < left_list_entries; i++) {
		nodes[i] = left_list[i].sin_addr.s_addr;
	}

	libraryNotificationLeave (nodes, i);

	return (0);
}

static int message_handler_req_exec_clm_nodejoin (int fd, void *message)
{
	struct req_exec_clm_nodejoin *req_exec_clm_nodejoin = (struct req_exec_clm_nodejoin *)message;
	int found;
	int i;

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

static int message_handler_req_clm_init (int fd, void *message)
{
	SaErrorT error = SA_ERR_SECURITY;
	struct res_lib_init res_lib_init;

	log_printf (LOG_LEVEL_DEBUG, "Got request to initalize cluster membership service.\n");
	if (connections[fd].authenticated) {
		connections[fd].service = SOCKET_SERVICE_CLM;
		error = SA_OK;
	}

	res_lib_init.header.magic = MESSAGE_MAGIC;
	res_lib_init.header.size = sizeof (struct res_lib_init);
	res_lib_init.header.id = MESSAGE_RES_INIT;
	res_lib_init.error = error;

	libais_send_response (fd, &res_lib_init, sizeof (res_lib_init));

	if (connections[fd].authenticated) {
		return (0);
	}
	return (-1);
}

int message_handler_req_clm_trackstart (int fd, void *message)
{
	struct req_clm_trackstart *req_clm_trackstart = (struct req_clm_trackstart *)message;


	connections[fd].ais_ci.u.libclm_ci.trackFlags = req_clm_trackstart->trackFlags;
	connections[fd].ais_ci.u.libclm_ci.notificationBufferAddress = req_clm_trackstart->notificationBufferAddress;

	libraryNotificationCurrentState (fd);

	return (0);
}

static int message_handler_req_clm_trackstop (int fd, void *message)
{
	connections[fd].ais_ci.u.libclm_ci.trackFlags = 0;
	connections[fd].ais_ci.u.libclm_ci.notificationBufferAddress = 0;

	return (0);
}

static int message_handler_req_clm_nodeget (int fd, void *message)
{
	struct req_clm_nodeget *req_clm_nodeget = (struct req_clm_nodeget *)message;
	struct res_clm_nodeget res_clm_nodeget;
	SaClmClusterNodeT *clusterNode = 0;
	int valid = 0;
	int i;

	log_printf (LOG_LEVEL_DEBUG, "nodeget: trying to find node %x\n", (int)req_clm_nodeget->nodeId);

	if (req_clm_nodeget->nodeId == SA_CLM_LOCAL_NODE_ID) {
		clusterNode = &clusterNodes[0];
		valid = 1;
	} else 
	for (i = 0; i < clusterNodeEntries; i++) {
		if (clusterNodes[i].nodeId == req_clm_nodeget->nodeId) {
			log_printf (LOG_LEVEL_DEBUG, "found host that matches one desired in nodeget.\n");
			clusterNode = &clusterNodes[i];
			valid = 1;
			break;
		}
	}

	res_clm_nodeget.header.magic = MESSAGE_MAGIC;
	res_clm_nodeget.header.size = sizeof (struct res_clm_nodeget);
	res_clm_nodeget.header.id = MESSAGE_RES_CLM_NODEGET;
	res_clm_nodeget.invocation = req_clm_nodeget->invocation;
	res_clm_nodeget.clusterNodeAddress = req_clm_nodeget->clusterNodeAddress;
	res_clm_nodeget.valid = valid;
	if (valid) {
		memcpy (&res_clm_nodeget.clusterNode, clusterNode, sizeof (SaClmClusterNodeT));
	}
	libais_send_response (fd, &res_clm_nodeget, sizeof (struct res_clm_nodeget));

	return (0);
}
