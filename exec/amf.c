/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include "../include/ais_types.h"
#include "../include/ais_msg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "gmi.h"
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "parse.h"
#include "main.h"
#include "handlers.h"

#define LOG_SERVICE LOG_SERVICE_AMF
#include "print.h"

struct invocation {
	struct conn_info *conn_info;
	int interface;
	int active;
};

struct invocation *invocation_entries = 0;

int invocation_entries_size = 0;

#ifdef INPARSEDOTH
enum amfOperationalState {
	AMF_OPER_DISABLED,
	AMF_OPER_ENABLED
};

enum amfAdministrativeState {
	AMF_ADMIN_UNLOCKED,
	AMF_ADMIN_LOCKED,
	AMF_ADMIN_STOPPING
};

enum amfOperationalAdministrativeState {
	AMF_ENABLED_UNLOCKED,
	AMF_DISABLED_UNLOCKED,
	AMF_DISABLED_LOCKED,
	AMF_ENABLED_STOPPING
};


/*
 * State machines for states in AMF
 */
enum amfEnabledUnlockedState {
	AMF_ENABLED_UNLOCKED_INITIAL,
	AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED,
	AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED,
	AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED,
	AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED,
	AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED,
	AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED,
};

enum amfDisabledUnlockedState {
	AMF_DISABLED_UNLOCKED_INITIAL,
	AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED,
	AMF_DISABLED_UNLOCKED_QUIESCED_COMPLETED,
	AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED,
	AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED
};
	
enum amfDisabledLockedState {
	AMF_DISABLED_LOCKED_INITIAL,
	AMF_DISABLED_LOCKED_QUIESCED_REQUESTED,
	AMF_DISABLED_LOCKED_QUIESCED_COMPLETED,
	AMF_DISABLED_LOCKED_OUT_OF_SERVICE_REQUESTED
	AMF_DISABLED_LOCKED_OUT_OF_SERVICE_COMPLETED
};

enum amfEnabledStoppingState {
	AMF_ENABLED_STOPPING_INITIAL,
	AMF_ENABLED_STOPPING_STOPPING_REQUESTED,
	AMF_ENABLED_STOPPING_STOPPING_COMPLETED
};

/*
 * Internal Functions
 */
static void componentOutOfServiceSetNoApi (
	struct saAmfComponent *component);

#endif
static void grow_amf_track_table (
	struct conn_info *conn_info,
	int growby);

static void sendProtectionGroupNotification (
	struct conn_info *conn_info,
	SaAmfProtectionGroupNotificationT *notificationBufferAddress,
	struct saAmfProtectionGroup *amfProtectionGroup,
	struct saAmfComponent *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent,
	SaUint8T trackFlags);

static int activeServiceUnitsCount (
	struct saAmfGroup *saAmfGroup);

static void component_unregister (
	struct saAmfComponent *component);

static void component_registerpriority (
	struct saAmfComponent *component,
	int priority);

static void enumerate_components (
	void (*function)(struct saAmfComponent *, void *data),
	void *data);

#ifdef COMPILE_OUT
static void CSIRemove (
	struct conn_info *conn_info);

static void haStateSetClusterInit (
	struct conn_info *conn_info,
	struct saAmfComponent *saAmfComponent);
#endif

static void haStateSetCluster (
	struct saAmfComponent *saAmfComponent,
	SaAmfHAStateT haState);

static void readinessStateSetApi (
	struct saAmfComponent *component,
	SaAmfReadinessStateT readinessState);

#ifdef COMPILE_OUT
static void readinessStateSetClusterInit (
	struct conn_info *conn_info,
	struct saAmfComponent *saAmfComponent);
#endif

static void readinessStateSetCluster (
	struct saAmfComponent *saAmfComponent,
	SaAmfReadinessStateT readinessState);

#ifdef COMPILE_OUT
static void enumerateComponentsClusterInit (
	struct saAmfComponent *component,
	void *data);
#endif

static void dsm (
	struct saAmfComponent *saAmfComponent);

#if 0 /* NOT IMPLEMENTED */
static void componentTerminate (
	struct conn_info *conn_info);
#endif

static void timer_function_libamf_healthcheck (
	void *data);

static struct saAmfProtectionGroup *findProtectionGroup (
	SaNameT *csiName);

static struct saAmfComponent *findComponentInProtectionGroup (
	SaNameT *csiName,
	SaNameT *compName);

static void sendProtectionGroupNotifications (
	struct saAmfComponent *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent);

static void sendProtectionGroupNotification (
	struct conn_info *conn_info,
	SaAmfProtectionGroupNotificationT *notificationBufferAddress,
	struct saAmfProtectionGroup *amfProtectionGroup,
	struct saAmfComponent *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent,
	SaUint8T trackFlags);

static void response_handler_readinessstatesetcallback (
	struct conn_info *conn_info,
	struct req_amf_response *req_amf_response);

static void response_handler_csisetcallback (
	struct conn_info *conn_info,
	struct req_amf_response *req_amf_response);

static void amf_confchg_nleave (
	struct saAmfComponent *component,
	void *data);

static void amf_confchg_njoin (
	struct saAmfComponent *component,
	void *data);

static int amf_confchg_fn (
	enum gmi_configuration_type configuration_type,
    struct sockaddr_in *member_list, int member_list_entries,
    struct sockaddr_in *left_list, int left_list_entries,
    struct sockaddr_in *joined_list, int joined_list_entries);

/***
static void amf_dump (void);
***/

static int amf_exit_fn (struct conn_info *conn_info);

static int amf_exec_init_fn (void);

static void amf_synchronize (void *message, struct in_addr source_addr);

static int message_handler_req_exec_amf_componentregister (void *message, struct in_addr source_addr);

static int message_handler_req_exec_amf_componentunregister (void *message, struct in_addr source_addr);

static int message_handler_req_exec_amf_errorreport (void *message, struct in_addr source_addr);

static int message_handler_req_exec_amf_errorcancelall (void *message, struct in_addr source_addr);

static int message_handler_req_exec_amf_readinessstateset (void *message, struct in_addr source_addr);

static int message_handler_req_exec_amf_hastateset (void *message, struct in_addr source_addr);

static int message_handler_req_amf_init (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_activatepoll (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_componentregister (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_componentunregister (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_readinessstateget (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_hastateget (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_protectiongrouptrackstart (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_protectiongrouptrackstop (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_errorreport (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_errorcancelall (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_stoppingcomplete (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_response (struct conn_info *conn_info, void *message);

static int message_handler_req_amf_componentcapabilitymodelget (struct conn_info *conn_info, void *message);

/*
int (*amf_libais_handler_fns[]) (struct conn_info *conn_info, void *) = {
	message_handler_req_lib_activatepoll,
	message_handler_req_amf_componentregister,
	message_handler_req_amf_componentunregister,
	message_handler_req_amf_readinessstateget,
	message_handler_req_amf_hastateget,
	message_handler_req_amf_protectiongrouptrackstart,
	message_handler_req_amf_protectiongrouptrackstop,
	message_handler_req_amf_errorreport,
	message_handler_req_amf_errorcancelall,
	message_handler_req_amf_stoppingcomplete,
	message_handler_req_amf_response,
	message_handler_req_amf_componentcapabilitymodelget
};
*/

struct libais_handler amf_libais_handlers[] =
{
	{ /* 0 */
		.libais_handler_fn	= message_handler_req_lib_activatepoll,
		.response_size		= sizeof (struct res_lib_activatepoll),
		.response_id		= MESSAGE_RES_LIB_ACTIVATEPOLL, // TODO RESPONSE
		.gmi_prio			= GMI_PRIO_RECOVERY
	},
	{ /* 1 */
		.libais_handler_fn	= message_handler_req_amf_componentregister,
		.response_size		= sizeof (struct res_lib_amf_componentregister),
		.response_id		= MESSAGE_RES_AMF_COMPONENTREGISTER,
		.gmi_prio			= GMI_PRIO_MED
	},
	{ /* 2 */
		.libais_handler_fn	= message_handler_req_amf_componentunregister,
		.response_size		= sizeof (struct res_lib_amf_componentunregister),
		.response_id		= MESSAGE_RES_AMF_COMPONENTUNREGISTER,
		.gmi_prio			= GMI_PRIO_MED
	},
	{ /* 3 */
		.libais_handler_fn	= message_handler_req_amf_readinessstateget,
		.response_size		= sizeof (struct res_lib_amf_readinessstateget),
		.response_id		= MESSAGE_RES_AMF_READINESSSTATEGET,
		.gmi_prio			= GMI_PRIO_RECOVERY
	},
	{ /* 4 */
		.libais_handler_fn	= message_handler_req_amf_hastateget,
		.response_size		= sizeof (struct res_lib_amf_hastateget),
		.response_id		= MESSAGE_RES_AMF_READINESSSTATEGET,
		.gmi_prio			= GMI_PRIO_RECOVERY
	},
	{ /* 5 */
		.libais_handler_fn	= message_handler_req_amf_protectiongrouptrackstart,
		.response_size		= sizeof (struct res_lib_amf_protectiongrouptrackstart),
		.response_id		= MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTART,
		.gmi_prio			= GMI_PRIO_RECOVERY
	},
	{ /* 6 */
		.libais_handler_fn	= message_handler_req_amf_protectiongrouptrackstop,
		.response_size		= sizeof (struct res_lib_amf_protectiongrouptrackstop),
		.response_id		= MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTOP,
		.gmi_prio			= GMI_PRIO_RECOVERY
	},
	{ /* 7 */
		.libais_handler_fn	= message_handler_req_amf_errorreport,
		.response_size		= sizeof (struct res_lib_amf_errorreport),
		.response_id		= MESSAGE_RES_AMF_ERRORREPORT,
		.gmi_prio			= GMI_PRIO_MED
	},
	{ /* 8 */
		.libais_handler_fn	= message_handler_req_amf_errorcancelall,
		.response_size		= sizeof (struct res_lib_amf_errorcancelall),
		.response_id		= MESSAGE_RES_AMF_ERRORCANCELALL,
		.gmi_prio			= GMI_PRIO_MED
	},
	{ /* 9 */
		.libais_handler_fn	= message_handler_req_amf_stoppingcomplete,
		.response_size		= sizeof (struct res_lib_amf_stoppingcomplete),
		.response_id		= MESSAGE_RES_AMF_STOPPINGCOMPLETE, // TODO 
		.gmi_prio			= GMI_PRIO_RECOVERY
	},
	{ /* 10 */
		.libais_handler_fn	= message_handler_req_amf_response,
		.response_size		= sizeof (struct res_lib_amf_response),
		.response_id		= MESSAGE_RES_AMF_RESPONSE, // TODO
		.gmi_prio			= GMI_PRIO_RECOVERY
	},
	{ /* 11 */
		.libais_handler_fn	= message_handler_req_amf_componentcapabilitymodelget,
		.response_size		= sizeof (struct res_lib_amf_componentcapabilitymodelget),
		.response_id		= MESSAGE_RES_AMF_COMPONENTCAPABILITYMODELGET,
		.gmi_prio			= GMI_PRIO_RECOVERY
	}
};

int (*amf_aisexec_handler_fns[]) (void *, struct in_addr source_addr) = {
	message_handler_req_exec_amf_componentregister,
	message_handler_req_exec_amf_componentunregister,
	message_handler_req_exec_amf_errorreport,
	message_handler_req_exec_amf_errorcancelall,
	message_handler_req_exec_amf_readinessstateset,
	message_handler_req_exec_amf_hastateset,
};

/*
 * Exports the interface for the service
 */
struct service_handler amf_service_handler = {
	.libais_handlers			=	amf_libais_handlers,
	.libais_handlers_count		= sizeof (amf_libais_handlers) / sizeof (struct libais_handler),
	.aisexec_handler_fns		= amf_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (amf_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn					= amf_confchg_fn,
	.libais_init_fn				= message_handler_req_amf_init,
	.libais_exit_fn				= amf_exit_fn,
	.exec_init_fn				= amf_exec_init_fn,
	.exec_dump_fn				= amf_dump
};

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

int req_amf_invocation_create (int interface, 
	struct conn_info *conn_info)
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
	invocation_addr->conn_info = conn_info;
	invocation_addr->active = 1;

	return (loc);
}

int req_amf_invocation_get_and_destroy (int invocation, int *interface,
	struct conn_info **conn_info)
{
	if (invocation > invocation_entries_size) {
printf ("a\n");
		return (-1);
	}
	if (invocation_entries[invocation].active == 0) {
printf ("b\n");
		return (-1);
	}

	*interface = invocation_entries[invocation].interface;
	*conn_info = invocation_entries[invocation].conn_info;
	memset (&invocation_entries[invocation], 0, sizeof (struct invocation));
	
	return (0);
}

static void component_unregisterpriority (
	struct saAmfComponent *component,
	int priority)
{
	struct req_exec_amf_componentunregister req_exec_amf_componentunregister;
	struct iovec iovecs[2];

	/*
	 * This only works on local components
	 */
	if (component == 0 || component->local != 1) {
		return;
	}
	log_printf (LOG_LEVEL_DEBUG, "component_unregister: unregistering component %s\n",
		getSaNameT (&component->name));
	component->probableCause = SA_AMF_NOT_RESPONDING;

	req_exec_amf_componentunregister.header.size = sizeof (struct req_exec_amf_componentunregister);
	req_exec_amf_componentunregister.header.id = MESSAGE_REQ_EXEC_AMF_COMPONENTUNREGISTER;

	req_exec_amf_componentunregister.source.conn_info = 0;
	req_exec_amf_componentunregister.source.in_addr.s_addr = 0;

	memset (&req_exec_amf_componentunregister.req_lib_amf_componentunregister,
		0, sizeof (struct req_lib_amf_componentunregister));
	memcpy (&req_exec_amf_componentunregister.req_lib_amf_componentunregister.compName,
		&component->name,
		sizeof (SaNameT));

	iovecs[0].iov_base = (char *)&req_exec_amf_componentunregister;
	iovecs[0].iov_len = sizeof (req_exec_amf_componentunregister);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, priority) == 0);
}

static void component_unregister (
	struct saAmfComponent *component)
{
	component_unregisterpriority (component, GMI_PRIO_MED);
	return;
}

static void component_registerpriority (
	struct saAmfComponent *component,
	int priority)
{
	struct req_exec_amf_componentregister req_exec_amf_componentregister;
	struct iovec iovecs[2];

	/*
	 * This only works on local components
	 */
	if (component == 0 || component->local != 1) {
		return;
	}
	log_printf (LOG_LEVEL_DEBUG, "component_register: registering component %s\n",
		getSaNameT (&component->name));

	req_exec_amf_componentregister.header.size = sizeof (struct req_exec_amf_componentregister);
	req_exec_amf_componentregister.header.id = MESSAGE_REQ_EXEC_AMF_COMPONENTREGISTER;

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

	iovecs[0].iov_base = (char *)&req_exec_amf_componentregister;
	iovecs[0].iov_len = sizeof (req_exec_amf_componentregister);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, priority) == 0);
}

/***
This should be used for a partition I think
**/
void enumerate_components (
	void (*function)(struct saAmfComponent *, void *data),
	void *data)
{
	struct list_head *AmfGroupList;
	struct list_head *AmfUnitList;
	struct list_head *AmfComponentList;

	struct saAmfGroup *saAmfGroup;
	struct saAmfUnit *AmfUnit;
	struct saAmfComponent *AmfComponent;


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
			for (AmfComponentList = AmfUnit->saAmfComponentHead.next;
				AmfComponentList != &AmfUnit->saAmfComponentHead;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList,
					struct saAmfComponent, saAmfComponentList);

				function (AmfComponent, data);
			}
		}
	}
}

int activeServiceUnitsCount (struct saAmfGroup *saAmfGroup) {
	struct saAmfUnit *saAmfUnit;
	struct saAmfComponent *saAmfComponent;
	struct list_head *saAmfComponentList;
	struct list_head *saAmfUnitList;
	int activeServiceUnits = 0;
	int thisServiceUnitActive;

	/*
	 * Search all units
	 */
	for (activeServiceUnits = 0, saAmfUnitList = saAmfGroup->saAmfUnitHead.next;
		saAmfUnitList != &saAmfGroup->saAmfUnitHead;
		saAmfUnitList = saAmfUnitList->next) {

		saAmfUnit = list_entry (saAmfUnitList,
			struct saAmfUnit, saAmfUnitList);

		/*
		 * Search all components
		 */
		for (thisServiceUnitActive = 1, saAmfComponentList = saAmfUnit->saAmfComponentHead.next;
			saAmfComponentList != &saAmfUnit->saAmfComponentHead;
			saAmfComponentList = saAmfComponentList->next) {

			saAmfComponent = list_entry (saAmfComponentList,
				struct saAmfComponent, saAmfComponentList);

			if (saAmfComponent->newHAState != SA_AMF_ACTIVE) {
				thisServiceUnitActive = 0;
			}
		}
		/*
		 * If all components are active in service unit, count service unit as active
	 	 */
		if (thisServiceUnitActive) {
			activeServiceUnits += 1;
		}
	}

	return (activeServiceUnits);
}

#ifdef CONFIG_TODO
This should be sent after a service unit is made out of service

void CSIRemove (struct conn_info *conn_info)
{
	struct res_lib_amf_csiremovecallback res_lib_amf_csiremovecallback;
		
	if (conn_info->active == 0 ||
		conn_info->service != SOCKET_SERVICE_AMF) {

		return;
	}

	log_printf (LOG_NOTICE_DEBUG, "executing CSI remove callback into API\n");

	res_lib_amf_csiremovecallback.header.id = MESSAGE_RES_AMF_CSIREMOVECALLBACK;
	res_lib_amf_csiremovecallback.header.size = sizeof (struct res_lib_amf_csiremovecallback);
	res_lib_amf_csiremovecallback.header.error = SA_OK;

	res_lib_amf_csiremovecallback.invocation =
		req_amf_response_set (
		MESSAGE_REQ_AMF_RESPONSE_SAAMFCSIREMOVECALLBACK,
		conn_info->fd);

	memcpy (&res_lib_amf_csiremovecallback.compName,
		&conn_info->component->name, sizeof (SaNameT));
	memcpy (&res_lib_amf_csiremovecallback.csiName,
		&conn_info->component->saAmfProtectionGroup->name, sizeof (SaNameT));

	res_lib_amf_csiremovecallback.csiFlags = SA_AMF_CSI_ALL_INSTANCES;
	libais_send_response (conn_info, &res_lib_amf_csiremovecallback,
		sizeof (struct res_lib_amf_csiremovecallback));
}
#endif

void haStateSetApi (struct saAmfComponent *component, SaAmfHAStateT haState)
{
	struct res_lib_amf_csisetcallback res_lib_amf_csisetcallback;

	log_printf (LOG_LEVEL_DEBUG, "sending ha state to API\n");

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
		component->conn_info->service != SOCKET_SERVICE_AMF) {
		return;
	}

	res_lib_amf_csisetcallback.header.id = MESSAGE_RES_AMF_CSISETCALLBACK;
	res_lib_amf_csisetcallback.header.size = sizeof (struct res_lib_amf_csisetcallback);
	res_lib_amf_csisetcallback.header.error = SA_OK;

	res_lib_amf_csisetcallback.invocation =
		req_amf_invocation_create (
		MESSAGE_REQ_AMF_RESPONSE_SAAMFCSISETCALLBACK,
		component->conn_info);
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

	libais_send_response (component->conn_info, &res_lib_amf_csisetcallback,
		sizeof (struct res_lib_amf_csisetcallback));
}

#ifdef COMPILE_OUT

void haStateSetClusterInit (
	struct conn_info *conn_info,
	struct saAmfComponent *saAmfComponent)
{
	struct req_exec_amf_hastatesetcluster req_exec_amf_hastatesetcluster;

	return;
	req_exec_amf_hastatesetcluster.header.id = MESSAGE_REQ_EXEC_AMF_HASTATESET;
	req_exec_amf_hastatesetcluster.header.size = sizeof (struct req_exec_amf_hastatesetcluster);
	memcpy (&req_exec_amf_hastatesetcluster.compName,
		&saAmfComponent->name, sizeof (SaNameT));
	req_exec_amf_hastatesetcluster.haState = saAmfComponent->currentHAState;

	log_printf (LOG_LEVEL_DEBUG, "Sending init ha state message to cluster node to set ha state of component %s\n", getSaNameT (&saAmfComponent->name));
	log_printf (LOG_LEVEL_DEBUG, "ha state is %d\n", saAmfComponent->currentHAState);

	libais_send_response (conn_info, &req_exec_amf_hastatesetcluster,
		sizeof (struct req_exec_amf_hastatesetcluster));
}
#endif

static void haStateSetCluster (
	struct saAmfComponent *component,
	SaAmfHAStateT haState)
{

	struct req_exec_amf_hastateset req_exec_amf_hastateset;
	struct iovec iovecs[2];

	req_exec_amf_hastateset.header.id = MESSAGE_REQ_EXEC_AMF_HASTATESET;
	req_exec_amf_hastateset.header.size = sizeof (struct req_exec_amf_hastateset);
	memcpy (&req_exec_amf_hastateset.compName, &component->name, sizeof (SaNameT));
	req_exec_amf_hastateset.haState = haState;

	log_printf (LOG_LEVEL_DEBUG, "Sending ha state to cluster for component %s\n", getSaNameT (&component->name));
	log_printf (LOG_LEVEL_DEBUG, "ha state is %d\n", haState);

	iovecs[0].iov_base = (char *)&req_exec_amf_hastateset;
	iovecs[0].iov_len = sizeof (req_exec_amf_hastateset);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_HIGH) == 0);
}

void readinessStateSetApi (struct saAmfComponent *component,
	SaAmfReadinessStateT readinessState)
{
	struct res_lib_amf_readinessstatesetcallback res_lib_amf_readinessstatesetcallback;

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
		component->conn_info->service != SOCKET_SERVICE_AMF) {

		return;
	}

	res_lib_amf_readinessstatesetcallback.header.id = MESSAGE_RES_AMF_READINESSSTATESETCALLBACK;
	res_lib_amf_readinessstatesetcallback.header.size = sizeof (struct res_lib_amf_readinessstatesetcallback);
	res_lib_amf_readinessstatesetcallback.header.error = SA_OK;
	res_lib_amf_readinessstatesetcallback.invocation =
		req_amf_invocation_create (
		MESSAGE_REQ_AMF_RESPONSE_SAAMFREADINESSSTATESETCALLBACK,
		component->conn_info);
	if (res_lib_amf_readinessstatesetcallback.invocation == -1) {
		printf ("TODO readiness set callback\n");
	}
	memcpy (&res_lib_amf_readinessstatesetcallback.compName,
		&component->name, sizeof (SaNameT));
	res_lib_amf_readinessstatesetcallback.readinessState = readinessState;
	component->newReadinessState = readinessState;

	log_printf (LOG_LEVEL_DEBUG, "Setting conn_info %p to readiness state %d\n", component->conn_info, readinessState);

	libais_send_response (component->conn_info, &res_lib_amf_readinessstatesetcallback,
		sizeof (struct res_lib_amf_readinessstatesetcallback));
}

#ifdef COMPILE_OUT
void readinessStateSetClusterInit (
	struct conn_info *conn_info,
	struct saAmfComponent *saAmfComponent)
{

	struct req_exec_amf_readinessstatesetcluster req_exec_amf_readinessstatesetcluster;

	return;
	req_exec_amf_readinessstatesetcluster.header.id = MESSAGE_REQ_EXEC_AMF_READINESSSTATESET;
	req_exec_amf_readinessstatesetcluster.header.size = sizeof (struct req_exec_amf_readinessstateset);
	memcpy (&req_exec_amf_readinessstatesetcluster.compName,
		&saAmfComponent->name, sizeof (SaNameT));
	req_exec_amf_readinessstatesetcluster.readinessState = saAmfComponent->currentReadinessState;

	log_printf (LOG_LEVEL_DEBUG, "Sending init message to one cluster node to set readiness state of component %s\n", getSaNameT (&saAmfComponent->name));
	log_printf (LOG_LEVEL_DEBUG, "readiness state is %d\n", saAmfComponent->currentReadinessState);

	libais_send_response (conn_info, &req_exec_amf_readinessstatesetcluster,
		sizeof (struct req_exec_amf_readinessstatesetcluster));
}
#endif

static void readinessStateSetCluster (
	struct saAmfComponent *component,
	SaAmfReadinessStateT readinessState)
{

	struct req_exec_amf_readinessstateset req_exec_amf_readinessstateset;
	struct iovec iovecs[2];

	req_exec_amf_readinessstateset.header.id = MESSAGE_REQ_EXEC_AMF_READINESSSTATESET;
	req_exec_amf_readinessstateset.header.size = sizeof (struct req_exec_amf_readinessstateset);
	memcpy (&req_exec_amf_readinessstateset.compName, &component->name, sizeof (SaNameT));
	req_exec_amf_readinessstateset.readinessState = readinessState;

	log_printf (LOG_LEVEL_DEBUG, "Sending message to all cluster nodes to set readiness state of component %s\n",
		getSaNameT (&component->name));
	log_printf (LOG_LEVEL_DEBUG, "readiness state is %d\n", readinessState);

	iovecs[0].iov_base = (char *)&req_exec_amf_readinessstateset;
	iovecs[0].iov_len = sizeof (req_exec_amf_readinessstateset);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_HIGH) == 0);
}

#ifdef CMOPILE_OUT
void enumerateComponentsClusterInit (
	struct saAmfComponent *component,
	void *data)
{
	struct conn_info *conn_info = (int)data;

	return;
	readinessStateSetClusterInit (fd, component);
	haStateSetClusterInit (fd, component);
}
#endif

static void dsmDisabledUnlockedRegisteredOrErrorCancel (
	struct saAmfComponent *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	int serviceUnitEnabled;
	
	log_printf (LOG_LEVEL_DEBUG, "dsmDisabledUnlockedRegisteredOrErrorCancel for %s\n",
		getSaNameT (&component->name));

	unit = component->saAmfUnit;
	for (serviceUnitEnabled = 1, list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list,
			struct saAmfComponent, saAmfComponentList);

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
	struct saAmfComponent *component)
{
	log_printf (LOG_LEVEL_DEBUG, "dsmDisabledUnlockedFailedComponent: for %s.\n",
			getSaNameT (&component->name));
	switch (component->enabledUnlockedState) {
    	case AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED:
    	case AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED:
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED;
			if (component->probableCause == SA_AMF_NOT_RESPONDING) {
				readinessStateSetCluster (component, SA_AMF_OUT_OF_SERVICE);
			} else {
				readinessStateSetApi (component, SA_AMF_OUT_OF_SERVICE);
			}
			break;

    	case AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED:
    	case AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED:
    	case AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED:
    	case AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED:
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED;
			if (component->probableCause == SA_AMF_NOT_RESPONDING) {
				haStateSetCluster (component, SA_AMF_QUIESCED);
			} else {
				haStateSetApi (component, SA_AMF_QUIESCED);
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
	struct saAmfComponent *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;

	unit = component->saAmfUnit;

	for (list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list, struct saAmfComponent, saAmfComponentList);
		dsmDisabledUnlockedFailedComponent (component);
	}
	return;
}

static void dsmDisabledUnlockedQuiescedRequested (
	struct saAmfComponent *component)
{
	component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_QUIESCED_COMPLETED;
	dsm (component);
}

static void dsmDisabledUnlockedQuiescedCompleted (
	struct saAmfComponent *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	int serviceUnitQuiesced;
	
	unit = component->saAmfUnit;
	for (serviceUnitQuiesced = 1, list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list, struct saAmfComponent, saAmfComponentList);

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
		for (list = unit->saAmfComponentHead.next;
			list != &unit->saAmfComponentHead;
			list = list->next) {

			component = list_entry (list, struct saAmfComponent, saAmfComponentList);

			log_printf (LOG_LEVEL_DEBUG, "dsm: Sending readiness state set to OUTOFSERVICE for comp %s.\n",
				getSaNameT (&component->name));
			readinessStateSetApi (component, SA_AMF_OUT_OF_SERVICE);
			component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED;
		}
	}
}

static void dsmDisabledUnlockedOutOfServiceRequested (
	struct saAmfComponent *component)
{
	component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED;
	dsm (component);
}

static void dsmDisabledUnlockedOutOfServiceCompleted (
	struct saAmfComponent *component)
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
	for (serviceUnitOutOfService = 1, list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list, struct saAmfComponent, saAmfComponentList);

		if (component->probableCause != SA_AMF_NOT_RESPONDING && component->registered) {
			if (component->currentReadinessState != SA_AMF_OUT_OF_SERVICE) {
				log_printf (LOG_LEVEL_DEBUG, "dsm: Can't transition states, found component not quiesced.\n");
				serviceUnitOutOfService = 0;
				break;
			}
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
				comp_list = unit->saAmfComponentHead.next;
				comp_list != &unit->saAmfComponentHead;
				comp_list = comp_list->next) {
	
				component = list_entry (comp_list,
					struct saAmfComponent, saAmfComponentList);
	
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
			for (list = unit->saAmfComponentHead.next;
				list != &unit->saAmfComponentHead;
				list = list->next) {
	
				component = list_entry (list,
				struct saAmfComponent, saAmfComponentList);
	
				haStateSetApi (component, SA_AMF_ACTIVE);
			}
		} else {
			log_printf (LOG_LEVEL_DEBUG, "Can't activate standby service unit because no standby is available.\n");
		}
	}
}

static void dsmEnabledUnlockedInitial (
	struct saAmfComponent *component)
{ 
	struct saAmfUnit *unit;
	struct list_head *list;

	unit = component->saAmfUnit;
	for (list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list, struct saAmfComponent, saAmfComponentList);

		readinessStateSetApi (component, SA_AMF_IN_SERVICE);
		log_printf (LOG_LEVEL_DEBUG, "dsm: telling component %s to enter SA_AMF_IN_SERVICE.\n",
			getSaNameT (&component->name));
		component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED;
	}
}
static void dsmEnabledUnlockedInServiceRequested (
	struct saAmfComponent *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	int in_service;

	log_printf (LOG_LEVEL_DEBUG, "dsmEnabledUnlockedInServiceRequested %s.\n", getSaNameT (&component->name));
	
	unit = component->saAmfUnit;
	for (in_service = 1, list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list, struct saAmfComponent, saAmfComponentList);

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
	struct saAmfComponent *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;
	SaAmfHAStateT newHaState;
	int activeServiceUnits;

	log_printf (LOG_LEVEL_DEBUG, "dsmEnabledUnlockedInServiceCompleted %s.\n", getSaNameT (&component->name));

	unit = component->saAmfUnit;
	for (list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list,
			struct saAmfComponent, saAmfComponentList);

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
		haStateSetApi (component, newHaState);
	}
}
	
static void dsmEnabledUnlockedActiveRequested (
	struct saAmfComponent *component)
{
	if (component->local == 1) {
		log_printf (LOG_LEVEL_DEBUG, "Adding healthcheck timer\n");
		poll_timer_add (aisexec_poll_handle,
			component->healthcheckInterval,
			(void *)component->conn_info,
			timer_function_libamf_healthcheck,
			&component->timer_healthcheck);
	}

	component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED;
}

static void dsmEnabledUnlockedStandbyRequested (
	struct saAmfComponent *component)
{
	if (component->local == 1) {

		log_printf (LOG_LEVEL_DEBUG, "Adding healthcheck timer\n");

		poll_timer_add (aisexec_poll_handle,
			component->healthcheckInterval,
			(void *)component->conn_info,
			timer_function_libamf_healthcheck,
			&component->timer_healthcheck);
	}

	component->enabledUnlockedState = AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED;
}

static void dsmEnabledUnlockedTransitionDisabledUnlocked (
	struct saAmfComponent *component)
{
	struct saAmfUnit *unit;
	struct list_head *list;

	unit = component->saAmfUnit;
	for (list = unit->saAmfComponentHead.next;
		list != &unit->saAmfComponentHead;
		list = list->next) {

		component = list_entry (list, struct saAmfComponent, saAmfComponentList);

		log_printf (LOG_LEVEL_DEBUG,  "Requesting component %s transition to disabled.\n",
			getSaNameT (&component->name));

		component->disabledUnlockedState = AMF_DISABLED_UNLOCKED_FAILED;
	}

	component->saAmfUnit->operationalAdministrativeState = AMF_DISABLED_UNLOCKED;
	dsm (component);
}

static void dsmSynchronizeStaus (
	struct saAmfComponent *component)
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

	for (list = unit->saAmfComponentHead.next; list != &unit->saAmfComponentHead; list = list->next) {
		activeServiceUnits = activeServiceUnitsCount(group);
		if (activeServiceUnits <= group->saAmfActiveUnitsDesired) {
			break;
		}
		if (component->currentHAState != SA_AMF_ACTIVE) {
			continue;
		}
		haStateSetApi (component, SA_AMF_STANDBY);
	}

	return;
}

	
static void dsmEnabledUnlocked (
	struct saAmfComponent *component)
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
	struct saAmfComponent *component)
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
	struct saAmfComponent *component)
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

#if 0
/*
 * This is currently unused, but executes the componentterminatecallback
 * callback in the AMF api.
 */
void componentTerminate (struct conn_info *conn_info)
{
	struct res_lib_amf_componentterminatecallback res_lib_amf_componentterminatecallback;

	res_lib_amf_componentterminatecallback.header.id = MESSAGE_RES_AMF_COMPONENTTERMINATECALLBACK;
	res_lib_amf_componentterminatecallback.header.size = sizeof (struct res_lib_amf_componentterminatecallback);
	res_lib_amf_componentterminatecallback.header.error = SA_OK;
	res_lib_amf_componentterminatecallback.invocation =
		req_amf_response_set (
		MESSAGE_REQ_AMF_RESPONSE_SAAMFCOMPONENTTERMINATECALLBACK,
		fd);
	memcpy (&res_lib_amf_componentterminatecallback.compName,
		&connections[fd].component->name, sizeof (SaNameT));
	connections[fd].component->newReadinessState = SA_AMF_OUT_OF_SERVICE;

	log_printf (LOG_LEVEL_DEBUG, "terminating component on fd %d\n", fd);
	libais_send_response (fd, &res_lib_amf_componentterminatecallback,
		sizeof (struct res_lib_amf_componentterminatecallback));
}
#endif /* Not currently implemented */

void errorReport (
	struct saAmfComponent *component,
	SaAmfProbableCauseT probableCause)
{
	struct req_exec_amf_errorreport req_exec_amf_errorreport;
	struct iovec iovecs[2];

	req_exec_amf_errorreport.header.size = sizeof (struct req_exec_amf_errorreport);
	req_exec_amf_errorreport.header.id = MESSAGE_REQ_EXEC_AMF_ERRORREPORT;

	req_exec_amf_errorreport.source.conn_info = 0;
	req_exec_amf_errorreport.source.in_addr.s_addr = 0;
	memcpy (&req_exec_amf_errorreport.req_lib_amf_errorreport.erroneousComponent,
		&component->name,
		sizeof (SaNameT));
	req_exec_amf_errorreport.req_lib_amf_errorreport.errorDescriptor.probableCause = probableCause;

	iovecs[0].iov_base = (char *)&req_exec_amf_errorreport;
	iovecs[0].iov_len = sizeof (req_exec_amf_errorreport);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED) == 0);
}

int healthcheck_instance = 0;

void timer_function_libamf_healthcheck (void *data) {
	struct res_lib_amf_healthcheckcallback res_lib_amf_healthcheckcallback;
	struct conn_info *conn_info = (struct conn_info *)data;

	res_lib_amf_healthcheckcallback.header.id = MESSAGE_RES_AMF_HEALTHCHECKCALLBACK;
	res_lib_amf_healthcheckcallback.header.size = sizeof (struct res_lib_amf_healthcheckcallback);
	res_lib_amf_healthcheckcallback.header.error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "checking healthcheck on component %s\n",
		getSaNameT (&conn_info->component->name));
	if (conn_info->component->healthcheck_outstanding == 1) {

		log_printf (LOG_LEVEL_DEBUG, "Healthcheck timed out on component %s\n",
			getSaNameT (&conn_info->component->name));

		/*
		 * Report the error to the rest of the cluster using the normal state machine
		 */
		errorReport (conn_info->component, SA_AMF_NOT_RESPONDING);

		conn_info->component->healthcheck_outstanding = 2;
	} else
	if (conn_info->component->healthcheck_outstanding == 0) {
		conn_info->component->healthcheck_outstanding = 1;
		/*
		 * Send healthcheck message
		 */
		res_lib_amf_healthcheckcallback.invocation =
			req_amf_invocation_create (
			MESSAGE_REQ_AMF_RESPONSE_SAAMFHEALTHCHECKCALLBACK,
			conn_info);
	if (res_lib_amf_healthcheckcallback.invocation == -1) {
		printf ("TODO healthcheck set callback\n");
	}
		memcpy (&res_lib_amf_healthcheckcallback.compName,
			&conn_info->component->name,
			sizeof (SaNameT));
		res_lib_amf_healthcheckcallback.checkType = SA_AMF_HEARTBEAT;

	log_printf (LOG_LEVEL_DEBUG, "Sending instance %d\n", healthcheck_instance);
	res_lib_amf_healthcheckcallback.instance = healthcheck_instance++;
		libais_send_response (conn_info,
			&res_lib_amf_healthcheckcallback,
			sizeof (struct res_lib_amf_healthcheckcallback));

		poll_timer_add (aisexec_poll_handle,
			conn_info->component->healthcheckInterval,
			(void *)conn_info,
			timer_function_libamf_healthcheck,
			&conn_info->component->timer_healthcheck);
	}
}

struct saAmfProtectionGroup *findProtectionGroup (
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

struct saAmfComponent *findComponentInProtectionGroup (
	SaNameT *csiName,
	SaNameT *compName)
{

	struct list_head *AmfGroupList = 0;
	struct list_head *AmfProtectionGroupList = 0;
	struct list_head *AmfComponentList = 0;

	struct saAmfGroup *saAmfGroup = 0;
	struct saAmfProtectionGroup *AmfProtectionGroup = 0;
	struct saAmfComponent *AmfComponent = 0;
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
						struct saAmfComponent, saAmfProtectionGroupList);

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

DECLARE_LIST_INIT (library_notification_send_listhead);

static gmi_recovery_plug_handle amf_recovery_plug_handle;

void sendProtectionGroupNotifications (
	struct saAmfComponent *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent)
{
	int i;
	struct conn_info *conn_info;
	struct list_head *list;

	log_printf (LOG_LEVEL_DEBUG, "sendProtectionGroupNotifications: sending PGs to API.\n");

	/*
	 * Iterate all tracked connections
	 */
	for (list = library_notification_send_listhead.next;
		list != &library_notification_send_listhead;
		list = list->next) {

		conn_info = list_entry (list, struct conn_info, conn_list);
		for (i = 0; i < conn_info->ais_ci.u.libamf_ci.trackEntries; i++) {
			if (conn_info->ais_ci.u.libamf_ci.tracks[i].active) {

				sendProtectionGroupNotification (conn_info,
					conn_info->ais_ci.u.libamf_ci.tracks[i].notificationBufferAddress, 
					changedComponent->saAmfProtectionGroup,
					changedComponent,
					changeToComponent,
					conn_info->ais_ci.u.libamf_ci.tracks[i].trackFlags);
			} /* if track flags active */
		} /* for all track entries */
	} /* for all connection entries */
}


void sendProtectionGroupNotification (struct conn_info *conn_info,
	SaAmfProtectionGroupNotificationT *notificationBufferAddress,
	struct saAmfProtectionGroup *amfProtectionGroup,
	struct saAmfComponent *changedComponent,
	SaAmfProtectionGroupChangesT changeToComponent,
	SaUint8T trackFlags)
{
	struct res_lib_amf_protectiongrouptrackcallback res_lib_amf_protectiongrouptrackcallback;
	SaAmfProtectionGroupNotificationT *protectionGroupNotification = 0;
	int notifyEntries = 0;
	struct saAmfComponent *component;
	struct list_head *componentList;

	/*
	 * Step through all components and generate protection group list for csi
	 */

	for (componentList = amfProtectionGroup->saAmfMembersHead.next;
		componentList != &amfProtectionGroup->saAmfMembersHead;
		componentList = componentList->next) {

		component = list_entry (componentList,
			struct saAmfComponent, saAmfProtectionGroupList);

		/*
		 * Generate new track entry for following cases:
		 * 1. If this component is the changed component and
		 *    SA_TRACK_CHANGES_ONLY is set
		 * 2. If track flags indicate SA_TRACK_CURRENT or SA_TRACK_CHANGES
		 */
		if (component == changedComponent ||
			(trackFlags & (SA_TRACK_CURRENT | SA_TRACK_CHANGES))) {

			protectionGroupNotification = (SaAmfProtectionGroupNotificationT *)mempool_realloc (protectionGroupNotification,
				sizeof (SaAmfProtectionGroupNotificationT) * (notifyEntries + 1));
			memcpy (&protectionGroupNotification[notifyEntries].member.compName, 
				&component->name, sizeof (SaNameT));
			memcpy (&protectionGroupNotification[notifyEntries].member.readinessState, 
				&component->currentReadinessState, sizeof (SaAmfReadinessStateT));
			memcpy (&protectionGroupNotification[notifyEntries].member.haState, 
				&component->currentHAState, sizeof (SaAmfHAStateT));
			if (component == changedComponent) {
				protectionGroupNotification[notifyEntries].change = changeToComponent;
			} else {
				protectionGroupNotification[notifyEntries].change = SA_AMF_PROTECTION_GROUP_NO_CHANGE;
			}
			notifyEntries += 1;
		}
	} /* for */

	/*
	 * Send track callback
	 */
	if (notifyEntries) {
		res_lib_amf_protectiongrouptrackcallback.header.size =
			sizeof (struct res_lib_amf_protectiongrouptrackcallback) +
			(notifyEntries * sizeof (SaAmfProtectionGroupNotificationT));
		res_lib_amf_protectiongrouptrackcallback.header.id = MESSAGE_RES_AMF_PROTECTIONGROUPTRACKCALLBACK;
		res_lib_amf_protectiongrouptrackcallback.header.error = SA_OK;
		res_lib_amf_protectiongrouptrackcallback.numberOfItems = notifyEntries;
		res_lib_amf_protectiongrouptrackcallback.numberOfMembers = notifyEntries;
		memcpy (&res_lib_amf_protectiongrouptrackcallback.csiName,
			&amfProtectionGroup->name, sizeof (SaNameT));

		res_lib_amf_protectiongrouptrackcallback.notificationBufferAddress = notificationBufferAddress;
		libais_send_response (conn_info, &res_lib_amf_protectiongrouptrackcallback,
			sizeof (struct res_lib_amf_protectiongrouptrackcallback));

		libais_send_response (conn_info, protectionGroupNotification,
			sizeof (SaAmfProtectionGroupNotificationT) * notifyEntries);

		mempool_free (protectionGroupNotification);
	}
}

/*
 * The response handler for readiness state set callback
 */
static void response_handler_readinessstatesetcallback (struct conn_info *conn_info,
	struct req_amf_response *req_amf_response)
{

	if (req_amf_response->error == SA_OK && conn_info->component) {
	log_printf (LOG_LEVEL_DEBUG, "CALLBACK sending readiness state to %s\n", 
		getSaNameT (&conn_info->component->name));
		readinessStateSetCluster (conn_info->component,
			conn_info->component->newReadinessState);
	}
}

/* 
 *	iterate service unit components
 *		telling all components not already QUIESCING to enter SA_AMF_QUIESCED state
 */
static void response_handler_csisetcallback (struct conn_info *conn_info,
	struct req_amf_response *req_amf_response)
{

	if (req_amf_response->error == SA_OK && conn_info->component) {
		haStateSetCluster (conn_info->component,
			conn_info->component->newHAState);
	}
}

static int amf_exec_init_fn (void)
{
	int res;

	res = gmi_recovery_plug_create (&amf_recovery_plug_handle);
	if (res != 0) {
		log_printf(LOG_LEVEL_ERROR,
		"Could not create recovery plug for amf service.\n");

		return (-1);
	}
	return (0);
}

void amf_confchg_njoin (struct saAmfComponent *component ,void *data)
{
	if (component->source_addr.s_addr != this_ip.sin_addr.s_addr) {
		return;
	}

	component_registerpriority (component, GMI_PRIO_RECOVERY);
	return;
}

void amf_confchg_nleave (struct saAmfComponent *component ,void *data)
{
	struct in_addr *source_addr;
	source_addr = (struct in_addr *)data;
	struct saAmfUnit *unit;
	struct list_head *list;
	struct saAmfComponent *leave_component = NULL;
	enum amfDisabledUnlockedState disablestate = AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED;

	if (component->source_addr.s_addr != source_addr->s_addr) {
		return;
	}

	if (!component->registered) {
		return;
	}

	log_printf (LOG_LEVEL_DEBUG, "amf_confchg_nleave(%s)\n", getSaNameT (&(component->name)));

        /* Component status Initialize */
	unit = component->saAmfUnit;
	
	for (list = unit->saAmfComponentHead.next; list != &unit->saAmfComponentHead; list = list->next) {

		component = list_entry (list,
			struct saAmfComponent, saAmfComponentList);

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

static int amf_confchg_fn (
	enum gmi_configuration_type configuration_type,
    struct sockaddr_in *member_list, int member_list_entries,
    struct sockaddr_in *left_list, int left_list_entries,
    struct sockaddr_in *joined_list, int joined_list_entries)
{
	int i;

	if (configuration_type == GMI_CONFIGURATION_REGULAR) {
		gmi_recovery_plug_unplug (amf_recovery_plug_handle);
	}

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
		enumerate_components (amf_confchg_nleave, (void *)&(left_list[i].sin_addr));
	}

	return (0);
}

int amf_exit_fn (struct conn_info *conn_info)
{
	/*
	 * Unregister all components registered to this file descriptor
	 */
	if (conn_info->service == SOCKET_SERVICE_AMF) {
		component_unregister (conn_info->component);

		if (conn_info->component && conn_info->component->timer_healthcheck) {
			poll_timer_delete (aisexec_poll_handle,
				conn_info->component->timer_healthcheck);

			conn_info->component->timer_healthcheck = 0;
		}

		if (conn_info->ais_ci.u.libamf_ci.tracks) {
			mempool_free (conn_info->ais_ci.u.libamf_ci.tracks);
			conn_info->ais_ci.u.libamf_ci.tracks = 0;
		}
	}

	return (0);
}

static int message_handler_req_exec_amf_componentregister (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_componentregister *req_exec_amf_componentregister = (struct req_exec_amf_componentregister *)message;
	struct res_lib_amf_componentregister res_lib_amf_componentregister;
	struct saAmfComponent *component;
	struct saAmfComponent *amfProxyComponent;
	SaErrorT error;

	log_printf (LOG_LEVEL_DEBUG, "Executive: ComponentRegister for component %s\n",
		getSaNameT (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName));

	/*
	 * Determine if proxy isn't registered
	 */
	error = SA_OK;
	component = findComponent (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName);
	amfProxyComponent = findComponent (&req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName);

	/*
	 * If a node is joining menber ship ,Component States Synchronize
	 */
	if (req_exec_amf_componentregister->source.in_addr.s_addr == 0) {
		amf_synchronize (message, source_addr);
		return (0);
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
	if (error == SA_OK) {
		if (component->registered) {
			error = SA_ERR_EXIST;
		}
	}

	/*
	 * Finally register component and setup links for proxy if
	 * proxy present
	 */
	if (error == SA_OK) {
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

		if (req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName.length > 0) {
			component->saAmfProxyComponent = amfProxyComponent;
		}
	}

	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (req_exec_amf_componentregister->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		if (error == SA_OK) {
			component->local = 1;
			req_exec_amf_componentregister->source.conn_info->component = component;
		}

		log_printf (LOG_LEVEL_DEBUG, "sending component register response to fd %d\n",
			    req_exec_amf_componentregister->source.conn_info->fd);

		res_lib_amf_componentregister.header.size = sizeof (struct res_lib_amf_componentregister);
		res_lib_amf_componentregister.header.id = MESSAGE_RES_AMF_COMPONENTREGISTER;
		res_lib_amf_componentregister.header.error = error;

		libais_send_response (req_exec_amf_componentregister->source.conn_info,
			&res_lib_amf_componentregister,
			sizeof (struct res_lib_amf_componentregister));
	}
	
	/*
	 * If no error on registration, determine if we should enter new state
	 */
	if (error == SA_OK) {
		dsm (component);
	}

	return (0);
}

static void amf_synchronize (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_componentregister *req_exec_amf_componentregister = (struct req_exec_amf_componentregister *)message;
	struct saAmfComponent *component;
	struct saAmfComponent *amfProxyComponent;

	log_printf (LOG_LEVEL_DEBUG, "amf_synchronize%s\n",
		getSaNameT (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName));

	/* Find Component */
	component = findComponent (&req_exec_amf_componentregister->req_lib_amf_componentregister.compName);
	amfProxyComponent = findComponent (&req_exec_amf_componentregister->req_lib_amf_componentregister.proxyCompName);

	/* If this node is Component onwer */
	if (component->source_addr.s_addr == this_ip.sin_addr.s_addr) {

		/* No Operation */
		return;
	}

	/* If this isn't Synchronizing target Node */
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

static int message_handler_req_exec_amf_componentunregister (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_componentunregister *req_exec_amf_componentunregister = (struct req_exec_amf_componentunregister *)message;
	struct res_lib_amf_componentunregister res_lib_amf_componentunregister;
	struct saAmfComponent *component;
	struct saAmfComponent *amfProxyComponent;
	SaErrorT error;

	log_printf (LOG_LEVEL_DEBUG, "Executive: Component_unregister for %s\n",
		getSaNameT (&req_exec_amf_componentunregister->req_lib_amf_componentunregister.compName));

	component = findComponent (&req_exec_amf_componentunregister->req_lib_amf_componentunregister.compName);
	amfProxyComponent = findComponent (&req_exec_amf_componentunregister->req_lib_amf_componentunregister.proxyCompName);

	/*
	 * Check for proxy and component not existing in system
	 */
	error = SA_OK;
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
	if (error == SA_OK && amfProxyComponent) {
		if (component->saAmfProxyComponent != amfProxyComponent) {
			error = SA_ERR_BAD_OPERATION;
		}
	}

	/*
	 * Finally unregister the component
	 */
	if (error == SA_OK) {
		component->registered = 0;
		dsmEnabledUnlockedTransitionDisabledUnlocked (component);
	}
	
	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (req_exec_amf_componentunregister->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		log_printf (LOG_LEVEL_DEBUG, "sending component unregister response to fd %d\n",
			req_exec_amf_componentunregister->source.conn_info->fd);

		res_lib_amf_componentunregister.header.size = sizeof (struct res_lib_amf_componentunregister);
		res_lib_amf_componentunregister.header.id = MESSAGE_RES_AMF_COMPONENTUNREGISTER;
		res_lib_amf_componentunregister.header.error = error;

		libais_send_response (req_exec_amf_componentunregister->source.conn_info,
			&res_lib_amf_componentunregister, sizeof (struct res_lib_amf_componentunregister));
	}

	return (0);
}

static int message_handler_req_exec_amf_errorreport (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_errorreport *req_exec_amf_errorreport = (struct req_exec_amf_errorreport *)message;
	struct res_lib_amf_errorreport res_lib_amf_errorreport;
	struct saAmfComponent *component;
	SaErrorT error = SA_ERR_BAD_OPERATION;

	log_printf (LOG_LEVEL_DEBUG, "Executive: ErrorReport for %s\n", 
		getSaNameT (&req_exec_amf_errorreport->req_lib_amf_errorreport.erroneousComponent));

	component = findComponent (&req_exec_amf_errorreport->req_lib_amf_errorreport.erroneousComponent);
	if (component && component->registered) {
		component->probableCause = req_exec_amf_errorreport->req_lib_amf_errorreport.errorDescriptor.probableCause;

		/*
		 * One registered component left, so transition
		 * SU to failed operational state
		 */
		dsmEnabledUnlockedTransitionDisabledUnlocked (component);
		error = SA_OK;
	}

	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (req_exec_amf_errorreport->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		log_printf (LOG_LEVEL_DEBUG, "sending error report response to fd %d\n",
			req_exec_amf_errorreport->source.conn_info->fd);

		res_lib_amf_errorreport.header.size = sizeof (struct res_lib_amf_errorreport);
		res_lib_amf_errorreport.header.id = MESSAGE_RES_AMF_ERRORREPORT;
		res_lib_amf_errorreport.header.error = error;

		libais_send_response (req_exec_amf_errorreport->source.conn_info,
			&res_lib_amf_errorreport, sizeof (struct res_lib_amf_errorreport));
	}

	return (0);
}

static int message_handler_req_exec_amf_errorcancelall (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_errorcancelall *req_exec_amf_errorcancelall = (struct req_exec_amf_errorcancelall *)message;
	struct res_lib_amf_errorcancelall res_lib_amf_errorcancelall;
	struct saAmfComponent *component;
	SaErrorT error = SA_ERR_BAD_OPERATION;

	log_printf (LOG_LEVEL_DEBUG, "Executive: ErrorCancelAll for %s\n", 
		getSaNameT (&req_exec_amf_errorcancelall->req_lib_amf_errorcancelall.compName));

	component = findComponent (&req_exec_amf_errorcancelall->req_lib_amf_errorcancelall.compName);
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
		error = SA_OK;
	}
	
	/*
	 * If this node originated the request to the cluster, respond back
	 * to the AMF library
	 */
	if (req_exec_amf_errorcancelall->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		log_printf (LOG_LEVEL_DEBUG, "sending error report response to fd %d\n",
			req_exec_amf_errorcancelall->source.conn_info->fd);

		res_lib_amf_errorcancelall.header.size = sizeof (struct res_lib_amf_errorcancelall);
		res_lib_amf_errorcancelall.header.id = MESSAGE_RES_AMF_ERRORCANCELALL;
		res_lib_amf_errorcancelall.header.error = error;

		libais_send_response (req_exec_amf_errorcancelall->source.conn_info,
			&res_lib_amf_errorcancelall, sizeof (struct res_lib_amf_errorcancelall));
	}

	return (0);
}

/*
 * If receiving this message from another cluster node, another cluster node
 * has selected a readiness state for a component connected to _that_ cluster
 * node.  That cluster node API has verified the readiness state, so its time to let
 * the rest of the cluster nodes know about the readiness state change.
 */
static int message_handler_req_exec_amf_readinessstateset (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_readinessstateset *req_exec_amf_readinessstateset = (struct req_exec_amf_readinessstateset *)message;
	struct saAmfComponent *component;

	component = findComponent (&req_exec_amf_readinessstateset->compName);
	if (component) {
	  	log_printf (LOG_LEVEL_DEBUG, "found component %s, setting current readiness state to %d\n",
			getSaNameT (&component->name),
			req_exec_amf_readinessstateset->readinessState);

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
static int message_handler_req_exec_amf_hastateset (void *message, struct in_addr source_addr)
{
	struct req_exec_amf_hastateset *req_exec_amf_hastateset = (struct req_exec_amf_hastateset *)message;
	struct saAmfComponent *component;

	component = findComponent (&req_exec_amf_hastateset->compName);
	if (component) {
	  	log_printf (LOG_LEVEL_DEBUG, "found component %s, setting current HA state to %d\n",
	  			getSaNameT (&component->name),
			req_exec_amf_hastateset->haState);
		component->currentHAState = req_exec_amf_hastateset->haState;
		component->newHAState = component->currentHAState;
		dsm (component);
	}
	
	return (0);
}

static int message_handler_req_amf_init (struct conn_info *conn_info, void *message)
{
	struct res_lib_init res_lib_init;
	SaErrorT error = SA_ERR_SECURITY;

	log_printf (LOG_LEVEL_DEBUG, "Got AMF request to initalize availability management framework service.\n");

	if (conn_info->authenticated) {
		conn_info->service = SOCKET_SERVICE_AMF;
		error = SA_OK;
	}

	res_lib_init.header.size = sizeof (struct res_lib_init);
	res_lib_init.header.id = MESSAGE_RES_INIT;
	res_lib_init.header.error = error;

	libais_send_response (conn_info, &res_lib_init, sizeof (res_lib_init));

	list_init (&conn_info->conn_list);

	if (conn_info->authenticated) {
		return (0);
	}
	return (-1);
}

static int message_handler_req_lib_activatepoll (struct conn_info *conn_info, void *message)
{
	struct res_lib_activatepoll res_lib_activatepoll;

	res_lib_activatepoll.header.size = sizeof (struct res_lib_activatepoll);
	res_lib_activatepoll.header.id = MESSAGE_RES_LIB_ACTIVATEPOLL;
	libais_send_response (conn_info, &res_lib_activatepoll, sizeof (struct res_lib_activatepoll));

	return (0);
}

static int message_handler_req_amf_componentregister (struct conn_info *conn_info, void *message)
{
	struct req_amf_componentregister *req_lib_amf_componentregister = (struct req_amf_componentregister *)message;
	struct req_exec_amf_componentregister req_exec_amf_componentregister;
	struct iovec iovecs[2];

	req_exec_amf_componentregister.header.size = sizeof (struct req_exec_amf_componentregister);
	req_exec_amf_componentregister.header.id = MESSAGE_REQ_EXEC_AMF_COMPONENTREGISTER;

	req_exec_amf_componentregister.source.conn_info = conn_info;
	req_exec_amf_componentregister.source.in_addr.s_addr = this_ip.sin_addr.s_addr;
	memcpy (&req_exec_amf_componentregister.req_lib_amf_componentregister,
		req_lib_amf_componentregister,
		sizeof (struct req_lib_amf_componentregister));

	iovecs[0].iov_base = (char *)&req_exec_amf_componentregister;
	iovecs[0].iov_len = sizeof (req_exec_amf_componentregister);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED) == 0);

	return (0);
}

static int message_handler_req_amf_componentunregister (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_componentunregister *req_lib_amf_componentunregister = (struct req_lib_amf_componentunregister *)message;
	struct req_exec_amf_componentunregister req_exec_amf_componentunregister;
	struct iovec iovecs[2];
	struct saAmfComponent *component;

	req_exec_amf_componentunregister.header.size = sizeof (struct req_exec_amf_componentunregister);
	req_exec_amf_componentunregister.header.id = MESSAGE_REQ_EXEC_AMF_COMPONENTUNREGISTER;

	req_exec_amf_componentunregister.source.conn_info = conn_info;
	req_exec_amf_componentunregister.source.in_addr.s_addr = this_ip.sin_addr.s_addr;
	memcpy (&req_exec_amf_componentunregister.req_lib_amf_componentunregister,
		req_lib_amf_componentunregister,
		sizeof (struct req_lib_amf_componentunregister));

	component = findComponent (&req_lib_amf_componentunregister->compName);
	if (component && component->registered && component->local) {
		component->probableCause = SA_AMF_NOT_RESPONDING;
	}
	iovecs[0].iov_base = (char *)&req_exec_amf_componentunregister;
	iovecs[0].iov_len = sizeof (req_exec_amf_componentunregister);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED) == 0);

	return (0);
}

static int message_handler_req_amf_readinessstateget (struct conn_info *conn_info, void *message)
{
	struct req_amf_readinessstateget *req_amf_readinessstateget = (struct req_amf_readinessstateget *)message;
	struct res_lib_amf_readinessstateget res_lib_amf_readinessstateget;
	struct saAmfComponent *component;

	log_printf (LOG_LEVEL_DEBUG, "got request to return readiness state\n");
	res_lib_amf_readinessstateget.header.id = MESSAGE_RES_AMF_READINESSSTATEGET;
	res_lib_amf_readinessstateget.header.size = sizeof (struct res_lib_amf_readinessstateget);
	res_lib_amf_readinessstateget.header.error = SA_ERR_NOT_EXIST;

	component = findComponent (&req_amf_readinessstateget->compName);
	log_printf (LOG_LEVEL_DEBUG, "readinessstateget: found component %p\n", component);
	if (component) {
		memcpy (&res_lib_amf_readinessstateget.readinessState, 
			&component->currentReadinessState, sizeof (SaAmfReadinessStateT));
		res_lib_amf_readinessstateget.header.error = SA_OK;
	}
	libais_send_response (conn_info, &res_lib_amf_readinessstateget, sizeof (struct res_lib_amf_readinessstateget));
	return (0);
}

static int message_handler_req_amf_hastateget (struct conn_info *conn_info, void *message)
{
	struct req_amf_hastateget *req_amf_hastateget = (struct req_amf_hastateget *)message;
	struct res_lib_amf_hastateget res_lib_amf_hastateget;
	struct saAmfComponent *component;

	res_lib_amf_hastateget.header.id = MESSAGE_RES_AMF_HASTATEGET;
	res_lib_amf_hastateget.header.size = sizeof (struct res_lib_amf_hastateget);
	res_lib_amf_hastateget.header.error = SA_ERR_NOT_EXIST;

	component = findComponentInProtectionGroup (&req_amf_hastateget->csiName, &req_amf_hastateget->compName);

	if (component) {
		memcpy (&res_lib_amf_hastateget.haState, 
			&component->currentHAState, sizeof (SaAmfHAStateT));
		res_lib_amf_hastateget.header.error = SA_OK;
	}
	libais_send_response (conn_info, &res_lib_amf_hastateget, sizeof (struct res_lib_amf_hastateget));
	return (0);
}

static int message_handler_req_amf_protectiongrouptrackstart (struct conn_info *conn_info, void *message)
{
	struct req_amf_protectiongrouptrackstart *req_amf_protectiongrouptrackstart = (struct req_amf_protectiongrouptrackstart *)message;
	struct res_lib_amf_protectiongrouptrackstart res_lib_amf_protectiongrouptrackstart;
	struct libamf_ci_trackentry *track = 0;
	int i;
	struct saAmfProtectionGroup *amfProtectionGroup;

	amfProtectionGroup = findProtectionGroup (&req_amf_protectiongrouptrackstart->csiName);

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
		res_lib_amf_protectiongrouptrackstart.header.error = SA_OK;
	}
	libais_send_response (conn_info, &res_lib_amf_protectiongrouptrackstart,
		sizeof (struct res_lib_amf_protectiongrouptrackstart));

	if (amfProtectionGroup &&
		req_amf_protectiongrouptrackstart->trackFlags & SA_TRACK_CURRENT) {

		sendProtectionGroupNotification (conn_info,
			track->notificationBufferAddress,
			amfProtectionGroup,
			0, 
			0,
			track->trackFlags);

		track->trackFlags &= ~SA_TRACK_CURRENT;
	}
	return (0);
}

static int message_handler_req_amf_protectiongrouptrackstop (struct conn_info *conn_info, void *message)
{
	struct req_amf_protectiongrouptrackstop *req_amf_protectiongrouptrackstop = (struct req_amf_protectiongrouptrackstop *)message;
	struct res_lib_amf_protectiongrouptrackstop res_lib_amf_protectiongrouptrackstop;
	struct libamf_ci_trackentry *track = 0;
	int i;

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
		res_lib_amf_protectiongrouptrackstop.header.error = SA_OK;
	}
	libais_send_response (conn_info, &res_lib_amf_protectiongrouptrackstop,
		sizeof (struct res_lib_amf_protectiongrouptrackstop));

	return (0);
}

static int message_handler_req_amf_errorreport (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_errorreport *req_lib_amf_errorreport = (struct req_lib_amf_errorreport *)message;
	struct req_exec_amf_errorreport req_exec_amf_errorreport;

	struct iovec iovecs[2];

	req_exec_amf_errorreport.header.size = sizeof (struct req_exec_amf_errorreport);
	req_exec_amf_errorreport.header.id = MESSAGE_REQ_EXEC_AMF_ERRORREPORT;

	req_exec_amf_errorreport.source.conn_info = conn_info;
	req_exec_amf_errorreport.source.in_addr.s_addr = this_ip.sin_addr.s_addr;
	memcpy (&req_exec_amf_errorreport.req_lib_amf_errorreport,
		req_lib_amf_errorreport,
		sizeof (struct req_lib_amf_errorreport));

	iovecs[0].iov_base = (char *)&req_exec_amf_errorreport;
	iovecs[0].iov_len = sizeof (req_exec_amf_errorreport);
//	iovecs[0].iov_len = sizeof (req_exec_amf_errorreport) - sizeof (req_lib_amf_errorreport);

//	iovecs[1].iov_base = (char *)&req_lib_amf_errorreport;
//	iovecs[1].iov_len = sizeof (req_lib_amf_errorreport);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED) == 0);

	return (0);
}

static int message_handler_req_amf_errorcancelall (struct conn_info *conn_info, void *message)
{
	struct req_lib_amf_errorcancelall *req_lib_amf_errorcancelall = (struct req_lib_amf_errorcancelall *)message;
	struct req_exec_amf_errorcancelall req_exec_amf_errorcancelall;

	struct iovec iovecs[2];

	req_exec_amf_errorcancelall.header.size = sizeof (struct req_exec_amf_errorcancelall);
	req_exec_amf_errorcancelall.header.id = MESSAGE_REQ_EXEC_AMF_ERRORCANCELALL;

	req_exec_amf_errorcancelall.source.conn_info = conn_info;
	req_exec_amf_errorcancelall.source.in_addr.s_addr = this_ip.sin_addr.s_addr;
	memcpy (&req_exec_amf_errorcancelall.req_lib_amf_errorcancelall,
		req_lib_amf_errorcancelall,
		sizeof (struct req_lib_amf_errorcancelall));

	iovecs[0].iov_base = (char *)&req_exec_amf_errorcancelall;
	iovecs[0].iov_len = sizeof (req_exec_amf_errorcancelall);
//	iovecs[0].iov_len = sizeof (req_exec_amf_errorcancelall) - sizeof (req_lib_amf_errorcancelall);

//	iovecs[1].iov_base = (char *)&req_lib_amf_errorcancelall;
//	iovecs[1].iov_len = sizeof (req_lib_amf_errorcancelall);

	assert (gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED) == 0);

	return (0);
}

static int message_handler_req_amf_stoppingcomplete (struct conn_info *conn_info_notused,
	void *message)
{
	struct req_amf_stoppingcomplete *req_amf_stoppingcomplete = (struct req_amf_stoppingcomplete *)message;

	struct conn_info *inv_conn_info;
	int interface;

	log_printf (LOG_LEVEL_DEBUG, "handling stopping complete\n");

	req_amf_invocation_get_and_destroy (req_amf_stoppingcomplete->invocation,
		&interface, &inv_conn_info);

	inv_conn_info->component->currentReadinessState = inv_conn_info->component->newReadinessState;

	readinessStateSetCluster (inv_conn_info->component, SA_AMF_STOPPING);

	sendProtectionGroupNotifications (inv_conn_info->component,
		SA_AMF_PROTECTION_GROUP_STATE_CHANGE);

	return (0);
}

void response_handler_healthcheckcallback (struct conn_info *conn_info,
	struct req_amf_response *req_amf_response) {

	if (req_amf_response->error == SA_OK) {
		log_printf (LOG_LEVEL_DEBUG, "setting healthcheck ok\n");
		conn_info->component->healthcheck_outstanding = 0;
	}
}

static int message_handler_req_amf_response (struct conn_info *conn_info_nouse, void *message)
{
	struct req_amf_response *req_amf_response = (struct req_amf_response *)message;
	struct conn_info *conn_info;
	int interface;
	int res;

	res = req_amf_invocation_get_and_destroy (req_amf_response->invocation,
		&interface, &conn_info);

	log_printf (LOG_LEVEL_DEBUG, "handling response connection %p interface %x\n", conn_info, interface);
	switch (interface) {
	case MESSAGE_REQ_AMF_RESPONSE_SAAMFHEALTHCHECKCALLBACK:
		response_handler_healthcheckcallback (conn_info, req_amf_response);
		break;

	case MESSAGE_REQ_AMF_RESPONSE_SAAMFREADINESSSTATESETCALLBACK:
		response_handler_readinessstatesetcallback (conn_info, req_amf_response);
		break;

	case MESSAGE_REQ_AMF_RESPONSE_SAAMFCSISETCALLBACK:
		response_handler_csisetcallback (conn_info, req_amf_response);
		break;

	case MESSAGE_REQ_AMF_RESPONSE_SAAMFCSIREMOVECALLBACK:
		break;

	default:
		// TODO
		log_printf (LOG_LEVEL_ERROR, "invalid invocation value %x\n", req_amf_response->invocation);
		break;
	}

	return (0);
}

static int message_handler_req_amf_componentcapabilitymodelget (struct conn_info *conn_info, void *message)
{
	struct req_amf_componentcapabilitymodelget *req_amf_componentcapabilitymodelget = (struct req_amf_componentcapabilitymodelget *)message;
	struct res_lib_amf_componentcapabilitymodelget res_lib_amf_componentcapabilitymodelget;
	struct saAmfComponent *component;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "componentcapabilitymodelget: Retrieve name %s.\n", getSaNameT (&req_amf_componentcapabilitymodelget->compName));
	component = findComponent (&req_amf_componentcapabilitymodelget->compName);
	if (component && component->registered) {
		memcpy (&res_lib_amf_componentcapabilitymodelget.componentCapabilityModel,
			&component->componentCapabilityModel, sizeof (SaAmfComponentCapabilityModelT));
	} else {
		error = SA_ERR_NOT_EXIST;
	}

	res_lib_amf_componentcapabilitymodelget.header.size = sizeof (struct res_lib_amf_componentcapabilitymodelget);
	res_lib_amf_componentcapabilitymodelget.header.id = MESSAGE_RES_AMF_COMPONENTCAPABILITYMODELGET;
	res_lib_amf_componentcapabilitymodelget.header.error = error;
	libais_send_response (conn_info, &res_lib_amf_componentcapabilitymodelget,
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

static char *amf_disabledunlockedstate_ntoa (int state)
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

static char *amf_enabledunlockedstate_ntoa (int state)
{
	static char str[64];
	if (state >= 0 && state < 7) {
		sprintf (str, "%s(%d)", enabled_unlocked_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}
	return (str);
}

static char readiness_state_text[4][32] = {
	"Unknown",
	"SA_AMF_OUT_OF_SERVICE",
	"SA_AMF_IN_SERVICE",
	"SA_AMF_QUIESCED",
};

static char *amf_readinessstate_ntoa (int state)
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
	"SA_AMF_ACTIVE",
	"SA_AMF_STANDBY",
	"SA_AMF_QUIESCED",
};

static char *amf_hastate_ntoa (SaAmfHAStateT state)
{

	static char str[32];

	if (state > 0 && state < 4) {
		sprintf (str, "%s(%d)", ha_state_text[state], state);
	}else{
		sprintf (str, "Unknown(%d)", state);
	}
	return (str);
}

static void amf_dump_comp (struct saAmfComponent *component ,void *data)
{
	char	name[64];
	int	level = LOG_LEVEL_NOTICE;
	data = NULL;

	log_printf (level, "----------------\n" );
	log_printf (level, "registered            = %d\n" ,component->registered);
	log_printf (level, "local                 = %d\n" ,component->local );
	log_printf (level, "source_addr           = %s\n" ,inet_ntoa (component->source_addr));
	memset (name, 0 , sizeof(name));
	memcpy (name, component->name.value, component->name.length);
	log_printf (level, "name                  = %s\n" ,name );
	log_printf (level, "currentReadinessState = %s\n" ,amf_readinessstate_ntoa (component->currentReadinessState));
	log_printf (level, "newReadinessState     = %s\n" ,amf_readinessstate_ntoa (component->newReadinessState));
	log_printf (level, "currentHAState        = %s\n" ,amf_hastate_ntoa (component->currentHAState));
	log_printf (level, "newHAState            = %s\n" ,amf_hastate_ntoa (component->newHAState));
	log_printf (level, "enabledUnlockedState  = %s\n" ,amf_enabledunlockedstate_ntoa (component->enabledUnlockedState));
	log_printf (level, "disabledUnlockedState = %s\n" ,amf_disabledunlockedstate_ntoa (component->disabledUnlockedState));
	log_printf (level, "probableCause         = %d\n" ,component->probableCause );
}

void amf_dump ( )
{
	enumerate_components (amf_dump_comp, NULL);
	fflush (stderr);

	return;
}
