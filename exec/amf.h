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
#include <sys/types.h>
#include <regex.h>

#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/list.h"
#include "../include/ipc_gen.h"
#include "objdb.h"
#include "timer.h"
#include "aispoll.h"

typedef void (*async_func_t)(void *param);

#define AMF_PROTOCOL_VERSION 1

enum scsm_states {
	IDLE = 1,
	PROBING_1,
	PROBING_2,
	CREATING_CLUSTER_MODEL,
	SYNCHRONIZING,
	NORMAL_OPERATION,
	UPDATING_CLUSTER_MODEL,
	UNCONFIGURED
};

/**
 * State descriptor for the AMF Synchronisation Control State
 * Machine (SCSM).
 */
struct scsm_descriptor {
	enum scsm_states           state;
	poll_timer_handle          timer_handle;

	/* node ID of current sync master */
	unsigned int               sync_master;

	struct amf_cluster        *cluster;
	struct amf_node           *node;
	struct amf_application    *app;
	int                        app_sync_completed;
	struct amf_sg             *sg;
	int                        sg_sync_completed;
	struct amf_su             *su;
	int                        su_sync_completed;
	struct amf_comp           *comp;
	int                        comp_sync_completed;
	struct amf_healthcheck    *healthcheck;
	struct amf_si             *si;
	int                        si_sync_completed;
	struct amf_si_assignment  *si_assignment;
	struct amf_csi            *csi;
	int                        csi_sync_completed;
	struct amf_csi_assignment *csi_assignment;
	struct amf_csi_attribute  *csi_attribute;
};

enum clc_component_types {
	clc_component_sa_aware = 0,			/* sa aware */
	clc_component_proxied_pre = 1,			/* proxied, pre-instantiable */
	clc_component_proxied_non_pre = 2,		/* proxied, non pre-instantiable */
	clc_component_non_proxied_non_sa_aware = 3	/* non-proxied, non sa aware */
};

typedef enum {
	USR_AMF_HA_STATE_REMOVED = SA_AMF_HA_QUIESCING + 1
} UsrAmfHaState;


typedef enum {
	APP_AC_UNINSTANTIATED = 1,
	APP_AC_STARTING_SGS,
	APP_AC_STARTED,
	APP_AC_ASSIGNING_WORKLOAD,
	APP_AC_WORKLOAD_ASSIGNED
} app_avail_control_state_t;

typedef enum {
	NODE_ACSM_REPAIR_NEEDED = 1,
	NODE_ACSM_IDLE_ESCALLATION_LEVEL_0,
	NODE_ACSM_IDLE_ESCALLATION_LEVEL_2,
	NODE_ACSM_IDLE_ESCALLATION_LEVEL_3,
	NODE_ACSM_FAILING_FAST_REBOOTING_NODE,
	NODE_ACSM_FAILING_FAST_ACTIVATING_STANDBY_NODE,
	NODE_ACSM_FAILING_GRACEFULLY_SWITCHING_OVER,
	NODE_ACSM_FAILING_GRACEFULLY_FAILING_OVER,
	NODE_ACSM_FAILING_GRACEFULLY_REBOOTING_NODE,
	NODE_ACSM_LEAVING_SPONTANEOUSLY_FAILING_OVER,
	NODE_ACSM_LEAVING_SPONTANEOUSLY_WAITING_FOR_NODE_TO_JOIN,
	NODE_ACSM_JOINING_STARTING_APPLICATIONS,
	NODE_ACSM_JOINING_ASSIGNING_WORKLOAD
} amf_node_acsm_state_t; 

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
	SG_AC_WaitingAfterOperationFailed,
	SG_AC_RemovingStandbyAssignments
} sg_avail_control_state_t;

typedef enum amf_sg_event_type {
	SG_UNKNOWN_EV,
	SG_FAILOVER_SU_EV,
	SG_FAILOVER_NODE_EV,
	SG_FAILOVER_COMP_EV,
	SG_SWITCH_OVER_NODE_EV,
	SG_START_EV,
	SG_AUTO_ADJUST_EV,
	SG_ASSIGN_SI_EV
} amf_sg_event_type_t;

typedef enum amf_su_event_type {
	SU_COMP_ERROR_SUSPECTED_EV = 1
} amf_su_event_type_t;

typedef enum {
	SU_RC_IDLE_ESCALATION_LEVEL_0 = 0,
	SU_RC_IDLE_ESCALATION_LEVEL_1,
	SU_RC_IDLE_ESCALATION_LEVEL_2,
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

typedef enum {
	CLUSTER_AC_UNINSTANTIATED = 1,
	CLUSTER_AC_STARTING_APPLICATIONS,
	CLUSTER_AC_WAITING_OVER_TIME_1,
	CLUSTER_AC_WAITING_OVER_TIME_2,
	CLUSTER_AC_ASSIGNING_WORKLOAD,
	CLUSTER_AC_STARTED,
	CLUSTER_AC_TERMINATING_APPLICATIONS,
	CLUSTER_AC_WORKLOAD_REMOVESD,
	CLUSTER_AC_REMOVING_WORKLOAD,
	CLUSTER_AC_DEACTIVATING_WORKLOAD,
	CLUSTER_AC_QUISING
} cluster_avail_control_state_t;

typedef enum amf_cluster_event_type {
	CLUSTER_SYNC_READY_EV,
	CLUSTER_EVENT_TYPE_CNT
} amf_cluster_event_type_t;

typedef enum amf_application_event_type {
	APPLICATION_ASSIGN_WORKLOAD_EV,
	APPLICATION_START_EV,
	APPLICATION_EVENT_TYPE_CNT
} amf_application_event_type_t;

typedef struct amf_fifo {
	int entry_type;
	struct amf_fifo *next;
	int size_of_data;
	uint8_t data[];
} amf_fifo_t;

typedef struct amf_cluster {
	/* Configuration Attributes */
	SaNameT name;
	SaUint32T saAmfClusterStartupTimeout;
	SaNameT saAmfClusterClmCluster;

	/* Runtime Attributes */
	SaAmfAdminStateT saAmfClusterAdminState;

	/* Relations */
	struct amf_node *node_head;
	struct amf_application *application_head;

	/* Implementation */
	poll_timer_handle timeout_handle;
	cluster_avail_control_state_t acsm_state;
	amf_fifo_t *deferred_events;
} amf_cluster_t;

typedef struct amf_node {
	/* Configuration Attributes */
	SaNameT name;
	SaNameT saAmfNodeClmNode;
	SaUint32T saAmfNodeSuFailOverProb;
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
	unsigned int nodeid;
	struct amf_node *next;
	amf_node_acsm_state_t acsm_state;
	amf_node_acsm_state_t history_state;

} amf_node_t;

typedef struct amf_application {
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

	/* Implementation */
	SaStringT clccli_path;
	struct amf_application *next;
	struct amf_node *node_to_start;
	app_avail_control_state_t acsm_state;
	amf_fifo_t *deferred_events;
} amf_application_t;

struct sg_recovery_scope {
	amf_sg_event_type_t event_type;
	struct amf_si **sis;
	struct amf_su **sus;
	struct amf_comp *comp;
	struct amf_node *node;
};

typedef struct amf_sg {
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
	SaUint32T saAmfSGCompRestartProb;
	SaUint32T saAmfSGCompRestartMax;
	SaUint32T saAmfSGSuRestartProb;
	SaUint32T saAmfSGSuRestartMax;
	SaUint32T saAmfSGAutoAdjustProb;
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
	SaStringT clccli_path;
	struct amf_sg            *next;
	sg_avail_control_state_t  avail_state;
	struct sg_recovery_scope  recovery_scope;
	struct amf_node *node_to_start;
	amf_fifo_t *deferred_events;
} amf_sg_t;

typedef struct amf_su {
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
	/* 	SaAmfReadinessStateT saAmfSUReadinessState; */
	SaAmfPresenceStateT saAmfSUPresenceState;
	/* SaNameT saAmfSUAssignedSIs; */
	SaNameT saAmfSUHostedByNode;
	/* SaUint32T saAmfSUNumCurrActiveSIs;  */
	/* SaUint32T saAmfSUNumCurrStandbySIs; */
	SaUint32T saAmfSURestartCount;

	/* Relations */
	struct amf_sg *sg;
	struct amf_comp *comp_head;

	/* Implementation */
	su_restart_control_state_t restart_control_state;
	su_restart_control_state_t escalation_level_history_state;
	SaStringT clccli_path;
	SaUint32T              su_failover_cnt;	/* missing in SAF specs? */
	SaUint32T              current_comp_instantiation_level;
	struct amf_su         *next;
	amf_fifo_t            *deferred_events;
} amf_su_t;

typedef struct amf_comp {
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
	/* SaAmfReadinessStateT saAmfCompReadinessState; */
	SaAmfPresenceStateT saAmfCompPresenceState;
	SaUint32T saAmfCompRestartCount;
	/* SaUint32T saAmfCompNumCurrActiveCsi;  */
	/* SaUint32T saAmfCompNumCurrStandbyCsi; */
	/*SaNameT saAmfCompAssignedCsi; */
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
	poll_timer_handle instantiate_timeout_handle;
	poll_timer_handle cleanup_timeout_handle;
	/*
	 * Flag that indicates of this component has a suspected error
	 */
	SaUint32T error_suspected;
} amf_comp_t;


typedef struct amf_healthcheck {
	/* Configuration Attributes */
	SaAmfHealthcheckKeyT safHealthcheckKey;
	SaUint32T saAmfHealthcheckMaxDuration;
	SaUint32T saAmfHealthcheckPeriod;

	/* Relations */
	struct amf_comp *comp;

	/* Implementation */
	struct amf_healthcheck *next;
	SaUint32T active;
	SaAmfHealthcheckInvocationT invocationType;
	SaAmfRecommendedRecoveryT recommendedRecovery;
	poll_timer_handle timer_handle_duration;
	poll_timer_handle timer_handle_period;

} amf_healthcheck_t;

typedef struct amf_si {
	/* Configuration Attributes */
	SaNameT name;
	SaNameT saAmfSIProtectedbySG;
	SaUint32T saAmfSIRank;
	SaUint32T saAmfSINumCSIs;
	SaUint32T saAmfSIPrefActiveAssignments;
	SaUint32T saAmfSIPrefStandbyAssignments;

	/* Runtime Attributes */
	SaAmfAdminStateT saAmfSIAdminState;
	/* SaAmfAssignmentStateT saAmfSIAssignmentState; */
	/* SaUint32T saAmfSINumCurrActiveAssignments; */
	/* SaUint32T saAmfSINumCurrStandbyAssignments; */

	/* Relations */
	struct amf_application   *application;
	struct amf_csi           *csi_head;
	struct amf_si_assignment *assigned_sis;
	struct amf_si_dependency *depends_on;
	struct amf_si_ranked_su  *ranked_sus;

	/* Implementation */
	struct amf_si *next;
} amf_si_t;

typedef struct amf_si_ranked_su {
	/* Configuration Attributes */
	SaNameT name;
	SaUint32T saAmfRank;

	/* Relations */
	struct amf_si *si;
	struct amf_su *su;

	/* Implementation */
	struct amf_si_ranked_su *su_next;
	struct amf_si_ranked_su *si_next;
} amf_si_ranked_su_t;

typedef struct amf_si_dependency {
	/* Configuration Attributes */
	SaNameT name;
	int saAmfToleranceTime;

	/* Relations */

	/* Implementation */
	struct amf_si_dependency *next;
} amf_si_dependency_t;

typedef struct amf_si_assignment {
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
	void (*removed_callback_fn) (void *_si_assignment);
} amf_si_assignment_t;

typedef struct amf_csi {
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
} amf_csi_t;

typedef struct amf_csi_attribute {
	/* Configuration Attributes */
	SaStringT name;
	SaStringT *value;

	/* Implementation */
	struct amf_csi_attribute *next;
} amf_csi_attribute_t;

typedef struct amf_csi_assignment {
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
} amf_csi_assignment_t;

enum amf_response_interfaces {
	AMF_RESPONSE_HEALTHCHECKCALLBACK = 1,
	AMF_RESPONSE_CSISETCALLBACK = 2,
	AMF_RESPONSE_CSIREMOVECALLBACK = 3,
	AMF_RESPONSE_COMPONENTTERMINATECALLBACK = 4
};

enum amf_message_req_types {
	MESSAGE_REQ_EXEC_AMF_COMPONENT_REGISTER = 0,
	MESSAGE_REQ_EXEC_AMF_COMPONENT_ERROR_REPORT = 1,
	MESSAGE_REQ_EXEC_AMF_COMPONENT_INSTANTIATE = 2,
	MESSAGE_REQ_EXEC_AMF_CLC_CLEANUP_COMPLETED = 3,
	MESSAGE_REQ_EXEC_AMF_HEALTHCHECK_TMO = 4,
	MESSAGE_REQ_EXEC_AMF_RESPONSE = 5,
	MESSAGE_REQ_EXEC_AMF_SYNC_START = 6,
	MESSAGE_REQ_EXEC_AMF_SYNC_DATA = 7,
	MESSAGE_REQ_EXEC_AMF_CLUSTER_START_TMO = 8,
	MESSAGE_REQ_EXEC_AMF_SYNC_REQUEST = 9,
	MESSAGE_REQ_EXEC_AMF_COMPONENT_INSTANTIATE_TMO = 10,
	MESSAGE_REQ_EXEC_AMF_COMPONENT_CLEANUP_TMO = 11
};

struct req_exec_amf_clc_cleanup_completed {
	mar_req_header_t header;
	SaNameT compName;
	int cleanup_exit_code;
};

struct req_exec_amf_healthcheck_tmo {
	mar_req_header_t header;
	SaNameT compName;
	SaAmfHealthcheckKeyT safHealthcheckKey;
	SaAmfRecommendedRecoveryT recommendedRecovery;
};

struct req_exec_amf_comp_instantiate {
	mar_req_header_t header;
	SaNameT compName;
};

struct req_exec_amf_comp_instantiate_tmo {
	mar_req_header_t header;
	SaNameT compName;
};

struct req_exec_amf_comp_cleanup_tmo {
	mar_req_header_t header;
	SaNameT compName;
};

struct req_exec_amf_cluster_start_tmo {
	mar_req_header_t header;
	SaNameT sourceNodeName;
};

/*===========================================================================*/
/* amfutil.c */

extern struct amf_cluster *amf_config_read (char **error_string);
extern void amf_runtime_attributes_print (struct amf_cluster *cluster);
extern int amf_enabled (struct objdb_iface_ver0 *objdb);
extern void *_amf_malloc (size_t size, char *file, unsigned int line);
extern void *_amf_realloc (void* ptr, size_t size, char *file, unsigned int line);
#define amf_malloc(size) _amf_malloc ((size), __FILE__, __LINE__)
extern void *_amf_calloc (size_t nmemb, size_t size, char *file,
	unsigned int line);
#define amf_calloc(nmemb,size) _amf_calloc ((nmemb), (size), __FILE__, __LINE__)
#define amf_realloc(ptr,size) _amf_realloc ((ptr), (size), __FILE__, __LINE__)

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
extern char *amf_serialize_SaUint16T (
	char *buf, int *size, int *offset, SaUint16T num);
extern char *amf_serialize_SaUint32T (
	char *buf, int *size, int *offset, SaUint32T num);
extern char *amf_serialize_SaUint64T (char *buf, SaUint64T num);
extern char *amf_serialize_opaque (
	char *buf, int *size, int *offset, void *cp, int cnt);
extern char *amf_deserialize_SaNameT (char *buf, SaNameT *name);
extern char *amf_deserialize_SaStringT (char *buf, SaStringT *str);
extern char *amf_deserialize_SaUint16T (char *buf, SaUint16T *num);
extern char *amf_deserialize_SaUint32T (char *buf, SaUint32T *num);
extern char *amf_deserialize_SaUint64T (char *buf, SaUint64T *num);
extern char *amf_deserialize_opaque (char *buf, void *dst, int *cnt);
extern int amf_msg_mcast (int msg_id, void *buf, size_t len);
extern void amf_util_init (void);
extern void amf_fifo_put (int entry_type, amf_fifo_t **root, 
	int size_of_data, void *data);
extern int amf_fifo_get (amf_fifo_t **root, void *data);
extern void amf_call_function_asynchronous (async_func_t async_func, void *param);

/*===========================================================================*/
/* amfnode.c */

/* General methods */
extern struct amf_node *amf_node_new (struct amf_cluster *cluster, char *name);
extern void amf_node_init (void);
extern void *amf_node_serialize (struct amf_node *node, int *len);
extern struct amf_node *amf_node_deserialize (
	struct amf_cluster *cluster, char *buf);
extern struct amf_node *amf_node_find (SaNameT *name);
extern struct amf_node *amf_node_find_by_nodeid (unsigned int nodeid);
extern struct amf_node *amf_node_find_by_hostname (const char *hostname);
extern void amf_node_sg_failed_over (struct amf_node *node, 
	struct amf_sg *sg_in);

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
extern struct amf_cluster *amf_cluster_deserialize (char *buf);
extern int amf_cluster_applications_assigned (struct amf_cluster *cluster);
extern int amf_cluster_applications_started_with_no_starting_sgs (
	struct amf_cluster *cluster);




/* Event methods */
extern void amf_cluster_start_tmo_event (int is_sync_master, 
	struct amf_cluster *cluster, SaNameT *sourceNodeName);
extern void amf_cluster_sync_ready (struct amf_cluster *cluster, 
	struct amf_node *node);
/**
 * 
 * @param cluster
 * @param app
 */
extern void amf_cluster_start_applications(struct amf_cluster *cluster);
extern void amf_cluster_assign_workload (struct amf_cluster *cluster);

/* Response event methods */
extern void amf_cluster_application_started (
	amf_cluster_t *cluster, amf_application_t *app);
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
	struct amf_cluster *cluster, char *buf);
extern int amf_application_all_sg_started (struct amf_application *app);

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
	struct amf_application *app, char *buf);

/**
 * Request SG to start (instantiate all SUs)
 * 
 * @param sg
 * @param node - NULL start all SUs in the SG
 * @param node - !NULL start all SUs in the SG for the specified
 *             node.
 */
extern int amf_sg_start (struct amf_sg *sg, struct amf_node *node);

/**
 * Assign SIs on a certain dependency level to SUs
 * @param sg
 * @param dependency_level
 */
extern int amf_sg_assign_si_req (struct amf_sg *sg, int dependency_level);

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
	struct amf_sg *sg, char *buf);
extern int amf_su_is_local (struct amf_su *su);
extern struct amf_si_assignment *amf_su_get_next_si_assignment (
	struct amf_su *su, const struct amf_si_assignment *si_assignment);
extern void amf_su_foreach_si_assignment (struct amf_su *su,
	void (*foreach_fn)(struct amf_su *su,
					   struct amf_si_assignment *si_assignment));
extern int amf_su_get_saAmfSUNumCurrActiveSIs (struct amf_su *su);
extern int amf_su_get_saAmfSUNumCurrStandbySIs (struct amf_su *su);
extern SaAmfReadinessStateT amf_su_get_saAmfSUReadinessState (
	struct amf_su *su);
extern int amf_su_are_all_comps_in_su (struct amf_su *su,
	SaAmfPresenceStateT state);

/* Event methods */
/**
 * 
 * @param su
 * @param comp
 */
extern amf_si_assignment_t *amf_su_assign_si (
	struct amf_su *su, struct amf_si *si, SaAmfHAStateT ha_state);
extern void amf_su_restart_req (struct amf_su *su);

/**
 * Request termination of all component in an SU
 * @param su
 */
void amf_su_terminate (struct amf_su *su);
extern struct amf_node *amf_su_get_node (struct amf_su *su);
extern void amf_su_escalation_level_reset (struct amf_su *su);
extern void amf_su_remove_assignment (struct amf_su *su);

/* Response event methods */
extern void amf_su_comp_state_changed (
	struct amf_su *su, struct amf_comp *comp, SaAmfStateT type, int state);
extern void amf_su_comp_error_suspected (
	struct amf_su *su,
	struct amf_comp *comp,
	SaAmfRecommendedRecoveryT recommended_recovery);
extern void amf_su_restart (struct amf_su *su);
void amf_su_operational_state_set (struct amf_su *su,
	SaAmfOperationalStateT oper_state);
extern int amf_su_instantiate (struct amf_su *su);
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
	struct amf_su *su, char *buf);
extern void amf_comp_foreach_csi_assignment (
	struct amf_comp *component,
	void (*foreach_fn)(struct amf_comp *component,
					   struct amf_csi_assignment *csi_assignment));
extern struct amf_csi_assignment *amf_comp_get_next_csi_assignment (
	struct amf_comp *component, const struct amf_csi_assignment *csi_assignment);
extern SaAmfReadinessStateT amf_comp_get_saAmfCompReadinessState (
	struct amf_comp *comp);
struct amf_comp *amf_comp_find_from_conn_info (void *conn);

/* Event methods */
extern void amf_comp_instantiate (struct amf_comp *comp);
extern void amf_comp_terminate (struct amf_comp *comp);
extern void amf_comp_node_left (struct amf_comp *comp);
extern void amf_comp_instantiate_event(struct amf_comp *comp);
extern void amf_comp_instantiate_tmo_event (struct amf_comp *comp);
extern void amf_comp_cleanup_tmo_event (struct amf_comp *comp);

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
	struct amf_comp *comp, SaAmfRecommendedRecoveryT recommendedRecovery);
extern void amf_comp_cleanup_completed (struct amf_comp *comp);
extern void amf_comp_cleanup_failed_completed (amf_comp_t *comp);

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
	struct amf_comp *comp, amf_comp_t *report_comp,
	SaAmfRecommendedRecoveryT recommendedRecovery);
extern int amf_comp_response_1 (
	SaInvocationT invocation, SaAisErrorT error, SaAisErrorT *retval,
	SaUint32T *interface, SaNameT *dn, SaAmfHealthcheckKeyT *healtcheck_key,
	SaAmfRecommendedRecoveryT *recommendedRecovery);
extern struct amf_comp *amf_comp_response_2 (
	SaUint32T interface, SaNameT *dn, SaAmfHealthcheckKeyT *healthcheckKey,
	SaAisErrorT error, SaAisErrorT *retval, 
	SaAmfRecommendedRecoveryT recommendedRecovery);
extern SaAisErrorT amf_comp_hastate_get (
	struct amf_comp *comp, SaNameT *csi_name, SaAmfHAStateT *ha_state);
extern SaAisErrorT amf_comp_healthcheck_confirm (
	struct amf_comp *comp,
	SaAmfHealthcheckKeyT *healthcheckKey,
	SaAisErrorT healthcheckResult);

extern amf_healthcheck_t *amf_healthcheck_new (struct amf_comp *comp);
extern void *amf_healthcheck_serialize (
	struct amf_healthcheck *healthcheck, int *len);
extern struct amf_healthcheck *amf_healthcheck_deserialize (
	struct amf_comp *comp, char *buf);

extern void amf_comp_csi_remove (amf_comp_t *component,
	amf_csi_assignment_t *csi_assignment);
extern void amf_comp_error_suspected_clear (amf_comp_t *comp);
extern void amf_comp_error_suspected_set (amf_comp_t *comp);
extern int amf_comp_is_error_suspected (amf_comp_t *comp);

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
	struct amf_application *app, char *buf);
extern void *amf_si_assignment_serialize (
	struct amf_si_assignment *si_assignment, int *len);
extern struct amf_si_assignment *amf_si_assignment_deserialize (
	struct amf_si *si, char *buf);
extern struct amf_si_assignment *amf_si_assignment_new (struct amf_si *si);
#if 0
char *amf_si_assignment_dn_make (struct amf_su *su, struct amf_si *si,
	SaNameT *name);
#endif
/**
 * Get number of active assignments for the specified SI
 * @param si
 * 
 * @return int
 */
extern int amf_si_get_saAmfSINumCurrActiveAssignments (struct amf_si *si);
/**
 * Get number of active assignments for the specified SI and SU
 * @param si
 * @param su
 * 
 * @return int
 */
extern int amf_si_su_get_saAmfSINumCurrActiveAssignments (struct amf_si *si,
	struct amf_su *su);

/**
 * Get number of standby assignments for the specified SI
 * @param si
 * 
 * @return int
 */

extern int amf_si_get_saAmfSINumCurrStandbyAssignments (struct amf_si *si);

/**
 * Get number of standby assignments for the specified SI and SU
 * @param si
 * 
 * @return int
 */
extern int amf_si_su_get_saAmfSINumCurrStandbyAssignments (struct amf_si *si,
	struct amf_su *su);

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

extern void amf_si_assignment_remove (amf_si_assignment_t *si_assignment,
	async_func_t async_func);

extern void amf_si_comp_csi_removed (
	struct amf_si *si, struct amf_csi_assignment *csi_assignment,
	SaAisErrorT error);

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
	struct amf_si *si, char *buf);
extern void *amf_csi_assignment_serialize (
	struct amf_csi_assignment *csi_assignment, int *len);
extern struct amf_csi_assignment *amf_csi_assignment_deserialize (
	struct amf_csi *csi, char *buf);
extern char *amf_csi_dn_make (struct amf_csi *csi, SaNameT *name);
extern char *amf_csi_assignment_dn_make (
	struct amf_csi_assignment *csi_assignment, SaNameT *name);
extern struct amf_csi_assignment *amf_csi_assignment_find (
	struct amf_cluster *cluster, SaNameT *name);
extern struct amf_csi_attribute *amf_csi_attribute_new (struct amf_csi *csi);
extern void *amf_csi_attribute_serialize (
	struct amf_csi_attribute *csi_attribute, int *len);
extern struct amf_csi_attribute *amf_csi_attribute_deserialize (
	struct amf_csi *csi, char *buf);
/* extern int sa_amf_grep(const char *string, char *pattern, size_t nmatch, */
/*	char** sub_match_array);                                             */

extern int sa_amf_grep(const char *string, char *pattern, size_t nmatch,
	SaNameT *sub_match_array);

/*===========================================================================*/
extern struct amf_node *this_amf_node;
extern struct amf_cluster *amf_cluster;

#endif /* AMF_H_DEFINED */
