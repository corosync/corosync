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
#include <netinet/in.h>
#include "../include/ais_types.h"
#include "../include/list.h"
#include "aispoll.h"
#include "gmi.h"

#ifndef PARSE_H_DEFINED
#define PARSE_H_DEFINED

typedef enum {
	GROUPCAPABILITYMODEL_2N,
	GROUPCAPABILITYMODEL_NPLUSM,
	GROUPCAPABILITYMODEL_NWAY,
	GROUPCAPABILITYMODEL_NWAYACTIVE,
	GROUPCAPABILITYMODEL_NOREDUNDANCY
} SaAmfGroupCapabilityModelT;

enum amfOperationalAdministrativeState {
	AMF_DISABLED_UNLOCKED = 0,
	AMF_DISABLED_LOCKED = 1,
	AMF_ENABLED_UNLOCKED = 2,
	AMF_ENABLED_STOPPING = 3
};

struct saAmfUnit {
	SaNameT name;
	struct list_head saAmfComponentHead;
	struct list_head saAmfUnitList;
	enum amfOperationalAdministrativeState operationalAdministrativeState;
	struct saAmfGroup *saAmfGroup;
};

struct saAmfProtectionGroup {
	SaNameT name;
	struct list_head saAmfMembersHead;
	struct list_head saAmfProtectionGroupList;
};

struct saAmfGroup {
	SaNameT name;
	SaAmfGroupCapabilityModelT model;
	SaUint32T saAmfActiveUnitsDesired;
	SaUint32T saAmfStandbyUnitsDesired;
	struct list_head saAmfGroupList;
	struct list_head saAmfProtectionGroupHead;
	struct list_head saAmfUnitHead;
};

/*
 * State machines for states in AMF
 */
enum amfOperationalState {
	AMF_OPER_DISABLED,
	AMF_OPER_ENABLED
};

enum amfAdministrativeState {
	AMF_ADMIN_UNLOCKED,
	AMF_ADMIN_LOCKED,
	AMF_ADMIN_STOPPING
};

enum amfEnabledUnlockedState {
	AMF_ENABLED_UNLOCKED_INITIAL = 0,
	AMF_ENABLED_UNLOCKED_IN_SERVICE_REQUESTED,
	AMF_ENABLED_UNLOCKED_IN_SERVICE_COMPLETED,
	AMF_ENABLED_UNLOCKED_ACTIVE_REQUESTED,
	AMF_ENABLED_UNLOCKED_ACTIVE_COMPLETED,
	AMF_ENABLED_UNLOCKED_STANDBY_REQUESTED,
	AMF_ENABLED_UNLOCKED_STANDBY_COMPLETED
};

enum amfDisabledUnlockedState {
	AMF_DISABLED_UNLOCKED_REGISTEREDORERRORCANCEL = 0,
	AMF_DISABLED_UNLOCKED_FAILED,
	AMF_DISABLED_UNLOCKED_QUIESCED_REQUESTED,
	AMF_DISABLED_UNLOCKED_QUIESCED_COMPLETED,
	AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_REQUESTED,
	AMF_DISABLED_UNLOCKED_OUT_OF_SERVICE_COMPLETED
};
	
enum amfDisabledLockedState {
	AMF_DISABLED_LOCKED_INITIAL = 0,
	AMF_DISABLED_LOCKED_QUIESCED_REQUESTED,
	AMF_DISABLED_LOCKED_QUIESCED_COMPLETED,
	AMF_DISABLED_LOCKED_OUT_OF_SERVICE_REQUESTED,
	AMF_DISABLED_LOCKED_OUT_OF_SERVICE_COMPLETED
};

enum amfEnabledStoppingState {
	AMF_ENABLED_STOPPING_INITIAL = 0,
	AMF_ENABLED_STOPPING_STOPPING_REQUESTED,
	AMF_ENABLED_STOPPING_STOPPING_COMPLETED
};

struct saAmfComponent {
	int registered;
	int local;
	struct conn_info *conn_info;
	SaNameT name;
	SaAmfReadinessStateT currentReadinessState;
	SaAmfReadinessStateT newReadinessState;
	SaAmfHAStateT currentHAState;
	SaAmfHAStateT newHAState;
	enum amfEnabledUnlockedState enabledUnlockedState;
	enum amfDisabledUnlockedState disabledUnlockedState;
	SaAmfComponentCapabilityModelT componentCapabilityModel;
	SaAmfProbableCauseT probableCause;
	int healthcheckInterval;
	poll_timer_handle timer_healthcheck;
	int healthcheck_outstanding;
	struct saAmfComponent *saAmfProxyComponent;
	struct list_head saAmfComponentList;
	struct list_head saAmfProtectionGroupList;
	struct saAmfUnit *saAmfUnit;
	struct saAmfProtectionGroup *saAmfProtectionGroup;
};

extern struct list_head saAmfGroupHead;

extern struct saAmfComponent *findComponent (SaNameT *name);

extern int SaNameTisNameT (SaNameT *name1, SaNameT *name2);

extern int amfReadGroups (char **error_string);

extern int readNetwork (char **error_string,
        struct sockaddr_in *mcast_addr,
        struct gmi_interface *interfaces,
        int interface_count);

#endif /* PARSE_H_DEFINED */
