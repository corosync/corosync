
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

#include "ais_types.h"

#ifndef AIS_CLM_H_DEFINED
#define AIS_CLM_H_DEFINED

typedef void (*SaClmClusterNodeGetCallbackT)(
	SaInvocationT invocation,
	SaClmClusterNodeT *clusterNode,
	SaErrorT error);

typedef void (*SaClmClusterTrackCallbackT) (
	SaClmClusterNotificationT *notificationBuffer,
	SaUint32T numberOfItems,
	SaUint32T numberOfMembers,
	SaUint64T viewNumber,
	SaErrorT error);

typedef struct {
	SaClmClusterNodeGetCallbackT saClmClusterNodeGetCallback;
	SaClmClusterTrackCallbackT saClmClusterTrackCallback;
} SaClmCallbacksT;

#ifdef __cplusplus
extern "C" {
#endif

SaErrorT
saClmInitialize (
	SaClmHandleT *clmHandle,
	const SaClmCallbacksT *clmCallbacks,
	const SaVersionT *version);


SaErrorT
saClmSelectionObjectGet (
		const SaClmHandleT *clmHandle,
		SaSelectionObjectT *selectionObject);

SaErrorT
saClmDispatch (
	const SaClmHandleT *clmHandle,
	SaDispatchFlagsT dispatchFlags);

SaErrorT
saClmFinalize (
	SaClmHandleT *clmHandle);

SaErrorT
saClmClusterTrackStart (
	const SaClmHandleT *clmHandle,
	SaUint8T trackFlags,
	SaClmClusterNotificationT *notificationBuffer,
	SaUint32T numberOfItems);

SaErrorT
saClmClusterTrackStop (
	const SaClmHandleT *clmHandle);

SaErrorT
saClmClusterNodeGet (
	SaClmNodeIdT nodeId,
	SaTimeT timeout,
	SaClmClusterNodeT *clusterNode);

SaErrorT
saClmClusterNodeGetAsync (
	const SaClmHandleT *clmHandle,
	SaInvocationT invocation,
	SaClmNodeIdT nodeId,
	SaClmClusterNodeT *clusterNode);

#ifdef __cplusplus
}
#endif

#endif /* AIS_CLM_H_DEFINED */


