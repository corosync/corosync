/*
 * Copyright (c) 2006-2015 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
 * Author: Jan Friesse (jfriesse@redhat.com)
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

#ifdef HAVE_ALLOCA_H
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
#include <time.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <qb/qbmap.h>

#include <corosync/corotypes.h>
#include <qb/qbipc_common.h>
#include <corosync/corodefs.h>
#include <corosync/list.h>
#include <corosync/logsys.h>
#include <corosync/coroapi.h>

#include <corosync/cpg.h>
#include <corosync/ipc_cpg.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#include "service.h"

LOGSYS_DECLARE_SUBSYS ("CPG");

#define GROUP_HASH_SIZE 32

enum cpg_message_req_types {
	MESSAGE_REQ_EXEC_CPG_PROCJOIN = 0,
	MESSAGE_REQ_EXEC_CPG_PROCLEAVE = 1,
	MESSAGE_REQ_EXEC_CPG_JOINLIST = 2,
	MESSAGE_REQ_EXEC_CPG_MCAST = 3,
	MESSAGE_REQ_EXEC_CPG_DOWNLIST_OLD = 4,
	MESSAGE_REQ_EXEC_CPG_DOWNLIST = 5,
	MESSAGE_REQ_EXEC_CPG_PARTIAL_MCAST = 6,
};

struct zcb_mapped {
	struct list_head list;
	void *addr;
	size_t size;
};
/*
 * state`		exec deliver
 * match group name, pid -> if matched deliver for YES:
 * XXX indicates impossible state
 *
 *			join			leave			mcast
 * UNJOINED		XXX			XXX			NO
 * LEAVE_STARTED	XXX			YES(unjoined_enter)	YES
 * JOIN_STARTED		YES(join_started_enter)	XXX			NO
 * JOIN_COMPLETED	XXX			NO			YES
 *
 * join_started_enter
 * 	set JOIN_COMPLETED
 *	add entry to process_info list
 * unjoined_enter
 *	set UNJOINED
 *	delete entry from process_info list
 *
 *
 *			library accept join error codes
 * UNJOINED		YES(CS_OK) 			set JOIN_STARTED
 * LEAVE_STARTED	NO(CS_ERR_BUSY)
 * JOIN_STARTED		NO(CS_ERR_EXIST)
 * JOIN_COMPlETED	NO(CS_ERR_EXIST)
 *
 *			library accept leave error codes
 * UNJOINED		NO(CS_ERR_NOT_EXIST)
 * LEAVE_STARTED	NO(CS_ERR_NOT_EXIST)
 * JOIN_STARTED		NO(CS_ERR_BUSY)
 * JOIN_COMPLETED	YES(CS_OK)			set LEAVE_STARTED
 *
 *			library accept mcast
 * UNJOINED		NO(CS_ERR_NOT_EXIST)
 * LEAVE_STARTED	NO(CS_ERR_NOT_EXIST)
 * JOIN_STARTED		YES(CS_OK)
 * JOIN_COMPLETED	YES(CS_OK)
 */
enum cpd_state {
	CPD_STATE_UNJOINED,
	CPD_STATE_LEAVE_STARTED,
	CPD_STATE_JOIN_STARTED,
	CPD_STATE_JOIN_COMPLETED
};

enum cpg_sync_state {
	CPGSYNC_DOWNLIST,
	CPGSYNC_JOINLIST
};

enum cpg_downlist_state_e {
       CPG_DOWNLIST_NONE,
       CPG_DOWNLIST_WAITING_FOR_MESSAGES,
       CPG_DOWNLIST_APPLYING,
};
static enum cpg_downlist_state_e downlist_state;
static struct list_head downlist_messages_head;
static struct list_head joinlist_messages_head;

struct cpg_pd {
	void *conn;
 	mar_cpg_name_t group_name;
	uint32_t pid;
	enum cpd_state cpd_state;
	unsigned int flags;
	int initial_totem_conf_sent;
	uint64_t transition_counter; /* These two are used when sending fragmented messages */
	uint64_t initial_transition_counter;
	struct list_head list;
	struct list_head iteration_instance_list_head;
	struct list_head zcb_mapped_list_head;
};

struct cpg_iteration_instance {
	hdb_handle_t handle;
	struct list_head list;
	struct list_head items_list_head; /* List of process_info */
	struct list_head *current_pointer;
};

DECLARE_HDB_DATABASE(cpg_iteration_handle_t_db,NULL);

DECLARE_LIST_INIT(cpg_pd_list_head);

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list_entries;

static unsigned int my_old_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_old_member_list_entries = 0;

static struct corosync_api_v1 *api = NULL;

static enum cpg_sync_state my_sync_state = CPGSYNC_DOWNLIST;

static mar_cpg_ring_id_t last_sync_ring_id;

struct process_info {
	unsigned int nodeid;
	uint32_t pid;
	mar_cpg_name_t group;
	struct list_head list; /* on the group_info members list */
};
DECLARE_LIST_INIT(process_info_list_head);

struct join_list_entry {
	uint32_t pid;
	mar_cpg_name_t group_name;
};

/*
 * Service Interfaces required by service_message_handler struct
 */
static char *cpg_exec_init_fn (struct corosync_api_v1 *);

static int cpg_lib_init_fn (void *conn);

static int cpg_lib_exit_fn (void *conn);

static void message_handler_req_exec_cpg_procjoin (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_procleave (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_joinlist (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_mcast (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_partial_mcast (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_downlist_old (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_downlist (
	const void *message,
	unsigned int nodeid);

static void exec_cpg_procjoin_endian_convert (void *msg);

static void exec_cpg_joinlist_endian_convert (void *msg);

static void exec_cpg_mcast_endian_convert (void *msg);

static void exec_cpg_partial_mcast_endian_convert (void *msg);

static void exec_cpg_downlist_endian_convert_old (void *msg);

static void exec_cpg_downlist_endian_convert (void *msg);

static void message_handler_req_lib_cpg_join (void *conn, const void *message);

static void message_handler_req_lib_cpg_leave (void *conn, const void *message);

static void message_handler_req_lib_cpg_finalize (void *conn, const void *message);

static void message_handler_req_lib_cpg_mcast (void *conn, const void *message);

static void message_handler_req_lib_cpg_partial_mcast (void *conn, const void *message);

static void message_handler_req_lib_cpg_membership (void *conn,
						    const void *message);

static void message_handler_req_lib_cpg_local_get (void *conn,
						   const void *message);

static void message_handler_req_lib_cpg_iteration_initialize (
	void *conn,
	const void *message);

static void message_handler_req_lib_cpg_iteration_next (
	void *conn,
	const void *message);

static void message_handler_req_lib_cpg_iteration_finalize (
	void *conn,
	const void *message);

static void message_handler_req_lib_cpg_zc_alloc (
	void *conn,
	const void *message);

static void message_handler_req_lib_cpg_zc_free (
	void *conn,
	const void *message);

static void message_handler_req_lib_cpg_zc_execute (
	void *conn,
	const void *message);

static int cpg_node_joinleave_send (unsigned int pid, const mar_cpg_name_t *group_name, int fn, int reason);

static int cpg_exec_send_downlist(void);

static int cpg_exec_send_joinlist(void);

static void downlist_messages_delete (void);

static void downlist_master_choose_and_send (void);

static void joinlist_inform_clients (void);

static void joinlist_messages_delete (void);

static void cpg_sync_init (
	const unsigned int *trans_list,
	size_t trans_list_entries,
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int  cpg_sync_process (void);

static void cpg_sync_activate (void);

static void cpg_sync_abort (void);

static void do_proc_join(
	const mar_cpg_name_t *name,
	uint32_t pid,
	unsigned int nodeid,
	int reason);

static void do_proc_leave(
	const mar_cpg_name_t *name,
	uint32_t pid,
	unsigned int nodeid,
	int reason);

static int notify_lib_totem_membership (
	void *conn,
	int member_list_entries,
	const unsigned int *member_list);

static inline int zcb_all_free (
	struct cpg_pd *cpd);

static char *cpg_print_group_name (
	const mar_cpg_name_t *group);

/*
 * Library Handler Definition
 */
static struct corosync_lib_handler cpg_lib_engine[] =
{
	{ /* 0 - MESSAGE_REQ_CPG_JOIN */
		.lib_handler_fn				= message_handler_req_lib_cpg_join,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 - MESSAGE_REQ_CPG_LEAVE */
		.lib_handler_fn				= message_handler_req_lib_cpg_leave,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 - MESSAGE_REQ_CPG_MCAST */
		.lib_handler_fn				= message_handler_req_lib_cpg_mcast,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 - MESSAGE_REQ_CPG_MEMBERSHIP */
		.lib_handler_fn				= message_handler_req_lib_cpg_membership,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 - MESSAGE_REQ_CPG_LOCAL_GET */
		.lib_handler_fn				= message_handler_req_lib_cpg_local_get,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 - MESSAGE_REQ_CPG_ITERATIONINITIALIZE */
		.lib_handler_fn				= message_handler_req_lib_cpg_iteration_initialize,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 - MESSAGE_REQ_CPG_ITERATIONNEXT */
		.lib_handler_fn				= message_handler_req_lib_cpg_iteration_next,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 - MESSAGE_REQ_CPG_ITERATIONFINALIZE */
		.lib_handler_fn				= message_handler_req_lib_cpg_iteration_finalize,
		.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 - MESSAGE_REQ_CPG_FINALIZE */
		.lib_handler_fn				= message_handler_req_lib_cpg_finalize,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn				= message_handler_req_lib_cpg_zc_alloc,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn				= message_handler_req_lib_cpg_zc_free,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn				= message_handler_req_lib_cpg_zc_execute,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn				= message_handler_req_lib_cpg_partial_mcast,
		.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED
	},

};

static struct corosync_exec_handler cpg_exec_engine[] =
{
	{ /* 0 - MESSAGE_REQ_EXEC_CPG_PROCJOIN */
		.exec_handler_fn	= message_handler_req_exec_cpg_procjoin,
		.exec_endian_convert_fn	= exec_cpg_procjoin_endian_convert
	},
	{ /* 1 - MESSAGE_REQ_EXEC_CPG_PROCLEAVE */
		.exec_handler_fn	= message_handler_req_exec_cpg_procleave,
		.exec_endian_convert_fn	= exec_cpg_procjoin_endian_convert
	},
	{ /* 2 - MESSAGE_REQ_EXEC_CPG_JOINLIST */
		.exec_handler_fn	= message_handler_req_exec_cpg_joinlist,
		.exec_endian_convert_fn	= exec_cpg_joinlist_endian_convert
	},
	{ /* 3 - MESSAGE_REQ_EXEC_CPG_MCAST */
		.exec_handler_fn	= message_handler_req_exec_cpg_mcast,
		.exec_endian_convert_fn	= exec_cpg_mcast_endian_convert
	},
	{ /* 4 - MESSAGE_REQ_EXEC_CPG_DOWNLIST_OLD */
		.exec_handler_fn	= message_handler_req_exec_cpg_downlist_old,
		.exec_endian_convert_fn	= exec_cpg_downlist_endian_convert_old
	},
	{ /* 5 - MESSAGE_REQ_EXEC_CPG_DOWNLIST */
		.exec_handler_fn	= message_handler_req_exec_cpg_downlist,
		.exec_endian_convert_fn	= exec_cpg_downlist_endian_convert
	},
	{ /* 6 - MESSAGE_REQ_EXEC_CPG_PARTIAL_MCAST */
		.exec_handler_fn	= message_handler_req_exec_cpg_partial_mcast,
		.exec_endian_convert_fn	= exec_cpg_partial_mcast_endian_convert
	},
};

struct corosync_service_engine cpg_service_engine = {
	.name				        = "corosync cluster closed process group service v1.01",
	.id					= CPG_SERVICE,
	.priority				= 1,
	.private_data_size			= sizeof (struct cpg_pd),
	.flow_control				= CS_LIB_FLOW_CONTROL_REQUIRED,
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
	.lib_init_fn				= cpg_lib_init_fn,
	.lib_exit_fn				= cpg_lib_exit_fn,
	.lib_engine				= cpg_lib_engine,
	.lib_engine_count			= sizeof (cpg_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn				= cpg_exec_init_fn,
	.exec_dump_fn				= NULL,
	.exec_engine				= cpg_exec_engine,
	.exec_engine_count		        = sizeof (cpg_exec_engine) / sizeof (struct corosync_exec_handler),
	.sync_init                              = cpg_sync_init,
	.sync_process                           = cpg_sync_process,
	.sync_activate                          = cpg_sync_activate,
	.sync_abort                             = cpg_sync_abort
};

struct corosync_service_engine *cpg_get_service_engine_ver0 (void)
{
	return (&cpg_service_engine);
}

struct req_exec_cpg_procjoin {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_uint32_t reason __attribute__((aligned(8)));
};

struct req_exec_cpg_mcast {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t msglen __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_uint8_t message[] __attribute__((aligned(8)));
};

struct req_exec_cpg_partial_mcast {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t msglen __attribute__((aligned(8)));
	mar_uint32_t fraglen __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_uint32_t type __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_uint8_t message[] __attribute__((aligned(8)));
};

struct req_exec_cpg_downlist_old {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_uint32_t left_nodes __attribute__((aligned(8)));
	mar_uint32_t nodeids[PROCESSOR_COUNT_MAX]  __attribute__((aligned(8)));
};

struct req_exec_cpg_downlist {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	/* merge decisions */
	mar_uint32_t old_members __attribute__((aligned(8)));
	/* downlist below */
	mar_uint32_t left_nodes __attribute__((aligned(8)));
	mar_uint32_t nodeids[PROCESSOR_COUNT_MAX]  __attribute__((aligned(8)));
};

struct downlist_msg {
	mar_uint32_t sender_nodeid;
	mar_uint32_t old_members __attribute__((aligned(8)));
	mar_uint32_t left_nodes __attribute__((aligned(8)));
	mar_uint32_t nodeids[PROCESSOR_COUNT_MAX]  __attribute__((aligned(8)));
	struct list_head list;
};

struct joinlist_msg {
	mar_uint32_t sender_nodeid;
	uint32_t pid;
	mar_cpg_name_t group_name;
	struct list_head list;
};

static struct req_exec_cpg_downlist g_req_exec_cpg_downlist;

/*
 * Function print group name. It's not reentrant
 */
static char *cpg_print_group_name(const mar_cpg_name_t *group)
{
	static char res[CPG_MAX_NAME_LENGTH * 4 + 1];
	int dest_pos = 0;
	char c;
	int i;

	for (i = 0; i < group->length; i++) {
		c = group->value[i];

		if (c >= ' ' && c < 0x7f && c != '\\') {
			res[dest_pos++] = c;
                } else {
			if (c == '\\') {
				res[dest_pos++] = '\\';
				res[dest_pos++] = '\\';
			} else {
				snprintf(res + dest_pos, sizeof(res) - dest_pos, "\\x%02X", c);
				dest_pos += 4;
			}
		}
	}
	res[dest_pos] = 0;

	return (res);
}

static void cpg_sync_init (
	const unsigned int *trans_list,
	size_t trans_list_entries,
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	int entries;
	int i, j;
	int found;

	my_sync_state = CPGSYNC_DOWNLIST;

	memcpy (my_member_list, member_list, member_list_entries *
		sizeof (unsigned int));
	my_member_list_entries = member_list_entries;

	last_sync_ring_id.nodeid = ring_id->rep.nodeid;
	last_sync_ring_id.seq = ring_id->seq;

	downlist_state = CPG_DOWNLIST_WAITING_FOR_MESSAGES;

	entries = 0;
	/*
	 * Determine list of nodeids for downlist message
	 */
	for (i = 0; i < my_old_member_list_entries; i++) {
		found = 0;
		for (j = 0; j < trans_list_entries; j++) {
			if (my_old_member_list[i] == trans_list[j]) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			g_req_exec_cpg_downlist.nodeids[entries++] =
				my_old_member_list[i];
		}
	}
	g_req_exec_cpg_downlist.left_nodes = entries;
}

static int cpg_sync_process (void)
{
	int res = -1;

	if (my_sync_state == CPGSYNC_DOWNLIST) {
		res = cpg_exec_send_downlist();
		if (res == -1) {
			return (-1);
		}
		my_sync_state = CPGSYNC_JOINLIST;
	}
	if (my_sync_state == CPGSYNC_JOINLIST) {
		res = cpg_exec_send_joinlist();
	}
	return (res);
}

static void cpg_sync_activate (void)
{
	memcpy (my_old_member_list, my_member_list,
		my_member_list_entries * sizeof (unsigned int));
	my_old_member_list_entries = my_member_list_entries;

	if (downlist_state == CPG_DOWNLIST_WAITING_FOR_MESSAGES) {
		downlist_master_choose_and_send ();
	}

	joinlist_inform_clients ();

	downlist_messages_delete ();
	downlist_state = CPG_DOWNLIST_NONE;
	joinlist_messages_delete ();

	notify_lib_totem_membership (NULL, my_member_list_entries, my_member_list);
}

static void cpg_sync_abort (void)
{
	downlist_state = CPG_DOWNLIST_NONE;
	downlist_messages_delete ();
	joinlist_messages_delete ();
}

static int notify_lib_totem_membership (
	void *conn,
	int member_list_entries,
	const unsigned int *member_list)
{
	struct list_head *iter;
	char *buf;
	int size;
	struct res_lib_cpg_totem_confchg_callback *res;

	size = sizeof(struct res_lib_cpg_totem_confchg_callback) +
		sizeof(mar_uint32_t) * (member_list_entries);
	buf = alloca(size);
	if (!buf)
		return CS_ERR_LIBRARY;

	res = (struct res_lib_cpg_totem_confchg_callback *)buf;
	res->member_list_entries = member_list_entries;
	res->header.size = size;
	res->header.id = MESSAGE_RES_CPG_TOTEM_CONFCHG_CALLBACK;
	res->header.error = CS_OK;

	memcpy (&res->ring_id, &last_sync_ring_id, sizeof (mar_cpg_ring_id_t));
	memcpy (res->member_list, member_list, res->member_list_entries * sizeof (mar_uint32_t));

	if (conn == NULL) {
		for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; iter = iter->next) {
			struct cpg_pd *cpg_pd = list_entry (iter, struct cpg_pd, list);
			api->ipc_dispatch_send (cpg_pd->conn, buf, size);
		}
	} else {
		api->ipc_dispatch_send (conn, buf, size);
	}

	return CS_OK;
}

static int notify_lib_joinlist(
	const mar_cpg_name_t *group_name,
	void *conn,
	int joined_list_entries,
	mar_cpg_address_t *joined_list,
	int left_list_entries,
	mar_cpg_address_t *left_list,
	int id)
{
	int size;
	char *buf;
	struct list_head *iter;
	int count;
	struct res_lib_cpg_confchg_callback *res;
	mar_cpg_address_t *retgi;

	count = 0;

	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi = list_entry (iter, struct process_info, list);
		if (mar_name_compare (&pi->group, group_name) == 0) {
			int i;
			int founded = 0;

			for (i = 0; i < left_list_entries; i++) {
				if (left_list[i].nodeid == pi->nodeid && left_list[i].pid == pi->pid) {
					founded++;
				}
			}

			if (!founded)
				count++;
		}
	}

	size = sizeof(struct res_lib_cpg_confchg_callback) +
		sizeof(mar_cpg_address_t) * (count + left_list_entries + joined_list_entries);
	buf = alloca(size);
	if (!buf)
		return CS_ERR_LIBRARY;

	res = (struct res_lib_cpg_confchg_callback *)buf;
	res->joined_list_entries = joined_list_entries;
	res->left_list_entries = left_list_entries;
	res->member_list_entries = count;
	retgi = res->member_list;
	res->header.size = size;
	res->header.id = id;
	res->header.error = CS_OK;
	memcpy(&res->group_name, group_name, sizeof(mar_cpg_name_t));

	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi=list_entry (iter, struct process_info, list);

		if (mar_name_compare (&pi->group, group_name) == 0) {
			int i;
			int founded = 0;

			for (i = 0;i < left_list_entries; i++) {
				if (left_list[i].nodeid == pi->nodeid && left_list[i].pid == pi->pid) {
					founded++;
				}
			}

			if (!founded) {
				retgi->nodeid = pi->nodeid;
				retgi->pid = pi->pid;
				retgi++;
			}
		}
	}

	if (left_list_entries) {
		memcpy (retgi, left_list, left_list_entries * sizeof(mar_cpg_address_t));
		retgi += left_list_entries;
	}

	if (joined_list_entries) {
		memcpy (retgi, joined_list, joined_list_entries * sizeof(mar_cpg_address_t));
		retgi += joined_list_entries;
	}

	if (conn) {
		api->ipc_dispatch_send (conn, buf, size);
	} else {
		for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; iter = iter->next) {
			struct cpg_pd *cpd = list_entry (iter, struct cpg_pd, list);
			if (mar_name_compare (&cpd->group_name, group_name) == 0) {
				assert (joined_list_entries <= 1);
				if (joined_list_entries) {
					if (joined_list[0].pid == cpd->pid &&
						joined_list[0].nodeid == api->totem_nodeid_get()) {
						cpd->cpd_state = CPD_STATE_JOIN_COMPLETED;
					}
				}
				if (cpd->cpd_state == CPD_STATE_JOIN_COMPLETED ||
					cpd->cpd_state == CPD_STATE_LEAVE_STARTED) {

					api->ipc_dispatch_send (cpd->conn, buf, size);
					cpd->transition_counter++;
				}
				if (left_list_entries) {
					if (left_list[0].pid == cpd->pid &&
						left_list[0].nodeid == api->totem_nodeid_get() &&
						left_list[0].reason == CONFCHG_CPG_REASON_LEAVE) {

						cpd->pid = 0;
						memset (&cpd->group_name, 0, sizeof(cpd->group_name));
						cpd->cpd_state = CPD_STATE_UNJOINED;
					}
				}
			}
		}
	}


	/*
	 * Traverse thru cpds and send totem membership for cpd, where it is not send yet
	 */
	for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; iter = iter->next) {
		struct cpg_pd *cpd = list_entry (iter, struct cpg_pd, list);

		if ((cpd->flags & CPG_MODEL_V1_DELIVER_INITIAL_TOTEM_CONF) && (cpd->initial_totem_conf_sent == 0)) {
			cpd->initial_totem_conf_sent = 1;

			notify_lib_totem_membership (cpd->conn, my_old_member_list_entries, my_old_member_list);
		}
	}

	return CS_OK;
}

static void downlist_log(const char *msg, struct downlist_msg* dl)
{
	log_printf (LOG_DEBUG,
		    "%s: sender %s; members(old:%d left:%d)",
		    msg,
		    api->totem_ifaces_print(dl->sender_nodeid),
		    dl->old_members,
		    dl->left_nodes);
}

static struct downlist_msg* downlist_master_choose (void)
{
	struct downlist_msg *cmp;
	struct downlist_msg *best = NULL;
	struct list_head *iter;
	uint32_t cmp_members;
	uint32_t best_members;
	uint32_t i;
	int ignore_msg;

	for (iter = downlist_messages_head.next;
		iter != &downlist_messages_head;
		iter = iter->next) {

		cmp = list_entry(iter, struct downlist_msg, list);
		downlist_log("comparing", cmp);

		ignore_msg = 0;
		for (i = 0; i < cmp->left_nodes; i++) {
			if (cmp->nodeids[i] == api->totem_nodeid_get()) {
				log_printf (LOG_DEBUG, "Ignoring this entry because I'm in the left list\n");

				ignore_msg = 1;
				break;
			}
		}

		if (ignore_msg) {
			continue ;
		}

		if (best == NULL) {
			best = cmp;
			continue;
		}

		best_members = best->old_members - best->left_nodes;
		cmp_members = cmp->old_members - cmp->left_nodes;

		if (cmp_members > best_members) {
			best = cmp;
		} else if (cmp_members == best_members) {
			if (cmp->old_members > best->old_members) {
				best = cmp;
			} else if (cmp->old_members == best->old_members) {
				if (cmp->sender_nodeid < best->sender_nodeid) {
					best = cmp;
				}
			}
		}
	}

	assert (best != NULL);

	return best;
}

static void downlist_master_choose_and_send (void)
{
	struct downlist_msg *stored_msg;
	struct list_head *iter;
	struct process_info *left_pi;
	qb_map_t *group_map;
	struct cpg_name cpg_group;
	mar_cpg_name_t group;
	struct confchg_data{
		struct cpg_name cpg_group;
		mar_cpg_address_t left_list[CPG_MEMBERS_MAX];
		int left_list_entries;
		struct list_head  list;
	} *pcd;
	qb_map_iter_t *miter;
	int i, size;

	downlist_state = CPG_DOWNLIST_APPLYING;

	stored_msg = downlist_master_choose ();
	if (!stored_msg) {
		log_printf (LOGSYS_LEVEL_DEBUG, "NO chosen downlist");
		return;
	}
	downlist_log("chosen downlist", stored_msg);

	group_map = qb_skiplist_create();

	/*
	 * only the cpg groups included in left nodes should receive
	 * confchg event, so we will collect these cpg groups and
	 * relative left_lists here.
	 */
	for (iter = process_info_list_head.next; iter != &process_info_list_head; ) {
		struct process_info *pi = list_entry(iter, struct process_info, list);
		iter = iter->next;

		left_pi = NULL;
		for (i = 0; i < stored_msg->left_nodes; i++) {

			if (pi->nodeid == stored_msg->nodeids[i]) {
				left_pi = pi;
				break;
			}
		}

		if (left_pi) {
			marshall_from_mar_cpg_name_t(&cpg_group, &left_pi->group);
			cpg_group.value[cpg_group.length] = 0;

			pcd = (struct confchg_data *)qb_map_get(group_map, cpg_group.value);
			if (pcd == NULL) {
				pcd = (struct confchg_data *)calloc(1, sizeof(struct confchg_data));
				memcpy(&pcd->cpg_group, &cpg_group, sizeof(struct cpg_name));
				qb_map_put(group_map, pcd->cpg_group.value, pcd);
			}
			size = pcd->left_list_entries;
			pcd->left_list[size].nodeid = left_pi->nodeid;
			pcd->left_list[size].pid = left_pi->pid;
			pcd->left_list[size].reason = CONFCHG_CPG_REASON_NODEDOWN;
			pcd->left_list_entries++;
			list_del (&left_pi->list);
			free (left_pi);
		}
	}

	/* send only one confchg event per cpg group */
	miter = qb_map_iter_create(group_map);
	while (qb_map_iter_next(miter, (void **)&pcd)) {
		marshall_to_mar_cpg_name_t(&group, &pcd->cpg_group);

		log_printf (LOG_DEBUG, "left_list_entries:%d", pcd->left_list_entries);
		for (i=0; i<pcd->left_list_entries; i++) {
			log_printf (LOG_DEBUG, "left_list[%d] group:%s, ip:%s, pid:%d",
				i, cpg_print_group_name(&group),
				(char*)api->totem_ifaces_print(pcd->left_list[i].nodeid),
				pcd->left_list[i].pid);
		}

		/* send confchg event */
		notify_lib_joinlist(&group, NULL,
			0, NULL,
			pcd->left_list_entries,
			pcd->left_list,
			MESSAGE_RES_CPG_CONFCHG_CALLBACK);

		free(pcd);
	}
	qb_map_iter_free(miter);
	qb_map_destroy(group_map);
}

/*
 * Remove processes that might have left the group while we were suspended.
 */
static void joinlist_remove_zombie_pi_entries (void)
{
	struct list_head *pi_iter;
	struct list_head *jl_iter;
	struct process_info *pi;
	struct joinlist_msg *stored_msg;
	int found;

	for (pi_iter = process_info_list_head.next; pi_iter != &process_info_list_head; ) {
		pi = list_entry (pi_iter, struct process_info, list);
		pi_iter = pi_iter->next;

		/*
		 * Ignore local node
		 */
		if (pi->nodeid == api->totem_nodeid_get()) {
			continue ;
		}

		/*
		 * Try to find message in joinlist messages
		 */
		found = 0;
		for (jl_iter = joinlist_messages_head.next;
			jl_iter != &joinlist_messages_head;
			jl_iter = jl_iter->next) {

			stored_msg = list_entry(jl_iter, struct joinlist_msg, list);

			if (stored_msg->sender_nodeid == api->totem_nodeid_get()) {
				continue ;
			}

			if (pi->nodeid == stored_msg->sender_nodeid &&
			    pi->pid == stored_msg->pid &&
			    mar_name_compare (&pi->group, &stored_msg->group_name) == 0) {
				found = 1;
				break ;
			}
		}

		if (!found) {
			do_proc_leave(&pi->group, pi->pid, pi->nodeid, CONFCHG_CPG_REASON_PROCDOWN);
		}
	}
}

static void joinlist_inform_clients (void)
{
	struct joinlist_msg *stored_msg;
	struct list_head *iter;
	unsigned int i;

	i = 0;
	for (iter = joinlist_messages_head.next;
		iter != &joinlist_messages_head;
		iter = iter->next) {

		stored_msg = list_entry(iter, struct joinlist_msg, list);

		log_printf (LOG_DEBUG, "joinlist_messages[%u] group:%s, ip:%s, pid:%d",
			i++, cpg_print_group_name(&stored_msg->group_name),
			(char*)api->totem_ifaces_print(stored_msg->sender_nodeid),
			stored_msg->pid);

		/* Ignore our own messages */
		if (stored_msg->sender_nodeid == api->totem_nodeid_get()) {
			continue ;
		}

		do_proc_join (&stored_msg->group_name, stored_msg->pid, stored_msg->sender_nodeid,
			CONFCHG_CPG_REASON_NODEUP);
	}

	joinlist_remove_zombie_pi_entries ();
}

static void downlist_messages_delete (void)
{
	struct downlist_msg *stored_msg;
	struct list_head *iter, *iter_next;

	for (iter = downlist_messages_head.next;
		iter != &downlist_messages_head;
		iter = iter_next) {

		iter_next = iter->next;

		stored_msg = list_entry(iter, struct downlist_msg, list);
		list_del (&stored_msg->list);
		free (stored_msg);
	}
}

static void joinlist_messages_delete (void)
{
	struct joinlist_msg *stored_msg;
	struct list_head *iter, *iter_next;

	for (iter = joinlist_messages_head.next;
		iter != &joinlist_messages_head;
		iter = iter_next) {

		iter_next = iter->next;

		stored_msg = list_entry(iter, struct joinlist_msg, list);
		list_del (&stored_msg->list);
		free (stored_msg);
	}
	list_init (&joinlist_messages_head);
}

static char *cpg_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	list_init (&downlist_messages_head);
	list_init (&joinlist_messages_head);
	api = corosync_api;
	return (NULL);
}

static void cpg_iteration_instance_finalize (struct cpg_iteration_instance *cpg_iteration_instance)
{
	struct list_head *iter, *iter_next;
	struct process_info *pi;

	for (iter = cpg_iteration_instance->items_list_head.next;
		iter != &cpg_iteration_instance->items_list_head;
		iter = iter_next) {

		iter_next = iter->next;

		pi = list_entry (iter, struct process_info, list);
		list_del (&pi->list);
		free (pi);
	}

	list_del (&cpg_iteration_instance->list);
	hdb_handle_destroy (&cpg_iteration_handle_t_db, cpg_iteration_instance->handle);
}

static void cpg_pd_finalize (struct cpg_pd *cpd)
{
	struct list_head *iter, *iter_next;
	struct cpg_iteration_instance *cpii;

	zcb_all_free(cpd);
	for (iter = cpd->iteration_instance_list_head.next;
		iter != &cpd->iteration_instance_list_head;
		iter = iter_next) {

		iter_next = iter->next;

		cpii = list_entry (iter, struct cpg_iteration_instance, list);

		cpg_iteration_instance_finalize (cpii);
	}

	list_del (&cpd->list);
}

static int cpg_lib_exit_fn (void *conn)
{
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "exit_fn for conn=%p", conn);

	if (cpd->group_name.length > 0 && cpd->cpd_state != CPD_STATE_LEAVE_STARTED) {
		cpg_node_joinleave_send (cpd->pid, &cpd->group_name,
				MESSAGE_REQ_EXEC_CPG_PROCLEAVE, CONFCHG_CPG_REASON_PROCDOWN);
	}

	cpg_pd_finalize (cpd);

	api->ipc_refcnt_dec (conn);
	return (0);
}

static int cpg_node_joinleave_send (unsigned int pid, const mar_cpg_name_t *group_name, int fn, int reason)
{
	struct req_exec_cpg_procjoin req_exec_cpg_procjoin;
	struct iovec req_exec_cpg_iovec;
	int result;

	memcpy(&req_exec_cpg_procjoin.group_name, group_name, sizeof(mar_cpg_name_t));
	req_exec_cpg_procjoin.pid = pid;
	req_exec_cpg_procjoin.reason = reason;

	req_exec_cpg_procjoin.header.size = sizeof(req_exec_cpg_procjoin);
	req_exec_cpg_procjoin.header.id = SERVICE_ID_MAKE(CPG_SERVICE, fn);

	req_exec_cpg_iovec.iov_base = (char *)&req_exec_cpg_procjoin;
	req_exec_cpg_iovec.iov_len = sizeof(req_exec_cpg_procjoin);

	result = api->totem_mcast (&req_exec_cpg_iovec, 1, TOTEM_AGREED);

	return (result);
}

/* Can byteswap join & leave messages */
static void exec_cpg_procjoin_endian_convert (void *msg)
{
	struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = msg;

	req_exec_cpg_procjoin->pid = swab32(req_exec_cpg_procjoin->pid);
	swab_mar_cpg_name_t (&req_exec_cpg_procjoin->group_name);
	req_exec_cpg_procjoin->reason = swab32(req_exec_cpg_procjoin->reason);
}

static void exec_cpg_joinlist_endian_convert (void *msg_v)
{
	char *msg = msg_v;
	struct qb_ipc_response_header *res = (struct qb_ipc_response_header *)msg;
	struct join_list_entry *jle = (struct join_list_entry *)(msg + sizeof(struct qb_ipc_response_header));

	swab_mar_int32_t (&res->size);

	while ((const char*)jle < msg + res->size) {
		jle->pid = swab32(jle->pid);
		swab_mar_cpg_name_t (&jle->group_name);
		jle++;
	}
}

static void exec_cpg_downlist_endian_convert_old (void *msg)
{
}

static void exec_cpg_downlist_endian_convert (void *msg)
{
	struct req_exec_cpg_downlist *req_exec_cpg_downlist = msg;
	unsigned int i;

	req_exec_cpg_downlist->left_nodes = swab32(req_exec_cpg_downlist->left_nodes);
	req_exec_cpg_downlist->old_members = swab32(req_exec_cpg_downlist->old_members);

	for (i = 0; i < req_exec_cpg_downlist->left_nodes; i++) {
		req_exec_cpg_downlist->nodeids[i] = swab32(req_exec_cpg_downlist->nodeids[i]);
	}
}


static void exec_cpg_mcast_endian_convert (void *msg)
{
	struct req_exec_cpg_mcast *req_exec_cpg_mcast = msg;

	swab_coroipc_request_header_t (&req_exec_cpg_mcast->header);
	swab_mar_cpg_name_t (&req_exec_cpg_mcast->group_name);
	req_exec_cpg_mcast->pid = swab32(req_exec_cpg_mcast->pid);
	req_exec_cpg_mcast->msglen = swab32(req_exec_cpg_mcast->msglen);
	swab_mar_message_source_t (&req_exec_cpg_mcast->source);
}

static void exec_cpg_partial_mcast_endian_convert (void *msg)
{
	struct req_exec_cpg_partial_mcast *req_exec_cpg_mcast = msg;

	swab_coroipc_request_header_t (&req_exec_cpg_mcast->header);
	swab_mar_cpg_name_t (&req_exec_cpg_mcast->group_name);
	req_exec_cpg_mcast->pid = swab32(req_exec_cpg_mcast->pid);
	req_exec_cpg_mcast->msglen = swab32(req_exec_cpg_mcast->msglen);
	req_exec_cpg_mcast->fraglen = swab32(req_exec_cpg_mcast->fraglen);
	req_exec_cpg_mcast->type = swab32(req_exec_cpg_mcast->type);
	swab_mar_message_source_t (&req_exec_cpg_mcast->source);
}

static struct process_info *process_info_find(const mar_cpg_name_t *group_name, uint32_t pid, unsigned int nodeid) {
	struct list_head *iter;

	for (iter = process_info_list_head.next; iter != &process_info_list_head; ) {
		struct process_info *pi = list_entry (iter, struct process_info, list);
		iter = iter->next;

		if (pi->pid == pid && pi->nodeid == nodeid &&
			mar_name_compare (&pi->group, group_name) == 0) {
				return pi;
		}
	}

	return NULL;
}

static void do_proc_join(
	const mar_cpg_name_t *name,
	uint32_t pid,
	unsigned int nodeid,
	int reason)
{
	struct process_info *pi;
	struct process_info *pi_entry;
	mar_cpg_address_t notify_info;
	struct list_head *list;
	struct list_head *list_to_add = NULL;

	if (process_info_find (name, pid, nodeid) != NULL) {
		return ;
 	}
	pi = malloc (sizeof (struct process_info));
	if (!pi) {
		log_printf(LOGSYS_LEVEL_WARNING, "Unable to allocate process_info struct");
		return;
	}
	pi->nodeid = nodeid;
	pi->pid = pid;
	memcpy(&pi->group, name, sizeof(*name));
	list_init(&pi->list);

	/*
	 * Insert new process in sorted order so synchronization works properly
	 */
	list_to_add = &process_info_list_head;
	for (list = process_info_list_head.next; list != &process_info_list_head; list = list->next) {

		pi_entry = list_entry(list, struct process_info, list);
		if (pi_entry->nodeid > pi->nodeid ||
			(pi_entry->nodeid == pi->nodeid && pi_entry->pid > pi->pid)) {

			break;
		}
		list_to_add = list;
	}
	list_add (&pi->list, list_to_add);

	notify_info.pid = pi->pid;
	notify_info.nodeid = nodeid;
	notify_info.reason = reason;

	notify_lib_joinlist(&pi->group, NULL,
			    1, &notify_info,
			    0, NULL,
			    MESSAGE_RES_CPG_CONFCHG_CALLBACK);
}

static void do_proc_leave(
	const mar_cpg_name_t *name,
	uint32_t pid,
	unsigned int nodeid,
	int reason)
{
	struct process_info *pi;
	struct list_head *iter;
	mar_cpg_address_t notify_info;

	notify_info.pid = pid;
	notify_info.nodeid = nodeid;
	notify_info.reason = reason;

	notify_lib_joinlist(name, NULL,
		0, NULL,
		1, &notify_info,
		MESSAGE_RES_CPG_CONFCHG_CALLBACK);

	for (iter = process_info_list_head.next; iter != &process_info_list_head; ) {
		pi = list_entry(iter, struct process_info, list);
		iter = iter->next;

		if (pi->pid == pid && pi->nodeid == nodeid &&
			mar_name_compare (&pi->group, name)==0) {
			list_del (&pi->list);
			free (pi);
		}
	}
}

static void message_handler_req_exec_cpg_downlist_old (
	const void *message,
	unsigned int nodeid)
{
	log_printf (LOGSYS_LEVEL_WARNING, "downlist OLD from node 0x%x",
		nodeid);
}

static void message_handler_req_exec_cpg_downlist(
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_cpg_downlist *req_exec_cpg_downlist = message;
	int i;
	struct list_head *iter;
	struct downlist_msg *stored_msg;
	int found;

	if (downlist_state != CPG_DOWNLIST_WAITING_FOR_MESSAGES) {
		log_printf (LOGSYS_LEVEL_WARNING, "downlist left_list: %d received in state %d",
			req_exec_cpg_downlist->left_nodes, downlist_state);
		return;
	}

	stored_msg = malloc (sizeof (struct downlist_msg));
	stored_msg->sender_nodeid = nodeid;
	stored_msg->old_members = req_exec_cpg_downlist->old_members;
	stored_msg->left_nodes = req_exec_cpg_downlist->left_nodes;
	memcpy (stored_msg->nodeids, req_exec_cpg_downlist->nodeids,
		req_exec_cpg_downlist->left_nodes * sizeof (mar_uint32_t));
	list_init (&stored_msg->list);
	list_add (&stored_msg->list, &downlist_messages_head);

	for (i = 0; i < my_member_list_entries; i++) {
		found = 0;
		for (iter = downlist_messages_head.next;
			iter != &downlist_messages_head;
			iter = iter->next) {

			stored_msg = list_entry(iter, struct downlist_msg, list);
			if (my_member_list[i] == stored_msg->sender_nodeid) {
				found = 1;
			}
		}
		if (!found) {
			return;
		}
	}

	downlist_master_choose_and_send ();
}


static void message_handler_req_exec_cpg_procjoin (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = message;

	log_printf(LOGSYS_LEVEL_DEBUG, "got procjoin message from cluster node 0x%x (%s) for pid %u",
		nodeid,
		api->totem_ifaces_print(nodeid),
		(unsigned int)req_exec_cpg_procjoin->pid);

	do_proc_join (&req_exec_cpg_procjoin->group_name,
		req_exec_cpg_procjoin->pid, nodeid,
		CONFCHG_CPG_REASON_JOIN);
}

static void message_handler_req_exec_cpg_procleave (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = message;

	log_printf(LOGSYS_LEVEL_DEBUG, "got procleave message from cluster node 0x%x (%s) for pid %u",
		nodeid,
		api->totem_ifaces_print(nodeid),
		(unsigned int)req_exec_cpg_procjoin->pid);

	do_proc_leave (&req_exec_cpg_procjoin->group_name,
		req_exec_cpg_procjoin->pid, nodeid,
		req_exec_cpg_procjoin->reason);
}


/* Got a proclist from another node */
static void message_handler_req_exec_cpg_joinlist (
	const void *message_v,
	unsigned int nodeid)
{
	const char *message = message_v;
	const struct qb_ipc_response_header *res = (const struct qb_ipc_response_header *)message;
	const struct join_list_entry *jle = (const struct join_list_entry *)(message + sizeof(struct qb_ipc_response_header));
	struct joinlist_msg *stored_msg;

	log_printf(LOGSYS_LEVEL_DEBUG, "got joinlist message from node 0x%x",
		nodeid);

	while ((const char*)jle < message + res->size) {
		stored_msg = malloc (sizeof (struct joinlist_msg));
		memset(stored_msg, 0, sizeof (struct joinlist_msg));
		stored_msg->sender_nodeid = nodeid;
		stored_msg->pid = jle->pid;
		memcpy(&stored_msg->group_name, &jle->group_name, sizeof(mar_cpg_name_t));
		list_init (&stored_msg->list);
		list_add (&stored_msg->list, &joinlist_messages_head);
		jle++;
	}
}

static void message_handler_req_exec_cpg_mcast (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_cpg_mcast *req_exec_cpg_mcast = message;
	struct res_lib_cpg_deliver_callback res_lib_cpg_mcast;
	int msglen = req_exec_cpg_mcast->msglen;
	struct list_head *iter, *pi_iter;
	struct cpg_pd *cpd;
	struct iovec iovec[2];
	int known_node = 0;

	res_lib_cpg_mcast.header.id = MESSAGE_RES_CPG_DELIVER_CALLBACK;
	res_lib_cpg_mcast.header.size = sizeof(res_lib_cpg_mcast) + msglen;
	res_lib_cpg_mcast.msglen = msglen;
	res_lib_cpg_mcast.pid = req_exec_cpg_mcast->pid;
	res_lib_cpg_mcast.nodeid = nodeid;

	memcpy(&res_lib_cpg_mcast.group_name, &req_exec_cpg_mcast->group_name,
		sizeof(mar_cpg_name_t));
	iovec[0].iov_base = (void *)&res_lib_cpg_mcast;
	iovec[0].iov_len = sizeof (res_lib_cpg_mcast);

	iovec[1].iov_base = (char*)message+sizeof(*req_exec_cpg_mcast);
	iovec[1].iov_len = msglen;

	for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; ) {
		cpd = list_entry(iter, struct cpg_pd, list);
		iter = iter->next;

		if ((cpd->cpd_state == CPD_STATE_LEAVE_STARTED || cpd->cpd_state == CPD_STATE_JOIN_COMPLETED)
			&& (mar_name_compare (&cpd->group_name, &req_exec_cpg_mcast->group_name) == 0)) {

			if (!known_node) {
				/* Try to find, if we know the node */
				for (pi_iter = process_info_list_head.next;
					pi_iter != &process_info_list_head; pi_iter = pi_iter->next) {

					struct process_info *pi = list_entry (pi_iter, struct process_info, list);

					if (pi->nodeid == nodeid &&
						mar_name_compare (&pi->group, &req_exec_cpg_mcast->group_name) == 0) {
						known_node = 1;
						break;
					}
				}
			}

			if (!known_node) {
				log_printf(LOGSYS_LEVEL_WARNING, "Unknown node -> we will not deliver message");
				return ;
			}

			api->ipc_dispatch_iov_send (cpd->conn, iovec, 2);
		}
	}
}

static void message_handler_req_exec_cpg_partial_mcast (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_cpg_partial_mcast *req_exec_cpg_mcast = message;
	struct res_lib_cpg_partial_deliver_callback res_lib_cpg_mcast;
	int msglen = req_exec_cpg_mcast->fraglen;
	struct list_head *iter, *pi_iter;
	struct cpg_pd *cpd;
	struct iovec iovec[2];
	int known_node = 0;

	log_printf(LOGSYS_LEVEL_DEBUG, "Got fragmented message from node %d, size = %d bytes\n", nodeid, msglen);

	res_lib_cpg_mcast.header.id = MESSAGE_RES_CPG_PARTIAL_DELIVER_CALLBACK;
	res_lib_cpg_mcast.header.size = sizeof(res_lib_cpg_mcast) + msglen;
	res_lib_cpg_mcast.fraglen = msglen;
	res_lib_cpg_mcast.msglen = req_exec_cpg_mcast->msglen;
	res_lib_cpg_mcast.pid = req_exec_cpg_mcast->pid;
	res_lib_cpg_mcast.type = req_exec_cpg_mcast->type;
	res_lib_cpg_mcast.nodeid = nodeid;

	memcpy(&res_lib_cpg_mcast.group_name, &req_exec_cpg_mcast->group_name,
	       sizeof(mar_cpg_name_t));
	iovec[0].iov_base = (void *)&res_lib_cpg_mcast;
	iovec[0].iov_len = sizeof (res_lib_cpg_mcast);

	iovec[1].iov_base = (char*)message+sizeof(*req_exec_cpg_mcast);
	iovec[1].iov_len = msglen;

	for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; ) {
		cpd = list_entry(iter, struct cpg_pd, list);
		iter = iter->next;

		if ((cpd->cpd_state == CPD_STATE_LEAVE_STARTED || cpd->cpd_state == CPD_STATE_JOIN_COMPLETED)
		    && (mar_name_compare (&cpd->group_name, &req_exec_cpg_mcast->group_name) == 0)) {

			if (!known_node) {
				/* Try to find, if we know the node */
				for (pi_iter = process_info_list_head.next;
				     pi_iter != &process_info_list_head; pi_iter = pi_iter->next) {

					struct process_info *pi = list_entry (pi_iter, struct process_info, list);

					if (pi->nodeid == nodeid &&
					    mar_name_compare (&pi->group, &req_exec_cpg_mcast->group_name) == 0) {
						known_node = 1;
						break;
					}
				}
			}

			if (!known_node) {
				log_printf(LOGSYS_LEVEL_WARNING, "Unknown node -> we will not deliver message");
				return ;
			}

			api->ipc_dispatch_iov_send (cpd->conn, iovec, 2);
		}
	}
}


static int cpg_exec_send_downlist(void)
{
	struct iovec iov;

	g_req_exec_cpg_downlist.header.id = SERVICE_ID_MAKE(CPG_SERVICE, MESSAGE_REQ_EXEC_CPG_DOWNLIST);
	g_req_exec_cpg_downlist.header.size = sizeof(struct req_exec_cpg_downlist);

	g_req_exec_cpg_downlist.old_members = my_old_member_list_entries;

	iov.iov_base = (void *)&g_req_exec_cpg_downlist;
	iov.iov_len = g_req_exec_cpg_downlist.header.size;

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int cpg_exec_send_joinlist(void)
{
	int count = 0;
	struct list_head *iter;
	struct qb_ipc_response_header *res;
 	char *buf;
	struct join_list_entry *jle;
	struct iovec req_exec_cpg_iovec;

 	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
 		struct process_info *pi = list_entry (iter, struct process_info, list);

 		if (pi->nodeid == api->totem_nodeid_get ()) {
 			count++;
		}
	}

	/* Nothing to send */
	if (!count)
		return 0;

	buf = alloca(sizeof(struct qb_ipc_response_header) + sizeof(struct join_list_entry) * count);
	if (!buf) {
		log_printf(LOGSYS_LEVEL_WARNING, "Unable to allocate joinlist buffer");
		return -1;
	}

	jle = (struct join_list_entry *)(buf + sizeof(struct qb_ipc_response_header));
	res = (struct qb_ipc_response_header *)buf;

 	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
 		struct process_info *pi = list_entry (iter, struct process_info, list);

		if (pi->nodeid == api->totem_nodeid_get ()) {
			memcpy (&jle->group_name, &pi->group, sizeof (mar_cpg_name_t));
			jle->pid = pi->pid;
			jle++;
		}
	}

	res->id = SERVICE_ID_MAKE(CPG_SERVICE, MESSAGE_REQ_EXEC_CPG_JOINLIST);
	res->size = sizeof(struct qb_ipc_response_header)+sizeof(struct join_list_entry) * count;

	req_exec_cpg_iovec.iov_base = buf;
	req_exec_cpg_iovec.iov_len = res->size;

	return (api->totem_mcast (&req_exec_cpg_iovec, 1, TOTEM_AGREED));
}

static int cpg_lib_init_fn (void *conn)
{
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);
	memset (cpd, 0, sizeof(struct cpg_pd));
	cpd->conn = conn;
	list_add (&cpd->list, &cpg_pd_list_head);

	list_init (&cpd->iteration_instance_list_head);
	list_init (&cpd->zcb_mapped_list_head);

	api->ipc_refcnt_inc (conn);
	log_printf(LOGSYS_LEVEL_DEBUG, "lib_init_fn: conn=%p, cpd=%p", conn, cpd);
	return (0);
}

/* Join message from the library */
static void message_handler_req_lib_cpg_join (void *conn, const void *message)
{
	const struct req_lib_cpg_join *req_lib_cpg_join = message;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);
	struct res_lib_cpg_join res_lib_cpg_join;
	cs_error_t error = CS_OK;
	struct list_head *iter;

	/* Test, if we don't have same pid and group name joined */
	for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; iter = iter->next) {
		struct cpg_pd *cpd_item = list_entry (iter, struct cpg_pd, list);

		if (cpd_item->pid == req_lib_cpg_join->pid &&
			mar_name_compare(&req_lib_cpg_join->group_name, &cpd_item->group_name) == 0) {

			/* We have same pid and group name joined -> return error */
			error = CS_ERR_EXIST;
			goto response_send;
		}
	}

	/*
	 * Same check must be done in process info list, because there may be not yet delivered
	 * leave of client.
	 */
	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi = list_entry (iter, struct process_info, list);

		if (pi->nodeid == api->totem_nodeid_get () && pi->pid == req_lib_cpg_join->pid &&
		    mar_name_compare(&req_lib_cpg_join->group_name, &pi->group) == 0) {
			/* We have same pid and group name joined -> return error */
			error = CS_ERR_TRY_AGAIN;
			goto response_send;
		}
	}

	if (req_lib_cpg_join->group_name.length > CPG_MAX_NAME_LENGTH) {
		error = CS_ERR_NAME_TOO_LONG;
		goto response_send;
	}

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CS_OK;
		cpd->cpd_state = CPD_STATE_JOIN_STARTED;
		cpd->pid = req_lib_cpg_join->pid;
		cpd->flags = req_lib_cpg_join->flags;
		memcpy (&cpd->group_name, &req_lib_cpg_join->group_name,
			sizeof (cpd->group_name));

		cpg_node_joinleave_send (req_lib_cpg_join->pid,
			&req_lib_cpg_join->group_name,
			MESSAGE_REQ_EXEC_CPG_PROCJOIN, CONFCHG_CPG_REASON_JOIN);
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CS_ERR_BUSY;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CS_ERR_EXIST;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CS_ERR_EXIST;
		break;
	}

response_send:
	res_lib_cpg_join.header.size = sizeof(res_lib_cpg_join);
        res_lib_cpg_join.header.id = MESSAGE_RES_CPG_JOIN;
        res_lib_cpg_join.header.error = error;
        api->ipc_response_send (conn, &res_lib_cpg_join, sizeof(res_lib_cpg_join));
}

/* Leave message from the library */
static void message_handler_req_lib_cpg_leave (void *conn, const void *message)
{
	struct res_lib_cpg_leave res_lib_cpg_leave;
	cs_error_t error = CS_OK;
	struct req_lib_cpg_leave  *req_lib_cpg_leave = (struct req_lib_cpg_leave *)message;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "got leave request on %p", conn);

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CS_ERR_BUSY;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CS_OK;
		cpd->cpd_state = CPD_STATE_LEAVE_STARTED;
		cpg_node_joinleave_send (req_lib_cpg_leave->pid,
			&req_lib_cpg_leave->group_name,
			MESSAGE_REQ_EXEC_CPG_PROCLEAVE,
			CONFCHG_CPG_REASON_LEAVE);
		break;
	}

	/* send return */
	res_lib_cpg_leave.header.size = sizeof(res_lib_cpg_leave);
	res_lib_cpg_leave.header.id = MESSAGE_RES_CPG_LEAVE;
	res_lib_cpg_leave.header.error = error;
	api->ipc_response_send(conn, &res_lib_cpg_leave, sizeof(res_lib_cpg_leave));
}

/* Finalize message from library */
static void message_handler_req_lib_cpg_finalize (
	void *conn,
	const void *message)
{
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);
	struct res_lib_cpg_finalize res_lib_cpg_finalize;
	cs_error_t error = CS_OK;

	log_printf (LOGSYS_LEVEL_DEBUG, "cpg finalize for conn=%p", conn);

	/*
	 * We will just remove cpd from list. After this call, connection will be
	 * closed on lib side, and cpg_lib_exit_fn will be called
	 */
	list_del (&cpd->list);
	list_init (&cpd->list);

	res_lib_cpg_finalize.header.size = sizeof (res_lib_cpg_finalize);
	res_lib_cpg_finalize.header.id = MESSAGE_RES_CPG_FINALIZE;
	res_lib_cpg_finalize.header.error = error;

	api->ipc_response_send (conn, &res_lib_cpg_finalize,
		sizeof (res_lib_cpg_finalize));
}

static int
memory_map (
	const char *path,
	size_t bytes,
	void **buf)
{
	int32_t fd;
	void *addr;
	int32_t res;

	fd = open (path, O_RDWR, 0600);

	unlink (path);

	if (fd == -1) {
		return (-1);
	}

	res = ftruncate (fd, bytes);
	if (res == -1) {
		goto error_close_unlink;
	}

	addr = mmap (NULL, bytes, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);

	if (addr == MAP_FAILED) {
		goto error_close_unlink;
	}
#ifdef MADV_NOSYNC
	madvise(addr, bytes, MADV_NOSYNC);
#endif

	res = close (fd);
	if (res) {
		munmap (addr, bytes);
		return (-1);
	}
	*buf = addr;
	return (0);

error_close_unlink:
	close (fd);
	unlink(path);
	return -1;
}

static inline int zcb_alloc (
	struct cpg_pd *cpd,
	const char *path_to_file,
	size_t size,
	void **addr)
{
	struct zcb_mapped *zcb_mapped;
	unsigned int res;

	zcb_mapped = malloc (sizeof (struct zcb_mapped));
	if (zcb_mapped == NULL) {
		return (-1);
	}

	res = memory_map (
		path_to_file,
		size,
		addr);
	if (res == -1) {
		free (zcb_mapped);
		return (-1);
	}

	list_init (&zcb_mapped->list);
	zcb_mapped->addr = *addr;
	zcb_mapped->size = size;
	list_add_tail (&zcb_mapped->list, &cpd->zcb_mapped_list_head);
	return (0);
}


static inline int zcb_free (struct zcb_mapped *zcb_mapped)
{
	unsigned int res;

	res = munmap (zcb_mapped->addr, zcb_mapped->size);
	list_del (&zcb_mapped->list);
	free (zcb_mapped);
	return (res);
}

static inline int zcb_by_addr_free (struct cpg_pd *cpd, void *addr)
{
	struct list_head *list;
	struct zcb_mapped *zcb_mapped;
	unsigned int res = 0;

	for (list = cpd->zcb_mapped_list_head.next;
		list != &cpd->zcb_mapped_list_head; list = list->next) {

		zcb_mapped = list_entry (list, struct zcb_mapped, list);

		if (zcb_mapped->addr == addr) {
			res = zcb_free (zcb_mapped);
			break;
		}

	}
	return (res);
}

static inline int zcb_all_free (
	struct cpg_pd *cpd)
{
	struct list_head *list;
	struct zcb_mapped *zcb_mapped;

	for (list = cpd->zcb_mapped_list_head.next;
		list != &cpd->zcb_mapped_list_head;) {

		zcb_mapped = list_entry (list, struct zcb_mapped, list);

		list = list->next;

		zcb_free (zcb_mapped);
	}
	return (0);
}

union u {
	uint64_t server_addr;
	void *server_ptr;
};

static uint64_t void2serveraddr (void *server_ptr)
{
	union u u;

	u.server_ptr = server_ptr;
	return (u.server_addr);
}

static void *serveraddr2void (uint64_t server_addr)
{
	union u u;

	u.server_addr = server_addr;
	return (u.server_ptr);
};

static void message_handler_req_lib_cpg_zc_alloc (
	void *conn,
	const void *message)
{
	mar_req_coroipcc_zc_alloc_t *hdr = (mar_req_coroipcc_zc_alloc_t *)message;
	struct qb_ipc_response_header res_header;
	void *addr = NULL;
	struct coroipcs_zc_header *zc_header;
	unsigned int res;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "path: %s", hdr->path_to_file);

	res = zcb_alloc (cpd, hdr->path_to_file, hdr->map_size,
		&addr);
	assert(res == 0);

	zc_header = (struct coroipcs_zc_header *)addr;
	zc_header->server_address = void2serveraddr(addr);

	res_header.size = sizeof (struct qb_ipc_response_header);
	res_header.id = 0;
	api->ipc_response_send (conn,
		&res_header,
		res_header.size);
}

static void message_handler_req_lib_cpg_zc_free (
	void *conn,
	const void *message)
{
	mar_req_coroipcc_zc_free_t *hdr = (mar_req_coroipcc_zc_free_t *)message;
	struct qb_ipc_response_header res_header;
	void *addr = NULL;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, " free'ing");

	addr = serveraddr2void (hdr->server_address);

	zcb_by_addr_free (cpd, addr);

	res_header.size = sizeof (struct qb_ipc_response_header);
	res_header.id = 0;
	api->ipc_response_send (
		conn, &res_header,
		res_header.size);
}

/* Fragmented mcast message from the library */
static void message_handler_req_lib_cpg_partial_mcast (void *conn, const void *message)
{
	const struct req_lib_cpg_partial_mcast *req_lib_cpg_mcast = message;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);
	mar_cpg_name_t group_name = cpd->group_name;

	struct iovec req_exec_cpg_iovec[2];
	struct req_exec_cpg_partial_mcast req_exec_cpg_mcast;
	struct res_lib_cpg_partial_send res_lib_cpg_partial_send;
	int msglen = req_lib_cpg_mcast->fraglen;
	int result;
	cs_error_t error = CS_ERR_NOT_EXIST;

	log_printf(LOGSYS_LEVEL_TRACE, "got fragmented mcast request on %p", conn);
	log_printf(LOGSYS_LEVEL_DEBUG, "Sending fragmented message size = %d bytes\n", msglen);

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CS_OK;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CS_OK;
		break;
	}

	res_lib_cpg_partial_send.header.size = sizeof(res_lib_cpg_partial_send);
	res_lib_cpg_partial_send.header.id = MESSAGE_RES_CPG_PARTIAL_SEND;

	if (req_lib_cpg_mcast->type == LIBCPG_PARTIAL_FIRST) {
		cpd->initial_transition_counter = cpd->transition_counter;
	}
	if (cpd->transition_counter != cpd->initial_transition_counter) {
		error = CS_ERR_INTERRUPT;
	}

	if (error == CS_OK) {
		req_exec_cpg_mcast.header.size = sizeof(req_exec_cpg_mcast) + msglen;
		req_exec_cpg_mcast.header.id = SERVICE_ID_MAKE(CPG_SERVICE,
							       MESSAGE_REQ_EXEC_CPG_PARTIAL_MCAST);
		req_exec_cpg_mcast.pid = cpd->pid;
		req_exec_cpg_mcast.msglen = req_lib_cpg_mcast->msglen;
		req_exec_cpg_mcast.type = req_lib_cpg_mcast->type;
		req_exec_cpg_mcast.fraglen = req_lib_cpg_mcast->fraglen;
		api->ipc_source_set (&req_exec_cpg_mcast.source, conn);
		memcpy(&req_exec_cpg_mcast.group_name, &group_name,
		       sizeof(mar_cpg_name_t));

		req_exec_cpg_iovec[0].iov_base = (char *)&req_exec_cpg_mcast;
		req_exec_cpg_iovec[0].iov_len = sizeof(req_exec_cpg_mcast);
		req_exec_cpg_iovec[1].iov_base = (char *)&req_lib_cpg_mcast->message;
		req_exec_cpg_iovec[1].iov_len = msglen;

		result = api->totem_mcast (req_exec_cpg_iovec, 2, TOTEM_AGREED);
		assert(result == 0);
	} else {
		log_printf(LOGSYS_LEVEL_ERROR, "*** %p can't mcast to group %s state:%d, error:%d",
			   conn, group_name.value, cpd->cpd_state, error);
	}

	res_lib_cpg_partial_send.header.error = error;
	api->ipc_response_send (conn, &res_lib_cpg_partial_send,
				sizeof (res_lib_cpg_partial_send));
}

/* Mcast message from the library */
static void message_handler_req_lib_cpg_mcast (void *conn, const void *message)
{
	const struct req_lib_cpg_mcast *req_lib_cpg_mcast = message;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);
	mar_cpg_name_t group_name = cpd->group_name;

	struct iovec req_exec_cpg_iovec[2];
	struct req_exec_cpg_mcast req_exec_cpg_mcast;
	int msglen = req_lib_cpg_mcast->msglen;
	int result;
	cs_error_t error = CS_ERR_NOT_EXIST;

	log_printf(LOGSYS_LEVEL_TRACE, "got mcast request on %p", conn);

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CS_OK;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CS_OK;
		break;
	}

	if (error == CS_OK) {
		req_exec_cpg_mcast.header.size = sizeof(req_exec_cpg_mcast) + msglen;
		req_exec_cpg_mcast.header.id = SERVICE_ID_MAKE(CPG_SERVICE,
			MESSAGE_REQ_EXEC_CPG_MCAST);
		req_exec_cpg_mcast.pid = cpd->pid;
		req_exec_cpg_mcast.msglen = msglen;
		api->ipc_source_set (&req_exec_cpg_mcast.source, conn);
		memcpy(&req_exec_cpg_mcast.group_name, &group_name,
			sizeof(mar_cpg_name_t));

		req_exec_cpg_iovec[0].iov_base = (char *)&req_exec_cpg_mcast;
		req_exec_cpg_iovec[0].iov_len = sizeof(req_exec_cpg_mcast);
		req_exec_cpg_iovec[1].iov_base = (char *)&req_lib_cpg_mcast->message;
		req_exec_cpg_iovec[1].iov_len = msglen;

		result = api->totem_mcast (req_exec_cpg_iovec, 2, TOTEM_AGREED);
		assert(result == 0);
	} else {
		log_printf(LOGSYS_LEVEL_ERROR, "*** %p can't mcast to group %s state:%d, error:%d",
			conn, group_name.value, cpd->cpd_state, error);
	}
}

static void message_handler_req_lib_cpg_zc_execute (
	void *conn,
	const void *message)
{
	mar_req_coroipcc_zc_execute_t *hdr = (mar_req_coroipcc_zc_execute_t *)message;
	struct qb_ipc_request_header *header;
	struct res_lib_cpg_mcast res_lib_cpg_mcast;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);
	struct iovec req_exec_cpg_iovec[2];
	struct req_exec_cpg_mcast req_exec_cpg_mcast;
	struct req_lib_cpg_mcast *req_lib_cpg_mcast;
	int result;
	cs_error_t error = CS_ERR_NOT_EXIST;

	log_printf(LOGSYS_LEVEL_TRACE, "got ZC mcast request on %p", conn);

	header = (struct qb_ipc_request_header *)(((char *)serveraddr2void(hdr->server_address) + sizeof (struct coroipcs_zc_header)));
	req_lib_cpg_mcast = (struct req_lib_cpg_mcast *)header;

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CS_ERR_NOT_EXIST;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CS_OK;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CS_OK;
		break;
	}

	res_lib_cpg_mcast.header.size = sizeof(res_lib_cpg_mcast);
	res_lib_cpg_mcast.header.id = MESSAGE_RES_CPG_MCAST;
	if (error == CS_OK) {
		req_exec_cpg_mcast.header.size = sizeof(req_exec_cpg_mcast) + req_lib_cpg_mcast->msglen;
		req_exec_cpg_mcast.header.id = SERVICE_ID_MAKE(CPG_SERVICE,
			MESSAGE_REQ_EXEC_CPG_MCAST);
		req_exec_cpg_mcast.pid = cpd->pid;
		req_exec_cpg_mcast.msglen = req_lib_cpg_mcast->msglen;
		api->ipc_source_set (&req_exec_cpg_mcast.source, conn);
		memcpy(&req_exec_cpg_mcast.group_name, &cpd->group_name,
			sizeof(mar_cpg_name_t));

		req_exec_cpg_iovec[0].iov_base = (char *)&req_exec_cpg_mcast;
		req_exec_cpg_iovec[0].iov_len = sizeof(req_exec_cpg_mcast);
		req_exec_cpg_iovec[1].iov_base = (char *)header + sizeof(struct req_lib_cpg_mcast);
		req_exec_cpg_iovec[1].iov_len = req_exec_cpg_mcast.msglen;

		result = api->totem_mcast (req_exec_cpg_iovec, 2, TOTEM_AGREED);
		if (result == 0) {
			res_lib_cpg_mcast.header.error = CS_OK;
		} else {
			res_lib_cpg_mcast.header.error = CS_ERR_TRY_AGAIN;
		}
	} else {
		res_lib_cpg_mcast.header.error = error;
	}

	api->ipc_response_send (conn, &res_lib_cpg_mcast,
		sizeof (res_lib_cpg_mcast));

}

static void message_handler_req_lib_cpg_membership (void *conn,
						    const void *message)
{
	struct req_lib_cpg_membership_get *req_lib_cpg_membership_get =
		(struct req_lib_cpg_membership_get *)message;
	struct res_lib_cpg_membership_get res_lib_cpg_membership_get;
	struct list_head *iter;
	int member_count = 0;

	res_lib_cpg_membership_get.header.id = MESSAGE_RES_CPG_MEMBERSHIP;
	res_lib_cpg_membership_get.header.error = CS_OK;
	res_lib_cpg_membership_get.header.size =
		sizeof (struct res_lib_cpg_membership_get);

	for (iter = process_info_list_head.next;
		iter != &process_info_list_head; iter = iter->next) {

		struct process_info *pi = list_entry (iter, struct process_info, list);
		if (mar_name_compare (&pi->group, &req_lib_cpg_membership_get->group_name) == 0) {
			res_lib_cpg_membership_get.member_list[member_count].nodeid = pi->nodeid;
			res_lib_cpg_membership_get.member_list[member_count].pid = pi->pid;
			member_count += 1;
		}
	}
	res_lib_cpg_membership_get.member_count = member_count;

	api->ipc_response_send (conn, &res_lib_cpg_membership_get,
		sizeof (res_lib_cpg_membership_get));
}

static void message_handler_req_lib_cpg_local_get (void *conn,
						   const void *message)
{
	struct res_lib_cpg_local_get res_lib_cpg_local_get;

	res_lib_cpg_local_get.header.size = sizeof (res_lib_cpg_local_get);
	res_lib_cpg_local_get.header.id = MESSAGE_RES_CPG_LOCAL_GET;
	res_lib_cpg_local_get.header.error = CS_OK;
	res_lib_cpg_local_get.local_nodeid = api->totem_nodeid_get ();

	api->ipc_response_send (conn, &res_lib_cpg_local_get,
		sizeof (res_lib_cpg_local_get));
}

static void message_handler_req_lib_cpg_iteration_initialize (
	void *conn,
	const void *message)
{
	const struct req_lib_cpg_iterationinitialize *req_lib_cpg_iterationinitialize = message;
	struct cpg_pd *cpd = (struct cpg_pd *)api->ipc_private_data_get (conn);
	hdb_handle_t cpg_iteration_handle = 0;
	struct res_lib_cpg_iterationinitialize res_lib_cpg_iterationinitialize;
	struct list_head *iter, *iter2;
	struct cpg_iteration_instance *cpg_iteration_instance;
	cs_error_t error = CS_OK;
	int res;

	log_printf (LOGSYS_LEVEL_DEBUG, "cpg iteration initialize");

	/* Because between calling this function and *next can be some operations which will
	 * change list, we must do full copy.
	 */

	/*
	 * Create new iteration instance
	 */
	res = hdb_handle_create (&cpg_iteration_handle_t_db, sizeof (struct cpg_iteration_instance),
			&cpg_iteration_handle);

	if (res != 0) {
		error = CS_ERR_NO_MEMORY;
		goto response_send;
	}

	res = hdb_handle_get (&cpg_iteration_handle_t_db, cpg_iteration_handle, (void *)&cpg_iteration_instance);

	if (res != 0) {
		error = CS_ERR_BAD_HANDLE;
		goto error_destroy;
	}

	list_init (&cpg_iteration_instance->items_list_head);
	cpg_iteration_instance->handle = cpg_iteration_handle;

	/*
	 * Create copy of process_info list "grouped by" group name
	 */
	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi = list_entry (iter, struct process_info, list);
		struct process_info *new_pi;

		if (req_lib_cpg_iterationinitialize->iteration_type == CPG_ITERATION_NAME_ONLY) {
			/*
			 * Try to find processed group name in our list new list
			 */
			int found = 0;

			for (iter2 = cpg_iteration_instance->items_list_head.next;
			     iter2 != &cpg_iteration_instance->items_list_head;
			     iter2 = iter2->next) {
				 struct process_info *pi2 = list_entry (iter2, struct process_info, list);

				 if (mar_name_compare (&pi2->group, &pi->group) == 0) {
					found = 1;
					break;
				 }
			}

			if (found) {
				/*
				 * We have this name in list -> don't add
				 */
				continue ;
			}
		} else if (req_lib_cpg_iterationinitialize->iteration_type == CPG_ITERATION_ONE_GROUP) {
			/*
			 * Test pi group name with request
			 */
			if (mar_name_compare (&pi->group, &req_lib_cpg_iterationinitialize->group_name) != 0)
				/*
				 * Not same -> don't add
				 */
				continue ;
		}

		new_pi = malloc (sizeof (struct process_info));
		if (!new_pi) {
			log_printf(LOGSYS_LEVEL_WARNING, "Unable to allocate process_info struct");

			error = CS_ERR_NO_MEMORY;

			goto error_put_destroy;
		}

		memcpy (new_pi, pi, sizeof (struct process_info));
		list_init (&new_pi->list);

		if (req_lib_cpg_iterationinitialize->iteration_type == CPG_ITERATION_NAME_ONLY) {
			/*
			 * pid and nodeid -> undefined
			 */
			new_pi->pid = new_pi->nodeid = 0;
		}

		/*
		 * We will return list "grouped" by "group name", so try to find right place to add
		 */
		for (iter2 = cpg_iteration_instance->items_list_head.next;
		     iter2 != &cpg_iteration_instance->items_list_head;
		     iter2 = iter2->next) {
			 struct process_info *pi2 = list_entry (iter2, struct process_info, list);

			 if (mar_name_compare (&pi2->group, &pi->group) == 0) {
				break;
			 }
		}

		list_add (&new_pi->list, iter2);
	}

	/*
	 * Now we have a full "grouped by" copy of process_info list
	 */

	/*
	 * Add instance to current cpd list
	 */
	list_init (&cpg_iteration_instance->list);
	list_add (&cpg_iteration_instance->list, &cpd->iteration_instance_list_head);

	cpg_iteration_instance->current_pointer = &cpg_iteration_instance->items_list_head;

error_put_destroy:
	hdb_handle_put (&cpg_iteration_handle_t_db, cpg_iteration_handle);
error_destroy:
	if (error != CS_OK) {
		hdb_handle_destroy (&cpg_iteration_handle_t_db, cpg_iteration_handle);
	}

response_send:
	res_lib_cpg_iterationinitialize.header.size = sizeof (res_lib_cpg_iterationinitialize);
	res_lib_cpg_iterationinitialize.header.id = MESSAGE_RES_CPG_ITERATIONINITIALIZE;
	res_lib_cpg_iterationinitialize.header.error = error;
	res_lib_cpg_iterationinitialize.iteration_handle = cpg_iteration_handle;

	api->ipc_response_send (conn, &res_lib_cpg_iterationinitialize,
		sizeof (res_lib_cpg_iterationinitialize));
}

static void message_handler_req_lib_cpg_iteration_next (
	void *conn,
	const void *message)
{
	const struct req_lib_cpg_iterationnext *req_lib_cpg_iterationnext = message;
	struct res_lib_cpg_iterationnext res_lib_cpg_iterationnext;
	struct cpg_iteration_instance *cpg_iteration_instance;
	cs_error_t error = CS_OK;
	int res;
	struct process_info *pi;

	log_printf (LOGSYS_LEVEL_DEBUG, "cpg iteration next");

	res = hdb_handle_get (&cpg_iteration_handle_t_db,
			req_lib_cpg_iterationnext->iteration_handle,
			(void *)&cpg_iteration_instance);

	if (res != 0) {
		error = CS_ERR_LIBRARY;
		goto error_exit;
	}

	assert (cpg_iteration_instance);

	cpg_iteration_instance->current_pointer = cpg_iteration_instance->current_pointer->next;

	if (cpg_iteration_instance->current_pointer == &cpg_iteration_instance->items_list_head) {
		error = CS_ERR_NO_SECTIONS;
		goto error_put;
	}

	pi = list_entry (cpg_iteration_instance->current_pointer, struct process_info, list);

	/*
	 * Copy iteration data
	 */
	res_lib_cpg_iterationnext.description.nodeid = pi->nodeid;
	res_lib_cpg_iterationnext.description.pid = pi->pid;
	memcpy (&res_lib_cpg_iterationnext.description.group,
			&pi->group,
			sizeof (mar_cpg_name_t));

error_put:
	hdb_handle_put (&cpg_iteration_handle_t_db, req_lib_cpg_iterationnext->iteration_handle);
error_exit:
	res_lib_cpg_iterationnext.header.size = sizeof (res_lib_cpg_iterationnext);
	res_lib_cpg_iterationnext.header.id = MESSAGE_RES_CPG_ITERATIONNEXT;
	res_lib_cpg_iterationnext.header.error = error;

	api->ipc_response_send (conn, &res_lib_cpg_iterationnext,
		sizeof (res_lib_cpg_iterationnext));
}

static void message_handler_req_lib_cpg_iteration_finalize (
	void *conn,
	const void *message)
{
	const struct req_lib_cpg_iterationfinalize *req_lib_cpg_iterationfinalize = message;
	struct res_lib_cpg_iterationfinalize res_lib_cpg_iterationfinalize;
	struct cpg_iteration_instance *cpg_iteration_instance;
	cs_error_t error = CS_OK;
	int res;

	log_printf (LOGSYS_LEVEL_DEBUG, "cpg iteration finalize");

	res = hdb_handle_get (&cpg_iteration_handle_t_db,
			req_lib_cpg_iterationfinalize->iteration_handle,
			(void *)&cpg_iteration_instance);

	if (res != 0) {
		error = CS_ERR_LIBRARY;
		goto error_exit;
	}

	assert (cpg_iteration_instance);

	cpg_iteration_instance_finalize (cpg_iteration_instance);
	hdb_handle_put (&cpg_iteration_handle_t_db, cpg_iteration_instance->handle);

error_exit:
	res_lib_cpg_iterationfinalize.header.size = sizeof (res_lib_cpg_iterationfinalize);
	res_lib_cpg_iterationfinalize.header.id = MESSAGE_RES_CPG_ITERATIONFINALIZE;
	res_lib_cpg_iterationfinalize.header.error = error;

	api->ipc_response_send (conn, &res_lib_cpg_iterationfinalize,
		sizeof (res_lib_cpg_iterationfinalize));
}
