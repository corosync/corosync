/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#ifndef AIS_COROSYNCCFG_H_DEFINED
#define AIS_COROSYNCCFG_H_DEFINED

#include <netinet/in.h>
#include "saAis.h"

typedef SaUint64T corosync_cfg_handle_t;

typedef enum {
	COROSYNC_CFG_ADMINISTRATIVETARGET_SERVICEUNIT = 0,
	COROSYNC_CFG_ADMINISTRATIVETARGET_SERVICEGROUP = 1,
	COROSYNC_CFG_ADMINISTRATIVETARGET_COMPONENTSERVICEINSTANCE = 2,
	COROSYNC_CFG_ADMINISTRATIVETARGET_NODE = 3
} CorosyncCfgAdministrativeTargetT;

typedef enum {
	COROSYNC_CFG_ADMINISTRATIVESTATE_UNLOCKED = 0,
	COROSYNC_CFG_ADMINISTRATIVESTATE_LOCKED = 1,
	COROSYNC_CFG_ADMINISTRATIVESTATE_STOPPING = 2
} CorosyncCfgAdministrativeStateT;

typedef enum {
	COROSYNC_CFG_OPERATIONALSTATE_ENABLED = 1,
	COROSYNC_CFG_OPERATIONALSTATE_DISABLED = 2
} CorosyncCfgOperationalStateT;

typedef enum {
	COROSYNC_CFG_READINESSSTATE_OUTOFSERVICE = 1,
	COROSYNC_CFG_READINESSSTATE_INSERVICE = 2,
	COROSYNC_CFG_READINESSSTATE_STOPPING = 3
} CorosyncCfgReadinessStateT;

typedef enum {
	COROSYNC_CFG_PRESENCESTATE_UNINSTANTIATED = 1,
	COROSYNC_CFG_PRESENCESTATE_INSTANTIATING = 2,
	COROSYNC_CFG_PRESENCESTATE_INSTANTIATED = 3,
	COROSYNC_CFG_PRESENCESTATE_TERMINATING = 4,
	COROSYNC_CFG_PRESENCESTATE_RESTARTING = 5,
	COROSYNC_CFG_PRESENCESTATE_INSTANTIATION_FAILED = 6,
	COROSYNC_CFG_PRESENCESTATE_TERMINATION_FAILED = 7
} CorosyncCfgPresenceStateT;

typedef enum {
	COROSYNC_CFG_STATETYPE_OPERATIONAL = 0,
	COROSYNC_CFG_STATETYPE_ADMINISTRATIVE = 1,
	COROSYNC_CFG_STATETYPE_READINESS = 2,
	COROSYNC_CFG_STATETYPE_HA = 3,
	COROSYNC_CFG_STATETYPE_PRESENCE = 4
} CorosyncCfgStateTypeT;

/* Shutdown types.
   REQUEST is the normal shutdown. other daemons will be consulted
   REGARDLESS will tell other daemons but ignore their opinions
   IMMEDIATE will shut down straight away (but still tell other nodes)
*/
typedef enum {
	COROSYNC_CFG_SHUTDOWN_FLAG_REQUEST = 0,
	COROSYNC_CFG_SHUTDOWN_FLAG_REGARDLESS = 1,
	COROSYNC_CFG_SHUTDOWN_FLAG_IMMEDIATE = 2,
} CorosyncCfgShutdownFlagsT;

typedef enum {
	COROSYNC_CFG_SHUTDOWN_FLAG_NO = 0,
	COROSYNC_CFG_SHUTDOWN_FLAG_YES = 1,
} CorosyncCfgShutdownReplyFlagsT;

typedef struct {
	SaNameT name;
	CorosyncCfgStateTypeT stateType;
	CorosyncCfgAdministrativeStateT administrativeState;
} CorosyncCfgStateNotificationT;

typedef struct {
        SaUint32T numberOfItems;
        CorosyncCfgStateNotificationT *notification;
} CorosyncCfgStateNotificationBufferT;

typedef void (*CorosyncCfgStateTrackCallbackT) (
	CorosyncCfgStateNotificationBufferT *notificationBuffer,
	SaAisErrorT error);

typedef void (*CorosyncCfgShutdownCallbackT) (
	corosync_cfg_handle_t cfg_handle,
	CorosyncCfgShutdownFlagsT flags);

typedef struct {
	CorosyncCfgStateTrackCallbackT
		corosyncCfgStateTrackCallback;
	CorosyncCfgShutdownCallbackT
		corosyncCfgShutdownCallback;
} CorosyncCfgCallbacksT;

/*
 * Interfaces
 */
#ifdef __cplusplus
extern "C" {
#endif

SaAisErrorT
corosync_cfg_initialize (
	corosync_cfg_handle_t *cfg_handle,
	const CorosyncCfgCallbacksT *cfgCallbacks);

SaAisErrorT
corosync_cfg_fd_get (
	corosync_cfg_handle_t cfg_handle,
	SaSelectionObjectT *selectionObject);

SaAisErrorT
corosync_cfg_dispatch (
	corosync_cfg_handle_t cfg_handle,
	SaDispatchFlagsT dispatchFlags);

SaAisErrorT
corosync_cfg_finalize (
	corosync_cfg_handle_t cfg_handle);

SaAisErrorT
corosync_cfg_ring_status_get (
	corosync_cfg_handle_t cfg_handle,
	char ***interface_names,
	char ***status,
	unsigned int *interface_count);

SaAisErrorT
corosync_cfg_ring_reenable (
	corosync_cfg_handle_t cfg_handle);

SaAisErrorT
corosync_cfg_service_load (
	corosync_cfg_handle_t cfg_handle,
	char *service_name,
	unsigned int service_ver);

SaAisErrorT
corosync_cfg_service_unload (
	corosync_cfg_handle_t cfg_handle,
	char *service_name,
	unsigned int service_ver);

SaAisErrorT
corosync_cfg_administrative_state_get (
	corosync_cfg_handle_t cfg_handle,
	CorosyncCfgAdministrativeTargetT administrativeTarget,
	CorosyncCfgAdministrativeStateT *administrativeState);

SaAisErrorT
corosync_cfg_administrative_state_set (
	corosync_cfg_handle_t cfg_handle,
	CorosyncCfgAdministrativeTargetT administrativeTarget,
	CorosyncCfgAdministrativeStateT administrativeState);

SaAisErrorT
corosync_cfg_kill_node (
	corosync_cfg_handle_t cfg_handle,
	unsigned int nodeid,
	char *reason);

SaAisErrorT
corosync_cfg_try_shutdown (
	corosync_cfg_handle_t cfg_handle,
	CorosyncCfgShutdownFlagsT flags);


SaAisErrorT
corosync_cfg_replyto_shutdown (
	corosync_cfg_handle_t cfg_handle,
	CorosyncCfgShutdownReplyFlagsT flags);

SaAisErrorT
corosync_cfg_state_track (
        corosync_cfg_handle_t cfg_handle,
        SaUint8T trackFlags,
        const CorosyncCfgStateNotificationT *notificationBuffer);

SaAisErrorT
corosync_cfg_state_track_stop (
        corosync_cfg_handle_t cfg_handle);

#ifdef __cplusplus
}
#endif

#endif /* AIS_COROSYNCCFG_H_DEFINED */
