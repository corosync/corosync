/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
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
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "totempg.h"
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "amfconfig.h"
#include "main.h"
#include "service.h"
#include "objdb.h"

#define LOG_SERVICE LOG_SERVICE_AMF
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

struct invocation {
	void *data;
	int interface;
	int active;
};

struct invocation *invocation_entries = 0;

int invocation_entries_size = 0;
int waiting = 0;

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
	
struct healthcheck_active {
	SaAmfHealthcheckKeyT key;
	SaAmfHealthcheckInvocationT invocationType;
	SaAmfRecommendedRecoveryT recommendedRecovery;
	struct amf_comp *comp;
	struct amf_healthcheck *healthcheck;
	poll_timer_handle timer_healthcheck_duration;
	poll_timer_handle timer_healthcheck_period;
	struct list_head list;
	int active;
};

static char *presencestate_ntoa (SaAmfPresenceStateT state);
static char *operationalstate_ntoa (SaAmfOperationalStateT state);
static char *hastate_ntoa (SaAmfHAStateT state);
static char *readinessstate_ntoa (int state);

static void amf_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
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

static void message_handler_req_lib_amf_protectiongrouptrackstart (void *conn, void *msg);

static void message_handler_req_lib_amf_protectiongrouptrackstop (void *conn, void *msg);

static void message_handler_req_lib_amf_componenterrorreport (void *conn, void *msg);

static void message_handler_req_lib_amf_componenterrorclear (void *conn, void *msg);

static void message_handler_req_lib_amf_response (void *conn, void *msg);

static void message_handler_req_exec_amf_operational_state_comp_set (
	void *message,
	struct totem_ip_address *source);

static void message_handler_req_exec_amf_presence_state_comp_set (
	void *message,
	struct totem_ip_address *source);

static void message_handler_req_exec_amf_administrative_state_csi_set (
	void *message,
	struct totem_ip_address *source);

static void message_handler_req_exec_amf_administrative_state_unit_set (
	void *message,
	struct totem_ip_address *source);

static void message_handler_req_exec_amf_administrative_state_group_set (
	void *message,
	struct totem_ip_address *source);

void presence_state_comp_set (
	struct amf_comp *comp,
	SaAmfPresenceStateT presence_state);

void operational_state_comp_set (
	struct amf_comp *comp,
	SaAmfOperationalStateT operational_state);

void operational_state_unit_set (
	struct amf_unit *unit,
	SaAmfOperationalStateT operational_state);

int clc_instantiate_all (void);
int clc_instantiate (struct amf_comp *comp);
int clc_terminate (struct amf_comp *comp);

int clc_cli_instantiate (struct amf_comp *comp);
int clc_instantiate_callback (struct amf_comp *comp);
int clc_csi_set_callback (struct amf_comp *comp);
int clc_cli_terminate (struct amf_comp *comp);
int clc_terminate_callback (struct amf_comp *comp);
int clc_csi_remove_callback (struct amf_comp *comp);
int clc_cli_cleanup (struct amf_comp *comp);
int clc_cli_cleanup_local (struct amf_comp *comp);
void healthcheck_activate (struct healthcheck_active *healthcheck_active);
void healthcheck_deactivate (struct healthcheck_active *healthcheck_active);
void comp_healthcheck_activate (struct amf_comp *comp);
void comp_healthcheck_deactivate (struct amf_comp *comp);
static void escalation_policy_restart (struct amf_comp *comp);

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
struct clc_interface clc_interface_sa_aware = {
	clc_cli_instantiate,
	clc_terminate_callback,
	clc_cli_cleanup
};

struct clc_interface clc_interface_proxied_pre = {
	clc_instantiate_callback,
	clc_terminate_callback,
	clc_cli_cleanup
};

struct clc_interface clc_interface_proxied_non_pre = {
	clc_csi_set_callback,
	clc_csi_remove_callback,
	clc_cli_cleanup_local
};

struct clc_interface clc_interface_non_proxied_non_saware = {
	clc_cli_instantiate,
	clc_cli_terminate,
	clc_cli_cleanup_local
};

struct clc_interface *clc_interfaces[4] = {
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
		.lib_handler_fn		= message_handler_req_lib_amf_protectiongrouptrackstart,
		.response_size		= sizeof (struct res_lib_amf_protectiongrouptrackstart),
		.response_id		= MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTART,
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

void amf_dump(void);
/*
 * Exports the interface for the service
 */
struct openais_service_handler amf_service_handler = {
	.name				= (unsigned char *)"openais availability management framework B.01.01",
	.id				= AMF_SERVICE,
	.private_data_size		= sizeof (struct amf_pd),
	.lib_init_fn			= amf_lib_init_fn,
	.lib_exit_fn			= amf_lib_exit_fn,
	.lib_service			= amf_lib_service,
	.lib_service_count		= sizeof (amf_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn			= amf_exec_init_fn,
	.exec_service			= amf_exec_service,
	.exec_service_count		= sizeof (amf_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn			= amf_confchg_fn,
    .exec_dump_fn       = amf_dump
};

struct openais_service_handler *amf_get_handler_ver0 (void);

struct openais_service_handler_iface_ver0 amf_service_handler_iface = {
	.openais_get_service_handler_ver0	= amf_get_handler_ver0
};

struct lcr_iface openais_amf_ver0[1] = {
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

struct lcr_comp amf_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_amf_ver0
};

struct openais_service_handler *amf_get_handler_ver0 (void)
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

int invocation_create (
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

int invocation_get_and_destroy (int invocation, int *interface,
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

void invocation_destroy_by_data (void *data)
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


void *clc_command_run (void *context)
{
	struct clc_command_run_data *clc_command_run_data = (struct clc_command_run_data *)context;
	pid_t pid;
	int res;
	char *argv[10];
	char *envp[10];
	int status;
	char cmd[1024];
	char env_comp_binary_name[1024];
	char env_comp_binary_path[1024];
	char env_comp_name[1024];
	char *binary_to_run = NULL;
	char *binary_path = NULL;
	char *clc_cli_interface = NULL;

	sleep (1);

printf ("clc_command_run()\n");
	pid = fork();

	if (pid == -1) {
		printf ("Couldn't fork process %s\n", strerror (errno));
		return (0);
	}

	if (pid) {
		waiting = 1;
printf ("waiting for pid %d to finish\n", pid);
		waitpid (pid, &status, 0);
		if (clc_command_run_data->completion_callback) {
			clc_command_run_data->completion_callback (context);
		}
		pthread_exit(0);
	}

	switch (clc_command_run_data->type) {
		case CLC_COMMAND_RUN_OPERATION_TYPE_INSTANTIATE:
			binary_to_run = clc_command_run_data->comp->instantiate_cmd;
			clc_cli_interface = "CLC_CLI_INTERFACE=instantiate";
			break;

		case CLC_COMMAND_RUN_OPERATION_TYPE_TERMINATE:
			binary_to_run = clc_command_run_data->comp->terminate_cmd;
			clc_cli_interface = "CLC_CLI_INTERFACE=terminate";
			break;

		case CLC_COMMAND_RUN_OPERATION_TYPE_CLEANUP:
			binary_to_run = clc_command_run_data->comp->cleanup_cmd;
			clc_cli_interface = "CLC_CLI_INTERFACE=cleanup";
			break;
		default:
			assert (0 != 1);
			break;
	}

	if (strlen (clc_command_run_data->comp->clccli_path)) {
		sprintf (cmd, "%s/%s",
			clc_command_run_data->comp->clccli_path,
			binary_to_run);
	} else
	if (strlen (clc_command_run_data->comp->unit->clccli_path)) {
		sprintf (cmd, "%s/%s",
			clc_command_run_data->comp->unit->clccli_path,
			binary_to_run);
	} else {
		sprintf (cmd, "%s/%s",
			clc_command_run_data->comp->unit->amf_group->clccli_path,
			binary_to_run);
	}

	if (strlen (clc_command_run_data->comp->binary_path)) {
		binary_path = clc_command_run_data->comp->binary_path;
	} else
	if (strlen (clc_command_run_data->comp->unit->binary_path)) {
		binary_path = clc_command_run_data->comp->unit->binary_path;
	} else {
		binary_path = clc_command_run_data->comp->unit->amf_group->binary_path;
	}

	argv[0] = cmd;
	argv[1] = '\0';

	envp[0] = cmd;
	envp[1] = clc_cli_interface;
	envp[2] = env_comp_binary_name;
	envp[3] = env_comp_binary_path;
	envp[4] = env_comp_name;
	envp[5] = '\0';

	sprintf (env_comp_binary_name, "COMP_BINARY_NAME=%s",
		clc_command_run_data->comp->binary_name);

	sprintf (env_comp_binary_path, "COMP_BINARY_PATH=%s",
		binary_path);

	strcpy (env_comp_name, "SA_AMF_COMPONENT_NAME=");

	strncat (env_comp_name, (char *)clc_command_run_data->comp->name.value,
		clc_command_run_data->comp->name.length);

	if (cmd[0] == '\0') {
		return (0);
	}
	printf ("running command '%s' with environment:\n", cmd);
	printf ("0 %s\n", envp[0]);
	printf ("1 %s\n", envp[1]);
	printf ("2 %s\n", envp[2]);
	printf ("3 %s\n", envp[3]);
	printf ("4 %s\n", envp[4]);
		
	res = execve (cmd, argv, envp);
	if (res == -1) {
		printf ("Couldn't exec process %d=%s\n", errno, strerror (errno));
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
int clc_cli_instantiate (struct amf_comp *comp)
{
	int res;
	pthread_t thread;

	struct clc_command_run_data *clc_command_run_data;

	printf ("clc_cli_instaniate\n");
	clc_command_run_data = malloc (sizeof (struct clc_command_run_data));
	clc_command_run_data->comp = comp;
	clc_command_run_data->type = CLC_COMMAND_RUN_OPERATION_TYPE_INSTANTIATE;
	clc_command_run_data->completion_callback = NULL;
	res = pthread_create (&thread, NULL, clc_command_run, (void *)clc_command_run_data);
	pthread_detach (thread);
// TODO error code from pthread_create
	return (res);
}

int clc_instantiate_callback (struct amf_comp *comp)
{
	printf ("clc_instantiate_callback\n");
	return (0);
}

int clc_csi_set_callback (struct amf_comp *comp)
{
	printf ("clc_csi_set_callback\n");
	return (0);
}

/*
 * Terminate possible operations
 */
int clc_cli_terminate (struct amf_comp *comp)
{
	printf ("clc_cli_terminate\n");
	return (0);
}
int clc_terminate_callback (struct amf_comp *comp)
{
	struct res_lib_amf_componentterminatecallback res_lib_amf_componentterminatecallback;
	struct component_terminate_callback_data *component_terminate_callback_data;

	printf ("clc_terminate_callback %p\n", comp->conn);
	if (comp->presence_state != SA_AMF_PRESENCE_INSTANTIATED) {
		printf ("component terminated but not instantiated %s - %d\n",
			getSaNameT (&comp->name), comp->presence_state);
		assert (0);
		return (0);
	}

printf ("component name terminating %s\n", getSaNameT (&comp->name));
printf ("component presence state %d\n", comp->presence_state);

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
printf ("Creating invocation %llu", 
	(unsigned long long)res_lib_amf_componentterminatecallback.invocation);
				        
	openais_conn_send_response (
		openais_conn_partner_get (comp->conn),
		&res_lib_amf_componentterminatecallback,
		sizeof (struct res_lib_amf_componentterminatecallback));

	return (0);
}

int clc_csi_remove_callback (struct amf_comp *comp)
{
	printf ("clc_tcsi_remove_callback\n");
	return (0);
}

/*
 * This reinstantiates the cleaned up component
 */
void clc_cleanup_completion_callback (void *context) {
	struct clc_command_run_data *clc_command_run_data = (struct clc_command_run_data *)context;
	
	escalation_policy_restart (clc_command_run_data->comp);
}
			
/*
 * Cleanup possible operations
 */
int clc_cli_cleanup (struct amf_comp *comp)
{
	int res;
	pthread_t thread;

	struct clc_command_run_data *clc_command_run_data;

	printf ("clc_cli_instaniate\n");
	clc_command_run_data = malloc (sizeof (struct clc_command_run_data));
	clc_command_run_data->comp = comp;
	clc_command_run_data->type = CLC_COMMAND_RUN_OPERATION_TYPE_CLEANUP;
	clc_command_run_data->completion_callback = clc_cleanup_completion_callback;

	res = pthread_create (&thread, NULL, clc_command_run, (void *)clc_command_run_data);
	pthread_detach (thread);
// TODO error code from pthread_create
	return (res);
	return (0);
}

int clc_cli_cleanup_local (struct amf_comp *comp)
{
	printf ("clc_cli_cleanup_local\n");
	return (0);
}

int clc_instantiate (struct amf_comp *comp)
{
	int res;

	printf ("clc instantiate for comp %s\n", getSaNameT (&comp->name));

	presence_state_comp_set (comp, SA_AMF_PRESENCE_INSTANTIATING);
	res = clc_interfaces[comp->comptype]->instantiate (comp);
	return (res);
}

int clc_terminate (struct amf_comp *comp)
{
	int res;

	printf ("clc terminate for comp %s\n", getSaNameT (&comp->name));
assert (0);
	operational_state_comp_set (comp, SA_AMF_OPERATIONAL_DISABLED);
	presence_state_comp_set (comp, SA_AMF_PRESENCE_TERMINATING);

	res = clc_interfaces[comp->comptype]->terminate (comp);
	return (0);
}

int clc_cleanup (struct amf_comp *comp)
{
	int res;

	printf ("clc cleanup for comp %s\n", getSaNameT (&comp->name));
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

	if (enabled) {
		res = openais_amf_config_read (&error_string);
		if (res == -1) {
			log_printf (LOG_LEVEL_ERROR, error_string);
			return res;
		}

		clc_instantiate_all ();
	}
	return (0);
}
static void amf_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
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

int amf_lib_exit_fn (void *conn)
{
	struct amf_comp *comp;
	struct amf_pd *amf_pd = (struct amf_pd *)openais_conn_private_data_get (conn);

	comp = amf_pd->comp;

	if (comp) {
		comp->conn = 0;

printf ("setting in exit fn to uninst for comp %p\n", comp);
		presence_state_comp_set (
			comp,
			SA_AMF_PRESENCE_UNINSTANTIATED);

		operational_state_comp_set (
			comp,
			SA_AMF_OPERATIONAL_DISABLED);

		comp_healthcheck_deactivate (comp);
	}
	return (0);
}

static int amf_lib_init_fn (void *conn)
{
	log_printf (LOG_LEVEL_DEBUG, "Got request to initalize availability management framework service.\n"); 
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
	component = find_comp (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName);
	amfProxyComponent = find_comp (&req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName);

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

DECLARE_LIST_INIT (library_notification_send_listhead);

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

struct healthcheck_active *find_healthcheck_active (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *key,
	SaAmfHealthcheckInvocationT invocation)
{
	struct list_head *list;
	struct healthcheck_active *ret_healthcheck_active = 0;
	struct healthcheck_active *healthcheck_active;

	for (list = comp->healthcheck_list.next;
		list != &comp->healthcheck_list;
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

void comp_healthcheck_activate (
	struct amf_comp *comp)
{
	struct list_head *key_list;
	struct healthcheck_active *healthcheck_active;

	for (key_list = comp->healthcheck_list.next;
		key_list != &comp->healthcheck_list;
		key_list = key_list->next) {

		healthcheck_active = list_entry (key_list,
			struct healthcheck_active, list);

		if (healthcheck_active->active == 0) {
			healthcheck_activate (healthcheck_active);
		}
	}
}

void comp_healthcheck_deactivate (
	struct amf_comp *comp)
{
	struct list_head *list;
	struct list_head *next;
	struct healthcheck_active *healthcheck_active;

	log_printf (LOG_LEVEL_NOTICE, "ZZZ comp_healthcheck_deactivate %s\n",
		getSaNameT (&comp->name));

	for (list = comp->healthcheck_list.next, next = list->next;
		list != &comp->healthcheck_list;
		list = next, next = list->next) {

		healthcheck_active = list_entry (list,
			struct healthcheck_active, list);

		printf ("healthcheck deactivating %p\n", healthcheck_active);
		healthcheck_deactivate (healthcheck_active);
	}
}

void presence_state_comp_set (
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

void readiness_state_comp_set (struct amf_comp *comp)
{
	printf ("inputs to readiness_state_comp_set\n");
	printf ("\tunit readiness state %s\n",
		readinessstate_ntoa (comp->unit->readiness_state));
	printf ("\tcomp operational state %s\n",
		operationalstate_ntoa (comp->unit->readiness_state));

	/*
	 * Set component readiness state appropriately
	 * if unit in service and component is enabled, it is in service
	 * otherwise it is out of service page 37
	 */
	if (comp->unit->readiness_state == SA_AMF_READINESS_IN_SERVICE &&
		comp->operational_state == SA_AMF_OPERATIONAL_ENABLED) {
		comp->readiness_state = SA_AMF_READINESS_IN_SERVICE;
	} else {
		comp->readiness_state = SA_AMF_READINESS_OUT_OF_SERVICE;
	}
	printf ("readiness_state_comp_set (%s)\n",
		operationalstate_ntoa (comp->operational_state));
}

void operational_state_comp_set (struct amf_comp *comp, SaAmfOperationalStateT operational_state)
{
	struct req_exec_amf_operational_state_comp_set req_exec_amf_operational_state_comp_set;
	struct iovec iovec;

	req_exec_amf_operational_state_comp_set.header.size = sizeof (struct req_exec_amf_operational_state_comp_set);
	req_exec_amf_operational_state_comp_set.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_OPERATIONAL_STATE_COMP_SET);

	req_exec_amf_operational_state_comp_set.operational_state = operational_state;
	memcpy (&req_exec_amf_operational_state_comp_set.name,
		&comp->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_amf_operational_state_comp_set;
	iovec.iov_len = sizeof (req_exec_amf_operational_state_comp_set);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
}

void csi_comp_set_callback (
	struct amf_comp *comp,
	struct amf_csi *csi,
	struct amf_pg *pg)
{
    struct list_head *name_value_list;
    struct res_lib_amf_csisetcallback* res_lib_amf_csisetcallback;     
    void*  p;

    struct csi_set_callback_data *csi_set_callback_data;
    struct amf_csi_name_value *name_value;



    size_t char_legnth_of_csi_attrs=0;
    size_t num_of_csi_attrs=0;
    printf("\t   Assigning CSI %s to component\n", getSaNameT (&csi->name));

    for (name_value_list = csi->name_value_head.next;
	name_value_list != &csi->name_value_head;
	name_value_list = name_value_list->next) {
	num_of_csi_attrs++;
	name_value = list_entry (name_value_list, struct amf_csi_name_value, csi_name_list);
	printf("\t\tname = %s, value = %s\n", name_value->name, name_value->value);
	char_legnth_of_csi_attrs += strlen(name_value->name);
	char_legnth_of_csi_attrs += strlen(name_value->value);
	char_legnth_of_csi_attrs += 2;
    }
    p = malloc(sizeof(struct res_lib_amf_csisetcallback)+
	       char_legnth_of_csi_attrs);


    assert(p);

    res_lib_amf_csisetcallback = (struct res_lib_amf_csisetcallback*)p;




    
    /* Address of the buffer containing the Csi name value pair  */
    char* csi_attribute_buf = res_lib_amf_csisetcallback->csi_attr_buf;
				       




   /* Byteoffset start att the zero byte  */
   unsigned int byte_offset = 0;

   for (name_value_list = csi->name_value_head.next;
	 name_value_list != &csi->name_value_head;
	 name_value_list = name_value_list->next) {
       
	  name_value = list_entry (name_value_list, struct amf_csi_name_value, csi_name_list);
	  
	  strcpy(&csi_attribute_buf[byte_offset],
		 (char*)name_value->name);

	  byte_offset += strlen(name_value->name) + 1;

	  strcpy(&csi_attribute_buf[byte_offset],
		 (char*)name_value->value);

	  byte_offset += strlen(name_value->value) + 1;
   }

   res_lib_amf_csisetcallback->number = num_of_csi_attrs;
      

   res_lib_amf_csisetcallback->csiFlags = SA_AMF_CSI_ADD_ONE;  

   switch (comp->unit->requested_ha_state) {
      case SA_AMF_HA_ACTIVE:
	  {
	      res_lib_amf_csisetcallback->csiStateDescriptor.activeDescriptor.activeCompName.length = 0;
	      res_lib_amf_csisetcallback->csiStateDescriptor.activeDescriptor.transitionDescriptor =
		  SA_AMF_CSI_NEW_ASSIGN; 
	      break;
	  }
      case  SA_AMF_HA_STANDBY:
	  {
	      
	      res_lib_amf_csisetcallback->csiStateDescriptor.standbyDescriptor.activeCompName.length = 0; 
	      res_lib_amf_csisetcallback->csiStateDescriptor.standbyDescriptor.standbyRank =  1;


	      break;
	  }
      case  SA_AMF_HA_QUIESCED:
	  {
	      /*TODO*/
	      break;
	  }
      case SA_AMF_HA_QUIESCING:
	  {
	      /*TODO*/
	      break;
	  }
      default:
	  {
	      assert(SA_AMF_HA_ACTIVE||SA_AMF_HA_STANDBY||SA_AMF_HA_QUIESCING||SA_AMF_HA_QUIESCED);	      
	      break;
	  }

      }

	 
      res_lib_amf_csisetcallback->header.id = 
	  MESSAGE_RES_AMF_CSISETCALLBACK;

      res_lib_amf_csisetcallback->header.size = 
	  sizeof (struct res_lib_amf_csisetcallback)+
	  char_legnth_of_csi_attrs;

      res_lib_amf_csisetcallback->header.error = SA_AIS_OK;


	memcpy (&res_lib_amf_csisetcallback->compName,
		&comp->name, sizeof (SaNameT));

	memcpy (&res_lib_amf_csisetcallback->csiName,
		&csi->name, sizeof (SaNameT));

	res_lib_amf_csisetcallback->haState = comp->unit->requested_ha_state;
	
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

void pg_create (struct amf_si *si, struct amf_pg **pg_out)
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
void csi_unit_set_callback (struct amf_unit *unit, struct amf_si *si)
{
    struct list_head *complist;
    struct list_head *csilist;
    struct list_head *typenamelist;
    struct amf_csi *csi;
    struct amf_pg *pg;
    struct amf_comp *comp;
    struct amf_comp_csi_type_name *type_name;

//    pg_create (csi_in->si, &pg);
    // TODO remove si from csi data structure

    printf ("assigning SI %s to ",
        getSaNameT (&si->name));

    printf ("SU %s for components:\n",
        getSaNameT (&unit->name));

    /*
    ** for each component in SU, find a CSI in the SI with the same type
    */
    for (complist = unit->comp_head.next;
        complist != &unit->comp_head;
        complist = complist->next) {

        comp = list_entry (complist, struct amf_comp, comp_list);

        printf ("\t%s\n", getSaNameT (&comp->name));

        int no_of_csi_types = 0;
        for (typenamelist = comp->csi_type_name_head.next;
             typenamelist != &comp->csi_type_name_head;
             typenamelist = typenamelist->next) {

                type_name = list_entry (typenamelist, struct amf_comp_csi_type_name, list);
                no_of_csi_types++;
                int no_of_assignments = 0;

                for (csilist = si->csi_head.next;
                      csilist != &si->csi_head;
                      csilist = csilist->next) {

                        csi = list_entry (csilist, struct amf_csi, csi_list);

                        if (!memcmp(csi->type_name.value, type_name->name.value, type_name->name.length)) {
                                csi_comp_set_callback (comp, csi, pg);
                                no_of_assignments++;
                        }
                }
                if (no_of_assignments == 0) {
                    printf ("\t   No CSIs of type %s configured?!!\n",
                            getSaNameT (&type_name->name));
                }
        }
        if (no_of_csi_types == 0) {
            printf ("\t   No CSI types configured for %s ?!!\n",
                    getSaNameT (&comp->name));
                }
    }
} 

void csi_comp_remove_callback (struct amf_comp *comp, struct amf_csi *csi)
{
	struct res_lib_amf_csiremovecallback res_lib_amf_csiremovecallback;
	struct csi_remove_callback_data *csi_remove_callback_data;

	printf ("\t%s\n",
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

extern struct list_head amf_groupHead;

int clc_instantiate_all (void) {
	struct list_head *list_group;
	struct amf_group *group;
	struct list_head *list_unit;
	struct amf_unit *unit;
	struct list_head *list_comp;
	struct amf_comp *comp;

        for (list_group = amf_groupHead.next;
                list_group != &amf_groupHead;
                list_group = list_group->next) {

                group = list_entry (list_group,
                        struct amf_group, group_list);

		for (list_unit = group->unit_head.next;
			list_unit != &group->unit_head;
			list_unit = list_unit->next) {

			unit = list_entry (list_unit,
				struct amf_unit, unit_list);

			for (list_comp = unit->comp_head.next;
				list_comp != &unit->comp_head;
				list_comp = list_comp->next) {

				comp = list_entry (list_comp,
					struct amf_comp, comp_list);

				if (strlen ((char *)comp->instantiate_cmd)) {

					clc_instantiate (comp);
				}
			}
		}
	}
	return (0);
}

void comp_terminate (struct amf_comp *comp)
{
	clc_terminate (comp);
}

void unit_terminate (struct amf_unit *unit)
{
	struct list_head *list_comp;
	struct amf_comp *comp;

	for (list_comp = unit->comp_head.next;
		list_comp != &unit->comp_head;
		list_comp = list_comp->next) {

		comp = list_entry (list_comp, struct amf_comp, comp_list);

		clc_terminate (comp);
	}

}

void comp_cleanup (struct amf_comp *comp)
{
	clc_cleanup (comp);
}

void unit_cleanup (struct amf_unit *unit)
{
	struct list_head *list_comp;
	struct amf_comp *comp;

	for (list_comp = unit->comp_head.next;
		list_comp != &unit->comp_head;
		list_comp = list_comp->next) {

		comp = list_entry (list_comp, struct amf_comp, comp_list);

		clc_cleanup (comp);
	}

}
	
void comp_restart (struct amf_comp *comp)
{
	presence_state_comp_set (comp, SA_AMF_PRESENCE_RESTARTING);
}

void unit_restart (struct amf_unit *unit)
{
	struct list_head *list_comp;
	struct amf_comp *comp;

	for (list_comp = unit->comp_head.next;
		list_comp != &unit->comp_head;
		list_comp = list_comp->next) {

		comp = list_entry (list_comp, struct amf_comp, comp_list);
		presence_state_comp_set (comp, SA_AMF_PRESENCE_RESTARTING);
	}
}

void clc_unit_instantiate (struct amf_unit *unit)
{
	struct list_head *list_comp;
	struct amf_comp *comp;

printf ("ZZZZZZZZZZZZZZZZZ clc_unit_instantitate\n");
	for (list_comp = unit->comp_head.next;
		list_comp != &unit->comp_head;
		list_comp = list_comp->next) {

		comp = list_entry (list_comp, struct amf_comp, comp_list);

		clc_instantiate (comp);
	}
}

void csi_unit_remove_callbacks (struct amf_unit *unit)
{
	struct list_head *list_si;
	struct list_head *list_csi;
	struct list_head *list_comp;
	struct amf_si *si;
	struct amf_csi *csi;
	struct amf_comp *comp;

	for (list_si = unit->si_head.next;
		list_si != &unit->si_head;
		list_si = list_si->next) {

		si = list_entry (list_si, struct amf_si, unit_list);

		for (list_csi = si->csi_head.next;
			list_csi != &si->csi_head;
			list_csi = list_csi->next) {

			csi = list_entry (list_csi, struct amf_csi, csi_list);

			for (list_comp = csi->unit->comp_head.next;
				list_comp != &csi->unit->comp_head;
				list_comp = list_comp->next) {

				comp = list_entry (list_comp, struct amf_comp, comp_list);
			}
		}
	}
}
// THIS MIGHT BE GOOD FOR SOMEPTHING ELSE
#ifdef COMPILE_OUT
void csi_unit_remove_callbacks (struct amf_unit *unit)
{
	struct list_head *list_comp;
	struct list_head *list_si;
	struct list_head *list_csi;
	struct list_head *list_pg;
	struct list_head *list_pg_comp;
	struct amf_comp *comp;
	struct amf_csi *csi;
	struct amf_si *si;
	struct amf_pg *pg;
	struct amf_pg_comp *pg_comp;
	
	for (list_si = unit->si_head.next;
		list_si != &unit->si_head;
		list_si = list_si->next) {

		si = list_entry (list_si, struct amf_si, unit_list);

		for (list_pg = si->pg_head.next;
			list_pg != &si->pg_head;
			list_pg = list_pg->next) {

			pg = list_entry (list_pg, struct amf_pg, pg_list);
			printf ("pg %x\n", pg);

			for (list_pg_comp = pg->pg_comp_head.next;
				list_pg_comp != &pg->pg_comp_head;
				list_pg_comp = list_pg_comp->next) {

				pg_comp = list_entry (list_pg_comp,
					struct amf_pg_comp, list);
				printf ("pg_comp %x\n", pg_comp);
				printf ("remove component callback\n");
				csi_comp_remove_callback (
					pg_comp->comp, 
					pg_comp->csi);
			}
		}
	}
}
#endif

char csi_number = 0;
void csi_unit_create (struct amf_unit *unit, struct amf_si *si,
	struct amf_csi **csi_out)
{
	struct amf_csi *csi;

	printf ("creating csi for si %p unit %p\n", si, unit);
	si->csi_count += 1;
	csi = malloc (sizeof (struct amf_csi));
	list_init (&csi->csi_list);
	list_add (&csi->csi_list, &si->csi_head);
	list_add (&si->unit_list, &unit->si_head);
	csi->si = si;
	csi->unit = unit;
	csi->pg_set = 0;

	sprintf ((char *)csi->name.value, "CSI %d", csi_number);
	csi->name.length = strlen ((char *)csi->name.value);

	csi_number += 1;
	*csi_out = csi;
}

void ha_state_unit_set (struct amf_unit *unit, struct amf_si *si,
		SaAmfHAStateT ha_state)
{

	printf ("Assigning SI %s ", getSaNameT (&si->name));
	printf ("to SU %s ", getSaNameT (&unit->name));
	printf ("with hastate %s\n", hastate_ntoa (ha_state));

	unit->requested_ha_state = ha_state;

	csi_unit_set_callback (unit, si);
}


int unit_inservice_count (struct amf_group *group)
{
	struct list_head *list;
	struct amf_unit *unit;
	int answer = 0;

	for (list = group->unit_head.next;
		list != &group->unit_head;
		list = list->next) {

		unit = list_entry (list,
			struct amf_unit, unit_list);

		if (unit->readiness_state == SA_AMF_READINESS_IN_SERVICE) {
			answer += 1;
		}
	}
	return (answer);
}

int comp_inservice_count (struct amf_unit *unit)
{
	struct list_head *list;
	struct amf_comp *comp;
	int answer = 0;

	for (list = unit->comp_head.next;
		list != &unit->comp_head;
		list = list->next) {

		comp = list_entry (list, struct amf_comp, comp_list);
		if (comp->readiness_state == SA_AMF_READINESS_IN_SERVICE) {
			answer += 1;
		}
	}
	return (answer);
}


int si_count (struct amf_group *group)
{
	struct list_head *list_si;
	struct amf_si *si;
	int answer = 0;

	for (list_si = group->si_head.next;
		list_si != &group->si_head;
		list_si = list_si->next) {

		si = list_entry (list_si, struct amf_si, si_list);

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


void assign_nm_active (struct amf_group *group, int su_units_assign)
{
	struct amf_unit *unit;
	struct amf_si *si;
	struct list_head *list_si;
	struct list_head *list_unit;
	int assigned = 0;
	int assign_per_su = 0;
	int total_assigned = 0;

	assign_per_su = si_count (group);
	assign_per_su = div_round (assign_per_su, su_units_assign);
	if (assign_per_su > group->maximum_active_instances) {
		assign_per_su = group->maximum_active_instances;
	}

	list_si = group->si_head.next;
	list_unit = group->unit_head.next;
	while (list_unit != &group->unit_head) {
		unit = list_entry (list_unit,
			struct amf_unit, unit_list);

		if (unit->readiness_state != SA_AMF_READINESS_IN_SERVICE) {
			list_unit = list_unit->next;
			continue; /* Not in service */
		}

		assigned = 0;
		while (list_si != &group->si_head &&
			assigned < assign_per_su &&
			total_assigned < si_count (group)) {

			si = list_entry (list_si, struct amf_si, si_list);
			assigned += 1;
			total_assigned += 1;
			ha_state_unit_set (unit, si, SA_AMF_HA_ACTIVE);
			list_si = list_si->next;
		}
		list_unit = list_unit->next;
	}
}

void assign_nm_standby (struct amf_group *group, int units_assign_standby)
{
	struct amf_unit *unit;
	struct amf_si *si;
	struct list_head *list_si;
	struct list_head *list_unit;
	int assigned = 0;
	int assign_per_su = 0;

	if (units_assign_standby == 0) {
		return;
	}
	assign_per_su = si_count (group);
	assign_per_su = div_round (assign_per_su, units_assign_standby);
	if (assign_per_su > group->maximum_standby_instances) {
		assign_per_su = group->maximum_standby_instances;
	}

	list_si = group->si_head.next;
	list_unit = group->unit_head.next;
	while (list_unit != &group->unit_head) {
		unit = list_entry (list_unit,
			struct amf_unit, unit_list);

		if (unit->readiness_state != SA_AMF_READINESS_IN_SERVICE ||
			unit->requested_ha_state == SA_AMF_HA_ACTIVE) {

			list_unit = list_unit->next;
			continue; /* Not available for assignment */
		}

		assigned = 0;
		while (list_si != &group->si_head && assigned < assign_per_su) {
			si = list_entry (list_si, struct amf_si, si_list);
			assigned += 1;
			ha_state_unit_set (unit, si, SA_AMF_HA_STANDBY);
			list_si = list_si->next;
		}
		list_unit = list_unit->next;
	}
}

void assign_nm_spare (struct amf_group *group)
{
	struct amf_unit *unit;
	struct list_head *list;

	for (list = group->unit_head.next;
		list != &group->unit_head;
		list = list->next) {

		unit = list_entry (list,
			struct amf_unit, unit_list);

		if (unit->readiness_state == SA_AMF_READINESS_IN_SERVICE &&
			(unit->requested_ha_state != SA_AMF_HA_ACTIVE &&
			unit->requested_ha_state != SA_AMF_HA_STANDBY)) {

			printf ("Assigning to SU %s with SPARE\n",
				getSaNameT (&unit->name));

		}
	}
}

void clear_requested_ha_state (struct amf_group *group)
{
	struct list_head *list;
	struct amf_unit *unit;

	for (list = group->unit_head.next;
		list != &group->unit_head;
		list = list->next) {

		unit = list_entry (list,
			struct amf_unit, unit_list);

		unit->requested_ha_state = 0;
	}

	csi_number = 0;
}

void assign_sis (struct amf_group *group)
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
		group->maximum_active_instances);

	standby_sus_needed = div_round (si_count(group),
		group->maximum_standby_instances);

	units_for_active = inservice_count - group->preferred_standby_units;
	if (units_for_active < 0) {
		units_for_active = 0;
	}

	units_for_standby = inservice_count - group->preferred_active_units;
	if (units_for_standby < 0) {
		units_for_standby = 0;
	}

	ii_spare = inservice_count - group->preferred_active_units - group->preferred_standby_units;
	if (ii_spare < 0) {
		ii_spare = 0;
	}

	/*
	 * Determine number of active and standby service units
	 * to assign based upon reduction procedure
	 */
	if ((inservice_count - active_sus_needed) < 0) {
		printf ("assignment VI - partial assignment with SIs drop outs\n");

		su_active_assign = active_sus_needed;
		su_standby_assign = 0;
		su_spare_assign = 0;
	} else
	if ((inservice_count - active_sus_needed - standby_sus_needed) < 0) {
		printf ("assignment V - partial assignment with reduction of standby units\n");

		su_active_assign = active_sus_needed;
		if (standby_sus_needed > units_for_standby) {
			su_standby_assign = units_for_standby;
		} else {
			su_standby_assign = standby_sus_needed;
		}
		su_spare_assign = 0;
	} else
	if ((group->maximum_standby_instances * units_for_standby) <= si_count (group)) {
		printf ("IV: full assignment with reduction of active service units\n");
		su_active_assign = inservice_count - standby_sus_needed;
		su_standby_assign = standby_sus_needed;
		su_spare_assign = 0;
	} else 
	if ((group->maximum_active_instances * units_for_active) <= si_count (group)) {

		printf ("III: full assignment with reduction of standby service units\n");
		su_active_assign = group->preferred_active_units;
		su_standby_assign = units_for_standby;
		su_spare_assign = 0;
	} else
	if (ii_spare == 0) {
		printf ("II: full assignment with spare reduction\n");

		su_active_assign = group->preferred_active_units;
		su_standby_assign = group->preferred_standby_units;
		su_spare_assign = 0;
	} else {
		printf ("I: full assignment with spares\n");

		su_active_assign = group->preferred_active_units;
		su_standby_assign = group->preferred_standby_units;
		su_spare_assign = ii_spare;
	}

	printf ("(inservice=%d) (assigning active=%d) (assigning standby=%d) (assigning spares=%d)\n",
		inservice_count, su_active_assign, su_standby_assign, su_spare_assign);
	assign_nm_active (group, su_active_assign);
	assign_nm_standby (group, su_standby_assign);
}

void readiness_state_unit_set (struct amf_unit *unit, SaAmfReadinessStateT readiness_state)
{
	printf ("Assigning unit %s ",
		getSaNameT (&unit->name));
	printf ("readiness state %s\n",
		readinessstate_ntoa (readiness_state));

	unit->readiness_state = readiness_state;
	assign_sis (unit->amf_group);
}

void presence_state_unit_set (struct amf_unit *unit, SaAmfPresenceStateT presence_state)
{
	printf ("Setting service unit presence state %s\n",
		presencestate_ntoa (presence_state));
}


static void escalation_policy_restart (struct amf_comp *comp)
{
	printf ("escalation_policy_restart %d\n", comp->unit->escalation_level);
printf ("escalation policy restart uninsint %p\n", comp);
	presence_state_comp_set (
		comp,
		SA_AMF_PRESENCE_UNINSTANTIATED);

	operational_state_comp_set (
		comp,
		SA_AMF_OPERATIONAL_DISABLED);

	switch (comp->unit->escalation_level) {

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

	switch (comp->unit->escalation_level) {
	case ESCALATION_LEVEL_NO_ESCALATION:
		comp->unit->restart_count += 1;
		if (comp->unit->restart_count >= comp->unit->amf_group->component_restart_max) {
			comp->unit->escalation_level = ESCALATION_LEVEL_ONE;
			escalation_policy_cleanup (comp);
			comp->unit->restart_count = 0;
			return;
		}
		printf ("Escalation level 0 - restart component\n");
		printf ("Cleaning up and restarting component.\n");
		comp_cleanup (comp);
		break;

	case ESCALATION_LEVEL_ONE:
		comp->unit->restart_count += 1;
		if (comp->unit->restart_count >= comp->unit->amf_group->unit_restart_max) {
			comp->unit->escalation_level = ESCALATION_LEVEL_TWO;
			escalation_policy_cleanup (comp);
			return;
		}
		printf ("Escalation level 1 - restart unit\n");
		printf ("Cleaning up and restarting unit.\n");
		unit_cleanup (comp->unit);
		break;

	case ESCALATION_LEVEL_TWO:
		printf ("Escalation level TWO\n");
		unit_cleanup (comp->unit);
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
	struct healthcheck_active *healthcheck_active =
		(struct healthcheck_active *)data;

	printf ("timeout occured on healthcheck for component %s.\n",
		getSaNameT (&healthcheck_active->comp->name));
	escalation_policy_cleanup (healthcheck_active->comp);
}

void healthcheck_activate (struct healthcheck_active *healthcheck_active)
{
	struct res_lib_amf_healthcheckcallback res_lib_amf_healthcheckcallback;

	healthcheck_active->active = 1;

// TODO	memset (&res_lib_amf_healthcheckcallback, 0, sizeof(res_lib_amf_healthcheckcallback));
	res_lib_amf_healthcheckcallback.header.id = MESSAGE_RES_AMF_HEALTHCHECKCALLBACK;
	res_lib_amf_healthcheckcallback.header.size = sizeof (struct res_lib_amf_healthcheckcallback);
	res_lib_amf_healthcheckcallback.header.error = SA_AIS_OK;


	log_printf (LOG_LEVEL_NOTICE, "sending healthcheck to component %s\n",
		getSaNameT (&healthcheck_active->comp->name));

	res_lib_amf_healthcheckcallback.invocation =
		invocation_create (
		AMF_RESPONSE_HEALTHCHECKCALLBACK,
		(void *)healthcheck_active);

	memcpy (&res_lib_amf_healthcheckcallback.compName,
		&healthcheck_active->comp->name,
		sizeof (SaNameT));

	memcpy (&res_lib_amf_healthcheckcallback.key,
		&healthcheck_active->key,
		sizeof (SaAmfHealthcheckKeyT));

	openais_conn_send_response (
		openais_conn_partner_get (healthcheck_active->comp->conn),
		&res_lib_amf_healthcheckcallback,
		sizeof (struct res_lib_amf_healthcheckcallback));

	poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_healthcheck_duration);

	poll_timer_add (aisexec_poll_handle,
		healthcheck_active->healthcheck->maximum_duration,
		(void *)healthcheck_active,
		timer_function_healthcheck_timeout,
		&healthcheck_active->timer_healthcheck_duration);
}

void healthcheck_deactivate (struct healthcheck_active *healthcheck_active)
{
	log_printf (LOG_LEVEL_NOTICE, "ZZZ deactivating healthcheck for component %s\n",
		getSaNameT (&healthcheck_active->comp->name));
	poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_healthcheck_period);

	poll_timer_delete (aisexec_poll_handle,
		healthcheck_active->timer_healthcheck_duration);

	invocation_destroy_by_data ((void *)healthcheck_active);

	list_del (&healthcheck_active->list);
	free (healthcheck_active);
}


static void timer_function_healthcheck_next (
	void *data)
{
	healthcheck_activate (data);
}

void healthcheck_unit_deactivate (
	struct amf_unit *unit)
{
	struct list_head *list;
	struct list_head *key_list;
	struct healthcheck_active *healthcheck_active;
	struct amf_comp *comp;

	for (list = unit->comp_head.next;
		list != &unit->comp_head;
		list = list->next) {

		comp = list_entry (list, struct amf_comp, comp_list);

		for (key_list = comp->healthcheck_list.next;
			key_list != &comp->healthcheck_list;
			key_list = key_list->next) {

			healthcheck_active = list_entry (key_list,
				struct healthcheck_active, list);

			healthcheck_deactivate (healthcheck_active);
		}
	}
}


void healthcheck_unit_activate (
	struct amf_unit *unit)
{
	struct list_head *list;
	struct list_head *key_list;
	struct healthcheck_active *healthcheck_active;
	struct amf_comp *comp;

	for (list = unit->comp_head.next;
		list != &unit->comp_head;
		list = list->next) {

		comp = list_entry (list, struct amf_comp, comp_list);

		for (key_list = comp->healthcheck_list.next;
			key_list != &comp->healthcheck_list;
			key_list = key_list->next) {

			healthcheck_active = list_entry (key_list,
				struct healthcheck_active, list);

			healthcheck_activate (healthcheck_active);
		}
	}
}

void operational_state_unit_set (
	struct amf_unit *unit,
	SaAmfOperationalStateT operational_state)
{
	if (operational_state == unit->operational_state) {
		printf ("Not assigning service unit new operational state - same state\n");
		return;
	}
	unit->operational_state = operational_state;
	printf ("Service unit operational state set to %s\n",
		operationalstate_ntoa (operational_state));
	if (operational_state == SA_AMF_OPERATIONAL_ENABLED) {
		readiness_state_unit_set (unit,
			SA_AMF_READINESS_IN_SERVICE);
		/*
		 * Start healthcheck now
		 */
// TODO		healthcheck_unit_activate (unit);
	} else
	if (operational_state == SA_AMF_OPERATIONAL_DISABLED) {
		readiness_state_unit_set (unit,
			SA_AMF_READINESS_OUT_OF_SERVICE);
//		ha_state_unit_set (unit, si, SA_AMF_HA_STANDBY);

//		healthcheck_unit_deactivate (unit);
	}
}


static void message_handler_req_exec_amf_operational_state_comp_set (
	void *message,
	struct totem_ip_address *address)
{
	struct req_exec_amf_operational_state_comp_set *req_exec_amf_operational_state_comp_set =
		(struct req_exec_amf_operational_state_comp_set *)message;
	struct amf_comp *comp;
	struct amf_comp *comp_compare;
	struct list_head *list;
	int all_set = 1;

	comp = find_comp (&req_exec_amf_operational_state_comp_set->name);
	comp->operational_state = req_exec_amf_operational_state_comp_set->operational_state;
	
	printf ("Setting component %s operational state to %s\n",
		getSaNameT (&comp->name),
		operationalstate_ntoa (comp->operational_state));
	/*
	 * If all operational states are ENABLED, then SU should be ENABLED
	 */
	for (list = comp->unit->comp_head.next;
		list != &comp->unit->comp_head;
		list = list->next) {

		comp_compare = list_entry (list,
			struct amf_comp, comp_list);
		if (comp_compare->operational_state != SA_AMF_OPERATIONAL_ENABLED) {
			all_set = 0;
			break;
		}
	}
	if (all_set) {
		operational_state_unit_set (comp->unit, 
			SA_AMF_OPERATIONAL_ENABLED);
	} else {
		operational_state_unit_set (comp->unit, 
			SA_AMF_OPERATIONAL_DISABLED);
	}
	readiness_state_comp_set (comp);
}

static void message_handler_req_exec_amf_presence_state_comp_set (
	void *message,
	struct totem_ip_address *address)
{
	struct req_exec_amf_presence_state_comp_set *req_exec_amf_presence_state_comp_set =
		(struct req_exec_amf_presence_state_comp_set *)message;
	struct amf_comp *comp;
	struct amf_comp *comp_compare;
	struct list_head *list;
	int all_set = 1;

	comp = find_comp (&req_exec_amf_presence_state_comp_set->name);
	if (req_exec_amf_presence_state_comp_set->presence_state == comp->presence_state) {
		printf ("duplicate presence state set, not setting presence state\n");
		return;
	}

	if (req_exec_amf_presence_state_comp_set->presence_state == SA_AMF_PRESENCE_UNINSTANTIATED) {
		comp->conn = 0;
	}

	/*
	 * The restarting state can only be entered from the uninstantiated state
	 */
	if (req_exec_amf_presence_state_comp_set->presence_state == SA_AMF_PRESENCE_RESTARTING &&
		comp->presence_state != SA_AMF_PRESENCE_UNINSTANTIATED) {

printf ("restart presence state set even though not in terminating state\n");
		return;
	}

	comp->presence_state = req_exec_amf_presence_state_comp_set->presence_state;
	if (comp->presence_state == SA_AMF_PRESENCE_RESTARTING) {
		printf ("SET TO RESTARTING instantiating now\n");
		clc_instantiate (comp);
	}

	printf ("Setting component %s presence state %s\n",
		getSaNameT (&comp->name),
		presencestate_ntoa (comp->presence_state));
	
	/*
	 * Restart components that are requested to enter the restarting presence state
	 */

	/*
	 * If all comp presence states are INSTANTIATED, then SU should be instantated
	 */
	for (list = comp->unit->comp_head.next;
		list != &comp->unit->comp_head;
		list = list->next) {

		comp_compare = list_entry (list,
			struct amf_comp, comp_list);
		if (comp_compare->presence_state != SA_AMF_PRESENCE_INSTANTIATED) {
			all_set = 0;
			break;
		}
	}

	if (all_set) {
		presence_state_unit_set (comp->unit, 
			SA_AMF_PRESENCE_INSTANTIATED);
	}
}

static void message_handler_req_exec_amf_administrative_state_csi_set (
	void *message,
	struct totem_ip_address *address)
{
//	struct req_exec_amf_administrative_state_csi_set *req_exec_amf_administrative_state_csi_set =
//		(struct req_exec_amf_administrative_state_csi_set *)message;
// TODO
}
static void message_handler_req_exec_amf_administrative_state_unit_set (
	void *message,
	struct totem_ip_address *address)
{
//	struct req_exec_amf_administrative_state_unit_set *req_exec_amf_administrative_state_unit_set =
//		(struct req_exec_amf_administrative_state_unit_set *)message;
// TODO
}
static void message_handler_req_exec_amf_administrative_state_group_set (
	void *message,
	struct totem_ip_address *source)
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

	comp = find_comp (&req_lib_amf_componentregister->compName);
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

	component = find_comp (&req_lib_amf_componentunregister->compName);
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
	struct healthcheck_active *healthcheck_active;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

printf ("healthcheck start\n");
fflush (stdout);
	healthcheck = find_healthcheck (&req_lib_amf_healthcheckstart->healthcheckKey);
	if (healthcheck == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	comp = find_comp (&req_lib_amf_healthcheckstart->compName);
	if (comp == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 *  Determine if this healthcheck is already active
	 */
	healthcheck_active = find_healthcheck_active (
		comp,
		&req_lib_amf_healthcheckstart->healthcheckKey,
		req_lib_amf_healthcheckstart->invocationType);
	if (healthcheck_active) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	healthcheck_active = malloc (sizeof (struct healthcheck_active));
	if (healthcheck_active == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	/*
	 * Make new instance of healthcheck key
	 */
	list_init (&healthcheck_active->list);
	memcpy (&healthcheck_active->key,
		&req_lib_amf_healthcheckstart->healthcheckKey,
		sizeof (SaAmfHealthcheckKeyT));
	healthcheck_active->comp = comp;
	healthcheck_active->invocationType = req_lib_amf_healthcheckstart->invocationType;
	healthcheck_active->healthcheck = healthcheck;
	healthcheck_active->timer_healthcheck_duration = 0;
	healthcheck_active->timer_healthcheck_period = 0;
	healthcheck_active->active = 0;

	list_add_tail (&healthcheck_active->list, &comp->healthcheck_list);

	if (comp->conn != 0) {
printf ("Activating healthcheck for the first time %p\n", healthcheck_active);
		healthcheck_activate (healthcheck_active);
	}

#ifdef TODO
do we want to do healtchecking only when full su has registered or also of non-fully registered sus
	if (comp->unit->operational_state == SA_AMF_OPERATIONAL_ENABLED) {
		/*
		 * Start healthcheck now
		 */
		healthcheck_unit_activate (comp->unit);
	}
#endif

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
	struct healthcheck_active *healthcheck_active;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	printf ("healthcheck stop\n");
	comp = find_comp (&req_lib_amf_healthcheckstop->compName);
	if (comp == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck_active = find_healthcheck_active (
		comp,
		&req_lib_amf_healthcheckstop->healthcheckKey,
		INVOCATION_DONT_COMPARE);

	printf ("active %p\n", healthcheck_active);
	if (healthcheck_active == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	healthcheck_deactivate (healthcheck_active);

error_exit:
	printf ("healthcheck stop\n");
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

static void message_handler_req_lib_amf_protectiongrouptrackstart (
	void *conn,
	void *msg)
{
#ifdef COMPILE_OUT
	struct req_lib_amf_protectiongrouptrackstart *req_lib_amf_protectiongrouptrackstart = (struct req_lib_amf_protectiongrouptrackstart *)message;
	struct res_lib_amf_protectiongrouptrackstart res_lib_amf_protectiongrouptrackstart;
	struct libamf_ci_trackentry *track = 0;
	int i;
	struct saAmfProtectionGroup *amfProtectionGroup;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_protectiongrouptrackstart()\n");

	amfProtectionGroup = protectiongroup_find (&req_lib_amf_protectiongrouptrackstart->csiName);
	if (amfProtectionGroup) {
		log_printf (LOG_LEVEL_DEBUG, "protectiongrouptrackstart: Got valid track start on CSI: %s.\n", getSaNameT (&req_lib_amf_protectiongrouptrackstart->csiName));
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
		track->trackFlags = req_lib_amf_protectiongrouptrackstart->trackFlags;
		track->notificationBufferAddress = req_lib_amf_protectiongrouptrackstart->notificationBufferAddress;
		memcpy (&track->csiName,
			&req_lib_amf_protectiongrouptrackstart->csiName, sizeof (SaNameT));

		conn_info->ais_ci.u.libamf_ci.trackActive += 1;

		list_add (&conn_info->conn_list, &library_notification_send_listhead);
	
		/*
		 * If SA_TRACK_CURRENT is specified, write out all current connections
		 */
	} else {
		log_printf (LOG_LEVEL_DEBUG, "invalid track start, csi not registered with system.\n");
	}

	res_lib_amf_protectiongrouptrackstart.header.id = MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTART;
	res_lib_amf_protectiongrouptrackstart.header.size = sizeof (struct res_lib_amf_protectiongrouptrackstart);
	res_lib_amf_protectiongrouptrackstart.header.error = SA_ERR_NOT_EXIST;

	if (amfProtectionGroup) {
		res_lib_amf_protectiongrouptrackstart.header.error = SA_AIS_OK;
	}
	openais_conn_send_response (conn, &res_lib_amf_protectiongrouptrackstart,
		sizeof (struct res_lib_amf_protectiongrouptrackstart));

	if (amfProtectionGroup &&
		req_lib_amf_protectiongrouptrackstart->trackFlags & SA_TRACK_CURRENT) {

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

	log_printf (LOG_LEVEL_NOTICE, "Handle : message_handler_req_lib_amf_componenterrorreport()\n");

printf ("ERROR REPORT\n");
	comp = find_comp (&req_lib_amf_componenterrorreport->erroneousComponent);
	if (comp) {
printf ("escalation policy terminate\n");
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

void pg_comp_create (
	struct amf_pg *pg,
	struct amf_csi *csi,
	struct amf_comp *comp)
{
	struct amf_pg_comp *pg_comp;

	printf ("creating component for pg\n");
	pg_comp = malloc (sizeof (struct amf_pg_comp));
	assert (pg_comp);
	pg_comp->comp = comp;
	pg_comp->csi = csi;
	list_init (&pg_comp->list);
	list_add_tail (&pg_comp->list, &pg->pg_comp_head);
}

static void message_handler_req_lib_amf_response (void *conn, void *msg)
{
	struct req_lib_amf_response *req_lib_amf_response = (struct req_lib_amf_response *)msg;
	struct res_lib_amf_response res_lib_amf_response;
	struct csi_set_callback_data *csi_set_callback_data;
	struct csi_remove_callback_data *csi_remove_callback_data;
	struct component_terminate_callback_data *component_terminate_callback_data;
	struct healthcheck_active *healthcheck_active;
	int interface;
	int res;
	void *data;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "message_handler_req_lib_amf_response()\n");

	res = invocation_get_and_destroy (req_lib_amf_response->invocation,
		&interface, &data);

	if (res == -1) {
		printf ("invocation not found\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	log_printf (LOG_LEVEL_DEBUG, "handling response connection interface %x\n", interface);
	switch (interface) {
	case AMF_RESPONSE_HEALTHCHECKCALLBACK:
		healthcheck_active = (struct healthcheck_active *)data;

		poll_timer_delete (aisexec_poll_handle,
			healthcheck_active->timer_healthcheck_duration);

		healthcheck_active->timer_healthcheck_duration = 0;

		poll_timer_add (aisexec_poll_handle,
			healthcheck_active->healthcheck->period,
			(void *)healthcheck_active,
			timer_function_healthcheck_next,
			&healthcheck_active->timer_healthcheck_period);
		break;

	case AMF_RESPONSE_CSISETCALLBACK:
		csi_set_callback_data = (struct csi_set_callback_data *)data;

		printf ("csi callback executed from library.\n");
		csi_set_callback_data->comp->ha_state =
			csi_set_callback_data->comp->unit->requested_ha_state;
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
		printf ("response from removing the CSI\n");
// AAAA
		list_del (&csi_remove_callback_data->csi->si->unit_list);
		list_del (&csi_remove_callback_data->csi->csi_list);
		free (csi_remove_callback_data);
		break;


	case AMF_RESPONSE_COMPONENTTERMINATECALLBACK:
		component_terminate_callback_data = (struct component_terminate_callback_data *)data;

		printf ("response from terminating component\n");
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

}


#ifdef COMPILE_OUT
/*
 * Executive Message Implementation 
 */
static void message_handler_req_exec_amf_componentregister (void *message, struct in_addr source_addr, int endian_conversion_required)
{
#ifdef COMPILE_OUT
	struct req_exec_amf_componentregister *req_exec_amf_componentregister = (struct req_exec_amf_componentregister *)message;
	struct res_lib_amf_componentregister res_lib_amf_componentregister;
	struct amf_comp *component;
	struct amf_comp *amfProxyComponent;
	SaAisErrorT error;

	log_printf (LOG_LEVEL_FROM_GMI, "Executive: ComponentRegister for component %s\n",
		getSaNameT (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName));

	/*
	 * Determine if proxy isn't registered
	 */
	error = SA_AIS_OK;
	component = find_comp (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName);
	amfProxyComponent = find_comp (&req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName);

	/*
	 * If a node is joining menber ship ,Component States Synchronize
	 */
	if (req_exec_amf_componentregister->source.in_addr.s_addr == 0) {
		amf_synchronize (message, source_addr);
		return;
	}

	/*
	 * If component not in configuration files, return error
	 */
	if (component == 0) {
		error = SA_ERR_NOT_EXIST;
	}

	/*
	 * If proxy doesn't exist and isn't registered, return error
	 */
	if ((amfProxyComponent == 0 &&
		req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName.length > 0) || 
		(amfProxyComponent && amfProxyComponent->registered == 0)) {

		error = SA_ERR_NOT_EXIST;
	}

	/*
	 * If component already registered, return error
	 */
	if (error == SA_AIS_OK) {
		if (component->registered) {
			error = SA_ERR_EXIST;
		}
	}

	/*
	 * Finally register component and setup links for proxy if
	 * proxy present
	 */
	if (error == SA_AIS_OK) {
		component->local = 0;
		component->registered = 1;
		component->conn_info = req_exec_amf_componentregister->source.conn_info;
		component->source_addr = source_addr;
//		component->currentReadinessState = SA_AMF_OUT_OF_SERVICE;
//		component->newReadinessState = SA_AMF_OUT_OF_SERVICE;
		component->currentHAState = 0;
		component->newHAState = 0;
		component->probableCause = 0;
		component->enabledUnlockedState = 0;
		component->disabledUnlockedState = 0;
		component->healthcheck_outstanding = 0;

		if (req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName.length > 0) {
			component->saAmfProxyComponent = amfProxyComponent;
		}
	}

	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (message_source_is_local(&req_exec_amf_componentregister->source)) {
		if (error == SA_AIS_OK) {
			component->local = 1;
			req_exec_amf_componentregister->source.conn_info->component = component;
		}

		log_printf (LOG_LEVEL_DEBUG, "sending component register response to fd %d\n",
			    req_exec_amf_componentregister->source.conn_info->fd);

		res_lib_amf_componentregister.header.size = sizeof (struct res_lib_amf_componentregister);
		res_lib_amf_componentregister.header.id = MESSAGE_RES_AMF_COMPONENTREGISTER;
		res_lib_amf_componentregister.header.error = error;

		openais_conn_send_response (req_exec_amf_componentregister->source.conn_info,
			&res_lib_amf_componentregister,
			sizeof (struct res_lib_amf_componentregister));
	}
	
	/*
	 * If no error on registration, determine if we should enter new state
	 */
	if (error == SA_AIS_OK) {
		dsm (component);
	}

#endif
}

static void message_handler_req_exec_amf_componentunregister (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_amf_componentunregister *req_exec_amf_componentunregister = (struct req_exec_amf_componentunregister *)message;
	struct res_lib_amf_componentunregister res_lib_amf_componentunregister;
	struct amf_comp *component;
	struct amf_comp *amfProxyComponent;
	SaAisErrorT error;

	log_printf (LOG_LEVEL_FROM_GMI, "Executive: Component_unregister for %s\n",
		getSaNameT (&req_exec_amf_componentunregister->req_lib_amf_componentunregister.compName));

	component = find_comp (&req_exec_amf_componentunregister->req_lib_amf_componentunregister.compName);
	amfProxyComponent = find_comp (&req_exec_amf_componentunregister->req_lib_amf_componentunregister.proxyCompName);

	/*
	 * Check for proxy and component not existing in system
	 */
	error = SA_AIS_OK;
	if (component == 0) {
		error = SA_ERR_NOT_EXIST;
	}
	if (req_exec_amf_componentunregister->req_lib_amf_componentunregister.proxyCompName.length > 0) {
		if (amfProxyComponent) {
			if (amfProxyComponent->registered == 0) {
				error = SA_ERR_NOT_EXIST;
			}
		} else {
			error = SA_ERR_NOT_EXIST;
		}
	}

	/*
	 * If there is a proxycompname, make sure it is the proxy
	 * of compName
	 */
	if (error == SA_AIS_OK && amfProxyComponent) {
		if (component->saAmfProxyComponent != amfProxyComponent) {
			error = SA_ERR_BAD_OPERATION;
		}
	}

	/*
	 * Finally unregister the component
	 */
	if (error == SA_AIS_OK) {
		component->registered = 0;
//		dsmEnabledUnlockedTransitionDisabledUnlocked (component);
	}
	
	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (message_source_is_local (&req_exec_amf_componentunregister->source)) {
		log_printf (LOG_LEVEL_DEBUG, "sending component unregister response to fd %d\n",
			req_exec_amf_componentunregister->source.conn_info->fd);

		res_lib_amf_componentunregister.header.size = sizeof (struct res_lib_amf_componentunregister);
		res_lib_amf_componentunregister.header.id = MESSAGE_RES_AMF_COMPONENTUNREGISTER;
		res_lib_amf_componentunregister.header.error = error;

		openais_conn_send_response (req_exec_amf_componentunregister->source.conn_info,
			&res_lib_amf_componentunregister, sizeof (struct res_lib_amf_componentunregister));
	}

	return;
}

static void message_handler_req_exec_amf_componenterrorreport (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_amf_componenterrorreport *req_exec_amf_componenterrorreport = (struct req_exec_amf_componenterrorreport *)message;
	struct res_lib_amf_componenterrorreport res_lib_amf_componenterrorreport;
	struct amf_comp *comp;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "Executive: ErrorReport for %s\n", 
		getSaNameT (&req_exec_amf_componenterrorreport->req_lib_amf_componenterrorreport.erroneousComponent));

	comp = find_comp (&req_exec_amf_componenterrorreport->req_lib_amf_componenterrorreport.erroneousComponent);
	if (comp == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
	}

	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (message_source_is_local (&req_exec_amf_componenterrorreport->source)) {
		log_printf (LOG_LEVEL_DEBUG, "sending error report response to fd %d\n",
			req_exec_amf_componenterrorreport->source.conn_info->fd);
		if (comp) {
		}

		res_lib_amf_componenterrorreport.header.size = sizeof (struct res_lib_amf_componenterrorreport);
		res_lib_amf_componenterrorreport.header.id = MESSAGE_RES_AMF_COMPONENTERRORREPORT;
		res_lib_amf_componenterrorreport.header.error = error;

		openais_conn_send_response (req_exec_amf_componenterrorreport->source.conn_info,
			&res_lib_amf_componenterrorreport, sizeof (struct res_lib_amf_componenterrorreport));
	}

	return (0);
}

static void message_handler_req_exec_amf_componenterrorclear (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_amf_componenterrorclear *req_exec_amf_componenterrorclear = (struct req_exec_amf_componenterrorclear *)message;
	struct res_lib_amf_componenterrorclear res_lib_amf_componenterrorclear;
	struct amf_comp *component;
	SaAisErrorT error = SA_ERR_BAD_OPERATION;

#ifdef COMPILE_OUT
	log_printf (LOG_LEVEL_FROM_GMI, "Executive: ErrorCancelAll for %s\n",
		getSaNameT (&req_exec_amf_componenterrorclear->req_lib_amf_componenterrorclear.compName));

	component = find_comp (&req_exec_amf_componenterrorclear->req_lib_amf_componenterrorclear.compName);
	if (component && component->registered) {
		/*
		 * Mark component in service if its a AMF service
		 * connected to this aisexec
		 */
		if (component->probableCause) {
			component->probableCause = 0;
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			dsm (component);
		}
		error = SA_AIS_OK;
	}
	
	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (message_source_is_local (&req_exec_amf_componenterrorclear->source)) {
		log_printf (LOG_LEVEL_DEBUG, "sending error report response to fd %d\n",
			req_exec_amf_componenterrorclear->source.conn_info->fd);

		res_lib_amf_componenterrorclear.header.size = sizeof (struct res_lib_amf_componenterrorclear);
		res_lib_amf_componenterrorclear.header.id = MESSAGE_RES_AMF_COMPONENTERRORCLEAR;
		res_lib_amf_componenterrorclear.header.error = error;

		openais_conn_send_response (req_exec_amf_componenterrorclear->source.conn_info,
			&res_lib_amf_componenterrorclear, sizeof (struct res_lib_amf_componenterrorclear));
	}

#endif
	return (0);
}
#endif
#ifdef COMPILE_OUT
static void grow_amf_track_table (struct conn_info *conn_info, int growby)
{
	struct libamf_ci_trackentry *tracks;
	int newsize;
	int currsize = conn_info->ais_ci.u.libamf_ci.trackEntries;

	
	newsize = growby + currsize;

	if (newsize > currsize) {
		tracks = (struct libamf_ci_trackentry *)mempool_realloc (conn_info->ais_ci.u.libamf_ci.tracks,
			(newsize) * sizeof (struct libamf_ci_trackentry));
		if (tracks == 0) {
#ifdef DEBUG
			printf ("grow_amf_track_table: out of memory, woops\n");
#endif
// TODO
			exit (1);
		}
		memset (&tracks[currsize], 0, growby * sizeof (struct libamf_ci_trackentry));
		conn_info->ais_ci.u.libamf_ci.trackEntries = newsize;
		conn_info->ais_ci.u.libamf_ci.tracks = tracks;
	}
}


static void component_unregister (
	struct amf_comp *component)
{
	struct req_exec_amf_componentunregister req_exec_amf_componentunregister;
	struct iovec iovec;

	/*
	 * This only works on local components
	 */
	if (component == 0 || component->local != 1) {
		return;
	}
	log_printf (LOG_LEVEL_ENTER_FUNC, "component_unregister: unregistering component %s\n",
		getSaNameT (&component->name));

	component->probableCause = SA_AMF_NOT_RESPONDING;

	req_exec_amf_componentunregister.header.size = sizeof (struct req_exec_amf_componentunregister);
	req_exec_amf_componentunregister.header.id = 
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_COMPONENTUNREGISTER);

	req_exec_amf_componentunregister.source.conn_info = 0;
	req_exec_amf_componentunregister.source.in_addr.s_addr = 0;

	memset (&req_exec_amf_componentunregister.req_lib_amf_componentunregister,
		0, sizeof (struct req_lib_amf_componentunregister));
	memcpy (&req_exec_amf_componentunregister.req_lib_amf_componentunregister.compName,
		&component->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_amf_componentunregister;
	iovec.iov_len = sizeof (req_exec_amf_componentunregister);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

static void component_register (
	struct amf_comp *component)
{
	struct req_exec_amf_componentregister req_exec_amf_componentregister;
	struct iovec iovec;

	/*
	 * This only works on local components
	 */
	if (component == 0 || component->local != 1) {
		return;
	}
	log_printf (LOG_LEVEL_ENTER_FUNC, "component_register: registering component %s\n",
		getSaNameT (&component->name));

	req_exec_amf_componentregister.header.size = sizeof (struct req_exec_amf_componentregister);
	req_exec_amf_componentregister.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_COMPONENTREGISTER);

	req_exec_amf_componentregister.source.conn_info = 0;
	req_exec_amf_componentregister.source.in_addr.s_addr = 0;
	req_exec_amf_componentregister.currentReadinessState = component->currentReadinessState;
	req_exec_amf_componentregister.newReadinessState = component->newReadinessState;
	req_exec_amf_componentregister.currentHAState = component->currentHAState;
	req_exec_amf_componentregister.newHAState = component->newHAState;

	memset (&req_exec_amf_componentregister.req_lib_amf_componentregister,
		0, sizeof (struct req_lib_amf_componentregister));
	memcpy (&req_exec_amf_componentregister.req_lib_amf_componentregister.compName,
		&component->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_amf_componentregister;
	iovec.iov_len = sizeof (req_exec_amf_componentregister);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

/***
This should be used for a partition I think
**/
void enumerate_components (
	void (*function)(struct amf_comp *, void *data),
	void *data)
{
	struct list_head *AmfGroupList;
	struct list_head *AmfUnitList;
	struct list_head *AmfComponentList;

	struct saAmfGroup *saAmfGroup;
	struct saAmfUnit *AmfUnit;
	struct amf_comp *AmfComponent;


	/*
	 * Search all groups
	 */
	for (AmfGroupList = saAmfGroupHead.next;
		AmfGroupList != &saAmfGroupHead;
		AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList,
			struct saAmfGroup, saAmfGroupList);

		/*
		 * Search all units
		 */
		for (AmfUnitList = saAmfGroup->saAmfUnitHead.next;
			AmfUnitList != &saAmfGroup->saAmfUnitHead;
			AmfUnitList = AmfUnitList->next) {

			AmfUnit = list_entry (AmfUnitList,
				struct saAmfUnit, saAmfUnitList);

			/*
			 * Search all components
			 */
			for (AmfComponentList = AmfUnit->amf_compHead.next;
				AmfComponentList != &AmfUnit->amf_compHead;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList,
					struct amf_comp, amf_compList);

				function (AmfComponent, data);
			}
		}
	}
}

void ha_state_api_set (struct amf_comp *component, SaAmfHAStateT haState)
{
	struct res_lib_amf_csisetcallback res_lib_amf_csisetcallback;
	memset (&res_lib_amf_csisetcallback,0,sizeof(res_lib_amf_csisetcallback));

	log_printf (LOG_LEVEL_ENTER_FUNC, "sending ha state to API\n");

	if (component->local != 1) {
		return;
	}
	if (component->probableCause == SA_AMF_NOT_RESPONDING) {
		return;
	}
	/*
	 * this should be an assertion
	 */
	if (component->conn_info->state != CONN_STATE_ACTIVE ||
		component->conn_info->service != AMF_SERVICE) {
		return;
	}

	res_lib_amf_csisetcallback.header.id = MESSAGE_RES_AMF_CSISETCALLBACK;
	res_lib_amf_csisetcallback.header.size = sizeof (struct res_lib_amf_csisetcallback);
	res_lib_amf_csisetcallback.header.error = SA_AIS_OK;

	if (res_lib_amf_csisetcallback.invocation == -1) {
		printf ("TODO set callback\n");
	}
	memcpy (&res_lib_amf_csisetcallback.compName,
		&component->name, sizeof (SaNameT));
	memcpy (&res_lib_amf_csisetcallback.csiName,
		&component->saAmfProtectionGroup->name, sizeof (SaNameT));
	res_lib_amf_csisetcallback.csiFlags = SA_AMF_CSI_ALL_INSTANCES;
	res_lib_amf_csisetcallback.haState = haState;
	// TODO set activeCompName to correct component name
	memcpy (&res_lib_amf_csisetcallback.activeCompName,
		&component->name, sizeof (SaNameT));
	res_lib_amf_csisetcallback.transitionDescriptor = SA_AMF_CSI_NEW_ASSIGN;

	component->newHAState = haState;

	openais_conn_send_response (component->conn_info->conn_info_partner,
		&res_lib_amf_csisetcallback,
		sizeof (struct res_lib_amf_csisetcallback));
}

static void ha_state_group_set (
	struct amf_comp *component,
	SaAmfHAStateT haState)
{
	struct req_exec_amf_hastateset req_exec_amf_hastateset;
	struct iovec iovec;

	req_exec_amf_hastateset.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_HASTATESET);
	req_exec_amf_hastateset.header.size = sizeof (struct req_exec_amf_hastateset);
	memcpy (&req_exec_amf_hastateset.compName, &component->name, sizeof (SaNameT));
	req_exec_amf_hastateset.haState = haState;

	log_printf (LOG_LEVEL_ENTER_FUNC, "Sending ha state to cluster for component %s\n", getSaNameT (&component->name));
	log_printf (LOG_LEVEL_DEBUG, "ha state is %d\n", haState);

	iovec.iov_base = (char *)&req_exec_amf_hastateset;
	iovec.iov_len = sizeof (req_exec_amf_hastateset);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovec, 1, TOTEMPG_AGREED) == 0);
}

void readiness_state_api_set (struct amf_comp *component,
	SaAmfReadinessStateT readinessState)
{
	struct res_lib_amf_readinessstatesetcallback res_lib_amf_readinessstatesetcallback;
	memset (&res_lib_amf_readinessstatesetcallback,0,sizeof(res_lib_amf_readinessstatesetcallback));

	/*
	 * If component is local, don't request service from API
	 */
	if (component->local != 1) {
		return;
	}
	if (component->probableCause == SA_AMF_NOT_RESPONDING) {
		return;
	}
		
	/*
	 * this should be an assertion
	 */
	if (component->conn_info->state != CONN_STATE_ACTIVE ||
		component->conn_info->service != AMF_SERVICE) {

		return;
	}

	res_lib_amf_readinessstatesetcallback.header.id = MESSAGE_RES_AMF_READINESSSTATESETCALLBACK;
	res_lib_amf_readinessstatesetcallback.header.size = sizeof (struct res_lib_amf_readinessstatesetcallback);
	res_lib_amf_readinessstatesetcallback.header.error = SA_AIS_OK;
	res_lib_amf_readinessstatesetcallback.invocation =
		req_lib_amf_invocation_create (
		MESSAGE_REQ_AMF_RESPONSE_SAAMFREADINESSSTATESETCALLBACK,
		comp);
	if (res_lib_amf_readinessstatesetcallback.invocation == -1) {
		printf ("TODO readiness set callback\n");
	}
	memcpy (&res_lib_amf_readinessstatesetcallback.compName,
		&component->name, sizeof (SaNameT));
	res_lib_amf_readinessstatesetcallback.readinessState = readinessState;
	component->newReadinessState = readinessState;

	log_printf (LOG_LEVEL_DEBUG, "Setting conn_info %p to readiness state %d\n", component->conn_info, readinessState);

	openais_conn_send_response (component->conn_info->conn_info_partner,
		&res_lib_amf_readinessstatesetcallback,
		sizeof (struct res_lib_amf_readinessstatesetcallback));
}

static void readiness_state_group_set (
	struct amf_comp *component,
	SaAmfReadinessStateT readinessState)
{
	struct req_exec_amf_readinessstateset req_exec_amf_readinessstateset;
	struct iovec iovec;

	req_exec_amf_readinessstateset.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_READINESSSTATESET);
	req_exec_amf_readinessstateset.header.size = sizeof (struct req_exec_amf_readinessstateset);
	memcpy (&req_exec_amf_readinessstateset.compName, &component->name, sizeof (SaNameT));
	req_exec_amf_readinessstateset.readinessState = readinessState;

	log_printf (LOG_LEVEL_ENTER_FUNC, "Sending message to all cluster nodes to set readiness state of component %s\n",
		getSaNameT (&component->name));
	log_printf (LOG_LEVEL_DEBUG, "readiness state is %d\n", readinessState);

	iovec.iov_base = (char *)&req_exec_amf_readinessstateset;
	iovec.iov_len = sizeof (req_exec_amf_readinessstateset);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

static void dsmDisabledUnlockedRegisteredOrErrorCancel (
	struct amf_comp *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	int serviceUnitEnabled;
	
	log_printf (LOG_LEVEL_DEBUG, "dsmDisabledUnlockedRegisteredOrErrorCancel for %s\n",
		getSaNameT (&component->name));

	unit = component->saAmfUnit;
	for (serviceUnitEnabled = 1, list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list,
			struct amf_comp, amf_compList);

		if (component->registered == 0 ||
			component->probableCause) {
			log_printf (LOG_LEVEL_DEBUG, "dsm: Can't transition states, found component not registered or failed.\n");
			serviceUnitEnabled = 0;
			break;
		}
	}
	if (serviceUnitEnabled == 1) {
		log_printf (LOG_LEVEL_DEBUG, "dsm entering AMF_ENABLED_UNLOCKED state.\n");
		component->saAmfUnit->operationalAdministrativeState = AMF_ENABLED_UNLOCKED;
		component->disabledUnlockedState = -1; // SHOULD BE INVALID
		component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_INITIAL;
		dsm (component);
	}
}

static void dsmDisabledUnlockedFailedComponent (
	struct amf_comp *component)
{
	log_printf (LOG_LEVEL_DEBUG, "dsmDisabledUnlockedFailedComponent: for %s.\n",
			getSaNameT (&component->name));
	switch (component->enabledUnlockedState) {
    	case AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED:
    	case AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED:
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED;
			if (component->probableCause == SA_AMF_NOT_RESPONDING) {
				readiness_state_group_set (component, SA_AMF_OUT_OF_SERVICE);
			} else {
				readiness_state_api_set (component, SA_AMF_OUT_OF_SERVICE);
			}
			break;

    	case AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED:
    	case AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED:
    	case AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED:
    	case AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED:
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED;
			if (component->probableCause == SA_AMF_NOT_RESPONDING) {
				ha_state_group_set (component, SA_AMF_QUIESCED);
			} else {
				ha_state_api_set (component, SA_AMF_QUIESCED);
			}
			poll_timer_delete (aisexec_poll_handle,
				component->timer_healthcheck);
			component->timer_healthcheck = 0;
			break;

	default:
		log_printf (LOG_LEVEL_DEBUG, "invalid case 5 %d\n", component->enabledUnlockedState);
		break;
	}
}

static void dsmDisabledUnlockedFailed (
	struct amf_comp *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;

	unit = component->saAmfUnit;

	for (list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list, struct amf_comp, amf_compList);
		dsmDisabledUnlockedFailedComponent (component);
	}
	return;
}

static void dsmDisabledUnlockedQuiescedRequested (
	struct amf_comp *component)
{
	component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_QUIESCED_COMPLETED;
	dsm (component);
}

static void dsmDisabledUnlockedQuiescedCompleted (
	struct amf_comp *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	int serviceUnitQuiesced;
	
	unit = component->saAmfUnit;
	for (serviceUnitQuiesced = 1, list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list, struct amf_comp, amf_compList);

		if (component->probableCause != SA_AMF_NOT_RESPONDING && component->registered) {
			if (component->currentHAState != SA_AMF_QUIESCED) {
				log_printf (LOG_LEVEL_DEBUG, "dsm: Can't transition states, found component not quiesced.\n");
				serviceUnitQuiesced = 0;
				break;
			}
		}
	}
	if (serviceUnitQuiesced == 1) {
		log_printf (LOG_LEVEL_DEBUG, "All components have quiesced, Quiescing completed\n");
		for (list = unit->amf_compHead.next;
			list != &unit->amf_compHead;
			list = list->next) {

			component = list_entry (list, struct amf_comp, amf_compList);

			log_printf (LOG_LEVEL_DEBUG, "dsm: Sending readiness state set to OUTOFSERVICE for comp %s.\n",
				getSaNameT (&component->name));

			if ( component->probableCause == SA_AMF_NOT_RESPONDING ) {
				readiness_state_group_set (component, SA_AMF_OUT_OF_SERVICE);
			} else {
				readiness_state_api_set (component, SA_AMF_OUT_OF_SERVICE);
			}
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED;
		}
	}
}

static void dsmDisabledUnlockedOutOfServiceRequested (
	struct amf_comp *component)
{
	component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED;
	dsm (component);
}

static void dsmDisabledUnlockedOutOfServiceCompleted (
	struct amf_comp *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	int serviceUnitOutOfService;
	struct saAmfGroup *group = 0;
	struct list_head *comp_list = 0;
	struct list_head *unit_list = 0;
	int serviceUnitInStandby = 0;
	int activeServiceUnits = 0;

	/*
	 * Once all components of a service unit are out of service,
	 * activate another service unit in standby
	 */
	log_printf (LOG_LEVEL_DEBUG, "dsmDisabledUnlockedOutOfServiceCompleted: component out of service %s\n", getSaNameT (&component->name));

	/*
	 * Determine if all components have responded to going out of service
	 */
	
	unit = component->saAmfUnit;
	for (serviceUnitOutOfService = 1, list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list, struct amf_comp, amf_compList);

		if (component->probableCause != SA_AMF_NOT_RESPONDING && component->registered) {
			if (component->currentReadinessState != SA_AMF_OUT_OF_SERVICE) {
				log_printf (LOG_LEVEL_DEBUG, "dsm: Can't transition states, found component not quiesced.\n");
				serviceUnitOutOfService = 0;
				break;
			}
		}

		if ( component->registered == 0 ) {
			protectiongroup_notifications_send (component, SA_AMF_PROTECTION_GROUP_REMOVED);
		}

	}

	group = unit->saAmfGroup;
	activeServiceUnits = activeServiceUnitsCount(group);
	if (activeServiceUnits>=group->saAmfActiveUnitsDesired) {
		return;
	}

	if (serviceUnitOutOfService == 1) {
		log_printf (LOG_LEVEL_DEBUG, "SU has gone out of service.\n");
		/*
		 * Search all units
		 */
		for (unit_list = group->saAmfUnitHead.next;
			unit_list != &group->saAmfUnitHead;
			unit_list = unit_list->next) {

			unit = list_entry (unit_list,
				struct saAmfUnit, saAmfUnitList);

			log_printf (LOG_LEVEL_DEBUG, "Checking if service unit is in standby %s\n", getSaNameT (&unit->name));
			/*
			 * Search all components
			 */
			for (serviceUnitInStandby = 1,
				comp_list = unit->amf_compHead.next;
				comp_list != &unit->amf_compHead;
				comp_list = comp_list->next) {
	
				component = list_entry (comp_list,
					struct amf_comp, amf_compList);
	
				if (component->currentHAState != SA_AMF_STANDBY) {
					serviceUnitInStandby = 0;
					break; /* for iteration of service unit components */
				}
			}
			if (serviceUnitInStandby) {
				break; /* for iteration of service group's service units */
			}
		}

		/*
		 * All components in service unit are standby, activate standby service unit
		 */
		if (serviceUnitInStandby) {
			log_printf (LOG_LEVEL_DEBUG, "unit in standby\n");
			for (list = unit->amf_compHead.next;
				list != &unit->amf_compHead;
				list = list->next) {
	
				component = list_entry (list,
				struct amf_comp, amf_compList);
	
				ha_state_api_set (component, SA_AMF_ACTIVE);
			}
		} else {
			log_printf (LOG_LEVEL_DEBUG, "Can't activate standby service unit because no standby is available.\n");
		}
	}
}

static void dsmEnabledUnlockedInitial (
	struct amf_comp *component)
{ 
	struct saAmfUnit *unit;
	struct list_head *list;

	unit = component->saAmfUnit;
	for (list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list, struct amf_comp, amf_compList);

		readiness_state_api_set (component, SA_AMF_IN_SERVICE);
		log_printf (LOG_LEVEL_DEBUG, "dsm: telling component %s to enter SA_AMF_IN_SERVICE.\n",
			getSaNameT (&component->name));
		component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED;
	}
}

static void dsmEnabledUnlockedInServiceRequested (
	struct amf_comp *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	int in_service;

	log_printf (LOG_LEVEL_DEBUG, "dsmEnabledUnlockedInServiceRequested %s.\n", getSaNameT (&component->name));
	
	unit = component->saAmfUnit;
	for (in_service = 1, list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list, struct amf_comp, amf_compList);

		if (component->currentReadinessState != SA_AMF_IN_SERVICE) {
			log_printf (LOG_LEVEL_DEBUG, "dsm: Found atleast one component not in service\n");
			in_service = 0;
			break;
		}
	}
	if (in_service) {
		log_printf (LOG_LEVEL_DEBUG, "DSM determined component is in service\n");
		
		component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED;
		dsm (component);
	}
}

static void dsmEnabledUnlockedInServiceCompleted (
	struct amf_comp *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	SaAmfHAStateT newHaState;
	int activeServiceUnits;

	log_printf (LOG_LEVEL_DEBUG, "dsmEnabledUnlockedInServiceCompleted %s.\n", getSaNameT (&component->name));

	unit = component->saAmfUnit;
	for (list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list,
			struct amf_comp, amf_compList);

		log_printf (LOG_LEVEL_DEBUG, "Requesting component go active.\n");

		/*
		 * Count number of active service units
		 */
		activeServiceUnits = activeServiceUnitsCount (component->saAmfUnit->saAmfGroup);
		if (activeServiceUnits < component->saAmfUnit->saAmfGroup->saAmfActiveUnitsDesired) {

			newHaState = SA_AMF_ACTIVE;
			log_printf (LOG_LEVEL_DEBUG, "Setting ha state of component %s to SA_AMF_ACTIVE\n", getSaNameT (&component->name));
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED;
		} else {
			newHaState = SA_AMF_STANDBY;
			log_printf (LOG_LEVEL_DEBUG, "Setting ha state of component %s to SA_AMF_STANDBY\n", getSaNameT (&component->name));
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED;
		}
		ha_state_api_set (component, newHaState);
	}
}
	
static void dsmEnabledUnlockedActiveRequested (
	struct amf_comp *component)
{
	if (component->local == 1) {
		log_printf (LOG_LEVEL_DEBUG, "Adding healthcheck timer1\n");
		poll_timer_add (aisexec_poll_handle,
			component->healthcheckInterval,
			(void *)component->conn_info,
			timer_function_libamf_healthcheck,
			&component->timer_healthcheck);
	}

	component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED;
}

static void dsmEnabledUnlockedStandbyRequested (
	struct amf_comp *component)
{
	if (component->local == 1) {

		log_printf (LOG_LEVEL_DEBUG, "Adding healthcheck timer2\n");

		poll_timer_add (aisexec_poll_handle,
			component->healthcheckInterval,
			(void *)component->conn_info,
			timer_function_libamf_healthcheck,
			&component->timer_healthcheck);
	}

	component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED;
}

static void dsmEnabledUnlockedTransitionDisabledUnlocked (
	struct amf_comp *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;

	unit = component->saAmfUnit;
	for (list = unit->amf_compHead.next;
		list != &unit->amf_compHead;
		list = list->next) {

		component = list_entry (list, struct amf_comp, amf_compList);

		log_printf (LOG_LEVEL_DEBUG,  "Requesting component %s transition to disabled.\n",
			getSaNameT (&component->name));

		component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_FAILED;
	}

	component->saAmfUnit->operationalAdministrativeState = AMF_DISABLED_UNLOCKED;
	dsm (component);
}

static void dsmSynchronizeStaus (
	struct amf_comp *component)
{
	enum amfOperationalAdministrativeState unit_status = AMF_DISABLED_UNLOCKED;
	struct saAmfUnit *unit;
	struct saAmfGroup *group;
	struct list_head *list;
	int activeServiceUnits;

	if (component->currentReadinessState == component->newReadinessState) {

		if (component->currentReadinessState == SA_AMF_OUT_OF_SERVICE) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_INITIAL;

		} else if (component->currentReadinessState == SA_AMF_IN_SERVICE) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED;
			unit_status = AMF_ENABLED_UNLOCKED;

		} else if  (component->currentReadinessState == SA_AMF_QUIESCED) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_QUIESCED_COMPLETED;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_INITIAL;
		}

	} else {
		if (component->newReadinessState == SA_AMF_OUT_OF_SERVICE) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_INITIAL;

		} else if (component->newReadinessState == SA_AMF_IN_SERVICE) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED;
			unit_status = AMF_ENABLED_UNLOCKED;
		} else {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_INITIAL;
		}
	}

	if (component->currentHAState == component->newHAState) {

		if (component->currentHAState == SA_AMF_ACTIVE) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED;
			unit_status = AMF_ENABLED_UNLOCKED;

		} else if (component->currentHAState == SA_AMF_STANDBY) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED;
			unit_status = AMF_ENABLED_UNLOCKED;

		} else {
			/* depend on readiness status */
		}

	} else {
		if (component->newHAState == SA_AMF_ACTIVE) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED;
			unit_status = AMF_ENABLED_UNLOCKED;

		} else if (component->newHAState == SA_AMF_STANDBY) {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED;
			unit_status = AMF_ENABLED_UNLOCKED;

		} else {
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED;
			component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_INITIAL;
		}
	}

	/* Syncronize Operational AdministrativeState */
	component->saAmfUnit->operationalAdministrativeState = unit_status;

	unit = component->saAmfUnit;
	group = unit->saAmfGroup;

	for (list = unit->amf_compHead.next; list != &unit->amf_compHead; list = list->next) {
		activeServiceUnits = activeServiceUnitsCount(group);
		if (activeServiceUnits <= group->saAmfActiveUnitsDesired) {
			break;
		}
		if (component->currentHAState != SA_AMF_ACTIVE) {
			continue;
		}
		ha_state_api_set (component, SA_AMF_STANDBY);
	}

	return;
}

	
static void dsmEnabledUnlocked (
	struct amf_comp *component)
{
	switch (component->enabledUnlockedState) {
		case AMF_ENABLED_UNLOCKED_INITIAL:
			dsmEnabledUnlockedInitial (component);
			break;
		case AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED:
			dsmEnabledUnlockedInServiceRequested (component);
			break;
		case AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED:
			dsmEnabledUnlockedInServiceCompleted (component);
			break;
		case AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED:
			dsmEnabledUnlockedActiveRequested (component);
			break;
		case AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED:
			/* noop - operational state */
			break;
		case AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED:
			dsmEnabledUnlockedStandbyRequested (component);
			break;
		case AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED:
			/* noop - operational state */
			break;
			
		default:
			log_printf (LOG_LEVEL_DEBUG, "dsmEnabledUnlocked: unkown state machine value.\n");
	}
}

static void dsmDisabledUnlocked (
	struct amf_comp *component)
{
	log_printf (LOG_LEVEL_DEBUG, "dsmDisabledUnlocked for %s state %d\n",
		getSaNameT (&component->name),
		component->disabledUnlockedState);

	switch (component->disabledUnlockedState) {
		case AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL:
			dsmDisabledUnlockedRegisteredOrErrorCancel (component);
			break;

		case AMF_DISABLED_UNLOCKED_FAILED:
			dsmDisabledUnlockedFailed (component);
			break;

		case AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED:
			dsmDisabledUnlockedQuiescedRequested (component);
			break;

		case AMF_DISABLED_UNLOCKED_QUIESCED_COMPLETED:
			dsmDisabledUnlockedQuiescedCompleted (component);
			break;

		case AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED:
			dsmDisabledUnlockedOutOfServiceRequested (component);
			break;

		case AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED:
			dsmDisabledUnlockedOutOfServiceCompleted (component);
			break;

		default:
			log_printf (LOG_LEVEL_DEBUG, "dsmDisabledUnlocked: unkown state machine value %d.\n", component->disabledUnlockedState);
	}
}

static void dsm (
	struct amf_comp *component)
{
	log_printf (LOG_LEVEL_DEBUG, "dsm for component %s\n", getSaNameT (&component->name));

	switch (component->saAmfUnit->operationalAdministrativeState) {
		case AMF_DISABLED_UNLOCKED:
			dsmDisabledUnlocked (component);
			break;
		case AMF_ENABLED_UNLOCKED:
			dsmEnabledUnlocked (component);
			break;
/*
	AMF_DISABLED_LOCKED,
	AMF_ENABLED_STOPPING
*/
		default:
			log_printf (LOG_LEVEL_DEBUG, "dsm: unknown state machine value.\n");
	}
}


void error_report (
	struct amf_comp *component,
	SaAmfProbableCauseT probableCause)
{
	struct req_exec_amf_componenterrorreport req_exec_amf_componenterrorreport;
	struct iovec iovec;

	req_exec_amf_componenterrorreport.header.size = sizeof (struct req_exec_amf_componenterrorreport);
	req_exec_amf_componenterrorreport.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_ERRORREPORT);

	req_exec_amf_componenterrorreport.source.conn_info = 0;
	req_exec_amf_componenterrorreport.source.in_addr.s_addr = 0;
	memcpy (&req_exec_amf_componenterrorreport.req_lib_amf_componenterrorreport.erroneousComponent,
		&component->name,
		sizeof (SaNameT));
	req_exec_amf_componenterrorreport.req_lib_amf_componenterrorreport.errorDescriptor.probableCause = probableCause;

	iovec.iov_base = (char *)&req_exec_amf_componenterrorreport;
	iovec.iov_len = sizeof (req_exec_amf_componenterrorreport);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovec, 2, TOTEMPG_AGREED) == 0);
}

int healthcheck_instance = 0;

struct saAmfProtectionGroup *protectiongroup_find (
	SaNameT *csiName)
{
	struct list_head *AmfGroupList;
	struct list_head *AmfProtectionGroupList;

	struct saAmfGroup *saAmfGroup;
	struct saAmfProtectionGroup *AmfProtectionGroup;

	/*
	 * Search all groups
	 */
	for (AmfGroupList = saAmfGroupHead.next;
		AmfGroupList != &saAmfGroupHead;
		AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList,
			struct saAmfGroup, saAmfGroupList);

		/*
		 * Search all protection groups
		 */
		for (AmfProtectionGroupList = saAmfGroup->saAmfProtectionGroupHead.next;
			AmfProtectionGroupList != &saAmfGroup->saAmfProtectionGroupHead;
			AmfProtectionGroupList = AmfProtectionGroupList->next) {

			AmfProtectionGroup = list_entry (AmfProtectionGroupList,
				struct saAmfProtectionGroup, saAmfProtectionGroupList);

			if (name_match (csiName, &AmfProtectionGroup->name)) {
				return (AmfProtectionGroup);
			}
		}
	}
	return (0);
}

struct amf_comp *component_in_protectiongroup_find (
	SaNameT *csiName,
	SaNameT *compName)
{

	struct list_head *AmfGroupList = 0;
	struct list_head *AmfProtectionGroupList = 0;
	struct list_head *AmfComponentList = 0;

	struct saAmfGroup *saAmfGroup = 0;
	struct saAmfProtectionGroup *AmfProtectionGroup = 0;
	struct amf_comp *AmfComponent = 0;
	int found = 0;

	/*
	 * Search all groups
	 */
	for (AmfGroupList = saAmfGroupHead.next;
		AmfGroupList != &saAmfGroupHead;
		AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList,
			struct saAmfGroup, saAmfGroupList);

		/*
		 * Search all protection groups
		 */
		for (AmfProtectionGroupList = saAmfGroup->saAmfProtectionGroupHead.next;
			AmfProtectionGroupList != &saAmfGroup->saAmfProtectionGroupHead;
			AmfProtectionGroupList = AmfProtectionGroupList->next) {

			AmfProtectionGroup = list_entry (AmfProtectionGroupList,
				struct saAmfProtectionGroup, saAmfProtectionGroupList);

			if (name_match (csiName, &AmfProtectionGroup->name)) {
				/*
				 * Search all components
				 */
				for (AmfComponentList = AmfProtectionGroup->saAmfMembersHead.next;
					AmfComponentList != &AmfProtectionGroup->saAmfMembersHead;
					AmfComponentList = AmfComponentList->next) {

					AmfComponent = list_entry (AmfComponentList,
						struct amf_comp, saAmfProtectionGroupList);

					if (name_match (compName, &AmfComponent->name)) {
						found = 1;
					}
				}
			}
		}
	}

	if (found) {
		return (AmfComponent);
	} else {
		return (0);
	}
}

/*
 * The response handler for readiness state set callback
 */
static void response_handler_readinessstatesetcallback (struct conn_info *conn_info,
	struct req_lib_amf_response *req_lib_amf_response)
{

	if (req_lib_amf_response->error == SA_AIS_OK && conn_info->component) {

	log_printf (LOG_LEVEL_ENTER_FUNC, "CALLBACK sending readiness state to %s\n", 
		getSaNameT (&conn_info->component->name));
		readiness_state_group_set (conn_info->component, conn_info->component->newReadinessState);
	}
}

/* 
 *	iterate service unit components
 *		telling all components not already QUIESCING to enter SA_AMF_QUIESCED state
 */
static void response_handler_csisetcallback (struct conn_info *conn_info,
	struct req_lib_amf_response *req_lib_amf_response)
{

	if (req_lib_amf_response->error == SA_AIS_OK && conn_info->component) {
		ha_state_group_set (conn_info->component, conn_info->component->newHAState);
	}
}



void amf_confchg_njoin (struct amf_comp *component ,void *data)
{
	if (component->source_addr.s_addr != this_ip->sin_addr.s_addr) {
		return;
	}

	component_register (component);
	return;
}

void amf_confchg_nleave (struct amf_comp *component ,void *data)
{
	struct in_addr *source_addr = (struct in_addr *)data;
	struct saAmfUnit *unit;
	struct list_head *list;
	struct amf_comp *leave_component = NULL;
	enum amfDisabledUnlockedState disablestate = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED;

	if (component->source_addr.s_addr != source_addr->s_addr) {
		return;
	}

	if (!component->registered) {
		return;
	}

	log_printf (LOG_LEVEL_ENTER_FUNC, "amf_confchg_nleave(%s)\n", getSaNameT (&(component->name)));

        /* Component status Initialize */
	unit = component->saAmfUnit;
	
	for (list = unit->amf_compHead.next; list != &unit->amf_compHead; list = list->next) {

		component = list_entry (list,
			struct amf_comp, amf_compList);

		if (component->source_addr.s_addr != source_addr->s_addr) {
			disablestate = AMF_DISABLED_UNLOCKED_FAILED;
			continue;
	  	}

		component->registered = 0;
		component->local = 0;
		component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
		component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_INITIAL;
		component->newReadinessState = SA_AMF_OUT_OF_SERVICE;
		component->currentReadinessState = SA_AMF_OUT_OF_SERVICE;
		component->newHAState = SA_AMF_QUIESCED;
		component->currentHAState = SA_AMF_QUIESCED;
		component->source_addr.s_addr = 0;
		leave_component = component;
	}

	if (leave_component == NULL) {
		return;
	}

	leave_component->saAmfUnit->operationalAdministrativeState = AMF_DISABLED_UNLOCKED;
	leave_component->disabledUnlockedState = disablestate;

	dsm (leave_component);
	leave_component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL;
	
	return;
}


/*
 * If receiving this message from another cluster node, another cluster node
 * has selected a readiness state for a component connected to _that_ cluster
 * node.  That cluster node API has verified the readiness state, so its time to let
 * the rest of the cluster nodes know about the readiness state change.
 */
static void message_handler_req_exec_amf_readinessstateset (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_amf_readinessstateset *req_exec_amf_readinessstateset = (struct req_exec_amf_readinessstateset *)message;
	struct amf_comp *component;

	component = find_comp (&req_exec_amf_readinessstateset->compName);
	if (component) {
	  	log_printf (LOG_LEVEL_FROM_GMI, 
			"Executive: message_handler_req_exec_amf_readinessstateset (%s, RD:%d)\n",
				getSaNameT (&component->name), req_exec_amf_readinessstateset->readinessState);

		component->currentReadinessState = req_exec_amf_readinessstateset->readinessState;
		component->newReadinessState = component->currentReadinessState;
		dsm (component);
	}
	
	return (0);
}

/*
 * If receiving this message from another cluster node, another cluster node
 * has selected a ha state for a component connected to _that_ cluster
 * node.  That cluster node API has verified the ha state, so its time to let
 * the rest of the cluster nodes know about the HA state change.
 */
static void message_handler_req_exec_amf_hastateset (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_amf_hastateset *req_exec_amf_hastateset = (struct req_exec_amf_hastateset *)message;
	struct amf_comp *component;
	SaAmfProtectionGroupChangesT changeToComponent = SA_AMF_PROTECTION_GROUP_STATE_CHANGE;

	component = find_comp (&req_exec_amf_hastateset->compName);
	if (!component) {
		return (0);
	}

  	log_printf (LOG_LEVEL_FROM_GMI, 
		"Executive: message_handler_req_exec_amf_hastateset (%s, HA:%d)\n",
				getSaNameT (&component->name), req_exec_amf_hastateset->haState);

	if ( component->currentHAState == 0 ) {
		if ( req_exec_amf_hastateset->haState == SA_AMF_ACTIVE 
		  || req_exec_amf_hastateset->haState == SA_AMF_STANDBY ) {
			changeToComponent = SA_AMF_PROTECTION_GROUP_ADDED;
		}
	} else {
		if (component->currentHAState == req_exec_amf_hastateset->haState) {
			changeToComponent = SA_AMF_PROTECTION_GROUP_NO_CHANGE;
		}
	}

	component->currentHAState = req_exec_amf_hastateset->haState;
	component->newHAState = component->currentHAState;
	dsm (component);

	if( changeToComponent != SA_AMF_PROTECTION_GROUP_NO_CHANGE ) {
		protectiongroup_notifications_send (component, changeToComponent);
	}
	
	return (0);
}



static void message_handler_req_lib_amf_readinessstateget (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_componentregister *req_lib_amf_componentregister = (struct req_lib_amf_componentregister *)message;
	struct req_exec_amf_componentregister req_exec_amf_componentregister;
	struct iovec iovec;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_amf_componentregister()\n");

	req_exec_amf_componentregister.header.size = sizeof (struct req_exec_amf_componentregister);
	req_exec_amf_componentregister.header.id =
		SERVICE_ID_MAKE (AMF_SERVICE, MESSAGE_REQ_EXEC_AMF_COMPONENTREGISTER);

	message_source_set (&req_exec_amf_componentregister.source, conn_info);

	memcpy (&req_exec_amf_componentregister.req_lib_amf_componentregister,
		req_lib_amf_componentregister,
		sizeof (struct req_lib_amf_componentregister));

	iovec.iov_base = (char *)&req_exec_amf_componentregister;
	iovec.iov_len = sizeof (req_exec_amf_componentregister);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
	return (0);
}

static void message_handler_req_amf_componentunregister (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_componentunregister *req_lib_amf_componentunregister = (struct req_lib_amf_componentunregister *)message;
	struct req_exec_amf_componentunregister req_exec_amf_componentunregister;
	struct iovec iovec;
	struct saAmfComponent *component;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_amf_componentunregister()\n");

	req_exec_amf_componentunregister.header.size = sizeof (struct req_exec_amf_componentunregister);
	req_exec_amf_componentunregister.header.id = MESSAGE_REQ_EXEC_AMF_COMPONENTUNREGISTER;

	message_source_set (&req_exec_amf_componentunregister.source, conn_info);

	memcpy (&req_exec_amf_componentunregister.req_lib_amf_componentunregister,
		req_lib_amf_componentunregister,
		sizeof (struct req_lib_amf_componentunregister));

	component = findComponent (&req_lib_amf_componentunregister->compName);
	if (component && component->registered && component->local) {
		component->probableCause = SA_AMF_NOT_RESPONDING;
	}
	iovec.iov_base = (char *)&req_exec_amf_componentunregister;
	iovec.iov_len = sizeof (req_exec_amf_componentunregister);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
	return (0);
}

static void message_handler_req_amf_readinessstateget (struct conn_info *conn_info, void *message)
{
	struct req_amf_readinessstateget *req_amf_readinessstateget = (struct req_amf_readinessstateget *)message;
>>>>>>> .r872
	struct res_lib_amf_readinessstateget res_lib_amf_readinessstateget;
	struct amf_comp *component;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_readinessstateget()\n");

	res_lib_amf_readinessstateget.header.id = MESSAGE_RES_AMF_READINESSSTATEGET;
	res_lib_amf_readinessstateget.header.size = sizeof (struct res_lib_amf_readinessstateget);
	res_lib_amf_readinessstateget.header.error = SA_ERR_NOT_EXIST;

	component = find_comp (&req_lib_amf_readinessstateget->compName);
	log_printf (LOG_LEVEL_DEBUG, "readinessstateget: found component %p\n", component);
	if (component) {
		memcpy (&res_lib_amf_readinessstateget.readinessState, 
			&component->currentReadinessState, sizeof (SaAmfReadinessStateT));
		res_lib_amf_readinessstateget.header.error = SA_AIS_OK;
	}
	openais_conn_send_response (conn_info, &res_lib_amf_readinessstateget, sizeof (struct res_lib_amf_readinessstateget));
	return (0);
}


static void message_handler_req_lib_amf_stoppingcomplete (struct conn_info *conn_info_notused,
	void *message)
{
	struct req_lib_amf_stoppingcomplete *req_lib_amf_stoppingcomplete = (struct req_lib_amf_stoppingcomplete *)message;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_amf_protectiongrouptrackstart()\n");

	amfProtectionGroup = protectiongroup_find (&req_amf_protectiongrouptrackstart->csiName);
	if (amfProtectionGroup) {
		log_printf (LOG_LEVEL_DEBUG, "protectiongrouptrackstart: Got valid track start on CSI: %s.\n", getSaNameT (&req_amf_protectiongrouptrackstart->csiName));
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
		track->trackFlags = req_amf_protectiongrouptrackstart->trackFlags;
		track->notificationBufferAddress = req_amf_protectiongrouptrackstart->notificationBufferAddress;
		memcpy (&track->csiName,
			&req_amf_protectiongrouptrackstart->csiName, sizeof (SaNameT));

		conn_info->ais_ci.u.libamf_ci.trackActive += 1;

		list_add (&conn_info->conn_list, &library_notification_send_listhead);
	
		/*
		 * If SA_TRACK_CURRENT is specified, write out all current connections
		 */
	} else {
		log_printf (LOG_LEVEL_DEBUG, "invalid track start, csi not registered with system.\n");
	}

	res_lib_amf_protectiongrouptrackstart.header.id = MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTART;
	res_lib_amf_protectiongrouptrackstart.header.size = sizeof (struct res_lib_amf_protectiongrouptrackstart);
	res_lib_amf_protectiongrouptrackstart.header.error = SA_ERR_NOT_EXIST;

	if (amfProtectionGroup) {
		res_lib_amf_protectiongrouptrackstart.header.error = SA_AIS_OK;
	}
	openais_conn_send_response (conn_info, &res_lib_amf_protectiongrouptrackstart,
		sizeof (struct res_lib_amf_protectiongrouptrackstart));

	if (amfProtectionGroup &&
		req_amf_protectiongrouptrackstart->trackFlags & SA_TRACK_CURRENT) {

		protectiongroup_notification_send (conn_info,
			track->notificationBufferAddress, 
			amfProtectionGroup,
			0,
			0,
			SA_TRACK_CHANGES_ONLY);

		track->trackFlags &= ~SA_TRACK_CURRENT;
	}
	return (0);
}

static void message_handler_req_amf_protectiongrouptrackstop (struct conn_info *conn_info, void *message)
{
	struct req_amf_protectiongrouptrackstop *req_amf_protectiongrouptrackstop = (struct req_amf_protectiongrouptrackstop *)message;
	struct res_lib_amf_protectiongrouptrackstop res_lib_amf_protectiongrouptrackstop;
	struct libamf_ci_trackentry *track = 0;
	int i;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_amf_protectiongrouptrackstop()\n");

	for (i = 0; i < conn_info->ais_ci.u.libamf_ci.trackEntries; i++) {
		if (name_match (&req_amf_protectiongrouptrackstop->csiName,
			&conn_info->ais_ci.u.libamf_ci.tracks[i].csiName)) {

			track = &conn_info->ais_ci.u.libamf_ci.tracks[i];
		}
	}

	if (track) {
		log_printf (LOG_LEVEL_DEBUG, "protectiongrouptrackstop: Trackstop on CSI: %s\n", getSaNameT (&req_amf_protectiongrouptrackstop->csiName));
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
	openais_conn_send_response (conn_info, &res_lib_amf_protectiongrouptrackstop,
		sizeof (struct res_lib_amf_protectiongrouptrackstop));

	return (0);
}

static void message_handler_req_amf_errorreport (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_errorreport *req_lib_amf_errorreport = (struct req_lib_amf_errorreport *)message;
	struct req_exec_amf_errorreport req_exec_amf_errorreport;

	struct iovec iovec;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_amf_errorreport()\n");

	req_exec_amf_errorreport.header.size = sizeof (struct req_exec_amf_errorreport);
	req_exec_amf_errorreport.header.id = MESSAGE_REQ_EXEC_AMF_ERRORREPORT;

	message_source_set (&req_exec_amf_errorreport.source, conn_info);

	memcpy (&req_exec_amf_errorreport.req_lib_amf_errorreport,
		req_lib_amf_errorreport,
		sizeof (struct req_lib_amf_errorreport));

	iovec.iov_base = (char *)&req_exec_amf_errorreport;
	iovec.iov_len = sizeof (req_exec_amf_errorreport);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static void message_handler_req_amf_errorcancelall (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_errorcancelall *req_lib_amf_errorcancelall = (struct req_lib_amf_errorcancelall *)message;
	struct req_exec_amf_errorcancelall req_exec_amf_errorcancelall;

	struct iovec iovec;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_amf_errorcancelall()\n");

	req_exec_amf_errorcancelall.header.size = sizeof (struct req_exec_amf_errorcancelall);
	req_exec_amf_errorcancelall.header.id = MESSAGE_REQ_EXEC_AMF_ERRORCANCELALL;

	message_source_set (&req_exec_amf_errorcancelall.source, conn_info);

	memcpy (&req_exec_amf_errorcancelall.req_lib_amf_errorcancelall,
		req_lib_amf_errorcancelall,
		sizeof (struct req_lib_amf_errorcancelall));

	iovec.iov_base = (char *)&req_exec_amf_errorcancelall;
	iovec.iov_len = sizeof (req_exec_amf_errorcancelall);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovec, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static void message_handler_req_amf_stoppingcomplete (struct conn_info *conn_info_notused,
	void *message)
{
	struct req_amf_stoppingcomplete *req_amf_stoppingcomplete = (struct req_amf_stoppingcomplete *)message;
	struct conn_info *inv_conn_info = NULL;
>>>>>>> .r872
	int interface;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_stoppingcomplete()\n");

	req_lib_amf_invocation_get_and_destroy (req_lib_amf_stoppingcomplete->invocation,
		&interface, &inv_conn_info);

	inv_conn_info->component->currentReadinessState = inv_conn_info->component->newReadinessState;

	readiness_state_group_set (inv_conn_info->component, SA_AMF_STOPPING);

	protectiongroup_notifications_send (inv_conn_info->component,SA_AMF_PROTECTION_GROUP_STATE_CHANGE);

	return (0);
}

void response_handler_healthcheckcallback (struct conn_info *conn_info,
	struct req_lib_amf_response *req_lib_amf_response) {

	if (req_lib_amf_response->error == SA_AIS_OK) {
		log_printf (LOG_LEVEL_DEBUG, "setting healthcheck ok\n");
		conn_info->component->healthcheck_outstanding = 0;
	}
}

static void message_handler_req_lib_amf_componentcapabilitymodelget (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_componentcapabilitymodelget *req_lib_amf_componentcapabilitymodelget = (struct req_lib_amf_componentcapabilitymodelget *)message;
	struct res_lib_amf_componentcapabilitymodelget res_lib_amf_componentcapabilitymodelget;
	struct amf_comp *component;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_FROM_LIB, "Handle : message_handler_req_lib_amf_componentcapabilitymodelget()\n");

	memset( &res_lib_amf_componentcapabilitymodelget,0,sizeof(res_lib_amf_componentcapabilitymodelget));
	log_printf (LOG_LEVEL_DEBUG, "componentcapabilitymodelget: Retrieve name %s.\n", getSaNameT (&req_lib_amf_componentcapabilitymodelget->compName));
	component = find_comp (&req_lib_amf_componentcapabilitymodelget->compName);
	if (component && component->registered) {
		memcpy (&res_lib_amf_componentcapabilitymodelget.componentCapabilityModel,
			&component->componentCapabilityModel, sizeof (SaAmfComponentCapabilityModelT));
	} else {
		error = SA_ERR_NOT_EXIST;
	}

	res_lib_amf_componentcapabilitymodelget.header.size = sizeof (struct res_lib_amf_componentcapabilitymodelget);
	res_lib_amf_componentcapabilitymodelget.header.id = MESSAGE_RES_AMF_COMPONENTCAPABILITYMODELGET;
	res_lib_amf_componentcapabilitymodelget.header.error = error;
	openais_conn_send_response (conn_info, &res_lib_amf_componentcapabilitymodelget,
		sizeof (struct res_lib_amf_componentcapabilitymodelget));

	return (0);
}

static char disabled_unlocked_state_text[6][64] = {
	"AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL",
	"AMF_DISABLED_UNLOCKED_FAILED",
	"AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED",
	"AMF_DISABLED_UNLOCKED_QUIESCED_COMPLETED",
	"AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED",
	"AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED"
};

static char *disabledunlockedstate_ntoa (int state)
{
	static char str[64];

	if (state >= 0 && state < 6) {
		sprintf (str, "%s(%d)", disabled_unlocked_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}

	return (str);
}

static char enabled_unlocked_state_text[7][64] = {
	"AMF_ENABLED_UNLOCKED_INITIAL",
	"AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED",
	"AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED",
	"AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED",
	"AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED",
	"AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED",
	"AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED"
};

static char *enabledunlockedstate_ntoa (int state)
{
	static char str[64];
	if (state >= 0 && state < 7) {
		sprintf (str, "%s(%d)", enabled_unlocked_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}
	return (str);
}

#endif
static char presence_state_text[8][32] = {
	"unknown",
	"uninstantiated",
	"instantiating",
	"instantiated",
	"terminating",
	"restarting",
	"instantion_failed",
	"terminiation_failed"
};

static char *presencestate_ntoa (SaAmfPresenceStateT state)
{
	static char str[32];

	if (state > 0 && state < 9) {
		sprintf (str, "%s(%d)", presence_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}
	return (str);
}
static char operational_state_text[4][64] = {
	"Unknown",
	"enabled",
	"disabled"
};

static char *operationalstate_ntoa (SaAmfOperationalStateT state)
{
	static char str[32];

	if (state > 0 && state < 3) {
		sprintf (str, "%s(%d)", operational_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}
	return (str);
}
	
static char readiness_state_text[4][32] = {
	"Unknown",
	"out of service",
	"in service",
	"quiesced",
};

static char *readinessstate_ntoa (int state)
{
	static char str[32];

	if (state > 0 && state < 4) {
		sprintf (str, "%s(%d)", readiness_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}
	return (str);
}

static char ha_state_text[4][32] = {
	"Unknown",
	"active",
	"standby",
	"quiesced",
};

static char *hastate_ntoa (SaAmfHAStateT state)
{

	static char str[32];

	if (state > 0 && state < 4) {
		sprintf (str, "%s(%d)", ha_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}
	return (str);
}

static void amf_dump_comp (struct amf_comp *component ,void *data)
{
	char	name[64];
	int	level = LOG_LEVEL_NOTICE;
	data = NULL;
        struct list_head* type_name_list;
        struct amf_comp_csi_type_name* type_name;

	log_printf (level, "----------------\n" );
	log_printf (level, "registered            = %d\n" ,component->registered);
	log_printf (level, "local                 = %d\n" ,component->local );
	log_printf (level, "source_addr           = %s\n" ,inet_ntoa (component->source_addr));
	memset (name, 0 , sizeof(name));
	memcpy (name, component->name.value, component->name.length);
	log_printf (level, "name                  = %s\n" ,name );
#if 1
        log_printf (level, "csi type names\n");
            for (type_name_list = component->csi_type_name_head.next;
                    type_name_list != &component->csi_type_name_head;
                    type_name_list = type_name_list->next) {

                    type_name = list_entry (type_name_list,
                            struct amf_comp_csi_type_name, list);

                    log_printf (level, "   name      = %s\n" , type_name->name);
            }
#endif
#if COMPILE_OUT
	/*
	*  TODO Change to correct state syntax and implement new ...state_ntoa
	*/    
	log_printf (level, "currentReadinessState = %s\n" ,readinessstate_ntoa (component->currentReadinessState));
	log_printf (level, "newReadinessState     = %s\n" ,readinessstate_ntoa (component->newReadinessState));
	log_printf (level, "currentHAState        = %s\n" ,hastate_ntoa (component->currentHAState));
	log_printf (level, "newHAState            = %s\n" ,hastate_ntoa (component->newHAState));
	log_printf (level, "enabledUnlockedState  = %s\n" ,enabledunlockedstate_ntoa (component->enabledUnlockedState));
	log_printf (level, "disabledUnlockedState = %s\n" ,disabledunlockedstate_ntoa (component->disabledUnlockedState));
	log_printf (level, "probableCause         = %d\n" ,component->probableCause );
#endif
}

void enumerate_components (
	void (*function)(struct amf_comp *, void *data),
	void *data)
{
	struct list_head *AmfGroupList;
	struct list_head *AmfUnitList;
	struct list_head *AmfComponentList;

	struct amf_group *saAmfGroup;
	struct amf_unit *AmfUnit;
	struct amf_comp *AmfComponent;


	/*
	 * Search all groups
	 */
	for (AmfGroupList = amf_groupHead.next;
		AmfGroupList != &amf_groupHead;
		AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList,
			struct amf_group, group_list);

		/*
		 * Search all units
		 */
		for (AmfUnitList = saAmfGroup->unit_head.next;
			AmfUnitList != &saAmfGroup->unit_head;
			AmfUnitList = AmfUnitList->next) {

			AmfUnit = list_entry (AmfUnitList,
				struct amf_unit, unit_list);

			/*
			 * Search all components
			 */
			for (AmfComponentList = AmfUnit->comp_head.next;
				AmfComponentList != &AmfUnit->comp_head;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList,
					struct amf_comp, comp_list);

				function (AmfComponent, data);
			}
		}
	}
}

void amf_dump ( )
{
	enumerate_components (amf_dump_comp, NULL);
	fflush (stderr);

	return;
}

