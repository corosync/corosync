/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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
#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/list.h"
#include "aispoll.h"
#include "../include/openaisCfg.h"

#ifndef AMFCONFIG_H_DEFINED
#define AMFCONFIG_H_DEFINED

typedef enum {
	GROUPCAPABILITYMODEL_2N,
	GROUPCAPABILITYMODEL_NPLUSM,
	GROUPCAPABILITYMODEL_NWAY,
	GROUPCAPABILITYMODEL_NWAYACTIVE,
	GROUPCAPABILITYMODEL_NOREDUNDANCY
} SaAmfGroupCapabilityModelT;

#ifdef COMPILE_OUT
enum amfOperationalAdministrativeState {
	AMF_DISABLED_UNLOCKED = 0,
	AMF_DISABLED_LOCKED = 1,
	AMF_ENABLED_UNLOCKED = 2,
	AMF_ENABLED_STOPPING = 3
};
#endif

struct amf_healthcheck {
	SaAmfHealthcheckKeyT key;
	SaTimeT period;
	SaTimeT maximum_duration;
	struct amf_comp *comp;
	struct list_head list;
};

enum escalation_levels {
	ESCALATION_LEVEL_NO_ESCALATION = 1,	/* execute component restart */
        ESCALATION_LEVEL_ONE = 2,		/* escalate to service unit restart */
        ESCALATION_LEVEL_TWO = 3,		/* escalate to service unit failover */
        ESCALATION_LEVEL_THREE = 4		/* escalate to node failover */
};

struct amf_unit {
	SaNameT name;
	struct list_head comp_head;
	struct list_head unit_list;
	struct amf_group *amf_group;
	struct list_head si_head;
	struct list_head si_list;
	int si_count;
	OpenaisCfgPresenceStateT presence_state;
	OpenaisCfgOperationalStateT operational_state;
	OpenaisCfgReadinessStateT readiness_state;
	SaAmfHAStateT assigned_ha_state;
	SaAmfHAStateT requested_ha_state;

	unsigned char clccli_path[1024];
	unsigned char binary_path[1024];

	poll_timer_handle restart_timer;
	int restart_count;
	enum escalation_levels escalation_level;
};

struct amf_si;
struct amf_csi {
	SaNameT name;
	struct list_head list;
	struct amf_unit *unit;
	struct list_head csi_list;
	struct list_head unit_head;
	struct amf_si *si;
	int pg_set;
};

struct amf_si {
	SaNameT name;
	int csi_count;
	struct list_head list;
	struct amf_group *group;
	struct list_head csi_head;
	struct list_head pg_head;
	struct list_head unit_list;
	OpenaisCfgAdministrativeStateT administrative_state;
	OpenaisCfgOperationalStateT operational_state;
};

struct amf_pg {
	SaNameT name;
	struct amf_comp *comp;
	struct list_head pg_list;
	struct list_head pg_comp_head;
};

struct amf_pg_comp {
	struct amf_comp *comp;
	struct amf_csi *csi;
	struct list_head list;
};

struct amf_group {
	SaNameT name;
	SaAmfGroupCapabilityModelT model;
	SaUint32T preferred_active_units;
	SaUint32T preferred_standby_units;
	SaUint32T maximum_active_instances;
	SaUint32T maximum_standby_instances;
	OpenaisCfgAdministrativeStateT administrativeState;
	struct list_head group_list;
	struct list_head unit_head;
	struct list_head si_head;

	unsigned char clccli_path[1024];
	unsigned char binary_path[1024];

	unsigned int component_restart_probation;
	unsigned int component_restart_max;

	unsigned int unit_restart_probation;
	unsigned int unit_restart_max;
};

enum clc_component_types {
	clc_component_sa_aware = 0,			/* sa aware */
	clc_component_proxied_pre = 1,			/* proxied, pre-instantiable */
	clc_component_proxied_non_pre = 2,		/* proxied, non pre-instantiable */
	clc_component_non_proxied_non_sa_aware = 3	/* non-proxied, non sa aware */
};

struct amf_comp {
	int registered;
	int local;
	void *conn;
	SaNameT name;
	struct in_addr source_addr;
	struct amf_comp *proxy_comp;
	struct amf_unit *unit;
	struct amf_pg *pg;
	struct amf_csi *csi;

	struct list_head comp_list;
	struct list_head healthcheck_list;

	enum clc_component_types comptype;

	unsigned char clccli_path[1024];
	unsigned char binary_path[1024];
	unsigned char binary_name[1024];

	unsigned char instantiate_cmd[1024];
	unsigned char terminate_cmd[1024];
	unsigned char cleanup_cmd[1024];
	unsigned char am_start_cmd[1024];
	unsigned char am_stop_cmd[1024];
	
	OpenaisCfgAdministrativeStateT administrative_state;
	OpenaisCfgOperationalStateT operational_state;
	OpenaisCfgReadinessStateT readiness_state;
	SaAmfHAStateT ha_state;
	OpenaisCfgPresenceStateT presence_state;
};

extern struct list_head amf_group_head;

extern struct amf_comp *find_comp (SaNameT *name);

extern struct amf_unit *find_unit (SaNameT *name);

extern struct amf_healthcheck *find_healthcheck (SaAmfHealthcheckKeyT *key);

#endif /* AMFCONFIG_H_DEFINED */
