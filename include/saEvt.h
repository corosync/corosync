/*
 * Copyright (c) 2004-2005 Mark Haverkamp
 * Copyright (c) 2004-2005 Open Source Development Lab
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
 * - Neither the name of the Open Source Developement Lab nor the names of its
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

#include "saAis.h"

#ifndef AIS_EVT_H_DEFINED
#define AIS_EVT_H_DEFINED

typedef SaUint64T SaEvtHandleT;
typedef SaUint64T SaEvtEventHandleT;
typedef SaUint64T SaEvtChannelHandleT;
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
	SaAisErrorT error
);

typedef struct{
    SaEvtChannelOpenCallbackT saEvtChannelOpenCallback;
    SaEvtEventDeliverCallbackT saEvtEventDeliverCallback; 
} SaEvtCallbacksT;

#define SA_EVT_CHANNEL_PUBLISHER  0X1
#define SA_EVT_CHANNEL_SUBSCRIBER 0X2
#define SA_EVT_CHANNEL_CREATE     0X4
typedef SaUint8T SaEvtChannelOpenFlagsT;

typedef struct {
	SaSizeT	allocatedSize;
    SaSizeT patternSize;
    SaUint8T *pattern;
} SaEvtEventPatternT;


#define SA_EVT_HIGHEST_PRIORITY 0
#define SA_EVT_LOWEST_PRIORITY  3

/*
 * Event ID values from 0 to 1000 are have special meanings
 * and aren't used for regular events.
 */

/*
 * Event ID for an allocated but not yet published event.
 */
#define SA_EVT_EVENTID_NONE	0

/*
 * Event ID for a "lost event".
 */
#define SA_EVT_EVENTID_LOST	1

/*
 * Pattern to indicate a "lost event" message.
 */
#define SA_EVT_LOST_EVENT "SA_EVT_LOST_EVENT_PATTERN"

/*
 * Size of the biggest data attachment to an event.
 */
#define SA_EVT_DATA_MAX_LEN (64 * 1024)

typedef struct {
	SaSizeT allocatedNumber;
    SaSizeT patternsNumber;
    SaEvtEventPatternT *patterns;
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
    SaSizeT filtersNumber;
    SaEvtEventFilterT *filters;
} SaEvtEventFilterArrayT;


#ifdef __cplusplus
extern "C" {
#endif

SaAisErrorT
saEvtInitialize(
	SaEvtHandleT *evtHandle, 
	const SaEvtCallbacksT *callbacks,
        SaVersionT *version);

SaAisErrorT
saEvtSelectionObjectGet(
	SaEvtHandleT evtHandle,
        SaSelectionObjectT *selectionObject);

SaAisErrorT
saEvtDispatch(
	SaEvtHandleT evtHandle, 
	SaDispatchFlagsT dispatchFlags);

SaAisErrorT
saEvtFinalize(
	SaEvtHandleT evtHandle);

SaAisErrorT
saEvtChannelOpen(
	SaEvtHandleT evtHandle, 
	const SaNameT *channelName,
	SaEvtChannelOpenFlagsT channelOpenFlags,
        SaTimeT timeout,
        SaEvtChannelHandleT *channelHandle);

SaAisErrorT
saEvtChannelOpenAsync(
	SaEvtHandleT evtHandle,
	SaInvocationT invocation,
	const SaNameT *channelName,
	SaEvtChannelOpenFlagsT channelOpenFlags);

SaAisErrorT
saEvtChannelClose(
	SaEvtChannelHandleT channelHandle);

SaAisErrorT
saEvtChannelUnlink(
	SaEvtHandleT evtHandle, 
	const SaNameT *channelName);

SaAisErrorT
saEvtEventAllocate(
	SaEvtChannelHandleT channelHandle,
	SaEvtEventHandleT *eventHandle);

SaAisErrorT
saEvtEventFree(
	SaEvtEventHandleT eventHandle);

SaAisErrorT
saEvtEventAttributesSet(
	SaEvtEventHandleT eventHandle,
	const SaEvtEventPatternArrayT *patternArray,
	SaEvtEventPriorityT priority,
	SaTimeT retentionTime,
	const SaNameT *publisherName);

SaAisErrorT
saEvtEventAttributesGet(
	SaEvtEventHandleT eventHandle,
	SaEvtEventPatternArrayT *patternArray,
	SaEvtEventPriorityT *priority,
	SaTimeT *retentionTime,
	SaNameT *publisherName,
	SaTimeT *publishTime,
	SaEvtEventIdT *eventId);

SaAisErrorT
saEvtEventDataGet(
	SaEvtEventHandleT eventHandle,
	void *eventData,
	SaSizeT *eventDataSize);

SaAisErrorT
saEvtEventPublish(
	SaEvtEventHandleT eventHandle,
	const void *eventData,
	SaSizeT eventDataSize,
	SaEvtEventIdT *eventId);

SaAisErrorT
saEvtEventSubscribe(
	SaEvtChannelHandleT channelHandle,
	const SaEvtEventFilterArrayT *filters,
	SaEvtSubscriptionIdT subscriptionId);

SaAisErrorT
saEvtEventUnsubscribe(
	SaEvtChannelHandleT channelHandle,
	SaEvtSubscriptionIdT subscriptionId);

SaAisErrorT
saEvtEventRetentionTimeClear(
	SaEvtChannelHandleT channelHandle,
	SaEvtEventIdT eventId);

#ifdef __cplusplus
}
#endif
#endif /* AIS_EVT_H_DEFINED */
/*
 *	vi: set autoindent tabstop=4 shiftwidth=4 :
 */
