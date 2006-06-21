/** @file exec/amf.c
 * 
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt
 * Description:
 * - Introduced AMF B.02 information model
 * - Use DN in API and multicast messages
 * - (Re-)Introduction of event based multicast messages
 * - Refactoring of code into several AMF files
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
 *   synchronising the state with an already started AMF node
 * - receiving AMF library requests (message_handler_req_lib_*)
 * - distributing AMF library requests to the cluster
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

#define LOG_LEVEL_FROM_LIB LOG_LEVEL_DEBUG
#define LOG_LEVEL_FROM_GMI LOG_LEVEL_DEBUG
#define LOG_LEVEL_ENTER_FUNC LOG_LEVEL_DEBUG

#ifndef HOST_NAME_MAX
# define HOST_NAME_MAX 255
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
static void message_handler_req_exec_amf_clc_cleanup_completed (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_healthcheck_tmo (
	void *message, unsigned int nodeid);
static void message_handler_req_exec_amf_response (
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
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
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
		.response_id		= MESSAGE_RES_AMF_RESPONSE, // TODO
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
		.exec_handler_fn = message_handler_req_exec_amf_clc_cleanup_completed,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_healthcheck_tmo,
	},
	{
		.exec_handler_fn = message_handler_req_exec_amf_response,
	}
};

/*
 * Exports the interface for the service
 */
static struct openais_service_handler amf_service_handler = {
	.name				= (unsigned char *)"openais availability management framework B.01.01",
	.id					= AMF_SERVICE,
	.private_data_size	= sizeof (struct amf_pd),
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
struct amf_cluster amf_cluster;

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
	SaInvocationT invocation;
	SaAisErrorT error;
};

/* IMPL */

static void amf_sync_init (void)
{
	ENTER("");
}

static int amf_sync_process (void)
{
	ENTER("");
	return 0; /* ready */
}

static void amf_sync_abort (void)
{
	ENTER("");
}

static void amf_sync_activate (void)
{
	ENTER("");
}

static int amf_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	int res;
	char *error_string;
	char hostname[HOST_NAME_MAX + 1];
	struct amf_node *node;

	log_init ("AMF");

	if (!amf_enabled (objdb)) {
		return 0;
	}
	
	res = amf_config_read (&amf_cluster, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		return res;
	}

	if (gethostname (hostname, sizeof(hostname)) == -1) {
		log_printf (LOG_LEVEL_ERROR, "gethostname failed: %d", errno);
		return -1;
	}

	/* look for this node */
	for (node = amf_cluster.node_head; node != NULL; node = node->next) {
		if (strcmp(hostname, getSaNameT (&node->name)) == 0) {
			this_amf_node = node;
		}
	}

	if (this_amf_node != NULL) {
		amf_cluster_init();
		amf_application_init();
		amf_sg_init();
		amf_su_init();
		amf_comp_init();
		amf_si_init();

		this_amf_node->saAmfNodeOperState = SA_AMF_OPERATIONAL_ENABLED;
		amf_cluster_start (&amf_cluster);
	} else {
		log_printf (LOG_LEVEL_INFO,
			"This CLM node (%s) is not configured as an AMF node, disabling.",
			hostname);
	}

	return (0);
}

static void amf_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	dprintf ("amf_confchg_fn : type = %d,mnum = %d,jnum = %d,lnum = %d\n",
		configuration_type,member_list_entries,
		joined_list_entries,left_list_entries);
}

static int amf_lib_exit_fn (void *conn)
{
	struct amf_comp *comp;
	struct amf_pd *amf_pd = (struct amf_pd *)openais_conn_private_data_get (conn);

	assert (amf_pd != NULL);
	comp = amf_pd->comp;
	assert (comp != NULL);
	comp->conn = NULL;
	dprintf ("Lib exit from comp %s\n", getSaNameT (&comp->name));

	return (0);
}

static int amf_lib_init_fn (void *conn)
{
	struct amf_pd *amf_pd = (struct amf_pd *)openais_conn_private_data_get (conn);

	list_init (&amf_pd->list);

	return (0);
}

#if 0
static int comp_inservice_count (struct amf_su *unit)
{
	struct amf_comp *comp;
	int answer = 0;

	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		if (comp->saAmfCompReadinessState == SA_AMF_READINESS_IN_SERVICE) {
			answer += 1;
		}
	}
	return (answer);
}
#endif

/*
 * Executive Message Implementation 
 */
static void message_handler_req_exec_amf_comp_register (
	void *message, unsigned int nodeid)
{
	struct res_lib_amf_componentregister res_lib;
	struct req_exec_amf_comp_register *req_exec = message;
	struct amf_comp *comp;
	SaAisErrorT error;

	comp = amf_comp_find (&amf_cluster, &req_exec->compName);
	assert (comp != NULL);
	ENTER ("'%s'", comp->name.value);
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

	comp = amf_comp_find (&amf_cluster, &req_exec->erroneousComponent);
	assert (comp != NULL);
	amf_comp_error_report (comp, req_exec->recommendedRecovery);
}

static void message_handler_req_exec_amf_clc_cleanup_completed (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_clc_cleanup_completed *req_exec = message;
	struct amf_comp *comp;

	comp = amf_comp_find (&amf_cluster, &req_exec->compName);
	if (comp == NULL) {
		log_printf (LOG_ERR, "'%s' not found", &req_exec->compName.value);
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

	comp = amf_comp_find (&amf_cluster, &req_exec->compName);
	if (comp == NULL) {
		log_printf (LOG_ERR, "'%s' not found", &req_exec->compName.value);
		return;
	}

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

	comp = amf_comp_response_2 (req_exec->invocation, req_exec->error, &retval);
	assert (comp != NULL);

	if (amf_su_is_local (comp->su)) {
		res_lib.header.id = MESSAGE_RES_AMF_RESPONSE;
		res_lib.header.size = sizeof (struct res_lib_amf_response);
		res_lib.header.error = retval;
		openais_conn_send_response (comp->conn, &res_lib, sizeof (res_lib));
	}
}

/*
 * Library Interface Implementation
 */
static void message_handler_req_lib_amf_componentregister (
	void *conn,
	 void *msg)
{
	struct req_lib_amf_componentregister *req_lib = msg;
	struct amf_comp *comp;

	comp = amf_comp_find (&amf_cluster, &req_lib->compName);
	if (comp) {
		struct req_exec_amf_comp_register req_exec;
		struct iovec iovec;
		struct amf_pd *amf_pd = openais_conn_private_data_get (conn);

		TRACE2("Lib comp register '%s'", &req_lib->compName.value);
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
		log_printf (LOG_ERR, "Lib comp register: comp '%s' not found", &req_lib->compName.value);
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

	component = amf_comp_find (&amf_cluster, &req_lib_amf_componentunregister->compName);
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

	comp = amf_comp_find (&amf_cluster, &req_lib->compName);

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

	comp = amf_comp_find (&amf_cluster, &req_lib->compName);
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

	comp = amf_comp_find (&amf_cluster, &req_lib->compName);
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

	comp = amf_comp_find (&amf_cluster, &req_lib->compName);
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

	comp = amf_comp_find (&amf_cluster, &req_lib->erroneousComponent);
	if (comp != NULL) {
		struct req_exec_amf_comp_error_report req_exec;
		struct iovec iovec;

		TRACE2("Lib comp error report for '%s'", &comp->name.value);

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
			&req_lib->erroneousComponent.value);
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

static void message_handler_req_lib_amf_response (void *conn, void *msg)
{
	struct req_lib_amf_response *req_lib = msg;
	int multicast;
	SaAisErrorT retval;

	/*
	* This is an optimisation to avoid multicast of healthchecks while keeping
	* a nice design. We multicast and make lib responses from this file.
	*/
	multicast = amf_comp_response_1 (
		req_lib->invocation, req_lib->error, &retval);

	if (multicast) {
		struct req_exec_amf_response req_exec;
		struct iovec iovec;

		req_exec.header.size = sizeof (struct req_exec_amf_response);
		req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
			MESSAGE_REQ_EXEC_AMF_RESPONSE);
		req_exec.invocation = req_lib->invocation;
		req_exec.error = req_lib->error;
		iovec.iov_base = (char *)&req_exec;
		iovec.iov_len = sizeof (req_exec);
		assert (totempg_groups_mcast_joined (
			openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
	} else {
		struct res_lib_amf_response res_lib;
		res_lib.header.id = MESSAGE_RES_AMF_RESPONSE;
		res_lib.header.size = sizeof (struct res_lib_amf_response);
		res_lib.header.error = retval;
		openais_conn_send_response (conn, &res_lib, sizeof (res_lib));
	}
}

static void amf_dump_fn (void)
{
	amf_runtime_attributes_print (&amf_cluster);
}
