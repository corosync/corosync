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
#include "saClm.h"
#include "ipc_gen.h"

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

struct message_source {
	struct conn_info *conn_info;
	struct in_addr in_addr;
} __attribute__((packed));

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
	evs_guarantee_t guarantee;
	int msg_len;
	char msg[0];
};

struct res_lib_evs_mcast_joined {
	struct res_header header;
};

struct req_lib_evs_mcast_groups {
	struct res_header header;
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
} __attribute__((packed));

struct req_exec_amf_componentregister {
	struct req_header header;
	struct message_source source;
	struct req_lib_amf_componentregister req_lib_amf_componentregister;
	SaAmfReadinessStateT currentReadinessState;
	SaAmfReadinessStateT newReadinessState;
	SaAmfHAStateT currentHAState;
	SaAmfHAStateT newHAState;
} __attribute__((packed));

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
	struct message_source source;
	SaNameT checkpointName;
	SaTimeT retentionDuration;
};

struct req_exec_ckpt_checkpointretentiondurationexpire {
	struct req_header header;
	SaNameT checkpointName;
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

#endif /* AIS_MSG_H_DEFINED */
