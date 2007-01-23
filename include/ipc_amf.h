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
#ifndef AIS_IPC_AMF_H_DEFINED
#define AIS_IPC_AMF_H_DEFINED

#include <netinet/in.h>
#include "ipc_gen.h"
#include "saAis.h"
#include "saAmf.h"

enum req_lib_amf_types {
	MESSAGE_REQ_AMF_COMPONENTREGISTER = 0,
	MESSAGE_REQ_AMF_COMPONENTUNREGISTER = 1,
	MESSAGE_REQ_AMF_PMSTART = 2,
	MESSAGE_REQ_AMF_PMSTOP = 3,
	MESSAGE_REQ_AMF_HEALTHCHECKSTART = 4,
	MESSAGE_REQ_AMF_HEALTHCHECKCONFIRM = 5,
	MESSAGE_REQ_AMF_HEALTHCHECKSTOP = 6,
	MESSAGE_REQ_AMF_HASTATEGET = 7,
	MESSAGE_REQ_AMF_CSIQUIESCINGCOMPLETE = 8,
	MESSAGE_REQ_AMF_PROTECTIONGROUPTRACK = 9,
	MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTOP = 10,
	MESSAGE_REQ_AMF_COMPONENTERRORREPORT = 11,
	MESSAGE_REQ_AMF_COMPONENTERRORCLEAR = 12,
	MESSAGE_REQ_AMF_RESPONSE = 13
};

enum res_lib_amf_types {
	MESSAGE_RES_AMF_COMPONENTREGISTER = 0,
	MESSAGE_RES_AMF_COMPONENTUNREGISTER = 1,
	MESSAGE_RES_AMF_PMSTART = 2,
	MESSAGE_RES_AMF_PMSTOP = 3,
	MESSAGE_RES_AMF_HEALTHCHECKSTART = 4,
	MESSAGE_RES_AMF_HEALTHCHECKCONFIRM = 5,
	MESSAGE_RES_AMF_HEALTHCHECKSTOP = 6,
	MESSAGE_RES_AMF_HASTATEGET = 7,
	MESSAGE_RES_AMF_CSIQUIESCINGCOMPLETE = 8,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACK = 9,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTOP = 10,
	MESSAGE_RES_AMF_COMPONENTERRORREPORT = 11,
	MESSAGE_RES_AMF_COMPONENTERRORCLEAR = 12,
	MESSAGE_RES_AMF_RESPONSE = 13,
	MESSAGE_RES_AMF_CSISETCALLBACK = 14,
	MESSAGE_RES_AMF_HEALTHCHECKCALLBACK = 15,
	MESSAGE_RES_AMF_CSIREMOVECALLBACK = 16,
	MESSAGE_RES_AMF_COMPONENTTERMINATECALLBACK = 17,
};

struct req_lib_amf_componentregister {
	mar_req_header_t header;
	SaNameT compName;
	SaNameT proxyCompName;
} __attribute__((packed));

struct res_lib_amf_componentregister {
	mar_res_header_t header;
};

struct req_lib_amf_componentunregister {
	mar_req_header_t header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct res_lib_amf_componentunregister {
	mar_res_header_t header;
};

struct req_lib_amf_pmstart {
	mar_req_header_t header;
	SaNameT compName;
	SaUint64T processId;
	SaInt32T descendentsTreeDepth;
	SaAmfPmErrorsT pmErrors;
	SaAmfRecommendedRecoveryT recommendedRecovery;
};

struct res_lib_amf_pmstart {
	mar_res_header_t header;
};

struct req_lib_amf_pmstop {
	mar_req_header_t header;
	SaNameT compName;
	SaAmfPmStopQualifierT stopQualifier;
	SaUint64T processId;
	SaAmfPmErrorsT pmErrors;
};

struct res_lib_amf_pmstop {
	mar_res_header_t header;
};

struct req_lib_amf_healthcheckstart {
	mar_req_header_t header;
	SaNameT compName;
	SaAmfHealthcheckKeyT healthcheckKey;
	SaAmfHealthcheckInvocationT invocationType;
	SaAmfRecommendedRecoveryT recommendedRecovery;
};

struct res_lib_amf_healthcheckstart {
	mar_res_header_t header;
};

struct req_lib_amf_healthcheckconfirm {
	mar_req_header_t header;
	SaNameT compName;
	SaAmfHealthcheckKeyT healthcheckKey;
	SaAisErrorT healthcheckResult;
};

struct res_lib_amf_healthcheckconfirm {
	mar_res_header_t header;
};

struct req_lib_amf_healthcheckstop {
	mar_req_header_t header;
	SaNameT compName;
	SaAmfHealthcheckKeyT healthcheckKey;
};

struct res_lib_amf_healthcheckstop {
	mar_res_header_t header;
};

struct req_lib_amf_hastateget {
	mar_req_header_t header;
	SaNameT compName;
	SaNameT csiName;
};

struct res_lib_amf_hastateget {
	mar_res_header_t header;
	SaAmfHAStateT haState;
};

struct req_lib_amf_csiquiescingcomplete {
	mar_req_header_t header;
	SaInvocationT invocation;
	SaAisErrorT error;
};

struct res_lib_amf_csiquiescingcomplete {
	mar_res_header_t header;
};

struct req_lib_amf_protectiongrouptrack {
	mar_req_header_t header;
	SaNameT csiName;
	SaUint8T trackFlags;
	SaAmfProtectionGroupNotificationBufferT *notificationBufferAddress;
};

struct res_lib_amf_protectiongrouptrack {
	mar_res_header_t header;
};
	

struct req_lib_amf_protectiongrouptrackstop {
	mar_req_header_t header;
	SaNameT csiName;
};

struct res_lib_amf_protectiongrouptrackstop {
	mar_res_header_t header;
};

struct req_lib_amf_componenterrorreport {
	mar_req_header_t header;
	SaNameT reportingComponent;
	SaNameT erroneousComponent;
	SaTimeT errorDetectionTime;
	SaAmfRecommendedRecoveryT recommendedRecovery;
	SaNtfIdentifierT ntfIdentifier;
};

struct res_lib_amf_componenterrorreport {
	mar_res_header_t header;
};

struct req_lib_amf_componenterrorclear {
	mar_req_header_t header;
	SaNameT compName;
};

struct res_lib_amf_componenterrorclear {
	mar_res_header_t header;
};

struct req_lib_amf_response {
	mar_req_header_t header;
	SaInvocationT invocation;
	SaAisErrorT error;
};

struct res_lib_amf_response {
	mar_res_header_t header;
};
struct res_lib_amf_healthcheckcallback {
	mar_res_header_t header;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfHealthcheckKeyT key;
};

#ifdef COMPILE_OUT

struct res_lib_amf_componentterminatecallback {
	mar_res_header_t header;
	SaInvocationT invocation;
	SaNameT compName;
};


#endif

/* struct res_lib_amf_csisetcallback {        */
/*         mar_res_header_t header;          */
/*         SaInvocationT invocation;          */
/*         SaNameT compName;                  */
/*         SaAmfHAStateT haState;             */
/*         SaAmfCSIDescriptorT csiDescriptor; */
/* };                                         */

struct res_lib_amf_csisetcallback {
	mar_res_header_t header;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfHAStateT haState;
	SaAmfCSIFlagsT csiFlags;
	SaNameT csiName;
	SaAmfCSIStateDescriptorT csiStateDescriptor;
	SaUint32T number;
	char csi_attr_buf[1]; /* Actual length will be calculated  */
};

struct res_lib_amf_csiremovecallback {
	mar_res_header_t header;
	SaInvocationT invocation;
	SaNameT compName;
	SaNameT csiName;
	SaAmfCSIFlagsT csiFlags;
};

struct res_lib_amf_componentterminatecallback {
	mar_res_header_t header;
	SaInvocationT invocation;
	SaNameT compName;
};


#ifdef COMPILE_OUT
struct res_lib_amf_protectiongrouptrackcallback {
	mar_res_header_t header;
	SaNameT csiName;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
	SaUint32T numberOfMembers;
	SaUint32T error;
	SaAmfProtectionGroupNotificationT notificationBuffer[0];
};

typedef enum {
        SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_AND_Y_STANDBY = 1,
        SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_OR_Y_STANDBY = 2,
        SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_Y_STANDBY = 3,
        SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_1_STANDBY = 4,
        SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE = 5,
        SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE = 6,
        SA_AMF_COMPONENT_CAPABILITY_NO_ACTIVE = 7
} SaAmfComponentCapabilityModelT;

#endif

#endif /* AIS_IPC_AMF_H_DEFINED */
