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
	MESSAGE_REQ_CLM_INIT,
	MESSAGE_REQ_AMF_INIT,
	MESSAGE_REQ_CKPT_INIT,
	MESSAGE_REQ_CKPT_CHECKPOINT_INIT,
	MESSAGE_REQ_CKPT_SECTIONITERATOR_INIT
};

enum res_init_types {
	MESSAGE_RES_INIT
};

#define	MESSAGE_REQ_LIB_ACTIVATEPOLL 0
#define	MESSAGE_RES_LIB_ACTIVATEPOLL 0

enum req_clm_types {
	MESSAGE_REQ_CLM_TRACKSTART = 1,
	MESSAGE_REQ_CLM_TRACKSTOP,
	MESSAGE_REQ_CLM_NODEGET
};

enum res_clm_types {
	MESSAGE_RES_CLM_TRACKCALLBACK = 1,
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

enum res_amf_types {
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
	MESSAGE_RES_AMF_ERRORCANCELALL
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
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE
};

enum res_lib_ckpt_sectioniterator_types {
	MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE = 1,
	MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT
};

enum nodeexec_message_types {
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
	MESSAGE_REQ_EXEC_CKPT_SECTIONREAD
};

#define MESSAGE_MAGIC 0x5a6b7c8d
struct message_header {
	int magic;
	int size;
	int id;
};

struct message_source {
	struct conn_info *conn_info;
	struct in_addr in_addr;
};

struct message_temp {
	struct message_header header;
	char message_data[0];
};

struct req_execauth_xmit_authkey {
	struct message_header header;
	int authModule;
	unsigned char random[16];
};

struct res_execauth_xmit_signature {
	struct message_header header;
	unsigned char signature[64];
	unsigned int signature_length;
};

struct req_execauth_connection_authorized {
	struct message_header header;
};

struct req_clm_trackstart {
	struct message_header header;
	SaUint8T trackFlags;
	SaClmClusterNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
};

struct req_lib_init {
	struct message_header header;
};

struct res_lib_init {
	struct message_header header;
	SaErrorT error;
};

struct req_lib_amf_componentregister {
	struct message_header header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct req_exec_amf_componentregister {
	struct message_header header;
	struct message_source source;
	struct req_lib_amf_componentregister req_lib_amf_componentregister;
};

struct res_lib_amf_componentregister {
	struct message_header header;
	SaErrorT error;
};

struct req_lib_amf_componentunregister {
	struct message_header header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct req_exec_amf_componentunregister {
	struct message_header header;
	struct message_source source;
	struct req_lib_amf_componentunregister req_lib_amf_componentunregister;
};

struct res_lib_amf_componentunregister {
	struct message_header header;
	SaErrorT error;
};

struct req_amf_readinessstateget {
	struct message_header header;
	SaNameT compName;
};

struct res_amf_readinessstateget {
	struct message_header header;
	SaAmfReadinessStateT readinessState;
	SaErrorT error;
};

struct res_amf_healthcheckcallback {
	struct message_header header;
	int instance;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfHealthcheckT checkType;
};

struct res_amf_readinessstatesetcallback {
	struct message_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfReadinessStateT readinessState;
};

struct req_exec_amf_readinessstateset {
	struct message_header header;
	SaNameT compName;
	SaAmfReadinessStateT readinessState;
};

struct req_exec_amf_hastateset {
	struct message_header header;
	SaNameT compName;
	SaAmfHAStateT haState;
};

struct req_exec_ckpt_checkpointclose {
	struct message_header header;
	SaNameT checkpointName;
};

struct req_exec_ckpt_checkpointretentiondurationset {
	struct message_header header;
	SaNameT checkpointName;
	SaTimeT retentionDuration;
};

struct res_amf_componentterminatecallback {
	struct message_header header;
	SaInvocationT invocation;
	SaNameT compName;
};

struct req_amf_hastateget {
	struct message_header header;
	SaNameT compName;
	SaNameT csiName;
};

struct res_amf_hastateget {
	struct message_header header;
	SaAmfHAStateT haState;
	SaErrorT error;
};

struct res_amf_csisetcallback {
	struct message_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaNameT csiName;
	SaAmfCSIFlagsT csiFlags;
	SaAmfHAStateT haState;
	SaNameT activeCompName;
	SaAmfCSITransitionDescriptorT transitionDescriptor;
};

struct res_amf_csiremovecallback {
	struct message_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaNameT csiName;
	SaAmfCSIFlagsT csiFlags;
};

struct req_amf_protectiongrouptrackstart {
	struct message_header header;
	SaNameT csiName;
	SaUint8T trackFlags;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
};

struct res_amf_protectiongrouptrackstart {
	struct message_header header;
	SaErrorT error;
};
	

struct req_amf_protectiongrouptrackstop {
	struct message_header header;
	SaNameT csiName;
};

struct res_amf_protectiongrouptrackstop {
	struct message_header header;
	SaErrorT error;
};

struct res_amf_protectiongrouptrackcallback {
	struct message_header header;
	SaNameT csiName;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
	SaUint32T numberOfMembers;
	SaUint32T error;
	SaAmfProtectionGroupNotificationT notificationBuffer[0];
};

struct req_lib_amf_errorreport {
	struct message_header header;
	SaNameT reportingComponent;
	SaNameT erroneousComponent;
	SaTimeT errorDetectionTime;
	SaAmfErrorDescriptorT errorDescriptor;
	SaAmfAdditionalDataT additionalData;
};

struct req_exec_amf_errorreport {
	struct message_header header;
	struct message_source source;
	struct req_lib_amf_errorreport req_lib_amf_errorreport;
};

struct res_lib_amf_errorreport {
	struct message_header header;
	SaErrorT error;
};

struct req_lib_amf_errorcancelall {
	struct message_header header;
	SaNameT compName;
};

struct req_exec_amf_errorcancelall {
	struct message_header header;
	struct message_source source;
	struct req_lib_amf_errorcancelall req_lib_amf_errorcancelall;
};
	
struct res_lib_amf_errorcancelall {
	struct message_header header;
	SaErrorT error;
};

struct req_amf_response {
	struct message_header header;
	SaInvocationT invocation;
	SaErrorT error;
};

struct req_amf_stoppingcomplete {
	struct message_header header;
	SaInvocationT invocation;
	SaErrorT error;
};

struct req_amf_componentcapabilitymodelget {
	struct message_header header;
	SaNameT compName;
};

struct res_amf_componentcapabilitymodelget {
	struct message_header header;
	SaAmfComponentCapabilityModelT componentCapabilityModel;
	SaErrorT error;
};

struct req_lib_activatepoll {
	struct message_header header;
};

struct res_lib_activatepoll {
	struct message_header header;
};

struct req_lib_ckpt_checkpointopen {
	struct message_header header;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags;
};

struct res_lib_ckpt_checkpointopen {
	struct message_header header;
	SaErrorT error;
};

struct req_exec_ckpt_checkpointopen {
	struct message_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointopen req_lib_ckpt_checkpointopen;
};


struct req_lib_ckpt_checkpointopenasync {
	struct message_header header;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags;
	SaInvocationT invocation;
};

struct res_lib_ckpt_checkpointopenasync {
	struct message_header header;
	SaCkptCheckpointHandleT checkpointHandle;
	SaInvocationT invocation;
	SaErrorT error;
};

struct req_lib_ckpt_checkpointclose {
	struct message_header header;
};

struct res_lib_ckpt_checkpointclose {
	struct message_header header;
	SaNameT checkpointName;
};

struct req_lib_ckpt_checkpointunlink {
	struct message_header header;
	SaNameT checkpointName;
};

struct res_lib_ckpt_checkpointunlink {
	struct message_header header;
	SaErrorT error;
};

struct req_exec_ckpt_checkpointunlink {
	struct message_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointunlink req_lib_ckpt_checkpointunlink;
};

struct req_lib_ckpt_checkpointretentiondurationset {
	struct message_header header;
	SaTimeT retentionDuration;
};

struct req_lib_ckpt_activecheckpointset {
	struct message_header header;
};

struct res_lib_ckpt_activecheckpointset {
	struct message_header header;
	SaErrorT error;
};

struct req_lib_ckpt_checkpointstatusget {
	struct message_header header;
};

struct res_lib_ckpt_checkpointstatusget {
	struct message_header header;
	SaCkptCheckpointStatusT checkpointStatus;
};

struct req_lib_ckpt_sectioncreate {
	struct message_header header;
	SaUint32T idLen;
	SaTimeT expirationTime;
	SaUint32T initialDataSize;
};

struct res_lib_ckpt_sectioncreate {
	struct message_header header;
	SaErrorT error;
};

struct req_exec_ckpt_sectioncreate {
	struct message_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectioncreate req_lib_ckpt_sectioncreate; /* this must be last */
};

struct req_lib_ckpt_sectiondelete {
	struct message_header header;
	SaUint32T idLen;
};

struct res_lib_ckpt_sectiondelete {
	struct message_header header;
	SaErrorT error;
};
struct req_exec_ckpt_sectiondelete {
	struct message_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectiondelete req_lib_ckpt_sectiondelete; /* this must be last */
};

struct req_lib_ckpt_sectionexpirationtimeset {
	struct message_header header;
	SaUint32T idLen;
	SaTimeT expirationTime;
};

struct res_lib_ckpt_sectionexpirationtimeset {
	struct message_header header;
	SaErrorT error;
};

struct req_exec_ckpt_sectionexpirationtimeset {
	struct message_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionexpirationtimeset req_lib_ckpt_sectionexpirationtimeset;
};

struct req_lib_ckpt_sectioniteratorinitialize {
	struct message_header header;
	SaNameT checkpointName;
	SaCkptSectionsChosenT sectionsChosen;
	SaTimeT expirationTime;
};

struct res_lib_ckpt_sectioniteratorinitialize {
	struct message_header header;
	SaErrorT error;
};

struct req_lib_ckpt_sectioniteratornext {
	struct message_header header;
};

struct res_lib_ckpt_sectioniteratornext {
	struct message_header header;
	SaCkptSectionDescriptorT sectionDescriptor;
	SaErrorT error;
};

struct req_lib_ckpt_sectionwrite {
	struct message_header header;
	SaUint32T idLen;
	SaOffsetT dataOffset;
	SaOffsetT dataSize;
};

struct res_lib_ckpt_sectionwrite {
	struct message_header header;
	SaErrorT error;
};

struct req_exec_ckpt_sectionwrite {
	struct message_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionwrite req_lib_ckpt_sectionwrite;
};

struct req_lib_ckpt_sectionoverwrite {
	struct message_header header;
	SaUint32T idLen;
	SaUint32T dataSize;
};

struct res_lib_ckpt_sectionoverwrite {
	struct message_header header;
	SaErrorT error;
};
	
struct req_exec_ckpt_sectionoverwrite {
	struct message_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionoverwrite req_lib_ckpt_sectionoverwrite;
};

struct req_lib_ckpt_sectionread {
	struct message_header header;
	SaUint32T idLen;
	SaOffsetT dataOffset;
	SaOffsetT dataSize;
};

struct res_lib_ckpt_sectionread {
	struct message_header header;
	SaErrorT error;
	SaSizeT dataRead;
};

struct req_exec_ckpt_sectionread {
	struct message_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionread req_lib_ckpt_sectionread;
};

struct req_lib_ckpt_checkpointsynchronize {
	struct message_header header;
};

struct res_lib_ckpt_checkpointsynchronize {
	struct message_header header;
	SaErrorT error;
};

struct req_lib_ckpt_checkpointsynchronizeasync {
	struct message_header header;
	SaInvocationT invocation;
};

struct req_clm_trackstop {
	struct message_header header;
	SaSizeT dataRead;
	SaErrorT error;
};
	
struct res_clm_trackcallback {
	struct message_header header;
	SaUint64T viewNumber;
	SaUint32T numberOfItems;
	SaUint32T numberOfMembers;
	SaClmClusterNotificationT *notificationBufferAddress;
	SaClmClusterNotificationT notificationBuffer[0];
};

struct req_clm_nodeget {
	struct message_header header;
	SaClmClusterNodeT *clusterNodeAddress;
	SaInvocationT invocation;
	SaClmNodeIdT nodeId;
};

struct res_clm_nodeget {
	struct message_header header;
	SaInvocationT invocation;
	SaClmClusterNodeT *clusterNodeAddress;
	SaClmClusterNodeT clusterNode;
	int valid;
};

struct res_clm_nodegetcallback {
	struct message_header header;
	SaInvocationT invocation;
	SaClmClusterNodeT *clusterNodeAddress;
	SaClmClusterNodeT clusterNode;
	int valid;
};

struct req_exec_clm_heartbeat {
	struct message_header header;
};

struct res_exec_clm_heartbeat {
	struct message_header header;
};

struct req_exec_clm_nodejoin {
	struct message_header header;
	SaClmClusterNodeT clusterNode;
};

#endif /* AIS_MSG_H_DEFINED */
