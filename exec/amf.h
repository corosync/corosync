/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt
 * Description: - Reworked to match AMF B.02 information model
 *              - New state machine design
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

#ifndef AMF_H_DEFINED
#define AMF_H_DEFINED

#include <limits.h>
#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/list.h"
#include "../include/ipc_gen.h"
#include "objdb.h"
#include "timer.h"

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
	openais_timer_handle timeout_handle;
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
	int active;
	SaAmfHealthcheckInvocationT invocationType;
	SaAmfRecommendedRecoveryT recommendedRecovery;
	openais_timer_handle timer_handle_duration;
	openais_timer_handle timer_handle_period;
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
	SaUint32T saAmfSINumCurrStandbyAssignments;

	/* Relations */
	struct amf_application   *application;
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
	struct amf_si *si;

	/* Implementation */
	struct amf_si_assignment *next;
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
	SaAmfHAStateT saAmfCSICompHAState; /* confirmed HA state */

	/* Relations */
	struct amf_csi  *csi;
	struct amf_comp *comp;

	/* Implementation */
	SaAmfHAStateT requested_ha_state;
	struct amf_csi_assignment *comp_next;
	struct amf_csi_assignment *csi_next;
};

enum amf_response_interfaces {
	AMF_RESPONSE_HEALTHCHECKCALLBACK = 1,
	AMF_RESPONSE_CSISETCALLBACK = 2,
	AMF_RESPONSE_CSIREMOVECALLBACK = 3,
	AMF_RESPONSE_COMPONENTTERMINATECALLBACK = 4
};

enum amf_message_req_types {
	MESSAGE_REQ_EXEC_AMF_COMPONENT_REGISTER = 0,
	MESSAGE_REQ_EXEC_AMF_COMPONENT_ERROR_REPORT = 1,
	MESSAGE_REQ_EXEC_AMF_CLC_CLEANUP_COMPLETED = 2,
	MESSAGE_REQ_EXEC_AMF_HEALTHCHECK_TMO = 3,
	MESSAGE_REQ_EXEC_AMF_RESPONSE = 4
};

struct req_exec_amf_clc_cleanup_completed {
	mar_req_header_t header;
	SaNameT compName;
};

struct req_exec_amf_healthcheck_tmo {
	mar_req_header_t header;
	SaNameT compName;
	SaAmfHealthcheckKeyT safHealthcheckKey;
};

/*===========================================================================*/
/* amfutil.c */

extern int amf_config_read (struct amf_cluster *cluster, char **error_string);
extern char *amf_serialize (struct amf_cluster *cluster);
extern int amf_deserialize (char *buf, struct amf_cluster *cluster);
extern void amf_state_print (struct amf_cluster *cluster);
extern void amf_runtime_attributes_print (struct amf_cluster *cluster);
extern int amf_enabled (struct objdb_iface_ver0 *objdb);
extern int amf_invocation_create (int interface, void *data);
extern int amf_invocation_get_and_destroy (
	int invocation, int *interface,	void **data);
extern void amf_invocation_destroy_by_data (void *data);

extern const char *amf_admin_state (int state);
extern const char *amf_op_state (int state);
extern const char *amf_presence_state (int state);
extern const char *amf_ha_state (int state);
extern const char *amf_readiness_state (int state);

/*===========================================================================*/
/* amfnode.c */

/* General methods */
extern struct amf_node *amf_node_create (void);
extern int amf_node_serialize (
	struct amf_node *node, char **buf, int *offset);
extern struct amf_node *amf_node_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);

/* Event methods */
extern void amf_node_sync_ready (struct amf_node *node);
extern void amf_node_leave (struct amf_node *node);
extern void amf_node_failover (struct amf_node *node);
extern void amf_node_switchover (struct amf_node *node);
extern void amf_node_failfast (struct amf_node *node);
extern void amf_node_comp_restart_req (
	struct amf_node *node, struct amf_comp *comp);
extern void amf_node_comp_failover_req (
	struct amf_node *node, struct amf_comp *comp);

enum amf_reboot_reason {
	TERMINATION_FAILED = 1,
	INSTANTIATION_FAILED = 2
};

extern int amf_node_reboot (
	struct amf_node *node, enum amf_reboot_reason reason);

/* Response event methods */
extern void amf_node_application_started (
	struct amf_node *node, struct amf_application *app);
extern void amf_node_application_workload_assigned (
	struct amf_node *node, struct amf_application *app);

/* Timer event methods */
extern void timer_function_node_probation_period_expired (void *node);

/*===========================================================================*/
/* amfcluster.c */

/* General methods */
extern void amf_cluster_init (void);
extern int amf_cluster_serialize (
	struct amf_cluster *cluster, char **buf, int *offset);
extern struct amf_cluster *amf_cluster_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);

/* Event methods */
extern void amf_cluster_start (struct amf_cluster *cluster);

/* Response event methods */
extern void amf_cluster_application_started (
	struct amf_cluster *cluster, struct amf_application *app);
extern void amf_cluster_application_workload_assigned (
	struct amf_cluster *cluster, struct amf_application *app);

/*===========================================================================*/
/* amfapp.c */

/* General methods */
extern void amf_application_init (void);
extern struct amf_application *amf_application_create (void);
extern int amf_application_calc_and_set_si_dependency_level (
	struct amf_application *app);
extern int amf_application_serialize (
	struct amf_application *application, char **buf, int *offset);
extern struct amf_application *amf_application_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);
extern int amf_application_si_count_get (struct amf_application *app);

/* Event methods */
extern void amf_application_start (
	struct amf_application *app, struct amf_node *node);
extern void amf_application_assign_workload (
	struct amf_application *app, struct amf_node *node);

/* Response event methods */
extern void amf_application_sg_started (
	struct amf_application *app, struct amf_sg *sg,	struct amf_node *node);
extern void amf_application_sg_assigned (
	struct amf_application *app, struct amf_sg *sg);

/*===========================================================================*/
/* amfsg.c */

/* General methods */
extern void amf_sg_init (void);
extern struct amf_sg *amf_sg_create (void);
extern int amf_sg_serialize (
	struct amf_sg *sg, char **buf, int *offset);
extern struct amf_sg *amf_sg_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);

/* Event methods */
extern void amf_sg_start (struct amf_sg *sg, struct amf_node *node);
extern void amf_sg_assign_si (struct amf_sg *sg, int dependency_level);

extern void amf_sg_failover_node_req (
	struct amf_sg *sg, struct amf_node *node);
extern void amf_sg_failover_su_req (
	struct amf_sg *sg, struct amf_node *node);
extern void amf_sg_failover_comp_req (
	struct amf_sg *sg, struct amf_node *node);
extern void amf_sg_switchover_node_req (
	struct amf_sg *sg, struct amf_node *node);

/* Response event methods */
extern void amf_sg_su_state_changed (
	struct amf_sg *sg, struct amf_su *su, SaAmfStateT type, int state);
extern void amf_sg_si_ha_state_changed (
	struct amf_sg *sg, struct amf_si *si, int state);
extern void amf_sg_su_assignment_removed (
	struct amf_sg *sg, struct amf_su *su);

/* Timer event methods */
//static void timer_function_auto_adjust_tmo (void *sg);

/*===========================================================================*/
/* amfsu.c */

/* General methods */
extern void amf_su_init (void);
extern struct amf_su *amf_su_create (void);
extern int amf_su_serialize (
	struct amf_su *su, char **buf, int *offset);
extern struct amf_su *amf_su_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);
extern int amf_su_is_local (struct amf_su *su);

/* Event methods */
extern void amf_su_instantiate (struct amf_su *su);
extern void amf_su_assign_si (
	struct amf_su *su, struct amf_si *si, SaAmfHAStateT ha_state);
extern void amf_su_restart_req (struct amf_su *su);
extern void amf_su_terminate (struct amf_su *su);
extern struct amf_node *amf_su_get_node (struct amf_su *su);
extern void amf_su_escalation_level_reset (struct amf_su *su);
extern void amf_su_remove_assignment (struct amf_su *su);

/* Response event methods */
extern void amf_su_comp_state_changed (
	struct amf_su *su, struct amf_comp *comp, SaAmfStateT type, int state);
extern void amf_su_comp_hastate_changed (
	struct amf_su *su, struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment);
extern void amf_su_comp_error_suspected (
	struct amf_su *su,
	struct amf_comp *comp,
	SaAmfRecommendedRecoveryT recommended_recovery);

/* Timer event methods */
//static void timer_function_su_probation_period_expired(void *data);

/*===========================================================================*/
/* amfcomp.c */

/* General methods */
extern void amf_comp_init (void);
extern struct amf_comp *amf_comp_create (struct amf_su *su);
extern char *amf_comp_dn_make (struct amf_comp *comp, SaNameT *name);
extern struct amf_comp *amf_comp_find (
	struct amf_cluster *cluster, SaNameT *name);
extern int amf_comp_serialize (
	struct amf_comp *comp, char **buf, int *offset);
extern struct amf_comp *amf_comp_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);

/* Event methods */
extern void amf_comp_instantiate (struct amf_comp *comp);
extern void amf_comp_terminate (struct amf_comp *comp);
extern void amf_comp_hastate_set (
	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment,
	SaAmfHAStateT requested_ha_state);
extern void amf_comp_restart (struct amf_comp *comp);
extern void amf_comp_operational_state_set (
	struct amf_comp *comp, SaAmfOperationalStateT opstate);
extern void amf_comp_readiness_state_set (
	struct amf_comp *comp, SaAmfReadinessStateT state);
extern struct amf_healthcheck *amf_comp_find_healthcheck (
	struct amf_comp *comp, SaAmfHealthcheckKeyT *key);
extern void amf_comp_healthcheck_tmo (
	struct amf_comp *comp, struct amf_healthcheck *healthcheck);
extern void amf_comp_cleanup_completed (struct amf_comp *comp);

/*
 * Originates from library                                                                
 */
extern SaAisErrorT amf_comp_healthcheck_start (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey,
	SaAmfHealthcheckInvocationT invocationType,
	SaAmfRecommendedRecoveryT recommendedRecovery);
extern SaAisErrorT amf_comp_healthcheck_stop (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey);
extern SaAisErrorT amf_comp_register (struct amf_comp *comp);
extern void amf_comp_unregister (struct amf_comp *comp);
extern void amf_comp_error_report (
	struct amf_comp *comp, SaAmfRecommendedRecoveryT recommendedRecovery);
extern int amf_comp_response_1 (
	SaInvocationT invocation, SaAisErrorT error, SaAisErrorT *retval);
extern struct amf_comp *amf_comp_response_2 (
	SaInvocationT invocation, SaAisErrorT error, SaAisErrorT *retval);
extern SaAisErrorT amf_comp_hastate_get (
	struct amf_comp *comp, SaNameT *csi_name, SaAmfHAStateT *ha_state);
extern SaAisErrorT amf_comp_healthcheck_confirm (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey,
	SaAisErrorT healthcheckResult);

/*===========================================================================*/
/* amfsi.c */

/* General methods */
extern void amf_si_init (void);
extern struct amf_si *amf_si_create (void);
extern int amf_si_calc_and_set_csi_dependency_level (struct amf_si *si);
extern int amf_si_serialize (
	struct amf_si *si, char **buf, int *offset);
extern struct amf_si *amf_si_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);

/* Event methods */
extern void amf_si_activate (
	struct amf_si *si,
	void (*activated_callback_fn)(struct amf_si *si, int result));
extern void amf_si_deactivate (
	struct amf_si *si,
	struct amf_csi *csi,
	void (*deactivated_callback_fn)(struct amf_si *si, int result));
extern void amf_si_set_ha_state (
	struct amf_si *si,
	SaAmfHAStateT ha_state,
	void (*set_ha_state_callback_fn)(struct amf_si *si, int result));

/* Response event methods */
extern void amf_si_comp_set_ha_state_done (
	struct amf_si *si, struct amf_csi_assignment *csi_assignment);
extern void amf_si_comp_set_ha_state_failed (
	struct amf_si *si, struct amf_csi_assignment *csi_assignment);


/* General methods */
extern struct amf_csi *amf_csi_create (void);
extern int amf_csi_serialize (
	struct amf_csi *csi, char **buf, int *offset);
extern struct amf_csi *amf_csi_deserialize (
	char **buf, int *size, struct amf_cluster *cluster);
extern char *amf_csi_dn_make (struct amf_csi *csi, SaNameT *name);

/*===========================================================================*/
extern struct amf_node *this_amf_node;
extern struct amf_cluster amf_cluster;

#endif /* AMF_H_DEFINED */
