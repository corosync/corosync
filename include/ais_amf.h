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

#ifndef AIS_AMF_H_DEFINED
#define AIS_AMF_H_DEFINED

#include "saAis.h"

typedef void (*SaAmfHealthcheckCallbackT) (
	SaInvocationT invocation,
	const SaNameT *compName,
	SaAmfHealthcheckT checkType);

typedef void (*SaAmfReadinessStateSetCallbackT) (
	SaInvocationT invocation,
	const SaNameT *compName,
	SaAmfReadinessStateT readinessState);

typedef void (*SaAmfComponentTerminateCallbackT) (
	SaInvocationT invocation,
	const SaNameT *compName);

typedef void (*SaAmfCSISetCallbackT) (
	SaInvocationT invocation,
	const SaNameT *compName,
	const SaNameT *csiName,
	SaAmfCSIFlagsT csiFlags,
	SaAmfHAStateT *haState,
	SaNameT *activeCompName,
	SaAmfCSITransitionDescriptorT transitionDescriptor);
			
typedef void (*SaAmfCSIRemoveCallbackT) (
	SaInvocationT invocation,
	const SaNameT *compName,
	const SaNameT *csiName,
	const SaAmfCSIFlagsT *csiFlags);

typedef void (*SaAmfProtectionGroupTrackCallbackT) (
	const SaNameT *csiName,
	SaAmfProtectionGroupNotificationT *notificationBuffer,
	SaUint32T numberOfItems,
	SaUint32T numberOfMembers,
	SaErrorT error);

typedef void (*SaAmfExternalComponentRestartCallbackT) (
	SaInvocationT invocation,
	const SaNameT *externalCompName);

typedef void (*SaAmfExternalComponentControlCallbackT) (
	const SaInvocationT invocation,
	const SaNameT *externalCompName,
	SaAmfExternalComponentActionT controlAction);

typedef void (*SaAmfPendingOperationConfirmCallbackT) (
	const SaInvocationT invocation,
	const SaNameT *compName,
	SaAmfPendingOperationFlagsT pendingOperationFlags);

typedef void (*SaAmfPendingOperationExpiredCallbackT) (
	const SaNameT *compName,
	SaAmfPendingOperationFlagsT pendingOperationFlags);

typedef struct {
	SaAmfHealthcheckCallbackT
		saAmfHealthcheckCallback;
	SaAmfReadinessStateSetCallbackT
		saAmfReadinessStateSetCallback;
	SaAmfComponentTerminateCallbackT
		saAmfComponentTerminateCallback;
	SaAmfCSISetCallbackT
		saAmfCSISetCallback;
	SaAmfCSIRemoveCallbackT
		saAmfCSIRemoveCallback;
	SaAmfProtectionGroupTrackCallbackT
		saAmfProtectionGroupTrackCallback;
	SaAmfExternalComponentRestartCallbackT
		saAmfExternalComponentRestartCallback;
	SaAmfExternalComponentControlCallbackT
		saAmfExternalComponentControlCallback;
	SaAmfPendingOperationConfirmCallbackT
		saAmfPendingOperationConfirmCallback;
	SaAmfPendingOperationExpiredCallbackT
		saAmfPendingOperationExpiredCallback;
} SaAmfCallbacksT;

/*
 * Interfaces
 */
#ifdef __cplusplus
extern "C" {
#endif

SaErrorT
saAmfInitialize (
	SaAmfHandleT *amfHandle,
	const SaAmfCallbacksT *amfCallbacks,
	const SaVersionT *version);

SaErrorT
saAmfSelectionObjectGet (
	const SaAmfHandleT *amfHandle,
	SaSelectionObjectT *selectionObject);

SaErrorT
saAmfDispatch (
	const SaAmfHandleT *amfHandle,
	SaDispatchFlagsT dispatchFlags);

SaErrorT
saAmfFinalize (
	const SaAmfHandleT *amfHandle);

SaErrorT
saAmfComponentRegister (
	const SaAmfHandleT *amfHandle,
	const SaNameT *compName,
	const SaNameT *proxyCompName);

SaErrorT
saAmfComponentUnregister (
	const SaAmfHandleT *amfHandle,
	const SaNameT *compName,
	const SaNameT *proxyCompName);

SaErrorT
saAmfCompNameGet (
	const SaAmfHandleT *amfHandle,
	SaNameT *compName);

SaErrorT
saAmfReadinessStateGet (
	const SaNameT *compName,
	SaAmfReadinessStateT *readinessState);

SaErrorT
saAmfStoppingComplete (
	SaInvocationT invocation,
	SaErrorT error);

SaErrorT
saAmfHAStateGet (
	const SaNameT *compName,
	const SaNameT *csiName,
	SaAmfHAStateT *haState);

SaErrorT
saAmfProtectionGroupTrackStart (
	const SaAmfHandleT *amfHandle,
	const SaNameT *csiName,
	SaUint8T trackFlags,
	const SaAmfProtectionGroupNotificationT *notificationBuffer,
	SaUint32T numberOfItems);

SaErrorT
saAmfProtectionGroupTrackStop (
	const SaAmfHandleT *amfHandle,
	const SaNameT *csiName);

SaErrorT
saAmfErrorReport (
	const SaNameT *reportingComponent,
	const SaNameT *erroneousComponent,
	SaTimeT errorDetectionTime,
	const SaAmfErrorDescriptorT *errorDescriptor,
	const SaAmfAdditionalDataT *additionalData);

SaErrorT
saAmfErrorCancelAll (
	const SaNameT *compName);

SaErrorT
saAmfComponentCapabilityModelGet (
	const SaNameT *compName,
	SaAmfComponentCapabilityModelT *componentCapabilityModel);

SaErrorT
saAmfPendingOperationGet (
	const SaNameT *compName,
	SaAmfPendingOperationFlagsT *pendingOperationFlags);

SaErrorT
saAmfResponse (
	SaInvocationT invocation,
	SaErrorT error);

#ifdef __cplusplus
}
#endif

#endif /* AIS_AMF_H_DEFINED */
