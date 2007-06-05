/*
 * Copyright (c) 2006 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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
#ifndef OPENAIS_BSD
#include <alloca.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
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
#include "../include/ipc_cpg.h"
#include "../include/mar_cpg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "totempg.h"
#include "totemip.h"
#include "main.h"
#include "ipc.h"
#include "mempool.h"
#include "service.h"
#include "jhash.h"
#include "swab.h"
#include "ipc.h"
#include "flow.h"
#include "print.h"

#define GROUP_HASH_SIZE 32

#define PI_FLAG_MEMBER 1

enum cpg_message_req_types {
	MESSAGE_REQ_EXEC_CPG_PROCJOIN = 0,
	MESSAGE_REQ_EXEC_CPG_PROCLEAVE = 1,
	MESSAGE_REQ_EXEC_CPG_JOINLIST = 2,
	MESSAGE_REQ_EXEC_CPG_MCAST = 3,
	MESSAGE_REQ_EXEC_CPG_DOWNLIST = 4
};

struct removed_group
{
	struct group_info *gi;
	struct list_head list; /* on removed_list */
	int left_list_entries;
	mar_cpg_address_t left_list[PROCESSOR_COUNT_MAX];
	int left_list_size;
};

struct group_info {
	mar_cpg_name_t group_name;
	struct list_head members;
	struct list_head list;    /* on hash list */
	struct removed_group *rg; /* when a node goes down */
};

struct process_info {
	unsigned int nodeid;
	uint32_t pid;
	uint32_t flags;
	void *conn;
	void *trackerconn;
	struct group_info *group;
	enum openais_flow_control_state flow_control_state;
	struct list_head list; /* on the group_info members list */
};

struct join_list_entry {
	uint32_t pid;
	mar_cpg_name_t group_name;
};

static struct list_head group_lists[GROUP_HASH_SIZE];

/*
 * Service Interfaces required by service_message_handler struct
 */
static void cpg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static int cpg_exec_init_fn (struct objdb_iface_ver0 *objdb);

static int cpg_lib_init_fn (void *conn);

static int cpg_lib_exit_fn (void *conn);

static void message_handler_req_exec_cpg_procjoin (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_procleave (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_joinlist (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_mcast (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_downlist (
	void *message,
	unsigned int nodeid);

static void exec_cpg_procjoin_endian_convert (void *msg);

static void exec_cpg_joinlist_endian_convert (void *msg);

static void exec_cpg_mcast_endian_convert (void *msg);

static void exec_cpg_downlist_endian_convert (void *msg);

static void message_handler_req_lib_cpg_join (void *conn, void *message);

static void message_handler_req_lib_cpg_leave (void *conn, void *message);

static void message_handler_req_lib_cpg_mcast (void *conn, void *message);

static void message_handler_req_lib_cpg_membership (void *conn, void *message);

static void message_handler_req_lib_cpg_trackstart (void *conn, void *message);

static void message_handler_req_lib_cpg_trackstop (void *conn, void *message);

static int cpg_node_joinleave_send (struct group_info *gi, struct process_info *pi, int fn, int reason);

static int cpg_exec_send_joinlist(void);

static void cpg_sync_init (void);
static int  cpg_sync_process (void);
static void cpg_sync_activate (void);
static void cpg_sync_abort (void);
/*
 * Library Handler Definition
 */
static struct openais_lib_handler cpg_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_cpg_join,
		.response_size				= sizeof (struct res_lib_cpg_join),
		.response_id				= MESSAGE_RES_CPG_JOIN,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_cpg_leave,
		.response_size				= sizeof (struct res_lib_cpg_leave),
		.response_id				= MESSAGE_RES_CPG_LEAVE,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_cpg_mcast,
		.response_size				= sizeof (struct res_lib_cpg_mcast),
		.response_id				= MESSAGE_RES_CPG_MCAST,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_cpg_membership,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CPG_MEMBERSHIP,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_cpg_trackstart,
		.response_size				= sizeof (struct res_lib_cpg_trackstart),
		.response_id				= MESSAGE_RES_CPG_TRACKSTART,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn				= message_handler_req_lib_cpg_trackstop,
		.response_size				= sizeof (struct res_lib_cpg_trackstart),
		.response_id				= MESSAGE_RES_CPG_TRACKSTOP,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct openais_exec_handler cpg_exec_service[] =
{
	{ /* 0 */
		.exec_handler_fn	= message_handler_req_exec_cpg_procjoin,
		.exec_endian_convert_fn	= exec_cpg_procjoin_endian_convert
	},
	{ /* 1 */
		.exec_handler_fn	= message_handler_req_exec_cpg_procleave,
		.exec_endian_convert_fn	= exec_cpg_procjoin_endian_convert
	},
	{ /* 2 */
		.exec_handler_fn	= message_handler_req_exec_cpg_joinlist,
		.exec_endian_convert_fn	= exec_cpg_joinlist_endian_convert
	},
	{ /* 3 */
		.exec_handler_fn	= message_handler_req_exec_cpg_mcast,
		.exec_endian_convert_fn	= exec_cpg_mcast_endian_convert
	},
	{ /* 4 */
		.exec_handler_fn	= message_handler_req_exec_cpg_downlist,
		.exec_endian_convert_fn	= exec_cpg_downlist_endian_convert
	},
};

struct openais_service_handler cpg_service_handler = {
	.name				        = "openais cluster closed process group service v1.01",
	.id					= CPG_SERVICE,
	.private_data_size			= sizeof (struct process_info),
	.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED,
	.lib_init_fn				= cpg_lib_init_fn,
	.lib_exit_fn				= cpg_lib_exit_fn,
	.lib_service				= cpg_lib_service,
	.lib_service_count			= sizeof (cpg_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn				= cpg_exec_init_fn,
	.exec_dump_fn				= NULL,
	.exec_service				= cpg_exec_service,
	.exec_service_count		        = sizeof (cpg_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn                             = cpg_confchg_fn,
	.sync_init                              = cpg_sync_init,
	.sync_process                           = cpg_sync_process,
	.sync_activate                          = cpg_sync_activate,
	.sync_abort                             = cpg_sync_abort
};

/*
 * Dynamic loader definition
 */
static struct openais_service_handler *cpg_get_service_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 cpg_service_handler_iface = {
	.openais_get_service_handler_ver0		= cpg_get_service_handler_ver0
};

static struct lcr_iface openais_cpg_ver0[1] = {
	{
		.name				= "openais_cpg",
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

static struct lcr_comp cpg_comp_ver0 = {
	.iface_count			= 1,
	.ifaces			        = openais_cpg_ver0
};


static struct openais_service_handler *cpg_get_service_handler_ver0 (void)
{
	return (&cpg_service_handler);
}

__attribute__ ((constructor)) static void cpg_comp_register (void) {
        lcr_interfaces_set (&openais_cpg_ver0[0], &cpg_service_handler_iface);

	lcr_component_register (&cpg_comp_ver0);
}

struct req_exec_cpg_procjoin {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_uint32_t reason __attribute__((aligned(8)));
};

struct req_exec_cpg_mcast {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t msglen __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_uint8_t message[] __attribute__((aligned(8)));
};

struct req_exec_cpg_downlist {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t left_nodes __attribute__((aligned(8)));
	mar_uint32_t nodeids[PROCESSOR_COUNT_MAX]  __attribute__((aligned(8)));
};

static struct req_exec_cpg_downlist req_exec_cpg_downlist;

static void cpg_sync_init (void)
{
}

static int cpg_sync_process (void)
{
	return cpg_exec_send_joinlist();
}

static void cpg_sync_activate (void)
{

}
static void cpg_sync_abort (void)
{

}



static int notify_lib_joinlist(
	struct group_info *gi,
	void *conn,
	int joined_list_entries,
	mar_cpg_address_t *joined_list,
	int left_list_entries,
	mar_cpg_address_t *left_list,
	int id)
{
	int count = 0;
	char *buf;
	struct res_lib_cpg_confchg_callback *res;
	struct list_head *iter;
	struct list_head *tmp;
	mar_cpg_address_t *retgi;
	int size;

	/* First, we need to know how many nodes are in the list. While we're
	   traversing this list, look for the 'us' entry so we know which
	   connection to send back down */
	for (iter = gi->members.next; iter != &gi->members; iter = iter->next) {
		struct process_info *pi = list_entry(iter, struct process_info, list);
		if (pi->pid)
			count++;
	}

	log_printf(LOG_LEVEL_DEBUG, "Sending new joinlist (%d elements) to clients\n", count);

	size = sizeof(struct res_lib_cpg_confchg_callback) +
		sizeof(mar_cpg_address_t) * (count + left_list_entries + joined_list_entries);
	buf = alloca(size);
	if (!buf)
		return SA_AIS_ERR_NO_SPACE;

	res = (struct res_lib_cpg_confchg_callback *)buf;
	res->joined_list_entries = joined_list_entries;
	res->left_list_entries = left_list_entries;
 	retgi = res->member_list;

	res->header.size = size;
	res->header.id = id;
	memcpy(&res->group_name, &gi->group_name, sizeof(mar_cpg_name_t));

	/* Build up the message */
	count = 0;
	for (iter = gi->members.next; iter != &gi->members; iter = iter->next) {
		struct process_info *pi = list_entry(iter, struct process_info, list);
		if (pi->pid) {
			/* Processes leaving will be removed AFTER this is done (so that they get their
			   own leave notifications), so exclude them from the members list here */
			int i;
			for (i=0; i<left_list_entries; i++) {
				if (left_list[i].pid == pi->pid && left_list[i].nodeid == pi->nodeid)
					goto next_member;
			}

			retgi->nodeid = pi->nodeid;
			retgi->pid = pi->pid;
			retgi++;
			count++;
		next_member: ;
		}
	}
	res->member_list_entries = count;

	if (left_list_entries) {
		memcpy(retgi, left_list, left_list_entries * sizeof(mar_cpg_address_t));
		retgi += left_list_entries;
	}

	if (joined_list_entries) {
		memcpy(retgi, joined_list, joined_list_entries * sizeof(mar_cpg_address_t));
		retgi += joined_list_entries;
	}

	if (conn) {
		openais_conn_send_response(conn, buf, size);
	}
	else {
		/* Send it to all listeners */
		for (iter = gi->members.next, tmp=iter->next; iter != &gi->members; iter = tmp, tmp=iter->next) {
			struct process_info *pi = list_entry(iter, struct process_info, list);
			if (pi->trackerconn && (pi->flags & PI_FLAG_MEMBER)) {
				if (openais_conn_send_response(pi->trackerconn, buf, size) == -1) {
					// Error ??
				}
			}
		}
	}

	return SA_AIS_OK;
}

static void remove_group(struct group_info *gi)
{
	list_del(&gi->list);
	free(gi);
}


static int cpg_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	int i;

	log_init ("CPG");

	for (i=0; i<GROUP_HASH_SIZE; i++) {
		list_init(&group_lists[i]);
	}

	return (0);
}

static int cpg_lib_exit_fn (void *conn)
{
	struct process_info *pi = (struct process_info *)openais_conn_private_data_get (conn);
	struct group_info *gi = pi->group;
	mar_cpg_address_t notify_info;

	log_printf(LOG_LEVEL_DEBUG, "exit_fn for conn=%p\n", conn);

	if (gi) {
		notify_info.pid = pi->pid;
		notify_info.nodeid = totempg_my_nodeid_get();
		notify_info.reason = CONFCHG_CPG_REASON_PROCDOWN;
		cpg_node_joinleave_send(gi, pi, MESSAGE_REQ_EXEC_CPG_PROCLEAVE, CONFCHG_CPG_REASON_PROCDOWN);
		list_del(&pi->list);
	}
	return (0);
}

static struct group_info *get_group(mar_cpg_name_t *name)
{
	struct list_head *iter;
	struct group_info *gi = NULL;
	struct group_info *itergi;
	uint32_t hash = jhash(name->value, name->length, 0) % GROUP_HASH_SIZE;

	for (iter = group_lists[hash].next; iter != &group_lists[hash]; iter = iter->next) {
		itergi = list_entry(iter, struct group_info, list);
		if (memcmp(itergi->group_name.value, name->value, name->length) == 0) {
			gi = itergi;
			break;
		}
	}

	if (!gi) {
		gi = malloc(sizeof(struct group_info));
		if (!gi) {
			log_printf(LOG_LEVEL_WARNING, "Unable to allocate group_info struct");
			return NULL;
		}
		memcpy(&gi->group_name, name, sizeof(mar_cpg_name_t));
		gi->rg = NULL;
		list_init(&gi->members);
		list_add(&gi->list, &group_lists[hash]);
	}
	return gi;
}

static int cpg_node_joinleave_send (struct group_info *gi, struct process_info *pi, int fn, int reason)
{
	struct req_exec_cpg_procjoin req_exec_cpg_procjoin;
	struct iovec req_exec_cpg_iovec;
	int result;

	memcpy(&req_exec_cpg_procjoin.group_name, &gi->group_name, sizeof(mar_cpg_name_t));
	req_exec_cpg_procjoin.pid = pi->pid;
	req_exec_cpg_procjoin.reason = reason;

	req_exec_cpg_procjoin.header.size = sizeof(req_exec_cpg_procjoin);
	req_exec_cpg_procjoin.header.id = SERVICE_ID_MAKE(CPG_SERVICE, fn);

	req_exec_cpg_iovec.iov_base = (char *)&req_exec_cpg_procjoin;
	req_exec_cpg_iovec.iov_len = sizeof(req_exec_cpg_procjoin);

	result = totempg_groups_mcast_joined (openais_group_handle, &req_exec_cpg_iovec, 1, TOTEMPG_AGREED);

	return (result);
}

static void remove_node_from_groups(
	unsigned int nodeid,
	struct list_head *remlist)
{
	int i;
	struct list_head *iter, *iter2, *tmp;
	struct process_info *pi;
	struct group_info *gi;

	for (i=0; i < GROUP_HASH_SIZE; i++) {
		for (iter = group_lists[i].next; iter != &group_lists[i]; iter = iter->next) {
			gi = list_entry(iter, struct group_info, list);
			for (iter2 = gi->members.next, tmp = iter2->next; iter2 != &gi->members; iter2 = tmp, tmp = iter2->next) {
				pi = list_entry(iter2, struct process_info, list);

				if (pi->nodeid == nodeid) {

					/* Add it to the list of nodes to send notifications for */
					if (!gi->rg) {
						gi->rg = malloc(sizeof(struct removed_group));
						if (gi->rg) {
							list_add(&gi->rg->list, remlist);
							gi->rg->gi = gi;
							gi->rg->left_list_entries = 0;
							gi->rg->left_list_size = PROCESSOR_COUNT_MAX;
						}
						else {
							log_printf(LOG_LEVEL_CRIT, "Unable to allocate removed group struct. CPG callbacks will be junk.");
							return;
						}
					}
					/* Do we need to increase the size ?
					 * Yes, I increase this exponentially. Generally, if you've got a lot of groups,
					 * you'll have a /lot/ of groups, and cgp_groupinfo is pretty small anyway
					 */
					if (gi->rg->left_list_size == gi->rg->left_list_entries) {
						int newsize;
						struct removed_group *newrg;

						list_del(&gi->rg->list);
						newsize = gi->rg->left_list_size * 2;
						newrg = realloc(gi->rg, sizeof(struct removed_group) + newsize*sizeof(mar_cpg_address_t));
						if (!newrg) {
							log_printf(LOG_LEVEL_CRIT, "Unable to realloc removed group struct. CPG callbacks will be junk.");
							return;
						}
						newrg->left_list_size = newsize+PROCESSOR_COUNT_MAX;
						gi->rg = newrg;
						list_add(&gi->rg->list, remlist);
					}
					gi->rg->left_list[gi->rg->left_list_entries].pid = pi->pid;
					gi->rg->left_list[gi->rg->left_list_entries].nodeid = pi->nodeid;
					gi->rg->left_list[gi->rg->left_list_entries].reason = CONFCHG_CPG_REASON_NODEDOWN;
					gi->rg->left_list_entries++;

					/* Remove node info for dead node */
					list_del(&pi->list);
					free(pi);
				}
			}
		}
	}
}


static void cpg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;
	uint32_t lowest_nodeid = 0xffffff;
	struct iovec req_exec_cpg_iovec;

	/* We don't send the library joinlist in here because it can end up
	   out of order with the rest of the messages (which are totem ordered).
	   So we get the lowest nodeid to send out a list of left nodes instead.
	   On receipt of that message, all nodes will then notify their local clients
	   of the new joinlist */

	if (left_list_entries) {
		for (i = 0; i < member_list_entries; i++) {
			if (member_list[i] < lowest_nodeid)
				lowest_nodeid = member_list[i];
		}

		log_printf(LOG_LEVEL_DEBUG, "confchg, low nodeid=%d, us = %d\n", lowest_nodeid, totempg_my_nodeid_get());
		if (lowest_nodeid == totempg_my_nodeid_get()) {

			req_exec_cpg_downlist.header.id = SERVICE_ID_MAKE(CPG_SERVICE, MESSAGE_REQ_EXEC_CPG_DOWNLIST);
			req_exec_cpg_downlist.header.size = sizeof(struct req_exec_cpg_downlist);

			req_exec_cpg_downlist.left_nodes = left_list_entries;
			for (i = 0; i < left_list_entries; i++) {
				req_exec_cpg_downlist.nodeids[i] = left_list[i];
			}
			log_printf(LOG_LEVEL_DEBUG, "confchg, build downlist: %d nodes\n", left_list_entries);
		}
	}

	/* Don't send this message until we get the final configuration message */
	if (configuration_type == TOTEM_CONFIGURATION_REGULAR && req_exec_cpg_downlist.left_nodes) {
		req_exec_cpg_iovec.iov_base = (char *)&req_exec_cpg_downlist;
		req_exec_cpg_iovec.iov_len = req_exec_cpg_downlist.header.size;

		totempg_groups_mcast_joined (openais_group_handle, &req_exec_cpg_iovec, 1, TOTEMPG_AGREED);
		req_exec_cpg_downlist.left_nodes = 0;
		log_printf(LOG_LEVEL_DEBUG, "confchg, sent downlist\n");
	}
}

static void cpg_flow_control_state_set_fn (
	void *context,
	enum openais_flow_control_state flow_control_state)
{
	struct process_info *process_info = (struct process_info *)context;

	process_info->flow_control_state = flow_control_state;
}

/* Can byteswap join & leave messages */
static void exec_cpg_procjoin_endian_convert (void *msg)
{
	struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = (struct req_exec_cpg_procjoin *)msg;

	req_exec_cpg_procjoin->pid = swab32(req_exec_cpg_procjoin->pid);
	swab_mar_cpg_name_t (&req_exec_cpg_procjoin->group_name);
	req_exec_cpg_procjoin->reason = swab32(req_exec_cpg_procjoin->reason);
}

static void exec_cpg_joinlist_endian_convert (void *msg)
{
	mar_res_header_t *res = (mar_res_header_t *)msg;
	struct join_list_entry *jle = (struct join_list_entry *)(msg + sizeof(mar_res_header_t));

	/* XXX shouldn't mar_res_header be swabbed? */

	while ((void*)jle < msg + res->size) {
		jle->pid = swab32(jle->pid);
		swab_mar_cpg_name_t (&jle->group_name);
		jle++;
	}
}

static void exec_cpg_downlist_endian_convert (void *msg)
{
	struct req_exec_cpg_downlist *req_exec_cpg_downlist = (struct req_exec_cpg_downlist *)msg;

	req_exec_cpg_downlist->left_nodes = swab32(req_exec_cpg_downlist->left_nodes);
}

static void exec_cpg_mcast_endian_convert (void *msg)
{
	struct req_exec_cpg_mcast *req_exec_cpg_mcast = (struct req_exec_cpg_mcast *)msg;

	swab_mar_req_header_t (&req_exec_cpg_mcast->header);
	swab_mar_cpg_name_t (&req_exec_cpg_mcast->group_name);
	req_exec_cpg_mcast->pid = swab32(req_exec_cpg_mcast->pid);
	req_exec_cpg_mcast->msglen = swab32(req_exec_cpg_mcast->msglen);
	swab_mar_message_source_t (&req_exec_cpg_mcast->source);
}

static void do_proc_join(
	mar_cpg_name_t *name,
	uint32_t pid,
	unsigned int nodeid,
	int reason)
{
	struct group_info *gi;
	struct process_info *pi;
	struct list_head *iter;
	mar_cpg_address_t notify_info;

	gi = get_group(name); /* this will always succeed ! */
	assert(gi);

	/* See if it already exists in this group */
	for (iter = gi->members.next; iter != &gi->members; iter = iter->next) {
		pi = list_entry(iter, struct process_info, list);
		if (pi->pid == pid && pi->nodeid == nodeid) {

			/* It could be a local join message */
			if ((nodeid == totempg_my_nodeid_get()) &&
				(!pi->flags & PI_FLAG_MEMBER)) {
				goto local_join;
			} else {
				return;
			}
		}
	}

	pi = malloc(sizeof(struct process_info));
	if (!pi) {
		log_printf(LOG_LEVEL_WARNING, "Unable to allocate process_info struct");
		return;
	}
	pi->nodeid = nodeid;
	pi->pid = pid;
	pi->group = gi;
	pi->conn = NULL;
	pi->trackerconn = NULL;
	list_add_tail(&pi->list, &gi->members);

local_join:

	pi->flags = PI_FLAG_MEMBER;
	notify_info.pid = pi->pid;
	notify_info.nodeid = nodeid;
	notify_info.reason = reason;

	notify_lib_joinlist(gi, NULL,
			    1, &notify_info,
			    0, NULL,
			    MESSAGE_RES_CPG_CONFCHG_CALLBACK);
}

static void message_handler_req_exec_cpg_downlist (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_downlist *req_exec_cpg_downlist = (struct req_exec_cpg_downlist *)message;
	int i;
	struct list_head removed_list;

	log_printf(LOG_LEVEL_DEBUG, "downlist left_list: %d\n", req_exec_cpg_downlist->left_nodes);

	list_init(&removed_list);

	/* Remove nodes from joined groups and add removed groups to the list */
	for (i = 0; i <  req_exec_cpg_downlist->left_nodes; i++) {
		remove_node_from_groups( req_exec_cpg_downlist->nodeids[i], &removed_list);
	}

	if (!list_empty(&removed_list)) {
		struct list_head *iter, *tmp;

		for (iter = removed_list.next, tmp=iter->next; iter != &removed_list; iter = tmp, tmp = iter->next) {
			struct removed_group *rg = list_entry(iter, struct removed_group, list);

			notify_lib_joinlist(rg->gi, NULL,
					    0, NULL,
					    rg->left_list_entries, rg->left_list,
					    MESSAGE_RES_CPG_CONFCHG_CALLBACK);
			rg->gi->rg = NULL;
			free(rg);
		}
	}
}

static void message_handler_req_exec_cpg_procjoin (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = (struct req_exec_cpg_procjoin *)message;

	log_printf(LOG_LEVEL_DEBUG, "got procjoin message from cluster node %d\n", nodeid);

	do_proc_join(&req_exec_cpg_procjoin->group_name,
		req_exec_cpg_procjoin->pid, nodeid,
		CONFCHG_CPG_REASON_JOIN);
}

static void message_handler_req_exec_cpg_procleave (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = (struct req_exec_cpg_procjoin *)message;
	struct group_info *gi;
	struct process_info *pi;
	struct list_head *iter;
	mar_cpg_address_t notify_info;

	log_printf(LOG_LEVEL_DEBUG, "got procleave message from cluster node %d\n", nodeid);

	gi = get_group(&req_exec_cpg_procjoin->group_name); /* this will always succeed ! */
	assert(gi);

	notify_info.pid = req_exec_cpg_procjoin->pid;
	notify_info.nodeid = nodeid;
	notify_info.reason = req_exec_cpg_procjoin->reason;

	notify_lib_joinlist(gi, NULL,
			    0, NULL,
			    1, &notify_info,
			    MESSAGE_RES_CPG_CONFCHG_CALLBACK);

        /* Find the node/PID to remove */
	for (iter = gi->members.next; iter != &gi->members; iter = iter->next) {
		pi = list_entry(iter, struct process_info, list);
		if (pi->pid == req_exec_cpg_procjoin->pid &&
		    pi->nodeid == nodeid) {

			list_del(&pi->list);
			if (!pi->conn)
				free(pi);

			if (list_empty(&gi->members)) {
				remove_group(gi);
			}
			break;
		}
	}
}


/* Got a proclist from another node */
static void message_handler_req_exec_cpg_joinlist (
	void *message,
	unsigned int nodeid)
{
	mar_res_header_t *res = (mar_res_header_t *)message;
	struct join_list_entry *jle = (struct join_list_entry *)(message + sizeof(mar_res_header_t));

	log_printf(LOG_LEVEL_NOTICE, "got joinlist message from node %d\n",
		nodeid);

	/* Ignore our own messages */
	if (nodeid == totempg_my_nodeid_get()) {
		return;
	}

	while ((void*)jle < message + res->size) {
		do_proc_join(&jle->group_name, jle->pid, nodeid,
			CONFCHG_CPG_REASON_NODEUP);
		jle++;
	}
}

static void message_handler_req_exec_cpg_mcast (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_mcast *req_exec_cpg_mcast = (struct req_exec_cpg_mcast *)message;
	struct res_lib_cpg_deliver_callback *res_lib_cpg_mcast;
	struct process_info *process_info;
	int msglen = req_exec_cpg_mcast->msglen;
	char buf[sizeof(*res_lib_cpg_mcast) + msglen];
	struct group_info *gi;
	struct list_head *iter;

	/*
	 * Track local messages so that flow is controlled on the local node
	 */
	gi = get_group(&req_exec_cpg_mcast->group_name); /* this will always succeed ! */
	assert(gi);

	res_lib_cpg_mcast = (struct res_lib_cpg_deliver_callback *)buf;
	res_lib_cpg_mcast->header.id = MESSAGE_RES_CPG_DELIVER_CALLBACK;
	res_lib_cpg_mcast->header.size = sizeof(*res_lib_cpg_mcast) + msglen;
	res_lib_cpg_mcast->msglen = msglen;
	res_lib_cpg_mcast->pid = req_exec_cpg_mcast->pid;
	res_lib_cpg_mcast->nodeid = nodeid;
	res_lib_cpg_mcast->flow_control_state = CPG_FLOW_CONTROL_DISABLED;
	if (message_source_is_local (&req_exec_cpg_mcast->source)) {
		openais_ipc_flow_control_local_decrement (req_exec_cpg_mcast->source.conn);
		process_info = (struct process_info *)openais_conn_private_data_get (req_exec_cpg_mcast->source.conn);
		res_lib_cpg_mcast->flow_control_state = process_info->flow_control_state;
	}
	memcpy(&res_lib_cpg_mcast->group_name, &gi->group_name,
		sizeof(mar_cpg_name_t));
	memcpy(&res_lib_cpg_mcast->message, (char*)message+sizeof(*req_exec_cpg_mcast),
		msglen);

	/* Send to all interested members */
	for (iter = gi->members.next; iter != &gi->members; iter = iter->next) {
		struct process_info *pi = list_entry(iter, struct process_info, list);
		if (pi->trackerconn) {
			openais_conn_send_response(
				pi->trackerconn,
				buf,
				res_lib_cpg_mcast->header.size);
		}
	}
}


static int cpg_exec_send_joinlist(void)
{
	int count = 0;
	char *buf;
	int i;
	struct list_head *iter;
	struct list_head *iter2;
	struct group_info *gi;
	mar_res_header_t *res;
	struct join_list_entry *jle;
	struct iovec req_exec_cpg_iovec;

	log_printf(LOG_LEVEL_DEBUG, "sending joinlist to cluster\n");

	/* Count the number of groups we are a member of */
	for (i=0; i<GROUP_HASH_SIZE; i++) {
		for (iter = group_lists[i].next; iter != &group_lists[i]; iter = iter->next) {
			gi = list_entry(iter, struct group_info, list);
			for (iter2 = gi->members.next; iter2 != &gi->members; iter2 = iter2->next) {
				struct process_info *pi = list_entry(iter2, struct process_info, list);
				if (pi->pid && pi->nodeid == totempg_my_nodeid_get()) {
					count++;
				}
			}
		}
	}

	/* Nothing to send */
	if (!count)
		return 0;

	buf = alloca(sizeof(mar_res_header_t) + sizeof(struct join_list_entry) * count);
	if (!buf) {
		log_printf(LOG_LEVEL_WARNING, "Unable to allocate joinlist buffer");
		return -1;
	}

	jle = (struct join_list_entry *)(buf + sizeof(mar_res_header_t));
	res = (mar_res_header_t *)buf;

	for (i=0; i<GROUP_HASH_SIZE; i++) {
		for (iter = group_lists[i].next; iter != &group_lists[i]; iter = iter->next) {

			gi = list_entry(iter, struct group_info, list);
			for (iter2 = gi->members.next; iter2 != &gi->members; iter2 = iter2->next) {

				struct process_info *pi = list_entry(iter2, struct process_info, list);
				if (pi->pid && pi->nodeid == totempg_my_nodeid_get()) {
					memcpy(&jle->group_name, &gi->group_name, sizeof(mar_cpg_name_t));
					jle->pid = pi->pid;
					jle++;
				}
			}
		}
	}

	res->id = SERVICE_ID_MAKE(CPG_SERVICE, MESSAGE_REQ_EXEC_CPG_JOINLIST);
	res->size = sizeof(mar_res_header_t)+sizeof(struct join_list_entry) * count;

	req_exec_cpg_iovec.iov_base = buf;
	req_exec_cpg_iovec.iov_len = res->size;

	return totempg_groups_mcast_joined (openais_group_handle, &req_exec_cpg_iovec, 1, TOTEMPG_AGREED);
}

static int cpg_lib_init_fn (void *conn)
{
	struct process_info *pi = (struct process_info *)openais_conn_private_data_get (conn);
	pi->conn = conn;

	log_printf(LOG_LEVEL_DEBUG, "lib_init_fn: conn=%p, pi=%p\n", conn, pi);
	return (0);
}

/* Join message from the library */
static void message_handler_req_lib_cpg_join (void *conn, void *message)
{
	struct req_lib_cpg_join *req_lib_cpg_join = (struct req_lib_cpg_join *)message;
	struct process_info *pi = (struct process_info *)openais_conn_private_data_get (conn);
	struct res_lib_cpg_join res_lib_cpg_join;
	struct group_info *gi;
	SaAisErrorT error = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "got join request on %p, pi=%p, pi->pid=%d\n", conn, pi, pi->pid);

	/* Already joined on this conn */
	if (pi->pid) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto join_err;
	}

	gi = get_group(&req_lib_cpg_join->group_name);
	if (!gi) {
		error = SA_AIS_ERR_NO_SPACE;
		goto join_err;
	}

	openais_ipc_flow_control_create (
		conn,
		CPG_SERVICE,
		req_lib_cpg_join->group_name.value,
		req_lib_cpg_join->group_name.length,
		cpg_flow_control_state_set_fn,
		pi);

	/* Add a node entry for us */
	pi->nodeid = totempg_my_nodeid_get();
	pi->pid = req_lib_cpg_join->pid;
	pi->group = gi;
	list_add(&pi->list, &gi->members);

	/* Tell the rest of the cluster */
	cpg_node_joinleave_send(gi, pi, MESSAGE_REQ_EXEC_CPG_PROCJOIN, CONFCHG_CPG_REASON_JOIN);

join_err:
	res_lib_cpg_join.header.size = sizeof(res_lib_cpg_join);
	res_lib_cpg_join.header.id = MESSAGE_RES_CPG_JOIN;
	res_lib_cpg_join.header.error = error;
	openais_conn_send_response(conn, &res_lib_cpg_join, sizeof(res_lib_cpg_join));
}

/* Leave message from the library */
static void message_handler_req_lib_cpg_leave (void *conn, void *message)
{
	struct process_info *pi = (struct process_info *)openais_conn_private_data_get (conn);
	struct res_lib_cpg_leave res_lib_cpg_leave;
	struct group_info *gi;
	SaAisErrorT error = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "got leave request on %p\n", conn);

	if (!pi || !pi->pid || !pi->group) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto leave_ret;
	}
	gi = pi->group;

	/* Tell other nodes we are leaving.
	   When we get this message back we will leave too */
	cpg_node_joinleave_send(gi, pi, MESSAGE_REQ_EXEC_CPG_PROCLEAVE, CONFCHG_CPG_REASON_LEAVE);
	pi->group = NULL;

	openais_ipc_flow_control_destroy (
		conn,
		CPG_SERVICE,
		(unsigned char *)gi->group_name.value,
		(unsigned int)gi->group_name.length);

leave_ret:
	/* send return */
	res_lib_cpg_leave.header.size = sizeof(res_lib_cpg_leave);
	res_lib_cpg_leave.header.id = MESSAGE_RES_CPG_LEAVE;
	res_lib_cpg_leave.header.error = error;
	openais_conn_send_response(conn, &res_lib_cpg_leave, sizeof(res_lib_cpg_leave));
}

/* Mcast message from the library */
static void message_handler_req_lib_cpg_mcast (void *conn, void *message)
{
	struct req_lib_cpg_mcast *req_lib_cpg_mcast = (struct req_lib_cpg_mcast *)message;
	struct process_info *pi = (struct process_info *)openais_conn_private_data_get (conn);
	struct group_info *gi = pi->group;
	struct iovec req_exec_cpg_iovec[2];
	struct req_exec_cpg_mcast req_exec_cpg_mcast;
	struct res_lib_cpg_mcast res_lib_cpg_mcast;
	int msglen = req_lib_cpg_mcast->msglen;
	int result;

	log_printf(LOG_LEVEL_DEBUG, "got mcast request on %p\n", conn);

	/* Can't send if we're not joined */
	if (!gi) {
		res_lib_cpg_mcast.header.size = sizeof(res_lib_cpg_mcast);
		res_lib_cpg_mcast.header.id = MESSAGE_RES_CPG_MCAST;
		res_lib_cpg_mcast.header.error = SA_AIS_ERR_ACCESS; /* TODO Better error code ?? */
		res_lib_cpg_mcast.flow_control_state = CPG_FLOW_CONTROL_DISABLED;
		openais_conn_send_response(conn, &res_lib_cpg_mcast,
			sizeof(res_lib_cpg_mcast));
		return;
	}

	req_exec_cpg_mcast.header.size = sizeof(req_exec_cpg_mcast) + msglen;
	req_exec_cpg_mcast.header.id = SERVICE_ID_MAKE(CPG_SERVICE,
		MESSAGE_REQ_EXEC_CPG_MCAST);
	req_exec_cpg_mcast.pid = pi->pid;
	req_exec_cpg_mcast.msglen = msglen;
	message_source_set (&req_exec_cpg_mcast.source, conn);
	memcpy(&req_exec_cpg_mcast.group_name, &gi->group_name,
		sizeof(mar_cpg_name_t));

	req_exec_cpg_iovec[0].iov_base = (char *)&req_exec_cpg_mcast;
	req_exec_cpg_iovec[0].iov_len = sizeof(req_exec_cpg_mcast);
	req_exec_cpg_iovec[1].iov_base = (char *)&req_lib_cpg_mcast->message;
	req_exec_cpg_iovec[1].iov_len = msglen;

	// TODO: guarantee type...
	result = totempg_groups_mcast_joined (openais_group_handle, req_exec_cpg_iovec, 2, TOTEMPG_AGREED);
	openais_ipc_flow_control_local_increment (conn);

	res_lib_cpg_mcast.header.size = sizeof(res_lib_cpg_mcast);
	res_lib_cpg_mcast.header.id = MESSAGE_RES_CPG_MCAST;
	res_lib_cpg_mcast.header.error = SA_AIS_OK;
	res_lib_cpg_mcast.flow_control_state = pi->flow_control_state;
	openais_conn_send_response(conn, &res_lib_cpg_mcast,
		sizeof(res_lib_cpg_mcast));
}

static void message_handler_req_lib_cpg_membership (void *conn, void *message)
{
	struct process_info *pi = (struct process_info *)openais_conn_private_data_get (conn);

	log_printf(LOG_LEVEL_DEBUG, "got membership request on %p\n", conn);
	if (!pi->group) {
		mar_res_header_t res;
		res.size = sizeof(res);
		res.id = MESSAGE_RES_CPG_MEMBERSHIP;
		res.error = SA_AIS_ERR_ACCESS; /* TODO Better error code */
		openais_conn_send_response(conn, &res, sizeof(res));
		return;
	}

	notify_lib_joinlist(pi->group, conn, 0, NULL, 0, NULL, MESSAGE_RES_CPG_MEMBERSHIP);
}


static void message_handler_req_lib_cpg_trackstart (void *conn, void *message)
{
	struct req_lib_cpg_trackstart *req_lib_cpg_trackstart = (struct req_lib_cpg_trackstart *)message;
	struct res_lib_cpg_trackstart res_lib_cpg_trackstart;
	struct group_info *gi;
	struct process_info *otherpi;
	void *otherconn;
	SaAisErrorT error = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "got trackstart request on %p\n", conn);

	gi = get_group(&req_lib_cpg_trackstart->group_name);
	if (!gi) {
		error = SA_AIS_ERR_NO_SPACE;
		goto tstart_ret;
	}

	/* Find the partner connection and add us to it's process_info struct */
	otherconn = openais_conn_partner_get (conn);
	otherpi = (struct process_info *)openais_conn_private_data_get (conn);
	otherpi->trackerconn = conn;

tstart_ret:
	res_lib_cpg_trackstart.header.size = sizeof(res_lib_cpg_trackstart);
	res_lib_cpg_trackstart.header.id = MESSAGE_RES_CPG_TRACKSTART;
	res_lib_cpg_trackstart.header.error = SA_AIS_OK;
	openais_conn_send_response(conn, &res_lib_cpg_trackstart, sizeof(res_lib_cpg_trackstart));
}

static void message_handler_req_lib_cpg_trackstop (void *conn, void *message)
{
	struct req_lib_cpg_trackstop *req_lib_cpg_trackstop = (struct req_lib_cpg_trackstop *)message;
	struct res_lib_cpg_trackstop res_lib_cpg_trackstop;
	struct process_info *otherpi;
	void *otherconn;
	struct group_info *gi;
	SaAisErrorT error = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "got trackstop request on %p\n", conn);

	gi = get_group(&req_lib_cpg_trackstop->group_name);
	if (!gi) {
		error = SA_AIS_ERR_NO_SPACE;
		goto tstop_ret;
	}

	/* Find the partner connection and add us to it's process_info struct */
	otherconn = openais_conn_partner_get (conn);
	otherpi = (struct process_info *)openais_conn_private_data_get (conn);
	otherpi->trackerconn = NULL;

tstop_ret:
	res_lib_cpg_trackstop.header.size = sizeof(res_lib_cpg_trackstop);
	res_lib_cpg_trackstop.header.id = MESSAGE_RES_CPG_TRACKSTOP;
	res_lib_cpg_trackstop.header.error = SA_AIS_OK;
	openais_conn_send_response(conn, &res_lib_cpg_trackstop.header, sizeof(res_lib_cpg_trackstop));
}
