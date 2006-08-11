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
#include "aispoll.h"

enum clc_component_types {
	clc_component_sa_aware = 0,			/* sa aware */
	clc_component_proxied_pre = 1,			/* proxied, pre-instantiable */
	clc_component_proxied_non_pre = 2,		/* proxied, non pre-instantiable */
	clc_component_non_proxied_non_sa_aware = 3	/* non-proxied, non sa aware */
};

typedef enum {
	SG_AC_Idle = 0,
	SG_AC_DeactivatingDependantWorkload,
	SG_AC_TerminatingSuspected,
	SG_AC_ActivatingStandby,
	SG_AC_AssigningStandbyToSpare,
	SG_AC_ReparingComponent,
	SG_AC_ReparingSu,
	SG_AC_AssigningOnRequest,
	SG_AC_InstantiatingServiceUnits,
	SG_AC_RemovingAssignment,
	SG_AC_AssigningActiveworkload,
	SG_AC_AssigningAutoAdjust,
	SG_AC_AssigningStandBy,
	SG_AC_WaitingAfterOperationFailed
} sg_avail_control_state_t;

typedef enum {
	SU_RC_ESCALATION_LEVEL_0 = 0,
	SU_RC_ESCALATION_LEVEL_1,
	SU_RC_ESCALATION_LEVEL_2,
	SU_RC_RESTART_COMP_DEACTIVATING,
	SU_RC_RESTART_COMP_RESTARTING,
	SU_RC_RESTART_COMP_SETTING,
	SU_RC_RESTART_COMP_ACTIVATING,
	SU_RC_RESTART_SU_DEACTIVATING,
	SU_RC_RESTART_SU_TERMINATING,
	SU_RC_RESTART_SU_INSTANTIATING,
	SU_RC_RESTART_SU_SETTING,
	SU_RC_RESTART_SU_ACTIVATING
} su_restart_control_state_t;

typedef enum {
	AMF_NONE,
	AMF_APPLICATION,
	AMF_CLUSTER,
	AMF_NODE,
	AMF_SG,
	AMF_SU,
	AMF_COMP,
	AMF_COMP_ENV_VAR,
	AMF_COMP_CS_TYPE,
	AMF_SI,
	AMF_SI_ASSIGNMENT,
	AMF_SI_RANKED_SU,
	AMF_SI_DEPENDENCY,
	AMF_CSI,
	AMF_CSI_ASSIGNMENT,
	AMF_CSI_ATTRIBUTE,
	AMF_HEALTHCHECK,
	AMF_CSI_DEPENDENCIES,
	AMF_CS_TYPE,
} amf_object_type_t;

struct amf_si_assignment;
struct amf_csi_assignment;
struct amf_healthcheck;

enum cluster_states {
	CLUSTER_UNINSTANTIATED,
	CLUSTER_STARTING,
	CLUSTER_STARTED
};

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
	enum cluster_states state;
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
	/* ordered list of SUs */
	struct amf_si      *si_head;
	struct amf_si      *si_tail;

	/* Implementation */
	char clccli_path[PATH_MAX];
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

    /* ordered list of SUs */
	struct amf_su          *su_head;

	/* Implementation */
	char clccli_path[PATH_MAX];
	struct amf_sg *next;
	sg_avail_control_state_t avail_state;
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
//	SaAmfReadinessStateT saAmfSUReadinessState;
	SaAmfPresenceStateT saAmfSUPresenceState;
//	SaNameT saAmfSUAssignedSIs;
	SaNameT saAmfSUHostedByNode;
/*     SaUint32T saAmfSUNumCurrActiveSIs;  */
/*     SaUint32T saAmfSUNumCurrStandbySIs; */
	SaUint32T saAmfSURestartCount;

	/* Relations */
	struct amf_sg *sg;
	struct amf_comp *comp_head;

	/* Implementation */
	su_restart_control_state_t restart_control_state;
	su_restart_control_state_t escalation_level_history_state;
	char clccli_path[PATH_MAX];
	SaUint32T              su_failover_cnt; /* missing in SAF specs? */
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
	SaUint32T saAmfCompDefaultClcCliTimeout;
	SaUint32T saAmfCompDefaultCallbackTimeOut;
	SaStringT saAmfCompInstantiateCmd;
	SaStringT saAmfCompInstantiateCmdArgv;
	SaUint32T saAmfCompInstantiateTimeout;
	SaUint32T saAmfCompInstantiationLevel;
	SaUint32T saAmfCompNumMaxInstantiateWithoutDelay;
	SaUint32T saAmfCompNumMaxInstantiateWithDelay;
	SaUint32T saAmfCompDelayBetweenInstantiateAttempts;
	SaStringT saAmfCompTerminateCmd;
	SaUint32T saAmfCompTerminateTimeout;
	SaStringT saAmfCompTerminateCmdArgv;
	SaStringT saAmfCompCleanupCmd;
	SaUint32T saAmfCompCleanupTimeout;
	SaStringT saAmfCompCleanupCmdArgv;
	SaStringT saAmfCompAmStartCmd;
	SaUint32T saAmfCompAmStartTimeout;
	SaStringT saAmfCompAmStartCmdArgv;
	SaUint32T saAmfCompNumMaxAmStartAttempt;
	SaStringT saAmfCompAmStopCmd;
	SaUint32T saAmfCompAmStopTimeout;
	SaStringT saAmfCompAmStopCmdArgv;
	SaUint32T saAmfCompNumMaxAmStopAttempt;
	SaUint32T saAmfCompTerminateCallbackTimeout;
	SaUint32T saAmfCompCSISetCallbackTimeout;
	SaUint32T saAmfCompQuiescingCompleteTimeout;
	SaUint32T saAmfCompCSIRmvCallbackTimeout;
	SaAmfRecommendedRecoveryT saAmfCompRecoveryOnError;
	SaBoolT saAmfCompDisableRestart;
	SaNameT saAmfCompProxyCsi;

	/* Runtime Attributes */
	SaAmfOperationalStateT saAmfCompOperState;
//	SaAmfReadinessStateT saAmfCompReadinessState;
	SaAmfPresenceStateT saAmfCompPresenceState;
	SaUint32T saAmfCompRestartCount;
/*     SaUint32T saAmfCompNumCurrActiveCsi;  */
/*     SaUint32T saAmfCompNumCurrStandbyCsi; */
/*	SaNameT saAmfCompAssignedCsi; */
	SaNameT saAmfCompCurrProxyName;
	SaNameT **saAmfCompCurrProxiedNames;

	/* Relations */
	struct amf_comp *proxy_comp;
	struct amf_su *su;

	/* Implementation */
	SaStringT clccli_path;
	struct amf_comp *next;
	void *conn;
	enum clc_component_types comptype;
	struct amf_healthcheck *healthcheck_head;

	/**
     * Flag that indicates of this component has a suspected error
	 */
	SaUint32T error_suspected;
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
	poll_timer_handle timer_handle_duration;
	poll_timer_handle timer_handle_period;
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
//	SaAmfAssignmentStateT saAmfSIAssignmentState;
//	SaUint32T saAmfSINumCurrActiveAssignments;
//	SaUint32T saAmfSINumCurrStandbyAssignments;

	/* Relations */
	struct amf_application   *application;
	struct amf_csi           *csi_head;
	struct amf_si_assignment *assigned_sis;
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
	struct amf_su *su;

	/* Implementation */
	SaAmfHAStateT requested_ha_state;
	struct amf_si_assignment *next;
	void (*assumed_callback_fn) (
		struct amf_si_assignment *si_assignment, int result);
};

struct amf_csi {
	/* Configuration Attributes */
	SaNameT name;
	SaNameT saAmfCSTypeName;
	SaNameT **saAmfCSIDependencies;

	/* Relations */
	struct amf_si *si;
	struct amf_csi_assignment *assigned_csis;
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
	struct amf_csi_assignment *next;
	struct amf_si_assignment *si_assignment;
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
	MESSAGE_REQ_EXEC_AMF_RESPONSE = 4,
	MESSAGE_REQ_EXEC_AMF_SYNC_START = 5,
	MESSAGE_REQ_EXEC_AMF_SYNC_DATA = 6,
	MESSAGE_REQ_EXEC_AMF_SYNC_READY = 7
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

extern struct amf_cluster *amf_config_read (char **error_string);
extern void amf_runtime_attributes_print (struct amf_cluster *cluster);
extern int amf_enabled (struct objdb_iface_ver0 *objdb);
extern void *_amf_malloc (size_t size, char *file, unsigned int line);
#define amf_malloc(size) _amf_malloc ((size), __FILE__, __LINE__)

extern const char *amf_admin_state (int state);
extern const char *amf_op_state (int state);
extern const char *amf_presence_state (int state);
extern const char *amf_ha_state (int state);
extern const char *amf_readiness_state (int state);
extern const char *amf_assignment_state (int state);

extern char *amf_serialize_SaNameT (
	char *buf, int *size, int *offset, SaNameT *name);
extern char *amf_serialize_SaStringT (
	char *buf, int *size, int *offset, SaStringT str);
extern char *amf_serialize_SaUint32T (
	char *buf, int *size, int *offset, SaUint32T num);
extern char *amf_serialize_SaUint64T (char *buf, SaUint64T num);
extern char *amf_serialize_opaque (
	char *buf, int *size, int *offset, char *cp, int cnt);
extern char *amf_deserialize_SaNameT (char *buf, SaNameT *name);
extern char *amf_deserialize_SaStringT (char *buf, SaStringT *str);
extern char *amf_deserialize_SaUint32T (char *buf, SaUint32T *num);
extern char *amf_deserialize_SaUint64T (char *buf, SaUint64T *num);
extern char *amf_deserialize_opaque (char *buf, char *dst, int *cnt);

/*===========================================================================*/
/* amfnode.c */

/* General methods */
extern struct amf_node *amf_node_new (struct amf_cluster *cluster, char *name);
extern void amf_node_init (void);
extern void *amf_node_serialize (struct amf_node *node, int *len);
extern struct amf_node *amf_node_deserialize (
	struct amf_cluster *cluster, char *buf, int size);
extern struct amf_node *amf_node_find (SaNameT *name);

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
extern struct amf_cluster *amf_cluster_new (void);
extern void *amf_cluster_serialize (struct amf_cluster *cluster, int *len);
extern struct amf_cluster *amf_cluster_deserialize (char *buf, int size);

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
extern struct amf_application *amf_application_find (
	struct amf_cluster *cluster, char *name);
extern struct amf_application *amf_application_new (
	struct amf_cluster *cluster);
extern void amf_application_delete (struct amf_application *app);
extern int amf_application_calc_and_set_si_dependency_level (
	struct amf_application *app);
extern void *amf_application_serialize (
	struct amf_application *application, int *len);
extern struct amf_application *amf_application_deserialize (
	struct amf_cluster *cluster, char *buf, int size);

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
extern struct amf_sg *amf_sg_find (struct amf_application *app, char *name);
extern struct amf_sg *amf_sg_new (struct amf_application *app, char *name);
extern void amf_sg_delete (struct amf_sg *sg);
extern void *amf_sg_serialize (struct amf_sg *sg, int *len);
extern struct amf_sg *amf_sg_deserialize (
	struct amf_application *app, char *buf, int size);

/**
 * Request SG to start (instantiate all SUs)
 * 
 * @param sg
 * @param node - NULL start all SUs in the SG
 * @param node - !NULL start all SUs in the SG for the specified
 *             node.
 */
extern void amf_sg_start (struct amf_sg *sg, struct amf_node *node);

/**
 * Assign SIs on a certain dependency level to SUs
 * @param sg
 * @param dependency_level
 */
extern void amf_sg_assign_si (struct amf_sg *sg, int dependency_level);

extern void amf_sg_failover_node_req (
	struct amf_sg *sg, struct amf_node *node);
extern void amf_sg_failover_su_req (
	struct amf_sg *sg, struct amf_su *su, struct amf_node *node);
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
extern void amf_sg_si_activated (
	struct amf_sg *sg, struct amf_si *si);

/* Timer event methods */
//static void timer_function_auto_adjust_tmo (void *sg);

/*===========================================================================*/
/* amfsu.c */

/* General methods */
extern void amf_su_init (void);
extern struct amf_su *amf_su_find (
	struct amf_cluster *cluster, SaNameT *name);
extern struct amf_su *amf_su_new (struct amf_sg *sg, char *name);
extern void amf_su_delete (struct amf_su *su);
extern char *amf_su_dn_make (struct amf_su *su, SaNameT *name);
extern void *amf_su_serialize (struct amf_su *su, int *len);
extern struct amf_su *amf_su_deserialize (
	struct amf_sg *sg, char *buf, int size);
extern int amf_su_is_local (struct amf_su *su);
extern struct amf_si_assignment *amf_su_get_next_si_assignment (
	struct amf_su *su, const struct amf_si_assignment *si_assignment);
extern void amf_su_foreach_si_assignment (
	struct amf_su *su,
	void (*foreach_fn)(struct amf_su *su,
					   struct amf_si_assignment *si_assignment));
extern int amf_su_get_saAmfSUNumCurrActiveSIs (struct amf_su *su);
extern int amf_su_get_saAmfSUNumCurrStandbySIs (struct amf_su *su);
extern SaAmfReadinessStateT amf_su_get_saAmfSUReadinessState (
	struct amf_su *su);

/* Event methods */
extern void amf_su_instantiate (struct amf_su *su);
extern void amf_su_assign_si (
	struct amf_su *su, struct amf_si *si, SaAmfHAStateT ha_state);
extern void amf_su_restart_req (struct amf_su *su);

/**
 * Request termination of all component in an SU
 * @param su
 */
extern void amf_su_terminate (struct amf_su *su);

extern struct amf_node *amf_su_get_node (struct amf_su *su);
extern void amf_su_escalation_level_reset (struct amf_su *su);
extern void amf_su_remove_assignment (struct amf_su *su);

/* Response event methods */
extern void amf_su_comp_state_changed (
	struct amf_su *su, struct amf_comp *comp, SaAmfStateT type, int state);
#if 0
extern void amf_su_comp_hastate_changed (
	struct amf_su *su, struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment);
#endif
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
extern struct amf_comp *amf_comp_new (struct amf_su *su, char *name);
extern void amf_comp_delete (struct amf_comp *comp);
extern char *amf_comp_dn_make (struct amf_comp *comp, SaNameT *name);
extern struct amf_comp *amf_comp_find (
	struct amf_cluster *cluster, SaNameT *name);
extern void *amf_comp_serialize (struct amf_comp *comp, int *len);
extern struct amf_comp *amf_comp_deserialize (
	struct amf_su *su, char *buf, int size);
extern void amf_comp_foreach_csi_assignment (
	struct amf_comp *component,
	void (*foreach_fn)(struct amf_comp *component,
					   struct amf_csi_assignment *csi_assignment));
extern struct amf_csi_assignment *amf_comp_get_next_csi_assignment (
	struct amf_comp *component, const struct amf_csi_assignment *csi_assignment);
extern SaAmfReadinessStateT amf_comp_get_saAmfCompReadinessState (
	struct amf_comp *comp);

/* Event methods */
extern void amf_comp_instantiate (struct amf_comp *comp);
extern void amf_comp_terminate (struct amf_comp *comp);

/**
 * Request the component to assume a HA state
 * @param comp
 * @param csi_assignment
 * @param requested_ha_state
 */
extern void amf_comp_hastate_set (
	struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment);

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

/**
 * Count number of active CSI assignments
 * @param component
 * 
 * @return int
 */
extern int amf_comp_get_saAmfCompNumCurrActiveCsi(struct amf_comp *component);

/**
 * Count number of standby CSI assignments
 * @param component
 * 
 * @return int
 */
extern int amf_comp_get_saAmfCompNumCurrStandbyCsi(struct amf_comp *component);

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
	SaInvocationT invocation, SaAisErrorT error, SaAisErrorT *retval,
	SaUint32T *interface, SaNameT *dn);
extern struct amf_comp *amf_comp_response_2 (
	SaUint32T interface, SaNameT *dn, SaAisErrorT error, SaAisErrorT *retval);
extern SaAisErrorT amf_comp_hastate_get (
	struct amf_comp *comp, SaNameT *csi_name, SaAmfHAStateT *ha_state);
extern SaAisErrorT amf_comp_healthcheck_confirm (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey,
	SaAisErrorT healthcheckResult);

extern void *amf_healthcheck_serialize (
	struct amf_healthcheck *healthcheck, int *len);
extern struct amf_healthcheck *amf_healthcheck_deserialize (
	struct amf_comp *comp, char *buf, int size);

/*===========================================================================*/
/* amfsi.c */

/* General methods */
extern void amf_si_init (void);
extern struct amf_si *amf_si_find (struct amf_application *app, char *name);
extern struct amf_si *amf_si_new (struct amf_application *app, char *name);
extern void amf_si_delete (struct amf_si *si);
extern int amf_si_calc_and_set_csi_dependency_level (struct amf_si *si);
extern void *amf_si_serialize (struct amf_si *si, int *len);
extern struct amf_si *amf_si_deserialize (
	struct amf_application *app, char *buf, int size);
extern void *amf_si_assignment_serialize (
	struct amf_si_assignment *si_assignment, int *len);
extern struct amf_si_assignment *amf_si_assignment_deserialize (
	struct amf_si *si, char *buf, int size);

/**
 * Get number of active assignments for the specified SI
 * @param si
 * 
 * @return int
 */
extern int amf_si_get_saAmfSINumCurrActiveAssignments (struct amf_si *si);

/**
 * Get number of standby assignments for the specified SI
 * @param si
 * 
 * @return int
 */
extern int amf_si_get_saAmfSINumCurrStandbyAssignments (struct amf_si *si);

/**
 * Get assignment state for the specified SI.
 * @param si
 * 
 * @return SaAmfAssignmentStateT
 */
extern SaAmfAssignmentStateT amf_si_get_saAmfSIAssignmentState (
	struct amf_si *si);

/* Event methods */

/**
 * Activate all active assignments. Request component to change
 * HA state to active.
 * @param si
 * @param activated_callback_fn
 */
extern void amf_si_activate (
	struct amf_si *si,
	void (*activated_callback_fn)(struct amf_si *si, int result));

/**
 * Deactivate all active assignments. Request component to
 * change HA state to quiesced.
 * 
 * @param si_assignment
 * @param deactivated_callback_fn
 * 
 * @return int 1 - deactived immediately
 * @return int 0 - asynchronous response through callback
 */
/***/
extern int amf_si_deactivate (
	struct amf_si_assignment *si_assignment,
	void (*deactivated_callback_fn)(struct amf_si_assignment *si_assignment,
		int result));

/**
 * Request SI (SU) to assume a HA state (request component)
 * 
 * @param si_assignment
 * @param assumed_ha_state_callback_fn
 */
extern void amf_si_ha_state_assume (
	struct amf_si_assignment *si_assignment,
	void (*assumed_ha_state_callback_fn)(struct amf_si_assignment *si_assignment,
		int result));

/**
 * Component reports to SI that a workload assignment succeeded.
 * 
 * @param si
 * @param csi_assignment
 */
extern void amf_si_comp_set_ha_state_done (
	struct amf_si *si, struct amf_csi_assignment *csi_assignment);

/**
 * Component reports to SI that a workload assignment failed.
 * @param si
 * @param csi_assignment
 */
extern void amf_si_comp_set_ha_state_failed (
	struct amf_si *si, struct amf_csi_assignment *csi_assignment);

/**
 * Request a CSI to delete all CSI assignments.
 * 
 * @param component
 * @param csi
 */
extern void amf_csi_delete_assignments (struct amf_csi *csi, struct amf_su *su);

/* General methods */
extern struct amf_csi *amf_csi_new (struct amf_si *si);
extern struct amf_csi *amf_csi_find (struct amf_si *si, char *name);
extern void amf_csi_delete (struct amf_csi *csi);
extern void *amf_csi_serialize (struct amf_csi *csi, int *len);
extern struct amf_csi *amf_csi_deserialize (
	struct amf_si *si, char *buf, int size);
extern void *amf_csi_assignment_serialize (
	struct amf_csi_assignment *csi_assignment, int *len);
extern struct amf_csi_assignment *amf_csi_assignment_deserialize (
	struct amf_csi *csi, char *buf, int size);
extern char *amf_csi_dn_make (struct amf_csi *csi, SaNameT *name);
extern char *amf_csi_assignment_dn_make (
	struct amf_csi_assignment *csi_assignment, SaNameT *name);
extern struct amf_csi_assignment *amf_csi_assignment_find (
	struct amf_cluster *cluster, SaNameT *name);

extern struct amf_csi_attribute *amf_csi_attribute_new (struct amf_csi *csi);
extern void *amf_csi_attribute_serialize (
	struct amf_csi_attribute *csi_attribute, int *len);
extern struct amf_csi_attribute *amf_csi_attribute_deserialize (
	struct amf_csi *csi, char *buf, int size);

/*===========================================================================*/
extern struct amf_node *this_amf_node;
extern struct amf_cluster *amf_cluster;

#endif /* AMF_H_DEFINED */
