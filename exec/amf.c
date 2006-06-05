/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt
 * Description: - Introduced AMF B.02 information model
 *              - Use DN in API and multicast messages
 *              - (Re-)Introduction of event based multicast messages
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
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "amfconfig.h"
#include "main.h"
#include "service.h"
#include "objdb.h"
#include "print.h"

#define LOG_LEVEL_FROM_LIB LOG_LEVEL_DEBUG
#define LOG_LEVEL_FROM_GMI LOG_LEVEL_DEBUG
#define LOG_LEVEL_ENTER_FUNC LOG_LEVEL_DEBUG

enum amf_message_req_types {
	MESSAGE_REQ_EXEC_AMF_COMPONENT_REGISTER = 0,
	MESSAGE_REQ_EXEC_AMF_COMPONENT_ERROR_REPORT = 1,
	MESSAGE_REQ_EXEC_AMF_CLC_CLEANUP_COMPLETED = 2,
	MESSAGE_REQ_EXEC_AMF_HEALTHCHECK_TMO = 3,
	MESSAGE_REQ_EXEC_AMF_RESPONSE = 4
};

#ifndef HOST_NAME_MAX
# define HOST_NAME_MAX 255
#endif

struct invocation {
	void *data;
	int interface;
	int active;
};

static struct invocation *invocation_entries = 0;

static int invocation_entries_size = 0;
static int waiting = 0;

enum amf_response_interfaces {
	AMF_RESPONSE_HEALTHCHECKCALLBACK = 1,
	AMF_RESPONSE_CSISETCALLBACK = 2,
	AMF_RESPONSE_CSIREMOVECALLBACK = 3,
	AMF_RESPONSE_COMPONENTTERMINATECALLBACK = 4
};

struct component_terminate_callback_data {
	struct amf_comp *comp;
};
	
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
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_comp_error_report (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_clc_cleanup_completed (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_healthcheck_tmo (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_response (
	void *message,
	unsigned int nodeid);

static void comp_presence_state_set (
	struct amf_comp *comp,
	SaAmfPresenceStateT presence_state);

static void comp_operational_state_set (
	struct amf_comp *comp,
	SaAmfOperationalStateT operational_state);

static void su_operational_state_set (
	struct amf_su *unit,
	SaAmfOperationalStateT operational_state);

static void su_presence_state_set (
	struct amf_su *su,
   SaAmfPresenceStateT presence_state);

static void sg_assign_si (struct amf_sg *group);
static int clc_instantiate (struct amf_comp *comp);
#if 0
static int clc_terminate (struct amf_comp *comp);
#endif
static int clc_cli_instantiate (struct amf_comp *comp);
static int clc_instantiate_callback (struct amf_comp *comp);
static int clc_csi_set_callback (struct amf_comp *comp);
static int clc_cli_terminate (struct amf_comp *comp);
static int clc_terminate_callback (struct amf_comp *comp);
static int clc_csi_remove_callback (struct amf_comp *comp);
static int clc_cli_cleanup (struct amf_comp *comp);
static int clc_cli_cleanup_local (struct amf_comp *comp);
static void healthcheck_activate (struct amf_healthcheck *healthcheck_active);
static void healthcheck_deactivate (struct amf_healthcheck *healthcheck_active);
//static void comp_healthcheck_activate (struct amf_comp *comp);
static void comp_healthcheck_deactivate (struct amf_comp *comp);
static void escalation_policy_restart (struct amf_comp *comp);
static void amf_runtime_attributes_print (void);
static void clc_su_instantiate (struct amf_su *unit);
static void cluster_start_applications (void *data);

static char *presence_state_text[] = {
	"UNKNOWN",
	"UNINSTANTIATED",
	"INSTANTIATING",
	"INSTANTIATED",
	"TERMINATING",
	"RESTARTING",
	"INSTANTION_FAILED",
	"TERMINIATION-FAILED"
};

static char *oper_state_text[] = {
	"UNKNOWN",
	"ENABLED",
	"DISABLED"
};

static char *admin_state_text[] = {
	"UNKNOWN",
	"UNLOCKED",
	"LOCKED",
	"LOCKED-INSTANTIATION",
	"SHUTTING-DOWN"
};

static char *readiness_state_text[] = {
	"UNKNOWN",
	"OUT-OF-SERVICE",
	"IN-SERVICE",
};

static char *ha_state_text[] = {
	"UNKNOWN",
	"ACTIVE",
	"STANDBY",
	"QUIESCED",
	"QUIESCING",
};

static char *assignment_state_text[] = {
	"UNKNOWN",
	"UNASSIGNED",
	"FULLY-ASSIGNED",
	"PARTIALLY-ASSIGNED"
};

struct libamf_ci_trackentry {
	int active;
	SaUint8T trackFlags;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaNameT csiName;
};

struct amf_comp;
struct amf_pd {
	struct amf_comp *comp;
	struct list_head list;
/*
	struct libamf_ci_trackentry *tracks;
	int trackEntries;
	int trackActive;
*/
};


struct clc_interface {
	int (*instantiate) (struct amf_comp *comp);
	int (*terminate) (struct amf_comp *comp);
	int (*cleanup) (struct amf_comp *comp);
};

/*
 * Life cycle functions
 */
static struct clc_interface clc_interface_sa_aware = {
	clc_cli_instantiate,
	clc_terminate_callback,
	clc_cli_cleanup
};

static struct clc_interface clc_interface_proxied_pre = {
	clc_instantiate_callback,
	clc_terminate_callback,
	clc_cli_cleanup
};

static struct clc_interface clc_interface_proxied_non_pre = {
	clc_csi_set_callback,
	clc_csi_remove_callback,
	clc_cli_cleanup_local
};

static struct clc_interface clc_interface_non_proxied_non_saware = {
	clc_cli_instantiate,
	clc_cli_terminate,
	clc_cli_cleanup_local
};

static struct clc_interface *clc_interfaces[4] = {
	&clc_interface_sa_aware,
	&clc_interface_proxied_pre,
	&clc_interface_proxied_non_pre,
	&clc_interface_non_proxied_non_saware
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
	.exec_dump_fn		= amf_runtime_attributes_print
};

static struct amf_node *this_amf_node;
static struct amf_cluster amf_cluster;

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

__attribute__ ((constructor)) static void register_this_component (void) {
        lcr_interfaces_set (&openais_amf_ver0[0], &amf_service_handler_iface);

	lcr_component_register (&amf_comp_ver0);
}

enum clc_command_run_operation_type {
	CLC_COMMAND_RUN_OPERATION_TYPE_INSTANTIATE = 1,
	CLC_COMMAND_RUN_OPERATION_TYPE_TERMINATE = 2,
	CLC_COMMAND_RUN_OPERATION_TYPE_CLEANUP = 3
};

struct clc_command_run_data {
	struct amf_comp *comp;
	enum clc_command_run_operation_type type;
	void (*completion_callback) (void *context);
};

struct req_exec_amf_comp_register {
	struct req_header header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct req_exec_amf_comp_error_report {
	struct req_header header;
	SaNameT reportingComponent;
	SaNameT erroneousComponent;
	SaTimeT errorDetectionTime;
	SaAmfRecommendedRecoveryT recommendedRecovery;
	SaNtfIdentifierT ntfIdentifier;
};

struct req_exec_amf_clc_cleanup_completed {
	struct req_header header;
	SaNameT compName;
};

struct req_exec_amf_healthcheck_tmo {
	struct req_header header;
	SaNameT compName;
};

struct req_exec_amf_response {
	struct req_header header;
	SaNameT compName;
    SaNameT csiName;
	unsigned int interface;
	SaAisErrorT error;
};

static char *comp_dn_make (struct amf_comp *comp, SaNameT *name)
{
	int	i = snprintf((char*) name->value, SA_MAX_NAME_LENGTH,
		"safComp=%s,safSu=%s,safSg=%s,safApp=%s",
		comp->name.value, comp->su->name.value,
		comp->su->sg->name.value, comp->su->sg->application->name.value);
	assert (i <= SA_MAX_NAME_LENGTH);
	name->length = i;
	return (char *)name->value;
}

static char *csi_dn_make (struct amf_csi *csi, SaNameT *name)
{
	int	i = snprintf((char*) name->value, SA_MAX_NAME_LENGTH,
		"safCsi=%s,safSi=%s,safApp=%s",
		csi->name.value, csi->si->name.value,
		csi->si->application->name.value);
	assert (i <= SA_MAX_NAME_LENGTH);
	name->length = i;
	return (char *)name->value;
}

static int invocation_create (
	int interface, 
	void *data)
{
	struct invocation *invocation_addr = 0;
	struct invocation *invocation_temp;
	int i;
	int loc = 0;

	for (i = 0; i < invocation_entries_size; i++) {
		if (invocation_entries[i].active == 0) {
			invocation_addr = &invocation_entries[i];
			loc = i;
			break;
		}
	}
	if (invocation_addr == 0) {
		invocation_temp = (struct invocation *)realloc (invocation_entries,
			(invocation_entries_size + 1) * sizeof (struct invocation));
		if (invocation_temp == 0) {
			return (-1);
		}
		invocation_entries = invocation_temp;
		invocation_addr = &invocation_entries[invocation_entries_size];
		loc = invocation_entries_size;
		invocation_entries_size += 1;
	}
	invocation_addr->interface = interface;
	invocation_addr->data = data;
	invocation_addr->active = 1;

	return (loc);
}

static int invocation_get_and_destroy (int invocation, int *interface,
	void **data)
{
	if (invocation > invocation_entries_size) {
		return (-1);
	}
	if (invocation_entries[invocation].active == 0) {
		return (-1);
	}

	*interface = invocation_entries[invocation].interface;
	*data = invocation_entries[invocation].data;
	memset (&invocation_entries[invocation], 0, sizeof (struct invocation));
	
	return (0);
}

static void invocation_destroy_by_data (void *data)
{
	int i;

	for (i = 0; i < invocation_entries_size; i++) {
		if (invocation_entries[i].data == data) {
			memset (&invocation_entries[i], 0,
				sizeof (struct invocation));
			break;
		}
	}
}

static void *clc_command_run (void *context)
{
	struct clc_command_run_data *clc_command_run_data =
		(struct clc_command_run_data *)context;
	pid_t pid;
	int res;
	char *argv[10];
	char *envp[10];
	int status;
	char path[PATH_MAX];
	char *cmd = 0;
	char *comp_argv = 0;
	char comp_name[SA_MAX_NAME_LENGTH];
	int i;

	ENTER_VOID();

	pid = fork();

	if (pid == -1) {
		dprintf ("Couldn't fork process %s\n", strerror (errno));
		return (0);
	}

	if (pid) {
		waiting = 1;
		dprintf ("waiting for pid %d to finish\n", pid);
		waitpid (pid, &status, 0);
		dprintf ("process (%d) finished with %d\n", pid, status);
		if (clc_command_run_data->completion_callback) {
			clc_command_run_data->completion_callback (context);
		}
		pthread_exit(0);
	}

	switch (clc_command_run_data->type) {
		case CLC_COMMAND_RUN_OPERATION_TYPE_INSTANTIATE:
			cmd = clc_command_run_data->comp->saAmfCompInstantiateCmd;
			comp_argv = clc_command_run_data->comp->saAmfCompInstantiateCmdArgv;
			break;

		case CLC_COMMAND_RUN_OPERATION_TYPE_TERMINATE:
			cmd = clc_command_run_data->comp->saAmfCompTerminateCmd;
			comp_argv = clc_command_run_data->comp->saAmfCompTerminateCmdArgv;
			break;

		case CLC_COMMAND_RUN_OPERATION_TYPE_CLEANUP:
			cmd = clc_command_run_data->comp->saAmfCompCleanupCmd;
			comp_argv = clc_command_run_data->comp->saAmfCompCleanupCmdArgv;
			break;
		default:
			assert (0 != 1);
			break;
	}

	/* If command is not an absolute path, search for paths in parent objects */
	if (cmd[0] != '/') {
		if (strlen (clc_command_run_data->comp->clccli_path)) {
			sprintf (path, "%s/%s",
					 clc_command_run_data->comp->clccli_path, cmd);
		} else if (strlen (clc_command_run_data->comp->su->clccli_path)) {
			sprintf (path, "%s/%s",
					 clc_command_run_data->comp->su->clccli_path, cmd);
		} else if (strlen (clc_command_run_data->comp->su->sg->clccli_path)) {
			sprintf (path, "%s/%s",
					 clc_command_run_data->comp->su->sg->clccli_path, cmd);
		} else if (strlen (clc_command_run_data->comp->su->sg->application->clccli_path)) {
			sprintf (path, "%s/%s",
					 clc_command_run_data->comp->su->sg->application->clccli_path, cmd);
		}
		cmd = path;
	}

	argv[0] = cmd;
	{
		/* make a proper argv array */
		i = 1;
        char *ptrptr;
		char *arg = strtok_r(comp_argv, " ", &ptrptr);
		while (arg) {
			argv[i] = arg;
			arg = strtok_r(NULL, " ", & ptrptr);
			i++;
		}
	}
	argv[i] = NULL;

	envp[0] = comp_name;
	i = snprintf(comp_name, SA_MAX_NAME_LENGTH,
				  "SA_AMF_COMPONENT_NAME=safComp=%s,safSu=%s,safSg=%s,safApp=%s",
				  clc_command_run_data->comp->name.value,
				  clc_command_run_data->comp->su->name.value,
				  clc_command_run_data->comp->su->sg->name.value,
				  clc_command_run_data->comp->su->sg->application->name.value);
	assert (i <= SA_MAX_NAME_LENGTH);

	for (i = 1; clc_command_run_data->comp->saAmfCompCmdEnv &&
		   clc_command_run_data->comp->saAmfCompCmdEnv[i - 1]; i++) {
		envp[i] = clc_command_run_data->comp->saAmfCompCmdEnv[i - 1];
	}
	envp[i] = NULL;

	dprintf ("running command '%s' with environment:\n", cmd);
	for (i = 0; envp[i] != NULL; i++) {
		dprintf ("   %s\n", envp[i]);
	}
	dprintf (" and argv:\n", cmd);
	for (i = 0; argv[i] != NULL; i++) {
		dprintf ("   %s\n", argv[i]);
	}
		
	res = execve (cmd, argv, envp);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Couldn't exec program %s (%s)\n",
					cmd, strerror (errno));
	}
	assert (res != -1);
	return (0);
}

/*
 * Instantiate possible operations
 */
static int clc_cli_instantiate (struct amf_comp *comp)
{
	int res;
	pthread_t thread;
	pthread_attr_t thread_attr;	/* thread attribute */

	struct clc_command_run_data *clc_command_run_data;

	ENTER("comp '%s'\n", getSaNameT (&comp->name));

	clc_command_run_data = malloc (sizeof (struct clc_command_run_data));
	if (clc_command_run_data == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}
	clc_command_run_data->comp = comp;
	clc_command_run_data->type = CLC_COMMAND_RUN_OPERATION_TYPE_INSTANTIATE;
	clc_command_run_data->completion_callback = NULL;
	pthread_attr_init (&thread_attr);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	res = pthread_create (&thread, &thread_attr, clc_command_run, (void *)clc_command_run_data);
	if (res != 0) {
		log_printf (LOG_LEVEL_ERROR, "pthread_create failed: %d", res);
	}
// TODO error code from pthread_create
	return (res);
}

static int clc_instantiate_callback (struct amf_comp *comp)
{
	ENTER("comp %s\n", getSaNameT (&comp->name));
	return (0);
}

static int clc_csi_set_callback (struct amf_comp *comp)
{
	ENTER("comp %s\n", getSaNameT (&comp->name));
	return (0);
}

/*
 * Terminate possible operations
 */
static int clc_cli_terminate (struct amf_comp *comp)
{
	ENTER("comp %s\n", getSaNameT (&comp->name));
	return (0);
}

static int clc_terminate_callback (struct amf_comp *comp)
{
	struct res_lib_amf_componentterminatecallback res_lib_amf_componentterminatecallback;
	struct component_terminate_callback_data *component_terminate_callback_data;

	ENTER("comp %s\n", getSaNameT (&comp->name));

	if (comp->saAmfCompPresenceState != SA_AMF_PRESENCE_INSTANTIATED) {
		dprintf ("component terminated but not instantiated %s - %d\n",
			getSaNameT (&comp->name), comp->saAmfCompPresenceState);
		assert (0);
		return (0);
	}

	dprintf ("component name terminating %s\n", getSaNameT (&comp->name));
	dprintf ("component presence state %d\n", comp->saAmfCompPresenceState);

	res_lib_amf_componentterminatecallback.header.id = MESSAGE_RES_AMF_COMPONENTTERMINATECALLBACK;
	res_lib_amf_componentterminatecallback.header.size = sizeof (struct res_lib_amf_componentterminatecallback);
	res_lib_amf_componentterminatecallback.header.error = SA_AIS_OK;


	memcpy (&res_lib_amf_componentterminatecallback.compName,
		&comp->name, sizeof (SaNameT));

	component_terminate_callback_data =
		malloc (sizeof (struct component_terminate_callback_data));
	if (component_terminate_callback_data == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}
	component_terminate_callback_data->comp = comp;

	res_lib_amf_componentterminatecallback.invocation =
		invocation_create (
		AMF_RESPONSE_COMPONENTTERMINATECALLBACK,
		component_terminate_callback_data);
	dprintf ("Creating invocation %llu", 
	(unsigned long long)res_lib_amf_componentterminatecallback.invocation);
				        
	openais_conn_send_response (
		openais_conn_partner_get (comp->conn),
		&res_lib_amf_componentterminatecallback,
		sizeof (struct res_lib_amf_componentterminatecallback));

	return (0);
}

static int clc_csi_remove_callback (struct amf_comp *comp)
{
	dprintf ("clc_tcsi_remove_callback\n");
	return (0);
}

/*
 * Clean up completed
 */
static void clc_cleanup_completion_callback (void *context) {
	struct clc_command_run_data *clc_command_run_data =
		(struct clc_command_run_data *)context;
	struct req_exec_amf_clc_cleanup_completed req;
	struct iovec iovec;

	TRACE2("CLC cleanup done for '%s'", clc_command_run_data->comp->name.value);

	req.header.size = sizeof (struct req_exec_amf_clc_cleanup_completed);
	req.header.id =	SERVICE_ID_MAKE (AMF_SERVICE,
		MESSAGE_REQ_EXEC_AMF_CLC_CLEANUP_COMPLETED);

	comp_dn_make (clc_command_run_data->comp, &req.compName);
	iovec.iov_base = (char *)&req;
	iovec.iov_len = sizeof (req);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
}
			
/*
 * Cleanup possible operations
 */
static int clc_cli_cleanup (struct amf_comp *comp)
{
	int res;
	pthread_t thread;
	pthread_attr_t thread_attr;	/* thread attribute */

	struct clc_command_run_data *clc_command_run_data;

	dprintf ("clc_cli_cleanup\n");
	clc_command_run_data = malloc (sizeof (struct clc_command_run_data));
	if (clc_command_run_data == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}
	clc_command_run_data->comp = comp;
	clc_command_run_data->type = CLC_COMMAND_RUN_OPERATION_TYPE_CLEANUP;
	clc_command_run_data->completion_callback = clc_cleanup_completion_callback;

	pthread_attr_init (&thread_attr);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	res = pthread_create (&thread, &thread_attr, clc_command_run, (void *)clc_command_run_data);
	if (res != 0) {
		log_printf (LOG_LEVEL_ERROR, "pthread_create failed: %d", res);
	}
// TODO error code from pthread_create
	return (res);
}

static int clc_cli_cleanup_local (struct amf_comp *comp)
{
	dprintf ("clc_cli_cleanup_local\n");
	return (0);
}

static int clc_instantiate (struct amf_comp *comp)
{
	int res = 0;

	dprintf ("clc instantiate for comp '%s'\n", getSaNameT (&comp->name));

	if (comp->saAmfCompPresenceState != SA_AMF_PRESENCE_RESTARTING) {
		comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATING);
	}

	if (comp->su->is_local) {
		res = clc_interfaces[comp->comptype]->instantiate (comp);
	}

	return res;
}

#if 0
static int clc_terminate (struct amf_comp *comp)
{
	int res;

	dprintf ("clc terminate for comp %s\n", getSaNameT (&comp->name));
	assert (0);
	operational_state_comp_set (comp, SA_AMF_OPERATIONAL_DISABLED);
	comp_presence_state_set (comp, SA_AMF_PRESENCE_TERMINATING);

	res = clc_interfaces[comp->comptype]->terminate (comp);
	return (0);
}
#endif

static int presence_state_all_comps_in_su_are_set (struct amf_su *su, SaAmfPresenceStateT state)
{
	int all_set = 1;
	struct amf_comp *comp;

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		if (comp->saAmfCompPresenceState != state) {
			all_set = 0;
		}
	}

	return all_set;
}

static int clc_cleanup (struct amf_comp *comp)
{
	int res = 0;
	dprintf ("clc cleanup for comp %s\n", getSaNameT (&comp->name));
	comp_healthcheck_deactivate (comp);
	comp_presence_state_set (comp, SA_AMF_PRESENCE_RESTARTING);

	/* when all components in su are restarting, the SU becomes restarting */
	if (presence_state_all_comps_in_su_are_set(comp->su, SA_AMF_PRESENCE_RESTARTING)) {
		su_presence_state_set (comp->su, SA_AMF_PRESENCE_RESTARTING);
	}

	if (comp->su->is_local) {
		res = clc_interfaces[comp->comptype]->cleanup (comp);
	}

	return res;
}

/* IMPL */

static void amf_runtime_attributes_print (void)
{
	struct amf_node *node;
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;
	struct amf_comp *comp;
	struct amf_si *si;
	struct amf_csi *csi;
	struct amf_si_assignment *si_assignment;
	struct amf_csi_assignment *csi_assignment;

	dprintf("AMF runtime attributes:");
	dprintf("===================================================");
	dprintf("safCluster=%s", getSaNameT(&amf_cluster.name));
	dprintf("  admin state: %s\n", admin_state_text[amf_cluster.saAmfClusterAdminState]);
	for (node = amf_cluster.node_head; node != NULL; node = node->next) {
		dprintf("  safNode=%s\n", getSaNameT (&node->name));
		dprintf("    admin state: %s\n", admin_state_text[node->saAmfNodeAdminState]);
		dprintf("    oper state:  %s\n", oper_state_text[node->saAmfNodeOperState]);
	}
	for (app = amf_cluster.application_head; app != NULL; app = app->next) {
		dprintf("  safApp=%s\n", getSaNameT(&app->name));
		dprintf("    admin state: %s\n", admin_state_text[app->saAmfApplicationAdminState]);
		dprintf("    num_sg:      %d\n", app->saAmfApplicationCurrNumSG);
		for (sg = app->sg_head;	sg != NULL; sg = sg->next) {
			dprintf("    safSG=%s\n", getSaNameT(&sg->name));
			dprintf("      admin state:        %s\n", admin_state_text[sg->saAmfSGAdminState]);
			dprintf("      assigned SUs        %d\n", sg->saAmfSGNumCurrAssignedSUs);
			dprintf("      non inst. spare SUs %d\n", sg->saAmfSGNumCurrNonInstantiatedSpareSUs);
			dprintf("      inst. spare SUs     %d\n", sg->saAmfSGNumCurrInstantiatedSpareSUs);
			for (su = sg->su_head; su != NULL; su = su->next) {
				dprintf("      safSU=%s\n", getSaNameT(&su->name));
				dprintf("        oper state:      %s\n", oper_state_text[su->saAmfSUOperState]);
				dprintf("        admin state:     %s\n", admin_state_text[su->saAmfSUAdminState]);
				dprintf("        readiness state: %s\n", readiness_state_text[su->saAmfSUReadinessState]);
				dprintf("        presence state:  %s\n", presence_state_text[su->saAmfSUPresenceState]);
				dprintf("        hosted by node   %s\n", su->saAmfSUHostedByNode.value);
				dprintf("        num active SIs   %d\n", su->saAmfSUNumCurrActiveSIs);
				dprintf("        num standby SIs  %d\n", su->saAmfSUNumCurrStandbySIs);
				dprintf("        restart count    %d\n", su->saAmfSURestartCount);
				dprintf("        escalation level %d\n", su->escalation_level);
				dprintf("        SU failover cnt  %d\n", su->su_failover_cnt);
				dprintf("        assigned SIs:");
				for (si_assignment = su->assigned_sis; si_assignment != NULL;
					si_assignment = si_assignment->next) {
					dprintf("          safSi=%s\n", si_assignment->si->name.value);
					dprintf("            HA state: %s\n",
						ha_state_text[si_assignment->saAmfSISUHAState]);
				}
				for (comp = su->comp_head; comp != NULL; comp = comp->next) {
					dprintf("        safComp=%s\n", getSaNameT(&comp->name));
					dprintf("          oper state:      %s\n",
						oper_state_text[comp->saAmfCompOperState]);
					dprintf("          readiness state: %s\n",
						readiness_state_text[comp->saAmfCompReadinessState]);
					dprintf("          presence state:  %s\n",
						presence_state_text[comp->saAmfCompPresenceState]);
					dprintf("          num active CSIs  %d\n",
						comp->saAmfCompNumCurrActiveCsi);
					dprintf("          num standby CSIs %d\n",
						comp->saAmfCompNumCurrStandbyCsi);
					dprintf("          restart count    %d\n", comp->saAmfCompRestartCount);
					dprintf("          assigned CSIs:");
					for (csi_assignment = comp->assigned_csis; csi_assignment != NULL;
						csi_assignment = csi_assignment->comp_next) {
						dprintf("            safCSI=%s\n", csi_assignment->csi->name.value);
						dprintf("              HA state: %s\n",
							ha_state_text[csi_assignment->saAmfCSICompHASate]);
					}
				}
			}
		}
		for (si = app->si_head; si != NULL; si = si->next) {
			dprintf("    safSi=%s\n", getSaNameT(&si->name));
			dprintf("      admin state:         %s\n", admin_state_text[si->saAmfSIAdminState]);
			dprintf("      assignm. state:      %s\n", assignment_state_text[si->saAmfSIAssignmentState]);
			dprintf("      active assignments:  %d\n", si->saAmfSINumCurrActiveAssignments);
			dprintf("      standby assignments: %d\n", si->saAmfSINumCurrStandbyAssignments);
			for (csi = si->csi_head; csi != NULL; csi = csi->next) {
				dprintf("      safCsi=%s\n", getSaNameT(&csi->name));
			}
		}
	}
	dprintf("===================================================");
}

/* to be removed... */
static int amf_enabled (struct objdb_iface_ver0 *objdb)
{
	unsigned int object_service_handle;
	char *value;
	int enabled = 0;

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);
	if (objdb->object_find (
			OBJECT_PARENT_HANDLE,
			"amf",
			strlen ("amf"),
			&object_service_handle) == 0) {

		value = NULL;
		if ( !objdb->object_key_get (object_service_handle,
							"mode",
							strlen ("mode"),
							(void *)&value,
							NULL) && value) {

			if (strcmp (value, "enabled") == 0) {
				enabled = 1;
			} else
			if (strcmp (value, "disabled") == 0) {
				enabled = 0;
			}
		}
	}

	return enabled;
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
		this_amf_node->saAmfNodeOperState = SA_AMF_OPERATIONAL_ENABLED;

		/* wait a while before starting applications as the AMF spec. says. */
		poll_timer_add(aisexec_poll_handle,
					   amf_cluster.saAmfClusterStartupTimeout,
					   NULL,
					   cluster_start_applications,
					   &amf_cluster.timeout_handle);
	} else {
		log_printf (LOG_LEVEL_INFO, "This CLM node (%s) is not configured as an AMF node, disabling...", hostname);
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
#ifdef COMPILE_OUT

	int i;

	log_printf (LOG_LEVEL_FROM_GMI, "Executive: amf_confchg_fn : type = %d,mnum = %d,jnum = %d,lnum = %d\n", configuration_type,member_list_entries,joined_list_entries,left_list_entries);

	recovery = 1;
	/*
	 * If node join, component register
	 */
	if ( joined_list_entries > 0 ) {
		enumerate_components (amf_confchg_njoin, NULL);
	}

	/*
	 * If node leave, component unregister
	 */
	for (i = 0; i<left_list_entries ; i++) {
		enumerate_components (amf_confchg_nleave, (void *)&(left_list[i]));
	}

#ifdef TODO
	if (configuration_type == TOTEMPG_CONFIGURATION_REGULAR) {
		totempg_recovery_plug_unplug (amf_recovery_plug_handle);
		recovery = 0;
	}
#endif

#endif
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

static DECLARE_LIST_INIT (library_notification_send_listhead);

// TODO static totempg_recovery_plug_handle amf_recovery_plug_handle;

#ifdef COMPILE_OUT
static void protectiongroup_notifications_send (
	struct amf_comp *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent)
{
	int i;
	struct conn_info *conn_info;
	struct list_head *list;

	log_printf (LOG_LEVEL_ENTER_FUNC, "protectiongroup_notifications_send: sending PGs to API.\n");

	/*
	 * Iterate all tracked connections
	 */
	for (list = library_notification_send_listhead.next;
		list != &library_notification_send_listhead;
		list = list->next) {

		conn_info = list_entry (list, struct conn_info, conn_list);
		for (i = 0; i < conn_info->ais_ci.u.libamf_ci.trackEntries; i++) {
			if (conn_info->ais_ci.u.libamf_ci.tracks[i].active) {

				if (conn_info->ais_ci.u.libamf_ci.tracks[i].csiName.length
				    != changedComponent->amf_pg->name.length) {
					continue;
				}
				if (memcmp (conn_info->ais_ci.u.libamf_ci.tracks[i].csiName.value,
					changedComponent->amf_pg->name.value,
					conn_info->ais_ci.u.libamf_ci.tracks[i].csiName.length)) {
					continue;
				}

#ifdef COMPILE_OUT
				protectiongroup_notification_send (conn_info,
					conn_info->ais_ci.u.libamf_ci.tracks[i].notificationBufferAddress, 
					changedComponent->saAmfProtectionGroup,
					changedComponent,
					changeToComponent,
					conn_info->ais_ci.u.libamf_ci.tracks[i].trackFlags);
#endif

			} /* if track flags active */
		} /* for all track entries */
	} /* for all connection entries */
}
#endif

#ifdef COMPILE_OUT
static int make_protectiongroup_notification_allcomponent (
	struct amf_comp *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent,
	SaAmfProtectionGroupNotificationT **notification )
{
	SaAmfProtectionGroupNotificationT *protectionGroupNotification = 0;
	int notifyEntries = 0;
	struct amf_comp *component;
	struct list_head *AmfGroupList;
	struct list_head *AmfUnitList;
	struct list_head *AmfComponentList;
	struct saAmfGroup *saAmfGroup;
	struct saAmfUnit *AmfUnit;

	for (AmfGroupList = saAmfGroupHead.next; AmfGroupList != &saAmfGroupHead; AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList, struct saAmfGroup, saAmfGroupList);
		/*
		 * Search all units
		 */
		for (AmfUnitList = saAmfGroup->saAmfUnitHead.next;
			AmfUnitList != &saAmfGroup->saAmfUnitHead;
			AmfUnitList = AmfUnitList->next) {

			AmfUnit = list_entry (AmfUnitList, struct saAmfUnit, saAmfUnitList);

			/*
			 * Search all components
			 */
			for (AmfComponentList = AmfUnit->amf_compHead.next;
				AmfComponentList != &AmfUnit->amf_compHead;
				AmfComponentList = AmfComponentList->next) {

				component = list_entry (AmfComponentList, struct amf_comp, amf_compList);

				protectionGroupNotification =
					 (SaAmfProtectionGroupNotificationT *)mempool_realloc (protectionGroupNotification,
						sizeof (SaAmfProtectionGroupNotificationT) * (notifyEntries + 1));
				memset (&protectionGroupNotification[notifyEntries],
						0,sizeof (SaAmfProtectionGroupNotificationT));
				memcpy (&protectionGroupNotification[notifyEntries].member.compName, 
						&component->name, sizeof (SaNameT));
//				memcpy (&protectionGroupNotification[notifyEntries].member.readinessState, 
//						&component->currentReadinessState, sizeof (SaAmfReadinessStateT));
				memcpy (&protectionGroupNotification[notifyEntries].member.haState, 
						&component->currentHAState, sizeof (SaAmfHAStateT));
				if (component == changedComponent) {
					protectionGroupNotification[notifyEntries].change = changeToComponent;
				} else {
					protectionGroupNotification[notifyEntries].change 
							= SA_AMF_PROTECTION_GROUP_NO_CHANGE;
				}
				notifyEntries += 1;
			}
		}
	}

	if (notifyEntries) {
		*notification = protectionGroupNotification;
	}
	return (notifyEntries);
}
#endif

#ifdef COMPILE_OUT
static int make_protectiongroup_notification (
	struct saAmfProtectionGroup *amfProtectionGroup,
	struct amf_comp *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent,
	SaAmfProtectionGroupNotificationT **notification )
{
	struct res_lib_amf_protectiongrouptrackcallback res_lib_amf_protectiongrouptrackcallback;
	int notifyEntries = 0;
	struct amf_comp *component;
	struct list_head *componentList;
	SaAmfProtectionGroupNotificationT *protectionGroupNotification = 0;

	memset (&res_lib_amf_protectiongrouptrackcallback,0,sizeof(res_lib_amf_protectiongrouptrackcallback));
	for (componentList = amfProtectionGroup->saAmfMembersHead.next;
		componentList != &amfProtectionGroup->saAmfMembersHead;
		componentList = componentList->next) {

		component = list_entry (componentList, struct amf_comp, saAmfProtectionGroupList);

		protectionGroupNotification =
			 (SaAmfProtectionGroupNotificationT *)mempool_realloc (protectionGroupNotification,
					sizeof (SaAmfProtectionGroupNotificationT) * (notifyEntries + 1));
		memset (&protectionGroupNotification[notifyEntries],0,sizeof (SaAmfProtectionGroupNotificationT));
		memcpy (&protectionGroupNotification[notifyEntries].member.compName, 
				&component->name, sizeof (SaNameT));
	//	memcpy (&protectionGroupNotification[notifyEntries].member.readinessState, 
	//			&component->currentReadinessState, sizeof (SaAmfReadinessStateT));
		memcpy (&protectionGroupNotification[notifyEntries].member.haState, 
				&component->currentHAState, sizeof (SaAmfHAStateT));
		if (component == changedComponent) {
			protectionGroupNotification[notifyEntries].change = changeToComponent;
		} else {
			protectionGroupNotification[notifyEntries].change = SA_AMF_PROTECTION_GROUP_NO_CHANGE;
		}
		notifyEntries += 1;
	} /* for */

	if (notifyEntries) {
		*notification = protectionGroupNotification;
	}

	return (notifyEntries);
	return (0);
}
#endif

#ifdef COMPILE_OUT
static void protectiongroup_notification_send (struct conn_info *conn_info,
	SaAmfProtectionGroupNotificationT *notificationBufferAddress,
	struct saAmfProtectionGroup *amfProtectionGroup,
	struct amf_comp *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent,
	SaUint8T trackFlags)
{
	//struct res_lib_amf_protectiongrouptrackcallback res_lib_amf_protectiongrouptrackcallback;
	SaAmfProtectionGroupNotificationT *protectionGroupNotification = 0;
	int notifyEntries;

	/*
	 * Step through all components and generate protection group list for csi
	 */
	memset (&res_lib_amf_protectiongrouptrackcallback, 0, sizeof(res_lib_amf_protectiongrouptrackcallback));
	if ( trackFlags == SA_TRACK_CHANGES ) {
		notifyEntries = make_protectiongroup_notification_allcomponent (changedComponent, 
				changeToComponent, &protectionGroupNotification);

	}else if (trackFlags == SA_TRACK_CHANGES_ONLY) {
		notifyEntries = make_protectiongroup_notification (amfProtectionGroup,
				changedComponent, changeToComponent, &protectionGroupNotification );
	}else{
		notifyEntries = 0;
	}

	/*
	 * Send track callback
	 */
	if (notifyEntries) {
		res_lib_amf_protectiongrouptrackcallback.header.size =
			sizeof (struct res_lib_amf_protectiongrouptrackcallback) +
			(notifyEntries * sizeof (SaAmfProtectionGroupNotificationT));
//		res_lib_amf_protectiongrouptrackcallback.header.id = MESSAGE_RES_AMF_PROTECTIONGROUPTRACKCALLBACK;
		res_lib_amf_protectiongrouptrackcallback.header.error = SA_AIS_OK;
		res_lib_amf_protectiongrouptrackcallback.numberOfItems = notifyEntries;
		res_lib_amf_protectiongrouptrackcallback.numberOfMembers = notifyEntries;
		memcpy (&res_lib_amf_protectiongrouptrackcallback.csiName,
			&amfProtectionGroup->name, sizeof (SaNameT));

		res_lib_amf_protectiongrouptrackcallback.notificationBufferAddress = notificationBufferAddress;
		openais_conn_send_response (conno, &res_lib_amf_protectiongrouptrackcallback,
			sizeof (struct res_lib_amf_protectiongrouptrackcallback));

		openais_conn_send_response (conno, protectionGroupNotification,
			sizeof (SaAmfProtectionGroupNotificationT) * notifyEntries);

		mempool_free (protectionGroupNotification);
	}
}

#endif

#define INVOCATION_DONT_COMPARE 0xFFFFFFFFULL
#if 0
static struct healthcheck_active *find_healthcheck_active (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *key,
	SaAmfHealthcheckInvocationT invocation)
{
	struct list_head *list;
	struct healthcheck_active *ret_healthcheck_active = 0;
	struct healthcheck_active *healthcheck_active;

	for (list = comp->healthcheck_head.next;
		list != &comp->healthcheck_head;
		list = list->next) {

		healthcheck_active = list_entry (list,
			struct healthcheck_active, list);

		if ((memcmp (key, &healthcheck_active->key,
			sizeof (SaAmfHealthcheckKeyT)) == 0) &&

			(invocation == INVOCATION_DONT_COMPARE ||
			 healthcheck_active->invocationType == invocation)) {

			ret_healthcheck_active = healthcheck_active;
			break;
		}
	}
	return (ret_healthcheck_active);
}

static void comp_healthcheck_activate (struct amf_comp *comp)
{
	struct amf_healthcheck *healthcheck;

	if (!comp->is_local)
		return;

	for (healthcheck = comp->healthcheck_head;
		healthcheck != NULL;
		healthcheck = healthcheck->next) {

		if (healthcheck->active == 0) {
			healthcheck_activate (healthcheck);
		}
	}
}
#endif

static void comp_healthcheck_deactivate (
	struct amf_comp *comp)
{
	struct amf_healthcheck *healthcheck;

	if (!comp->su->is_local)
		return;

	ENTER ("'%s'\n", getSaNameT (&comp->name));

	for (healthcheck = comp->healthcheck_head;
		  healthcheck != NULL;
		  healthcheck = healthcheck->next) {

		if (healthcheck->active) {
			healthcheck_deactivate (healthcheck);
		}
	}
}

static void comp_presence_state_set (struct amf_comp *comp,
	SaAmfPresenceStateT presence_state)
{
	comp->saAmfCompPresenceState = presence_state;
	TRACE1 ("Setting comp '%s' presence state: %s\n",
			comp->name.value, presence_state_text[comp->saAmfCompPresenceState]);

	/*
	 * If all comp presence states are INSTANTIATED, then SU should be instantated
	 */
	if ((presence_state == SA_AMF_PRESENCE_INSTANTIATED) &&
		presence_state_all_comps_in_su_are_set (comp->su, SA_AMF_PRESENCE_INSTANTIATED)) {

		su_presence_state_set (comp->su, SA_AMF_PRESENCE_INSTANTIATED);
	}
}

static void comp_readiness_state_set (struct amf_comp *comp)
{
	/*
	 * Set component readiness state appropriately
	 * if unit in service and component is enabled, it is in service
	 * otherwise it is out of service page 50 B.02.01
	 */
	if (comp->su->saAmfSUReadinessState == SA_AMF_READINESS_IN_SERVICE &&
		comp->saAmfCompOperState == SA_AMF_OPERATIONAL_ENABLED) {
		comp->saAmfCompReadinessState = SA_AMF_READINESS_IN_SERVICE;
	} else if (comp->su->saAmfSUReadinessState == SA_AMF_READINESS_STOPPING && 
		comp->saAmfCompOperState == SA_AMF_OPERATIONAL_ENABLED) {
		comp->saAmfCompReadinessState = SA_AMF_READINESS_STOPPING;
	} else {
		comp->saAmfCompReadinessState = SA_AMF_READINESS_OUT_OF_SERVICE;
	}
	TRACE1 ("Setting comp '%s' readiness state: %s\n",
		comp->name.value, readiness_state_text[comp->saAmfCompReadinessState]);
}

static void comp_operational_state_set (struct amf_comp *comp,
										SaAmfOperationalStateT oper_state)
{
	struct amf_comp *comp_compare;
	int all_set;

	comp->saAmfCompOperState = oper_state;
	TRACE1 ("Setting comp '%s' operational state: %s\n",
			comp->name.value, oper_state_text[comp->saAmfCompOperState]);

	/*
	 * If all operational states are ENABLED, then SU should be ENABLED
	 */
	if (oper_state == SA_AMF_OPERATIONAL_ENABLED) {
		all_set = 1;
		for (comp_compare = comp->su->comp_head; comp_compare != NULL; comp_compare = comp->next) {
			if (comp_compare->saAmfCompOperState != SA_AMF_OPERATIONAL_ENABLED) {
				all_set = 0;
				break;
			}
		}
		if (all_set) {
			su_operational_state_set (comp->su, SA_AMF_OPERATIONAL_ENABLED);
		} else {
			su_operational_state_set (comp->su, SA_AMF_OPERATIONAL_DISABLED);
		}
	}
}

static void comp_csi_set_callback (
	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment)
{
	struct res_lib_amf_csisetcallback* res_lib_amf_csisetcallback;     
	void*  p;
	struct amf_csi_attribute *attribute;
	size_t char_length_of_csi_attrs=0;
	size_t num_of_csi_attrs=0;
	int i;
	struct amf_csi *csi = csi_assignment->csi;
	int ha_state = csi_assignment->saAmfCSICompHASate;

	if (!comp->su->is_local)
		return;

	dprintf("\t   Assigning CSI '%s' state %s to comp '%s'\n",
		getSaNameT (&csi->name), ha_state_text[ha_state], comp->name.value);

	for (attribute = csi->attributes_head;
		 attribute != NULL;
		 attribute = attribute->next) {
		for (i = 0; attribute->value[i] != NULL; i++) {
			num_of_csi_attrs++;
			char_length_of_csi_attrs += strlen(attribute->name);
			char_length_of_csi_attrs += strlen(attribute->value[i]);
			char_length_of_csi_attrs += 2;
		}
	}
	p = malloc(sizeof(struct res_lib_amf_csisetcallback)+
			   char_length_of_csi_attrs);
	if (p == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	res_lib_amf_csisetcallback = (struct res_lib_amf_csisetcallback*)p;

	/* Address of the buffer containing the Csi name value pair  */
	char* csi_attribute_buf = res_lib_amf_csisetcallback->csi_attr_buf;

	/* Byteoffset start at the zero byte  */
	unsigned int byte_offset = 0;

	for (attribute = csi->attributes_head;
		 attribute != NULL;
		 attribute = attribute->next) {

		for (i = 0; attribute->value[i] != NULL; i++) {
			strcpy(&csi_attribute_buf[byte_offset], (char*)attribute->name);
			byte_offset += strlen(attribute->name) + 1;
			strcpy(&csi_attribute_buf[byte_offset], (char*)attribute->value[i]);
			byte_offset += strlen(attribute->value[i]) + 1;
		}
	}

	res_lib_amf_csisetcallback->number = num_of_csi_attrs;
	res_lib_amf_csisetcallback->csiFlags = SA_AMF_CSI_ADD_ONE;  

	switch (ha_state) {
		case SA_AMF_HA_ACTIVE: {
				res_lib_amf_csisetcallback->csiStateDescriptor.activeDescriptor.activeCompName.length = 0;
				res_lib_amf_csisetcallback->csiStateDescriptor.activeDescriptor.transitionDescriptor =
					SA_AMF_CSI_NEW_ASSIGN; 
				break;
			}
		case SA_AMF_HA_STANDBY: {
				res_lib_amf_csisetcallback->csiStateDescriptor.standbyDescriptor.activeCompName.length = 0; 
				res_lib_amf_csisetcallback->csiStateDescriptor.standbyDescriptor.standbyRank =  1;
				break;
			}
		case SA_AMF_HA_QUIESCED: {
				/*TODO*/
				break;
			}
		case SA_AMF_HA_QUIESCING: {
				/*TODO*/
				break;
			}
		default: {
				assert(SA_AMF_HA_ACTIVE||SA_AMF_HA_STANDBY||SA_AMF_HA_QUIESCING||SA_AMF_HA_QUIESCED);         
				break;
			}
	}
	 
	res_lib_amf_csisetcallback->header.id = MESSAGE_RES_AMF_CSISETCALLBACK;
	res_lib_amf_csisetcallback->header.size = 
		sizeof (struct res_lib_amf_csisetcallback)+
		char_length_of_csi_attrs;
	res_lib_amf_csisetcallback->header.error = SA_AIS_OK;

	comp_dn_make (comp, &res_lib_amf_csisetcallback->compName);
	csi_dn_make (csi, &res_lib_amf_csisetcallback->csiName);

	res_lib_amf_csisetcallback->haState = ha_state;
	res_lib_amf_csisetcallback->invocation =
		invocation_create (AMF_RESPONSE_CSISETCALLBACK, csi_assignment);

	openais_conn_send_response (openais_conn_partner_get (comp->conn),
		res_lib_amf_csisetcallback,
		res_lib_amf_csisetcallback->header.size);

	free(p);
}
#if 0
static void pg_create (struct amf_si *si, struct amf_pg **pg_out)
{
	struct amf_pg *pg;
//	struct amf_pg_comp *pg_comp;

	pg = malloc (sizeof (struct amf_pg));
	assert (pg);
	list_init (&pg->pg_comp_head);
	list_init (&pg->pg_list);
	list_add (&pg->pg_list, &si->pg_head);
	*pg_out = pg;
}
#endif

#if 0
static void csi_comp_remove_callback (struct amf_comp *comp, struct amf_csi *csi)
{
	struct res_lib_amf_csiremovecallback res_lib_amf_csiremovecallback;
	struct csi_remove_callback_data *csi_remove_callback_data;

	dprintf ("\t%s\n",
		getSaNameT (&comp->name));

	res_lib_amf_csiremovecallback.header.id = MESSAGE_RES_AMF_CSIREMOVECALLBACK;
	res_lib_amf_csiremovecallback.header.size = sizeof (struct res_lib_amf_csiremovecallback);
	res_lib_amf_csiremovecallback.header.error = SA_AIS_OK;

	csi_remove_callback_data = malloc (sizeof (struct csi_remove_callback_data));
	assert (csi_remove_callback_data); // TODO failure here of malloc
	csi_remove_callback_data->csi = csi;

	res_lib_amf_csiremovecallback.invocation =
		invocation_create (
		AMF_RESPONSE_CSIREMOVECALLBACK,
		csi_remove_callback_data);

	memcpy (&res_lib_amf_csiremovecallback.compName,
		&comp->name, sizeof (SaNameT));

	memcpy (&res_lib_amf_csiremovecallback.csiName,
		&csi->name, sizeof (SaNameT));

	res_lib_amf_csiremovecallback.csiFlags = 0;
				        
	openais_conn_send_response (
		openais_conn_partner_get (comp->conn),
		&res_lib_amf_csiremovecallback,
		sizeof (struct res_lib_amf_csiremovecallback));

}
#endif

static void cluster_assign_workload (void *data)
{
	struct amf_application *app;
	struct amf_sg *sg;

	dprintf("2nd Cluster start timer expired, assigning SIs\n");

	for (app = amf_cluster.application_head; app != NULL; app = app->next) {
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			sg_assign_si (sg);
		}
	}
}

static void cluster_start_applications (void *data)
{
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;

	dprintf("1st Cluster start timer expired, instantiating SUs");

	for (app = amf_cluster.application_head; app != NULL; app = app->next) {
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			for (su = sg->su_head; su != NULL; su = su->next) {
				/* Only start SUs configured to this node */
				if (name_match (&this_amf_node->name,
								&su->saAmfSUHostedByNode )) {
					su->is_local = 1;
					clc_su_instantiate (su);
				}
			}
		}
	}

	/* wait a while before assigning SIs as the AMF spec. says. */
	poll_timer_add(aisexec_poll_handle,
				   amf_cluster.saAmfClusterStartupTimeout,
				   NULL,
				   cluster_assign_workload,
				   &amf_cluster.timeout_handle);
}

#if 0
static void comp_terminate (struct amf_comp *comp)
{
	clc_terminate (comp);
}

static void unit_terminate (struct amf_su *unit)
{
	struct amf_comp *comp;

	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		clc_terminate (comp);
	}
}
#endif

static void comp_cleanup (struct amf_comp *comp)
{
	clc_cleanup (comp);
}

static void su_cleanup (struct amf_su *unit)
{
	struct amf_comp *comp;

	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		clc_cleanup (comp);
	}
}
	
static void comp_restart (struct amf_comp *comp)
{
	comp_presence_state_set (comp, SA_AMF_PRESENCE_RESTARTING);
}

static void comp_reassign_csis (struct amf_comp *comp)
{
	struct amf_csi_assignment *csi_assignment = comp->assigned_csis;

	ENTER ("'%s'", comp->name.value);

	for (; csi_assignment; csi_assignment = csi_assignment->comp_next) {
		comp_csi_set_callback (comp, csi_assignment);
	}
}

#if 0
static void unit_restart (struct amf_su *unit)
{
	struct amf_comp *comp;

	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		comp_presence_state_set (comp, SA_AMF_PRESENCE_RESTARTING);
	}
}
#endif

static void clc_su_instantiate (struct amf_su *unit)
{
	struct amf_comp *comp;

	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		clc_instantiate (comp);
	}
}

static void comp_assign_csi (struct amf_comp *comp, struct amf_csi *csi,
							 SaAmfHAStateT ha_state)
{
	struct amf_csi_assignment *csi_assignment;

	dprintf ("  Assigning CSI '%s' to comp '%s' with hastate %s\n",
			 getSaNameT (&csi->name), getSaNameT (&comp->name),
			 ha_state_text[ha_state]);

	csi_assignment = malloc (sizeof (struct amf_csi_assignment));
	if (csi_assignment == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	csi_assignment->comp_next = comp->assigned_csis;
	comp->assigned_csis = csi_assignment;
	setSaNameT (&csi_assignment->name, (char*)comp->name.value);
	csi_assignment->saAmfCSICompHASate = ha_state;
	csi_assignment->csi = csi;
	csi_assignment->comp = comp;
	csi_assignment->saAmfCSICompHASate = ha_state;

	if (ha_state == SA_AMF_HA_ACTIVE)
		comp->saAmfCompNumCurrActiveCsi++;
	else if (ha_state == SA_AMF_HA_STANDBY)
		comp->saAmfCompNumCurrStandbyCsi++;
	else
		assert (0);

	comp_csi_set_callback (comp, csi_assignment);
}

static void su_assign_si (struct amf_su *su, struct amf_si *si,
		SaAmfHAStateT ha_state)
{
	struct amf_si_assignment *si_assignment;

	dprintf ("Assigning SI '%s' to SU '%s' with hastate %s\n",
			 getSaNameT (&si->name), getSaNameT (&su->name),
			 ha_state_text[ha_state]);

	si_assignment = malloc (sizeof (struct amf_si_assignment));
	if (si_assignment == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}
	setSaNameT (&si_assignment->name, (char*)su->name.value);
	si_assignment->saAmfSISUHAState = ha_state;
	si_assignment->next = su->assigned_sis;
	su->assigned_sis = si_assignment;
	si_assignment->si = si;

	if (ha_state == SA_AMF_HA_ACTIVE) {
		si->saAmfSINumCurrActiveAssignments++;
		su->saAmfSUNumCurrActiveSIs++;
	} else if (ha_state == SA_AMF_HA_STANDBY) {
		su->saAmfSUNumCurrStandbySIs++;
		si->saAmfSINumCurrStandbyAssignments++;
	} else
		assert(0);

	if ((si->saAmfSINumCurrActiveAssignments == si->saAmfSIPrefActiveAssignments) &&
		(si->saAmfSINumCurrStandbyAssignments == si->saAmfSIPrefStandbyAssignments)) {
		si->saAmfSIAssignmentState = SA_AMF_ASSIGNMENT_FULLY_ASSIGNED;
	} else if ((si->saAmfSINumCurrActiveAssignments < si->saAmfSIPrefActiveAssignments) ||
			   (si->saAmfSINumCurrStandbyAssignments < si->saAmfSIPrefStandbyAssignments)) {
		si->saAmfSIAssignmentState = SA_AMF_ASSIGNMENT_PARTIALLY_ASSIGNED;
	}

	{
		struct amf_csi *csi;
		struct amf_comp *comp;
		SaNameT *cs_type;
		int i;

		/*
		** for each component in SU, find a CSI in the SI with the same type
		*/
		for (comp = su->comp_head; comp != NULL; comp = comp->next) {
			int no_of_cs_types = 0;
			for (i = 0; comp->saAmfCompCsTypes[i]; i++) {
				cs_type = comp->saAmfCompCsTypes[i];
				no_of_cs_types++;
				int no_of_assignments = 0;

				for (csi = si->csi_head; csi != NULL; csi = csi->next) {
					if (!memcmp(csi->saAmfCSTypeName.value, cs_type->value, cs_type->length)) {
						comp_assign_csi (comp, csi, ha_state);
						no_of_assignments++;
					}
				}
				if (no_of_assignments == 0) {
					log_printf (LOG_WARNING, "\t   No CSIs of type %s configured?!!\n",
						getSaNameT (cs_type));
				}
			}
			if (no_of_cs_types == 0) {
				log_printf (LOG_LEVEL_ERROR, "\t   No CS types configured for comp %s ?!!\n",
					getSaNameT (&comp->name));
			}
		}
	}
}

static int su_inservice_count_get (struct amf_sg *sg)
{
	struct amf_su *unit;
	int answer = 0;

	for (unit = sg->su_head; unit != NULL; unit = unit->next) {
		if (unit->saAmfSUReadinessState == SA_AMF_READINESS_IN_SERVICE) {
			answer += 1;
		}
	}
	return (answer);
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

static int application_si_count_get (struct amf_application *app)
{
	struct amf_si *si;
	int answer = 0;

	for (si = app->si_head; si != NULL; si = si->next) {
		answer += 1;
	}
	return (answer);
}


static inline int div_round (int a, int b)
{
	int res;
	
	res = a / b;
	if ((a % b) != 0)
		res++;
	return res;
}


static void sg_assign_nm_active (struct amf_sg *sg, int su_units_assign)
{
	struct amf_su *unit;
	struct amf_si *si;
	int assigned = 0;
	int assign_per_su = 0;
	int total_assigned = 0;

	assign_per_su = application_si_count_get (sg->application);
	assign_per_su = div_round (assign_per_su, su_units_assign);
	if (assign_per_su > sg->saAmfSGMaxActiveSIsperSUs) {
		assign_per_su = sg->saAmfSGMaxActiveSIsperSUs;
	}

	si = sg->application->si_head;
	unit = sg->su_head;
	while (unit != NULL) {
		if (unit->saAmfSUReadinessState != SA_AMF_READINESS_IN_SERVICE ||
			unit->saAmfSUNumCurrActiveSIs == sg->saAmfSGMaxActiveSIsperSUs ||
			unit->saAmfSUNumCurrStandbySIs > 0) {

			unit = unit->next;
			continue; /* Not in service */
		}

		assigned = 0;
		while (si != NULL &&
			assigned < assign_per_su &&
			total_assigned < application_si_count_get (sg->application)) {

			assigned += 1;
			total_assigned += 1;
			su_assign_si (unit, si, SA_AMF_HA_ACTIVE);
			si = si->next;
		}
		unit = unit->next;
	}
	
	if (total_assigned == 0) {
		dprintf ("Error: No SIs assigned!");
	}
}

static void sg_assign_nm_standby (struct amf_sg *sg, int units_assign_standby)
{
	struct amf_su *unit;
	struct amf_si *si;
	int assigned = 0;
	int assign_per_su = 0;
	int total_assigned = 0;

	if (units_assign_standby == 0) {
		return;
	}
	assign_per_su = application_si_count_get (sg->application);
	assign_per_su = div_round (assign_per_su, units_assign_standby);
	if (assign_per_su > sg->saAmfSGMaxStandbySIsperSUs) {
		assign_per_su = sg->saAmfSGMaxStandbySIsperSUs;
	}

	si = sg->application->si_head;
	unit = sg->su_head;
	while (unit != NULL) {
		if (unit->saAmfSUReadinessState != SA_AMF_READINESS_IN_SERVICE ||
			unit->saAmfSUNumCurrActiveSIs > 0 ||
			unit->saAmfSUNumCurrStandbySIs == sg->saAmfSGMaxStandbySIsperSUs) {

			unit = unit->next;
			continue; /* Not available for assignment */
		}

		assigned = 0;
		while (si != NULL && assigned < assign_per_su) {
			assigned += 1;
			total_assigned += 1;
			su_assign_si (unit, si, SA_AMF_HA_STANDBY);
			si = si->next;
		}
		unit = unit->next;
	}
	if (total_assigned == 0) {
		dprintf ("Error: No SIs assigned!");
	}
}
#if 0
static void assign_nm_spare (struct amf_sg *sg)
{
	struct amf_su *unit;

	for (unit = sg->su_head; unit != NULL; unit = unit->next) {
		if (unit->saAmfSUReadinessState == SA_AMF_READINESS_IN_SERVICE &&
			(unit->requested_ha_state != SA_AMF_HA_ACTIVE &&
			unit->requested_ha_state != SA_AMF_HA_STANDBY)) {

			dprintf ("Assigning to SU %s with SPARE\n",
				getSaNameT (&unit->name));
		}
	}
}
#endif

static void sg_assign_si (struct amf_sg *sg)
{
	int active_sus_needed;
	int standby_sus_needed;
	int inservice_count;
	int units_for_standby;
	int units_for_active;
	int ii_spare;
	int su_active_assign;
	int su_standby_assign;
	int su_spare_assign;

	ENTER ("'%s'", sg->name.value);
	/*
	 * Number of SUs to assign to active or standby state
	 */
	inservice_count = (float)su_inservice_count_get (sg);

	active_sus_needed = div_round (application_si_count_get (sg->application),
		sg->saAmfSGMaxActiveSIsperSUs);

	standby_sus_needed = div_round (application_si_count_get (sg->application),
		sg->saAmfSGMaxStandbySIsperSUs);

	units_for_active = inservice_count - sg->saAmfSGNumPrefStandbySUs;
	if (units_for_active < 0) {
		units_for_active = 0;
	}

	units_for_standby = inservice_count - sg->saAmfSGNumPrefActiveSUs;
	if (units_for_standby < 0) {
		units_for_standby = 0;
	}

	ii_spare = inservice_count - sg->saAmfSGNumPrefActiveSUs - sg->saAmfSGNumPrefStandbySUs;
	if (ii_spare < 0) {
		ii_spare = 0;
	}

	/*
	 * Determine number of active and standby service units
	 * to assign based upon reduction procedure
	 */
	if ((inservice_count - active_sus_needed) < 0) {
		dprintf ("assignment VI - partial assignment with SIs drop outs\n");

		su_active_assign = active_sus_needed;
		su_standby_assign = 0;
		su_spare_assign = 0;
	} else
	if ((inservice_count - active_sus_needed - standby_sus_needed) < 0) {
		dprintf ("assignment V - partial assignment with reduction of standby units\n");

		su_active_assign = active_sus_needed;
		if (standby_sus_needed > units_for_standby) {
			su_standby_assign = units_for_standby;
		} else {
			su_standby_assign = standby_sus_needed;
		}
		su_spare_assign = 0;
	} else
	if ((sg->saAmfSGMaxStandbySIsperSUs * units_for_standby) <= application_si_count_get (sg->application)) {
		dprintf ("IV: full assignment with reduction of active service units\n");
		su_active_assign = inservice_count - standby_sus_needed;
		su_standby_assign = standby_sus_needed;
		su_spare_assign = 0;
	} else 
	if ((sg->saAmfSGMaxActiveSIsperSUs * units_for_active) <= application_si_count_get (sg->application)) {

		dprintf ("III: full assignment with reduction of standby service units\n");
		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = units_for_standby;
		su_spare_assign = 0;
	} else
	if (ii_spare == 0) {
		dprintf ("II: full assignment with spare reduction\n");

		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = sg->saAmfSGNumPrefStandbySUs;
		su_spare_assign = 0;
	} else {
		dprintf ("I: full assignment with spares\n");

		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = sg->saAmfSGNumPrefStandbySUs;
		su_spare_assign = ii_spare;
	}

	dprintf ("(inservice=%d) (assigning active=%d) (assigning standby=%d) (assigning spares=%d)\n",
		inservice_count, su_active_assign, su_standby_assign, su_spare_assign);
	sg_assign_nm_active (sg, su_active_assign);
	sg_assign_nm_standby (sg, su_standby_assign);
	LEAVE ("'%s'", sg->name.value);
}

static int sg_all_su_in_service(struct amf_sg *sg)
{
	struct amf_su   *su;
	struct amf_comp *comp;
	int ready = 1;

	for (su = sg->su_head; su != NULL; su = su->next) {
		for (comp = su->comp_head; comp != NULL; comp = comp->next) {
			if (su->saAmfSUReadinessState != SA_AMF_READINESS_IN_SERVICE) {
				ready = 0;
			}
		}
	}

	return ready;
}

static void su_readiness_state_set (struct amf_su *su,
									SaAmfReadinessStateT readiness_state)
{
	su->saAmfSUReadinessState = readiness_state;
	TRACE1 ("Setting SU '%s' readiness state: %s\n",
			&su->name.value, readiness_state_text[readiness_state]);
}

static void su_presence_state_set (struct amf_su *su,
								   SaAmfPresenceStateT presence_state)
{
	su->saAmfSUPresenceState = presence_state;
	TRACE1 ("Setting SU '%s' presence state: %s\n",
			su->name.value, presence_state_text[presence_state]);
}


static void escalation_policy_restart (struct amf_comp *comp)
{
	dprintf ("escalation_policy_restart %d\n", comp->su->escalation_level);
	dprintf ("escalation policy restart uninsint %p\n", comp);
//	comp_presence_state_set (comp, SA_AMF_PRESENCE_UNINSTANTIATED);
//	comp_operational_state_set (comp, SA_AMF_OPERATIONAL_DISABLED);
	switch (comp->su->escalation_level) {

	case ESCALATION_LEVEL_NO_ESCALATION:
		comp_restart (comp);
		break;

	case ESCALATION_LEVEL_ONE:
		comp_restart (comp);
		break;

	case ESCALATION_LEVEL_TWO:
		break;

	case ESCALATION_LEVEL_THREE:
		break;

	}
}

static void escalation_policy_cleanup (struct amf_comp *comp)
{
	
//	escalation_timer_start (comp);

	switch (comp->su->escalation_level) {
	case ESCALATION_LEVEL_NO_ESCALATION:
		comp->saAmfCompRestartCount += 1;
		if (comp->saAmfCompRestartCount >= comp->su->sg->saAmfSGCompRestartMax) {
			comp->su->escalation_level = ESCALATION_LEVEL_ONE;
			escalation_policy_cleanup (comp);
			comp->saAmfCompRestartCount = 0;
			return;
		}
		dprintf ("Escalation level 0 - restart component\n");
		dprintf ("Cleaning up and restarting component.\n");
		comp_cleanup (comp);
		break;

	case ESCALATION_LEVEL_ONE:
		comp->su->saAmfSURestartCount += 1;
		if (comp->su->saAmfSURestartCount >= comp->su->sg->saAmfSGSuRestartMax) {
			comp->su->escalation_level = ESCALATION_LEVEL_TWO;
			escalation_policy_cleanup (comp);
			comp->saAmfCompRestartCount = 0;
			comp->su->saAmfSURestartCount = 0;
			return;
		}
		dprintf ("Escalation level 1 - restart unit\n");
		dprintf ("Cleaning up and restarting unit.\n");
		su_cleanup (comp->su);
		break;

	case ESCALATION_LEVEL_TWO:
		dprintf ("Escalation level TWO\n");
		su_cleanup (comp->su);
//		unit_terminate_failover (comp);
		break;

	case ESCALATION_LEVEL_THREE:
//TODO
		break;
	}
}

static void timer_function_healthcheck_timeout (
	void *data)
{
	struct req_exec_amf_healthcheck_tmo req_exec;
	struct iovec iovec;
	struct amf_healthcheck *healthcheck = (struct amf_healthcheck *)data;

	TRACE2 ("timeout occured on healthcheck for component %s.\n",
		getSaNameT (&healthcheck->comp->name));
	req_exec.header.size = sizeof (struct req_exec_amf_healthcheck_tmo);
	req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
		MESSAGE_REQ_EXEC_AMF_HEALTHCHECK_TMO);

	comp_dn_make (healthcheck->comp, &req_exec.compName);
	iovec.iov_base = (char *)&req_exec;
	iovec.iov_len = sizeof (req_exec);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
}

static void healthcheck_activate (struct amf_healthcheck *healthcheck_active)
{
	struct res_lib_amf_healthcheckcallback res_lib_amf_healthcheckcallback;

	healthcheck_active->active = 1;

// TODO	memset (&res_lib_amf_healthcheckcallback, 0, sizeof(res_lib_amf_healthcheckcallback));
	res_lib_amf_healthcheckcallback.header.id = MESSAGE_RES_AMF_HEALTHCHECKCALLBACK;
	res_lib_amf_healthcheckcallback.header.size = sizeof (struct res_lib_amf_healthcheckcallback);
	res_lib_amf_healthcheckcallback.header.error = SA_AIS_OK;

	res_lib_amf_healthcheckcallback.invocation =
		invocation_create (
		AMF_RESPONSE_HEALTHCHECKCALLBACK,
		(void *)healthcheck_active);

	comp_dn_make (healthcheck_active->comp, &res_lib_amf_healthcheckcallback.compName);
	memcpy (&res_lib_amf_healthcheckcallback.key,
		&healthcheck_active->safHealthcheckKey,
		sizeof (SaAmfHealthcheckKeyT));

	TRACE8 ("sending healthcheck to component %s",
			res_lib_amf_healthcheckcallback.compName.value);
	openais_conn_send_response (
		openais_conn_partner_get (healthcheck_active->comp->conn),
		&res_lib_amf_healthcheckcallback,
		sizeof (struct res_lib_amf_healthcheckcallback));

	poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_handle_duration);

	poll_timer_add (aisexec_poll_handle,
		healthcheck_active->saAmfHealthcheckMaxDuration,
		(void *)healthcheck_active,
		timer_function_healthcheck_timeout,
		&healthcheck_active->timer_handle_duration);
}

static void healthcheck_deactivate (struct amf_healthcheck *healthcheck_active)
{
	dprintf ("deactivating healthcheck for component %s\n",
             getSaNameT (&healthcheck_active->comp->name));

    poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_handle_period);

	poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_handle_duration);

	invocation_destroy_by_data ((void *)healthcheck_active);
	healthcheck_active->active = 0;
}


static void timer_function_healthcheck_next_fn (void *data)
{
	healthcheck_activate (data);
}

static  void su_operational_state_set (
	struct amf_su *unit,
	SaAmfOperationalStateT oper_state)
{
	struct amf_comp* comp;

	if (oper_state == unit->saAmfSUOperState) {
		log_printf (LOG_INFO,
					"Not assigning service unit new operational state - same state\n");
		return;
	}

	unit->saAmfSUOperState = oper_state;
	TRACE1 ("Setting SU '%s' operational state: %s\n",
			unit->name.value, oper_state_text[oper_state]);

	if (oper_state == SA_AMF_OPERATIONAL_ENABLED) {
		su_readiness_state_set (unit, SA_AMF_READINESS_IN_SERVICE);
		for (comp = unit->comp_head; comp; comp = comp->next) {
			comp_readiness_state_set (comp);
		}

		if (sg_all_su_in_service(unit->sg)) {
			TRACE1 ("All SUs in SG '%s' in service, assigning SIs\n", unit->sg->name.value);
			sg_assign_si (unit->sg);
			if (amf_cluster.timeout_handle) {
				poll_timer_delete (aisexec_poll_handle, amf_cluster.timeout_handle);
			}
		}
	} else if (oper_state == SA_AMF_OPERATIONAL_DISABLED) {
		su_readiness_state_set (unit, SA_AMF_READINESS_OUT_OF_SERVICE);
	}
}

/*
 * Executive Message Implementation 
 */
static void message_handler_req_exec_amf_comp_register (
	void *message, unsigned int nodeid)
{
	struct res_lib_amf_componentregister res_lib;
	struct req_exec_amf_comp_register *req_exec = message;
	struct amf_comp *comp;

	comp = amf_find_comp (&amf_cluster, &req_exec->compName);
	if (comp == NULL) {
		log_printf (LOG_ERR, "Component '%s' not found", &req_exec->compName.value);
		return;
	}

	TRACE2("Exec comp register '%s'", &req_exec->compName.value);

	if (comp->saAmfCompPresenceState == SA_AMF_PRESENCE_RESTARTING) {
		comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATED);
		comp_readiness_state_set (comp);
		if (comp->saAmfCompReadinessState == SA_AMF_READINESS_IN_SERVICE) {
			comp_reassign_csis (comp);
		}
	} else if ((comp->saAmfCompPresenceState == SA_AMF_PRESENCE_INSTANTIATING) ||
			   (comp->saAmfCompPresenceState == SA_AMF_PRESENCE_UNINSTANTIATED)){
		comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATED);
		comp_operational_state_set (comp, SA_AMF_OPERATIONAL_ENABLED);
	}
	else {
		assert (0);
	}

	if (comp->su->is_local) {
		res_lib.header.id = MESSAGE_RES_AMF_COMPONENTREGISTER;
		res_lib.header.size = sizeof (struct res_lib_amf_componentregister);
		res_lib.header.error = SA_AIS_OK;
		openais_conn_send_response (comp->conn, &res_lib, sizeof (res_lib));
	}
}

static void message_handler_req_exec_amf_comp_error_report (
	void *message, unsigned int nodeid)
{
	struct res_lib_amf_componenterrorreport res_lib;
	struct req_exec_amf_comp_error_report *req_exec = message;
	struct amf_comp *comp;

	comp = amf_find_comp (&amf_cluster, &req_exec->erroneousComponent);
	if (comp == NULL) {
		log_printf (LOG_ERR, "Component '%s' not found", &req_exec->erroneousComponent);
		return;
	}

	TRACE2("Exec comp error report '%s'", &req_exec->reportingComponent.value);

	if (comp->su->is_local) {
		res_lib.header.size = sizeof (struct res_lib_amf_componenterrorreport);
		res_lib.header.id = MESSAGE_RES_AMF_COMPONENTERRORREPORT;
		res_lib.header.error = SA_AIS_OK;
		openais_conn_send_response (comp->conn, &res_lib, sizeof (res_lib));
	}
	escalation_policy_cleanup (comp);
}

static void message_handler_req_exec_amf_clc_cleanup_completed (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_clc_cleanup_completed *req_exec = message;
	struct amf_comp *comp;

	comp = amf_find_comp (&amf_cluster, &req_exec->compName);
	if (comp == NULL) {
		log_printf (LOG_ERR, "'%s' not found", &req_exec->compName.value);
		return;
	}

	TRACE2("Exec CLC cleanup completed for '%s'", &req_exec->compName.value);
	clc_instantiate (comp);
}

static void message_handler_req_exec_amf_healthcheck_tmo (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_healthcheck_tmo *req_exec = message;
	struct amf_comp *comp;

	comp = amf_find_comp (&amf_cluster, &req_exec->compName);
	if (comp == NULL) {
		log_printf (LOG_ERR, "'%s' not found", &req_exec->compName.value);
		return;
	}

	TRACE2("Exec healthcheck tmo for '%s'", &comp->name.value);
	escalation_policy_cleanup (comp);
}

static void message_handler_req_exec_amf_response (
	void *message, unsigned int nodeid)
{
	struct req_exec_amf_response *req_exec = message;
	struct amf_comp *comp;

	comp = amf_find_comp (&amf_cluster, &req_exec->compName);
	assert (comp != NULL);

	switch (req_exec->interface) {
		case AMF_RESPONSE_CSISETCALLBACK:
			dprintf ("Exec csi set callback response from '%s', error: %d",
					 &req_exec->compName.value, req_exec->error);
			break;

		case AMF_RESPONSE_CSIREMOVECALLBACK:
			dprintf ("Exec csi remove callback response from '%s', error: %d",
					 &req_exec->compName.value, req_exec->error);
			break;

		case AMF_RESPONSE_COMPONENTTERMINATECALLBACK:
			dprintf ("Exec component terminate callback response from '%s', error: %s",
					 &req_exec->compName.value, req_exec->error);
			break;

		default:
			assert (0);
			break;
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

	comp = amf_find_comp (&amf_cluster, &req_lib->compName);
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

	component = amf_find_comp (&amf_cluster, &req_lib_amf_componentunregister->compName);
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
	struct amf_healthcheck *healthcheck;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	comp = amf_find_comp (&amf_cluster, &req_lib->compName);
	if (comp == 0) {
		log_printf (LOG_ERR, "Healthcheckstart: Component '%s' not found",
					req_lib->compName.value);
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck = amf_find_healthcheck (comp, &req_lib->healthcheckKey);
	if (healthcheck == 0) {
		log_printf (LOG_ERR, "Healthcheckstart: Healthcheck '%s' not found",
					req_lib->healthcheckKey.key);
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	dprintf ("Healthcheckstart: '%s', key '%s'",
			 req_lib->compName.value, req_lib->healthcheckKey.key);

	/*
	 *  Determine if this healthcheck is already active
	 */
	if (healthcheck->active) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	/*
	 * Initialise
	 */
	healthcheck->invocationType = req_lib->invocationType;
	healthcheck->timer_handle_duration = 0;
	healthcheck->timer_handle_period = 0;
	healthcheck->active = 0;

	if (comp->conn == NULL) {
		comp->conn = conn;
	}

	healthcheck_activate (healthcheck);

error_exit:
	res_lib.header.id = MESSAGE_RES_AMF_HEALTHCHECKSTART;
	res_lib.header.size = sizeof (res_lib);
	res_lib.header.error = error;

	openais_conn_send_response (conn, &res_lib,
		sizeof (struct res_lib_amf_healthcheckstart));
}

static void message_handler_req_lib_amf_healthcheckconfirm (
	void *conn,
	void *msg)
{
}

static void message_handler_req_lib_amf_healthcheckstop (
	void *conn,
	void *msg)
{
	struct req_lib_amf_healthcheckstop *req_lib = msg;
	struct res_lib_amf_healthcheckstop res_lib;
	struct amf_healthcheck *healthcheck;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	dprintf ("healthcheck stop\n");

	comp = amf_find_comp (&amf_cluster, &req_lib->compName);
	if (comp == 0) {
		log_printf (LOG_ERR, "Healthcheckstop: Component '%s' not found",
					req_lib->compName.value);
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck = amf_find_healthcheck (comp, &req_lib->healthcheckKey);
	if (healthcheck == 0) {
		log_printf (LOG_ERR, "Healthcheckstop: Healthcheck '%s' not found",
					req_lib->healthcheckKey.key);
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck_deactivate (healthcheck);

error_exit:
	res_lib.header.id = MESSAGE_RES_AMF_HEALTHCHECKSTOP;
	res_lib.header.size = sizeof (res_lib);
	res_lib.header.error = error;
	openais_conn_send_response (conn, &res_lib, sizeof (res_lib));
}

static void message_handler_req_lib_amf_hastateget (
	void *conn,
	void *msg)
{
#ifdef COMPILE_OUT
	struct req_lib_amf_hastateget *req_lib_amf_hastateget = (struct req_lib_amf_hastateget *)msg;
	struct res_lib_amf_hastateget res_lib_amf_hastateget;
	struct amf_comp *component;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_hastateget()\n");

	res_lib_amf_hastateget.header.id = MESSAGE_RES_AMF_HASTATEGET;
	res_lib_amf_hastateget.header.size = sizeof (struct res_lib_amf_hastateget);
	res_lib_amf_hastateget.header.error = SA_ERR_NOT_EXIST;

#ifdef COMPILE_OUT
	component = component_in_protectiongroup_find (&req_lib_amf_hastateget->csiName, &req_lib_amf_hastateget->compName);
#endif

	if (component) {
		memcpy (&res_lib_amf_hastateget.haState, 
			&component->currentHAState, sizeof (SaAmfHAStateT));
		res_lib_amf_hastateget.header.error = SA_AIS_OK;
	}
	openais_conn_send_response (conn, &res_lib_amf_hastateget, sizeof (struct res_lib_amf_hastateget));
#endif
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

	comp = amf_find_comp (&amf_cluster, &req_lib->erroneousComponent);
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

		assert (totempg_groups_mcast_joined (openais_group_handle,
			&iovec, 1, TOTEMPG_AGREED) == 0);
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

#if 0
static void pg_comp_create (
	struct amf_pg *pg,
	struct amf_csi *csi,
	struct amf_comp *comp)
{
	struct amf_pg_comp *pg_comp;

	dprintf ("creating component for pg\n");
	pg_comp = malloc (sizeof (struct amf_pg_comp));
	assert (pg_comp);
	pg_comp->comp = comp;
	pg_comp->csi = csi;
	list_init (&pg_comp->list);
	list_add_tail (&pg_comp->list, &pg->pg_comp_head);
}
#endif

static void message_handler_req_lib_amf_response (void *conn, void *msg)
{
	struct res_lib_amf_response res_lib;
	struct req_lib_amf_response *req_lib = msg;
	struct req_exec_amf_response req_exec;
	struct iovec iovec;
	struct amf_pd *pd = openais_conn_private_data_get (conn);
	int res;
	int interface;
	void *data;
	int error = SA_AIS_OK;

	res = invocation_get_and_destroy (req_lib->invocation, &interface, &data);

	if (res == -1) {
		log_printf (LOG_ERR, "Lib response: invocation not found\n");
		error = SA_AIS_ERR_INVALID_PARAM;
		goto end;
	}

	switch (interface) {
		case AMF_RESPONSE_HEALTHCHECKCALLBACK: {
			struct amf_healthcheck *healthcheck = data;
			SaNameT name;
			TRACE3 ("Lib healthcheck response from '%s'",
					comp_dn_make (healthcheck->comp, &name));

			poll_timer_delete (aisexec_poll_handle,
							   healthcheck->timer_handle_duration);
			healthcheck->timer_handle_duration = 0;

			poll_timer_add (aisexec_poll_handle,
							healthcheck->saAmfHealthcheckPeriod,
							(void *)healthcheck,
							timer_function_healthcheck_next_fn,
							&healthcheck->timer_handle_period);
			break;
		}
		case AMF_RESPONSE_CSISETCALLBACK: {
			struct amf_csi_assignment *csi_assignment = data;
			dprintf ("Lib csi '%s' set callback response from '%s', error: %d",
					 csi_assignment->csi->name.value, csi_assignment->comp->name.value, req_lib->error);
			memcpy (&req_exec.csiName, &csi_assignment->name, sizeof (SaNameT));
			break;
		}
		case AMF_RESPONSE_CSIREMOVECALLBACK: {
			struct amf_csi_assignment *csi_assignment = data;
			dprintf ("Lib csi '%s' remove callback response from '%s', error: %d",
					 csi_assignment->csi->name.value, csi_assignment->comp->name.value, req_lib->error);
			memcpy (&req_exec.csiName, &csi_assignment->name, sizeof (SaNameT));
			break;
		}
		case AMF_RESPONSE_COMPONENTTERMINATECALLBACK: {
			struct component_terminate_callback_data *component_terminate_callback_data;
			component_terminate_callback_data = data;

			dprintf ("Lib component terminate callback response, error: %d", req_lib->error);
			comp_healthcheck_deactivate (component_terminate_callback_data->comp);

			escalation_policy_restart (component_terminate_callback_data->comp);
			break;
		}
		default:
			assert (0);
			break;
	}

	/* Keep healthcheck responses node local */
	if (interface != AMF_RESPONSE_HEALTHCHECKCALLBACK) {
		assert (pd && pd->comp);
		req_exec.header.size = sizeof (struct req_exec_amf_response);
		req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
											  MESSAGE_REQ_EXEC_AMF_RESPONSE);
		comp_dn_make (pd->comp, &req_exec.compName);
		req_exec.interface = interface;
		req_exec.error = req_lib->error;
		iovec.iov_base = (char *)&req_exec;
		iovec.iov_len = sizeof (req_exec);
		assert (totempg_groups_mcast_joined (openais_group_handle,
											 &iovec, 1, TOTEMPG_AGREED) == 0);
	}

end:
	res_lib.header.id = MESSAGE_RES_AMF_RESPONSE;
	res_lib.header.size = sizeof (struct res_lib_amf_response);
	res_lib.header.error = error;
	openais_conn_send_response (conn, &res_lib, sizeof (res_lib));
}

