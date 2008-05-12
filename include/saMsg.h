/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

#ifndef SAMSG_H_DEFINED
#define SAMSG_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

typedef SaUint64T SaMsgHandleT;

typedef SaUint64T SaMsgQueueHandleT;

typedef SaUint64T SaMsgSenderIdT;

#define SA_MSG_MESSAGE_DELIVERED_ACK 0x1

typedef SaUint32T SaMsgAckFlagsT;

#define SA_MSG_QUEUE_PERSISTENT 0x1

typedef SaUint32T SaMsgQueueCreationFlagsT;

#define SA_MSG_MESSAGE_HIGHEST_PRIORITY 0
#define SA_MSG_MESSAGE_LOWEST_PRIORITY 3

typedef struct {
	SaMsgQueueCreationFlagsT creationFlags;
	SaSizeT size[SA_MSG_MESSAGE_LOWEST_PRIORITY + 1];
	SaTimeT retentionTime;
} SaMsgQueueCreationAttributesT;

typedef enum {
	SA_MSG_QUEUE_GROUP_ROUND_ROBIN = 1,
	SA_MSG_QUEUE_GROUP_LOCAL_ROUND_ROBIN = 2,
	SA_MSG_QUEUE_GROUP_LOCAL_BEST_QUEUE = 3,
	SA_MSG_QUEUE_GROUP_BROADCAST = 4
} SaMsgQueueGroupPolicyT;

#define SA_MSG_QUEUE_CREATE 0x1
#define SA_MSG_QUEUE_RECEIVE_CALLBACK 0x2
#define SA_MSG_QUEUE_EMPTY 0x4

typedef SaUint32T SaMsgQueueOpenFlagsT;

typedef struct {
	SaSizeT queueSize;
	SaSizeT queueUsed;
	SaUint32T numberOfMessages;
} SaMsgQueueUsageT;

typedef struct {
	SaMsgQueueCreationFlagsT creationFlags;
	SaTimeT retentionTime;
	SaTimeT closeTime;
	SaMsgQueueUsageT saMsgQueueUsage[SA_MSG_MESSAGE_LOWEST_PRIORITY + 1];
} SaMsgQueueStatusT;

typedef enum {
	SA_MSG_QUEUE_GROUP_NO_CHANGE = 1,
	SA_MSG_QUEUE_GROUP_ADDED = 2,
	SA_MSG_QUEUE_GROUP_REMOVED = 3,
	SA_MSG_QUEUE_GROUP_STATE_CHANGED = 4
} SaMsgQueueGroupChangesT;

typedef struct {
	SaNameT queueName;
} SaMsgQueueGroupMemberT;

typedef struct {
	SaMsgQueueGroupMemberT member;
	SaMsgQueueGroupChangesT change;
} SaMsgQueueGroupNotificationT;

typedef struct {
	SaUint32T numberOfItems;
	SaMsgQueueGroupNotificationT *notification;
	SaMsgQueueGroupPolicyT queueGroupPolicy;
} SaMsgQueueGroupNotificationBufferT;

typedef struct {
	SaUint32T type;
	SaUint32T version;
	SaSizeT size;
	SaNameT *senderName;
	void *data;
	SaUint8T priority;
} SaMsgMessageT;

typedef void (*SaMsgQueueOpenCallbackT) (
	SaInvocationT invocation,
	SaMsgQueueHandleT queueHandle,
	SaAisErrorT error);

typedef void (*SaMsgQueueGroupTrackCallbackT) (
	const SaNameT *queueGroupName,
	const SaMsgQueueGroupNotificationBufferT *notificationBuffer,
	SaUint32T numberOfMembers,
	SaAisErrorT error);

typedef void (*SaMsgMessageDeliveredCallbackT) (
	SaInvocationT invocation,
	SaAisErrorT error);
	
typedef void (*SaMsgMessageReceivedCallbackT) (
	SaMsgQueueHandleT queueHandle);

typedef struct {
	SaMsgQueueOpenCallbackT saMsgQueueOpenCallback;
	SaMsgQueueGroupTrackCallbackT saMsgQueueGroupTrackCallback;
	SaMsgMessageDeliveredCallbackT saMsgMessageDeliveredCallback;
	SaMsgMessageReceivedCallbackT saMsgMessageReceivedCallback;
} SaMsgCallbacksT;

SaAisErrorT
saMsgInitialize (
	SaMsgHandleT *msgHandle,
	const SaMsgCallbacksT *msgCallbacks,
	SaVersionT *version);

SaAisErrorT saMsgSelectionObjectGet (
	SaMsgHandleT msgHandle,
	SaSelectionObjectT *selectionObject);

SaAisErrorT
saMsgDispatch (
	SaMsgHandleT msgHandle,
	SaDispatchFlagsT dispatchFlags);

SaAisErrorT
saMsgFinalize (
	SaMsgHandleT msgHandle);

SaAisErrorT
saMsgQueueOpen (
	SaMsgHandleT msgHandle,
	const SaNameT *queueName,
	const SaMsgQueueCreationAttributesT *creationAttributes,
	SaMsgQueueOpenFlagsT openFlags,
	SaTimeT timeout,
	SaMsgQueueHandleT *queueHandle);

SaAisErrorT
saMsgQueueOpenAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaNameT *queueName,
	const SaMsgQueueCreationAttributesT *creationAttributes,
	SaMsgQueueOpenFlagsT openFlags);

SaAisErrorT
saMsgQueueClose (
	SaMsgQueueHandleT msgHandle);

SaAisErrorT
saMsgQueueStatusGet (
	SaMsgQueueHandleT msgHandle,
	const SaNameT *queueName,
	SaMsgQueueStatusT *queueStatus);

SaAisErrorT
saMsgQueueUnlink (
	SaMsgQueueHandleT msgHandle,
	const SaNameT *queueName);

SaAisErrorT
saMsgQueueGroupCreate (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	SaMsgQueueGroupPolicyT queueGroupPolicy);

SaAisErrorT
saMsgQueueGroupInsert (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	const SaNameT *queueName);

SaAisErrorT
saMsgQueueGroupRemove (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	const SaNameT *queueName);

SaAisErrorT
saMsgQueueGroupDelete (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName);

SaAisErrorT
saMsgQueueGroupTrack (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	SaUint8T trackFlags,
	SaMsgQueueGroupNotificationBufferT *notificationBuffer);

SaAisErrorT
saMsgQueueGroupTrackStop (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName);

SaAisErrorT
saMsgMessageSend (
	SaMsgHandleT msgHandle,
	const SaNameT *destination,
	const SaMsgMessageT *message,
	SaTimeT timeout);

SaAisErrorT
saMsgMessageSendAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaNameT *destination,
	const SaMsgMessageT *message,
	SaMsgAckFlagsT ackFlags);

SaAisErrorT
saMsgMessageGet (
	SaMsgQueueHandleT queueHandle,
	SaMsgMessageT *message,
	SaTimeT *sendTIme,
	SaMsgSenderIdT *senderId,
	SaTimeT timeout);

SaAisErrorT
saMsgMessageCancel (
	SaMsgQueueHandleT queueHandle);

SaAisErrorT
saMsgMessageSendReceive (
	SaMsgHandleT msgHandle,
	const SaNameT *destination,
	const SaMsgMessageT *sendMessage,
	SaMsgMessageT *receiveMessage,
	SaTimeT *replySendTime,
	SaTimeT timeout);

SaAisErrorT
saMsgMessageReply (
	SaMsgHandleT msgHandle,
	const SaMsgMessageT *replyMessage,
	const SaMsgSenderIdT *senderId,
	SaTimeT timeout);

SaAisErrorT saMsgMessageReplyAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaMsgMessageT *replyMessage,
	const SaMsgSenderIdT *senderId,
	SaMsgAckFlagsT ackFlags);

#endif /* SAMSG_H_DEFINED */

