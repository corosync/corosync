/*
 * Copyright (c) 2009-2015 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Christine Caulfield (ccaulfie@redhat.com)
 *          Fabio M. Di Nitto   (fdinitto@redhat.com)
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
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include <qb/qbipc_common.h>

#include "quorum.h"
#include <corosync/corodefs.h>
#include <corosync/list.h>
#include <corosync/logsys.h>
#include <corosync/coroapi.h>
#include <corosync/icmap.h>
#include <corosync/votequorum.h>
#include <corosync/ipc_votequorum.h>

#include "service.h"
#include "util.h"

LOGSYS_DECLARE_SUBSYS ("VOTEQ");

/*
 * interface with corosync
 */

static struct corosync_api_v1 *corosync_api;

/*
 * votequorum global config vars
 */


static char qdevice_name[VOTEQUORUM_QDEVICE_MAX_NAME_LEN];
static struct cluster_node *qdevice = NULL;
static unsigned int qdevice_timeout = VOTEQUORUM_QDEVICE_DEFAULT_TIMEOUT;
static unsigned int qdevice_sync_timeout = VOTEQUORUM_QDEVICE_DEFAULT_SYNC_TIMEOUT;
static uint8_t qdevice_can_operate = 1;
static void *qdevice_reg_conn = NULL;
static uint8_t qdevice_master_wins = 0;

static uint8_t two_node = 0;

static uint8_t wait_for_all = 0;
static uint8_t wait_for_all_status = 0;

static enum {ATB_NONE, ATB_LOWEST, ATB_HIGHEST, ATB_LIST} auto_tie_breaker = ATB_NONE, initial_auto_tie_breaker = ATB_NONE;
static int lowest_node_id = -1;
static int highest_node_id = -1;

#define DEFAULT_LMS_WIN   10000
static uint8_t last_man_standing = 0;
static uint32_t last_man_standing_window = DEFAULT_LMS_WIN;

static uint8_t allow_downscale = 0;
static uint32_t ev_barrier = 0;

static uint8_t ev_tracking = 0;
static uint32_t ev_tracking_barrier = 0;
static int ev_tracking_fd = -1;

/*
 * votequorum_exec defines/structs/forward definitions
 */

struct req_exec_quorum_nodeinfo {
	struct   qb_ipc_request_header header __attribute__((aligned(8)));
	uint32_t nodeid;
	uint32_t votes;
	uint32_t expected_votes;
	uint32_t flags;
} __attribute__((packed));

struct req_exec_quorum_reconfigure {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	uint32_t nodeid;
	uint32_t value;
	uint8_t param;
	uint8_t _pad0;
	uint8_t _pad1;
	uint8_t _pad2;
} __attribute__((packed));

struct req_exec_quorum_qdevice_reg {
	struct		qb_ipc_request_header header __attribute__((aligned(8)));
	uint32_t	operation;
	char		qdevice_name[VOTEQUORUM_QDEVICE_MAX_NAME_LEN];
} __attribute__((packed));

struct req_exec_quorum_qdevice_reconfigure {
	struct	qb_ipc_request_header header __attribute__((aligned(8)));
	char	oldname[VOTEQUORUM_QDEVICE_MAX_NAME_LEN];
	char	newname[VOTEQUORUM_QDEVICE_MAX_NAME_LEN];
} __attribute__((packed));

/*
 * votequorum_exec onwire version (via totem)
 */

#include "votequorum.h"

/*
 * votequorum_exec onwire messages (via totem)
 */

#define MESSAGE_REQ_EXEC_VOTEQUORUM_NODEINFO            0
#define MESSAGE_REQ_EXEC_VOTEQUORUM_RECONFIGURE         1
#define MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_REG         2
#define MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_RECONFIGURE 3

static void votequorum_exec_send_expectedvotes_notification(void);
static int votequorum_exec_send_quorum_notification(void *conn, uint64_t context);
static int votequorum_exec_send_nodelist_notification(void *conn, uint64_t context);

#define VOTEQUORUM_RECONFIG_PARAM_EXPECTED_VOTES 1
#define VOTEQUORUM_RECONFIG_PARAM_NODE_VOTES     2
#define VOTEQUORUM_RECONFIG_PARAM_CANCEL_WFA     3

static int votequorum_exec_send_reconfigure(uint8_t param, unsigned int nodeid, uint32_t value);

/*
 * used by req_exec_quorum_qdevice_reg
 */
#define VOTEQUORUM_QDEVICE_OPERATION_UNREGISTER 0
#define VOTEQUORUM_QDEVICE_OPERATION_REGISTER   1

/*
 * votequorum internal node status/view
 */

#define NODE_FLAGS_QUORATE               1
#define NODE_FLAGS_LEAVING               2
#define NODE_FLAGS_WFASTATUS             4
#define NODE_FLAGS_FIRST                 8
#define NODE_FLAGS_QDEVICE_REGISTERED   16
#define NODE_FLAGS_QDEVICE_ALIVE        32
#define NODE_FLAGS_QDEVICE_CAST_VOTE    64
#define NODE_FLAGS_QDEVICE_MASTER_WINS 128

typedef enum {
	NODESTATE_MEMBER=1,
	NODESTATE_DEAD,
	NODESTATE_LEAVING
} nodestate_t;

struct cluster_node {
	int         node_id;
	nodestate_t state;
	uint32_t    votes;
	uint32_t    expected_votes;
	uint32_t    flags;
	struct      list_head list;
};

/*
 * votequorum internal quorum status
 */

static uint8_t quorum;
static uint8_t cluster_is_quorate;

/*
 * votequorum membership data
 */

static struct cluster_node *us;
static struct list_head cluster_members_list;
static unsigned int quorum_members[PROCESSOR_COUNT_MAX];
static unsigned int previous_quorum_members[PROCESSOR_COUNT_MAX];
static unsigned int atb_nodelist[PROCESSOR_COUNT_MAX];
static int quorum_members_entries = 0;
static int previous_quorum_members_entries = 0;
static int atb_nodelist_entries = 0;
static struct memb_ring_id quorum_ringid;

/*
 * pre allocate all cluster_nodes + one for qdevice
 */
static struct cluster_node cluster_nodes[PROCESSOR_COUNT_MAX+2];
static int cluster_nodes_entries = 0;

/*
 * votequorum tracking
 */
struct quorum_pd {
	unsigned char track_flags;
	int tracking_enabled;
	uint64_t tracking_context;
	struct list_head list;
	void *conn;
};

static struct list_head trackers_list;

/*
 * votequorum timers
 */

static corosync_timer_handle_t qdevice_timer;
static int qdevice_timer_set = 0;
static corosync_timer_handle_t last_man_standing_timer;
static int last_man_standing_timer_set = 0;
static int sync_nodeinfo_sent = 0;
static int sync_wait_for_poll_or_timeout = 0;

/*
 * Service Interfaces required by service_message_handler struct
 */

static int sync_in_progress = 0;

static void votequorum_sync_init (
	const unsigned int *trans_list,
	size_t trans_list_entries,
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int votequorum_sync_process (void);
static void votequorum_sync_activate (void);
static void votequorum_sync_abort (void);

static quorum_set_quorate_fn_t quorum_callback;

/*
 * votequorum_exec handler and definitions
 */

static char *votequorum_exec_init_fn (struct corosync_api_v1 *api);
static int votequorum_exec_exit_fn (void);
static int votequorum_exec_send_nodeinfo(uint32_t nodeid);

static void message_handler_req_exec_votequorum_nodeinfo (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_nodeinfo_endian_convert (void *message);

static void message_handler_req_exec_votequorum_reconfigure (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_reconfigure_endian_convert (void *message);

static void message_handler_req_exec_votequorum_qdevice_reg (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_qdevice_reg_endian_convert (void *message);

static void message_handler_req_exec_votequorum_qdevice_reconfigure (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_qdevice_reconfigure_endian_convert (void *message);

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
		.exec_handler_fn	= message_handler_req_exec_votequorum_qdevice_reg,
		.exec_endian_convert_fn = exec_votequorum_qdevice_reg_endian_convert
	},
	{ /* 3 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_qdevice_reconfigure,
		.exec_endian_convert_fn	= exec_votequorum_qdevice_reconfigure_endian_convert
	},
};

/*
 * Library Handler and Functions Definitions
 */

static int quorum_lib_init_fn (void *conn);

static int quorum_lib_exit_fn (void *conn);

static void qdevice_timer_fn(void *arg);

static void message_handler_req_lib_votequorum_getinfo (void *conn,
							const void *message);

static void message_handler_req_lib_votequorum_setexpected (void *conn,
							    const void *message);

static void message_handler_req_lib_votequorum_setvotes (void *conn,
							 const void *message);

static void message_handler_req_lib_votequorum_trackstart (void *conn,
							   const void *message);

static void message_handler_req_lib_votequorum_trackstop (void *conn,
							  const void *message);

static void message_handler_req_lib_votequorum_qdevice_register (void *conn,
								 const void *message);

static void message_handler_req_lib_votequorum_qdevice_unregister (void *conn,
								   const void *message);

static void message_handler_req_lib_votequorum_qdevice_update (void *conn,
							       const void *message);

static void message_handler_req_lib_votequorum_qdevice_poll (void *conn,
							     const void *message);

static void message_handler_req_lib_votequorum_qdevice_master_wins (void *conn,
							     const void *message);

static struct corosync_lib_handler quorum_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_getinfo,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_setexpected,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_setvotes,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_trackstart,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_trackstop,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_register,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_unregister,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_update,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_poll,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_master_wins,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_service_engine votequorum_service_engine = {
	.name				= "corosync vote quorum service v1.0",
	.id				= VOTEQUORUM_SERVICE,
	.priority			= 2,
	.private_data_size		= sizeof (struct quorum_pd),
	.allow_inquorate		= CS_LIB_ALLOW_INQUORATE,
	.flow_control			= COROSYNC_LIB_FLOW_CONTROL_REQUIRED,
	.lib_init_fn			= quorum_lib_init_fn,
	.lib_exit_fn			= quorum_lib_exit_fn,
	.lib_engine			= quorum_lib_service,
	.lib_engine_count		= sizeof (quorum_lib_service) / sizeof (struct corosync_lib_handler),
	.exec_init_fn			= votequorum_exec_init_fn,
	.exec_exit_fn			= votequorum_exec_exit_fn,
	.exec_engine			= votequorum_exec_engine,
	.exec_engine_count		= sizeof (votequorum_exec_engine) / sizeof (struct corosync_exec_handler),
	.sync_init			= votequorum_sync_init,
	.sync_process			= votequorum_sync_process,
	.sync_activate			= votequorum_sync_activate,
	.sync_abort			= votequorum_sync_abort
};

struct corosync_service_engine *votequorum_get_service_engine_ver0 (void)
{
	return (&votequorum_service_engine);
}

static struct default_service votequorum_service[] = {
	{
		.name		= "corosync_votequorum",
		.ver		= 0,
		.loader		= votequorum_get_service_engine_ver0
	},
};

/*
 * common/utility macros/functions
 */

#define max(a,b) (((a) > (b)) ? (a) : (b))

#define list_iterate(v, head) \
	for (v = (head)->next; v != head; v = v->next)

static void node_add_ordered(struct cluster_node *newnode)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &newnode->list;

	ENTER();

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (newnode->node_id < node->node_id) {
			break;
		}
	}

	if (!node) {
		list_add(&newnode->list, &cluster_members_list);
	} else {
		newlist->prev = tmp->prev;
		newlist->next = tmp;
		tmp->prev->next = newlist;
		tmp->prev = newlist;
	}

	LEAVE();
}

static struct cluster_node *allocate_node(unsigned int nodeid)
{
	struct cluster_node *cl = NULL;
	struct list_head *tmp;

	ENTER();

	if (cluster_nodes_entries <= PROCESSOR_COUNT_MAX + 1) {
		cl = (struct cluster_node *)&cluster_nodes[cluster_nodes_entries];
		cluster_nodes_entries++;
	} else {
		list_iterate(tmp, &cluster_members_list) {
			cl = list_entry(tmp, struct cluster_node, list);
			if (cl->state == NODESTATE_DEAD) {
				break;
			}
		}
		/*
		 * this should never happen
		 */
		if (!cl) {
			log_printf(LOGSYS_LEVEL_CRIT, "Unable to find memory for node %u data!!", nodeid);
			goto out;
		}
		list_del(tmp);
	}

	memset(cl, 0, sizeof(struct cluster_node));
	cl->node_id = nodeid;
	if (nodeid != VOTEQUORUM_QDEVICE_NODEID) {
		node_add_ordered(cl);
	}

out:
	LEAVE();

	return cl;
}

static struct cluster_node *find_node_by_nodeid(unsigned int nodeid)
{
	struct cluster_node *node;
	struct list_head *tmp;

	ENTER();

	if (nodeid == us->node_id) {
		LEAVE();
		return us;
	}

	if (nodeid == VOTEQUORUM_QDEVICE_NODEID) {
		LEAVE();
		return qdevice;
	}

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (node->node_id == nodeid) {
			LEAVE();
			return node;
		}
	}

	LEAVE();
	return NULL;
}

static void get_lowest_node_id(void)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;

	ENTER();

	lowest_node_id = us->node_id;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if ((node->state == NODESTATE_MEMBER) &&
		    (node->node_id < lowest_node_id)) {
			lowest_node_id = node->node_id;
		}
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "lowest node id: %d us: %d", lowest_node_id, us->node_id);
	icmap_set_uint32("runtime.votequorum.lowest_node_id", lowest_node_id);

	LEAVE();
}

static void get_highest_node_id(void)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;

	ENTER();

	highest_node_id = us->node_id;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if ((node->state == NODESTATE_MEMBER) &&
		    (node->node_id > highest_node_id)) {
			highest_node_id = node->node_id;
		}
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "highest node id: %d us: %d", highest_node_id, us->node_id);
	icmap_set_uint32("runtime.votequorum.highest_node_id", highest_node_id);

	LEAVE();
}

static int check_low_node_id_partition(void)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	int found = 0;

	ENTER();

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if ((node->state == NODESTATE_MEMBER) &&
		    (node->node_id == lowest_node_id)) {
				found = 1;
		}
	}

	LEAVE();
	return found;
}

static int check_high_node_id_partition(void)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	int found = 0;

	ENTER();

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if ((node->state == NODESTATE_MEMBER) &&
		    (node->node_id == highest_node_id)) {
				found = 1;
		}
	}

	LEAVE();
	return found;
}

static int is_in_nodelist(int nodeid, unsigned int *members, int entries)
{
	int i;
	ENTER();

	for (i=0; i<entries; i++) {
		if (nodeid == members[i]) {
			LEAVE();
			return 1;
		}
	}
	LEAVE();
	return 0;
}

/*
 * The algorithm for a list of tie-breaker nodes is:
 * travel the list of nodes in the auto_tie_breaker list,
 * if the node IS in our current partition, check if the
 * nodes earlier in the atb list are in the 'previous' partition;
 * If none are found then we are safe to be quorate, if any are
 * then we cannot be as we don't know if that node is up or down.
 * If we don't have a node in the current list we are NOT quorate.
 * Obviously if we find the first node in the atb list in our
 * partition then we are quorate.
 *
 * Special cases lowest nodeid, and highest nodeid are handled separately.
 */
static int check_auto_tie_breaker(void)
{
	int i, j;
	int res;
	ENTER();

	if (auto_tie_breaker == ATB_LOWEST) {
		res = check_low_node_id_partition();
		log_printf(LOGSYS_LEVEL_DEBUG, "ATB_LOWEST decision: %d", res);
		LEAVE();
		return res;
	}
	if (auto_tie_breaker == ATB_HIGHEST) {
		res = check_high_node_id_partition();
		log_printf(LOGSYS_LEVEL_DEBUG, "ATB_HIGHEST decision: %d", res);
		LEAVE();
		return res;
	}

	/* Assume ATB_LIST, we should never be called for ATB_NONE */
	for (i=0; i < atb_nodelist_entries; i++) {
		if (is_in_nodelist(atb_nodelist[i], quorum_members, quorum_members_entries)) {
			/*
			 * Node is in our partition, if any of its predecessors are
			 * in the previous quorum partition then it might be in the
			 * 'other half' (as we've got this far without seeing it here)
			 * and so we can't be quorate.
			 */
			for (j=0; j<i; j++) {
				if (is_in_nodelist(atb_nodelist[j], previous_quorum_members, previous_quorum_members_entries)) {
					log_printf(LOGSYS_LEVEL_DEBUG, "ATB_LIST found node %d in previous partition but not here, quorum denied", atb_nodelist[j]);
					LEAVE();
					return 0;
				}
			}

			/*
			 * None of the other list nodes were in the previous partition, if there
			 * are enough votes, we can be quorate
			 */
			log_printf(LOGSYS_LEVEL_DEBUG, "ATB_LIST found node %d in current partition, we can be quorate", atb_nodelist[i]);
			LEAVE();
			return 1;
		}
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "ATB_LIST found no list nodes in current partition, we cannot be quorate");
	LEAVE();
	return 0;
}

/*
 * atb_string can be either:
 *   'lowest'
 *   'highest'
 *   a list of nodeids
 */
static void parse_atb_string(char *atb_string)
{
	char *ptr;
	long num;

	ENTER();
	auto_tie_breaker = ATB_NONE;

	if (!strcmp(atb_string, "lowest"))
		auto_tie_breaker = ATB_LOWEST;

	if (!strcmp(atb_string, "highest"))
		auto_tie_breaker = ATB_HIGHEST;

	if (atoi(atb_string)) {

		atb_nodelist_entries = 0;
		ptr = atb_string;
		do {
			num = strtol(ptr, &ptr, 10);
			if (num) {
				log_printf(LOGSYS_LEVEL_DEBUG, "ATB nodelist[%d] = %d", atb_nodelist_entries, num);
				atb_nodelist[atb_nodelist_entries++] = num;
			}
		} while (num);

		if (atb_nodelist_entries) {
			auto_tie_breaker = ATB_LIST;
		}
	}
	icmap_set_uint32("runtime.votequorum.atb_type", auto_tie_breaker);
	log_printf(LOGSYS_LEVEL_DEBUG, "ATB type = %d", auto_tie_breaker);

	/* Make sure we got something */
	if (auto_tie_breaker == ATB_NONE) {
		log_printf(LOGSYS_LEVEL_WARNING, "auto_tie_breaker_nodes is not valid. It must be 'lowest', 'highest' or a space-separated list of node IDs. auto_tie_breaker is disabled");
		auto_tie_breaker = ATB_NONE;
	}
	LEAVE();
}

static int check_qdevice_master(void)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	int found = 0;

	ENTER();

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if ((node->state == NODESTATE_MEMBER) &&
		    (node->flags & NODE_FLAGS_QDEVICE_MASTER_WINS) &&
		    (node->flags & NODE_FLAGS_QDEVICE_CAST_VOTE)) {
				found = 1;
		}
	}

	LEAVE();
	return found;
}

static void decode_flags(uint32_t flags)
{
	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG,
		   "flags: quorate: %s Leaving: %s WFA Status: %s First: %s Qdevice: %s QdeviceAlive: %s QdeviceCastVote: %s QdeviceMasterWins: %s",
		   (flags & NODE_FLAGS_QUORATE)?"Yes":"No",
		   (flags & NODE_FLAGS_LEAVING)?"Yes":"No",
		   (flags & NODE_FLAGS_WFASTATUS)?"Yes":"No",
		   (flags & NODE_FLAGS_FIRST)?"Yes":"No",
		   (flags & NODE_FLAGS_QDEVICE_REGISTERED)?"Yes":"No",
		   (flags & NODE_FLAGS_QDEVICE_ALIVE)?"Yes":"No",
		   (flags & NODE_FLAGS_QDEVICE_CAST_VOTE)?"Yes":"No",
		   (flags & NODE_FLAGS_QDEVICE_MASTER_WINS)?"Yes":"No");

	LEAVE();
}

/*
 * load/save are copied almost pristine from totemsrp,c
 */
static int load_ev_tracking_barrier(void)
{
	int res = 0;
	char filename[PATH_MAX];

	ENTER();

	snprintf(filename, sizeof(filename) - 1, "%s/ev_tracking", get_run_dir());

	ev_tracking_fd = open(filename, O_RDWR, 0700);
	if (ev_tracking_fd != -1) {
		res = read (ev_tracking_fd, &ev_tracking_barrier, sizeof(uint32_t));
		close(ev_tracking_fd);
		if (res == sizeof (uint32_t)) {
		        LEAVE();
			return 0;
		}
	}

	ev_tracking_barrier = 0;
	umask(0);
	ev_tracking_fd = open (filename, O_CREAT|O_RDWR, 0700);
	if (ev_tracking_fd != -1) {
		res = write (ev_tracking_fd, &ev_tracking_barrier, sizeof (uint32_t));
		if ((res == -1) || (res != sizeof (uint32_t))) {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "Unable to write to %s", filename);
		}
		close(ev_tracking_fd);
		LEAVE();
		return 0;
	}
	log_printf(LOGSYS_LEVEL_WARNING,
		   "Unable to create %s file", filename);

	LEAVE();

	return -1;
}

static void update_wait_for_all_status(uint8_t wfa_status)
{
	ENTER();

	wait_for_all_status = wfa_status;
	if (wait_for_all_status) {
		us->flags |= NODE_FLAGS_WFASTATUS;
	} else {
		us->flags &= ~NODE_FLAGS_WFASTATUS;
	}
	icmap_set_uint8("runtime.votequorum.wait_for_all_status",
			wait_for_all_status);

	LEAVE();
}

static void update_two_node(void)
{
	ENTER();

	icmap_set_uint8("runtime.votequorum.two_node", two_node);

	LEAVE();
}

static void update_ev_barrier(uint32_t expected_votes)
{
	ENTER();

	ev_barrier = expected_votes;
	icmap_set_uint32("runtime.votequorum.ev_barrier", ev_barrier);

	LEAVE();
}

static void update_qdevice_can_operate(uint8_t status)
{
	ENTER();

	qdevice_can_operate = status;
	icmap_set_uint8("runtime.votequorum.qdevice_can_operate", qdevice_can_operate);

	LEAVE();
}

static void update_qdevice_master_wins(uint8_t allow)
{
	ENTER();

	qdevice_master_wins = allow;
	icmap_set_uint8("runtime.votequorum.qdevice_master_wins", qdevice_master_wins);

	LEAVE();
}

static void update_ev_tracking_barrier(uint32_t ev_t_barrier)
{
	int res;

	ENTER();

	ev_tracking_barrier = ev_t_barrier;
	icmap_set_uint32("runtime.votequorum.ev_tracking_barrier", ev_tracking_barrier);

	if (lseek (ev_tracking_fd, 0, SEEK_SET) != 0) {
		log_printf(LOGSYS_LEVEL_WARNING,
			   "Unable to update ev_tracking_barrier on disk data!!!");
		LEAVE();
		return;
	}

	res = write (ev_tracking_fd, &ev_tracking_barrier, sizeof (uint32_t));
	if (res != sizeof (uint32_t)) {
		log_printf(LOGSYS_LEVEL_WARNING,
			   "Unable to update ev_tracking_barrier on disk data!!!");
	}
#ifdef HAVE_FDATASYNC
	fdatasync(ev_tracking_fd);
#else
	fsync(ev_tracking_fd);
#endif

	LEAVE();
}

/*
 * quorum calculation core bits
 */

static int calculate_quorum(int allow_decrease, unsigned int max_expected, unsigned int *ret_total_votes)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	unsigned int total_votes = 0;
	unsigned int highest_expected = 0;
	unsigned int newquorum, q1, q2;
	unsigned int total_nodes = 0;

	ENTER();

	if ((allow_downscale) && (allow_decrease) && (max_expected)) {
		max_expected = max(ev_barrier, max_expected);
	}

	list_iterate(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		log_printf(LOGSYS_LEVEL_DEBUG, "node %u state=%d, votes=%u, expected=%u",
			   node->node_id, node->state, node->votes, node->expected_votes);

		if (node->state == NODESTATE_MEMBER) {
			highest_expected = max(highest_expected, node->expected_votes);
			total_votes += node->votes;
			total_nodes++;
		}
	}

	if (us->flags & NODE_FLAGS_QDEVICE_CAST_VOTE) {
		log_printf(LOGSYS_LEVEL_DEBUG, "node 0 state=1, votes=%u", qdevice->votes);
		total_votes += qdevice->votes;
		total_nodes++;
	}

	if (max_expected > 0) {
		highest_expected = max_expected;
	}

	/*
	 * This quorum calculation is taken from the OpenVMS Cluster Systems
	 * manual, but, then, you guessed that didn't you
	 */
	q1 = (highest_expected + 2) / 2;
	q2 = (total_votes + 2) / 2;
	newquorum = max(q1, q2);

	/*
	 * Normally quorum never decreases but the system administrator can
	 * force it down by setting expected votes to a maximum value
	 */
	if (!allow_decrease) {
		newquorum = max(quorum, newquorum);
	}

	/*
	 * The special two_node mode allows each of the two nodes to retain
	 * quorum if the other fails.  Only one of the two should live past
	 * fencing (as both nodes try to fence each other in split-brain.)
	 * Also: if there are more than two nodes, force us inquorate to avoid
	 * any damage or confusion.
	 */
	if (two_node && total_nodes <= 2) {
		newquorum = 1;
	}

	if (ret_total_votes) {
		*ret_total_votes = total_votes;
	}

	LEAVE();
	return newquorum;
}

static void update_node_expected_votes(int new_expected_votes)
{
	struct list_head *nodelist;
	struct cluster_node *node;

	if (new_expected_votes) {
		list_iterate(nodelist, &cluster_members_list) {
			node = list_entry(nodelist, struct cluster_node, list);

			if (node->state == NODESTATE_MEMBER) {
				node->expected_votes = new_expected_votes;
			}
		}
	}
}

static void are_we_quorate(unsigned int total_votes)
{
	int quorate;
	int quorum_change = 0;

	ENTER();

	/*
	 * wait for all nodes to show up before granting quorum
	 */

	if ((wait_for_all) && (wait_for_all_status)) {
		if (total_votes != us->expected_votes) {
			log_printf(LOGSYS_LEVEL_NOTICE,
				   "Waiting for all cluster members. "
				   "Current votes: %d expected_votes: %d",
				   total_votes, us->expected_votes);
			cluster_is_quorate = 0;
			return;
		}
		update_wait_for_all_status(0);
	}

	if (quorum > total_votes) {
		quorate = 0;
	} else {
		quorate = 1;
		get_lowest_node_id();
		get_highest_node_id();
	}

	if ((auto_tie_breaker != ATB_NONE) &&
	    /* Must be a half (or half-1) split */
	    (total_votes == (us->expected_votes / 2)) &&
	    /* If the 'other' partition in a split might have quorum then we can't run ATB */
	    (previous_quorum_members_entries - quorum_members_entries < quorum) &&
	    (check_auto_tie_breaker() == 1)) {
		quorate = 1;
	}

	if ((qdevice_master_wins) &&
	    (!quorate) &&
	    (check_qdevice_master() == 1)) {
		log_printf(LOGSYS_LEVEL_DEBUG, "node is quorate as part of master_wins partition");
		quorate = 1;
	}

	if (cluster_is_quorate && !quorate) {
		quorum_change = 1;
		log_printf(LOGSYS_LEVEL_DEBUG, "quorum lost, blocking activity");
	}
	if (!cluster_is_quorate && quorate) {
		quorum_change = 1;
		log_printf(LOGSYS_LEVEL_DEBUG, "quorum regained, resuming activity");
	}

	cluster_is_quorate = quorate;
	if (cluster_is_quorate) {
		us->flags |= NODE_FLAGS_QUORATE;
	} else {
		us->flags &= ~NODE_FLAGS_QUORATE;
	}

	if (wait_for_all) {
		if (quorate) {
			update_wait_for_all_status(0);
		} else {
			update_wait_for_all_status(1);
		}
	}

	if ((quorum_change) &&
	    (sync_in_progress == 0)) {
		quorum_callback(quorum_members, quorum_members_entries,
				cluster_is_quorate, &quorum_ringid);
		votequorum_exec_send_quorum_notification(NULL, 0L);
	}

	LEAVE();
}

static void get_total_votes(unsigned int *totalvotes, unsigned int *current_members)
{
	unsigned int total_votes = 0;
	unsigned int cluster_members = 0;
	struct list_head *nodelist;
	struct cluster_node *node;

	ENTER();

	list_iterate(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);
		if (node->state == NODESTATE_MEMBER) {
			cluster_members++;
			total_votes += node->votes;
		}
	}

	if (qdevice->votes) {
		total_votes += qdevice->votes;
		cluster_members++;
	}

	*totalvotes = total_votes;
	*current_members = cluster_members;

	LEAVE();
}

/*
 * Recalculate cluster quorum, set quorate and notify changes
 */
static void recalculate_quorum(int allow_decrease, int by_current_nodes)
{
	unsigned int total_votes = 0;
	unsigned int cluster_members = 0;

	ENTER();

	get_total_votes(&total_votes, &cluster_members);

	if (!by_current_nodes) {
		cluster_members = 0;
	}

	/*
	 * Keep expected_votes at the highest number of votes in the cluster
	 */
	log_printf(LOGSYS_LEVEL_DEBUG, "total_votes=%d, expected_votes=%d", total_votes, us->expected_votes);
	if (total_votes > us->expected_votes) {
		us->expected_votes = total_votes;
		votequorum_exec_send_expectedvotes_notification();
	}

	if ((ev_tracking) &&
	    (us->expected_votes > ev_tracking_barrier)) {
		update_ev_tracking_barrier(us->expected_votes);
	}

	quorum = calculate_quorum(allow_decrease, cluster_members, &total_votes);
	update_node_expected_votes(cluster_members);

	are_we_quorate(total_votes);

	LEAVE();
}

/*
 * configuration bits and pieces
 */

static int votequorum_read_nodelist_configuration(uint32_t *votes,
						  uint32_t *nodes,
						  uint32_t *expected_votes)
{
	icmap_iter_t iter;
	const char *iter_key;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	uint32_t our_pos, node_pos;
	uint32_t nodecount = 0;
	uint32_t nodelist_expected_votes = 0;
	uint32_t node_votes = 0;
	int res = 0;

	ENTER();

	if (icmap_get_uint32("nodelist.local_node_pos", &our_pos) != CS_OK) {
		log_printf(LOGSYS_LEVEL_DEBUG,
			   "No nodelist defined or our node is not in the nodelist");
		return 0;
	}

	iter = icmap_iter_init("nodelist.node.");

	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {

		res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		nodecount++;

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.quorum_votes", node_pos);
		if (icmap_get_uint32(tmp_key, &node_votes) != CS_OK) {
			node_votes = 1;
		}

		nodelist_expected_votes = nodelist_expected_votes + node_votes;

		if (node_pos == our_pos) {
			*votes = node_votes;
		}
	}

	*expected_votes = nodelist_expected_votes;
	*nodes = nodecount;

	icmap_iter_finalize(iter);

	LEAVE();

	return 1;
}

static int votequorum_qdevice_is_configured(uint32_t *qdevice_votes)
{
	char *qdevice_model = NULL;
	int ret = 0;

	ENTER();

	if (icmap_get_string("quorum.device.model", &qdevice_model) == CS_OK) {
		if (strlen(qdevice_model)) {
			if (icmap_get_uint32("quorum.device.votes", qdevice_votes) != CS_OK) {
				*qdevice_votes = -1;
			}
			if (icmap_get_uint32("quorum.device.timeout", &qdevice_timeout) != CS_OK) {
				qdevice_timeout = VOTEQUORUM_QDEVICE_DEFAULT_TIMEOUT;
			}
			if (icmap_get_uint32("quorum.device.sync_timeout", &qdevice_sync_timeout) != CS_OK) {
				qdevice_sync_timeout = VOTEQUORUM_QDEVICE_DEFAULT_SYNC_TIMEOUT;
			}
			update_qdevice_can_operate(1);
			ret = 1;
		}

		free(qdevice_model);
	}

	LEAVE();

	return ret;
}

#define VOTEQUORUM_READCONFIG_STARTUP 0
#define VOTEQUORUM_READCONFIG_RUNTIME 1

static char *votequorum_readconfig(int runtime)
{
	uint32_t node_votes = 0, qdevice_votes = 0;
	uint32_t node_expected_votes = 0, expected_votes = 0;
	uint32_t node_count = 0;
	uint8_t atb = 0;
	int have_nodelist, have_qdevice;
	char *atb_string = NULL;
	char *error = NULL;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Reading configuration (runtime: %d)", runtime);

	/*
	 * Set the few things we re-read at runtime back to their defaults
	 */
	if (runtime) {
		two_node = 0;
		expected_votes = 0;
		/* auto_tie_breaker cannot be changed by config reload, but
		 * we automatically disable it on odd-sized clusters without
		 * wait_for_all.
		 * We may need to re-enable it when membership changes to ensure
		 * that auto_tie_breaker is consistent across all nodes */
		auto_tie_breaker = initial_auto_tie_breaker;
		icmap_set_uint32("runtime.votequorum.atb_type", auto_tie_breaker);
	}

	/*
	 * gather basic data here
	 */
	icmap_get_uint32("quorum.expected_votes", &expected_votes);
	have_nodelist = votequorum_read_nodelist_configuration(&node_votes, &node_count, &node_expected_votes);
	have_qdevice = votequorum_qdevice_is_configured(&qdevice_votes);
	icmap_get_uint8("quorum.two_node", &two_node);

	/*
	 * do config verification and enablement
	 */

	if ((!have_nodelist) && (!expected_votes)) {
		if (!runtime) {
			error = (char *)"configuration error: nodelist or quorum.expected_votes must be configured!";
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: nodelist or quorum.expected_votes must be configured!");
			log_printf(LOGSYS_LEVEL_CRIT, "will continue with current runtime data");
		}
		goto out;
	}

	/*
	 * two_node and qdevice are not compatible in the same config.
	 * try to make an educated guess of what to do
	 */

	if ((two_node) && (have_qdevice)) {
		if (!runtime) {
			error = (char *)"configuration error: two_node and quorum device cannot be configured at the same time!";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: two_node and quorum device cannot be configured at the same time!");
			if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
				log_printf(LOGSYS_LEVEL_CRIT, "quorum device is registered, disabling two_node");
				two_node = 0;
			} else {
				log_printf(LOGSYS_LEVEL_CRIT, "quorum device is not registered, allowing two_node");
				update_qdevice_can_operate(0);
			}
		}
	}

	/*
	 * Enable special features
	 */
	if (!runtime) {
		if (two_node) {
			wait_for_all = 1;
		}

		icmap_get_uint8("quorum.allow_downscale", &allow_downscale);
		icmap_get_uint8("quorum.wait_for_all", &wait_for_all);
		icmap_get_uint8("quorum.last_man_standing", &last_man_standing);
		icmap_get_uint32("quorum.last_man_standing_window", &last_man_standing_window);
		icmap_get_uint8("quorum.expected_votes_tracking", &ev_tracking);
		icmap_get_uint8("quorum.auto_tie_breaker", &atb);
		icmap_get_string("quorum.auto_tie_breaker_node", &atb_string);

		/* auto_tie_breaker defaults to LOWEST */
		if (atb) {
		    auto_tie_breaker = ATB_LOWEST;
		    icmap_set_uint32("runtime.votequorum.atb_type", auto_tie_breaker);
		}
		else {
			auto_tie_breaker = ATB_NONE;
			if (atb_string) {
				log_printf(LOGSYS_LEVEL_WARNING,
					   "auto_tie_breaker_node: is meaningless if auto_tie_breaker is set to 0");
			}
		}

		if (atb && atb_string) {
			parse_atb_string(atb_string);
		}
		free(atb_string);
		initial_auto_tie_breaker = auto_tie_breaker;

		/* allow_downscale requires ev_tracking */
		if (allow_downscale) {
		    ev_tracking = 1;
		}

		if (ev_tracking) {
		    if (load_ev_tracking_barrier() < 0) {
		        LEAVE();
		        return ((char *)"Unable to load ev_tracking file!");
		    }
		    update_ev_tracking_barrier(ev_tracking_barrier);
		}

	}

	/* two_node and auto_tie_breaker are not compatible as two_node uses
	 * a fence race to decide quorum whereas ATB decides based on node id
	 */
	if (two_node && auto_tie_breaker != ATB_NONE) {
	        log_printf(LOGSYS_LEVEL_CRIT, "two_node and auto_tie_breaker are both specified but are not compatible.");
		log_printf(LOGSYS_LEVEL_CRIT, "two_node has been disabled, please fix your corosync.conf");
		two_node = 0;
	}

	/* If ATB is set and the cluster has an odd number of nodes then wait_for_all needs
	 * to be set so that an isolated half+1 without the tie breaker node
	 * does not have quorum on reboot.
	 */
	if ((auto_tie_breaker != ATB_NONE) && (node_expected_votes % 2) &&
	    (!wait_for_all)) {
		if (last_man_standing) {
			/* if LMS is set too, it's a fatal configuration error. We can't dictate to the user what
			 *  they might want so we'll just quit.
			 */
			log_printf(LOGSYS_LEVEL_CRIT, "auto_tie_breaker is set, the cluster has an odd number of nodes\n");
			log_printf(LOGSYS_LEVEL_CRIT, "and last_man_standing is also set. With this situation a better\n");
			log_printf(LOGSYS_LEVEL_CRIT, "solution would be to disable LMS, leave ATB enabled, and also\n");
			log_printf(LOGSYS_LEVEL_CRIT, "enable wait_for_all (mandatory for ATB in odd-numbered clusters).\n");
			log_printf(LOGSYS_LEVEL_CRIT, "Due to this ambiguity, corosync will fail to start. Please fix your corosync.conf\n");
			error = (char *)"configuration error: auto_tie_breaker & last_man_standing not available in odd sized cluster";
			goto out;
		}
		else {
			log_printf(LOGSYS_LEVEL_CRIT, "auto_tie_breaker is set and the cluster has an odd number of nodes.\n");
			log_printf(LOGSYS_LEVEL_CRIT, "wait_for_all needs to be set for this configuration but it is missing\n");
			log_printf(LOGSYS_LEVEL_CRIT, "Therefore auto_tie_breaker has been disabled. Please fix your corosync.conf\n");
			auto_tie_breaker = ATB_NONE;
			icmap_set_uint32("runtime.votequorum.atb_type", auto_tie_breaker);
		}
	}

	/*
	 * quorum device is not compatible with last_man_standing and auto_tie_breaker
	 * neither lms or atb can be set at runtime, so there is no need to check for
	 * runtime incompatibilities, but qdevice can be configured _after_ LMS and ATB have
	 * been enabled at startup.
	 */

	if ((have_qdevice) && (last_man_standing)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device is not compatible with last_man_standing";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device is not compatible with last_man_standing");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	if ((have_qdevice) && (auto_tie_breaker != ATB_NONE)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device is not compatible with auto_tie_breaker";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device is not compatible with auto_tie_breaker");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	if ((have_qdevice) && (allow_downscale)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device is not compatible with allow_downscale";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device is not compatible with allow_downscale");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	/*
	 * if user specifies quorum.expected_votes + quorum.device but NOT the device.votes
	 * we don't know what the quorum device should vote.
	 */

	if ((expected_votes) && (have_qdevice) && (qdevice_votes == -1)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device.votes must be specified when quorum.expected_votes is set";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device.votes must be specified when quorum.expected_votes is set");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	/*
	 * if user specifies a node list with uneven votes and no device.votes
	 * we cannot autocalculate the votes
	 */

	if ((have_qdevice) &&
	    (qdevice_votes == -1) &&
	    (have_nodelist) &&
	    (node_count != node_expected_votes)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device.votes must be specified when not all nodes votes 1";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device.votes must be specified when not all nodes votes 1");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	/*
	 * validate quorum device votes vs expected_votes
	 */

	if ((qdevice_votes > 0) && (expected_votes)) {
		int delta = expected_votes - qdevice_votes;
		if (delta < 2) {
			if (!runtime) {
				error = (char *)"configuration error: quorum.device.votes is too high or expected_votes is too low";
				goto out;
			} else {
				log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device.votes is too high or expected_votes is too low");
				log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
				update_qdevice_can_operate(0);
			}
		}
	}

	/*
	 * automatically calculate device votes and adjust expected_votes from nodelist
	 */

	if ((have_qdevice) &&
	    (qdevice_votes == -1) &&
	    (!expected_votes) &&
	    (have_nodelist) &&
	    (node_count == node_expected_votes)) {
		qdevice_votes = node_expected_votes - 1;
		node_expected_votes = node_expected_votes + qdevice_votes;
	}

	/*
	 * set this node votes and expected_votes
	 */
	log_printf(LOGSYS_LEVEL_DEBUG, "ev_tracking=%d, ev_tracking_barrier = %d: expected_votes = %d\n", ev_tracking, ev_tracking_barrier, expected_votes);

	if (ev_tracking) {
	        expected_votes = ev_tracking_barrier;
	}

	if (have_nodelist) {
		us->votes = node_votes;
		us->expected_votes = node_expected_votes;
	} else {
		us->votes = 1;
		icmap_get_uint32("quorum.votes", &us->votes);
	}

	if (expected_votes) {
		us->expected_votes = expected_votes;
	}

	/*
	 * set qdevice votes
	 */

	if (!have_qdevice) {
		qdevice->votes = 0;
	}

	if (qdevice_votes != -1) {
		qdevice->votes = qdevice_votes;
	}

	update_ev_barrier(us->expected_votes);
	update_two_node();
	if (wait_for_all) {
		update_wait_for_all_status(1);
	}

out:
	LEAVE();
	return error;
}

static void votequorum_refresh_config(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	int old_votes, old_expected_votes;
	uint8_t reloading;
	uint8_t cancel_wfa;

	ENTER();

	/*
	 * If a full reload is in progress then don't do anything until it's done and
	 * can reconfigure it all atomically
	 */
	if (icmap_get_uint8("config.totemconfig_reload_in_progress", &reloading) == CS_OK && reloading) {
		return ;
	}

	icmap_get_uint8("quorum.cancel_wait_for_all", &cancel_wfa);
	if (strcmp(key_name, "quorum.cancel_wait_for_all") == 0 &&
	    cancel_wfa >= 1) {
	        icmap_set_uint8("quorum.cancel_wait_for_all", 0);
		votequorum_exec_send_reconfigure(VOTEQUORUM_RECONFIG_PARAM_CANCEL_WFA,
						 us->node_id, 0);
		return;
	}

	old_votes = us->votes;
	old_expected_votes = us->expected_votes;

	/*
	 * Reload the configuration
	 */
	votequorum_readconfig(VOTEQUORUM_READCONFIG_RUNTIME);

	/*
	 * activate new config
	 */
	votequorum_exec_send_nodeinfo(us->node_id);
	votequorum_exec_send_nodeinfo(VOTEQUORUM_QDEVICE_NODEID);
	if (us->votes != old_votes) {
		votequorum_exec_send_reconfigure(VOTEQUORUM_RECONFIG_PARAM_NODE_VOTES,
						 us->node_id, us->votes);
	}
	if (us->expected_votes != old_expected_votes) {
		votequorum_exec_send_reconfigure(VOTEQUORUM_RECONFIG_PARAM_EXPECTED_VOTES,
						 us->node_id, us->expected_votes);
	}

	LEAVE();
}

static void votequorum_exec_add_config_notification(void)
{
	icmap_track_t icmap_track_nodelist = NULL;
	icmap_track_t icmap_track_quorum = NULL;
	icmap_track_t icmap_track_reload = NULL;

	ENTER();

	icmap_track_add("nodelist.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		votequorum_refresh_config,
		NULL,
		&icmap_track_nodelist);

	icmap_track_add("quorum.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		votequorum_refresh_config,
		NULL,
		&icmap_track_quorum);

	icmap_track_add("config.totemconfig_reload_in_progress",
		ICMAP_TRACK_ADD | ICMAP_TRACK_MODIFY,
		votequorum_refresh_config,
		NULL,
		&icmap_track_reload);

	LEAVE();
}

/*
 * votequorum_exec core
 */

static int votequorum_exec_send_reconfigure(uint8_t param, unsigned int nodeid, uint32_t value)
{
	struct req_exec_quorum_reconfigure req_exec_quorum_reconfigure;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_reconfigure.nodeid = nodeid;
	req_exec_quorum_reconfigure.value = value;
	req_exec_quorum_reconfigure.param = param;
	req_exec_quorum_reconfigure._pad0 = 0;
	req_exec_quorum_reconfigure._pad1 = 0;
	req_exec_quorum_reconfigure._pad2 = 0;

	req_exec_quorum_reconfigure.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_RECONFIGURE);
	req_exec_quorum_reconfigure.header.size = sizeof(req_exec_quorum_reconfigure);

	iov[0].iov_base = (void *)&req_exec_quorum_reconfigure;
	iov[0].iov_len = sizeof(req_exec_quorum_reconfigure);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_nodeinfo(uint32_t nodeid)
{
	struct req_exec_quorum_nodeinfo req_exec_quorum_nodeinfo;
	struct iovec iov[1];
	struct cluster_node *node;
	int ret;

	ENTER();

	node = find_node_by_nodeid(nodeid);
	if (!node) {
		return -1;
	}

	req_exec_quorum_nodeinfo.nodeid = nodeid;
	req_exec_quorum_nodeinfo.votes = node->votes;
	req_exec_quorum_nodeinfo.expected_votes = node->expected_votes;
	req_exec_quorum_nodeinfo.flags = node->flags;
	if (nodeid != VOTEQUORUM_QDEVICE_NODEID) {
		decode_flags(node->flags);
	}

	req_exec_quorum_nodeinfo.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_NODEINFO);
	req_exec_quorum_nodeinfo.header.size = sizeof(req_exec_quorum_nodeinfo);

	iov[0].iov_base = (void *)&req_exec_quorum_nodeinfo;
	iov[0].iov_len = sizeof(req_exec_quorum_nodeinfo);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_qdevice_reconfigure(const char *oldname, const char *newname)
{
	struct req_exec_quorum_qdevice_reconfigure req_exec_quorum_qdevice_reconfigure;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_qdevice_reconfigure.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_RECONFIGURE);
	req_exec_quorum_qdevice_reconfigure.header.size = sizeof(req_exec_quorum_qdevice_reconfigure);
	strcpy(req_exec_quorum_qdevice_reconfigure.oldname, oldname);
	strcpy(req_exec_quorum_qdevice_reconfigure.newname, newname);

	iov[0].iov_base = (void *)&req_exec_quorum_qdevice_reconfigure;
	iov[0].iov_len = sizeof(req_exec_quorum_qdevice_reconfigure);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_qdevice_reg(uint32_t operation, const char *qdevice_name_req)
{
	struct req_exec_quorum_qdevice_reg req_exec_quorum_qdevice_reg;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_qdevice_reg.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_REG);
	req_exec_quorum_qdevice_reg.header.size = sizeof(req_exec_quorum_qdevice_reg);
	req_exec_quorum_qdevice_reg.operation = operation;
	strcpy(req_exec_quorum_qdevice_reg.qdevice_name, qdevice_name_req);

	iov[0].iov_base = (void *)&req_exec_quorum_qdevice_reg;
	iov[0].iov_len = sizeof(req_exec_quorum_qdevice_reg);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_quorum_notification(void *conn, uint64_t context)
{
	struct res_lib_votequorum_quorum_notification *res_lib_votequorum_notification;
	struct list_head *tmp;
	struct cluster_node *node;
	int i = 0;
	int cluster_members = 0;
	int size;
	char buf[sizeof(struct res_lib_votequorum_quorum_notification) + sizeof(struct votequorum_node) * (PROCESSOR_COUNT_MAX + 2)];

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Sending quorum callback, quorate = %d", cluster_is_quorate);

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		cluster_members++;
        }
	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
		cluster_members++;
	}

	size = sizeof(struct res_lib_votequorum_quorum_notification) + sizeof(struct votequorum_node) * cluster_members;

	res_lib_votequorum_notification = (struct res_lib_votequorum_quorum_notification *)&buf;
	res_lib_votequorum_notification->quorate = cluster_is_quorate;
	res_lib_votequorum_notification->context = context;
	res_lib_votequorum_notification->node_list_entries = cluster_members;
	res_lib_votequorum_notification->header.id = MESSAGE_RES_VOTEQUORUM_QUORUM_NOTIFICATION;
	res_lib_votequorum_notification->header.size = size;
	res_lib_votequorum_notification->header.error = CS_OK;

	/* Send all known nodes and their states */
	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		res_lib_votequorum_notification->node_list[i].nodeid = node->node_id;
		res_lib_votequorum_notification->node_list[i++].state = node->state;
        }
	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
		res_lib_votequorum_notification->node_list[i].nodeid = VOTEQUORUM_QDEVICE_NODEID;
		res_lib_votequorum_notification->node_list[i++].state = qdevice->state;
	}

	/* Send it to all interested parties */
	if (conn) {
		int ret = corosync_api->ipc_dispatch_send(conn, &buf, size);
		LEAVE();
		return ret;
	} else {
		struct quorum_pd *qpd;

		list_iterate(tmp, &trackers_list) {
			qpd = list_entry(tmp, struct quorum_pd, list);
			res_lib_votequorum_notification->context = qpd->tracking_context;
			corosync_api->ipc_dispatch_send(qpd->conn, &buf, size);
		}
	}

	LEAVE();

	return 0;
}

static int votequorum_exec_send_nodelist_notification(void *conn, uint64_t context)
{
	struct res_lib_votequorum_nodelist_notification *res_lib_votequorum_notification;
	int i = 0;
	int size;
	struct list_head *tmp;
	char buf[sizeof(struct res_lib_votequorum_nodelist_notification) + sizeof(uint32_t) * quorum_members_entries];

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Sending nodelist callback. ring_id = %d/%lld", quorum_ringid.rep.nodeid, quorum_ringid.seq);

	size = sizeof(struct res_lib_votequorum_nodelist_notification) + sizeof(uint32_t) * quorum_members_entries;

	res_lib_votequorum_notification = (struct res_lib_votequorum_nodelist_notification *)&buf;
	res_lib_votequorum_notification->node_list_entries = quorum_members_entries;
	res_lib_votequorum_notification->ring_id.nodeid = quorum_ringid.rep.nodeid;
	res_lib_votequorum_notification->ring_id.seq = quorum_ringid.seq;
	res_lib_votequorum_notification->context = context;

	for (i=0; i<quorum_members_entries; i++) {
		res_lib_votequorum_notification->node_list[i] = quorum_members[i];
	}

	res_lib_votequorum_notification->header.id = MESSAGE_RES_VOTEQUORUM_NODELIST_NOTIFICATION;
	res_lib_votequorum_notification->header.size = size;
	res_lib_votequorum_notification->header.error = CS_OK;

	/* Send it to all interested parties */
	if (conn) {
		int ret = corosync_api->ipc_dispatch_send(conn, &buf, size);
		LEAVE();
		return ret;
	} else {
		struct quorum_pd *qpd;

		list_iterate(tmp, &trackers_list) {
			qpd = list_entry(tmp, struct quorum_pd, list);
			res_lib_votequorum_notification->context = qpd->tracking_context;
			corosync_api->ipc_dispatch_send(qpd->conn, &buf, size);
		}
	}

	LEAVE();

	return 0;
}

static void votequorum_exec_send_expectedvotes_notification(void)
{
	struct res_lib_votequorum_expectedvotes_notification res_lib_votequorum_expectedvotes_notification;
	struct quorum_pd *qpd;
	struct list_head *tmp;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Sending expected votes callback");

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

	LEAVE();
}

static void exec_votequorum_qdevice_reconfigure_endian_convert (void *message)
{
	ENTER();

	LEAVE();
}

static void message_handler_req_exec_votequorum_qdevice_reconfigure (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_qdevice_reconfigure *req_exec_quorum_qdevice_reconfigure = message;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Received qdevice name change req from node %u [from: %s to: %s]",
		   nodeid,
		   req_exec_quorum_qdevice_reconfigure->oldname,
		   req_exec_quorum_qdevice_reconfigure->newname);

	if (!strcmp(req_exec_quorum_qdevice_reconfigure->oldname, qdevice_name)) {
		log_printf(LOGSYS_LEVEL_DEBUG, "Allowing qdevice rename");
		memset(qdevice_name, 0, VOTEQUORUM_QDEVICE_MAX_NAME_LEN);
		strcpy(qdevice_name, req_exec_quorum_qdevice_reconfigure->newname);
		/*
		 * TODO: notify qdevices about name change?
		 *       this is not relevant for now and can wait later on since
		 *       qdevices are local only and libvotequorum is not final
		 */
	}

	LEAVE();
}

static void exec_votequorum_qdevice_reg_endian_convert (void *message)
{
	struct req_exec_quorum_qdevice_reg *req_exec_quorum_qdevice_reg = message;

	ENTER();

	req_exec_quorum_qdevice_reg->operation = swab32(req_exec_quorum_qdevice_reg->operation);

	LEAVE();
}

static void message_handler_req_exec_votequorum_qdevice_reg (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_qdevice_reg *req_exec_quorum_qdevice_reg = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	int wipe_qdevice_name = 1;
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	cs_error_t error = CS_OK;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Received qdevice op %u req from node %u [%s]",
		   req_exec_quorum_qdevice_reg->operation,
		   nodeid, req_exec_quorum_qdevice_reg->qdevice_name);

	switch(req_exec_quorum_qdevice_reg->operation)
	{
	case VOTEQUORUM_QDEVICE_OPERATION_REGISTER:
		if (nodeid != us->node_id) {
			if (!strlen(qdevice_name)) {
				log_printf(LOGSYS_LEVEL_DEBUG, "Remote qdevice name recorded");
				strcpy(qdevice_name, req_exec_quorum_qdevice_reg->qdevice_name);
			}
			LEAVE();
			return;
		}

		/*
		 * protect against the case where we broadcast qdevice registration
		 * to new memebers, we receive the message back, but there is no registration
		 * connection in progress
		 */
		if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
			LEAVE();
			return;
		}

		/*
		 * this should NEVER happen
		 */
		if (!qdevice_reg_conn) {
			log_printf(LOGSYS_LEVEL_WARNING, "Unable to determine origin of the qdevice register call!");
			LEAVE();
			return;
		}

		/*
		 * registering our own device in this case
		 */
		if (!strlen(qdevice_name)) {
			strcpy(qdevice_name, req_exec_quorum_qdevice_reg->qdevice_name);
		}

		/*
		 * check if it is our device or something else
		 */
		if ((!strncmp(req_exec_quorum_qdevice_reg->qdevice_name,
			      qdevice_name, VOTEQUORUM_QDEVICE_MAX_NAME_LEN))) {
			us->flags |= NODE_FLAGS_QDEVICE_REGISTERED;
			votequorum_exec_send_nodeinfo(VOTEQUORUM_QDEVICE_NODEID);
			votequorum_exec_send_nodeinfo(us->node_id);
		} else {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "A new qdevice with different name (new: %s old: %s) is trying to register!",
				   req_exec_quorum_qdevice_reg->qdevice_name, qdevice_name);
			error = CS_ERR_EXIST;
		}

		res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
		res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
		res_lib_votequorum_status.header.error = error;
		corosync_api->ipc_response_send(qdevice_reg_conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));
		qdevice_reg_conn = NULL;
		break;
	case VOTEQUORUM_QDEVICE_OPERATION_UNREGISTER:
		list_iterate(tmp, &cluster_members_list) {
			node = list_entry(tmp, struct cluster_node, list);
			if ((node->state == NODESTATE_MEMBER) &&
			    (node->flags & NODE_FLAGS_QDEVICE_REGISTERED)) {
				wipe_qdevice_name = 0;
			}
		}

		if (wipe_qdevice_name) {
			memset(qdevice_name, 0, VOTEQUORUM_QDEVICE_MAX_NAME_LEN);
		}

		break;
	}
	LEAVE();
}

static void exec_votequorum_nodeinfo_endian_convert (void *message)
{
	struct req_exec_quorum_nodeinfo *nodeinfo = message;

	ENTER();

	nodeinfo->nodeid = swab32(nodeinfo->nodeid);
	nodeinfo->votes = swab32(nodeinfo->votes);
	nodeinfo->expected_votes = swab32(nodeinfo->expected_votes);
	nodeinfo->flags = swab32(nodeinfo->flags);

	LEAVE();
}

static void message_handler_req_exec_votequorum_nodeinfo (
	const void *message,
	unsigned int sender_nodeid)
{
	const struct req_exec_quorum_nodeinfo *req_exec_quorum_nodeinfo = message;
	struct cluster_node *node = NULL;
	int old_votes;
	int old_expected;
	uint32_t old_flags;
	nodestate_t old_state;
	int new_node = 0;
	int allow_downgrade = 0;
	int by_node = 0;
	unsigned int nodeid = req_exec_quorum_nodeinfo->nodeid;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "got nodeinfo message from cluster node %u", sender_nodeid);
	log_printf(LOGSYS_LEVEL_DEBUG, "nodeinfo message[%u]: votes: %d, expected: %d flags: %d",
					nodeid,
					req_exec_quorum_nodeinfo->votes,
					req_exec_quorum_nodeinfo->expected_votes,
					req_exec_quorum_nodeinfo->flags);

	if (nodeid != VOTEQUORUM_QDEVICE_NODEID) {
		decode_flags(req_exec_quorum_nodeinfo->flags);
	}

	node = find_node_by_nodeid(nodeid);
	if (!node) {
		node = allocate_node(nodeid);
		new_node = 1;
	}
	if (!node) {
		corosync_api->error_memory_failure();
		LEAVE();
		return;
	}

	if (new_node) {
		old_votes = 0;
		old_expected = 0;
		old_state = NODESTATE_DEAD;
		old_flags = 0;
	} else {
		old_votes = node->votes;
		old_expected = node->expected_votes;
		old_state = node->state;
		old_flags = node->flags;
	}

	if (nodeid == VOTEQUORUM_QDEVICE_NODEID) {
		struct cluster_node *sender_node = find_node_by_nodeid(sender_nodeid);

		assert(sender_node != NULL);

		if ((!cluster_is_quorate) &&
		    (sender_node->flags & NODE_FLAGS_QUORATE)) {
			node->votes = req_exec_quorum_nodeinfo->votes;
		} else {
			node->votes = max(node->votes, req_exec_quorum_nodeinfo->votes);
		}
		goto recalculate;
	}

	/* Update node state */
	node->flags = req_exec_quorum_nodeinfo->flags;
	node->votes = req_exec_quorum_nodeinfo->votes;
	node->state = NODESTATE_MEMBER;

	if (node->flags & NODE_FLAGS_LEAVING) {
		node->state = NODESTATE_LEAVING;
		allow_downgrade = 1;
		by_node = 1;
	}

	if ((!cluster_is_quorate) &&
	    (node->flags & NODE_FLAGS_QUORATE)) {
		allow_downgrade = 1;
		us->expected_votes = req_exec_quorum_nodeinfo->expected_votes;
	}

	if (node->flags & NODE_FLAGS_QUORATE || (ev_tracking)) {
		node->expected_votes = req_exec_quorum_nodeinfo->expected_votes;
	} else {
		node->expected_votes = us->expected_votes;
	}

	if ((last_man_standing) && (node->votes > 1)) {
		log_printf(LOGSYS_LEVEL_WARNING, "Last Man Standing feature is supported only when all"
						 "cluster nodes votes are set to 1. Disabling LMS.");
		last_man_standing = 0;
		if (last_man_standing_timer_set) {
			corosync_api->timer_delete(last_man_standing_timer);
			last_man_standing_timer_set = 0;
		}
	}

recalculate:
	if ((new_node) ||
	    (nodeid == us->node_id) ||
	    (node->flags & NODE_FLAGS_FIRST) ||
	    (old_votes != node->votes) ||
	    (old_expected != node->expected_votes) ||
	    (old_flags != node->flags) ||
	    (old_state != node->state)) {
		recalculate_quorum(allow_downgrade, by_node);
	}

	if ((wait_for_all) &&
	    (!(node->flags & NODE_FLAGS_WFASTATUS)) &&
	    (node->flags & NODE_FLAGS_QUORATE)) {
		update_wait_for_all_status(0);
	}

	LEAVE();
}

static void exec_votequorum_reconfigure_endian_convert (void *message)
{
	struct req_exec_quorum_reconfigure *reconfigure = message;

	ENTER();

	reconfigure->nodeid = swab32(reconfigure->nodeid);
	reconfigure->value = swab32(reconfigure->value);

	LEAVE();
}

static void message_handler_req_exec_votequorum_reconfigure (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_reconfigure *req_exec_quorum_reconfigure = message;
	struct cluster_node *node;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "got reconfigure message from cluster node %u for %u",
					nodeid, req_exec_quorum_reconfigure->nodeid);

	switch(req_exec_quorum_reconfigure->param)
	{
	case VOTEQUORUM_RECONFIG_PARAM_EXPECTED_VOTES:
		update_node_expected_votes(req_exec_quorum_reconfigure->value);

		votequorum_exec_send_expectedvotes_notification();
		update_ev_barrier(req_exec_quorum_reconfigure->value);
		if (ev_tracking) {
		    us->expected_votes = max(us->expected_votes, ev_tracking_barrier);
		}
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	case VOTEQUORUM_RECONFIG_PARAM_NODE_VOTES:
		node = find_node_by_nodeid(req_exec_quorum_reconfigure->nodeid);
		if (!node) {
			LEAVE();
			return;
		}
		node->votes = req_exec_quorum_reconfigure->value;
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	case VOTEQUORUM_RECONFIG_PARAM_CANCEL_WFA:
	        update_wait_for_all_status(0);
		log_printf(LOGSYS_LEVEL_INFO, "wait_for_all_status reset by user on node %d.",
			   req_exec_quorum_reconfigure->nodeid);
		recalculate_quorum(0, 0);

	        break;

	}

	LEAVE();
}

static int votequorum_exec_exit_fn (void)
{
	int ret = 0;

	ENTER();

	/*
	 * tell the other nodes we are leaving
	 */

	if (allow_downscale) {
		us->flags |= NODE_FLAGS_LEAVING;
		ret = votequorum_exec_send_nodeinfo(us->node_id);
	}

	if ((ev_tracking) && (ev_tracking_fd != -1)) {
	    close(ev_tracking_fd);
	}


	LEAVE();
	return ret;
}

static void votequorum_set_icmap_ro_keys(void)
{
	icmap_set_ro_access("quorum.allow_downscale", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("quorum.wait_for_all", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("quorum.last_man_standing", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("quorum.last_man_standing_window", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("quorum.expected_votes_tracking", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("quorum.auto_tie_breaker", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("quorum.auto_tie_breaker_node", CS_FALSE, CS_TRUE);
}

static char *votequorum_exec_init_fn (struct corosync_api_v1 *api)
{
	char *error = NULL;

	ENTER();

	/*
	 * make sure we start clean
	 */
	list_init(&cluster_members_list);
	list_init(&trackers_list);
	qdevice = NULL;
	us = NULL;
	memset(cluster_nodes, 0, sizeof(cluster_nodes));

	/*
	 * Allocate a cluster_node for qdevice
	 */
	qdevice = allocate_node(VOTEQUORUM_QDEVICE_NODEID);
	if (!qdevice) {
		LEAVE();
		return ((char *)"Could not allocate node.");
	}
	qdevice->votes = 0;
	memset(qdevice_name, 0, VOTEQUORUM_QDEVICE_MAX_NAME_LEN);

	/*
	 * Allocate a cluster_node for us
	 */
	us = allocate_node(corosync_api->totem_nodeid_get());
	if (!us) {
		LEAVE();
		return ((char *)"Could not allocate node.");
	}

	icmap_set_uint32("runtime.votequorum.this_node_id", us->node_id);

	us->state = NODESTATE_MEMBER;
	us->votes = 1;
	us->flags |= NODE_FLAGS_FIRST;

	error = votequorum_readconfig(VOTEQUORUM_READCONFIG_STARTUP);
	if (error) {
		return error;
	}
	recalculate_quorum(0, 0);

	/*
	 * Set RO keys in icmap
	 */
	votequorum_set_icmap_ro_keys();

	/*
	 * Listen for changes
	 */
	votequorum_exec_add_config_notification();

	/*
	 * Start us off with one node
	 */
	votequorum_exec_send_nodeinfo(us->node_id);

	LEAVE();

	return (NULL);
}

/*
 * votequorum service core
 */

static void votequorum_last_man_standing_timer_fn(void *arg)
{
	ENTER();

	last_man_standing_timer_set = 0;
	if (cluster_is_quorate) {
		recalculate_quorum(1,1);
	}

	LEAVE();
}

static void votequorum_sync_init (
	const unsigned int *trans_list, size_t trans_list_entries,
	const unsigned int *member_list, size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i, j;
	int found;
	int left_nodes;
	struct cluster_node *node;

	ENTER();

	sync_in_progress = 1;
	sync_nodeinfo_sent = 0;
	sync_wait_for_poll_or_timeout = 0;

	if (member_list_entries > 1) {
		us->flags &= ~NODE_FLAGS_FIRST;
	}

	/*
	 * we don't need to track which nodes have left directly,
	 * since that info is in the node db, but we need to know
	 * if somebody has left for last_man_standing
	 */
	left_nodes = 0;
	for (i = 0; i < quorum_members_entries; i++) {
		found = 0;
		for (j = 0; j < member_list_entries; j++) {
			if (quorum_members[i] == member_list[j]) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			left_nodes = 1;
			node = find_node_by_nodeid(quorum_members[i]);
			if (node) {
				node->state = NODESTATE_DEAD;
			}
		}
	}

	if (last_man_standing) {
		if (((member_list_entries >= quorum) && (left_nodes)) ||
		    ((member_list_entries <= quorum) && (auto_tie_breaker != ATB_NONE) && (check_low_node_id_partition() == 1))) {
			if (last_man_standing_timer_set) {
				corosync_api->timer_delete(last_man_standing_timer);
				last_man_standing_timer_set = 0;
			}
			corosync_api->timer_add_duration((unsigned long long)last_man_standing_window*1000000,
							 NULL, votequorum_last_man_standing_timer_fn,
							 &last_man_standing_timer);
			last_man_standing_timer_set = 1;
		}
	}

	memcpy(previous_quorum_members, quorum_members, sizeof(unsigned int) * quorum_members_entries);
	previous_quorum_members_entries = quorum_members_entries;

	memcpy(quorum_members, member_list, sizeof(unsigned int) * member_list_entries);
	quorum_members_entries = member_list_entries;
	memcpy(&quorum_ringid, ring_id, sizeof(*ring_id));

	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED && us->flags & NODE_FLAGS_QDEVICE_ALIVE) {
		/*
		 * Reset poll timer. Sync waiting is interrupted on valid qdevice poll or after timeout
		 */
		if (qdevice_timer_set) {
			corosync_api->timer_delete(qdevice_timer);
		}
		corosync_api->timer_add_duration((unsigned long long)qdevice_sync_timeout*1000000, qdevice,
						 qdevice_timer_fn, &qdevice_timer);
		qdevice_timer_set = 1;
		sync_wait_for_poll_or_timeout = 1;

		log_printf(LOGSYS_LEVEL_INFO, "waiting for quorum device %s poll (but maximum for %u ms)",
			qdevice_name, qdevice_sync_timeout);
	}

	LEAVE();
}

static int votequorum_sync_process (void)
{
	if (!sync_nodeinfo_sent) {
		votequorum_exec_send_nodeinfo(us->node_id);
		votequorum_exec_send_nodeinfo(VOTEQUORUM_QDEVICE_NODEID);
		if (strlen(qdevice_name)) {
			votequorum_exec_send_qdevice_reg(VOTEQUORUM_QDEVICE_OPERATION_REGISTER,
							 qdevice_name);
		}
		votequorum_exec_send_nodelist_notification(NULL, 0LL);
		sync_nodeinfo_sent = 1;
	}

	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED && sync_wait_for_poll_or_timeout) {
		/*
		 * Waiting for qdevice to poll with new ringid or timeout
		 */

		return (-1);
	}

	return 0;
}

static void votequorum_sync_activate (void)
{
	recalculate_quorum(0, 0);
	quorum_callback(quorum_members, quorum_members_entries,
			cluster_is_quorate, &quorum_ringid);
	votequorum_exec_send_quorum_notification(NULL, 0L);

	sync_in_progress = 0;
}

static void votequorum_sync_abort (void)
{

}

char *votequorum_init(struct corosync_api_v1 *api,
	quorum_set_quorate_fn_t q_set_quorate_fn)
{
	char *error;

	ENTER();

	if (q_set_quorate_fn == NULL) {
		return ((char *)"Quorate function not set");
	}

	corosync_api = api;
	quorum_callback = q_set_quorate_fn;

	error = corosync_service_link_and_init(corosync_api,
		&votequorum_service[0]);
	if (error) {
		return (error);
	}

	LEAVE();

	return (NULL);
}

/*
 * Library Handler init/fini
 */

static int quorum_lib_init_fn (void *conn)
{
	struct quorum_pd *pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();

	list_init (&pd->list);
	pd->conn = conn;

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

/*
 * library internal functions
 */

static void qdevice_timer_fn(void *arg)
{
	ENTER();

	if ((!(us->flags & NODE_FLAGS_QDEVICE_ALIVE)) ||
	    (!qdevice_timer_set)) {
		LEAVE();
		return;
	}

	us->flags &= ~NODE_FLAGS_QDEVICE_ALIVE;
	us->flags &= ~NODE_FLAGS_QDEVICE_CAST_VOTE;
	log_printf(LOGSYS_LEVEL_INFO, "lost contact with quorum device %s", qdevice_name);
	votequorum_exec_send_nodeinfo(us->node_id);

	qdevice_timer_set = 0;
	sync_wait_for_poll_or_timeout = 0;

	LEAVE();
}

/*
 * Library Handler Functions
 */

static void message_handler_req_lib_votequorum_getinfo (void *conn, const void *message)
{
	const struct req_lib_votequorum_getinfo *req_lib_votequorum_getinfo = message;
	struct res_lib_votequorum_getinfo res_lib_votequorum_getinfo;
	struct cluster_node *node;
	unsigned int highest_expected = 0;
	unsigned int total_votes = 0;
	cs_error_t error = CS_OK;
	uint32_t nodeid = req_lib_votequorum_getinfo->nodeid;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "got getinfo request on %p for node %u", conn, req_lib_votequorum_getinfo->nodeid);

	if (nodeid == VOTEQUORUM_QDEVICE_NODEID) {
		nodeid = us->node_id;
	}

	node = find_node_by_nodeid(nodeid);
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

		if (node->flags & NODE_FLAGS_QDEVICE_CAST_VOTE) {
			total_votes += qdevice->votes;
		}

		switch(node->state) {
			case NODESTATE_MEMBER:
				res_lib_votequorum_getinfo.state = VOTEQUORUM_NODESTATE_MEMBER;
				break;
			case NODESTATE_DEAD:
				res_lib_votequorum_getinfo.state = VOTEQUORUM_NODESTATE_DEAD;
				break;
			case NODESTATE_LEAVING:
				res_lib_votequorum_getinfo.state = VOTEQUORUM_NODESTATE_LEAVING;
				break;
			default:
				res_lib_votequorum_getinfo.state = node->state;
				break;
		}
		res_lib_votequorum_getinfo.state = node->state;
		res_lib_votequorum_getinfo.votes = node->votes;
		res_lib_votequorum_getinfo.expected_votes = node->expected_votes;
		res_lib_votequorum_getinfo.highest_expected = highest_expected;

		res_lib_votequorum_getinfo.quorum = quorum;
		res_lib_votequorum_getinfo.total_votes = total_votes;
		res_lib_votequorum_getinfo.flags = 0;
		res_lib_votequorum_getinfo.nodeid = node->node_id;

		if (two_node) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_TWONODE;
		}
		if (cluster_is_quorate) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_QUORATE;
		}
		if (wait_for_all) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_WAIT_FOR_ALL;
		}
		if (last_man_standing) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_LAST_MAN_STANDING;
		}
		if (auto_tie_breaker != ATB_NONE) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_AUTO_TIE_BREAKER;
		}
		if (allow_downscale) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_ALLOW_DOWNSCALE;
		}

		memset(res_lib_votequorum_getinfo.qdevice_name, 0, VOTEQUORUM_QDEVICE_MAX_NAME_LEN);
		strcpy(res_lib_votequorum_getinfo.qdevice_name, qdevice_name);
		res_lib_votequorum_getinfo.qdevice_votes = qdevice->votes;

		if (node->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_QDEVICE_REGISTERED;
		}
		if (node->flags & NODE_FLAGS_QDEVICE_ALIVE) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_QDEVICE_ALIVE;
		}
		if (node->flags & NODE_FLAGS_QDEVICE_CAST_VOTE) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_QDEVICE_CAST_VOTE;
		}
		if (node->flags & NODE_FLAGS_QDEVICE_MASTER_WINS) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_QDEVICE_MASTER_WINS;
		}
	} else {
		error = CS_ERR_NOT_EXIST;
	}

	res_lib_votequorum_getinfo.header.size = sizeof(res_lib_votequorum_getinfo);
	res_lib_votequorum_getinfo.header.id = MESSAGE_RES_VOTEQUORUM_GETINFO;
	res_lib_votequorum_getinfo.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_getinfo, sizeof(res_lib_votequorum_getinfo));
	log_printf(LOGSYS_LEVEL_DEBUG, "getinfo response error: %d", error);

	LEAVE();
}

static void message_handler_req_lib_votequorum_setexpected (void *conn, const void *message)
{
	const struct req_lib_votequorum_setexpected *req_lib_votequorum_setexpected = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;
	unsigned int newquorum;
	unsigned int total_votes;
	uint8_t allow_downscale_status = 0;

	ENTER();

	allow_downscale_status = allow_downscale;
	allow_downscale = 0;

	/*
	 * Validate new expected votes
	 */
	newquorum = calculate_quorum(1, req_lib_votequorum_setexpected->expected_votes, &total_votes);
	allow_downscale = allow_downscale_status;
	if (newquorum < total_votes / 2 ||
	    newquorum > total_votes) {
		error = CS_ERR_INVALID_PARAM;
		goto error_exit;
	}
	update_node_expected_votes(req_lib_votequorum_setexpected->expected_votes);

	votequorum_exec_send_reconfigure(VOTEQUORUM_RECONFIG_PARAM_EXPECTED_VOTES, us->node_id,
					 req_lib_votequorum_setexpected->expected_votes);

error_exit:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

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

	/*
	 * Check votes is valid
	 */
	saved_votes = node->votes;
	node->votes = req_lib_votequorum_setvotes->votes;

	newquorum = calculate_quorum(1, 0, &total_votes);

	if (newquorum < total_votes / 2 ||
	    newquorum > total_votes) {
		node->votes = saved_votes;
		error = CS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	votequorum_exec_send_reconfigure(VOTEQUORUM_RECONFIG_PARAM_NODE_VOTES, nodeid,
					 req_lib_votequorum_setvotes->votes);

error_exit:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_trackstart (void *conn,
							   const void *message)
{
	const struct req_lib_votequorum_trackstart *req_lib_votequorum_trackstart = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);
	cs_error_t error = CS_OK;

	ENTER();

	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_votequorum_trackstart->track_flags & CS_TRACK_CURRENT ||
	    req_lib_votequorum_trackstart->track_flags & CS_TRACK_CHANGES) {
		log_printf(LOGSYS_LEVEL_DEBUG, "sending initial status to %p", conn);
		votequorum_exec_send_nodelist_notification(conn, req_lib_votequorum_trackstart->context);
		votequorum_exec_send_quorum_notification(conn, req_lib_votequorum_trackstart->context);
	}

	if (quorum_pd->tracking_enabled) {
		error = CS_ERR_EXIST;
		goto response_send;
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

response_send:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_trackstop (void *conn,
							  const void *message)
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

	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_register (void *conn,
								 const void *message)
{
	const struct req_lib_votequorum_qdevice_register *req_lib_votequorum_qdevice_register = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (!qdevice_can_operate) {
		log_printf(LOGSYS_LEVEL_INFO, "Registration of quorum device is disabled by incorrect corosync.conf. See logs for more information");
		error = CS_ERR_ACCESS;
		goto out;
	}

	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
		if ((!strncmp(req_lib_votequorum_qdevice_register->name,
		     qdevice_name, VOTEQUORUM_QDEVICE_MAX_NAME_LEN))) {
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "A new qdevice with different name (new: %s old: %s) is trying to re-register!",
				   req_lib_votequorum_qdevice_register->name, qdevice_name);
			error = CS_ERR_EXIST;
			goto out;
		}
	} else {
		if (qdevice_reg_conn != NULL) {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "Registration request already in progress");
			error = CS_ERR_TRY_AGAIN;
			goto out;
		}
		qdevice_reg_conn = conn;
		if (votequorum_exec_send_qdevice_reg(VOTEQUORUM_QDEVICE_OPERATION_REGISTER,
						     req_lib_votequorum_qdevice_register->name) != 0) {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "Unable to send qdevice registration request to cluster");
			error = CS_ERR_TRY_AGAIN;
			qdevice_reg_conn = NULL;
		} else {
			LEAVE();
			return;
		}
	}

out:

	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_unregister (void *conn,
								   const void *message)
{
	const struct req_lib_votequorum_qdevice_unregister *req_lib_votequorum_qdevice_unregister = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
		if (strncmp(req_lib_votequorum_qdevice_unregister->name, qdevice_name, VOTEQUORUM_QDEVICE_MAX_NAME_LEN)) {
			error = CS_ERR_INVALID_PARAM;
			goto out;
		}
		if (qdevice_timer_set) {
			corosync_api->timer_delete(qdevice_timer);
			qdevice_timer_set = 0;
			sync_wait_for_poll_or_timeout = 0;
		}
		us->flags &= ~NODE_FLAGS_QDEVICE_REGISTERED;
		us->flags &= ~NODE_FLAGS_QDEVICE_ALIVE;
		us->flags &= ~NODE_FLAGS_QDEVICE_CAST_VOTE;
		us->flags &= ~NODE_FLAGS_QDEVICE_MASTER_WINS;
		votequorum_exec_send_nodeinfo(us->node_id);
		votequorum_exec_send_qdevice_reg(VOTEQUORUM_QDEVICE_OPERATION_UNREGISTER,
						 req_lib_votequorum_qdevice_unregister->name);
	} else {
		error = CS_ERR_NOT_EXIST;
	}

out:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_update (void *conn,
							       const void *message)
{
	const struct req_lib_votequorum_qdevice_update *req_lib_votequorum_qdevice_update = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
		if (strncmp(req_lib_votequorum_qdevice_update->oldname, qdevice_name, VOTEQUORUM_QDEVICE_MAX_NAME_LEN)) {
			error = CS_ERR_INVALID_PARAM;
			goto out;
		}
		votequorum_exec_send_qdevice_reconfigure(req_lib_votequorum_qdevice_update->oldname,
							 req_lib_votequorum_qdevice_update->newname);
	} else {
		error = CS_ERR_NOT_EXIST;
	}

out:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_poll (void *conn,
							     const void *message)
{
	const struct req_lib_votequorum_qdevice_poll *req_lib_votequorum_qdevice_poll = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;
	uint32_t oldflags;

	ENTER();

	if (!qdevice_can_operate) {
		error = CS_ERR_ACCESS;
		goto out;
	}

	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
		if (!(req_lib_votequorum_qdevice_poll->ring_id.nodeid == quorum_ringid.rep.nodeid &&
		      req_lib_votequorum_qdevice_poll->ring_id.seq == quorum_ringid.seq)) {
			log_printf(LOGSYS_LEVEL_DEBUG, "Received poll ring id (%u.%"PRIu64") != last sync "
			    "ring id (%u.%"PRIu64"). Ignoring poll call.",
			    req_lib_votequorum_qdevice_poll->ring_id.nodeid, req_lib_votequorum_qdevice_poll->ring_id.seq,
			    quorum_ringid.rep.nodeid, quorum_ringid.seq);
			error = CS_ERR_MESSAGE_ERROR;
			goto out;
		}
		if (strncmp(req_lib_votequorum_qdevice_poll->name, qdevice_name, VOTEQUORUM_QDEVICE_MAX_NAME_LEN)) {
			error = CS_ERR_INVALID_PARAM;
			goto out;
		}

		if (qdevice_timer_set) {
			corosync_api->timer_delete(qdevice_timer);
			qdevice_timer_set = 0;
		}

		oldflags = us->flags;

		us->flags |= NODE_FLAGS_QDEVICE_ALIVE;

		if (req_lib_votequorum_qdevice_poll->cast_vote) {
			us->flags |= NODE_FLAGS_QDEVICE_CAST_VOTE;
		} else {
			us->flags &= ~NODE_FLAGS_QDEVICE_CAST_VOTE;
		}

		if (us->flags != oldflags) {
			votequorum_exec_send_nodeinfo(us->node_id);
		}

		corosync_api->timer_add_duration((unsigned long long)qdevice_timeout*1000000, qdevice,
						 qdevice_timer_fn, &qdevice_timer);
		qdevice_timer_set = 1;
		sync_wait_for_poll_or_timeout = 0;
	} else {
		error = CS_ERR_NOT_EXIST;
	}

out:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_master_wins (void *conn,
							     const void *message)
{
	const struct req_lib_votequorum_qdevice_master_wins *req_lib_votequorum_qdevice_master_wins = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;
	uint32_t oldflags = us->flags;

	ENTER();

	if (!qdevice_can_operate) {
		error = CS_ERR_ACCESS;
		goto out;
	}

	if (us->flags & NODE_FLAGS_QDEVICE_REGISTERED) {
		if (strncmp(req_lib_votequorum_qdevice_master_wins->name, qdevice_name, VOTEQUORUM_QDEVICE_MAX_NAME_LEN)) {
			error = CS_ERR_INVALID_PARAM;
			goto out;
		}

		if (req_lib_votequorum_qdevice_master_wins->allow) {
			us->flags |= NODE_FLAGS_QDEVICE_MASTER_WINS;
		} else {
			us->flags &= ~NODE_FLAGS_QDEVICE_MASTER_WINS;
		}

		if (us->flags != oldflags) {
			votequorum_exec_send_nodeinfo(us->node_id);
		}

		update_qdevice_master_wins(req_lib_votequorum_qdevice_master_wins->allow);
	} else {
		error = CS_ERR_NOT_EXIST;
	}

out:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}
