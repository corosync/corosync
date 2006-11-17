/** @file exec/amf.c
 * 
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt, Anders Eriksson, Lars Holm
 * Description:
 * - Introduced AMF B.02 information model
 * - Use DN in API and multicast messages
 * - (Re-)Introduction of event based multicast messages
 * - Refactoring of code into several AMF files
 * - AMF Synchronisation Control State Machine
 *
 * All rights reserved.
 *
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
 * 
 * AMF Main
 * 
 * The functions in this file are responsible for:
 * - starting the AMF service (amf_exec_init_fn)
 * - build the information model from the configuration file or by
 *   synchronizing the state with an already started AMF node
 * - receiving AMF library requests (message_handler_req_lib_*)
 * - multicast AMF library requests to the cluster
 * - receiving multicasts (message_handler_req_exec_*) and dispatch the
 *   requests to a component instance
 * - send responses to the AMF library (return values for API calls)
 * - handling EVS configuration change events (node leave/join)
 * - handling node synchronisation events (amf_sync_*)
 * - printing the AMF runtime attributes upon user request (USR2 signal)
 * 
 * Some API requests are responded to directly in the lib message_handler.
 * This is normally done when the API request parameters are wrong, e.g. a
 * component cannot be found. In that case, the error handling must be taken
 * care of in the lib message handler.
 * 
 * 1. AMF Synchronization Control State Machine
 * =========================================
 * 
 * 1.1  State Transition Table
 * 
 * State:                  Event:                Action:  New state:
 * ===========================================================================
 * -                       init[AMF disabled]             UNCONFIGURED
 * -                       init                           IDLE
 * IDLE                    node_joined              A0    PROBING-1
 * PROBING-1               timer1 timeout           A1    PROBING-2
 * PROBING-1               SYNC_START               A2    UPDATING_CLUSTER_MODEL
 * PROBING-1               node_joined              A7    PROBING-1
 * PROBING-2               SYNC_START[From me]            CREATING_CLUSTER_MODEL
 * PROBING-2               SYNC_START[From other]         UPDATING_CLUSTER_MODEL
 * PROBING-2               node_joined              A7    PROBING-2
 * CREATING_CLUSTER_MODEL  Model created            A8    SYNCHRONIZING
 * SYNCHRONIZING           sync_activate            A10   NORMAL_OPERATION
 * SYNCHRONIZING           node_left[sync_master]   A5    SYNCHRONIZING
 * SYNCHRONIZING           node_joined[sync_master
 *                           == me]                 A1    SYNCHRONIZING
 * UPDATING_CLUSTER_MODEL  SYNC_DATA                A3    UPDATING_CLUSTER_MODEL
 * UPDATING_CLUSTER_MODEL  sync_activate            A4    NORMAL_OPERATION
 * UPDATING_CLUSTER_MODEL  SYNC_START               A5    UPDATING_CLUSTER_MODEL
 * UPDATING_CLUSTER_MODEL  node_left[sync_master]         PROBING-1
 * NORMAL_OPERATION        sync_init                      SYNCHRONIZING
 * NORMAL_OPERATION        node_left[sync_master]   A6    NORMAL_OPERATION
 * NORMAL_OPERATION        SYNC_REQUEST             A8    NORMAL_OPERATION
 * Any                     SYNC_REQUEST             A9    No change
 *
 * 1.2 State Description
 * =====================
 * IDLE -  Waiting to join cluster.
 * PROBING-1 - Start timer1; wait for timer1 to expire or to get synchronised by
 *             another node.
 * PROBING-2 - Waiting for SYNC_START
 * CREATING_CLUSTER_MODEL - Read configuration file and create cluster model
 * UPDATING_CLUSTER_MODEL - Save sync master node ID; receive SYNC_DATA, 
 *                          deserialize and save.
 * SYNCHRONIZING - If sync master: multicast SYNC_START followed by encoded AMF
 *                 objects as SYNC_DATA; multicast SYNC_READY
 * NORMAL - Start cluster or node; wait for cluster changes
 *
 * 1.3 Action Description
 * ======================
 * A0 - Start timer1
 * A1 - Multicast SYNC_START message
 * A2 - Stop timer1
 * A3 - Decode AMF object and save
 * A4 - Create cluster model; cluster sync ready
 * A5 - Free received SYNC_DATA
 * A6 - Calculate new sync master
 * A7 - Multicast SYNC_REQUEST message
 * A8 - Update AMF node object(s) with CLM nodeid
 * A9 - Save CLM nodeid & hostname
 * A10- Delete CLM nodes; cluster sync ready
 */

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <netdb.h>
#include <sys/stat.h>

#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_amf.h"
#include "../include/list.h"
#include "../lcr/lcr_comp.h"
#include "totempg.h"
#include "mempool.h"
#include "util.h"
#include "amf.h"
#include "main.h"
#include "ipc.h"
#include "service.h"
#include "objdb.h"
#include "print.h"

#ifdef AMFTEST
#define static
#endif

#ifndef HOST_NAME_MAX
# define HOST_NAME_MAX 255
#endif

#define SYNCTRACE(format, args...) do { \
	TRACE6(">%s: " format, __FUNCTION__, ##args); \
} while (0)

/*                                                              
 * The time AMF will wait to get synchronised by another node
 * before it assumes it is alone in the cluster or the first
 * node to start.
 */
#ifndef AMF_SYNC_TIMEOUT
#define AMF_SYNC_TIMEOUT 3000
#endif

static void amf_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);
static int amf_lib_exit_fn (void *conn);
static int amf_exec_init_fn (struct objdb_iface_ver0 *objdb);
static int amf_lib_init_fn (void *conn);
static void message_handler_req_lib_amf_componentregister (void *conn, void *msg);
static void message_handler_req_lib_amf_componentunregister (void *conn, void *msg);
static void message_handler_req_lib_amf_pmstart (void *conn, void *msg);
static void message_handler_req_lib_amf_pmstop (void *conn, void *msg);
static void message_handler_req_lib_amf_healthcheckstart (void *conn, void *msg);
static void message_handler_req_lib_amf_healthcheckconfirm (void *conn, void *msg);
static void message_handler_req_lib_amf_healthcheckstop (void *conn, void *msg);
static void message_handler_req_lib_amf_hastateget (void *conn, void *message);
static void message_handler_req_lib_amf_csiquiescingcomplete (void *conn, void *msg);
static void message_handler_req_lib_amf_protectiongrouptrack (void *conn, void *msg);
static void message_handler_req_lib_amf_protectiongrouptrackstop (void *conn, void *msg);
static void message_handler_req_lib_amf_componenterrorreport (void *conn, void *msg);
static void message_handler_req_lib_amf_componenterrorclear (void *conn, void *msg);
static void message_handler_req_lib_amf_response (void *conn, void *msg);
static void message_handler_req_exec_amf_comp_register (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_comp_error_report (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_comp_instantiate (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_clc_cleanup_completed (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_healthcheck_tmo (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_response (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_sync_start (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_sync_data (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_sync_ready (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_cluster_start_tmo (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_sync_request (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_comp_instantiate_tmo(
	void *message, unsigned int nodeid);
static void amf_dump_fn (void);
static void amf_sync_init (void);
static int amf_sync_process (void);
static void amf_sync_abort (void);
static void amf_sync_activate (void);

struct amf_pd {
	struct amf_comp *comp;
	struct list_head list;
};

/*
 * Service Handler Definition
 */
static struct openais_lib_handler amf_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_amf_componentregister,
		.response_size		= sizeof (struct res_lib_amf_componentregister),
		.response_id		= MESSAGE_RES_AMF_COMPONENTREGISTER,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_amf_componentunregister,
		.response_size		= sizeof (struct res_lib_amf_componentunregister),
		.response_id		= MESSAGE_RES_AMF_COMPONENTUNREGISTER,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_amf_pmstart,
		.response_size		= sizeof (struct res_lib_amf_pmstart),
		.response_id		= MESSAGE_RES_AMF_PMSTART,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_amf_pmstop,
		.response_size		= sizeof (struct res_lib_amf_pmstop),
		.response_id		= MESSAGE_RES_AMF_PMSTOP,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_amf_healthcheckstart,
		.response_size		= sizeof (struct res_lib_amf_healthcheckstart),
		.response_id		= MESSAGE_RES_AMF_HEALTHCHECKSTART,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_amf_healthcheckconfirm,
		.response_size		= sizeof (struct res_lib_amf_healthcheckconfirm),
		.response_id		= MESSAGE_RES_AMF_HEALTHCHECKCONFIRM,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_amf_healthcheckstop,
		.response_size		= sizeof (struct res_lib_amf_healthcheckstop),
		.response_id		= MESSAGE_RES_AMF_HEALTHCHECKSTOP,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_amf_hastateget,
		.response_size		= sizeof (struct res_lib_amf_hastateget),
		.response_id		= MESSAGE_RES_AMF_HASTATEGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_amf_csiquiescingcomplete,
		.response_size		= sizeof (struct res_lib_amf_csiquiescingcomplete),
		.response_id		= MESSAGE_RES_AMF_CSIQUIESCINGCOMPLETE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_amf_protectiongrouptrack,
		.response_size		= sizeof (struct res_lib_amf_protectiongrouptrack),
		.response_id		= MESSAGE_RES_AMF_PROTECTIONGROUPTRACK,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_amf_protectiongrouptrackstop,
		.response_size		= sizeof (struct res_lib_amf_protectiongrouptrackstop),
		.response_id		= MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTOP,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_amf_componenterrorreport,
		.response_size		= sizeof (struct res_lib_amf_componenterrorreport),
		.response_id		= MESSAGE_RES_AMF_COMPONENTERRORREPORT,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn		= message_handler_req_lib_amf_componenterrorclear,
		.response_size		= sizeof (struct res_lib_amf_componenterrorclear),
		.response_id		= MESSAGE_RES_AMF_COMPONENTERRORCLEAR,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn		= message_handler_req_lib_amf_response,
		.response_size		= sizeof (struct res_lib_amf_response),
		.response_id		= MESSAGE_RES_AMF_RESPONSE,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
};

/*
 * Multicast message handlers
 */
static struct openais_exec_handler amf_exec_service[] = {
	{
		.exec_handler_fn = message_handler_req_exec_amf_comp_register,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_comp_error_report,
	},
    {
		.exec_handler_fn = message_handler_req_exec_amf_comp_instantiate,
    },
	{
		.exec_handler_fn = message_handler_req_exec_amf_clc_cleanup_completed,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_healthcheck_tmo,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_response,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_sync_start,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_sync_data,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_sync_ready,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_cluster_start_tmo,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_sync_request,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_comp_instantiate_tmo,
	},
};

/*
 * Exports the interface for the service
 */
static struct openais_service_handler amf_service_handler = {
	.name				= "openais availability management framework B.01.01",
	.id					= AMF_SERVICE,
	.private_data_size	= sizeof (struct amf_pd),
	.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn		= amf_lib_init_fn,
	.lib_exit_fn		= amf_lib_exit_fn,
	.lib_service		= amf_lib_service,
	.lib_service_count	= sizeof (amf_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn		= amf_exec_init_fn,
	.exec_service		= amf_exec_service,
	.exec_service_count	= sizeof (amf_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn			= amf_confchg_fn,
	.exec_dump_fn		= amf_dump_fn,
	.sync_init          = amf_sync_init,
	.sync_process       = amf_sync_process,
	.sync_activate      = amf_sync_activate,
	.sync_abort         = amf_sync_abort,
};

struct amf_node *this_amf_node;
struct amf_cluster *amf_cluster;

static struct openais_service_handler *amf_get_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 amf_service_handler_iface = {
	.openais_get_service_handler_ver0	= amf_get_handler_ver0
};

static struct lcr_iface openais_amf_ver0[1] = {
	{
		.name			= "openais_amf",
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

static struct lcr_comp amf_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_amf_ver0
};

static struct openais_service_handler *amf_get_handler_ver0 (void)
{
	return (&amf_service_handler);
}

__attribute__ ((constructor)) static void register_this_component (void)
{
	lcr_interfaces_set (&openais_amf_ver0[0], &amf_service_handler_iface);
	lcr_component_register (&amf_comp_ver0);
}

struct req_exec_amf_comp_register {
	mar_req_header_t header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct req_exec_amf_comp_error_report {
	mar_req_header_t header;
	SaNameT reportingComponent;
	SaNameT erroneousComponent;
	SaTimeT errorDetectionTime;
	SaAmfRecommendedRecoveryT recommendedRecovery;
	SaNtfIdentifierT ntfIdentifier;
};


struct req_exec_amf_response {
	mar_req_header_t header;
	SaUint32T interface;
	SaNameT dn;
	SaAisErrorT error;
};

struct req_exec_amf_sync_data {
	mar_req_header_t header;
	SaUint32T protocol_version;
	SaUint32T object_type;
};

struct req_exec_amf_sync_request {
	mar_req_header_t header;
	SaUint32T protocol_version;
	char hostname[HOST_NAME_MAX + 1];
};

static const char *scsm_state_names[] = {
	"Unknown",
	"IDLE",
	"PROBING-1",
	"PROBING-2",
	"CREATING_CLUSTER_MODEL",
	"SYNCHRONIZING",
	"NORMAL_OPERATION",
	"UPDATING_CLUSTER_MODEL",
	"UNCONFIGURED"
};

/**
 * Storage for AMF Synchronisation Control State Machine (SCSM).
 */
static struct scsm_descriptor scsm;

typedef struct clm_node {
	unsigned int nodeid;
	char hostname[HOST_NAME_MAX + 1];
	struct clm_node *next;
} clm_node_t;

static char hostname[HOST_NAME_MAX + 1];

/*
 * Nodes in the cluster, only used for initial start
 * since before the AMF node object exist, we don't
 * have storage for the information received in
 * SYNC_REQUEST msg.
 */
static clm_node_t *clm_nodes;

/******************************************************************************
 * Internal (static) utility functions
 *****************************************************************************/

/**
 * Find a CLM node object using nodeid as query. Allocate and
 * return new object if not found.
 * 
 * @param nodeid
 * 
 * @return clm_node_t*
 */
static clm_node_t *clm_node_find_by_nodeid (unsigned int nodeid)
{
	clm_node_t *clm_node;

	for (clm_node = clm_nodes; clm_node != NULL; clm_node = clm_node->next) {
		if (clm_node->nodeid == nodeid) {
			return clm_node;
		}
	}

	clm_node = amf_malloc (sizeof (clm_node_t));
	clm_node->nodeid = nodeid;
	clm_node->next = clm_nodes;
	clm_nodes = clm_node;

	return clm_node;
}

/**
 * Init nodeids in the AMF node objects using information in the
 * CLM node objects.
 */
static void nodeids_init (void)
{
	amf_node_t *amf_node;
	clm_node_t *clm_node;

	ENTER ("");

	for (clm_node = clm_nodes; clm_node != NULL; clm_node = clm_node->next) {
        /*
         * Iterate all AMF nodes if several AMF nodes are mapped to this
         * particular CLM node.* 
		*/
		for (amf_node = amf_cluster->node_head; amf_node != NULL;
			  amf_node = amf_node->next) {

			if (strcmp ((char*)amf_node->saAmfNodeClmNode.value,
				clm_node->hostname) == 0) {

				dprintf ("%s id set to %u", amf_node->name.value, clm_node->nodeid);
				amf_node->nodeid = clm_node->nodeid;
			}
		}
	}
}

/**
 * Return pointer to this node object.
 * 
 * @param cluster
 * 
 * @return struct amf_node*
 */
static struct amf_node *get_this_node_obj (void)
{
	char hostname[HOST_NAME_MAX + 1];

	if (gethostname (hostname, sizeof(hostname)) == -1) {
		log_printf (LOG_LEVEL_ERROR, "gethostname failed: %d", errno);
		openais_exit_error (AIS_DONE_FATAL_ERR);
	}

	return amf_node_find_by_hostname (hostname);
}

/**
 * Prints old and new sync state, sets new state
 * @param state
 */
static void sync_state_set (enum scsm_states state)
{
	SYNCTRACE ("changing sync ctrl state from %s to %s",
		scsm_state_names[scsm.state], scsm_state_names[state]);
	scsm.state = state;
}

/**
 * Multicast SYNC_DATA message containing a model object.
 * 
 * @param buf
 * @param len
 * @param object_type
 * 
 * @return int
 */
static int mcast_sync_data (
	void *buf, int len, amf_object_type_t object_type)
{
	struct req_exec_amf_sync_data req_exec;
	struct iovec iov[2];
	int res;

	req_exec.header.size = sizeof (struct req_exec_amf_sync_data) + len;
	SYNCTRACE ("%d bytes, type %u", req_exec.header.size , object_type);
	req_exec.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_SYNC_DATA);
	req_exec.protocol_version = AMF_PROTOCOL_VERSION;
	req_exec.object_type = object_type;

	iov[0].iov_base = &req_exec;
	iov[0].iov_len  = sizeof (struct req_exec_amf_sync_data);
	iov[1].iov_base = buf;
	iov[1].iov_len  = len;

	res = totempg_groups_mcast_joined (
		openais_group_handle, iov, 2, TOTEMPG_AGREED);

	if (res != 0) {
		dprintf("Unable to send %d bytes of sync data\n", req_exec.header.size);
	}

	return res;
}

/**
 * Timer callback function. The time waiting for external
 * synchronisation has expired, start competing with other
 * nodes to determine who should read config file.
 * @param data
 */
static void timer_function_scsm_timer1_tmo (void *data)
{
	SYNCTRACE ("");
	amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_SYNC_START, NULL, 0);
	sync_state_set (PROBING_2);
}

/**
 * Execute synchronisation upon request. Should be implemented
 * by the SYNC service. Can only be used during initial start
 * since no deferral of lib or timer events is performed.
 */
static void sync_request (void)
{
	int res;

	SYNCTRACE ("");

	assert (amf_cluster->acsm_state == CLUSTER_AC_UNINSTANTIATED);

	amf_sync_init ();

	do {
		res = amf_sync_process ();
		if (res == 1) {
			/* cannot handle this now, should be implemented using totem
			callbacks... */
			openais_exit_error (AIS_DONE_FATAL_ERR);
		}
	} while (res != 0);

	amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_SYNC_READY, NULL, 0);
}

/**
 * Read the configuration file and create cluster model.
 */
static int create_cluster_model (void)
{
	char *error_string;

	SYNCTRACE("");

	amf_cluster = amf_config_read (&error_string);
	if (amf_cluster == NULL) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_AMFCONFIGREAD);
	}

	this_amf_node = get_this_node_obj ();

	if (this_amf_node == NULL) {
		log_printf (LOG_LEVEL_INFO,
			"Info: This node is not configured as an AMF node, disabling.");
		return -1;
	}

	this_amf_node->nodeid = this_ip->nodeid;

	return 0;
}

/**
 * Calculate a sync master (has the lowest node ID) from the
 * members in the cluster. Possibly excluding some members.
 * 
 * @param member_list
 * @param member_list_entries
 * @param exclude_list
 * @param exclude_list_entries
 * 
 * @return int - node ID of new sync master
 */
static unsigned int calc_sync_master (
	unsigned int *member_list, int member_list_entries,
	unsigned int *exclude_list, int exclude_list_entries)
{
	int i, j, exclude;
	unsigned int master = this_ip->nodeid; /* assume this node is master */

	for (i = 0; i < member_list_entries; i++) {
		if (member_list[i] < master) {
			exclude = 0;
			for (j = 0; j < exclude_list_entries; j++) {
				if (member_list[i] == exclude_list[j]) {
					exclude = 1;
					break;
				}
			}
			if (exclude) {
				continue;
			}
			master = member_list[i];
		}
	}

	return master;
}


static void free_synced_data (void)
{
	struct amf_node *node;
	struct amf_application *app;

	SYNCTRACE ("state %s", scsm_state_names[scsm.state]);

	if (scsm.cluster) {
		for (node = scsm.cluster->node_head; node != NULL;) {
			struct amf_node *tmp = node;
			node = node->next;
			free (tmp);
		}
		for (app = scsm.cluster->application_head; app != NULL;) {
			struct amf_application *tmp = app;
			app = app->next;
			amf_application_delete (tmp);
		}

		free (scsm.cluster);
		scsm.cluster = NULL;
	}
}

static int healthcheck_sync (struct amf_healthcheck *healthcheck)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", healthcheck->safHealthcheckKey.key);

	buf = amf_healthcheck_serialize (healthcheck, &len);
	res = mcast_sync_data (buf, len, AMF_HEALTHCHECK);
	free (buf);
	if (res != 0) {
		return 1; /* try again later */
	}

	return 0;
}

static int comp_sync (struct amf_comp *comp)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", comp->name.value);

	if (!scsm.comp_sync_completed) {
		buf = amf_comp_serialize (comp, &len);
		res = mcast_sync_data (buf, len, AMF_COMP);
		free (buf);
		if (res != 0) {
			return 1; /* try again later */
		}
		scsm.comp_sync_completed = 1;
	}

	if (scsm.healthcheck == NULL) {
		scsm.healthcheck = scsm.comp->healthcheck_head;
	}
	for (; scsm.healthcheck != NULL; scsm.healthcheck = scsm.healthcheck->next) {
		if (healthcheck_sync (scsm.healthcheck) != 0) {
			return 1; /* try again later */
		}
	}

	scsm.comp_sync_completed = 0;

	return 0;
}

static int su_sync (struct amf_su *su)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", su->name.value);

	if (!scsm.su_sync_completed) {
		buf = amf_su_serialize (su, &len);
		res = mcast_sync_data (buf, len, AMF_SU);
		free (buf);
		if (res != 0) {
			return 1; /* try again later */
		}
		scsm.su_sync_completed = 1;
	}

	if (scsm.comp == NULL) {
		scsm.comp = scsm.su->comp_head;
	}
	for (; scsm.comp != NULL; scsm.comp = scsm.comp->next) {
		if (comp_sync (scsm.comp) != 0) {
			return 1; /* try again later */
		}
	}
	scsm.su_sync_completed = 0;

	return 0;
}

static int sg_sync (struct amf_sg *sg)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", sg->name.value);

	if (!scsm.sg_sync_completed) {
		buf = amf_sg_serialize (sg, &len);
		res = mcast_sync_data (buf, len, AMF_SG);
		free (buf);
		if (res != 0) {
			return 1; /* try again later */
		}
		scsm.sg_sync_completed = 1;
	}

	if (scsm.su == NULL) {
		scsm.su = scsm.sg->su_head;
	}
	for (; scsm.su != NULL; scsm.su = scsm.su->next) {
		if (su_sync (scsm.su) != 0) {
			return 1; /* try again later */
		}
	}
	scsm.sg_sync_completed = 0;

	return 0;
}

static int csi_assignment_sync (struct amf_csi_assignment *csi_assignment)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", csi_assignment->name.value);

	buf = amf_csi_assignment_serialize (csi_assignment, &len);
	res = mcast_sync_data (buf, len, AMF_CSI_ASSIGNMENT);
	free (buf);
	if (res != 0) {
		return 1; /* try again later */
	}

	return 0;
}

static int csi_attribute_sync (struct amf_csi_attribute *csi_attribute)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", csi_attribute->name);

	buf = amf_csi_attribute_serialize (csi_attribute, &len);
	res = mcast_sync_data (buf, len, AMF_CSI_ATTRIBUTE);
	free (buf);
	if (res != 0) {
		return 1; /* try again later */
	}

	return 0;
}

static int csi_sync (struct amf_csi *csi)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", csi->name.value);

	if (!scsm.csi_sync_completed) {
		buf = amf_csi_serialize (csi, &len);
		res = mcast_sync_data (buf, len, AMF_CSI);
		free (buf);
		if (res != 0) {
			return 1; /* try again later */
		}
		scsm.csi_sync_completed = 1;
	}

	if (scsm.csi_assignment == NULL && scsm.csi_attribute == NULL) {
		scsm.csi_assignment = scsm.csi->assigned_csis;
	}
	for (; scsm.csi_assignment != NULL; 
	     scsm.csi_assignment = scsm.csi_assignment->next) {
		if (csi_assignment_sync (scsm.csi_assignment) != 0) {
			return 1; /* try again later */
		}
	}
	if (scsm.csi_attribute == NULL) {
		scsm.csi_attribute = scsm.csi->attributes_head;
	}
	for (; scsm.csi_attribute != NULL; scsm.csi_attribute = scsm.csi_attribute->next) {
		if (csi_attribute_sync (scsm.csi_attribute) != 0) {
			return 1; /* try again later */
		}
	}

	scsm.csi_sync_completed = 0;

	return 0;
}

static int si_assignment_sync (struct amf_si_assignment *si_assignment)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", si_assignment->name.value);

	buf = amf_si_assignment_serialize (si_assignment, &len);
	res = mcast_sync_data (buf, len, AMF_SI_ASSIGNMENT);
	free (buf);
	if (res != 0) {
		return 1; /* try again later */
	}

	return 0;
}

static int si_sync (struct amf_si *si)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", si->name.value);

	if (!scsm.si_sync_completed) {
		buf = amf_si_serialize (si, &len);
		res = mcast_sync_data (buf, len, AMF_SI);
		free (buf);
		if (res != 0) {
			return 1; /* try again later */
		}
		scsm.si_sync_completed = 1;
	}

	if (scsm.si_assignment == NULL && scsm.csi == NULL) {
		scsm.si_assignment = scsm.si->assigned_sis;
	}
	for (; scsm.si_assignment != NULL; scsm.si_assignment = scsm.si_assignment->next) {
		if (si_assignment_sync (scsm.si_assignment) != 0) {
			return 1; /* try again later */
		}
	}

	if (scsm.csi == NULL) {
		scsm.csi = scsm.si->csi_head;
	}
	for (; scsm.csi != NULL; scsm.csi = scsm.csi->next) {
		if (csi_sync (scsm.csi) != 0) {
			return 1; /* try again later */
		}
	}
	scsm.si_sync_completed = 0;

	return 0;
}

static int application_sync (struct amf_application *app)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", app->name.value);

	if (!scsm.app_sync_completed) {
		buf = amf_application_serialize (app, &len);
		res = mcast_sync_data (buf, len, AMF_APPLICATION);
		free (buf);
		if (res != 0) {
			return 1; /* try again later */
		}
		scsm.app_sync_completed = 1;
	}

	if (scsm.sg == NULL && scsm.si == NULL) {
		scsm.sg = scsm.app->sg_head;
	}

	for (; scsm.sg != NULL; scsm.sg = scsm.sg->next) {
		if (sg_sync (scsm.sg) != 0) {
			return 1; /* try again later */
		}
	}

	if (scsm.si == NULL) {
		scsm.si = scsm.app->si_head;
	}
	for (; scsm.si != NULL; scsm.si = scsm.si->next) {
		if (si_sync (scsm.si) != 0) {
			return 1; /* try again later */
		}
	}
	scsm.app_sync_completed = 0;

	return 0;
}

static int node_sync (struct amf_node *node)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", node->name.value);
	buf = amf_node_serialize (node, &len);
	res = mcast_sync_data (buf, len, AMF_NODE);
	free (buf);
	if (res != 0) {
		return 1; /* try again later */
	}

	return 0;
}

static int cluster_sync (struct amf_cluster *cluster)
{
	char *buf;
	int len, res;

	SYNCTRACE ("%s", cluster->name.value);
	buf = amf_cluster_serialize (cluster, &len);
	res = mcast_sync_data (buf, len, AMF_CLUSTER);
	free (buf);
	if (res != 0) {
		return 1; /* try again later */
	}

	return 0;
}

/**
 * Returns true (1) if the nodeid is a member of the list
 * @param nodeid
 * @param list
 * @param entries
 * 
 * @return int
 */
static int is_member (
	unsigned int nodeid, unsigned int *list, unsigned int entries)
{
	int i;

	for (i = 0; i < entries; i++) {
		if (list[i] == nodeid) {
			return 1;
		}
	}

	return 0;
}



/**
 * Start the AMF nodes that has joined
 */

static void cluster_joined_nodes_start (void)
{
	int i;
	struct amf_node *node;

	for (i = 0; i < scsm.joined_list_entries; i++) {
		node = amf_node_find_by_nodeid (scsm.joined_list[i]);

		if (node != NULL) {
			amf_cluster_sync_ready (amf_cluster, node);
		} else {
			log_printf (LOG_LEVEL_INFO,
				"Info: Node %u is not configured as an AMF node", scsm.joined_list[i]);
		}
	}
}

/******************************************************************************
 * AMF Framework callback implementation                       *
 *****************************************************************************/

static void amf_sync_init (void)
{
	SYNCTRACE ("state %s", scsm_state_names[scsm.state]);

	switch (scsm.state) {
		case UNCONFIGURED:
		case PROBING_1:
		case PROBING_2:
		case SYNCHRONIZING:
			break;
		case NORMAL_OPERATION:
			if (scsm.joined_list_entries > 0) {
				sync_state_set (SYNCHRONIZING);
			}
			break;
		default:
			dprintf ("unknown state: %u", scsm.state);;
			assert (0);
			break;
	}

	if (scsm.state == SYNCHRONIZING && scsm.sync_master == this_ip->nodeid) {
		amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_SYNC_START, NULL, 0);
		assert (amf_cluster != NULL);
		nodeids_init ();
		scsm.cluster = amf_cluster;
		scsm.node = amf_cluster->node_head;
		scsm.app = amf_cluster->application_head;
		scsm.app_sync_completed = 0;
		scsm.sg = NULL;
		scsm.sg_sync_completed = 0;
		scsm.su = NULL;
		scsm.su_sync_completed = 0;
		scsm.comp = NULL;
		scsm.comp_sync_completed = 0;
		scsm.healthcheck = NULL;
		scsm.si = NULL;
		scsm.si_sync_completed = 0;
		scsm.si_assignment = NULL;
		scsm.csi = NULL;
		scsm.csi_sync_completed = 0;
		scsm.csi_assignment = NULL;
		scsm.csi_attribute = NULL;
	}
}

/**
 * SCSM state SYNCHRONIZING processing function. If in correct
 * state, encode and send each object in the information model
 * as a SYNC_DATA message. Depth first traversal to preserve
 * parent/child relations.
 * 
 * @return int
 */
static int amf_sync_process (void)
{
	SYNCTRACE ("state %s", scsm_state_names[scsm.state]);

	if (scsm.state != SYNCHRONIZING || scsm.sync_master != this_ip->nodeid) {
		return 0;
	}

	if (scsm.cluster) {
		if (cluster_sync (scsm.cluster) != 0) {
			return 1; /* try again later */
		}
		scsm.cluster = NULL; /* done with cluster object */
	}

	for (; scsm.node != NULL; scsm.node = scsm.node->next) {
		if (node_sync (scsm.node) != 0) {
			return 1; /* try again later */
		}
	}

#ifdef AMFTEST
	{
		/*                                                              
		 * Test code to generate the event "sync master died" in the
		* middle of synchronization.
		*/
		struct stat buf;
		if (stat ("/tmp/amf_sync_master_crash", &buf) == 0) {
			printf("bye...\n");
			*((int*)NULL) = 0xbad;
		}
	}
#endif

	for (; scsm.app != NULL; scsm.app = scsm.app->next) {
		if (application_sync (scsm.app) != 0) {
			return 1; /* try again later */
		}
	}

	SYNCTRACE ("ready");

	return 0; /* ready */
}

/**
 * SCSM abnormal exit function for state SYNCHRONIZING
 */
static void amf_sync_abort (void)
{
	SYNCTRACE ("state %s", scsm_state_names[scsm.state]);
	memset (&scsm, 0, sizeof (scsm));
	assert (0); /* not ready... */
}

/**
 * SCSM normal exit function for states SYNCHRONIZING &
 * UPDATING_CLUSTER_MODEL. All synced objects are now
 * commited, start node/cluster.
 */
static void amf_sync_activate (void)
{
	clm_node_t *clm_node = clm_nodes;

	SYNCTRACE ("state %s", scsm_state_names[scsm.state]);

	switch (scsm.state) {
		case SYNCHRONIZING:
			/* Delete all CLM nodes, not needed any longer. */
			while (clm_node != NULL) {
				clm_node_t *tmp = clm_node;
				clm_node = clm_node->next;
				free (tmp);
			}
			clm_nodes = NULL;
			sync_state_set (NORMAL_OPERATION);
			
			cluster_joined_nodes_start ();
			break;
		case UPDATING_CLUSTER_MODEL:
			amf_cluster = scsm.cluster;
			assert (amf_cluster != NULL);
			scsm.cluster = NULL;
			this_amf_node = get_this_node_obj ();
			sync_state_set (NORMAL_OPERATION);
			if (this_amf_node != NULL) {
				this_amf_node->nodeid = this_ip->nodeid;
#ifdef AMFDEBUG
				amf_runtime_attributes_print (amf_cluster);
#endif
				amf_cluster_sync_ready (amf_cluster, this_amf_node);
			} else {
				log_printf (LOG_LEVEL_INFO,
					"Info: This node is not configured as an AMF node, disabling.");
				sync_state_set (UNCONFIGURED);
			}
			break;
		case UNCONFIGURED:
		case PROBING_1:
		case NORMAL_OPERATION:
			break;
		default:
			dprintf ("unknown state: %u", scsm.state);;
			assert (0);
			break;
	}

	SYNCTRACE ("");
}

/**
 * First AMF function to be called by the framework. AMF
 * execution continues when this node joins the cluster.
 * @param objdb
 * 
 * @return int
 */
static int amf_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	log_init ("AMF");

	if (gethostname (hostname, sizeof (hostname)) == -1) {
		log_printf (LOG_LEVEL_ERROR, "gethostname failed: %d", errno);
		openais_exit_error (AIS_DONE_FATAL_ERR);
	}

	if (objdb != NULL && !amf_enabled (objdb)) {
		sync_state_set (UNCONFIGURED);
		return 0;
	}

	sync_state_set (IDLE);

	amf_cluster_init();
	amf_node_init();
	amf_application_init();
	amf_sg_init();
	amf_su_init();
	amf_comp_init();
	amf_si_init();
	amf_util_init ();

	return (0);
}

/**
 * Cluster configuration change event handler
 * @param configuration_type
 * @param member_list
 * @param member_list_entries
 * @param left_list
 * @param left_list_entries
 * @param joined_list
 * @param joined_list_entries
 * @param ring_id
 */
static void amf_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	ENTER ("mnum: %d, jnum: %d, lnum: %d, sync state: %s, ring ID %llu rep %s\n",
		member_list_entries, joined_list_entries, left_list_entries,
		scsm_state_names[scsm.state], ring_id->seq, totemip_print (&ring_id->rep));

	/*
	* Save nodes that joined, needed to initialize each
	* node's totem node id later.
	 */
	scsm.joined_list_entries = joined_list_entries;
	if (scsm.joined_list != NULL) {
		free (scsm.joined_list);
	}
	scsm.joined_list = amf_malloc (joined_list_entries * sizeof (unsigned int));
	memcpy (scsm.joined_list, joined_list, sizeof (unsigned int) * joined_list_entries);

	switch (scsm.state) {
		case IDLE: {
			sync_state_set (PROBING_1);
			if (poll_timer_add (aisexec_poll_handle, AMF_SYNC_TIMEOUT, NULL,
				timer_function_scsm_timer1_tmo,	&scsm.timer_handle) != 0) {

				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			break;
		}
		case PROBING_1:
			/* fall-through */
		case PROBING_2:
			if (joined_list_entries > 0) {
				struct req_exec_amf_sync_request msg;
				memcpy (msg.hostname, hostname, strlen (hostname) + 1);
				msg.protocol_version = AMF_PROTOCOL_VERSION;
				amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_SYNC_REQUEST,
					&msg.protocol_version, sizeof (msg) - sizeof (mar_req_header_t));
			}
			break;
		case UNCONFIGURED:
			break;
		case UPDATING_CLUSTER_MODEL:
			if (!is_member (scsm.sync_master, member_list, member_list_entries)) {
				/*
				TODO: ???
				free_synced_data ();
				*/

				sync_state_set (PROBING_1);
				if (poll_timer_add (aisexec_poll_handle, AMF_SYNC_TIMEOUT, NULL,
					timer_function_scsm_timer1_tmo, &scsm.timer_handle) != 0) {

					openais_exit_error (AIS_DONE_FATAL_ERR);
				}
			}
			break;
		case SYNCHRONIZING: {
			if (joined_list_entries > 0 && scsm.sync_master == this_ip->nodeid) {
				/* restart sync */
				amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_SYNC_START, NULL, 0);
			}
			/* If the sync master left the cluster, calculate a new sync
			*  master between the remaining nodes in the cluster excluding
			*  the nodes we are just syncing.
			 */
			if (!is_member (scsm.sync_master, member_list, member_list_entries)) {
				scsm.sync_master =
					calc_sync_master (
						member_list, member_list_entries,
						scsm.joined_list, scsm.joined_list_entries);

				if (scsm.sync_master == this_ip->nodeid) {
					/* restart sync */
					SYNCTRACE ("I am (new) sync master");
					amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_SYNC_START, NULL, 0);
				}
			}
			break;
		}
		case NORMAL_OPERATION: {
			/* If the sync master left the cluster, calculate a new sync
			*  master between the remaining nodes in the cluster.
			 */
			if (!is_member (scsm.sync_master, member_list, member_list_entries)) {
				scsm.sync_master =
					calc_sync_master (
						member_list, member_list_entries, NULL, 0);

				if (scsm.sync_master == this_ip->nodeid) {
					SYNCTRACE ("I am (new) sync master");
				}
			}

			if (left_list_entries > 0) {
				int i;
				struct amf_node *node;

				for (i = 0; i < left_list_entries; i++) {
					node = amf_node_find_by_nodeid (left_list[i]);
					if (node != NULL) {
						amf_node_leave(node);
					}
				}
			}
			break;
		}
		default:
			log_printf (LOG_LEVEL_ERROR, "unknown state: %u\n", scsm.state);
			assert (0);
			break;
	}
}


static int amf_lib_exit_fn (void *conn)
{
	struct amf_comp *comp;
	struct amf_pd *amf_pd = (struct amf_pd *)openais_conn_private_data_get (conn);

	assert (amf_pd != NULL);
	comp = amf_pd->comp;

    /* Make sure this is not a new connection */
	if (comp != NULL && comp->conn == conn ) {
		comp->conn = NULL;
		dprintf ("Lib exit from comp %s\n", getSaNameT (&comp->name));
	}

	return (0);
}

static int amf_lib_init_fn (void *conn)
{
	struct amf_pd *amf_pd = (struct amf_pd *)openais_conn_private_data_get (conn);

	list_init (&amf_pd->list);

	return (0);
}

static void amf_dump_fn (void)
{
	if (amf_cluster == NULL) {
		return;
	}
	amf_runtime_attributes_print (amf_cluster);
}



/******************************************************************************
 * Executive Message Implementation 
 *****************************************************************************/

static void message_handler_req_exec_amf_comp_register (
	void *message, unsigned int nodeid)
{
	struct res_lib_amf_componentregister res_lib;
	struct req_exec_amf_comp_register *req_exec = message;
	struct amf_comp *comp;
	SaAisErrorT error;

	if (scsm.state != NORMAL_OPERATION) {
		return;
	}

	comp = amf_comp_find (amf_cluster, &req_exec->compName);
	assert (comp != NULL);
	TRACE1 ("ComponentRegister: '%s'", comp->name.value);
	error = amf_comp_register (comp);
	
	if (amf_su_is_local (comp->su)) {
		res_lib.header.id = MESSAGE_RES_AMF_COMPONENTREGISTER;
		res_lib.header.size = sizeof (struct res_lib_amf_componentregister);
		res_lib.header.error = error;
		openais_conn_send_response (
			comp->conn, &res_lib, sizeof (struct res_lib_amf_componentregister));
	}
}

static void message_handler_req_exec_amf_comp_error_report (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_comp_error_report *req_exec = message;
	struct amf_comp *comp;

	if (scsm.state != NORMAL_OPERATION) {
		return;
	}

	comp = amf_comp_find (amf_cluster, &req_exec->erroneousComponent);
	assert (comp != NULL);
	amf_comp_error_report (comp, req_exec->recommendedRecovery);
}


static void message_handler_req_exec_amf_comp_instantiate(
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_comp_instantiate *req_exec = message;
	struct amf_comp *component;

	component = amf_comp_find (amf_cluster, &req_exec->compName);
	if (component == NULL) {
		log_printf (LOG_ERR, "Error: '%s' not found", req_exec->compName.value);
		return;

	}

	amf_comp_instantiate_event (component);
}

static void message_handler_req_exec_amf_comp_instantiate_tmo(
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_comp_instantiate_tmo *req_exec = message;
	struct amf_comp *component;

	component = amf_comp_find (amf_cluster, &req_exec->compName);
	if (component == NULL) {
		log_printf (LOG_ERR, "Error: '%s' not found", req_exec->compName.value);
		return;

	}
	amf_comp_instantiate_tmo_event (component);
}

static void message_handler_req_exec_amf_clc_cleanup_completed (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_clc_cleanup_completed *req_exec = message;
	struct amf_comp *comp;

	if (scsm.state != NORMAL_OPERATION) {
		return;
	}

	comp = amf_comp_find (amf_cluster, &req_exec->compName);
	if (comp == NULL) {
		log_printf (LOG_ERR, "Error: '%s' not found", req_exec->compName.value);
		return;
	}

	amf_comp_cleanup_completed (comp);
}

static void message_handler_req_exec_amf_healthcheck_tmo (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_healthcheck_tmo *req_exec = message;
	struct amf_comp *comp;
	struct amf_healthcheck *healthcheck;

	if (scsm.state != NORMAL_OPERATION) {
		return;
	}

	comp = amf_comp_find (amf_cluster, &req_exec->compName);
	if (comp == NULL) {
		log_printf (LOG_ERR, "Error: '%s' not found", req_exec->compName.value);
		return;
	}

	ENTER ("%s", comp->name.value);

	healthcheck = amf_comp_find_healthcheck (comp, &req_exec->safHealthcheckKey);

	amf_comp_healthcheck_tmo (comp, healthcheck);
}


static void message_handler_req_exec_amf_response (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_response *req_exec = message;
	struct amf_comp *comp;
	struct res_lib_amf_response res_lib;
	SaAisErrorT retval;

	if (scsm.state != NORMAL_OPERATION) {
		return;
	}

	TRACE1 ("AmfResponse: %s", req_exec->dn.value);

	comp = amf_comp_response_2 (
		req_exec->interface, &req_exec->dn, req_exec->error, &retval);
	assert (comp != NULL);

	if (amf_su_is_local (comp->su)) {
		res_lib.header.id = MESSAGE_RES_AMF_RESPONSE;
		res_lib.header.size = sizeof (struct res_lib_amf_response);
		res_lib.header.error = retval;
		openais_conn_send_response (comp->conn, &res_lib, sizeof (res_lib));
	}
}

static void message_handler_req_exec_amf_sync_start (
	void *message, unsigned int nodeid)
{
	SYNCTRACE ("from: %s", totempg_ifaces_print (nodeid));

	switch (scsm.state) {
		case IDLE:
			break;
		case PROBING_1:
			poll_timer_delete (aisexec_poll_handle, scsm.timer_handle);
			scsm.timer_handle = 0;
			sync_state_set (UPDATING_CLUSTER_MODEL);
			scsm.sync_master = nodeid;
			break;
		case PROBING_2:
			if (this_ip->nodeid == nodeid) {
				scsm.sync_master = nodeid;
				sync_state_set (CREATING_CLUSTER_MODEL);
				if (create_cluster_model() == 0) {
					sync_state_set (SYNCHRONIZING);
					sync_request ();
				} else {
                    /* TODO: I am sync master but not AMF node */
					log_printf (LOG_LEVEL_ERROR,
								"AMF sync error: I am sync master but not AMF node");
					openais_exit_error (AIS_DONE_FATAL_ERR);
				}
			} else {
				sync_state_set (UPDATING_CLUSTER_MODEL);
				scsm.sync_master = nodeid;
			}
			break;
		case SYNCHRONIZING:
			break;
		case UPDATING_CLUSTER_MODEL:
			free_synced_data ();
			scsm.sync_master = nodeid;
			break;
		case UNCONFIGURED:
			break;
		default:
			dprintf ("unknown state %d", scsm.state);
			assert (0);
			break;
	}
}

static void message_handler_req_exec_amf_sync_data (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_sync_data *req_exec = message;
	char *tmp = ((char*)message) + sizeof (struct req_exec_amf_sync_data);

	SYNCTRACE ("rec %d bytes, ptr %p, type %d", req_exec->header.size, message,
		req_exec->object_type);

#if 0
	if (req_exec->protocol_version != AMF_PROTOCOL_VERSION) {
		log_printf (LOG_ERR, "Error: Protocol version not supported");
		return;
	}
#endif

	if (scsm.state != UPDATING_CLUSTER_MODEL) {
		return;
	}

	switch (req_exec->object_type)
	{
		case AMF_CLUSTER:
			if ((scsm.cluster = amf_cluster_deserialize (tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("Cluster '%s' deserialised", scsm.cluster->name.value);
			break;
		case AMF_NODE:
			if ((scsm.node = amf_node_deserialize (scsm.cluster, tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("Node '%s' deserialised", scsm.node->name.value);
			break;
		case AMF_APPLICATION:
			if ((scsm.app = amf_application_deserialize (scsm.cluster, tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("App '%s' deserialised", scsm.app->name.value);
			break;
		case AMF_SG:
			if ((scsm.sg = amf_sg_deserialize (scsm.app, tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("SG '%s' deserialised", scsm.sg->name.value);
			break;
		case AMF_SU:
			if ((scsm.su = amf_su_deserialize (scsm.sg, tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("SU '%s' deserialised", scsm.su->name.value);
			break;
		case AMF_COMP:
			if ((scsm.comp = amf_comp_deserialize (scsm.su, tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("Component '%s' deserialised", scsm.comp->name.value);
			break;
		case AMF_HEALTHCHECK:
			if ((scsm.healthcheck = amf_healthcheck_deserialize (scsm.comp,
				tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("Healthcheck '%s' deserialised",
					 scsm.healthcheck->safHealthcheckKey.key);
			break;
		case AMF_SI:
			if ((scsm.si = amf_si_deserialize (scsm.app, tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("SI '%s' deserialised", scsm.si->name.value);
			break;
		case AMF_SI_ASSIGNMENT:
			if ((scsm.si_assignment = amf_si_assignment_deserialize (scsm.si,
				tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("SI Ass '%s' deserialised",
					 scsm.si_assignment->name.value);
			break;
		case AMF_CSI:
			if ((scsm.csi = amf_csi_deserialize (scsm.si,
				tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("CSI '%s' deserialised", scsm.csi->name.value);
			break;
		case AMF_CSI_ASSIGNMENT:
			if ((scsm.csi_assignment = amf_csi_assignment_deserialize (
				scsm.csi, tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("CSI Ass '%s' deserialised",
					 scsm.csi_assignment->name.value);
			break;
		case AMF_CSI_ATTRIBUTE:
			if ((scsm.csi_attribute = amf_csi_attribute_deserialize (scsm.csi,
				tmp)) == NULL) {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			SYNCTRACE ("CSI Attr '%s' deserialised",
					 scsm.csi_attribute->name);
			break;
		default:
			dprintf ("unknown object: %u", req_exec->object_type);
			assert (0);
			break;
	}
}

/**
 * Commit event handler for the previously received objects.
 * Start this cluster/node now. Used at initial cluster start
 * only.
 * @param message
 * @param nodeid
 */
static void message_handler_req_exec_amf_sync_ready (
	void *message, unsigned int nodeid)
{
	SYNCTRACE ("from: %s", totempg_ifaces_print (nodeid));

	amf_sync_activate ();
}



static void message_handler_req_exec_amf_cluster_start_tmo (
	void *message, unsigned int nodeid)
{
	if (scsm.state != NORMAL_OPERATION) {
		return;
	}
	amf_cluster_start_tmo_event (nodeid == scsm.sync_master, amf_cluster);
}

static void message_handler_req_exec_amf_sync_request (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_sync_request *req_exec = message;
	clm_node_t *clm_node;
	
	SYNCTRACE ("from: %s, name: %s, state %s", totempg_ifaces_print (nodeid),
		req_exec->hostname, scsm_state_names[scsm.state]);

	clm_node = clm_node_find_by_nodeid (nodeid);
	assert (clm_node != NULL);
	strcpy (clm_node->hostname, req_exec->hostname);

	if (scsm.state == NORMAL_OPERATION) {
		amf_node_t *amf_node = amf_cluster->node_head;
		/*
		 * Iterate all AMF nodes if several AMF nodes are mapped to this
         * particular CLM node.
		*/
		for (; amf_node != NULL; amf_node = amf_node->next) {
			if (strcmp ((char*)amf_node->saAmfNodeClmNode.value,
				req_exec->hostname) == 0) {

				amf_node->nodeid = nodeid;
			}
		}
	}
}


/*****************************************************************************
 * Library Interface Implementation
 ****************************************************************************/

static void message_handler_req_lib_amf_componentregister (
	void *conn,
	 void *msg)
{
	struct req_lib_amf_componentregister *req_lib = msg;
	struct amf_comp *comp;

	assert (scsm.state == NORMAL_OPERATION);

	comp = amf_comp_find (amf_cluster, &req_lib->compName);
	if (comp) {
		struct req_exec_amf_comp_register req_exec;
		struct iovec iovec;
		struct amf_pd *amf_pd = openais_conn_private_data_get (conn);

		TRACE2("Comp register '%s'", req_lib->compName.value);
		comp->conn = conn;
		amf_pd->comp = comp;
		req_exec.header.size = sizeof (struct req_exec_amf_comp_register);
		req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
			MESSAGE_REQ_EXEC_AMF_COMPONENT_REGISTER);
		memcpy (&req_exec.compName, &req_lib->compName, sizeof (SaNameT));
		memcpy (&req_exec.proxyCompName,
			&req_lib->proxyCompName, sizeof (SaNameT));
		iovec.iov_base = (char *)&req_exec;
		iovec.iov_len = sizeof (req_exec);
		assert (totempg_groups_mcast_joined (openais_group_handle,
			&iovec, 1, TOTEMPG_AGREED) == 0);
	} else {
		struct res_lib_amf_componentregister res_lib;
		log_printf (LOG_ERR, "Error: Comp register: '%s' not found", req_lib->compName.value);
		res_lib.header.id = MESSAGE_RES_AMF_COMPONENTREGISTER;
		res_lib.header.size = sizeof (struct res_lib_amf_componentregister);
		res_lib.header.error = SA_AIS_ERR_INVALID_PARAM;
		openais_conn_send_response (
			conn, &res_lib, sizeof (struct res_lib_amf_componentregister));
	}
}

static void message_handler_req_lib_amf_componentunregister (
	void *conn,
	void *msg)
{
#ifdef COMPILE_OUT
	struct req_lib_amf_componentunregister *req_lib_amf_componentunregister = (struct req_lib_amf_componentunregister *)message;
	struct req_exec_amf_componentunregister req_exec_amf_componentunregister;
	struct iovec iovec;
	struct amf_comp *component;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_componentunregister()\n");

	req_exec_amf_componentunregister.header.size = sizeof (struct req_exec_amf_componentunregister);
	req_exec_amf_componentunregister.header.id = 
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_COMPONENTUNREGISTER);

	message_source_set (&req_exec_amf_componentunregister.source, conn_info);

	memcpy (&req_exec_amf_componentunregister.req_lib_amf_componentunregister,
		req_lib_amf_componentunregister,
		sizeof (struct req_lib_amf_componentunregister));

	component = amf_comp_find (amf_cluster, &req_lib_amf_componentunregister->compName);
	if (component && component->registered && component->local) {
//		component->probableCause = SA_AMF_NOT_RESPONDING;
	}
	iovec.iov_base = (char *)&req_exec_amf_componentunregister;
	iovec.iov_len = sizeof (req_exec_amf_componentunregister);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
#endif
}

static void message_handler_req_lib_amf_pmstart (
	void *conn,
	void *msg)
{
}

static void message_handler_req_lib_amf_pmstop (
	void *conn,
	void *msg)
{
}

static void message_handler_req_lib_amf_healthcheckstart (
	void *conn, void *msg)
{
	struct req_lib_amf_healthcheckstart *req_lib = msg;
	struct res_lib_amf_healthcheckstart res_lib;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	comp = amf_comp_find (amf_cluster, &req_lib->compName);

	if (comp != NULL) {
		comp->conn = conn;
		error = amf_comp_healthcheck_start (
			comp, &req_lib->healthcheckKey, req_lib->invocationType,
			req_lib->recommendedRecovery);
	} else {
		log_printf (LOG_ERR, "Healthcheckstart: Component '%s' not found",
			req_lib->compName.value);
		error = SA_AIS_ERR_NOT_EXIST;
	}

	res_lib.header.id = MESSAGE_RES_AMF_HEALTHCHECKSTART;
	res_lib.header.size = sizeof (res_lib);
	res_lib.header.error = error;
	openais_conn_send_response (conn, &res_lib,
		sizeof (struct res_lib_amf_healthcheckstart));
}

static void message_handler_req_lib_amf_healthcheckconfirm (
	void *conn,	void *msg)
{
	struct req_lib_amf_healthcheckconfirm *req_lib = msg;
	struct res_lib_amf_healthcheckconfirm res_lib;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	comp = amf_comp_find (amf_cluster, &req_lib->compName);
	if (comp != NULL) {
		error = amf_comp_healthcheck_confirm (
			comp, &req_lib->healthcheckKey, req_lib->healthcheckResult);
	} else {
		log_printf (LOG_ERR, "Healthcheck confirm: Component '%s' not found",
			req_lib->compName.value);
		error = SA_AIS_ERR_NOT_EXIST;
	}

	res_lib.header.id = MESSAGE_RES_AMF_HEALTHCHECKCONFIRM;
	res_lib.header.size = sizeof (res_lib);
	res_lib.header.error = error;
	openais_conn_send_response (conn, &res_lib, sizeof (res_lib));
}

static void message_handler_req_lib_amf_healthcheckstop (
	void *conn,	void *msg)
{
	struct req_lib_amf_healthcheckstop *req_lib = msg;
	struct res_lib_amf_healthcheckstop res_lib;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	comp = amf_comp_find (amf_cluster, &req_lib->compName);
	if (comp != NULL) {
		error = amf_comp_healthcheck_stop (comp, &req_lib->healthcheckKey);
	} else {
		log_printf (LOG_ERR, "Healthcheckstop: Component '%s' not found",
			req_lib->compName.value);
		error = SA_AIS_ERR_NOT_EXIST;
	}

	res_lib.header.id = MESSAGE_RES_AMF_HEALTHCHECKSTOP;
	res_lib.header.size = sizeof (res_lib);
	res_lib.header.error = error;
	openais_conn_send_response (conn, &res_lib, sizeof (res_lib));
}

static void message_handler_req_lib_amf_hastateget (void *conn, void *msg)
{
	struct req_lib_amf_hastateget *req_lib = msg;
	struct res_lib_amf_hastateget res_lib;
	struct amf_comp *comp;
	SaAmfHAStateT ha_state;
	SaAisErrorT error;

	comp = amf_comp_find (amf_cluster, &req_lib->compName);
	if (comp != NULL) {
		error = amf_comp_hastate_get (comp, &req_lib->csiName, &ha_state);
		res_lib.haState = ha_state;
		res_lib.header.error = error;
	} else {
		log_printf (LOG_ERR, "HA state get: Component '%s' not found",
			req_lib->compName.value);
		error = SA_AIS_ERR_NOT_EXIST;
	}

	res_lib.header.id = MESSAGE_RES_AMF_HASTATEGET;
	res_lib.header.size = sizeof (struct res_lib_amf_hastateget);
	res_lib.header.error = error;

	openais_conn_send_response (conn, &res_lib,
		sizeof (struct res_lib_amf_hastateget));
}

static void message_handler_req_lib_amf_protectiongrouptrack (
	void *conn,
	void *msg)
{
#ifdef COMPILE_OUT
	struct req_lib_amf_protectiongrouptrack *req_lib_amf_protectiongrouptrack = (struct req_lib_amf_protectiongrouptrack *)message;
	struct res_lib_amf_protectiongrouptrack res_lib_amf_protectiongrouptrack;
	struct libamf_ci_trackentry *track = 0;
	int i;
	struct saAmfProtectionGroup *amfProtectionGroup;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_protectiongrouptrack()\n");

	amfProtectionGroup = protectiongroup_find (&req_lib_amf_protectiongrouptrack->csiName);
	if (amfProtectionGroup) {
		log_printf (LOG_LEVEL_DEBUG, "protectiongrouptrack: Got valid track start on CSI: %s.\n", getSaNameT (&req_lib_amf_protectiongrouptrack->csiName));
		for (i = 0; i < conn_info->ais_ci.u.libamf_ci.trackEntries; i++) {
			if (conn_info->ais_ci.u.libamf_ci.tracks[i].active == 0) {
				track = &conn_info->ais_ci.u.libamf_ci.tracks[i];
				break;
			}
		}

		if (track == 0) {
			grow_amf_track_table (conn_info, 1);
			track = &conn_info->ais_ci.u.libamf_ci.tracks[i];
		}

		track->active = 1;
		track->trackFlags = req_lib_amf_protectiongrouptrack->trackFlags;
		track->notificationBufferAddress = req_lib_amf_protectiongrouptrack->notificationBufferAddress;
		memcpy (&track->csiName,
			&req_lib_amf_protectiongrouptrack->csiName, sizeof (SaNameT));

		conn_info->ais_ci.u.libamf_ci.trackActive += 1;

		list_add (&conn_info->conn_list, &library_notification_send_listhead);
	
		/*
		 * If SA_TRACK_CURRENT is specified, write out all current connections
		 */
	} else {
		log_printf (LOG_LEVEL_DEBUG, "invalid track start, csi not registered with system.\n");
	}

	res_lib_amf_protectiongrouptrack.header.id = MESSAGE_RES_AMF_PROTECTIONGROUPTRACK;
	res_lib_amf_protectiongrouptrack.header.size = sizeof (struct res_lib_amf_protectiongrouptrack);
	res_lib_amf_protectiongrouptrack.header.error = SA_ERR_NOT_EXIST;

	if (amfProtectionGroup) {
		res_lib_amf_protectiongrouptrack.header.error = SA_AIS_OK;
	}
	openais_conn_send_response (conn, &res_lib_amf_protectiongrouptrack,
		sizeof (struct res_lib_amf_protectiongrouptrack));

	if (amfProtectionGroup &&
		req_lib_amf_protectiongrouptrack->trackFlags & SA_TRACK_CURRENT) {

		protectiongroup_notification_send (conn_info,
			track->notificationBufferAddress, 
			amfProtectionGroup,
			0,
			0,
			SA_TRACK_CHANGES_ONLY);

		track->trackFlags &= ~SA_TRACK_CURRENT;
	}
#endif
}

static void message_handler_req_lib_amf_csiquiescingcomplete (
	void *conn,
	void *msg)
{
}

static void message_handler_req_lib_amf_protectiongrouptrackstop (
	void *conn,
	void *msg)
{
#ifdef COMPILE_OUT
	struct req_lib_amf_protectiongrouptrackstop *req_lib_amf_protectiongrouptrackstop = (struct req_lib_amf_protectiongrouptrackstop *)message;
	struct res_lib_amf_protectiongrouptrackstop res_lib_amf_protectiongrouptrackstop;
	struct libamf_ci_trackentry *track = 0;
	int i;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_protectiongrouptrackstop()\n");

	for (i = 0; i < conn_info->ais_ci.u.libamf_ci.trackEntries; i++) {
		if (name_match (&req_lib_amf_protectiongrouptrackstop->csiName,
			&conn_info->ais_ci.u.libamf_ci.tracks[i].csiName)) {

			track = &conn_info->ais_ci.u.libamf_ci.tracks[i];
		}
	}

	if (track) {
		log_printf (LOG_LEVEL_DEBUG, "protectiongrouptrackstop: Trackstop on CSI: %s\n", getSaNameT (&req_lib_amf_protectiongrouptrackstop->csiName));
		memset (track, 0, sizeof (struct libamf_ci_trackentry));
		conn_info->ais_ci.u.libamf_ci.trackActive -= 1;
		if (conn_info->ais_ci.u.libamf_ci.trackActive == 0) {
			list_del (&conn_info->conn_list);
		}
	}

	res_lib_amf_protectiongrouptrackstop.header.id = MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTOP;
	res_lib_amf_protectiongrouptrackstop.header.size = sizeof (struct res_lib_amf_protectiongrouptrackstop);
	res_lib_amf_protectiongrouptrackstop.header.error = SA_ERR_NOT_EXIST;

	if (track) {
		res_lib_amf_protectiongrouptrackstop.header.error = SA_AIS_OK;
	}
	openais_conn_send_response (conn, &res_lib_amf_protectiongrouptrackstop,
		sizeof (struct res_lib_amf_protectiongrouptrackstop));

#endif
}

static void message_handler_req_lib_amf_componenterrorreport (
	void *conn,
	void *msg)
{
	struct req_lib_amf_componenterrorreport *req_lib = msg;
	struct amf_comp *comp;

	assert (scsm.state == NORMAL_OPERATION);

	comp = amf_comp_find (amf_cluster, &req_lib->erroneousComponent);
	if (comp != NULL) {
		struct req_exec_amf_comp_error_report req_exec;
		struct iovec iovec;

		TRACE2("Lib comp error report for '%s'", comp->name.value);

		req_exec.header.size = sizeof (struct req_exec_amf_comp_error_report);
		req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
			MESSAGE_REQ_EXEC_AMF_COMPONENT_ERROR_REPORT);

		memcpy (&req_exec.reportingComponent, &req_lib->reportingComponent,
			sizeof (SaNameT));
		memcpy (&req_exec.erroneousComponent, &req_lib->erroneousComponent,
			sizeof (SaNameT));
		memcpy (&req_exec.errorDetectionTime, &req_lib->errorDetectionTime,
			sizeof (SaTimeT));
		memcpy (&req_exec.recommendedRecovery, &req_lib->recommendedRecovery,
			sizeof (SaAmfRecommendedRecoveryT));
		memcpy (&req_exec.ntfIdentifier, &req_lib->ntfIdentifier,
			sizeof (SaNtfIdentifierT));

		iovec.iov_base = (char *)&req_exec;
		iovec.iov_len = sizeof (req_exec);

		assert (totempg_groups_mcast_joined (
			openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
	} else {
		struct res_lib_amf_componenterrorreport res_lib;

		log_printf (LOG_ERR, "Component %s not found",
			req_lib->erroneousComponent.value);
		res_lib.header.size = sizeof (struct res_lib_amf_componenterrorreport);
		res_lib.header.id = MESSAGE_RES_AMF_COMPONENTERRORREPORT;
		res_lib.header.error = SA_AIS_ERR_NOT_EXIST;
		openais_conn_send_response (conn, &res_lib,
			sizeof (struct res_lib_amf_componenterrorreport));
	}
}

static void message_handler_req_lib_amf_componenterrorclear (
	void *conn,
	void *msg)
{
#ifdef COMPILLE_OUT
	struct req_lib_amf_componenterrorclear *req_lib_amf_componenterrorclear = (struct req_lib_amf_componenterrorclear *)message;
	struct req_exec_amf_componenterrorclear req_exec_amf_componenterrorclear;

	struct iovec iovec;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_componenterrorclear()\n");

	req_exec_amf_componenterrorclear.header.size = sizeof (struct req_exec_amf_componenterrorclear);
	req_exec_amf_componenterrorclear.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_COMPONENTERRORCLEAR);

	message_source_set (&req_exec_amf_componenterrorclear.source, conn_info);

	memcpy (&req_exec_amf_componenterrorclear.req_lib_amf_componenterrorclear,
		req_lib_amf_componenterrorclear,
		sizeof (struct req_lib_amf_componenterrorclear));

	iovec.iov_base = (char *)&req_exec_amf_componenterrorclear;
	iovec.iov_len = sizeof (req_exec_amf_componenterrorclear);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
#endif

}

/**
 * Handle a response from a component.
 * 
 * Healthcheck responses are handled locally and directly. This
 * way we do not get healthcheck duration timeouts during e.g.
 * AMF sync.
 * 
 * Other events need to be multicasted. If we are syncing, defer
 * these event by returning TRY-AGAIN to the component.
 * 
 * No flow control was requested by AMF from the IPC layer (on
 * purpose) for this lib handler. It is needed to handle
 * healthcheck responses if it takes longer to sync than the
 * duration period.
 * 
 * When multicasting, check for space in the TOTEM outbound
 * queue and return TRY-AGAIN if the queue is full.
 * 
 * @param conn
 * @param msg
 */
static void message_handler_req_lib_amf_response (void *conn, void *msg)
{
	struct res_lib_amf_response res_lib;
	struct req_lib_amf_response *req_lib = msg;
	int multicast, send_ok;
	SaAisErrorT retval;
	SaUint32T interface;
	SaNameT dn;

	/*
	* This is an optimisation to avoid multicast of healthchecks while keeping
	* a nice design. We multicast and make lib responses from this file.
	*/
	multicast = amf_comp_response_1 (
		req_lib->invocation, req_lib->error, &retval, &interface, &dn);

	if (multicast) {
		struct req_exec_amf_response req_exec;
		struct iovec iovec;

		if (scsm.state != NORMAL_OPERATION) {
			retval = SA_AIS_ERR_TRY_AGAIN;
			goto send_response;
		}

		req_exec.header.size = sizeof (struct req_exec_amf_response);
		req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
			MESSAGE_REQ_EXEC_AMF_RESPONSE);
		req_exec.interface = interface;
		memcpy (&req_exec.dn, &dn, sizeof (SaNameT));
		req_exec.error = req_lib->error;
		iovec.iov_base = (char *)&req_exec;
		iovec.iov_len = sizeof (req_exec);
		send_ok = totempg_groups_send_ok_joined (openais_group_handle, &iovec, 1);

		if (send_ok) {
			if (totempg_groups_mcast_joined (
				openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0) {
				goto end;
			} else {
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
		} else {
			/* TOTEM queue is full, try again later */
			retval = SA_AIS_ERR_TRY_AGAIN;
		}
	}

send_response:
	res_lib.header.id = MESSAGE_RES_AMF_RESPONSE;
	res_lib.header.size = sizeof (struct res_lib_amf_response);
	res_lib.header.error = retval;
	openais_conn_send_response (conn, &res_lib, sizeof (res_lib));
end:
	return;
}

