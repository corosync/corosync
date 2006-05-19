/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt
 * Description: Introduced AMF B.02 information model
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
	MESSAGE_REQ_EXEC_AMF_OPERATIONAL_STATE_COMP_SET = 0,
	MESSAGE_REQ_EXEC_AMF_PRESENCE_STATE_COMP_SET = 1,
	MESSAGE_REQ_EXEC_AMF_ADMINISTRATIVE_STATE_CSI_SET = 2,
	MESSAGE_REQ_EXEC_AMF_ADMINISTRATIVE_STATE_UNIT_SET = 3,
	MESSAGE_REQ_EXEC_AMF_ADMINISTRATIVE_STATE_GROUP_SET = 4
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

struct csi_set_callback_data {
	struct amf_comp *comp;
	struct amf_csi *csi;
	struct amf_pg *pg;
};

struct csi_remove_callback_data {
	struct amf_csi *csi;
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

static void message_handler_req_exec_amf_operational_state_comp_set (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_presence_state_comp_set (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_administrative_state_csi_set (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_administrative_state_unit_set (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_amf_administrative_state_group_set (
	void *message,
	unsigned int nodeid);

static void presence_state_comp_set (
	struct amf_comp *comp,
	SaAmfPresenceStateT presence_state);

static void operational_state_comp_set (
	struct amf_comp *comp,
	SaAmfOperationalStateT operational_state);

static void operational_state_unit_set (
	struct amf_su *unit,
	SaAmfOperationalStateT operational_state);

static void assign_sis (struct amf_sg *group);
static void clc_instantiate_all (void *data);
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
static void comp_healthcheck_activate (struct amf_comp *comp);
static void comp_healthcheck_deactivate (struct amf_comp *comp);
static void escalation_policy_restart (struct amf_comp *comp);
static void amf_dump(void);

static char *presence_state_text[] = {
	"unknown",
	"uninstantiated",
	"instantiating",
	"instantiated",
	"terminating",
	"restarting",
	"instantion_failed",
	"terminiation_failed"
};

static char *oper_state_text[] = {
	"Unknown",
	"enabled",
	"disabled"
};

static char *admin_state_text[] = {
	"Unknown",
	"unlocked",
	"locked",
	"locked_instantiation",
	"shutting_down"
};

static char *readiness_state_text[] = {
	"Unknown",
	"out of service",
	"in service",
};

static char *ha_state_text[] = {
	"Unknown",
	"active",
	"standby",
	"quiesced",
	"quiescing",
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
		.exec_handler_fn	= message_handler_req_exec_amf_operational_state_comp_set,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_amf_presence_state_comp_set,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_amf_administrative_state_csi_set,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_amf_administrative_state_unit_set,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_amf_administrative_state_group_set
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
    .exec_dump_fn		= amf_dump
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
	char env_comp_name[1024];
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

	envp[0] = env_comp_name;
	strcpy (env_comp_name, "SA_AMF_COMPONENT_NAME=");
	strncat (env_comp_name, (char *)clc_command_run_data->comp->name.value,
		clc_command_run_data->comp->name.length);

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

struct req_exec_amf_operational_state_comp_set {
	struct req_header header;
	SaNameT name;
	SaAmfOperationalStateT operational_state;
};

struct req_exec_amf_presence_state_comp_set {
	struct req_header header;
	SaNameT name;
	SaAmfPresenceStateT presence_state;
};

struct req_exec_amf_administrative_state_csi_set {
	struct req_header header;
	SaNameT name;
	SaAmfAdminStateT administrative_state;
};

struct req_exec_amf_administrative_state_unit_set {
	struct req_header header;
	SaNameT name;
	SaAmfAdminStateT administrative_state;
};

struct req_exec_amf_administrative_state_group_set {
	struct req_header header;
	SaNameT name;
	SaAmfAdminStateT administrative_state;
};
struct req_exec_amf_comp_restart {
	struct req_header header;
	SaNameT compName;
};

/*
 * Instantiate possible operations
 */
static int clc_cli_instantiate (struct amf_comp *comp)
{
	int res;
	pthread_t thread;
	pthread_attr_t thread_attr;	/* thread attribute */

	struct clc_command_run_data *clc_command_run_data;

	ENTER("comp %s\n", getSaNameT (&comp->name));

	clc_command_run_data = malloc (sizeof (struct clc_command_run_data));
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
	assert (component_terminate_callback_data); // TODO failure here of malloc
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
 * This reinstantiates the cleaned up component
 */
static void clc_cleanup_completion_callback (void *context) {
	struct clc_command_run_data *clc_command_run_data = (struct clc_command_run_data *)context;
	
	escalation_policy_restart (clc_command_run_data->comp);
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
	int res;

	dprintf ("clc instantiate for comp %s\n", getSaNameT (&comp->name));

	presence_state_comp_set (comp, SA_AMF_PRESENCE_INSTANTIATING);
	res = clc_interfaces[comp->comptype]->instantiate (comp);
	return (res);
}

#if 0
static int clc_terminate (struct amf_comp *comp)
{
	int res;

	dprintf ("clc terminate for comp %s\n", getSaNameT (&comp->name));
	assert (0);
	operational_state_comp_set (comp, SA_AMF_OPERATIONAL_DISABLED);
	presence_state_comp_set (comp, SA_AMF_PRESENCE_TERMINATING);

	res = clc_interfaces[comp->comptype]->terminate (comp);
	return (0);
}
#endif

static int clc_cleanup (struct amf_comp *comp)
{
	int res;

	dprintf ("clc cleanup for comp %s\n", getSaNameT (&comp->name));
	comp_healthcheck_deactivate (comp);
	operational_state_comp_set (comp, SA_AMF_OPERATIONAL_DISABLED);
	presence_state_comp_set (comp, SA_AMF_PRESENCE_TERMINATING);
	res = clc_interfaces[comp->comptype]->cleanup (comp);
	return (0);
}

/* IMPL */

static int amf_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	int res;
	char *error_string;
	unsigned int object_service_handle;
	int enabled = 0;
	char *value;
	char hostname[HOST_NAME_MAX + 1];
	struct amf_node *node;
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;
	struct amf_comp *comp;
	struct amf_si *si;
	struct amf_csi *csi;
	int i;

	log_init ("AMF");

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

	if (!enabled) {
		return 0;
	}
	
	if (gethostname (hostname, sizeof(hostname)) == -1) {
		log_printf (LOG_LEVEL_ERROR, "gethostname failed: %d", errno);
		return -1;
	}

	res = amf_config_read (&amf_cluster, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		return res;
	}

	dprintf("Cluster: %s", getSaNameT(&amf_cluster.name));
	dprintf("  timeout:               %d ms\n", amf_cluster.saAmfClusterStartupTimeout);
	dprintf("  admin state:           %s\n", admin_state_text[amf_cluster.saAmfClusterAdminState]);
	for (node = amf_cluster.node_head; node != NULL; node = node->next) {
		dprintf("  Node:                %s\n", getSaNameT (&node->name));
		dprintf("    su_fail_over_prob: %u\n", node->saAmfNodeSuFailOverProb);
		dprintf("    su_fail_over_max:  %u\n", node->saAmfNodeSuFailoverMax);
		dprintf("    auto repair:       %u\n", node->saAmfNodeAutoRepair);

		/* look for this node */
		if (strcmp(hostname, getSaNameT (&node->name)) == 0) {
			this_amf_node = node;
		}
	}
	for (app = amf_cluster.application_head; app != NULL; app = app->next) {
		dprintf("  Application:       %s\n", getSaNameT(&app->name));
		dprintf("    num_sg:          %d\n", app->saAmfApplicationCurrNumSG);
		for (sg = app->sg_head;	sg != NULL; sg = sg->next) {
			dprintf("    SG:             %s\n", getSaNameT(&sg->name));
			dprintf("      red model:    %u\n", sg->saAmfSGRedundancyModel);
			dprintf("      auto adjust:  %u\n", sg->saAmfSGAutoAdjust);
			for (su = sg->su_head; su != NULL; su = su->next) {
				dprintf("      SU:           %s\n", getSaNameT(&su->name));
				dprintf("        rank:       %u\n", su->saAmfSURank);
				for (comp = su->comp_head; comp != NULL; comp = comp->next) {
					dprintf("        Comp:           %s\n", getSaNameT(&comp->name));
					dprintf("          category:     %u\n", comp->saAmfCompCategory);
					dprintf("          env vars:");
					i = 0;
					while (comp->saAmfCompCmdEnv && comp->saAmfCompCmdEnv[i]) {
						dprintf("            %s", comp->saAmfCompCmdEnv[i]);
						i++;
					}
				}
			}
		}
		for (si = app->si_head; si != NULL; si = si->next) {
			dprintf("    SI:             %s\n", getSaNameT(&si->name));
			for (csi = si->csi_head; csi != NULL; csi = csi->next) {
				dprintf("      CSI:          %s\n", getSaNameT(&csi->name));
			}
		}
	}

	if (this_amf_node != NULL) {
		/* wait a while before instantiating SUs as the AMF spec. says. */
		poll_timer_add(aisexec_poll_handle,
					   amf_cluster.saAmfClusterStartupTimeout,
					   NULL,
					   clc_instantiate_all,
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

	comp = amf_pd->comp;

	TRACE8("amf_lib_exit_fn");

	if (comp) {
		comp->conn = 0;
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

#ifdef COMPILE_OUT
static void amf_synchronize (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_componentregister *req_exec_amf_componentregister = (struct req_exec_amf_componentregister *)message;
	struct amf_comp *component;
	struct amf_comp *amfProxyComponent;

	log_printf (LOG_LEVEL_ENTER_FUNC, "amf_synchronize%s\n",
		getSaNameT (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName));

	/* Find Component */
	component = amf_find_comp (&amf_cluster, &req_exec_amf_componentregister->req_lib_amf_componentregister.compName);
	amfProxyComponent = amf_find_comp (&amf_cluster, &req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName);

	/* If this processor is component owner */
	if (component->source_addr.s_addr == this_ip->sin_addr.s_addr) {

		/* No Operation */
		return;
	}

	/* If this isn't synchronizing target processor */
	if (!(component->local == 0 &&  component->registered == 0)){ 

		/* No Operation */
		return;
	}

	/* Synchronize Status */
	component->local = 0;
	component->registered = 1;
	component->conn_info = req_exec_amf_componentregister->source.conn_info;
	component->source_addr = source_addr;
	component->currentReadinessState = SA_AMF_OUT_OF_SERVICE;
	component->newReadinessState = SA_AMF_OUT_OF_SERVICE;
	component->currentHAState = SA_AMF_QUIESCED;
	component->newHAState = SA_AMF_QUIESCED;
	component->probableCause = 0;
	component->enabledUnlockedState = 0;
	component->disabledUnlockedState = 0;
	component->currentReadinessState = req_exec_amf_componentregister->currentReadinessState;
	component->newReadinessState = req_exec_amf_componentregister->newReadinessState;
	component->currentHAState = req_exec_amf_componentregister->currentHAState;
	component->newHAState = req_exec_amf_componentregister->newHAState;

	if (req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName.length > 0) {
		component->saAmfProxyComponent = amfProxyComponent;
	}

	/*
	 *  Determine if we should enter new state
	 */
	dsmSynchronizeStaus (component);

	return;
}
#endif

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

static void error_report (struct amf_comp *comp)
{
	struct req_exec_amf_error_report req_exec_amf_error_report;
	struct iovec iovec;

	req_exec_amf_error_report.header.size = sizeof (struct req_exec_amf_error_report);
	req_exec_amf_error_report.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE,  MESSAGE_REQ_EXEC_AMF_ERROR_REPORT);
	memcpy (&req_exec_amf_error_report.compName,
		&comp->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_amf_error_report;
	iovec.iov_len = sizeof (req_exec_amf_error_report);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
}

static void TODO_COMP_RESTART_THISISADEADPLACEHOLDER (struct amf_comp *comp)
{
	struct req_exec_amf_comp_restart req_exec_amf_comp_restart;
	struct iovec iovec;

	req_exec_amf_comp_restart.header.size = sizeof (struct req_exec_amf_comp_restart);
	req_exec_amf_comp_restart.header.id = 
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_UNIT_RESTART);
	memcpy (&req_exec_amf_comp_restart.compName, &comp->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_amf_comp_restart;
	iovec.iov_len = sizeof (req_exec_amf_comp_restart);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
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
#endif

static void comp_healthcheck_activate (struct amf_comp *comp)
{
	struct amf_healthcheck *healthcheck;

	for (healthcheck = comp->healthcheck_head;
		healthcheck != NULL;
		healthcheck = healthcheck->next) {

		if (healthcheck->active == 0) {
			healthcheck_activate (healthcheck);
		}
	}
}

static void comp_healthcheck_deactivate (
	struct amf_comp *comp)
{
	struct amf_healthcheck *healthcheck;

	log_printf (LOG_LEVEL_NOTICE, "ZZZ comp_healthcheck_deactivate %s\n",
		getSaNameT (&comp->name));

	for (healthcheck = comp->healthcheck_head;
		  healthcheck != NULL;
		  healthcheck = healthcheck->next) {

		dprintf ("healthcheck deactivating %p\n", healthcheck);
		healthcheck_deactivate (healthcheck);
	}
}

static void presence_state_comp_set (
	struct amf_comp *comp,
	SaAmfPresenceStateT presence_state)
{
	struct req_exec_amf_presence_state_comp_set req_exec_amf_presence_state_comp_set;
	struct iovec iovec;

	req_exec_amf_presence_state_comp_set.header.size = sizeof (struct req_exec_amf_presence_state_comp_set);
	req_exec_amf_presence_state_comp_set.header.id = 
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_PRESENCE_STATE_COMP_SET);
	req_exec_amf_presence_state_comp_set.presence_state = presence_state;
	memcpy (&req_exec_amf_presence_state_comp_set.name,
		&comp->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_amf_presence_state_comp_set;
	iovec.iov_len = sizeof (req_exec_amf_presence_state_comp_set);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
}

static void readiness_state_comp_set (struct amf_comp *comp)
{
	dprintf ("inputs to readiness_state_comp_set\n");
	dprintf ("\tunit readiness state %s\n",
		readiness_state_text[comp->su->saAmfSUReadinessState]);
	dprintf ("\tcomp operational state %s\n",
		oper_state_text[comp->su->saAmfSUReadinessState]);

	/*
	 * Set component readiness state appropriately
	 * if unit in service and component is enabled, it is in service
	 * otherwise it is out of service page 37
	 */
	if (comp->su->saAmfSUReadinessState == SA_AMF_READINESS_IN_SERVICE &&
		comp->saAmfCompOperState == SA_AMF_OPERATIONAL_ENABLED) {
		comp->saAmfCompReadinessState = SA_AMF_READINESS_IN_SERVICE;
	} else {
		comp->saAmfCompReadinessState = SA_AMF_READINESS_OUT_OF_SERVICE;
	}
	dprintf ("readiness_state_comp_set (%s)\n",
		oper_state_text[comp->saAmfCompOperState]);
}

static void operational_state_comp_set (struct amf_comp *comp, SaAmfOperationalStateT oper_state)
{
	struct req_exec_amf_operational_state_comp_set req_exec_amf_operational_state_comp_set;
	struct iovec iovec;

	req_exec_amf_operational_state_comp_set.header.size = sizeof (struct req_exec_amf_operational_state_comp_set);
	req_exec_amf_operational_state_comp_set.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_OPERATIONAL_STATE_COMP_SET);

	req_exec_amf_operational_state_comp_set.operational_state = oper_state;
	memcpy (&req_exec_amf_operational_state_comp_set.name,
		&comp->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_amf_operational_state_comp_set;
	iovec.iov_len = sizeof (req_exec_amf_operational_state_comp_set);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
}

static void csi_comp_set_callback (
	struct amf_comp *comp,
	struct amf_csi *csi,
	struct amf_pg *pg)
{
	struct res_lib_amf_csisetcallback* res_lib_amf_csisetcallback;     
    void*  p;
    struct csi_set_callback_data *csi_set_callback_data;
    struct amf_csi_attribute *attribute;
    size_t char_length_of_csi_attrs=0;
    size_t num_of_csi_attrs=0;
	int i;

    dprintf("\t   Assigning CSI %s to component\n", getSaNameT (&csi->name));

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

    assert(p);

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

	switch (comp->su->requested_ha_state) {
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

	memcpy (&res_lib_amf_csisetcallback->compName,
			&comp->name, sizeof (SaNameT));

	memcpy (&res_lib_amf_csisetcallback->csiName,
			&csi->name, sizeof (SaNameT));

	res_lib_amf_csisetcallback->haState = comp->su->requested_ha_state;
	
	csi_set_callback_data = malloc (sizeof (struct csi_set_callback_data));
	assert (csi_set_callback_data); // TODO failure here of malloc
	csi_set_callback_data->comp = comp;
	csi_set_callback_data->csi = csi;
	csi_set_callback_data->pg = pg;
 
	res_lib_amf_csisetcallback->invocation =
		invocation_create (
		AMF_RESPONSE_CSISETCALLBACK,
		csi_set_callback_data);
	
	openais_conn_send_response (
	    openais_conn_partner_get (comp->conn),
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
static void csi_unit_set_callback (struct amf_su *unit, struct amf_si *si)
{
    struct amf_csi *csi;
    struct amf_pg *pg = NULL;
    struct amf_comp *comp;
    SaNameT *cs_type;
	int i;

//    pg_create (csi_in->si, &pg);
    // TODO remove si from csi data structure

    /*
    ** for each component in SU, find a CSI in the SI with the same type
    */
    for (comp = unit->comp_head; comp != NULL; comp = comp->next) {

        dprintf ("\t%s\n", getSaNameT (&comp->name));

        int no_of_csi_types = 0;
        for (i = 0; comp->saAmfCompCsTypes[i]; i++) {
				cs_type = comp->saAmfCompCsTypes[i];
                no_of_csi_types++;
                int no_of_assignments = 0;

                for (csi = si->csi_head; csi != NULL; csi = csi->next) {
                        if (!memcmp(csi->saAmfCSTypeName.value, cs_type->value, cs_type->length)) {
                                csi_comp_set_callback (comp, csi, pg);
                                no_of_assignments++;
                        }
                }
                if (no_of_assignments == 0) {
                    log_printf (LOG_WARNING, "\t   No CSIs of type %s configured?!!\n",
                            getSaNameT (cs_type));
                }
        }
        if (no_of_csi_types == 0) {
           log_printf (LOG_LEVEL_ERROR, "\t   No CS types configured for comp %s ?!!\n",
                    getSaNameT (&comp->name));
                }
    }
} 
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
static void assign_sis_timeout_fn(void *data)
{
	struct amf_application *app;
	struct amf_sg *group;

	dprintf("2nd Cluster start timer expired, assigning SIs\n");

	for (app = amf_cluster.application_head; app != NULL; app = app->next) {
		for (group = app->sg_head; group != NULL; group = group->next) {
			assign_sis(group);
		}
	}
}

static void clc_instantiate_all (void *data)
{
	struct amf_application *app;
	struct amf_sg *group;
	struct amf_su *unit;
	struct amf_comp *comp;

	dprintf("1st Cluster start timer expired, instantiating SUs\n");

	for (app = amf_cluster.application_head; app != NULL; app = app->next) {
		for (group = app->sg_head; group != NULL; group = group->next) {
			for (unit = group->su_head; unit != NULL; unit = unit->next) {
				for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
					if (strlen ((char *)comp->saAmfCompInstantiateCmd)) {
						clc_instantiate (comp);
					}
				}
			}
		}
	}

	/* wait a while before assigning SIs as the AMF spec. says. */
	poll_timer_add(aisexec_poll_handle,
				   amf_cluster.saAmfClusterStartupTimeout,
				   NULL,
				   assign_sis_timeout_fn,
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

static void unit_cleanup (struct amf_su *unit)
{
	struct amf_comp *comp;

	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		clc_cleanup (comp);
	}
}
	
static void comp_restart (struct amf_comp *comp)
{
	presence_state_comp_set (comp, SA_AMF_PRESENCE_RESTARTING);
}
#if 0
static void unit_restart (struct amf_su *unit)
{
	struct amf_comp *comp;

	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		presence_state_comp_set (comp, SA_AMF_PRESENCE_RESTARTING);
	}
}
static void clc_unit_instantiate (struct amf_su *unit)
{
	struct amf_comp *comp;

	dprintf ("ZZZZZZZZZZZZZZZZZ clc_unit_instantitate\n");
	for (comp = unit->comp_head; comp != NULL; comp = comp->next) {
		clc_instantiate (comp);
	}
}
#endif
static void ha_state_unit_set (struct amf_su *unit, struct amf_si *si,
		SaAmfHAStateT ha_state)
{
	dprintf ("Assigning SI %s to SU %s with hastate %s\n",
			 getSaNameT (&si->name), getSaNameT (&unit->name), ha_state_text[ha_state]);

	unit->requested_ha_state = ha_state;

	csi_unit_set_callback (unit, si);
}


static int unit_inservice_count (struct amf_sg *group)
{
	struct amf_su *unit;
	int answer = 0;

	for (unit = group->su_head; unit != NULL; unit = unit->next) {
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

static int si_count (struct amf_sg *group)
{
	struct amf_si *si;
	int answer = 0;

	for (si = group->application->si_head; si != NULL; si = si->next) {
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


static void assign_nm_active (struct amf_sg *group, int su_units_assign)
{
	struct amf_su *unit;
	struct amf_si *si;
	int assigned = 0;
	int assign_per_su = 0;
	int total_assigned = 0;

	assign_per_su = si_count (group);
	assign_per_su = div_round (assign_per_su, su_units_assign);
	if (assign_per_su > group->saAmfSGMaxActiveSIsperSUs) {
		assign_per_su = group->saAmfSGMaxActiveSIsperSUs;
	}

	si = group->application->si_head;
	unit = group->su_head;
	while (unit != NULL) {
		if (unit->saAmfSUReadinessState != SA_AMF_READINESS_IN_SERVICE) {
			unit = unit->next;
			continue; /* Not in service */
		}

		assigned = 0;
		while (si != NULL &&
			assigned < assign_per_su &&
			total_assigned < si_count (group)) {

			assigned += 1;
			total_assigned += 1;
			ha_state_unit_set (unit, si, SA_AMF_HA_ACTIVE);
			si = si->next;
		}
		unit = unit->next;
	}
}

static void assign_nm_standby (struct amf_sg *group, int units_assign_standby)
{
	struct amf_su *unit;
	struct amf_si *si;
	int assigned = 0;
	int assign_per_su = 0;

	if (units_assign_standby == 0) {
		return;
	}
	assign_per_su = si_count (group);
	assign_per_su = div_round (assign_per_su, units_assign_standby);
	if (assign_per_su > group->saAmfSGMaxStandbySIsperSUs) {
		assign_per_su = group->saAmfSGMaxStandbySIsperSUs;
	}

	si = group->application->si_head;
	unit = group->su_head;
	while (unit != NULL) {
		if (unit->saAmfSUReadinessState != SA_AMF_READINESS_IN_SERVICE ||
			unit->requested_ha_state == SA_AMF_HA_ACTIVE) {

			unit = unit->next;
			continue; /* Not available for assignment */
		}

		assigned = 0;
		while (si != NULL && assigned < assign_per_su) {
			assigned += 1;
			ha_state_unit_set (unit, si, SA_AMF_HA_STANDBY);
			si = si->next;
		}
		unit = unit->next;
	}
}
#if 0
static void assign_nm_spare (struct amf_sg *group)
{
	struct amf_su *unit;

	for (unit = group->su_head; unit != NULL; unit = unit->next) {
		if (unit->saAmfSUReadinessState == SA_AMF_READINESS_IN_SERVICE &&
			(unit->requested_ha_state != SA_AMF_HA_ACTIVE &&
			unit->requested_ha_state != SA_AMF_HA_STANDBY)) {

			dprintf ("Assigning to SU %s with SPARE\n",
				getSaNameT (&unit->name));
		}
	}
}
#endif
static void clear_requested_ha_state (struct amf_sg *group)
{
	struct amf_su *unit;

	for (unit = group->su_head; unit != NULL; unit = unit->next) {
		unit->requested_ha_state = SA_AMF_HA_QUIESCED;
	}
}

static void assign_sis (struct amf_sg *group)
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

	clear_requested_ha_state (group);

	/*
	 * Number of SUs to assign to active or standby state
	 */
	inservice_count = (float)unit_inservice_count (group);

	active_sus_needed = div_round (si_count(group),
		group->saAmfSGMaxActiveSIsperSUs);

	standby_sus_needed = div_round (si_count(group),
		group->saAmfSGMaxStandbySIsperSUs);

	units_for_active = inservice_count - group->saAmfSGNumPrefStandbySUs;
	if (units_for_active < 0) {
		units_for_active = 0;
	}

	units_for_standby = inservice_count - group->saAmfSGNumPrefActiveSUs;
	if (units_for_standby < 0) {
		units_for_standby = 0;
	}

	ii_spare = inservice_count - group->saAmfSGNumPrefActiveSUs - group->saAmfSGNumPrefStandbySUs;
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
	if ((group->saAmfSGMaxStandbySIsperSUs * units_for_standby) <= si_count (group)) {
		dprintf ("IV: full assignment with reduction of active service units\n");
		su_active_assign = inservice_count - standby_sus_needed;
		su_standby_assign = standby_sus_needed;
		su_spare_assign = 0;
	} else 
	if ((group->saAmfSGMaxActiveSIsperSUs * units_for_active) <= si_count (group)) {

		dprintf ("III: full assignment with reduction of standby service units\n");
		su_active_assign = group->saAmfSGNumPrefActiveSUs;
		su_standby_assign = units_for_standby;
		su_spare_assign = 0;
	} else
	if (ii_spare == 0) {
		dprintf ("II: full assignment with spare reduction\n");

		su_active_assign = group->saAmfSGNumPrefActiveSUs;
		su_standby_assign = group->saAmfSGNumPrefStandbySUs;
		su_spare_assign = 0;
	} else {
		dprintf ("I: full assignment with spares\n");

		su_active_assign = group->saAmfSGNumPrefActiveSUs;
		su_standby_assign = group->saAmfSGNumPrefStandbySUs;
		su_spare_assign = ii_spare;
	}

	dprintf ("(inservice=%d) (assigning active=%d) (assigning standby=%d) (assigning spares=%d)\n",
		inservice_count, su_active_assign, su_standby_assign, su_spare_assign);
	assign_nm_active (group, su_active_assign);
	assign_nm_standby (group, su_standby_assign);
}

static int all_sus_in_sg_ready(struct amf_sg *sg)
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

static void readiness_state_unit_set (struct amf_su *unit, SaAmfReadinessStateT readiness_state)
{
	dprintf ("Assigning unit %s readiness state %s\n",
			 getSaNameT (&unit->name), readiness_state_text[readiness_state]);

	unit->saAmfSUReadinessState = readiness_state;

	if (all_sus_in_sg_ready(unit->sg)){
		assign_sis (unit->sg);
		if (amf_cluster.timeout_handle) {
			poll_timer_delete (aisexec_poll_handle, amf_cluster.timeout_handle);
		}
	}
}

static void presence_state_unit_set (struct amf_su *unit, SaAmfPresenceStateT presence_state)
{
	dprintf ("Setting service unit presence state %s\n",
		presence_state_text[presence_state]);
}


static void escalation_policy_restart (struct amf_comp *comp)
{
	dprintf ("escalation_policy_restart %d\n", comp->su->escalation_level);
	dprintf ("escalation policy restart uninsint %p\n", comp);
	presence_state_comp_set (
		comp,
		SA_AMF_PRESENCE_UNINSTANTIATED);

	operational_state_comp_set (
		comp,
		SA_AMF_OPERATIONAL_DISABLED);

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
		comp->su->saAmfSURestartCount += 1;
		if (comp->su->saAmfSURestartCount >= comp->su->sg->saAmfSGCompRestartMax) {
			comp->su->escalation_level = ESCALATION_LEVEL_ONE;
			escalation_policy_cleanup (comp);
			comp->su->saAmfSURestartCount = 0;
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
			return;
		}
		dprintf ("Escalation level 1 - restart unit\n");
		dprintf ("Cleaning up and restarting unit.\n");
		unit_cleanup (comp->su);
		break;

	case ESCALATION_LEVEL_TWO:
		dprintf ("Escalation level TWO\n");
		unit_cleanup (comp->su);
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
	struct amf_healthcheck *healthcheck = (struct amf_healthcheck *)data;

	dprintf ("timeout occured on healthcheck for component %s.\n",
		getSaNameT (&healthcheck->comp->name));
	escalation_policy_cleanup (healthcheck->comp);
}

static void healthcheck_activate (struct amf_healthcheck *healthcheck_active)
{
	struct res_lib_amf_healthcheckcallback res_lib_amf_healthcheckcallback;

	healthcheck_active->active = 1;

// TODO	memset (&res_lib_amf_healthcheckcallback, 0, sizeof(res_lib_amf_healthcheckcallback));
	res_lib_amf_healthcheckcallback.header.id = MESSAGE_RES_AMF_HEALTHCHECKCALLBACK;
	res_lib_amf_healthcheckcallback.header.size = sizeof (struct res_lib_amf_healthcheckcallback);
	res_lib_amf_healthcheckcallback.header.error = SA_AIS_OK;

	TRACE8 ("sending healthcheck to component %s",
			getSaNameT (&healthcheck_active->comp->name));

	res_lib_amf_healthcheckcallback.invocation =
		invocation_create (
		AMF_RESPONSE_HEALTHCHECKCALLBACK,
		(void *)healthcheck_active);

	memcpy (&res_lib_amf_healthcheckcallback.compName,
		&healthcheck_active->comp->name,
		sizeof (SaNameT));

	memcpy (&res_lib_amf_healthcheckcallback.key,
		&healthcheck_active->safHealthcheckKey,
		sizeof (SaAmfHealthcheckKeyT));

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
	log_printf (LOG_LEVEL_NOTICE,
				"ZZZ deactivating healthcheck for component %s\n",
				getSaNameT (&healthcheck_active->comp->name));
	poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_handle_period);

	poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_handle_duration);

	invocation_destroy_by_data ((void *)healthcheck_active);
	healthcheck_active->active = 0;
}


static void timer_function_healthcheck_next (
	void *data)
{
	healthcheck_activate (data);
}

static  void operational_state_unit_set (
	struct amf_su *unit,
	SaAmfOperationalStateT oper_state)
{
	if (oper_state == unit->saAmfSUOperState) {
		dprintf ("Not assigning service unit new operational state - same state\n");
		return;
	}
	unit->saAmfSUOperState = oper_state;
	dprintf ("Service unit operational state set to %s\n",
		oper_state_text[oper_state]);
	if (oper_state == SA_AMF_OPERATIONAL_ENABLED) {
		readiness_state_unit_set (unit,
			SA_AMF_READINESS_IN_SERVICE);
		/*
		 * Start healthcheck now
		 */
// TODO		healthcheck_unit_activate (unit);
	} else
	if (oper_state == SA_AMF_OPERATIONAL_DISABLED) {
		readiness_state_unit_set (unit,
			SA_AMF_READINESS_OUT_OF_SERVICE);
//		ha_state_unit_set (unit, si, SA_AMF_HA_STANDBY);

//		healthcheck_unit_deactivate (unit);
	}
}


static void message_handler_req_exec_amf_operational_state_comp_set (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_amf_operational_state_comp_set *req_exec_amf_operational_state_comp_set =
		(struct req_exec_amf_operational_state_comp_set *)message;
	struct amf_comp *comp;
	struct amf_comp *comp_compare;
	int all_set = 1;

	comp = amf_find_comp (&amf_cluster, &req_exec_amf_operational_state_comp_set->name);
	comp->saAmfCompOperState = req_exec_amf_operational_state_comp_set->operational_state;
	
	dprintf ("Setting component %s operational state to %s\n",
		getSaNameT (&comp->name),
		oper_state_text[comp->saAmfCompOperState]);
	/*
	 * If all operational states are ENABLED, then SU should be ENABLED
	 */
	for (comp_compare = comp->su->comp_head; comp_compare != NULL; comp_compare = comp_compare->next) {
		if (comp_compare->saAmfCompOperState != SA_AMF_OPERATIONAL_ENABLED) {
			all_set = 0;
			break;
		}
	}
	if (all_set) {
		operational_state_unit_set (comp->su, 
			SA_AMF_OPERATIONAL_ENABLED);
	} else {
		operational_state_unit_set (comp->su, 
			SA_AMF_OPERATIONAL_DISABLED);
	}
	readiness_state_comp_set (comp);
}

static void message_handler_req_exec_amf_presence_state_comp_set (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_amf_presence_state_comp_set *req_exec_amf_presence_state_comp_set =
		(struct req_exec_amf_presence_state_comp_set *)message;
	struct amf_comp *comp;
	struct amf_comp *comp_compare;
	int all_set = 1;

	ENTER_VOID();

	comp = amf_find_comp (&amf_cluster, &req_exec_amf_presence_state_comp_set->name);
	assert(comp);
	if (req_exec_amf_presence_state_comp_set->presence_state == comp->saAmfCompPresenceState) {
		dprintf ("duplicate presence state set, not setting presence state\n");
		return;
	}

	if (req_exec_amf_presence_state_comp_set->presence_state == SA_AMF_PRESENCE_UNINSTANTIATED) {
		comp->conn = 0;
	}

	/*
	 * The restarting state can only be entered from the uninstantiated state
	 */
	if (req_exec_amf_presence_state_comp_set->presence_state == SA_AMF_PRESENCE_RESTARTING &&
		comp->saAmfCompPresenceState != SA_AMF_PRESENCE_UNINSTANTIATED) {

		dprintf ("restart presence state set even though not in terminating state\n");
		return;
	}

	comp->saAmfCompPresenceState = req_exec_amf_presence_state_comp_set->presence_state;
	if (comp->saAmfCompPresenceState == SA_AMF_PRESENCE_RESTARTING) {
		dprintf ("SET TO RESTARTING instantiating now\n");
		clc_instantiate (comp);
	}

	dprintf ("Setting component %s presence state %s\n",
		getSaNameT (&comp->name),
		presence_state_text[comp->saAmfCompPresenceState]);
	
	/*
	 * Restart components that are requested to enter the restarting presence state
	 */

	/*
	 * If all comp presence states are INSTANTIATED, then SU should be instantated
	 */
	for (comp_compare = comp->su->comp_head; comp_compare != NULL; comp_compare = comp->next) {
		if (comp_compare->saAmfCompPresenceState != SA_AMF_PRESENCE_INSTANTIATED) {
			all_set = 0;
			break;
		}
	}

	if (all_set) {
		presence_state_unit_set (comp->su, 
			SA_AMF_PRESENCE_INSTANTIATED);
	}
}

static void message_handler_req_exec_amf_administrative_state_csi_set (
	void *message,
	unsigned int nodeid)
{
//	struct req_exec_amf_administrative_state_csi_set *req_exec_amf_administrative_state_csi_set =
//		(struct req_exec_amf_administrative_state_csi_set *)message;
// TODO
}
static void message_handler_req_exec_amf_administrative_state_unit_set (
	void *message,
	unsigned int nodeid)
{
//	struct req_exec_amf_administrative_state_unit_set *req_exec_amf_administrative_state_unit_set =
//		(struct req_exec_amf_administrative_state_unit_set *)message;
// TODO
}
static void message_handler_req_exec_amf_administrative_state_group_set (
	void *message,
	unsigned int nodeid)
{
//	struct req_exec_amf_administrative_state_group_set *req_exec_amf_administrative_state_group_set =
//		(struct req_exec_amf_administrative_state_group_set *)message;
// TODO
}


/*
 * Library Interface Implementation
 */
static void message_handler_req_lib_amf_componentregister (
	void *conn,
	 void *msg)
{
	struct req_lib_amf_componentregister *req_lib_amf_componentregister =
		(struct req_lib_amf_componentregister *)msg;
	struct res_lib_amf_componentregister res_lib_amf_componentregister;
	struct amf_comp *comp;
	struct amf_pd *amf_pd = (struct amf_pd *)openais_conn_private_data_get (conn);
	SaAisErrorT error = SA_AIS_ERR_NOT_EXIST;

	comp = amf_find_comp (&amf_cluster, &req_lib_amf_componentregister->compName);
	if (comp) {
		presence_state_comp_set (comp,
			SA_AMF_PRESENCE_INSTANTIATED);
		operational_state_comp_set (comp,
			SA_AMF_OPERATIONAL_ENABLED);
		comp->conn = conn;
		amf_pd->comp = comp;
		comp_healthcheck_activate (comp);
		error = SA_AIS_OK;
	}

	res_lib_amf_componentregister.header.id = MESSAGE_RES_AMF_COMPONENTREGISTER;
	res_lib_amf_componentregister.header.size = sizeof (struct res_lib_amf_componentregister);
	res_lib_amf_componentregister.header.error = error;
	openais_conn_send_response (conn, &res_lib_amf_componentregister,
		sizeof (struct res_lib_amf_componentregister));
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
	struct req_lib_amf_healthcheckstart *req_lib_amf_healthcheckstart =
		(struct req_lib_amf_healthcheckstart *)msg;
	struct res_lib_amf_healthcheckstart res_lib_amf_healthcheckstart;
	struct amf_healthcheck *healthcheck;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	ENTER();

	comp = amf_find_comp (&amf_cluster, &req_lib_amf_healthcheckstart->compName);
	if (comp == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck = amf_find_healthcheck (comp, &req_lib_amf_healthcheckstart->healthcheckKey);
	if (healthcheck == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

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
	healthcheck->invocationType = req_lib_amf_healthcheckstart->invocationType;
	healthcheck->timer_handle_duration = 0;
	healthcheck->timer_handle_period = 0;
	healthcheck->active = 0;

	if (comp->conn == NULL) {
		comp->conn = conn;
	}

	healthcheck_activate (healthcheck);

error_exit:
	res_lib_amf_healthcheckstart.header.id = MESSAGE_RES_AMF_HEALTHCHECKSTART;
	res_lib_amf_healthcheckstart.header.size = sizeof (struct res_lib_amf_healthcheckstart);
	res_lib_amf_healthcheckstart.header.error = error;

	openais_conn_send_response (conn, &res_lib_amf_healthcheckstart,
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
	struct req_lib_amf_healthcheckstop *req_lib_amf_healthcheckstop =
		(struct req_lib_amf_healthcheckstop *)msg;
	struct res_lib_amf_healthcheckstop res_lib_amf_healthcheckstop;
	struct amf_healthcheck *healthcheck;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	dprintf ("healthcheck stop\n");
	comp = amf_find_comp (&amf_cluster, &req_lib_amf_healthcheckstop->compName);
	if (comp == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck = amf_find_healthcheck (
		comp,
		&req_lib_amf_healthcheckstop->healthcheckKey);

	dprintf ("active %p\n", healthcheck);
	if (healthcheck == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck_deactivate (healthcheck);

error_exit:
	dprintf ("healthcheck stop\n");
	res_lib_amf_healthcheckstop.header.id = MESSAGE_RES_AMF_HEALTHCHECKSTOP;
	res_lib_amf_healthcheckstop.header.size = sizeof (struct res_lib_amf_healthcheckstop);
	res_lib_amf_healthcheckstop.header.error = error;

	openais_conn_send_response (conn, &res_lib_amf_healthcheckstop,
		sizeof (struct res_lib_amf_healthcheckstop));
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
	struct req_lib_amf_componenterrorreport *req_lib_amf_componenterrorreport = (struct req_lib_amf_componenterrorreport *)msg;
	struct res_lib_amf_componenterrorreport res_lib_amf_componenterrorreport;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_ERR_NOT_EXIST;

	ENTER();
	log_printf (LOG_LEVEL_NOTICE, "Handle : message_handler_req_lib_amf_componenterrorreport()\n");

	dprintf ("ERROR REPORT\n");
	comp = amf_find_comp (&amf_cluster, &req_lib_amf_componenterrorreport->erroneousComponent);
	if (comp) {
		dprintf ("escalation policy terminate\n");
		escalation_policy_cleanup (comp);
		error = SA_AIS_OK;
	}

	res_lib_amf_componenterrorreport.header.size = sizeof (struct res_lib_amf_componenterrorreport);
	res_lib_amf_componenterrorreport.header.id = MESSAGE_RES_AMF_COMPONENTERRORREPORT;
	res_lib_amf_componenterrorreport.header.error = error;

	openais_conn_send_response (
		conn, &res_lib_amf_componenterrorreport,
		sizeof (struct res_lib_amf_componenterrorreport));
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
	struct req_lib_amf_response *req_lib_amf_response = (struct req_lib_amf_response *)msg;
	struct res_lib_amf_response res_lib_amf_response;
	struct csi_set_callback_data *csi_set_callback_data;
	struct csi_remove_callback_data *csi_remove_callback_data;
	struct component_terminate_callback_data *component_terminate_callback_data;
	struct amf_healthcheck *healthcheck_active;
	int interface;
	int res;
	void *data;
	SaAisErrorT error = SA_AIS_OK;

	ENTER_VOID();

	res = invocation_get_and_destroy (req_lib_amf_response->invocation,
		&interface, &data);

	if (res == -1) {
		dprintf ("invocation not found\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	switch (interface) {
	case AMF_RESPONSE_HEALTHCHECKCALLBACK:
		dprintf ("healtcheck callback executed from library.\n");
		healthcheck_active = (struct amf_healthcheck*)data;

		poll_timer_delete (aisexec_poll_handle,
			healthcheck_active->timer_handle_duration);

		healthcheck_active->timer_handle_duration = 0;

		poll_timer_add (aisexec_poll_handle,
			healthcheck_active->saAmfHealthcheckPeriod,
			(void *)healthcheck_active,
			timer_function_healthcheck_next,
			&healthcheck_active->timer_handle_period);
		break;

	case AMF_RESPONSE_CSISETCALLBACK:
		csi_set_callback_data = (struct csi_set_callback_data *)data;

		dprintf ("csi set callback executed from library.\n");
//		list_add (&csi_set_callback_data->comp->
				/*
		pg_comp_create (
			csi_set_callback_data->pg,
			csi_set_callback_data->csi,
			csi_set_callback_data->comp);
			*/

		free (csi_set_callback_data);
		break;

	case AMF_RESPONSE_CSIREMOVECALLBACK:
		csi_remove_callback_data = (struct csi_remove_callback_data *)data;
		dprintf ("response from removing the CSI\n");
// AAAA
//		list_del (&csi_remove_callback_data->csi->si->su_list);
//		list_del (&csi_remove_callback_data->csi->si_csi_list);
		free (csi_remove_callback_data);
		break;


	case AMF_RESPONSE_COMPONENTTERMINATECALLBACK:
		component_terminate_callback_data = (struct component_terminate_callback_data *)data;

		dprintf ("response from terminating component\n");
		comp_healthcheck_deactivate (component_terminate_callback_data->comp);

		escalation_policy_restart (component_terminate_callback_data->comp);
		break;

	default:
		// TODO
		log_printf (LOG_LEVEL_ERROR, "invalid invocation value %x\n", req_lib_amf_response->invocation);
		break;
	}

error_exit:
	res_lib_amf_response.header.id = MESSAGE_RES_AMF_RESPONSE;
	res_lib_amf_response.header.size = sizeof (struct res_lib_amf_response);
	res_lib_amf_response.header.error = SA_AIS_OK;
	openais_conn_send_response (conn, &res_lib_amf_response,
		sizeof (struct res_lib_amf_response));

	LEAVE_VOID();
}

static void amf_dump_comp (struct amf_comp *comp ,void *data)
{
#if 0
	int	level = LOG_LEVEL_NOTICE;
	data = NULL;
	struct list_head* cs_types;
	struct amf_cs_type* cs_type;

	log_printf (level, "----------------\n" );
	log_printf (level, "source_addr              = %s\n",
				inet_ntoa (comp->source_addr));
	log_printf (level, "unit                     = %s\n",
				comp->su->name.value);
	log_printf (level, "name                     = %s\n", comp->name.value);
	log_printf (level, "cs types\n");
	for (cs_types = comp->cs_types.next;
		 cs_types != &comp->cs_types;
		 cs_types = cs_types->next) {
		cs_type = list_entry (cs_types, struct amf_cs_type, comp_cs_type_list);
		log_printf (level, "   name      = %s\n" , cs_type->attr_name.value);
	}

	log_printf (level, "category                 = %u\n", comp->category);
	log_printf (level, "capability               = %u\n", comp->capability);
	log_printf (level, "num_max_active_csi       = %u\n",
				comp->num_max_active_csi);
	log_printf (level, "num_max_standby_csi      = %u\n",
				comp->num_max_standby_csi);
	log_printf (level, "default_clc_cli_timeout  = %u\n",
				comp->default_clc_cli_timeout);
	log_printf (level, "default_callback_timeout = %u\n",
				comp->default_callback_timeout);
	log_printf (level, "oper state               = %s\n",
				oper_state_text[comp->saAmfCompOperState]);
	log_printf (level, "readiness state          = %s\n",
				readiness_state_text[comp->saAmfCompReadinessState]);
	log_printf (level, "presence state           = %s\n",
				presence_state_text[comp->saAmfCompPresenceState]);
	log_printf (level, "restart_count            = %u\n",
				comp->restart_count);
#endif
}

static void enumerate_components (
	void (*function)(struct amf_comp *, void *data),
	void *data)
{
	#if 0
	struct list_head *AmfGroupList;
	struct list_head *AmfUnitList;
	struct list_head *AmfComponentList;

	struct amf_sg *saAmfGroup;
	struct amf_su *AmfUnit;
	struct amf_comp *AmfComponent;


	/*
	 * Search all groups
	 */
	for (AmfGroupList = amf_sg_head.next;
		AmfGroupList != &amf_sg_head;
		AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList,
			struct amf_sg, application_sg_list);

		/*
		 * Search all units
		 */
		for (AmfUnitList = saAmfGroup->su_head.next;
			AmfUnitList != &saAmfGroup->su_head;
			AmfUnitList = AmfUnitList->next) {

			AmfUnit = list_entry (AmfUnitList,
				struct amf_su, sg_su_list);

			/*
			 * Search all components
			 */
 			for (AmfComponentList = AmfUnit->comp_head.next;
				AmfComponentList != &AmfUnit->comp_head;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList,
					struct amf_comp, su_comp_list);

				function (AmfComponent, data);
			}
		}
	}
	#endif
}

static void amf_dump ( )
{
	enumerate_components (amf_dump_comp, NULL);
}

