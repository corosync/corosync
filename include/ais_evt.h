/*
 * Copyright (c) 2004 Mark Haverkamp
 * Copyright (c) 2004 Open Source Development Lab
 *
 * All rights reserved.
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

#ifdef __cplusplus
extern "C" {
#endif

SaErrorT
saEvtInitialize(
	SaEvtHandleT *evtHandle, 
	const SaEvtCallbacksT *callbacks,
        SaVersionT *version);

SaErrorT
saEvtSelectionObjectGet(
	SaEvtHandleT evtHandle,
        SaSelectionObjectT *selectionObject);

SaErrorT
saEvtDispatch(
	SaEvtHandleT evtHandle, 
	SaDispatchFlagsT dispatchFlags);

SaErrorT
saEvtFinalize(
	SaEvtHandleT evtHandle);

SaErrorT
saEvtChannelOpen(
	SaEvtHandleT evtHandle, 
	const SaNameT *channelName,
	SaEvtChannelOpenFlagsT channelOpenFlags,
        SaTimeT timeout,
        SaEvtChannelHandleT *channelHandle);

SaErrorT
saEvtChannelOpenAsync(
	SaEvtHandleT evtHandle,
	SaInvocationT invocation,
	const SaNameT *channelName,
	SaEvtChannelOpenFlagsT channelOpenFlags);

SaErrorT
saEvtChannelClose(
	SaEvtChannelHandleT channelHandle);

SaErrorT
saEvtChannelUnlink(
	SaEvtHandleT evtHandle, 
	const SaNameT *channelName);

SaErrorT
saEvtEventAllocate(
	SaEvtChannelHandleT channelHandle,
	SaEvtEventHandleT *eventHandle);

SaErrorT
saEvtEventFree(
	SaEvtEventHandleT eventHandle);

SaErrorT
saEvtEventAttributesSet(
	SaEvtEventHandleT eventHandle,
	const SaEvtEventPatternArrayT *patternArray,
	SaEvtEventPriorityT priority,
	SaTimeT retentionTime,
	const SaNameT *publisherName);

SaErrorT
saEvtEventAttributesGet(
	SaEvtEventHandleT eventHandle,
	SaEvtEventPatternArrayT *patternArray,
	SaEvtEventPriorityT *priority,
	SaTimeT *retentionTime,
	SaNameT *publisherName,
	SaTimeT *publishTime,
	SaEvtEventIdT *eventId);

SaErrorT
saEvtEventDataGet(
	SaEvtEventHandleT eventHandle,
	void *eventData,
	SaSizeT *eventDataSize);

SaErrorT
saEvtEventPublish(
	SaEvtEventHandleT eventHandle,
	const void *eventData,
	SaSizeT eventDataSize,
	SaEvtEventIdT *eventId);

SaErrorT
saEvtEventSubscribe(
	SaEvtChannelHandleT channelHandle,
	const SaEvtEventFilterArrayT *filters,
	SaEvtSubscriptionIdT subscriptionId);

SaErrorT
saEvtEventUnsubscribe(
	SaEvtChannelHandleT channelHandle,
	SaEvtSubscriptionIdT subscriptionId);

SaErrorT
saEvtEventRetentionTimeClear(
	SaEvtChannelHandleT channelHandle,
	SaEvtEventIdT eventId);

#ifdef __cplusplus
}
#endif
