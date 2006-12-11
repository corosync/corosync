/** @file amfcomp.c
 * 
 * Copyright (c) 2006 Ericsson AB.
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc. Copyright (c) 2006
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Author: Hans Feldt, Anders Eriksson, Lars Holm
 * - Introduced AMF B.02 information model
 * - Use DN in API and multicast messages
 * - (Re-)Introduction of event based multicast messages
 * - Refactoring of code into several AMF files
 * - Component/SU restart, SU failover
 * - Constructors/destructors
 * - Serializers/deserializers
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
#include "timer.h"
#include "ipc.h"
#include "service.h"
#include "util.h"
#include "amf.h"
#include "print.h"
#include "main.h"

enum clc_command_run_operation_type {
	CLC_COMMAND_RUN_OPERATION_TYPE_INSTANTIATE = 1,
	CLC_COMMAND_RUN_OPERATION_TYPE_TERMINATE = 2,
	CLC_COMMAND_RUN_OPERATION_TYPE_CLEANUP = 3
};

struct clc_command_run_data {
	struct amf_comp *comp;
	enum clc_command_run_operation_type type;
	void (*completion_callback) (void *context);
	int exit_code;
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
static void lib_csi_set_request (
	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment);

static void comp_recover_action (amf_comp_t *comp, 
	SaAmfRecommendedRecoveryT recommendedRecovery);

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

static int is_not_instantiating_or_instantiated_or_restarting (amf_comp_t *comp)
{
	return (!(comp->saAmfCompPresenceState == SA_AMF_PRESENCE_INSTANTIATING ||
			  comp->saAmfCompPresenceState == SA_AMF_PRESENCE_INSTANTIATED ||
			  comp->saAmfCompPresenceState == SA_AMF_PRESENCE_RESTARTING));
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
		if (invocation_temp == NULL) {
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
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

static int invocation_get_and_destroy (
	SaUint64T invocation, unsigned int *interface, void **data)
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

/**
 * Set suspected error flag and report to SU.
 * 
 * @param comp
 * @param recommended_recovery
 */
static void report_error_suspected (
	struct amf_comp *comp,
	SaAmfRecommendedRecoveryT recommended_recovery)
{
	ENTER ("%s, recommended_recovery = %d",
		comp->name.value, recommended_recovery);
	amf_comp_error_suspected_set (comp);
	comp_recover_action (comp, recommended_recovery);
}


#ifndef xprintf
#define xprintf(...)
#endif
static void *clc_command_run (void *context)
{
	struct clc_command_run_data *clc_command_run_data =
		(struct clc_command_run_data *)context;
	clc_command_run_data->exit_code = 0;

	pid_t pid;
	int res;
	char **argv = NULL;
	char **envp = NULL;
	int status;
	char path[PATH_MAX];
	char *cmd = 0;
	char *comp_argv = 0;
	char comp_name[SA_MAX_NAME_LENGTH + 24];
	int i;
	int argv_size;
	int envp_size;

	ENTER_VOID();

	pid = fork();

	if (pid == -1) {
		fprintf (stderr, "Couldn't fork process %s\n", strerror (errno));
		return (0);
	}

	if (pid) {
		xprintf ("waiting for pid %d to finish\n", pid);
		waitpid (pid, &status, 0);
		if (WIFEXITED (status) != 0 && WEXITSTATUS(status) != 0) {
			fprintf (stderr, "Error: CLC_CLI (%d) failed with exit status:"
				" %d - %s\n", pid, WEXITSTATUS(status),
				strerror (WEXITSTATUS(status)));
			/*
             * Store the exit code from the script in the return data.
             */
			clc_command_run_data->exit_code = WEXITSTATUS(status);
		}
		if (WIFSIGNALED (status) != 0) {
			fprintf (stderr, "Error: CLC_CLI (%d) failed with exit status:"
				" %d\n", pid, WTERMSIG(status));
			/*                                                              
			 * TODO: remove this and handle properly later...
			 */

			/*                                                              
			 * Healthcheck timout will expire laterfore the component
			 * and this will lead to Intantiation failed for the component.
			 */

		}
		xprintf ("process (%d) finished with %x\n", pid, status);
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
		if (clc_command_run_data->comp->clccli_path != NULL) {
			sprintf (path, "%s/%s",
				clc_command_run_data->comp->clccli_path, cmd);
		} else if (clc_command_run_data->comp->su->clccli_path != NULL) {
			sprintf (path, "%s/%s",
				clc_command_run_data->comp->su->clccli_path, cmd);
		} else if (clc_command_run_data->comp->su->sg->clccli_path != NULL) {
			sprintf (path, "%s/%s",
				clc_command_run_data->comp->su->sg->clccli_path, cmd);
		} else if (clc_command_run_data->comp->su->sg->application->clccli_path != NULL) {
			sprintf (path, "%s/%s",
				clc_command_run_data->comp->su->sg->application->clccli_path, cmd);
		}
		cmd = path;
	}

	argv_size = 2;
	argv = amf_malloc (sizeof (char*) * argv_size);
	argv[0] = cmd;
	{
		/* make a proper argv array */
		i = 1;
		char *ptrptr;
		char *arg = strtok_r(comp_argv, " ", &ptrptr);
		while (arg) {
			argv_size++;
			argv = realloc (argv, sizeof (char*) * argv_size);
			if (argv == NULL) {
				fprintf (stderr, "out-of-memory");  
				exit (-1);
			}
			argv[i] = arg;
			arg = strtok_r(NULL, " ", &ptrptr);
			i++;
		}
	}
	argv[i] = NULL;

	i = snprintf (comp_name, SA_MAX_NAME_LENGTH,
		"SA_AMF_COMPONENT_NAME=safComp=%s,safSu=%s,safSg=%s,safApp=%s",
		clc_command_run_data->comp->name.value,
		clc_command_run_data->comp->su->name.value,
		clc_command_run_data->comp->su->sg->name.value,
		clc_command_run_data->comp->su->sg->application->name.value);
	assert (i <= sizeof (comp_name));

	/* two is for component name and NULL termination */
	envp_size = 2;
	envp = amf_malloc (sizeof (char*) * envp_size);
	envp[0] = comp_name;
	for (i = 1; clc_command_run_data->comp->saAmfCompCmdEnv &&
			clc_command_run_data->comp->saAmfCompCmdEnv[i - 1]; i++) {
		envp_size++;
		envp = realloc (envp, sizeof (char*) * envp_size);
		if (envp == NULL) {
			fprintf (stderr, "out-of-memory");
			exit (-1);
		}
		envp[i] = clc_command_run_data->comp->saAmfCompCmdEnv[i - 1];
	}
	envp[i] = NULL;

	xprintf ("running command '%s' with environment (%d):\n", cmd, envp_size);
	for (i = 0; envp[i] != NULL; i++) {
		xprintf ("   %s\n", envp[i]);
	}
	xprintf (" and argv (%d):\n", argv_size);
	for (i = 0; argv[i] != NULL; i++) {
		xprintf ("   %s\n", argv[i]);
	}

	res = execve (cmd, argv, envp);
	if (res == -1) {
		fprintf (stderr, "Couldn't exec program %s (%s)\n",
			cmd, strerror (errno));
	}
	exit (res);	/* abnormal exit of forked process */

	return (0);
}

static void amf_comp_instantiate_tmo (void *component)
{
	SaNameT compName;
	amf_comp_dn_make (component, &compName);

	amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_COMPONENT_INSTANTIATE_TMO,
		&compName, sizeof (SaNameT));
}

static void amf_comp_cleanup_tmo (void *component)
{
	SaNameT compName;
	amf_comp_dn_make (component, &compName);

	amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_COMPONENT_CLEANUP_TMO,
		&compName, sizeof (SaNameT));
}

static void start_component_instantiate_timer (struct amf_comp *component)
{
	ENTER("%s",component->name.value);
	if (component->instantiate_timeout_handle == 0) {
		poll_timer_add (aisexec_poll_handle, 
			component->saAmfCompInstantiateTimeout,
			component,
			amf_comp_instantiate_tmo,
			&component->instantiate_timeout_handle);
	}
}

static void start_component_cleanup_timer (struct amf_comp *component)
{
	ENTER("%s",component->name.value);
	if (component->cleanup_timeout_handle == 0) {
		poll_timer_add (aisexec_poll_handle, 
			component->saAmfCompCleanupTimeout,
			component,
			amf_comp_cleanup_tmo,
			&component->cleanup_timeout_handle);
	}
}

void stop_component_cleanup_timer (struct amf_comp *component)
{
	ENTER("%s",component->name.value);

	if (component->cleanup_timeout_handle != 0) {
		poll_timer_delete (aisexec_poll_handle, 
			component->cleanup_timeout_handle);
		component->cleanup_timeout_handle  = 0;
	}
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

	clc_command_run_data = amf_malloc (sizeof (struct clc_command_run_data));
	clc_command_run_data->comp = comp;
	clc_command_run_data->type = CLC_COMMAND_RUN_OPERATION_TYPE_INSTANTIATE;
	clc_command_run_data->completion_callback = NULL;
	pthread_attr_init (&thread_attr);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	res = pthread_create (&thread, &thread_attr, clc_command_run,
		(void *)clc_command_run_data);
	if (res != 0) {
		log_printf (LOG_LEVEL_ERROR, "pthread_create failed: %d", res);
	}
	start_component_instantiate_timer (comp);
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

/**
 * Request component to terminate itself
 * @param comp
 * 
 * @return int
 */
static int lib_comp_terminate_request (struct amf_comp *comp)
{
	struct res_lib_amf_componentterminatecallback res_lib;
	struct component_terminate_callback_data *component_terminate_callback_data;

	ENTER("comp %s\n", getSaNameT (&comp->name));

	res_lib.header.id = MESSAGE_RES_AMF_COMPONENTTERMINATECALLBACK;
	res_lib.header.size = sizeof (struct res_lib_amf_componentterminatecallback);
	res_lib.header.error = SA_AIS_OK;

	memcpy (&res_lib.compName, &comp->name, sizeof (SaNameT));

	component_terminate_callback_data =
		amf_malloc (sizeof (struct component_terminate_callback_data));
	component_terminate_callback_data->comp = comp;

	res_lib.invocation =
		invocation_create (
		AMF_RESPONSE_COMPONENTTERMINATECALLBACK,
		component_terminate_callback_data);

	openais_conn_send_response (
		openais_conn_partner_get (comp->conn),
		&res_lib,
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

	req.header.size = sizeof (struct req_exec_amf_clc_cleanup_completed);
	req.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
		MESSAGE_REQ_EXEC_AMF_CLC_CLEANUP_COMPLETED);

	amf_comp_dn_make (clc_command_run_data->comp, &req.compName);
	iovec.iov_base = (char *)&req;
	iovec.iov_len = sizeof (req);

	/*
     * Exit code from the invoked cleanup script.
     */
	req.cleanup_exit_code = clc_command_run_data->exit_code;

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
	clc_command_run_data = amf_malloc (sizeof (struct clc_command_run_data));
	clc_command_run_data->comp = comp;
	clc_command_run_data->type = CLC_COMMAND_RUN_OPERATION_TYPE_CLEANUP;
	clc_command_run_data->completion_callback = mcast_cleanup_completion_event;
	start_component_cleanup_timer (comp);
	pthread_attr_init (&thread_attr);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	res = pthread_create (&thread, &thread_attr, clc_command_run,
		(void *)clc_command_run_data);
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
	comp_presence_state_set (comp, SA_AMF_PRESENCE_TERMINATING);
	operational_state_comp_set (comp, SA_AMF_OPERATIONAL_DISABLED);

	res = clc_interfaces[comp->comptype]->terminate (comp);
	return (0);
}
#endif

char *amf_comp_dn_make (struct amf_comp *comp, SaNameT *name)
{
	int i = snprintf ((char*) name->value, SA_MAX_NAME_LENGTH,
		"safComp=%s,safSu=%s,safSg=%s,safApp=%s",
		comp->name.value, comp->su->name.value,
		comp->su->sg->name.value, comp->su->sg->application->name.value);
	assert (i <= SA_MAX_NAME_LENGTH);
	name->length = i;
	return (char *)name->value;
}

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

		if (key->keyLen == healthcheck->safHealthcheckKey.keyLen && 
			memcmp (key, &healthcheck->safHealthcheckKey,key->keyLen) == 0) {
			ret_healthcheck = healthcheck;
			break;
		}
	}

	return (ret_healthcheck);
}

/**
 * Constructor for component objects. Adds component last in
 * the list owned by the specified SU. Always returns a
 * valid comp object, out-of-memory problems are handled
 * here. Default values are initialized.
 * @param su
 * @param name
 * 
 * @return struct amf_comp*
 */
struct amf_comp *amf_comp_new(struct amf_su *su, char *name)
{
	struct amf_comp *tail = su->comp_head;
	struct amf_comp *comp = amf_calloc (1, sizeof (struct amf_comp));

	while (tail != NULL) {
		if (tail->next == NULL) {
			break;
		}
		tail = tail->next;
	}

	if (tail == NULL) {
		su->comp_head = comp;
	} else {
		tail->next = comp;
	}
	comp->su = su;

	/* setup default values from spec. */
	comp->saAmfCompNumMaxInstantiateWithoutDelay = 2;
	comp->saAmfCompNumMaxAmStartAttempt = 2;
	comp->saAmfCompNumMaxAmStopAttempt = 2;

	comp->saAmfCompOperState = SA_AMF_OPERATIONAL_DISABLED;
	comp->saAmfCompPresenceState = SA_AMF_PRESENCE_UNINSTANTIATED;
	amf_comp_error_suspected_clear (comp);
	setSaNameT (&comp->name, name);
	comp->instantiate_timeout_handle = 0;
	comp->cleanup_timeout_handle = 0;
	return comp;
}

void amf_comp_delete (struct amf_comp *comp)
{
	int i;
	struct amf_healthcheck *healthcheck;

	for (healthcheck = comp->healthcheck_head; healthcheck != NULL;) {
		struct amf_healthcheck *tmp = healthcheck;
		healthcheck = healthcheck->next;
		free (tmp);
	}

	for (i = 0; comp->saAmfCompCsTypes[i] != NULL; i++) {
		free (comp->saAmfCompCsTypes[i]);
	}
	for (i = 0; comp->saAmfCompCmdEnv[i] != NULL; i++) {
		free (comp->saAmfCompCmdEnv[i]);
	}

	free (comp->saAmfCompInstantiateCmd);
	free (comp->saAmfCompInstantiateCmdArgv);
	free (comp->saAmfCompTerminateCmd);
	free (comp->saAmfCompTerminateCmdArgv);
	free (comp->saAmfCompCleanupCmd);
	free (comp->saAmfCompCleanupCmdArgv);
	free (comp->saAmfCompAmStartCmd);
	free (comp->saAmfCompAmStartCmdArgv);
	free (comp->saAmfCompAmStopCmd);
	free (comp->saAmfCompAmStopCmdArgv);
	free (comp->clccli_path);

	free (comp);
}

struct amf_comp *amf_comp_find_from_conn_info (void *conn)
{
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;
	struct amf_comp *comp;

	for (app = amf_cluster->application_head; app != NULL; app = app->next) {
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			for (su = sg->su_head; su != NULL; su = su->next) {
				for (comp = su->comp_head; comp != NULL; comp = comp->next) {
					if (comp->conn == conn) {
						goto end;
					}
				}
			}
		}
	}
end:
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

	assert (cluster != NULL && name != NULL);

	/* malloc new buffer since strtok_r writes to its first argument */
	buf = amf_malloc (name->length + 1);
	memcpy (buf, name->value,name ->length + 1);

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

	app = amf_application_find (cluster, app_name);
	if (app == NULL) {
		goto end;
	}

	sg = amf_sg_find (app, sg_name);
	if (sg == NULL) {
		goto end;
	}

	for (su = sg->su_head; su != NULL; su = su->next) {
		if (strncmp (su_name, (char*)su->name.value, su->name.length) == 0) {
			for (comp = su->comp_head; comp != NULL; comp = comp->next) {
				if (comp->name.length == strlen(comp_name) && 
					strncmp (comp_name, (char*)comp->name.value,
					comp->name.length) == 0) {
					goto end;
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

static void comp_ha_state_set ( struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment,
	SaAmfHAStateT ha_state)
{
	/* set confirmed HA state */
	csi_assignment->saAmfCSICompHAState = ha_state;
	TRACE1 ("Setting comp '%s', SU '%s' CSI '%s', HA state: %s\n",
		comp->name.value, comp->su->name.value,
		csi_assignment->csi->name.value,
		amf_ha_state (csi_assignment->saAmfCSICompHAState));
	amf_si_comp_set_ha_state_done (csi_assignment->csi->si, csi_assignment);
}

static void comp_presence_state_set (struct amf_comp *comp,
	SaAmfPresenceStateT presence_state)
{
	comp->saAmfCompPresenceState = presence_state;
	TRACE1 ("Setting comp '%s', SU '%s' presence state: %s\n",
		comp->name.value, comp->su->name.value,
		amf_presence_state (comp->saAmfCompPresenceState));

	amf_su_comp_state_changed (
		comp->su, comp, SA_AMF_PRESENCE_STATE, presence_state);
}

struct amf_csi_assignment *amf_comp_get_next_csi_assignment (
	struct amf_comp *component,
	const struct amf_csi_assignment *csi_assignment) 
{
	struct amf_si *si;
	struct amf_csi *csi;
	struct amf_csi_assignment *tmp_csi_assignment;
	SaNameT dn;

	amf_comp_dn_make (component, &dn);

	if (csi_assignment == NULL) {
		si = component->su->sg->application->si_head;
		csi = si->csi_head;
		tmp_csi_assignment = csi->assigned_csis;
	} else {
		tmp_csi_assignment = csi_assignment->next;
		if (tmp_csi_assignment == NULL) {
			csi = csi_assignment->csi->next;
			if (csi == NULL) {
				si = csi_assignment->csi->si->next;
				if (si == NULL) {
					return NULL;
				} else {
					csi = si->csi_head;
					tmp_csi_assignment = csi->assigned_csis;
				}
			} else {
				si = csi->si;
				tmp_csi_assignment = csi->assigned_csis;
			}
		} else {
			csi = tmp_csi_assignment->csi;
			si = csi->si;
		}
	}

	for (; si != NULL; si = si->next) {
		if (tmp_csi_assignment == NULL && csi == NULL && si != NULL) {
			csi = si->csi_head;
			tmp_csi_assignment = csi->assigned_csis;
		}

		for (; csi != NULL; csi = csi->next) {
			if (tmp_csi_assignment == NULL && csi != NULL) {
				tmp_csi_assignment = csi->assigned_csis;
			}

			for (; tmp_csi_assignment != NULL;
				tmp_csi_assignment = tmp_csi_assignment->next) {

				if (name_match (&tmp_csi_assignment->name, &dn)) {
					return tmp_csi_assignment;
				}
			}
		}
	}

	return NULL;
}

void amf_comp_foreach_csi_assignment (
	struct amf_comp *component,
	void (*foreach_fn) (struct amf_comp *component,
		struct amf_csi_assignment *csi_assignment))
{
	struct amf_csi_assignment *csi_assignment;

	assert (foreach_fn != NULL);
	csi_assignment = amf_comp_get_next_csi_assignment (component, NULL);
	while (csi_assignment != NULL) {
		foreach_fn (component, csi_assignment);
		csi_assignment = amf_comp_get_next_csi_assignment (
			component, csi_assignment);
	}
}

static struct amf_csi_assignment *csi_assignment_find_in (
	struct amf_comp *component, SaNameT *csi_name) 
{
	struct amf_csi_assignment *csi_assignment;
	SaNameT dn;

	csi_assignment = amf_comp_get_next_csi_assignment (component, NULL);
	while (csi_assignment != NULL) {
		amf_csi_dn_make (csi_assignment->csi, &dn);
		if (name_match (csi_name, &dn)) {
			return csi_assignment;
		}
		csi_assignment = amf_comp_get_next_csi_assignment (
			component, csi_assignment);
	}

	return NULL;
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
	if (healthcheck->active == 0) {
		log_printf (LOG_ERR, "Healthcheck timeout: ignored key = %s, "
							 "due to wrong state = %d, comp = %s",
			healthcheck->safHealthcheckKey.key, 
			healthcheck->comp->saAmfCompPresenceState, 
			healthcheck->comp->name.value);
		goto out;
	}
	req_exec.header.size = sizeof (struct req_exec_amf_healthcheck_tmo);
	req_exec.header.id = SERVICE_ID_MAKE (AMF_SERVICE,
		MESSAGE_REQ_EXEC_AMF_HEALTHCHECK_TMO);

	amf_comp_dn_make (healthcheck->comp, &req_exec.compName);
	memcpy (&req_exec.safHealthcheckKey,
		&healthcheck->safHealthcheckKey, sizeof (SaAmfHealthcheckKeyT));
	req_exec.recommendedRecovery = healthcheck->recommendedRecovery;
	iovec.iov_base = (char *)&req_exec;
	iovec.iov_len = sizeof (req_exec);

	assert (totempg_groups_mcast_joined (openais_group_handle,
		&iovec, 1, TOTEMPG_AGREED) == 0);
out:
	return;
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
	struct res_lib_amf_healthcheckcallback res_lib;

	res_lib.header.id = MESSAGE_RES_AMF_HEALTHCHECKCALLBACK;
	res_lib.header.size = sizeof (struct res_lib_amf_healthcheckcallback);
	res_lib.header.error = SA_AIS_OK;
	res_lib.invocation =
		invocation_create (AMF_RESPONSE_HEALTHCHECKCALLBACK, healthcheck);

	amf_comp_dn_make (healthcheck->comp, &res_lib.compName);
	memcpy (&res_lib.key, &healthcheck->safHealthcheckKey,
		sizeof (SaAmfHealthcheckKeyT));

	TRACE7 ("sending healthcheck request to component %s",
		res_lib.compName.value);
	openais_conn_send_response (
		openais_conn_partner_get (healthcheck->comp->conn),
		&res_lib, sizeof (struct res_lib_amf_healthcheckcallback));
}

static void lib_csi_set_request (
	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment)
{
	struct res_lib_amf_csisetcallback* res_lib;     
	void*  p;
	struct amf_csi_attribute *attribute;
	size_t char_length_of_csi_attrs=0;
	size_t num_of_csi_attrs=0;
	int i;
	struct amf_csi *csi;
	char* csi_attribute_buf;
	unsigned int byte_offset;

	if (!amf_su_is_local (comp->su))
		return;

	csi = csi_assignment->csi;

	ENTER ("Assigning CSI '%s' state %s to comp '%s'\n",
		getSaNameT (&csi->name),
		amf_ha_state (csi_assignment->requested_ha_state),
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
	p = amf_malloc(sizeof(struct res_lib_amf_csisetcallback) +
		char_length_of_csi_attrs);
	res_lib = (struct res_lib_amf_csisetcallback*)p;

	/* Address of the buffer containing the Csi name value pair  */
	csi_attribute_buf = res_lib->csi_attr_buf;

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

	res_lib->number = num_of_csi_attrs;
	res_lib->csiFlags = SA_AMF_CSI_ADD_ONE;  

	switch (csi_assignment->requested_ha_state) {
		case SA_AMF_HA_ACTIVE: {
			res_lib->csiStateDescriptor.activeDescriptor.activeCompName.length = 0;
			res_lib->csiStateDescriptor.activeDescriptor.transitionDescriptor =
					SA_AMF_CSI_NEW_ASSIGN; 
			break;
		}
		case SA_AMF_HA_STANDBY: {
			res_lib->csiStateDescriptor.standbyDescriptor.activeCompName.length = 0; 
			res_lib->csiStateDescriptor.standbyDescriptor.standbyRank =  1;
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

	res_lib->header.id = MESSAGE_RES_AMF_CSISETCALLBACK;
	res_lib->header.size = 
		sizeof (struct res_lib_amf_csisetcallback) +
		char_length_of_csi_attrs;
	res_lib->header.error = SA_AIS_OK;

	amf_comp_dn_make (comp, &res_lib->compName);
	amf_csi_dn_make (csi, &res_lib->csiName);

	res_lib->haState = csi_assignment->requested_ha_state;
	res_lib->invocation =
		invocation_create (AMF_RESPONSE_CSISETCALLBACK, csi_assignment);
	openais_conn_send_response (
		openais_conn_partner_get (comp->conn), res_lib, res_lib->header.size);
	
	free(p);
}

static void stop_component_instantiate_timer (struct amf_comp *component)
{
	ENTER("%s",component->name.value);

	if (component->instantiate_timeout_handle) {
		dprintf ("Stop component instantiate timer");
		poll_timer_delete (aisexec_poll_handle, 
			component->instantiate_timeout_handle);
		component->instantiate_timeout_handle = 0;
	}
}

SaAisErrorT amf_comp_register (struct amf_comp *comp)
{
	TRACE2("Exec comp register '%s'", comp->name.value);
	stop_component_instantiate_timer (comp);

	switch (comp->saAmfCompPresenceState) {
		case SA_AMF_PRESENCE_RESTARTING:
			comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATED);
			break;
		case SA_AMF_PRESENCE_INSTANTIATING:
			amf_comp_operational_state_set (comp, SA_AMF_OPERATIONAL_ENABLED);
			comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATED);
			break;
		case SA_AMF_PRESENCE_INSTANTIATION_FAILED:
            /* ignore due to instantitate timeout a while ago  */
			break;
		default:
			log_printf(LOG_LEVEL_ERROR,"comp->saAmfCompPresenceState = %d",
				comp->saAmfCompPresenceState);
			assert (0);
			break;
		
	}
	
	return SA_AIS_OK;
}

void amf_comp_error_report (struct amf_comp *comp, amf_comp_t* reporting_comp, 
	SaAmfRecommendedRecoveryT recommendedRecovery)
{
	struct res_lib_amf_componenterrorreport res_lib;
	TRACE2("Exec comp error report on comp'%s' from %s", comp->name.value, 
		reporting_comp->name.value );
	 
	if (amf_su_is_local (reporting_comp->su)) {
		res_lib.header.size = sizeof (struct res_lib_amf_componenterrorreport);
		res_lib.header.id = MESSAGE_RES_AMF_COMPONENTERRORREPORT;
		res_lib.header.error = SA_AIS_OK;
		openais_conn_send_response (reporting_comp->conn, &res_lib, sizeof (res_lib));
	}

    /* Report to SU and let it handle the problem */
	report_error_suspected (comp, recommendedRecovery);
}

/**
 * Healthcheck timeout event handler
 * @param comp
 * @param healthcheck
 */
void amf_comp_healthcheck_tmo (
	struct amf_comp *comp, SaAmfRecommendedRecoveryT recommendedRecovery)
{
	TRACE2("Exec healthcheck tmo for '%s'", comp->name.value);

	/* report to SU and let it handle the problem */
	report_error_suspected (comp, recommendedRecovery);
}

static void clear_ha_state (
	struct amf_comp *comp, struct amf_csi_assignment *csi_assignment)
{
	ENTER ("");
	csi_assignment->saAmfCSICompHAState = 0;
}


static void comp_recover_action (amf_comp_t *comp, 
	SaAmfRecommendedRecoveryT recommendedRecovery)
{


	ENTER ("%s %d %d", comp->name.value,recommendedRecovery, 
		comp->saAmfCompRecoveryOnError);

	amf_node_t *node = amf_node_find (&comp->su->saAmfSUHostedByNode);
	switch (recommendedRecovery) {
		case SA_AMF_NO_RECOMMENDATION: {
			/*
             * If the recommendation was SA_AMF_NO_RECOMMENDATION,
             * then use the configured recovery action for the component
             */
			switch (comp->saAmfCompRecoveryOnError) {
				case SA_AMF_NO_RECOMMENDATION:
					if (comp->saAmfCompDisableRestart) {
                        /* Comp or SU failover */
						amf_node_comp_failover_req (node, comp);
					} else {
						/* Component restart */
						amf_su_comp_error_suspected (comp->su, comp, 
							recommendedRecovery);
					}
				case SA_AMF_COMPONENT_RESTART:
					if (comp->saAmfCompDisableRestart) {
                        /* Comp or SU failover */
						amf_node_comp_failover_req (node, comp);
					} else {
						/* Component restart */
						amf_su_comp_error_suspected (comp->su, comp, 
							recommendedRecovery);
					}
					break;
				case SA_AMF_COMPONENT_FAILOVER:
                    /* SU failover */
					amf_node_comp_failover_req (node, comp);
					break;
				case SA_AMF_NODE_SWITCHOVER:
					break;
				case SA_AMF_NODE_FAILOVER: { 
                    /* Node failover */
					amf_node_t *node = amf_node_find (
						&comp->su->saAmfSUHostedByNode);
					amf_node_failover(node);
				}
				break;
				case SA_AMF_NODE_FAILFAST:
					break;
				case SA_AMF_CLUSTER_RESET:
					break;
				case SA_AMF_APPLICATION_RESTART:
				default:
					dprintf("recommendedRecovery=%d",recommendedRecovery);
					assert (0);
					break;
			}
			break;
		}
		case SA_AMF_COMPONENT_RESTART:
			if (comp->saAmfCompDisableRestart == SA_TRUE) {
				amf_node_comp_failover_req (node, comp);
			} else {
				amf_su_comp_error_suspected (comp->su, comp, recommendedRecovery);
			}
			break;
		case SA_AMF_COMPONENT_FAILOVER:
			amf_node_comp_failover_req (node, comp);
			break;
		case SA_AMF_NODE_SWITCHOVER:
			break;
		case SA_AMF_NODE_FAILOVER:
            /* Node failover */
			amf_node_failover (amf_node_find (&comp->su->saAmfSUHostedByNode));
			break;
		case SA_AMF_NODE_FAILFAST:
			break;
		case SA_AMF_CLUSTER_RESET:
			break;
		case SA_AMF_APPLICATION_RESTART:
		default:
			assert (0);
			break;

	}
}

/**
 * Event method to be called when a cleanup completed event is received
 * with failure.
 * @param comp
 */
void amf_comp_cleanup_failed_completed (amf_comp_t *comp)
{
	ENTER ("'%s'", comp->name.value);

	stop_component_cleanup_timer (comp);
	amf_comp_error_suspected_clear (comp);
	amf_comp_operational_state_set (comp, SA_AMF_OPERATIONAL_DISABLED);
	comp_presence_state_set (comp, SA_AMF_PRESENCE_TERMINATION_FAILED);
}

/**
 * Event method to be called when a cleanup completed event is received
 * @param comp
 */
void amf_comp_cleanup_completed (struct amf_comp *comp)
{
	TRACE2("Exec CLC cleanup completed for '%s'", comp->name.value);
	stop_component_cleanup_timer (comp);

	/* Set all CSI's confirmed HA state to unknown  */
	amf_comp_foreach_csi_assignment (comp, clear_ha_state);

	amf_comp_error_suspected_clear (comp);
	
	if (comp->saAmfCompPresenceState == SA_AMF_PRESENCE_RESTARTING) {
		amf_comp_instantiate (comp);
	} else {
		comp_presence_state_set (comp, SA_AMF_PRESENCE_UNINSTANTIATED);
	}
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
	
	if (is_not_instantiating_or_instantiated_or_restarting (comp)) {
		log_printf (LOG_ERR, "Healthcheckstart: ignored key = %s, "
							 "due to wrong state = %d, comp = %s",
			healthcheckKey->key, comp->saAmfCompPresenceState, comp->name.value);
		error = SA_AIS_OK;
		goto error_exit;	
	}
		

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

	dprintf ("Healthcheckstop: '%s'", comp->name.value);
 
	if (!amf_su_is_local (comp->su)) {
		return SA_AIS_OK;
	}

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
	ENTER ("'%s' SU '%s'", getSaNameT (&comp->name),
		getSaNameT (&comp->su->name));

	switch (comp->saAmfCompPresenceState) {
		case SA_AMF_PRESENCE_RESTARTING:
			/* fall through */
		case SA_AMF_PRESENCE_UNINSTANTIATED:
			if (amf_su_is_local (comp->su)) {
				TRACE1("Send instantiate event for comp '%s' from host %s", 
					comp->name.value, comp->su->saAmfSUHostedByNode.value);
				SaNameT compName;
				amf_comp_dn_make (comp, &compName);
				amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_COMPONENT_INSTANTIATE,
					&compName, sizeof (SaNameT));
			}
			break;
		default:
			dprintf("Instantiate ignored in Component presence state %d", 
				comp->saAmfCompPresenceState);
			break;
	}
}

void amf_comp_cleanup_tmo_event (struct amf_comp *comp)
{
	ENTER ("Comp cleanup timeout after %d ms '%s' '%s'", 
		comp->saAmfCompCleanupTimeout, comp->su->name.value,
		comp->name.value);
	amf_comp_error_suspected_clear(comp);	
	amf_comp_operational_state_set (comp, SA_AMF_OPERATIONAL_DISABLED);
	comp_presence_state_set (comp, SA_AMF_PRESENCE_TERMINATION_FAILED);
}

void amf_comp_instantiate_tmo_event (struct amf_comp *comp)
{
	ENTER ("Comp instantiate timeout after %d ms '%s' '%s'", 
		comp->saAmfCompInstantiateTimeout, comp->su->name.value,
		comp->name.value);

	switch (comp->saAmfCompPresenceState) {
		case SA_AMF_PRESENCE_RESTARTING:
			amf_comp_operational_state_set (comp, SA_AMF_OPERATIONAL_DISABLED);
			comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATION_FAILED);

			break;
		case SA_AMF_PRESENCE_INSTANTIATING:

			amf_comp_operational_state_set (comp, SA_AMF_OPERATIONAL_DISABLED);
			comp_presence_state_set (comp, SA_AMF_PRESENCE_INSTANTIATION_FAILED);

			break;
		case SA_AMF_PRESENCE_INSTANTIATED:
			assert (comp->instantiate_timeout_handle == 0);
			break;
		default:
			dprintf("Presence state = %d", comp->saAmfCompPresenceState);
			assert (0);
			break;
	}
}

void amf_comp_instantiate_event (struct amf_comp *component)
{
   int res;
   ENTER ("");
	switch (component->saAmfCompPresenceState) {
		case SA_AMF_PRESENCE_INSTANTIATING:
		case SA_AMF_PRESENCE_INSTANTIATED:
		case SA_AMF_PRESENCE_TERMINATING:
		case SA_AMF_PRESENCE_INSTANTIATION_FAILED:
		case SA_AMF_PRESENCE_TERMINATION_FAILED:
			dprintf("Instantiate ignored in Component presence state %d", 
				component->saAmfCompPresenceState);
			break;
		case SA_AMF_PRESENCE_UNINSTANTIATED:

			comp_presence_state_set (component, SA_AMF_PRESENCE_INSTANTIATING);
			amf_su_comp_state_changed(component->su, 
				component,SA_AMF_PRESENCE_STATE,SA_AMF_PRESENCE_INSTANTIATING);
			if (amf_su_is_local (component->su)) {
				res = clc_interfaces[component->comptype]->instantiate (
					component);
			}

			break;
		case SA_AMF_PRESENCE_RESTARTING:
			if (amf_su_is_local (component->su)) {
				res = clc_interfaces[component->comptype]->instantiate (
					component);
			}
			break;
		default:
			dprintf("Component presence state %d", 
				component->saAmfCompPresenceState);
			assert (0);
			break;
	}
}

void amf_comp_readiness_state_set (struct amf_comp *comp,
	SaAmfReadinessStateT state)
{
	TRACE1 ("Setting comp '%s' readiness state: %s\n",
		comp->name.value, amf_readiness_state (state));
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
	SaInvocationT invocation, SaAisErrorT error, SaAisErrorT *retval,
	SaUint32T *interface, SaNameT *dn, SaAmfHealthcheckKeyT *healtcheck_key,
	SaAmfRecommendedRecoveryT *recommendedRecovery)
{
	int res;
	void *data;

	res = invocation_get_and_destroy (invocation, interface, &data);

	if (res == -1) {
		log_printf (LOG_ERR, "Lib response: invocation not found\n");
		*retval = SA_AIS_ERR_INVALID_PARAM;
		return 0;
	}

	switch (*interface) {
		case AMF_RESPONSE_HEALTHCHECKCALLBACK: {
				struct amf_healthcheck *healthcheck = data;

				amf_comp_dn_make (healthcheck->comp, dn);
				TRACE7 ("Healthcheck response from '%s': %d",dn->value, error);
				/*
                 * Healthcheck with erroneous response
                 * and no recovery action is in progress.
				 */
				memcpy(healtcheck_key, &healthcheck->safHealthcheckKey,
					sizeof (SaAmfHealthcheckKeyT));
				*recommendedRecovery = healthcheck->recommendedRecovery;

				if (error != SA_AIS_OK && 
					!amf_comp_is_error_suspected (healthcheck->comp)) {
					return 1; 
				}
				if (is_not_instantiating_or_instantiated_or_restarting(
					healthcheck->comp)) {
					log_printf (LOG_ERR, "HealthcheckResponse: ignored for key = %s, "
										 "due to wrong state = %d comp = %s",
						healthcheck->safHealthcheckKey.key, 
						healthcheck->comp->saAmfCompPresenceState,
						healthcheck->comp->name.value);
					*retval = SA_AIS_OK;
					return 0;  /* do not multicast event */
				}

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
			amf_csi_assignment_dn_make (data, dn);
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

	/* XXX we fall here in case NDEBUG is set */
	*retval = -1;
	return 0;
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
struct amf_comp *amf_comp_response_2 (SaUint32T interface, SaNameT *dn, 
	SaAmfHealthcheckKeyT *healthcheck_key, SaAisErrorT error, 
	SaAisErrorT *retval, SaAmfRecommendedRecoveryT recommendedRecovery)
{
	struct amf_csi_assignment *csi_assignment;
	struct amf_comp *comp = NULL;

	assert (retval != NULL);

	*retval = SA_AIS_OK;

	switch (interface) {
		case AMF_RESPONSE_CSISETCALLBACK: {
			ENTER("'%s'", dn->value);
				csi_assignment = amf_csi_assignment_find (amf_cluster, dn);
				assert (csi_assignment != NULL);
				comp = csi_assignment->comp;
				dprintf ("CSI '%s' set callback response from '%s', error: %d",
					csi_assignment->csi->name.value,
					csi_assignment->comp->name.value, error);
				comp = csi_assignment->comp;
				if (error == SA_AIS_OK) {
					comp_ha_state_set (
						comp, csi_assignment, csi_assignment->requested_ha_state);
				} else if (error == SA_AIS_ERR_FAILED_OPERATION) {
					amf_si_comp_set_ha_state_failed (csi_assignment->csi->si,
						csi_assignment);
				} else {
					*retval = SA_AIS_ERR_INVALID_PARAM;
				}
				break;
			}
		case AMF_RESPONSE_CSIREMOVECALLBACK: {
			ENTER("'%s'", dn->value);
				csi_assignment = amf_csi_assignment_find (amf_cluster, dn);
				assert (csi_assignment != NULL);
				dprintf ("Lib csi '%s' remove callback response from '%s', error: %d",
					csi_assignment->csi->name.value,
					csi_assignment->comp->name.value, error);
				comp = csi_assignment->comp;
				if (error == SA_AIS_OK || error == SA_AIS_ERR_FAILED_OPERATION) {
					amf_si_comp_csi_removed (csi_assignment->csi->si,
						csi_assignment, error);
				} else {
					*retval = SA_AIS_ERR_INVALID_PARAM;
				}
				break;
			}
		case AMF_RESPONSE_HEALTHCHECKCALLBACK: {
			dprintf("AMF_RESPONSE_HEALTHCHECKCALLBACK for %s", dn->value);
			comp = amf_comp_find (amf_cluster, dn);
			
			assert (comp);
			amf_healthcheck_t *healthcheck = amf_comp_find_healthcheck (
				comp, healthcheck_key);
			assert (comp);
			healthcheck->recommendedRecovery = recommendedRecovery; 
			comp_recover_action (comp, healthcheck->recommendedRecovery);
				
			break;
		}

#if 0
		case AMF_RESPONSE_COMPONENTTERMINATECALLBACK: {
				struct component_terminate_callback_data *callback_data = data;
				dprintf ("Lib comp '%s' terminate callback response, error: %d",
					callback_data->comp->name.value, error);
				comp_presence_state_set (callback_data->comp,
					SA_AMF_PRESENCE_UNINSTANTIATED);
				break;
			}
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
	struct amf_comp *component,
	struct amf_csi_assignment *csi_assignment)
{
	ENTER ("'%s'", csi_assignment->csi->name.value);
	
	assert (component != NULL && csi_assignment != NULL);


	if (!amf_comp_is_error_suspected (component)) {
		lib_csi_set_request(component, csi_assignment);
	} else {
		if (csi_assignment->requested_ha_state == SA_AMF_HA_QUIESCED) {
			csi_assignment->saAmfCSICompHAState = csi_assignment->requested_ha_state;
		} else {
			dprintf ("csi_assignment->requested_ha_state = %d", 
				component->error_suspected);
			assert (0);
		}
	}

	LEAVE("");
}

/**
 * Request termination of a component
 * @param comp
 */
void amf_comp_terminate (struct amf_comp *comp)
{
	dprintf ("comp terminate '%s'\n", getSaNameT (&comp->name));
	comp_presence_state_set (comp, SA_AMF_PRESENCE_TERMINATING);

	if (amf_su_is_local (comp->su)) {
		amf_comp_healthcheck_stop (comp, NULL);
		if (amf_comp_is_error_suspected(comp)) {
			clc_interfaces[comp->comptype]->cleanup (comp);
		} else {
			clc_interfaces[comp->comptype]->terminate (comp);
		}
	}
}

/**
 * Request restart of a component
 * @param comp
 */
void amf_comp_restart (struct amf_comp *comp)
{
	dprintf ("comp restart '%s'\n", getSaNameT (&comp->name));
	comp_presence_state_set (comp, SA_AMF_PRESENCE_RESTARTING);
	comp->saAmfCompRestartCount += 1;

	if (amf_su_is_local (comp->su)) {
		amf_comp_healthcheck_stop (comp, NULL);
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

	assert (comp != NULL && csi_name != NULL && ha_state != NULL);

	dprintf ("comp ha state get from comp '%s' CSI '%s'\n",
		getSaNameT (&comp->name), csi_name->value);

	assignment = csi_assignment_find_in (comp, csi_name);
	if (assignment != NULL) {
		*ha_state = assignment->saAmfCSICompHAState;
		return SA_AIS_OK;
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

	healthcheck = amf_comp_find_healthcheck (comp, healthcheckKey);
	if (is_not_instantiating_or_instantiated_or_restarting(comp)) {
		log_printf (LOG_ERR, "HealthcheckConfirm: ignored for key = %s, "
							 "due to wrong state = %d, comp = %s",
			healthcheckKey->key, comp->saAmfCompPresenceState, comp->name.value);
		error = SA_AIS_OK;
		goto out;
	}
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
			if (!comp->error_suspected) {
				poll_timer_delete (aisexec_poll_handle,
				healthcheck->timer_handle_period);
				mcast_healthcheck_tmo_event (healthcheck);
			}
		} else {
			error = SA_AIS_ERR_INVALID_PARAM;
		}
	} else {
		error = SA_AIS_ERR_INVALID_PARAM;
	}
out:
	return error;
}

void amf_comp_init (void)
{
	log_init ("AMF");
}

void amf_comp_operational_state_set (struct amf_comp *comp,
	SaAmfOperationalStateT oper_state)
{
	comp->saAmfCompOperState = oper_state;
	TRACE1 ("Setting comp '%s', SU '%s' operational state: %s\n",
		comp->name.value, comp->su->name.value,
		amf_op_state (comp->saAmfCompOperState));
	amf_su_comp_state_changed (
		comp->su, comp, SA_AMF_OP_STATE, oper_state);
}

int amf_comp_get_saAmfCompNumCurrActiveCsi(struct amf_comp *component)
{
	int cnt = 0;
	struct amf_csi_assignment *csi_assignment;

	csi_assignment = amf_comp_get_next_csi_assignment (component, NULL);
	while (csi_assignment != NULL) {
		if (csi_assignment->saAmfCSICompHAState == SA_AMF_HA_ACTIVE) {
			cnt++;
		}
		csi_assignment = amf_comp_get_next_csi_assignment (
			component, csi_assignment);
	}

	return cnt;
}

int amf_comp_get_saAmfCompNumCurrStandbyCsi(struct amf_comp *component)
{
	int cnt = 0;
	struct amf_csi_assignment *csi_assignment;

	csi_assignment = amf_comp_get_next_csi_assignment (component, NULL);
	while (csi_assignment != NULL) {
		if (csi_assignment->saAmfCSICompHAState == SA_AMF_HA_STANDBY) {
			cnt++;
		}
		csi_assignment = amf_comp_get_next_csi_assignment (
			component, csi_assignment);
	}

	return cnt;
}

SaAmfReadinessStateT amf_comp_get_saAmfCompReadinessState (
	struct amf_comp *component)
{
	if (component->saAmfCompOperState == SA_AMF_OPERATIONAL_ENABLED) {
		return amf_su_get_saAmfSUReadinessState (component->su);

	} else if (component->saAmfCompOperState == SA_AMF_OPERATIONAL_DISABLED) {
		return SA_AMF_READINESS_OUT_OF_SERVICE;
	}

	assert (0);
	/* XXX we fall here in case NDEBUG is set */
	return -1;
}
/**
 * Component is informed that the node where the 'real'
 * component process is executing has unexpectadly left the
 * node. If there is a pending interaction between AMF
 * (component) and the 'real' component process, then component
 * will indicate to its subordinate objects the interaction
 * failed. Pending presence state changes is indicated by
 * reporting the new state is uninstantiated while pending csi
 * operations are indicated by 'operation failed'.
 * @param comp
 * 
 * @return void
 */
void amf_comp_node_left (struct amf_comp *component)
{
	int change_pending = 0;
	struct amf_csi_assignment *csi_assignment;

	ENTER("saAmfCompPresenceState = %d", component->saAmfCompPresenceState);
	amf_comp_error_suspected_clear (component);
	if (component->saAmfCompPresenceState == SA_AMF_PRESENCE_INSTANTIATING ||
		component->saAmfCompPresenceState == SA_AMF_PRESENCE_RESTARTING ||
		component->saAmfCompPresenceState == SA_AMF_PRESENCE_TERMINATING) {
		change_pending = 1;

	}

	component->saAmfCompPresenceState = SA_AMF_PRESENCE_UNINSTANTIATED;

	if (amf_su_are_all_comps_in_su (component->su,
		SA_AMF_PRESENCE_UNINSTANTIATED)) {
		component->su->saAmfSUPresenceState = SA_AMF_PRESENCE_UNINSTANTIATED;
	}

	if (change_pending) {
		change_pending = 0;
		amf_su_comp_state_changed ( component->su,
			component,
			SA_AMF_PRESENCE_STATE,
			SA_AMF_PRESENCE_UNINSTANTIATED);
	}

	if (component->saAmfCompOperState == SA_AMF_OPERATIONAL_ENABLED) {
		change_pending = 1;
	}

	component->saAmfCompOperState = SA_AMF_OPERATIONAL_DISABLED;
	if (change_pending) {
		change_pending =0;
		amf_su_comp_state_changed (component->su,
			component,
			SA_AMF_OP_STATE,
			SA_AMF_OPERATIONAL_DISABLED);
	}

	csi_assignment = amf_comp_get_next_csi_assignment (component, NULL);
	while (csi_assignment != NULL) {
		if (csi_assignment->requested_ha_state != 
			csi_assignment->saAmfCSICompHAState) {
			amf_si_comp_set_ha_state_failed (
				csi_assignment->csi->si,csi_assignment);
		}
		csi_assignment = amf_comp_get_next_csi_assignment (
			component, csi_assignment);
	}
}

/**
 * Serialize a component including variable length arrays and
 * strings to a buffer returned. Buffer is to be freed by
 * caller.
 * @param component
 * @param len
 * 
 * @return void*
 */
void *amf_comp_serialize (struct amf_comp *component, int *len)
{
	char *buf = NULL;
	int i, offset = 0, size = 0;

	TRACE8 ("%s", component->name.value);

	buf = amf_serialize_SaNameT (buf, &size, &offset, &component->name);

	/* count cstypes and write to buf */
	for (i = 0; component->saAmfCompCsTypes &&
		component->saAmfCompCsTypes[i] != NULL; i++);
	buf = amf_serialize_SaUint32T (buf, &size, &offset, i);

	for (i = 0; component->saAmfCompCsTypes &&
		component->saAmfCompCsTypes[i] != NULL; i++) {
		buf = amf_serialize_SaNameT (
			buf, &size, &offset, component->saAmfCompCsTypes[i]);
	}

	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompCategory);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompCapability);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompNumMaxActiveCsi);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompNumMaxStandbyCsi);

	/* count environment vars and write to buf */
	for (i = 0; component->saAmfCompCmdEnv &&
		  component->saAmfCompCmdEnv[i] != NULL; i++);
	buf = amf_serialize_SaUint32T (buf, &size, &offset, i);

	for (i = 0; component->saAmfCompCmdEnv &&
		  component->saAmfCompCmdEnv[i] != NULL; i++) {
		buf = amf_serialize_SaStringT (
			buf, &size, &offset, component->saAmfCompCmdEnv[i]);
	}

	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompDefaultClcCliTimeout);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompDefaultCallbackTimeOut);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompInstantiateCmd);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompInstantiateCmdArgv);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompInstantiateTimeout);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompInstantiationLevel);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompNumMaxInstantiateWithoutDelay);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompNumMaxInstantiateWithDelay);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompDelayBetweenInstantiateAttempts);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompTerminateCmd);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompTerminateTimeout);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompTerminateCmdArgv);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompCleanupCmd);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompCleanupTimeout);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompCleanupCmdArgv);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompAmStartCmd);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompAmStartTimeout);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompAmStartCmdArgv);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompNumMaxAmStartAttempt);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompAmStopCmd);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompAmStopTimeout);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->saAmfCompAmStopCmdArgv);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompNumMaxAmStopAttempt);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompTerminateCallbackTimeout);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompCSISetCallbackTimeout);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompQuiescingCompleteTimeout);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompCSIRmvCallbackTimeout);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompRecoveryOnError);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompDisableRestart);
	buf = amf_serialize_SaNameT (
		buf, &size, &offset, &component->saAmfCompProxyCsi);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompOperState);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompPresenceState);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->saAmfCompRestartCount);
	buf = amf_serialize_SaNameT (
		buf, &size, &offset, &component->saAmfCompCurrProxyName);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, component->clccli_path);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->comptype);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, component->error_suspected);

	*len = offset;

	return buf;
}

/**
 * Deserialize a buffer into a AMF component object.
 * @param su
 * @param buf
 * @param size
 * 
 * @return struct amf_comp*
 */
struct amf_comp *amf_comp_deserialize (struct amf_su *su, char *buf)
{
	char *tmp = buf;
	int i;
	SaUint32T cnt;
	struct amf_comp *component = amf_comp_new (su, "");

	tmp = amf_deserialize_SaNameT (tmp, &component->name);
	tmp = amf_deserialize_SaUint32T (tmp, &cnt);
	component->saAmfCompCsTypes = amf_malloc ((cnt + 1) * sizeof (SaNameT*));
	for (i = 0; i < cnt; i++) {
		component->saAmfCompCsTypes[i] = amf_malloc (sizeof (SaNameT));
		tmp = amf_deserialize_SaNameT (tmp, component->saAmfCompCsTypes[i]);
	}
	component->saAmfCompCsTypes[i] = NULL;

	tmp = amf_deserialize_SaUint32T (tmp, &component->saAmfCompCategory);
	tmp = amf_deserialize_SaUint32T (tmp, &component->saAmfCompCapability);
	tmp = amf_deserialize_SaUint32T (tmp, &component->saAmfCompNumMaxActiveCsi);
	tmp = amf_deserialize_SaUint32T (tmp, &component->saAmfCompNumMaxStandbyCsi);

	tmp = amf_deserialize_SaUint32T (tmp, &cnt);
	component->saAmfCompCmdEnv = amf_malloc ((cnt + 1) * sizeof (SaStringT*));
	for (i = 0; i < cnt; i++) {
		tmp = amf_deserialize_SaStringT (tmp, &component->saAmfCompCmdEnv[i]);
	}
	component->saAmfCompCmdEnv[i] = NULL;

	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompDefaultClcCliTimeout);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompDefaultCallbackTimeOut);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompInstantiateCmd);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompInstantiateCmdArgv);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompInstantiateTimeout);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompInstantiationLevel);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompNumMaxInstantiateWithoutDelay);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompNumMaxInstantiateWithDelay);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompDelayBetweenInstantiateAttempts);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompTerminateCmd);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompTerminateTimeout);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompTerminateCmdArgv);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompCleanupCmd);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompCleanupTimeout);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompCleanupCmdArgv);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompAmStartCmd);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompAmStartTimeout);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompAmStartCmdArgv);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompNumMaxAmStartAttempt);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompAmStopCmd);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompAmStopTimeout);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->saAmfCompAmStopCmdArgv);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompNumMaxAmStopAttempt);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompTerminateCallbackTimeout);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompCSISetCallbackTimeout);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompQuiescingCompleteTimeout);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompCSIRmvCallbackTimeout);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompRecoveryOnError);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompDisableRestart);
	tmp = amf_deserialize_SaNameT (
		tmp, &component->saAmfCompProxyCsi);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompOperState);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompPresenceState);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->saAmfCompRestartCount);
	tmp = amf_deserialize_SaNameT (
		tmp, &component->saAmfCompCurrProxyName);
	tmp = amf_deserialize_SaStringT (
		tmp, &component->clccli_path);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->comptype);
	tmp = amf_deserialize_SaUint32T (
		tmp, &component->error_suspected);

	return component;
}

void *amf_healthcheck_serialize (struct amf_healthcheck *healthcheck, int *len)
{
	char *buf = NULL;
	int offset = 0, size = 0;

	TRACE8 ("%s", healthcheck->safHealthcheckKey.key);

	buf = amf_serialize_opaque (buf, &size, &offset,
		&healthcheck->safHealthcheckKey.key, SA_AMF_HEALTHCHECK_KEY_MAX);
	buf = amf_serialize_SaUint16T (buf, &size, &offset,
		healthcheck->safHealthcheckKey.keyLen);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		healthcheck->saAmfHealthcheckMaxDuration);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		healthcheck->saAmfHealthcheckPeriod);

	*len = offset;

	return buf;
}

struct amf_healthcheck *amf_healthcheck_deserialize (
	struct amf_comp *comp, char *buf)
{
	char *tmp = buf;
	int cnt;
	amf_healthcheck_t *healthcheck = amf_healthcheck_new (comp);

	tmp = amf_deserialize_opaque (tmp, &healthcheck->safHealthcheckKey.key, &cnt);
	tmp = amf_deserialize_SaUint16T (tmp,
		&healthcheck->safHealthcheckKey.keyLen);
	tmp = amf_deserialize_SaUint32T (tmp,
		&healthcheck->saAmfHealthcheckMaxDuration);
	tmp = amf_deserialize_SaUint32T (tmp,
		&healthcheck->saAmfHealthcheckPeriod);

	return healthcheck;
}

amf_healthcheck_t *amf_healthcheck_new (struct amf_comp *comp)
{
	amf_healthcheck_t *healthcheck = amf_calloc (1, sizeof (amf_healthcheck_t));

	healthcheck->comp = comp;
	healthcheck->next = comp->healthcheck_head;
	comp->healthcheck_head = healthcheck;

	return healthcheck;
}

void amf_comp_csi_remove (amf_comp_t *component,
	amf_csi_assignment_t *csi_assignment)
{
	struct res_lib_amf_csiremovecallback res_lib;

	ENTER("");

	res_lib.header.id = MESSAGE_RES_AMF_CSIREMOVECALLBACK;
	res_lib.header.size = sizeof (struct res_lib_amf_csiremovecallback);
	res_lib.header.error = SA_AIS_OK;
	res_lib.invocation =
		invocation_create (AMF_RESPONSE_CSIREMOVECALLBACK, csi_assignment);

	amf_comp_dn_make (component, &res_lib.compName);
	amf_csi_dn_make (csi_assignment->csi, &res_lib.csiName);
	res_lib.csiFlags = SA_AMF_CSI_TARGET_ONE;

	TRACE7 ("sending CSI remove request to component %s",
		res_lib.compName.value);
	openais_conn_send_response (
		openais_conn_partner_get (component->conn),
		&res_lib, sizeof (struct res_lib_amf_csiremovecallback));
}

void amf_comp_error_suspected_clear (amf_comp_t *comp)
{
	comp->error_suspected = 0;
}

void amf_comp_error_suspected_set (amf_comp_t *comp)
{
	comp->error_suspected = 1;
}

int amf_comp_is_error_suspected (amf_comp_t *comp)
{
	return comp->error_suspected ? 1 : 0;
}

