/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt
 * Description: Reworked to match AMF B.02 information model
 *
 * All rights reserved.
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

#include <limits.h>
#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/list.h"
#include "aispoll.h"

#ifndef AMFCONFIG_H_DEFINED
#define AMFCONFIG_H_DEFINED


enum escalation_levels {
	ESCALATION_LEVEL_NO_ESCALATION = 1,	/* execute component restart */
	ESCALATION_LEVEL_ONE = 2,		/* escalate to service unit restart */
	ESCALATION_LEVEL_TWO = 3,		/* escalate to service unit failover */
	ESCALATION_LEVEL_THREE = 4		/* escalate to node failover */
};

enum clc_component_types {
	clc_component_sa_aware = 0,			/* sa aware */
	clc_component_proxied_pre = 1,			/* proxied, pre-instantiable */
	clc_component_proxied_non_pre = 2,		/* proxied, non pre-instantiable */
	clc_component_non_proxied_non_sa_aware = 3	/* non-proxied, non sa aware */
};

struct amf_si_assignment;
struct amf_csi_assignment;
struct amf_healthcheck;

struct amf_cluster {
	/* Configuration Attributes */
	SaNameT name;
	int saAmfClusterStartupTimeout;
	SaNameT saAmfClusterClmCluster;

	/* Runtime Attributes */
	SaAmfAdminStateT saAmfClusterAdminState;

	/* Relations */
	struct amf_node *node_head;
	struct amf_application *application_head;

	/* Implementation */
	poll_timer_handle timeout_handle;
};

struct amf_node {
	/* Configuration Attributes */
	SaNameT name;
	SaNameT saAmfNodeClmNode;
	int saAmfNodeSuFailOverProb;
	SaUint32T saAmfNodeSuFailoverMax;
	SaBoolT saAmfNodeAutoRepair;
	SaBoolT saAmfNodeRebootOnInstantiationFailure;
	SaBoolT saAmfNodeRebootOnTerminationFailure;

	/* Runtime Attributes */
	SaAmfAdminStateT saAmfNodeAdminState;
	SaAmfOperationalStateT saAmfNodeOperState;

	/* Relations */
	struct amf_cluster *cluster;

	/* Implementation */
	struct amf_node *next;
};

struct amf_application {
	/* Configuration Attributes */
	SaNameT name;

	/* Runtime Attributes */
	SaAmfAdminStateT saAmfApplicationAdminState;
	SaUint32T saAmfApplicationCurrNumSG;

	/* Relations */
	struct amf_cluster *cluster;
	struct amf_sg      *sg_head;
	struct amf_si      *si_head;

	/* Implementation */
	char clccli_path[PATH_MAX];
	char binary_path[PATH_MAX];
	struct amf_application *next;
};

struct amf_sg {
	/* Configuration Attributes */
	SaNameT name;
	saAmfRedundancyModelT saAmfSGRedundancyModel;
	SaBoolT saAmfSGAutoAdjust;
	SaUint32T saAmfSGNumPrefActiveSUs;
	SaUint32T saAmfSGNumPrefStandbySUs;
	SaUint32T saAmfSGNumPrefInserviceSUs;
	SaUint32T saAmfSGNumPrefAssignedSUs;
	SaUint32T saAmfSGMaxActiveSIsperSUs;
	SaUint32T saAmfSGMaxStandbySIsperSUs;
	SaTimeT saAmfSGCompRestartProb;
	SaUint32T saAmfSGCompRestartMax;
	SaTimeT saAmfSGSuRestartProb;
	SaUint32T saAmfSGSuRestartMax;
	SaTimeT saAmfSGAutoAdjustProb;
	SaBoolT saAmfSGAutoRepair;

	/* Runtime Attributes */
	SaAmfAdminStateT saAmfSGAdminState;
	SaUint32T saAmfSGNumCurrAssignedSUs;
	SaUint32T saAmfSGNumCurrNonInstantiatedSpareSUs;
	SaUint32T saAmfSGNumCurrInstantiatedSpareSUs;

	/* Relations */
	struct amf_application *application;
	struct amf_su          *su_head;

	/* Implementation */
	char clccli_path[PATH_MAX];
	char binary_path[PATH_MAX];
	struct amf_sg *next;
};

struct amf_su {
	/* Configuration Attributes */
	SaNameT name;
	SaUint32T saAmfSURank;
	SaUint32T saAmfSUNumComponents;
	SaBoolT saAmfSUIsExternal;
	SaBoolT saAmfSUFailover;

	/* Runtime Attributes */
	SaBoolT saAmfSUPreInstantiable;
	SaAmfOperationalStateT saAmfSUOperState;
	SaAmfAdminStateT saAmfSUAdminState;
	SaAmfReadinessStateT saAmfSUReadinessState;
	SaAmfPresenceStateT saAmfSUPresenceState;
	SaNameT saAmfSUAssignedSIs;
	SaNameT saAmfSUHostedByNode;
	SaUint32T saAmfSUNumCurrActiveSIs;
	SaUint32T saAmfSUNumCurrStandbySIs;
	SaUint32T saAmfSURestartCount;

	/* Relations */
	struct amf_sg *sg;
	struct amf_comp *comp_head;
	struct amf_si_assignment *assigned_sis;

	/* Implementation */
	char clccli_path[PATH_MAX];
	char binary_path[PATH_MAX];
	SaUint32T              su_failover_cnt; /* missing in SAF specs? */
	enum escalation_levels escalation_level;
	SaAmfHAStateT          requested_ha_state;
	struct amf_su         *next;
};

struct amf_comp {
	/* Configuration Attributes */
	SaNameT name;
	SaNameT **saAmfCompCsTypes;
	saAmfCompCategoryT saAmfCompCategory;
	saAmfCompCapabilityModelT saAmfCompCapability;
	SaUint32T saAmfCompNumMaxActiveCsi;
	SaUint32T saAmfCompNumMaxStandbyCsi;
	SaStringT *saAmfCompCmdEnv;
	int saAmfCompDefaultClcCliTimeout;
	int saAmfCompDefaultCallbackTimeOut;
	SaStringT saAmfCompInstantiateCmd;
	SaStringT saAmfCompInstantiateCmdArgv;
	int saAmfCompInstantiateTimeout;
	SaUint32T saAmfCompInstantiationLevel;
	SaUint32T saAmfCompNumMaxInstantiateWithoutDelay;
	SaUint32T saAmfCompNumMaxInstantiateWithDelay;
	int saAmfCompDelayBetweenInstantiateAttempts;
	SaStringT saAmfCompTerminateCmd;
	int saAmfCompTerminateTimeout;
	SaStringT saAmfCompTerminateCmdArgv;
	SaStringT saAmfCompCleanupCmd;
	int saAmfCompCleanupTimeout;
	SaStringT saAmfCompCleanupCmdArgv;
	SaStringT saAmfCompAmStartCmd;
	int saAmfCompAmStartTimeout;
	SaStringT saAmfCompAmStartCmdArgv;
	SaUint32T saAmfCompNumMaxAmStartAttempt;
	SaStringT saAmfCompAmStopCmd;
	int saAmfCompAmStopTimeout;
	SaStringT saAmfCompAmStopCmdArgv;
	SaUint32T saAmfCompNumMaxAmStopAttempt;
	int saAmfCompTerminateCallbackTimeout;
	int saAmfCompCSISetCallbackTimeout;
	int saAmfCompQuiescingCompleteTimeout;
	int saAmfCompCSIRmvCallbackTimeout;
	SaAmfRecommendedRecoveryT saAmfCompRecoveryOnError;
	SaBoolT saAmfCompDisableRestart;
	SaNameT saAmfCompProxyCsi;

	/* Runtime Attributes */
	SaAmfOperationalStateT saAmfCompOperState;
	SaAmfReadinessStateT saAmfCompReadinessState;
	SaAmfPresenceStateT saAmfCompPresenceState;
	SaUint32T saAmfCompRestartCount;
	SaUint32T saAmfCompNumCurrActiveCsi;
	SaUint32T saAmfCompNumCurrStandbyCsi;
	SaNameT saAmfCompAssignedCsi;
	SaNameT saAmfCompCurrProxyName;
	SaNameT saAmfCompCurrProxiedNames;

	/* Relations */
	struct amf_comp *proxy_comp;
	struct amf_su *su;
	struct amf_csi_assignment *assigned_csis;

	/* Implementation */
	char clccli_path[PATH_MAX];
	char binary_path[PATH_MAX];
	struct amf_comp *next;
	void *conn;
	enum clc_component_types comptype;
	struct amf_healthcheck *healthcheck_head;
};

struct amf_healthcheck {
	/* Configuration Attributes */
	SaAmfHealthcheckKeyT safHealthcheckKey;
	int saAmfHealthcheckMaxDuration;
	int saAmfHealthcheckPeriod;

	/* Relations */
	struct amf_comp *comp;

	/* Implementation */
	struct amf_healthcheck *next;
	SaAmfHealthcheckInvocationT invocationType;
	poll_timer_handle timer_handle_duration;
	poll_timer_handle timer_handle_period;
	int active;
};

struct amf_si {
	/* Configuration Attributes */
	SaNameT name;
	SaNameT saAmfSIProtectedbySG;
	SaUint32T saAmfSIRank;
	SaUint32T saAmfSINumCSIs;
	SaUint32T saAmfSIPrefActiveAssignments;
	SaUint32T saAmfSIPrefStandbyAssignments;

	/* Runtime Attributes */
	SaAmfAdminStateT saAmfSIAdminState;
	SaAmfAssignmentStateT saAmfSIAssignmentState;
	SaUint32T saAmfSINumCurrActiveAssignments;
	SaUint32T saAmfSINumICurrStandbyAssignments;

	/* Relations */
	struct amf_application   *application;
	struct amf_sg            *protects_sg;
	struct amf_csi           *csi_head;
	struct amf_si_assignment *si_assignments;
	struct amf_si_dependency *depends_on;
	struct amf_si_ranked_su  *ranked_sus;

	/* Implementation */
	struct amf_si *next;
};

struct amf_si_ranked_su {
	/* Configuration Attributes */
	SaNameT name;
	SaUint32T saAmfRank;

	/* Relations */
	struct amf_si *si;
	struct amf_su *su;

	/* Implementation */
	struct amf_si_ranked_su *su_next;
	struct amf_si_ranked_su *si_next;
};

struct amf_si_dependency {
	/* Configuration Attributes */
	SaNameT name;
	int saAmfToleranceTime;

	/* Relations */

	/* Implementation */
	struct amf_si_dependency *next;
};

struct amf_si_assignment {
	/* Runtime Attributes */
	SaNameT name;
	SaAmfHAStateT saAmfSISUHAState;

	/* Relations */

	/* Implementation */
};

struct amf_csi {
	/* Configuration Attributes */
	SaNameT name;
	SaNameT saAmfCSTypeName;
	SaNameT **saAmfCSIDependencies;

	/* Relations */
	struct amf_si *si;
	struct amf_csi_assignment *csi_assignments;
	struct amf_csi_attribute *attributes_head;

	/* Implementation */
	struct amf_csi *next;
	int pg_set;
};

struct amf_csi_attribute {
	/* Configuration Attributes */
	SaStringT name;
	SaStringT *value;

	/* Implementation */
	struct amf_csi_attribute *next;
};

struct amf_csi_assignment {
	/* Runtime Attributes */
	SaNameT name;
	SaAmfHAStateT saAmfCSICompHASate;

	/* Relations */
	struct amf_csi  *csi;
	struct amf_comp *comp;

	/* Implementation */
	struct amf_csi_assignment *comp_next;
	struct amf_csi_assignment *csi_next;
};

extern struct amf_comp *amf_find_comp (struct amf_cluster *cluster, SaNameT *name);
extern struct amf_su *amf_find_unit (struct amf_cluster *cluster, SaNameT *name);
extern struct amf_healthcheck *amf_find_healthcheck (struct amf_comp *comp, SaAmfHealthcheckKeyT *key);
extern int amf_config_read (struct amf_cluster *cluster, char **error_string);

#endif /* AMFCONFIG_H_DEFINED */
