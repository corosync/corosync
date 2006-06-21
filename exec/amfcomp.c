/** @file amfcomp.c
 * 
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 *  Author: Hans Feldt
 * - Introduced AMF B.02 information model
 * - Use DN in API and multicast messages
 * - (Re-)Introduction of event based multicast messages
 * - Refactoring of code into several AMF files
 *  Author: Anders Eriksson
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
 * AMF Component Class Implementation
 * 
 * This file contains functions for handling AMF-components. It can be 
 * viewed as the implementation of the AMF Component class (called comp)
 * as described in SAI-Overview-B.02.01. The SA Forum specification 
 * SAI-AIS-AMF-B.02.01 has been used as specification of the behaviour
 * and is referred to as 'the spec' below.
 * 
 * The functions in this file are responsible for handling the following
 * types of components:
 * 	- sa-aware components
 * 	  (proxy or non-proxy)
 *	- non-sa-aware components
 *	  (non-proxied non-pre-instantiable and
 *     proxied pre-instantiable or not pre-instantiable)
 *
 * The functions of this file are also responsible for:
 *	- handling all communication with the AMF API library supported by the
 *    AMF main function, see below
 *	- instantiating and terminating components upon request
 *	- updating the ha-state of the CSI-assignment related to the component
 *	- initiating an error report to the parent SU
 *	- handling all run time attributes of the AMF Component; all cached
 *	  attributes are stored as variables and sent to the IMM service
 *	  upon the changes described in the specification.
 *
 * Incoming events from the AMF library is primarily handled by the AMF
 * main function which:
 * 	<1> transforms the incoming event to an event that is multicast
 *	    to all AMF service instances in the cluster
 *	<2> the event received from multicast is tranformed to a function
 *	    call of the external interface of comp
 *
 * Outgoing events to the AMF library is handled by static functions called
 * lib_<api callback function name>_request which creates an invocation handle
 * unique to this call and stores any variables comp want to associate to the
 * call back so it is possible to pick them up when the component responses
 * through the API. Finally, a timer is started to supervise that a response
 * really is received.
 *
 * Comp initiates error reports to its parent SU in the cases described in
 * paragraph 3.3.2.2 in the spec. Comp delegates all actions to SU except
 *	- it stores the received or pre-configured recommended recovery
 *	  action 
 *	- sets the operational state to DISABLED unless the
 *	  recommended recovery action was SA_AMF_COMP_RESTART. (In this case
 *	  SU or node may set operational state of the component later on
 *	  when it has been fully investigated that no escallation to a
 *	  more powerful recovery action shall be made.)
 *
 * Comp contains the following state machines:
 *	- presence state machine (PRSM)
 *	- operational state machine (OPSM)
 *	- readiness state machine (RESM)
 *	- ha state per component service instance (CSI)
 *
 * The behaviour of comp is mainly controlled by the presence state machine,
 * while the operational and readiness state machines are used only to report
 * information to its parent (service unit SU) and management (IMM). Comp does
 * not control the logic to assign a CSI to itself and neither to decide the
 * value of the ha-state but only to faciltate the communication of the CSI
 * set (or remove) order and to evaluate the response from the library.
 *
 * The presence state machine implements all the states described in the 
 * specification.
 * The '-ING' states of PRSM are designed as composite states (UML terminology).
 * Being a composite state means that the state contains substates.
 * PRSM composite states are:
 * 	- TERMINATING (TERMINATE and CLEANUP)
 *	- INSTANTIATING (INSTANTIATE, INSTANTIATEDELAY and CLEANUP)
 *	- RESTARTING (TERMINATE, INSTANTIATE, INSTANTIATEDELAY and CLEANUP)
 *
 * The reason for introducing these composite states is to make it easier to
 * understand the implementation of the behaviour described in paragraphs
 * 4.1 - 4.6 in the spec. The comp PRSM implements all the logic described
 * except for node reboot, which is handled by the AMF Node class.
 * Also PRSM reports all changes of state to its parent SU.
 * 
 */


#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_amf.h"
#include "totempg.h"
#include "aispoll.h"
#include "main.h"
#include "ipc.h"
#include "service.h"
#include "util.h"
#include "amf.h"
#include "print.h"

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

struct clc_interface {
	int (*instantiate) (struct amf_comp *comp);
	int (*terminate) (struct amf_comp *comp);
	int (*cleanup) (struct amf_comp *comp);
};

struct csi_remove_callback_data {
	struct amf_csi *csi;
};

struct component_terminate_callback_data {
	struct amf_comp *comp;
};

static void comp_presence_state_set (
	struct amf_comp *comp,
	SaAmfPresenceStateT presence_state);
static void comp_operational_state_set (
	struct amf_comp *comp,
	SaAmfOperationalStateT operational_state);
static int clc_cli_instantiate (struct amf_comp *comp);
static int clc_instantiate_callback (struct amf_comp *comp);
static int clc_csi_set_callback (struct amf_comp *comp);
static int clc_cli_terminate (struct amf_comp *comp);
static int lib_comp_terminate_request (struct amf_comp *comp);
static int clc_csi_remove_callback (struct amf_comp *comp);
static int clc_cli_cleanup (struct amf_comp *comp);
static int clc_cli_cleanup_local (struct amf_comp *comp);
static void healthcheck_deactivate (struct amf_healthcheck *healthcheck_active);
static void lib_healthcheck_request (struct amf_healthcheck *healthcheck);
static void timer_function_healthcheck_tmo (void *_healthcheck);

/*
 * Life cycle functions
 */
static struct clc_interface clc_interface_sa_aware = {
	clc_cli_instantiate,
	lib_comp_terminate_request,
	clc_cli_cleanup
};

static struct clc_interface clc_interface_proxied_pre = {
	clc_instantiate_callback,
	lib_comp_terminate_request,
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

struct invocation {
	void *data;
	int interface;
	int active;
};

static struct invocation *invocation_entries = 0;
static int invocation_entries_size = 0;

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

static int invocation_get_and_destroy (SaUint64T invocation, int *interface,
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

static int invocation_get (SaUint64T invocation, int *interface,
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

char *amf_comp_dn_make (struct amf_comp *comp, SaNameT *name)
{
	int	i = snprintf((char*) name->value, SA_MAX_NAME_LENGTH,
		"safComp=%s,safSu=%s,safSg=%s,safApp=%s",
		comp->name.value, comp->su->name.value,
		comp->su->sg->name.value, comp->su->sg->application->name.value);
	assert (i <= SA_MAX_NAME_LENGTH);
	name->length = i;
	return (char *)name->value;
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
	assert (i < 10);

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
	assert (i < 10);

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

static int lib_comp_terminate_request (struct amf_comp *comp)
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
static void mcast_cleanup_completion_event (void *context)
{
	struct clc_command_run_data *clc_command_run_data =
		(struct clc_command_run_data *)context;
	struct req_exec_amf_clc_cleanup_completed req;
	struct iovec iovec;

	TRACE2("CLC cleanup done for '%s'",
		   clc_command_run_data->comp->name.value);

	req.header.size = sizeof (struct req_exec_amf_clc_cleanup_completed);
	req.header.id =	SERVICE_ID_MAKE (AMF_SERVICE,
		MESSAGE_REQ_EXEC_AMF_CLC_CLEANUP_COMPLETED);

	amf_comp_dn_make (clc_command_run_data->comp, &req.compName);
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
	clc_command_run_data->completion_callback = mcast_cleanup_completion_event;

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

struct amf_healthcheck *amf_comp_find_healthcheck (
	struct amf_comp *comp, SaAmfHealthcheckKeyT *key)
{
	struct amf_healthcheck *healthcheck;
	struct amf_healthcheck *ret_healthcheck = 0;

	if (key == NULL) {
		return NULL;
	}

	for (healthcheck = comp->healthcheck_head;
		healthcheck != NULL;
		healthcheck = healthcheck->next) {

		if (memcmp (key, &healthcheck->safHealthcheckKey,
					sizeof (SaAmfHealthcheckKeyT)) == 0) {
			ret_healthcheck = healthcheck;
			break;
		}
	}

	return (ret_healthcheck);
}

struct amf_comp *amf_comp_create(struct amf_su *su)
{
	struct amf_comp *comp = calloc (1, sizeof (struct amf_comp));

	if (comp == NULL) {
		openais_exit_error(AIS_DONE_OUT_OF_MEMORY);
	}
	comp->next = su->comp_head;
	su->comp_head = comp;
	comp->su = su;
	comp->saAmfCompOperState = SA_AMF_OPERATIONAL_DISABLED;
	comp->saAmfCompPresenceState = SA_AMF_PRESENCE_UNINSTANTIATED;
	comp->saAmfCompNumMaxInstantiateWithoutDelay = 2;
	comp->saAmfCompNumMaxAmStartAttempt = 2;
	comp->saAmfCompNumMaxAmStopAttempt = 2;

	return comp;
}

struct amf_comp *amf_comp_find (struct amf_cluster *cluster, SaNameT *name)
{
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;
	struct amf_comp *comp = NULL;
	char *app_name;
	char *sg_name;
	char *su_name;
	char *comp_name;
	char *ptrptr;
	char *buf;

	/* malloc new buffer since strtok_r writes to its first argument */
	buf = malloc (name->length);
	memcpy (buf, name->value,name ->length);

	comp_name = strtok_r(buf, ",", &ptrptr);
	su_name = strtok_r(NULL, ",", &ptrptr);
	sg_name = strtok_r(NULL, ",", &ptrptr);
	app_name = strtok_r(NULL, ",", &ptrptr);

	if (comp_name == NULL || su_name == NULL ||
		sg_name == NULL || app_name == NULL) {
		goto end;
	}

	comp_name +=  8;
	su_name += 6;
	sg_name += 6;
	app_name += 7;

	for (app = cluster->application_head; app != NULL; app = app->next) {
		if (strncmp (app_name,
					 (char*)app->name.value, app->name.length) == 0) {
			for (sg = app->sg_head; sg != NULL; sg = sg->next) {
				if (strncmp (sg_name, (char*)sg->name.value,
							 sg->name.length) == 0) {
					for (su = sg->su_head; su != NULL; su = su->next) {
						if (strncmp (su_name, (char*)su->name.value,
									 su->name.length) == 0) {
							for (comp = su->comp_head;
								  comp != NULL;
								  comp = comp->next) {
								if (strncmp (comp_name,
											 (char*)comp->name.value,
											 comp->name.length) == 0) {
									goto end;
								}
							}
						}
					}
				}
			}
		}
	}

end:
	free (buf);
	return comp;
}

void amf_comp_healthcheck_deactivate (struct amf_comp *comp)
{
	struct amf_healthcheck *healthcheck;

	if (!amf_su_is_local (comp->su))
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

static void comp_ha_state_set (	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment,
	SaAmfHAStateT ha_state)
{
	csi_assignment->saAmfCSICompHAState = ha_state;
	TRACE1 ("Setting comp '%s' HA state: %s\n",
			comp->name.value, amf_ha_state (csi_assignment->saAmfCSICompHAState));
	amf_su_comp_hastate_changed (comp->su, comp, csi_assignment);
}

static void comp_presence_state_set (struct amf_comp *comp,
	SaAmfPresenceStateT presence_state)
{
	comp->saAmfCompPresenceState = presence_state;
	TRACE1 ("Setting comp '%s' presence state: %s\n",
			comp->name.value, amf_presence_state (comp->saAmfCompPresenceState));

	amf_su_comp_state_changed (
		comp->su, comp, SA_AMF_PRESENCE_STATE, presence_state);
}

static void comp_operational_state_set (struct amf_comp *comp,
	SaAmfOperationalStateT oper_state)
{
	comp->saAmfCompOperState = oper_state;
	TRACE1 ("Setting comp '%s' operational state: %s\n",
			comp->name.value, amf_op_state (comp->saAmfCompOperState));
	amf_su_comp_state_changed (
		comp->su, comp, SA_AMF_OP_STATE, oper_state);
}

#if 0
static void lib_csi_remove_request (struct amf_comp *comp,
	struct amf_csi *csi)
{
	struct res_lib_amf_csiremovecallback res_lib_amf_csiremovecallback;
	struct csi_remove_callback_data *csi_remove_callback_data;

	dprintf ("\t%s\n", getSaNameT (&comp->name));

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

static void comp_reassign_csis (struct amf_comp *comp)
{
	struct amf_csi_assignment *csi_assignment = comp->assigned_csis;

	ENTER ("'%s'", comp->name.value);

	for (; csi_assignment; csi_assignment = csi_assignment->comp_next) {
		amf_comp_hastate_set (comp, csi_assignment,
			csi_assignment->saAmfCSICompHAState);
	}
}

static void healthcheck_deactivate (
	struct amf_healthcheck *healthcheck_active)
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

/**
 * This function is called by the timer subsystem when AMF should request
 * a new healthcheck from a component.
 * @param data
 */
static void timer_function_healthcheck_next_fn (void *_healthcheck)
{
	struct amf_healthcheck *healthcheck = _healthcheck;

	/* send healthcheck request to component */
	lib_healthcheck_request (healthcheck);

	/* start duration timer for response */
	poll_timer_add (aisexec_poll_handle,
		healthcheck->saAmfHealthcheckMaxDuration,
		(void *)healthcheck,
		timer_function_healthcheck_tmo,
		&healthcheck->timer_handle_duration);
}

/**
 * Multicast a healthcheck timeout event.
 * @param healthcheck
 */
static void mcast_healthcheck_tmo_event (
	struct amf_healthcheck *healthcheck)
{
	struct req_exec_amf_healthcheck_tmo req_exec;
	struct iovec iovec;
	req_exec.header.size = sizeof (struct req_exec_amf_healthcheck_tmo);
	req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
		MESSAGE_REQ_EXEC_AMF_HEALTHCHECK_TMO);

	amf_comp_dn_make (healthcheck->comp, &req_exec.compName);
	memcpy (&req_exec.safHealthcheckKey,
			&healthcheck->safHealthcheckKey, sizeof (SaAmfHealthcheckKeyT));
	iovec.iov_base = (char *)&req_exec;
	iovec.iov_len = sizeof (req_exec);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
}

/**
 * This function is called by the timer subsystem when a component has not
 * performed a healthcheck on time.
 * The event is multicasted to the cluster.
 * @param data
 */
static void timer_function_healthcheck_tmo (
	void *_healthcheck)
{
	struct amf_healthcheck *healthcheck = (struct amf_healthcheck *)_healthcheck;

	TRACE2 ("timeout occured on healthcheck for component %s.\n",
		getSaNameT (&healthcheck->comp->name));

	mcast_healthcheck_tmo_event (healthcheck);
}

static void lib_healthcheck_request (struct amf_healthcheck *healthcheck)
{
	struct res_lib_amf_healthcheckcallback res_lib_amf_healthcheckcallback;

	res_lib_amf_healthcheckcallback.header.id =
		MESSAGE_RES_AMF_HEALTHCHECKCALLBACK;
	res_lib_amf_healthcheckcallback.header.size =
		sizeof (struct res_lib_amf_healthcheckcallback);
	res_lib_amf_healthcheckcallback.header.error = SA_AIS_OK;

	res_lib_amf_healthcheckcallback.invocation =
		invocation_create (AMF_RESPONSE_HEALTHCHECKCALLBACK, healthcheck);

	amf_comp_dn_make (healthcheck->comp,
					  &res_lib_amf_healthcheckcallback.compName);
	memcpy (&res_lib_amf_healthcheckcallback.key,
		&healthcheck->safHealthcheckKey,
		sizeof (SaAmfHealthcheckKeyT));

	TRACE8 ("sending healthcheck request to component %s",
			res_lib_amf_healthcheckcallback.compName.value);
	openais_conn_send_response (
		openais_conn_partner_get (healthcheck->comp->conn),
		&res_lib_amf_healthcheckcallback,
		sizeof (struct res_lib_amf_healthcheckcallback));
}

static void lib_csi_set_request (
	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment,
	SaAmfHAStateT requested_ha_state)
{
	struct res_lib_amf_csisetcallback* res_lib_amf_csisetcallback;     
	void*  p;
	struct amf_csi_attribute *attribute;
	size_t char_length_of_csi_attrs=0;
	size_t num_of_csi_attrs=0;
	int i;
	struct amf_csi *csi;
	char* csi_attribute_buf;
	unsigned int byte_offset;

	csi_assignment->requested_ha_state = requested_ha_state;
	csi = csi_assignment->csi;

	dprintf("\t   Assigning CSI '%s' state %s to comp '%s'\n",
		getSaNameT (&csi->name), amf_ha_state (requested_ha_state),
			comp->name.value);

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
	csi_attribute_buf = res_lib_amf_csisetcallback->csi_attr_buf;

	/* Byteoffset start at the zero byte  */
	byte_offset = 0;

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

	switch (requested_ha_state) {
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
		sizeof (struct res_lib_amf_csisetcallback) +
		char_length_of_csi_attrs;
	res_lib_amf_csisetcallback->header.error = SA_AIS_OK;

	amf_comp_dn_make (comp, &res_lib_amf_csisetcallback->compName);
	amf_csi_dn_make (csi, &res_lib_amf_csisetcallback->csiName);

	res_lib_amf_csisetcallback->haState = requested_ha_state;
	res_lib_amf_csisetcallback->invocation =
		invocation_create (AMF_RESPONSE_CSISETCALLBACK, csi_assignment);

	openais_conn_send_response (openais_conn_partner_get (comp->conn),
		res_lib_amf_csisetcallback,
		res_lib_amf_csisetcallback->header.size);

	free(p);
}

SaAisErrorT amf_comp_register (struct amf_comp *comp)
{
	TRACE2("Exec comp register '%s'", &comp->name.value);

	if (comp->saAmfCompPresenceState == SA_AMF_PRESENCE_RESTARTING) {
		comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATED);
		if (comp->saAmfCompReadinessState == SA_AMF_READINESS_IN_SERVICE) {
			comp_reassign_csis (comp);
		}
	} else if (comp->saAmfCompPresenceState == SA_AMF_PRESENCE_INSTANTIATING) {
		comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATED);
		comp_operational_state_set (comp, SA_AMF_OPERATIONAL_ENABLED);
	}
	else {
		assert (0);
	}

	return SA_AIS_OK;
}

void amf_comp_error_report (
	struct amf_comp *comp, SaAmfRecommendedRecoveryT recommendedRecovery)
{
	struct res_lib_amf_componenterrorreport res_lib;

	TRACE2("Exec comp error report '%s'", &comp->name.value);

	if (amf_su_is_local (comp->su)) {
		res_lib.header.size = sizeof (struct res_lib_amf_componenterrorreport);
		res_lib.header.id = MESSAGE_RES_AMF_COMPONENTERRORREPORT;
		res_lib.header.error = SA_AIS_OK;
		openais_conn_send_response (comp->conn, &res_lib, sizeof (res_lib));
	}

	/* report to SU and let it handle the problem */
	amf_su_comp_error_suspected (comp->su, comp, recommendedRecovery);
}

/**
 * Healthcheck timeout event handler
 * @param comp
 * @param healthcheck
 */
void amf_comp_healthcheck_tmo (
	struct amf_comp *comp, struct amf_healthcheck *healthcheck)
{
	TRACE2("Exec healthcheck tmo for '%s'", &comp->name.value);

	/* report to SU and let it handle the problem */
	amf_su_comp_error_suspected (
		comp->su, comp, healthcheck->recommendedRecovery);
}

/**
 * Event method to be called when a cleanup completed event is received
 * @param comp
 */
void amf_comp_cleanup_completed (struct amf_comp *comp)
{
	TRACE2("Exec CLC cleanup completed for '%s'", &comp->name.value);
	amf_comp_instantiate (comp);
}

/**
 * Handle the request from a component to start a healthcheck
 * 
 * @param comp
 * @param healthcheckKey
 * @param invocationType
 * @param recommendedRecovery
 * 
 * @return SaAisErrorT - return value to component
 */
SaAisErrorT amf_comp_healthcheck_start (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey,
	SaAmfHealthcheckInvocationT invocationType,
	SaAmfRecommendedRecoveryT recommendedRecovery)
{
	struct amf_healthcheck *healthcheck;
	SaAisErrorT error = SA_AIS_OK;

	healthcheck = amf_comp_find_healthcheck (comp, healthcheckKey);
	if (healthcheck == 0) {
		log_printf (LOG_ERR, "Healthcheckstart: Healthcheck '%s' not found",
					healthcheckKey->key);
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	dprintf ("Healthcheckstart: '%s', key '%s'",
			 comp->name.value, healthcheckKey->key);

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
	healthcheck->invocationType = invocationType;
	healthcheck->recommendedRecovery = recommendedRecovery;
	healthcheck->timer_handle_duration = 0;
	healthcheck->timer_handle_period = 0;
	healthcheck->active = 1;

	if (invocationType == SA_AMF_HEALTHCHECK_AMF_INVOKED) {
		/* start timer to execute first healthcheck request */
		poll_timer_add (aisexec_poll_handle,
						healthcheck->saAmfHealthcheckPeriod,
						(void *)healthcheck,
						timer_function_healthcheck_next_fn,
						&healthcheck->timer_handle_period);
	} else if (invocationType == SA_AMF_HEALTHCHECK_COMPONENT_INVOKED) {
		/* start supervision timer */
		poll_timer_add (aisexec_poll_handle,
			healthcheck->saAmfHealthcheckPeriod,
			(void *)healthcheck,
			timer_function_healthcheck_tmo,
			&healthcheck->timer_handle_period);
	} else {
		error = SA_AIS_ERR_INVALID_PARAM;
	}

error_exit:
	return error;
}

/**
 * Stop all or a specifed healthcheck
 * @param comp
 * @param healthcheckKey - NULL if all
 * 
 * @return SaAisErrorT
 */
SaAisErrorT amf_comp_healthcheck_stop (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey)
{
	struct amf_healthcheck *healthcheck;
	SaAisErrorT error = SA_AIS_OK;

	dprintf ("Healthcheckstop: '%s', key '%s'",
			 comp->name.value, healthcheckKey->key);

	if (healthcheckKey == NULL) {
		for (healthcheck = comp->healthcheck_head;
			  healthcheck != NULL;
			  healthcheck = healthcheck->next) {
			healthcheck_deactivate (healthcheck);
		}
	} else {
		healthcheck = amf_comp_find_healthcheck (comp, healthcheckKey);
		if (healthcheck == NULL) {
			log_printf (LOG_ERR, "Healthcheckstop: Healthcheck '%s' not found",
						healthcheckKey->key);
			error = SA_AIS_ERR_NOT_EXIST;
		} else {
			healthcheck_deactivate (healthcheck);
		}
	}

	return error;
}

/**
 * Instantiate a component
 * @param comp
 */
void amf_comp_instantiate (struct amf_comp *comp)
{
	int res = 0;

	ENTER ("'%s'", getSaNameT (&comp->name));

	if (comp->saAmfCompPresenceState != SA_AMF_PRESENCE_RESTARTING) {
		comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATING);
	}

	if (amf_su_is_local (comp->su)) {
		res = clc_interfaces[comp->comptype]->instantiate (comp);
	}
}

void amf_comp_readiness_state_set (struct amf_comp *comp,
	SaAmfReadinessStateT state)
{
#if 0
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
#endif

	comp->saAmfCompReadinessState = state;
	TRACE1 ("Setting comp '%s' readiness state: %s\n",
		comp->name.value, amf_readiness_state (comp->saAmfCompReadinessState));
}

/**
 * Handle a component response (received from the lib) of an earlier AMF request.
 * This function should be invoked when the lib request is received.
 * @param invocation [in] associates the response with the request (callback)
 * @param error [in] response from the component of the associated callback
 * @param retval [out] contains return value to component when needed
 * 
 * @return ==0 respond to component, do not multicast
 * @return >0  do not respond to component, multicast response
 */
int amf_comp_response_1 (
	SaInvocationT invocation, SaAisErrorT error, SaAisErrorT *retval)
{
	int res;
	int interface;
	void *data;

	res = invocation_get (invocation, &interface, &data);

	if (res == -1) {
		log_printf (LOG_ERR, "Lib response: invocation not found\n");
		*retval = SA_AIS_ERR_INVALID_PARAM;
		return 0;
	}

	switch (interface) {
		case AMF_RESPONSE_HEALTHCHECKCALLBACK: {
			struct amf_healthcheck *healthcheck = data;
			SaNameT name;
			TRACE3 ("Healthcheck response from '%s': %d",
					amf_comp_dn_make (healthcheck->comp, &name), error);

			if (healthcheck->invocationType == SA_AMF_HEALTHCHECK_AMF_INVOKED) {
				/* the response was on time, delete supervision timer */
				poll_timer_delete (aisexec_poll_handle,
								   healthcheck->timer_handle_duration);
				healthcheck->timer_handle_duration = 0;

				/* start timer to execute next healthcheck request */
				poll_timer_add (aisexec_poll_handle,
								healthcheck->saAmfHealthcheckPeriod,
								(void *)healthcheck,
								timer_function_healthcheck_next_fn,
								&healthcheck->timer_handle_period);
				*retval = SA_AIS_OK;
			} else {
				*retval = SA_AIS_ERR_INVALID_PARAM;
			}

			return 0; /* do not multicast event */
			break;
		}
		case AMF_RESPONSE_CSISETCALLBACK: /* fall-through */
	    case AMF_RESPONSE_CSIREMOVECALLBACK:
			return 1; /* multicast event */
			break;
#if 0
		case AMF_RESPONSE_COMPONENTTERMINATECALLBACK: {
			struct component_terminate_callback_data *component_terminate_callback_data;
			component_terminate_callback_data = data;

			dprintf ("Lib component terminate callback response, error: %d", error);
			amf_comp_healthcheck_deactivate (component_terminate_callback_data->comp);
			escalation_policy_restart (component_terminate_callback_data->comp);
			return 1;
			break;
		}
#endif
		default:
			assert (0);
			break;
	}
}

/**
 * Handle a component response (received from EVS) of an earlier AMF request.
 * This function should be invoked when the multicast request is received.
 * @param invocation [in] associates the response with the request (callback)
 * @param error [in] response from the component of the associated callback
 * @param retval [out] contains return value to component when needed
 * 
 * @return component to which the response should be sent
 */
struct amf_comp *amf_comp_response_2 (
	SaInvocationT invocation, SaAisErrorT error, SaAisErrorT *retval)
{
	int res;
	int interface;
	void *data;
	struct amf_comp *comp = NULL;

	assert (retval != NULL);

	*retval = SA_AIS_OK;

	res = invocation_get_and_destroy (invocation, &interface, &data);
	if (res == -1) {
		log_printf (LOG_ERR, "Comp response: invocation not found\n");
		*retval = SA_AIS_ERR_INVALID_PARAM;
		return NULL;
	}

	switch (interface) {
		case AMF_RESPONSE_CSISETCALLBACK: {
			struct amf_csi_assignment *csi_assignment = data;
			dprintf ("CSI '%s' set callback response from '%s', error: %d",
					 csi_assignment->csi->name.value, csi_assignment->comp->name.value, error);
			comp = csi_assignment->comp;
			if (error == SA_AIS_OK) {
				comp_ha_state_set (comp, csi_assignment,
								   csi_assignment->requested_ha_state);
			} else if (error == SA_AIS_ERR_FAILED_OPERATION) {
				amf_su_comp_error_suspected (comp->su, comp,
											 comp->saAmfCompRecoveryOnError);
			} else {
				*retval = SA_AIS_ERR_INVALID_PARAM;
			}
			break;
		}
		case AMF_RESPONSE_CSIREMOVECALLBACK: {
			struct amf_csi_assignment *csi_assignment = data;
			dprintf ("Lib csi '%s' remove callback response from '%s', error: %d",
					 csi_assignment->csi->name.value, csi_assignment->comp->name.value, error);
			comp = csi_assignment->comp;
			amf_su_comp_hastate_changed (comp->su, comp, csi_assignment);
			break;
		}
#if 0
		case AMF_RESPONSE_COMPONENTTERMINATECALLBACK:
			break;
#endif
		default:
			assert (0);
			break;
	}

	return comp;
}

/**
 * Request a component to assume a particular HA state
 * @param comp
 * @param csi_assignment
 * @param requested_ha_state
 */
void amf_comp_hastate_set (
	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment,
	SaAmfHAStateT requested_ha_state)
{
	assert (comp != NULL && csi_assignment != NULL);

	if (!amf_su_is_local (comp->su))
		return;

	lib_csi_set_request(comp, csi_assignment, requested_ha_state);
}

/**
 * Request termination of a component
 * @param comp
 */
void amf_comp_terminate (struct amf_comp *comp)
{
	dprintf ("comp terminate '%s'\n", getSaNameT (&comp->name));
	amf_comp_healthcheck_stop (comp, NULL);
	comp_presence_state_set (comp, SA_AMF_PRESENCE_TERMINATING);

	if (amf_su_is_local (comp->su)) {
		clc_interfaces[comp->comptype]->terminate (comp);
	}
}

/**
 * Request restart of a component
 * @param comp
 */
void amf_comp_restart (struct amf_comp *comp)
{
	dprintf ("comp restart '%s'\n", getSaNameT (&comp->name));
	amf_comp_healthcheck_stop (comp, NULL);
	comp_presence_state_set (comp, SA_AMF_PRESENCE_RESTARTING);

	if (amf_su_is_local (comp->su)) {
		clc_interfaces[comp->comptype]->cleanup (comp);
	}
}

/**
 * Request to return the HA state for a components CSI
 * @param comp
 * @param csi_name
 * @param ha_state
 * 
 * @return SaAisErrorT
 */
SaAisErrorT amf_comp_hastate_get (
	struct amf_comp *comp, SaNameT *csi_name, SaAmfHAStateT *ha_state)
{
	struct amf_csi_assignment *assignment;
	SaNameT name;

	assert (comp != NULL && csi_name != NULL && ha_state != NULL);

	dprintf ("comp ha state get from comp '%s' CSI '%s'\n",
			 getSaNameT (&comp->name), csi_name->value);

	for (assignment = comp->assigned_csis;
		  assignment != NULL; assignment = assignment->comp_next) {
		amf_csi_dn_make (assignment->csi, &name);
		if (name_match (csi_name, &name)) {
			*ha_state = assignment->saAmfCSICompHAState;
			return SA_AIS_OK;
		}
	}

	return SA_AIS_ERR_INVALID_PARAM;
}

/**
 * Response from a component informs AMF that it has performed a healthcheck
 * @param comp
 * @param healthcheckKey
 * @param healthcheckResult
 * 
 * @return SaAisErrorT
 */
SaAisErrorT amf_comp_healthcheck_confirm (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey,
	SaAisErrorT healthcheckResult)
{
	struct amf_healthcheck *healthcheck;
	SaAisErrorT error = SA_AIS_OK;

	dprintf ("Healthcheckconfirm: '%s', key '%s'",
			 comp->name.value, healthcheckKey->key);

	healthcheck = amf_comp_find_healthcheck (comp, healthcheckKey);
	if (healthcheck == NULL) {
		log_printf (LOG_ERR, "Healthcheckstop: Healthcheck '%s' not found",
					healthcheckKey->key);
		error = SA_AIS_ERR_NOT_EXIST;
	} else if (healthcheck->active) {
		if (healthcheckResult == SA_AIS_OK) {
			/* the response was on time, restart the supervision timer */
			poll_timer_delete (aisexec_poll_handle,
							   healthcheck->timer_handle_period);
			poll_timer_add (aisexec_poll_handle,
				healthcheck->saAmfHealthcheckPeriod,
				(void *)healthcheck,
				timer_function_healthcheck_tmo,
				&healthcheck->timer_handle_period);
		} else if (healthcheckResult == SA_AIS_ERR_FAILED_OPERATION) {
			/* send to cluster */
			mcast_healthcheck_tmo_event (healthcheck);
		} else {
			error = SA_AIS_ERR_INVALID_PARAM;
		}
	} else {
		error = SA_AIS_ERR_INVALID_PARAM;
	}

	return error;
}

void amf_comp_init (void)
{
	log_init ("AMF");
}

