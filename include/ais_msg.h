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
#ifndef AIS_MSG_H_DEFINED
#define AIS_MSG_H_DEFINED

#include <netinet/in.h>
#include "ais_types.h"
#include "evs.h"

enum req_amf_response_interfaces {
	MESSAGE_REQ_AMF_RESPONSE_SAAMFHEALTHCHECKCALLBACK = 1,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFREADINESSSTATESETCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFCOMPONENTTERMINATECALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFCSISETCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFCSIREMOVECALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFEXTERNALCOMPONENTRESTARTCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFEXTERNALCOMPONENTCONTROLCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFPENDINGOPERATIONCONFIRMCALLBACK
};

enum req_init_types {
	MESSAGE_REQ_EVS_INIT,
	MESSAGE_REQ_CLM_INIT,
	MESSAGE_REQ_AMF_INIT,
	MESSAGE_REQ_CKPT_INIT,
	MESSAGE_REQ_CKPT_CHECKPOINT_INIT,
	MESSAGE_REQ_CKPT_SECTIONITERATOR_INIT,
	MESSAGE_REQ_EVT_INIT
};

enum res_init_types {
	MESSAGE_RES_INIT
};

#define	MESSAGE_REQ_LIB_ACTIVATEPOLL 0
#define	MESSAGE_RES_LIB_ACTIVATEPOLL 50

enum req_lib_evs_types {
	MESSAGE_REQ_EVS_JOIN = 1,
	MESSAGE_REQ_EVS_LEAVE,
	MESSAGE_REQ_EVS_MCAST_JOINED,
	MESSAGE_REQ_EVS_MCAST_GROUPS
};

enum res_lib_evs_types {
	MESSAGE_RES_EVS_DELIVER_CALLBACK = 1,
	MESSAGE_RES_EVS_CONFCHG_CALLBACK,
	MESSAGE_RES_EVS_JOIN,
	MESSAGE_RES_EVS_LEAVE,
	MESSAGE_RES_EVS_MCAST_JOINED,
	MESSAGE_RES_EVS_MCAST_GROUPS
};

enum req_clm_types {
	MESSAGE_REQ_CLM_TRACKSTART = 1,
	MESSAGE_REQ_CLM_TRACKSTOP,
	MESSAGE_REQ_CLM_NODEGET
};

enum res_clm_types {
	
	MESSAGE_RES_CLM_TRACKCALLBACK = 1,
	MESSAGE_RES_CLM_TRACKSTART,
	MESSAGE_RES_CLM_TRACKSTOP,
	MESSAGE_RES_CLM_NODEGET,
	MESSAGE_RES_CLM_NODEGETCALLBACK
};

enum req_amf_types {
	MESSAGE_REQ_AMF_COMPONENTREGISTER = 1,
	MESSAGE_REQ_AMF_COMPONENTUNREGISTER,
	MESSAGE_REQ_AMF_READINESSSTATEGET,
	MESSAGE_REQ_AMF_HASTATEGET,
	MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTART,
	MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTOP,
	MESSAGE_REQ_AMF_ERRORREPORT,
	MESSAGE_REQ_AMF_ERRORCANCELALL,
	MESSAGE_REQ_AMF_STOPPINGCOMPLETE,
	MESSAGE_REQ_AMF_RESPONSE,
	MESSAGE_REQ_AMF_COMPONENTCAPABILITYMODELGET
};

enum res_lib_amf_types {
	MESSAGE_RES_AMF_COMPONENTREGISTER = 1,
	MESSAGE_RES_AMF_COMPONENTUNREGISTER,
	MESSAGE_RES_AMF_READINESSSTATEGET,
	MESSAGE_RES_AMF_HASTATEGET,
	MESSAGE_RES_AMF_HEALTHCHECKCALLBACK,
	MESSAGE_RES_AMF_READINESSSTATESETCALLBACK,
	MESSAGE_RES_AMF_COMPONENTTERMINATECALLBACK,
	MESSAGE_RES_AMF_CSISETCALLBACK,
	MESSAGE_RES_AMF_CSIREMOVECALLBACK,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTART,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACKCALLBACK,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTOP,
	MESSAGE_RES_AMF_COMPONENTCAPABILITYMODELGET,
	MESSAGE_RES_AMF_ERRORREPORT,
	MESSAGE_RES_AMF_ERRORCANCELALL,
	MESSAGE_RES_AMF_STOPPINGCOMPLETE,
	MESSAGE_RES_AMF_RESPONSE
};

enum req_lib_ckpt_checkpoint_types {
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPEN = 1,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTUNLINK,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET,
	MESSAGE_REQ_CKPT_CHECKPOINT_ACTIVECHECKPOINTSET,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONCREATE,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONDELETE,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONWRITE,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONOVERWRITE,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONREAD,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC
};

enum req_lib_ckpt_sectioniterator_types {
	MESSAGE_REQ_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE = 1,
	MESSAGE_REQ_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT
};

enum res_lib_ckpt_types {
	MESSAGE_RES_CKPT_CHECKPOINTOPENASYNC = 1,
	MESSAGE_RES_CKPT_CHECKPOINTSYNCHRONIZEASYNC
};

enum res_lib_ckpt_checkpoint_types {
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN = 1,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET,
	MESSAGE_RES_CKPT_CHECKPOINT_ACTIVECHECKPOINTSET,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC
};

enum res_lib_ckpt_sectioniterator_types {
	MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE = 1,
	MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT
};

enum nodeexec_message_types {
	MESSAGE_REQ_EXEC_EVS_MCAST,
	MESSAGE_REQ_EXEC_CLM_NODEJOIN,
	MESSAGE_REQ_EXEC_AMF_COMPONENTREGISTER,
	MESSAGE_REQ_EXEC_AMF_COMPONENTUNREGISTER,
	MESSAGE_REQ_EXEC_AMF_ERRORREPORT,
	MESSAGE_REQ_EXEC_AMF_ERRORCANCELALL,
	MESSAGE_REQ_EXEC_AMF_READINESSSTATESET,
	MESSAGE_REQ_EXEC_AMF_HASTATESET,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET,
	MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE,
	MESSAGE_REQ_EXEC_CKPT_SECTIONDELETE,
	MESSAGE_REQ_EXEC_CKPT_SECTIONEXPIRATIONTIMESET,
	MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE,
	MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE,
	MESSAGE_REQ_EXEC_CKPT_SECTIONREAD,
	MESSAGE_REQ_EXEC_EVT_EVENTDATA,
	MESSAGE_REQ_EXEC_EVT_CHANCMD
};

enum req_evt_types {
	MESSAGE_REQ_EVT_OPEN_CHANNEL = 1,
	MESSAGE_REQ_EVT_CLOSE_CHANNEL,
	MESSAGE_REQ_EVT_SUBSCRIBE,
	MESSAGE_REQ_EVT_UNSUBSCRIBE,
	MESSAGE_REQ_EVT_PUBLISH,
	MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME,
	MESSAGE_REQ_EVT_EVENT_DATA
};

enum res_evt_types {
	MESSAGE_RES_EVT_OPEN_CHANNEL = 1,
	MESSAGE_RES_EVT_CLOSE_CHANNEL,
	MESSAGE_RES_EVT_SUBSCRIBE,
	MESSAGE_RES_EVT_UNSUBSCRIBE,
	MESSAGE_RES_EVT_PUBLISH,
	MESSAGE_RES_EVT_CLEAR_RETENTIONTIME,
	MESSAGE_RES_EVT_CHAN_OPEN_CALLBACK,
	MESSAGE_RES_EVT_EVENT_DATA,
	MESSAGE_RES_EVT_AVAILABLE
};

struct req_header {
	int size;
	int id;
};

struct res_header {
	int size;
	int id;
	SaErrorT error;
};

struct message_source {
	struct conn_info *conn_info;
	struct in_addr in_addr;
};

struct req_execauth_xmit_authkey {
	struct req_header header;
	int authModule;
	unsigned char random[16];
};

struct res_execauth_xmit_signature {
	struct req_header header;
	unsigned char signature[64];
	unsigned int signature_length;
};

struct req_execauth_connection_authorized {
	struct req_header header;
};

struct req_clm_trackstart {
	struct req_header header;
	SaUint8T trackFlags;
	SaClmClusterNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
};

struct res_evs_deliver_callback {
	struct res_header header;
	struct in_addr source_addr;
	int msglen;
	char msg[0];
};

struct res_evs_confchg_callback {
	struct res_header header;
	int member_list_entries;
	int left_list_entries;
	int joined_list_entries;
	struct in_addr member_list[16];
	struct in_addr left_list[16];
	struct in_addr joined_list[16];
};

struct req_lib_evs_join {
	struct res_header header;
	int group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_join {
	struct res_header header;
};

struct req_lib_evs_leave {
	struct res_header header;
	int group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_leave {
	struct res_header header;
};

struct req_lib_evs_mcast_joined {
	struct res_header header;
	evs_priority_t priority;
	evs_guarantee_t guarantee;
	int msg_len;
	char msg[0];
};

struct res_lib_evs_mcast_joined {
	struct res_header header;
};

struct req_lib_evs_mcast_groups {
	struct res_header header;
	evs_priority_t priority;
	evs_guarantee_t guarantee;
	int msg_len;
	int group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_mcast_groups {
	struct res_header header;
};


struct req_exec_evs_mcast {
	struct req_header header;
	int group_entries;
	int msg_len;
	struct evs_group groups[0];
	/* data goes here */
};

struct res_clm_trackstart {
	struct res_header header;
};

struct req_lib_init {
	struct req_header header;
};

struct res_lib_init {
	struct res_header header;
};

struct req_lib_amf_componentregister {
	struct req_header header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct req_exec_amf_componentregister {
	struct req_header header;
	struct message_source source;
	struct req_lib_amf_componentregister req_lib_amf_componentregister;
};

struct res_lib_amf_componentregister {
	struct res_header header;
};

struct req_lib_amf_componentunregister {
	struct req_header header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct req_exec_amf_componentunregister {
	struct req_header header;
	struct message_source source;
	struct req_lib_amf_componentunregister req_lib_amf_componentunregister;
};

struct res_lib_amf_componentunregister {
	struct res_header header;
};

struct req_amf_readinessstateget {
	struct req_header header;
	SaNameT compName;
};

struct res_lib_amf_readinessstateget {
	struct res_header header;
	SaAmfReadinessStateT readinessState;
};

struct res_lib_amf_healthcheckcallback {
	struct res_header header;
	int instance;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfHealthcheckT checkType;
};

struct res_lib_amf_readinessstatesetcallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfReadinessStateT readinessState;
};

struct req_exec_amf_readinessstateset {
	struct req_header header;
	SaNameT compName;
	SaAmfReadinessStateT readinessState;
};

struct req_exec_amf_hastateset {
	struct req_header header;
	SaNameT compName;
	SaAmfHAStateT haState;
};

struct req_exec_ckpt_checkpointclose {
	struct req_header header;
	SaNameT checkpointName;
};

struct req_exec_ckpt_checkpointretentiondurationset {
	struct req_header header;
	SaNameT checkpointName;
	SaTimeT retentionDuration;
};

struct res_lib_amf_componentterminatecallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
};

struct req_amf_hastateget {
	struct req_header header;
	SaNameT compName;
	SaNameT csiName;
};

struct res_lib_amf_hastateget {
	struct res_header header;
	SaAmfHAStateT haState;
};

struct res_lib_amf_csisetcallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaNameT csiName;
	SaAmfCSIFlagsT csiFlags;
	SaAmfHAStateT haState;
	SaNameT activeCompName;
	SaAmfCSITransitionDescriptorT transitionDescriptor;
};

struct res_lib_amf_csiremovecallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaNameT csiName;
	SaAmfCSIFlagsT csiFlags;
};

struct req_amf_protectiongrouptrackstart {
	struct req_header header;
	SaNameT csiName;
	SaUint8T trackFlags;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
};

struct res_lib_amf_protectiongrouptrackstart {
	struct res_header header;
};
	

struct req_amf_protectiongrouptrackstop {
	struct req_header header;
	SaNameT csiName;
};

struct res_lib_amf_protectiongrouptrackstop {
	struct res_header header;
};

struct res_lib_amf_protectiongrouptrackcallback {
	struct res_header header;
	SaNameT csiName;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
	SaUint32T numberOfMembers;
	SaUint32T error;
	SaAmfProtectionGroupNotificationT notificationBuffer[0];
};

struct req_lib_amf_errorreport {
	struct req_header header;
	SaNameT reportingComponent;
	SaNameT erroneousComponent;
	SaTimeT errorDetectionTime;
	SaAmfErrorDescriptorT errorDescriptor;
	SaAmfAdditionalDataT additionalData;
};

struct req_exec_amf_errorreport {
	struct req_header header;
	struct message_source source;
	struct req_lib_amf_errorreport req_lib_amf_errorreport;
};

struct res_lib_amf_errorreport {
	struct res_header header;
};

struct req_lib_amf_errorcancelall {
	struct req_header header;
	SaNameT compName;
};

struct req_exec_amf_errorcancelall {
	struct req_header header;
	struct message_source source;
	struct req_lib_amf_errorcancelall req_lib_amf_errorcancelall;
};
	
struct res_lib_amf_errorcancelall {
	struct res_header header;
};

struct req_amf_response {
	struct req_header header;
	SaInvocationT invocation;
	SaErrorT error;
};

struct res_lib_amf_response {
	struct res_header header;
};

struct req_amf_stoppingcomplete {
	struct req_header header;
	SaInvocationT invocation;
	SaErrorT error;
};

struct res_lib_amf_stoppingcomplete {
	struct req_header header;
};

struct req_amf_componentcapabilitymodelget {
	struct req_header header;
	SaNameT compName;
};

struct res_lib_amf_componentcapabilitymodelget {
	struct res_header header;
	SaAmfComponentCapabilityModelT componentCapabilityModel;
};

struct req_lib_activatepoll {
	struct req_header header;
};

struct res_lib_activatepoll {
	struct res_header header;
};

struct req_lib_ckpt_checkpointopen {
	struct req_header header;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags;
};

struct res_lib_ckpt_checkpointopen {
	struct res_header header;
};

struct req_exec_ckpt_checkpointopen {
	struct req_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointopen req_lib_ckpt_checkpointopen;
};


struct req_lib_ckpt_checkpointopenasync {
	struct req_header header;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags;
	SaInvocationT invocation;
};

struct res_lib_ckpt_checkpointopenasync {
	struct res_header header;
	SaCkptCheckpointHandleT checkpointHandle;
	SaInvocationT invocation;
};

struct req_lib_ckpt_checkpointclose {
	struct req_header header;
};

struct res_lib_ckpt_checkpointclose {
	struct res_header header;
	SaNameT checkpointName;
};

struct req_lib_ckpt_checkpointunlink {
	struct req_header header;
	SaNameT checkpointName;
};

struct res_lib_ckpt_checkpointunlink {
	struct res_header header;
};

struct req_exec_ckpt_checkpointunlink {
	struct req_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointunlink req_lib_ckpt_checkpointunlink;
};

struct req_lib_ckpt_checkpointretentiondurationset {
	struct req_header header;
	SaTimeT retentionDuration;
};
struct res_lib_ckpt_checkpointretentiondurationset {
	struct res_header header;
};

struct req_lib_ckpt_activecheckpointset {
	struct req_header header;
};

struct res_lib_ckpt_activecheckpointset {
	struct res_header header;
};

struct req_lib_ckpt_checkpointstatusget {
	struct req_header header;
};

struct res_lib_ckpt_checkpointstatusget {
	struct res_header header;
	SaCkptCheckpointStatusT checkpointStatus;
};

struct req_lib_ckpt_sectioncreate {
	struct req_header header;
	SaUint32T idLen;
	SaTimeT expirationTime;
	SaUint32T initialDataSize;
};

struct res_lib_ckpt_sectioncreate {
	struct res_header header;
};

struct req_exec_ckpt_sectioncreate {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectioncreate req_lib_ckpt_sectioncreate; /* this must be last */
};

struct req_lib_ckpt_sectiondelete {
	struct req_header header;
	SaUint32T idLen;
};

struct res_lib_ckpt_sectiondelete {
	struct res_header header;
};
struct req_exec_ckpt_sectiondelete {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectiondelete req_lib_ckpt_sectiondelete; /* this must be last */
};

struct req_lib_ckpt_sectionexpirationtimeset {
	struct req_header header;
	SaUint32T idLen;
	SaTimeT expirationTime;
};

struct res_lib_ckpt_sectionexpirationtimeset {
	struct res_header header;
};

struct req_exec_ckpt_sectionexpirationtimeset {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionexpirationtimeset req_lib_ckpt_sectionexpirationtimeset;
};

struct req_lib_ckpt_sectioniteratorinitialize {
	struct req_header header;
	SaNameT checkpointName;
	SaCkptSectionsChosenT sectionsChosen;
	SaTimeT expirationTime;
};

struct res_lib_ckpt_sectioniteratorinitialize {
	struct res_header header;
};

struct req_lib_ckpt_sectioniteratornext {
	struct req_header header;
};

struct res_lib_ckpt_sectioniteratornext {
	struct res_header header;
	SaCkptSectionDescriptorT sectionDescriptor;
};

struct req_lib_ckpt_sectionwrite {
	struct req_header header;
	SaUint32T idLen;
	SaOffsetT dataOffset;
	SaOffsetT dataSize;
};

struct res_lib_ckpt_sectionwrite {
	struct res_header header;
};

struct req_exec_ckpt_sectionwrite {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionwrite req_lib_ckpt_sectionwrite;
};

struct req_lib_ckpt_sectionoverwrite {
	struct req_header header;
	SaUint32T idLen;
	SaUint32T dataSize;
};

struct res_lib_ckpt_sectionoverwrite {
	struct res_header header;
};
	
struct req_exec_ckpt_sectionoverwrite {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionoverwrite req_lib_ckpt_sectionoverwrite;
};

struct req_lib_ckpt_sectionread {
	struct req_header header;
	SaUint32T idLen;
	SaOffsetT dataOffset;
	SaOffsetT dataSize;
};

struct res_lib_ckpt_sectionread {
	struct res_header header;
	SaSizeT dataRead;
};

struct req_exec_ckpt_sectionread {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionread req_lib_ckpt_sectionread;
};

struct req_lib_ckpt_checkpointsynchronize {
	struct req_header header;
};

struct res_lib_ckpt_checkpointsynchronize {
	struct res_header header;
};

struct req_lib_ckpt_checkpointsynchronizeasync {
	struct req_header header;
	SaInvocationT invocation;
};

struct res_lib_ckpt_checkpointsynchronizeasync {
	struct res_header header;
};

struct req_clm_trackstop {
	struct req_header header;
	SaSizeT dataRead;
	SaErrorT error;
};

struct res_clm_trackstop {
	struct res_header header;
};

struct res_clm_trackcallback {
	struct res_header header;
	SaUint64T viewNumber;
	SaUint32T numberOfItems;
	SaUint32T numberOfMembers;
	SaClmClusterNotificationT *notificationBufferAddress;
	SaClmClusterNotificationT notificationBuffer[0];
};

struct req_clm_nodeget {
	struct req_header header;
	SaClmClusterNodeT *clusterNodeAddress;
	SaInvocationT invocation;
	SaClmNodeIdT nodeId;
};

struct res_clm_nodeget {
	struct res_header header;
	SaInvocationT invocation;
	SaClmClusterNodeT *clusterNodeAddress;
	SaClmClusterNodeT clusterNode;
	int valid;
};

struct res_clm_nodegetcallback {
	struct res_header header;
	SaInvocationT invocation;
	SaClmClusterNodeT *clusterNodeAddress;
	SaClmClusterNodeT clusterNode;
	int valid;
};

struct req_exec_clm_heartbeat {
	struct req_header header;
};

struct res_exec_clm_heartbeat {
	struct res_header header;
};

struct req_exec_clm_nodejoin {
	struct req_header header;
	SaClmClusterNodeT clusterNode;
};


/* 
 * MESSAGE_REQ_EVT_OPEN_CHANNEL
 *
 * ico_head				Request head
 * ico_open_flag:		Channel open flags
 * ico_channel_name:	Name of channel to open
 * ico_c_handle:		Local lib channel handle (used in returned event data)
 * ico_timeout:			Used only by open
 * ico_invocation:		Used only by async open
 *
 */
struct req_evt_channel_open {

	struct req_header	ico_head;
	SaUint8T				ico_open_flag;
	SaNameT					ico_channel_name;
	SaEvtChannelHandleT		ico_c_handle;	/* client chan handle */
	SaTimeT					ico_timeout;    /* open only */
	SaInvocationT			ico_invocation; /* open async only */
};

/*
 * MESSAGE_RES_EVT_OPEN_CHANNEL
 * 
 *
 * ico_head:			Results head
 * ico_error:			Request results
 * ico_channel_handle:	Server side channel handle (used in channel ops)
 *
 */
struct res_evt_channel_open {

	struct res_header	ico_head;
	uint32_t				ico_channel_handle;/* svr chan handle */

};

/*
 * MESSAGE_RES_EVT_CHAN_OPEN_CALLBACK
 *
 * TODO: Define this
 */
struct res_evt_open_chan_async {
	struct res_header	ico_head;
};


/*
 * MESSAGE_REQ_EVT_CLOSE_CHANNEL
 *
 * icc_head:			Request head
 * icc_channel_handle:	Server handle of channel to close
 *
 */
struct req_evt_channel_close {

	struct req_header	icc_head;
	uint32_t				icc_channel_handle;
};

/*
 * MESSAGE_RES_EVT_CLOSE_CHANNEL
 *
 * icc_head:		Results head
 * icc_error:		Request result
 *
 */
struct res_evt_channel_close {
	struct res_header	icc_head;
};

/* 
 * MESSAGE_REQ_EVT_SUBSCRIBE
 *
 * ics_head:			Request head
 * ics_channel_handle:	Server handle of channel
 * ics_sub_id:			Subscription ID
 * ics_filter_size:		Size of supplied filter data
 * ics_filter_count:	Number of filters supplied
 * ics_filter_data:		Filter data
 *
 */
struct req_evt_event_subscribe {

	struct req_header	ics_head;
	uint32_t				ics_channel_handle;
	SaEvtSubscriptionIdT	ics_sub_id;
	uint32_t				ics_filter_size;
	uint32_t				ics_filter_count;
	uint8_t					ics_filter_data[0];

};

/*
 * MESSAGE_RES_EVT_SUBSCRIBE
 *
 * ics_head:		Result head
 * ics_error:		Request results
 *
 */
struct res_evt_event_subscribe {
	struct res_header	ics_head;
};

/*
 * MESSAGE_REQ_EVT_UNSUBSCRIBE
 *
 * icu_head:			Request head
 * icu_channel_handle:	Server handle of channel
 * icu_sub_id:			Subscription ID
 *
 */
struct req_evt_event_unsubscribe {

	struct req_header	icu_head;
	uint32_t				icu_channel_handle;
	SaEvtSubscriptionIdT	icu_sub_id;
};


/*
 * MESSAGE_RES_EVT_UNSUBSCRIBE
 *
 * icu_head:		Results head
 * icu_error:		request result
 *
 */
struct res_evt_event_unsubscribe {
	struct res_header	icu_head;

};

/*
 * MESSAGE_REQ_EVT_EVENT_DATA
 * MESSAGE_RES_EVT_AVAILABLE
 *
 * evd_head:		Request Head
 */
struct res_evt_event_data {
		struct res_header	evd_head;
};

/*
 * MESSAGE_REQ_EVT_PUBLISH			(1)
 * MESSAGE_RES_EVT_EVENT_DATA		(2)
 * MESSAGE_REQ_EXEC_EVT_EVENTDATA	(3)
 *
 * led_head:				Request/Results head
 * led_svr_channel_handle:	Server channel handle (1 only)
 * led_lib_channel_handle:	Lib channel handle (2 only)
 * led_chan_name:			Channel name (3 only)
 * led_event_id:			Event ID (2 and 3 only)
 * led_sub_id:				Subscription ID (2 only)
 * led_publisher_node_id:	Node ID of event publisher
 * led_publisher_name:		Node name of event publisher
 * led_retention_time:		Event retention time
 * led_publish_time:		Publication time of the event
 * led_priority:			Event priority
 * led_user_data_offset:	Offset to user data
 * led_user_data_size:		Size of user data
 * led_patterns_number:		Number of patterns in the event
 * led_body:				Pattern and user data
 */
struct lib_event_data {
	struct res_header		led_head;
	uint32_t				led_svr_channel_handle;
	uint32_t				led_lib_channel_handle;
	SaNameT					led_chan_name;
	SaEvtEventIdT			led_event_id;
	SaEvtSubscriptionIdT	led_sub_id;
	SaClmNodeIdT			led_publisher_node_id;
	SaNameT					led_publisher_name;
	SaTimeT					led_retention_time;
	SaTimeT					led_publish_time;
	SaEvtEventPriorityT		led_priority;
	uint32_t				led_user_data_offset;
	uint32_t				led_user_data_size;
	uint32_t				led_patterns_number;
	uint8_t					led_body[0];

};

/*
 * MESSAGE_RES_EVT_PUBLISH
 *
 * iep_head:		Result head
 * iep_error:		Request results
 * iep_event_id:	Event ID of published message
 *
 */
struct res_evt_event_publish {

	struct res_header	iep_head;
	SaEvtEventIdT			iep_event_id;
};

/*
 * MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME
 *
 * Request message:
 *
 * iec_head:			Request head
 * iec_event_id:		ID of event to clear
 * iec_channel_handle:	Server handle of associate channel
 *
 */
struct req_evt_event_clear_retentiontime {

	struct req_header	iec_head;
	uint64_t				iec_event_id;
	uint32_t				iec_channel_handle;

};

/*
 * MESSAGE_RES_EVT_CLEAR_RETENTIONTIME
 *
 * iec_head:		Results head
 * iec_error:		Request result
 *
 */
struct res_evt_event_clear_retentiontime {
	struct res_header	iec_head;
};


/*
 * MESSAGE_REQ_EXEC_EVT_CHANCMD
 *
 * chc_header:	Request head
 * chc_chan:	Channel Name
 * chc_op:		Channel operation (open, close, clear retentiontime)
 */

struct req_evt_chan_command {
	struct req_header 	chc_head;
	int 				chc_op;
	union {
		SaNameT			chc_chan;
		SaEvtEventIdT	chc_event_id;
	} u;
};
#endif /* AIS_MSG_H_DEFINED */
