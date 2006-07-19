/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
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
#ifndef AIS_OPENAISCFG_H_DEFINED
#define AIS_OPENAISCFG_H_DEFINED

#include <netinet/in.h>
#include "saAis.h"

typedef SaUint64T openais_cfg_handle_t;

typedef enum {
	OPENAIS_CFG_ADMINISTRATIVETARGET_SERVICEUNIT = 0,
	OPENAIS_CFG_ADMINISTRATIVETARGET_SERVICEGROUP = 1,
	OPENAIS_CFG_ADMINISTRATIVETARGET_COMPONENTSERVICEINSTANCE = 2,
	OPENAIS_CFG_ADMINISTRATIVETARGET_NODE = 3
} OpenaisCfgAdministrativeTargetT;

typedef enum {
	OPENAIS_CFG_ADMINISTRATIVESTATE_UNLOCKED = 0,
	OPENAIS_CFG_ADMINISTRATIVESTATE_LOCKED = 1,
	OPENAIS_CFG_ADMINISTRATIVESTATE_STOPPING = 2
} OpenaisCfgAdministrativeStateT;

typedef enum {
	OPENAIS_CFG_OPERATIONALSTATE_ENABLED = 1,
	OPENAIS_CFG_OPERATIONALSTATE_DISABLED = 2
} OpenaisCfgOperationalStateT;

typedef enum {
	OPENAIS_CFG_READINESSSTATE_OUTOFSERVICE = 1,
	OPENAIS_CFG_READINESSSTATE_INSERVICE = 2,
	OPENAIS_CFG_READINESSSTATE_STOPPING = 3
} OpenaisCfgReadinessStateT;

typedef enum {
	OPENAIS_CFG_PRESENCESTATE_UNINSTANTIATED = 1,
	OPENAIS_CFG_PRESENCESTATE_INSTANTIATING = 2,
	OPENAIS_CFG_PRESENCESTATE_INSTANTIATED = 3,
	OPENAIS_CFG_PRESENCESTATE_TERMINATING = 4,
	OPENAIS_CFG_PRESENCESTATE_RESTARTING = 5,
	OPENAIS_CFG_PRESENCESTATE_INSTANTIATION_FAILED = 6,
	OPENAIS_CFG_PRESENCESTATE_TERMINATION_FAILED = 7
} OpenaisCfgPresenceStateT;

typedef enum {
	OPENAIS_CFG_STATETYPE_OPERATIONAL = 0,
	OPENAIS_CFG_STATETYPE_ADMINISTRATIVE = 1,
	OPENAIS_CFG_STATETYPE_READINESS = 2,
	OPENAIS_CFG_STATETYPE_HA = 3,
	OPENAIS_CFG_STATETYPE_PRESENCE = 4
} OpenaisCfgStateTypeT;

typedef struct {
	SaNameT name;
	OpenaisCfgStateTypeT stateType;
	OpenaisCfgAdministrativeStateT administrativeState;
} OpenaisCfgStateNotificationT;

typedef struct {
        SaUint32T numberOfItems;
        OpenaisCfgStateNotificationT *notification;
} OpenaisCfgStateNotificationBufferT;

typedef void (*OpenaisCfgStateTrackCallbackT) (
	OpenaisCfgStateNotificationBufferT *notificationBuffer,
	SaAisErrorT error);

typedef struct {
	OpenaisCfgStateTrackCallbackT
		openaisCfgStateTrackCallback;
} OpenaisCfgCallbacksT;

/*
 * Interfaces
 */
#ifdef __cplusplus
extern "C" {
#endif

SaAisErrorT
openais_cfg_initialize (
	openais_cfg_handle_t *cfg_handle,
	const OpenaisCfgCallbacksT *cfgCallbacks);

SaAisErrorT
openais_cfg_fd_get (
	openais_cfg_handle_t cfg_handle,
	SaSelectionObjectT *selectionObject);

SaAisErrorT
openais_cfg_dispatch (
	openais_cfg_handle_t cfg_handle,
	SaDispatchFlagsT dispatchFlags);

SaAisErrorT
openais_cfg_finalize (
	openais_cfg_handle_t cfg_handle);

SaAisErrorT
openais_cfg_ring_status_get (
	openais_cfg_handle_t cfg_handle,
	char ***interface_names,
	char ***status,
	unsigned int *interface_count);

SaAisErrorT
openais_cfg_ring_reenable (
	openais_cfg_handle_t cfg_handle);

SaAisErrorT
openais_cfg_administrative_state_get (
	openais_cfg_handle_t cfg_handle,
	OpenaisCfgAdministrativeTargetT administrativeTarget,
	OpenaisCfgAdministrativeStateT *administrativeState);

SaAisErrorT
openais_cfg_administrative_state_set (
	openais_cfg_handle_t cfg_handle,
	OpenaisCfgAdministrativeTargetT administrativeTarget,
	OpenaisCfgAdministrativeStateT administrativeState);

SaAisErrorT
openais_cfg_state_track (
        openais_cfg_handle_t cfg_handle,
        SaUint8T trackFlags,
        const OpenaisCfgStateNotificationT *notificationBuffer);

SaAisErrorT
openais_cfg_state_track_stop (
        openais_cfg_handle_t cfg_handle);

#ifdef __cplusplus
}
#endif

#endif /* AIS_OPENAISCFG_H_DEFINED */
