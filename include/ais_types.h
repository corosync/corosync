/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
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

#ifndef AIS_TYPES_H_DEFINED
#define AIS_TYPES_H_DEFINED

typedef enum {
	SA_FALSE = 0,
	SA_TRUE = 1
} SaBoolT;

typedef char SaInt8T;
typedef short SaInt16T;
typedef long SaInt32T;
typedef long long SaInt64T;

typedef unsigned char SaUint8T;
typedef unsigned short SaUint16T;
typedef unsigned long SaUint32T;
typedef unsigned long long SaUint64T;

typedef SaInt64T SaTimeT;

#define SA_MAX_NAME_LENGTH 256
typedef struct {
		SaUint16T length;
		unsigned char value[SA_MAX_NAME_LENGTH];
} SaNameT;

typedef struct {
	char releaseCode;
	unsigned char major;
	unsigned char minor;
} SaVersionT;

#define SA_TRACK_CURRENT 0x01
#define SA_TRACK_CHANGES 0x02
#define SA_TRACK_CHANGES_ONLY 0x04

#define SA_CLM_LOCAL_NODE_ID 0x00

typedef enum {
	SA_DISPATCH_ONE = 1,
	SA_DISPATCH_ALL = 2,
	SA_DISPATCH_BLOCKING = 3
} SaDispatchFlagsT;

typedef enum {
	SA_OK = 1,
	SA_ERR_LIBRARY = 2,
	SA_ERR_VERSION = 3,
	SA_ERR_INIT = 4,
	SA_ERR_TIMEOUT = 5,
	SA_ERR_TRY_AGAIN = 6,
	SA_ERR_INVALID_PARAM = 7,
	SA_ERR_NO_MEMORY = 8,
	SA_ERR_BAD_HANDLE = 9,
	SA_ERR_BUSY = 10,
	SA_ERR_ACCESS = 11,
	SA_ERR_NOT_EXIST = 12,
	SA_ERR_NAME_TOO_LONG = 13,
	SA_ERR_EXIST = 14,
	SA_ERR_NO_SPACE = 15,
	SA_ERR_INTERRUPT = 16,
	SA_ERR_SYSTEM = 17,
	SA_ERR_NAME_NOT_FOUND = 18,
	SA_ERR_NO_RESOURCES = 19,
	SA_ERR_NOT_SUPPORTED = 20,
	SA_ERR_BAD_OPERATION = 21,
	SA_ERR_FAILED_OPERATION = 22,
	SA_ERR_MESSAGE_ERROR = 23,
	SA_ERR_NO_MESSAGE = 24,
	SA_ERR_QUEUE_FULL = 25,
	SA_ERR_QUEUE_NOT_AVAILABLE = 26,
	SA_ERR_BAD_CHECKPOINT = 27,
	SA_ERR_BAD_FLAGS = 28,
	SA_ERR_SECURITY = 29
} SaErrorT;

typedef SaUint32T SaClmNodeIdT;

typedef int SaClmHandleT;
typedef int SaSelectionObjectT;
typedef int SaInvocationT;

#define SA_CLM_MAX_ADDRESS_LENGTH 64
typedef struct {
	SaUint8T length;
	unsigned char value[SA_CLM_MAX_ADDRESS_LENGTH];
} SaClmNodeAddressT;

typedef struct {
	SaClmNodeIdT nodeId;
	SaClmNodeAddressT nodeAddress;
	SaNameT nodeName;
	SaNameT clusterName;
	SaBoolT member;
	SaTimeT bootTimestamp;
} SaClmClusterNodeT;

typedef enum {
	SA_CLM_NODE_NO_CHANGE = 1,
	SA_CLM_NODE_JOINED = 2,
	SA_CLM_NODE_LEFT = 3
} SaClmClusterChangesT;

typedef struct {
	SaClmClusterNodeT clusterNode;
	SaClmClusterChangesT clusterChanges;
} SaClmClusterNotificationT;

/*
 * AMF Definitions
 */
typedef int SaAmfHandleT;

typedef enum {
	SA_AMF_HEARTBEAT = 1,
	SA_AMF_HEALTHCHECK_LEVEL1 = 2,
	SA_AMF_HEALTHCHECK_LEVEL2 = 3,
	SA_AMF_HEALTHCHECK_LEVEL3 = 4
} SaAmfHealthcheckT;

typedef enum {
	SA_AMF_OUT_OF_SERVICE = 1,
	SA_AMF_IN_SERVICE = 2,
	SA_AMF_STOPPING = 3
} SaAmfReadinessStateT;

typedef enum {
	SA_AMF_ACTIVE = 1,
	SA_AMF_STANDBY = 2,
	SA_AMF_QUIESCED = 3
} SaAmfHAStateT;

typedef enum {
	SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_AND_Y_STANDBY = 1,
	SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_OR_Y_STANDBY = 2,
	SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_Y_STANDBY = 3,
	SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_1_STANDBY = 4,
	SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE = 5,
	SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE = 6,
	SA_AMF_COMPONENT_CAPABILITY_NO_ACTIVE = 7
} SaAmfComponentCapabilityModelT;

#define SA_AMF_CSI_ADD_NEW_ISTANCE 0x01
#define SA_AMF_CSI_ALL_INSTANCES 0x02

typedef SaUint32T SaAmfCSIFlagsT;

typedef enum {
	SA_AMF_CSI_NEW_ASSIGN = 1,
	SA_AMF_CSI_QUIESCED = 2,
	SA_AMF_CSI_NOT_QUIESCED = 3,
	SA_AMF_CSI_STILL_ACTIVE = 4
} SaAmfCSITransitionDescriptorT;

typedef enum {
	SA_AMF_RESET = 1,
	SA_AMF_REBOOT = 2,
	SA_AMF_POWER_ON = 3,
	SA_AMF_POWER_OFF = 4
} SaAmfExternalComponentActionT;

#define SA_AMF_SWITCHOVER_OPERATION 0x01
#define SA_AMF_SHUTDOWN_OPERATION 0x02
typedef SaUint32T SaAmfPendingOperationFlagsT;

typedef struct {
	SaNameT compName;
	SaAmfReadinessStateT readinessState;
	SaAmfHAStateT haState;
} SaAmfProtectionGroupMemberT;

typedef enum {
	SA_AMF_PROTECTION_GROUP_NO_CHANGE = 1,
	SA_AMF_PROTECTION_GROUP_ADDED = 2,
	SA_AMF_PROTECTION_GROUP_REMOVED = 3,
	SA_AMF_PROTECTION_GROUP_STATE_CHANGE = 4
} SaAmfProtectionGroupChangesT;

typedef struct {
	SaAmfProtectionGroupMemberT member;
	SaAmfProtectionGroupChangesT change;
} SaAmfProtectionGroupNotificationT;

typedef enum {
	SA_AMF_COMMUNICATION_ALARM_TYPE = 1,
	SA_AMF_QUALITY_OF_SERVICE_ALARM_TYPE = 2,
	SA_AMF_PROCESSING_ERROR_ALARM_TYPE = 3,
	SA_AMF_EQUIPMENT_ALARM_TYPE = 4,
	SA_AMF_ENVIRONMENTAL_ALARM_TYPE = 5
} SaAmfErrorReportTypeT;

typedef enum {
	SA_AMF_APPLICATION_SUBSYSTEM_FAILURE = 1,
	SA_AMF_BANDWIDTH_REDUCED = 2,
	SA_AMF_CALL_ESTABLISHMENT_ERROR = 3,
	SA_AMF_COMMUNICATION_PROTOCOL_ERROR = 4,
	SA_AMF_COMMUNICATION_SUBSYSTEM_FAILURE = 5,
	SA_AMF_CONFIGURATION_ERROR = 6,
	SA_AMF_CONGESTION = 7,
	SA_AMF_CORRUPT_DATA = 8,
	SA_AMF_CPU_CYCLES_LIMIT_EXCEEDED = 9,
	SA_AMF_EQUIPMENT_MALFUNCTION = 10,
	SA_AMF_FILE_ERROR = 11,
	SA_AMF_IO_DEVICE_ERROR = 12,
	SA_AMF_LAN_ERROR, SA_AMF_OUT_OF_MEMORY = 13,
	SA_AMF_PERFORMANCE_DEGRADED = 14,
	SA_AMF_PROCESSOR_PROBLEM = 15,
	SA_AMF_RECEIVE_FAILURE = 16,
	SA_AMF_REMOTE_NODE_TRNASMISSION_ERROR = 17,
	SA_AMF_RESOURCE_AT_OR_NEARING_CAPACITY = 18,
	SA_AMF_RESPONSE_TIME_EXCESSIVE = 19,
	SA_AMF_RETRANSMISSION_RATE_EXCESSIVE = 20,
	SA_AMF_SOFTWARE_ERROR = 21,
	SA_AMF_SOFTWARE_PROGRAM_ABNORMALLY_TERMINATED = 22,
	SA_AMF_SOFTWARE_PROGRAM_ERROR = 23,
	SA_AMF_STORAGE_CAPACITY_PROBLEM = 24,
	SA_AMF_TIMING_PROBLEM = 25,
	SA_AMF_UNDERLYING_REOUSRCE_UNAVAILABLE = 26,
	SA_AMF_INTERNAL_ERROR = 27,
	SA_AMF_NO_SERVICE_ERROR = 28,
	SA_AMF_SOFTWARE_LIBRARY_ERROR = 29,
	SA_AMF_NOT_RESPONDING = 30
} SaAmfProbableCauseT;

typedef enum {
	SA_AMF_CLEARED = 1,
	SA_AMF_NO_IMPACT = 2,
	SA_AMF_INDETERMINATE = 3,
	SA_AMF_CRITICAL = 4,
	SA_AMF_MAJOR = 5,
	SA_AMF_WEDGED_COMPONENT_FAILURE = 6,
	SA_AMF_COMPONENT_TERMINATED_FAILURE = 7,
	SA_AMF_NODE_FAILURE = 8,
	SA_AMF_MINOR = 9,
	SA_AMF_WARNING = 10
} SaAmfErrorImpactAndSeverityT;

typedef enum {
	SA_AMF_NO_RECOMMENDATION = 1,
	SA_AMF_INTERNALLY_RECOVERED = 2,
	SA_AMF_COMPONENT_RESTART = 3,
	SA_AMF_COMPONENT_FAILOVER = 4,
	SA_AMF_NODE_SWITCHOVER = 5,
	SA_AMF_NODE_FAILOVER = 6,
	SA_AMF_NODE_FAILFAST = 7,
	SA_AMF_CLUSTER_RESET = 8
} SaAmfRecommendedRecoveryT;

typedef struct {
	SaAmfErrorReportTypeT errorReportType;
	SaAmfProbableCauseT probableCause;
	SaAmfErrorImpactAndSeverityT errorImpactAndSeverity;
	SaAmfRecommendedRecoveryT recommendedRecovery;
} SaAmfErrorDescriptorT;

typedef int SaSizeT;
#define SA_AMF_OPAQUE_BUFFER_SIZE_MAX 512
typedef struct {
	char *buffer;
	SaSizeT size;
} SaAmfErrorBufferT;

typedef struct {
	SaAmfErrorBufferT *specificProblem;
	SaAmfErrorBufferT *additionalText;
	SaAmfErrorBufferT *additionalInformation;
} SaAmfAdditionalDataT;

typedef int SaCkptHandleT;

typedef int SaCkptCheckpointHandleT;

typedef int SaCkptSectionIteratorT;

#define SA_CKPT_WR_ALL_REPLICAS 0x1
#define SA_CKPT_WR_ACTIVE_REPLICA 0x2
#define SA_CKPT_WR_ACTIVE_REPLICA_WEAK 0x4

typedef SaUint32T SaCkptCheckpointCreationFlagsT;

typedef struct {
	SaCkptCheckpointCreationFlagsT creationFlags;
	SaSizeT checkpointSize;
	SaTimeT retentionDuration;
	SaUint32T maxSections;
	SaSizeT maxSectionSize;
	SaUint32T maxSectionIdSize;
} SaCkptCheckpointCreationAttributesT;

#define SA_CKPT_CHECKPOINT_READ 0x1
#define SA_CKPT_CHECKPOINT_WRITE 0x2
#define SA_CKPT_CHECKPOINT_COLOCATED 0x4
typedef SaUint32T SaCkptCheckpointOpenFlagsT;

#define SA_CKPT_DEFAULT_SECTION_ID { 0, 0 }
#define SA_CKPT_GENERATED_SECTION_ID { 0, 0 }

typedef struct {
	SaUint8T *id;
	SaUint32T idLen;
} SaCkptSectionIdT;

typedef struct {
	SaCkptSectionIdT *sectionId;
	SaTimeT expirationTime;
} SaCkptSectionCreationAttributesT;

typedef enum {
	SA_CKPT_SECTION_VALID = 1,
	SA_CKPT_SECTION_CORRUPTED = 2
} SaCkptSectionStateT;

typedef struct {
	SaCkptSectionIdT sectionId;
	SaTimeT expirationTime;
	SaSizeT sectionSize;
	SaCkptSectionStateT sectionState;
	SaTimeT lastUpdate;
} SaCkptSectionDescriptorT;

typedef enum {
	SA_CKPT_SECTIONS_FOREVER = 1,
	SA_CKPT_SECTIONS_LEQ_EXPIRATION_TIME = 2,
	SA_CKPT_SECTIONS_GEQ_EXPIRATION_TIME = 3,
	SA_CKPT_SECTIONS_CORRUPTED = 4,
	SA_CKPT_SECTIONS_ANY = 5
} SaCkptSectionsChosenT;

typedef SaUint32T SaOffsetT;

typedef struct {
	SaCkptSectionIdT sectionId;
	void *dataBuffer;
	SaSizeT dataSize;
	SaOffsetT dataOffset;
	SaSizeT readSize;
} SaCkptIOVectorElementT;

typedef struct {
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaUint32T numberOfSections;
	SaUint32T memoryUsed;
} SaCkptCheckpointStatusT;

typedef void (*SaCkptCheckpointOpenCallbackT) (
	SaInvocationT invocation,	
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaErrorT error);

typedef void (*SaCkptCheckpointSynchronizeCallbackT) (
	SaInvocationT invocation,	
	SaErrorT error);

typedef struct {
	SaCkptCheckpointOpenCallbackT saCkptCheckpointOpenCallback;
	SaCkptCheckpointSynchronizeCallbackT saCkptCheckpointSynchronizeCallback;
} SaCkptCallbacksT;


typedef SaUint32T SaEvtHandleT;
typedef SaUint32T SaEvtEventHandleT;
typedef SaUint32T SaEvtChannelHandleT;
typedef SaUint32T SaEvtSubscriptionIdT;


typedef void
(*SaEvtEventDeliverCallbackT)(
	SaEvtSubscriptionIdT subscriptionId,
	const SaEvtEventHandleT eventHandle,
	const SaSizeT eventDataSize
);

typedef void 
(*SaEvtChannelOpenCallbackT)(
	SaInvocationT invocation,
	SaEvtChannelHandleT channelHandle,
	SaErrorT error
);

typedef struct{
    const SaEvtChannelOpenCallbackT saEvtChannelOpenCallback;
    const SaEvtEventDeliverCallbackT saEvtEventDeliverCallback; 
} SaEvtCallbacksT;

#define SA_EVT_CHANNEL_PUBLISHER  0X1
#define SA_EVT_CHANNEL_SUBSCRIBER 0X2
#define SA_EVT_CHANNEL_CREATE     0X4
typedef SaUint8T SaEvtChannelOpenFlagsT;

typedef struct {
    SaUint8T *pattern;
    SaSizeT patternSize;
} SaEvtEventPatternT;


#define SA_EVT_HIGHEST_PRIORITY 0
#define SA_EVT_LOWEST_PRIORITY  3

#define SA_EVT_DATA_MAX_LEN (64 * 1024)
#define SA_EVT_LOST_EVENT "SA_EVT_LOST_EVENT_PATTERN"

typedef struct {
    SaEvtEventPatternT *patterns;
    SaSizeT patternsNumber;
} SaEvtEventPatternArrayT;

typedef SaUint8T SaEvtEventPriorityT;
typedef SaUint64T SaEvtEventIdT;

typedef enum {
    SA_EVT_PREFIX_FILTER = 1,
    SA_EVT_SUFFIX_FILTER = 2,
    SA_EVT_EXACT_FILTER = 3,
    SA_EVT_PASS_ALL_FILTER = 4
} SaEvtEventFilterTypeT;

typedef struct {
    SaEvtEventFilterTypeT filterType;
    SaEvtEventPatternT filter;
} SaEvtEventFilterT;

typedef struct {
    SaEvtEventFilterT *filters;
    SaSizeT filtersNumber;
} SaEvtEventFilterArrayT;


#endif /* AIS_TYPES_H_DEFINED */
