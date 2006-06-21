/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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
#ifndef IPC_MSG_H_DEFINED
#define IPC_MSG_H_DEFINED

#include "saAis.h"
#include "saMsg.h"
#include "ipc_gen.h"

enum req_lib_msg_queue_types {
	MESSAGE_REQ_MSG_QUEUEOPEN = 0,
	MESSAGE_REQ_MSG_QUEUEOPENASYNC = 1,
	MESSAGE_REQ_MSG_QUEUECLOSE = 2,
	MESSAGE_REQ_MSG_QUEUESTATUSGET = 3,
	MESSAGE_REQ_MSG_QUEUEUNLINK = 4,
	MESSAGE_REQ_MSG_QUEUEGROUPCREATE = 5,
	MESSAGE_REQ_MSG_QUEUEGROUPINSERT = 6,
	MESSAGE_REQ_MSG_QUEUEGROUPREMOVE = 7,
	MESSAGE_REQ_MSG_QUEUEGROUPDELETE = 8,
	MESSAGE_REQ_MSG_QUEUEGROUPTRACK = 9,
	MESSAGE_REQ_MSG_QUEUEGROUPTRACKSTOP = 10,
	MESSAGE_REQ_MSG_MESSAGESEND = 11,
	MESSAGE_REQ_MSG_MESSAGEGET = 12,
	MESSAGE_REQ_MSG_MESSAGECANCEL = 13,
	MESSAGE_REQ_MSG_MESSAGESENDRECEIVE = 14,
	MESSAGE_REQ_MSG_MESSAGEREPLY = 15
};

enum res_lib_msg_queue_types {
	MESSAGE_RES_MSG_QUEUEOPEN = 0,
	MESSAGE_RES_MSG_QUEUEOPENASYNC = 2,
	MESSAGE_RES_MSG_QUEUECLOSE = 3,
	MESSAGE_RES_MSG_QUEUESTATUSGET = 4,
	MESSAGE_RES_MSG_QUEUEUNLINK = 5,
	MESSAGE_RES_MSG_QUEUEGROUPCREATE = 6,
	MESSAGE_RES_MSG_QUEUEGROUPINSERT = 7,
	MESSAGE_RES_MSG_QUEUEGROUPREMOVE = 8,
	MESSAGE_RES_MSG_QUEUEGROUPDELETE = 9,
	MESSAGE_RES_MSG_QUEUEGROUPTRACK = 10,
	MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP = 11,
	MESSAGE_RES_MSG_MESSAGESEND = 12,
	MESSAGE_RES_MSG_MESSAGESENDASYNC = 13,
	MESSAGE_RES_MSG_MESSAGEGET = 14,
	MESSAGE_RES_MSG_MESSAGECANCEL = 15,
	MESSAGE_RES_MSG_MESSAGESENDRECEIVE = 16,
	MESSAGE_RES_MSG_MESSAGEREPLY = 17,
	MESSAGE_RES_MSG_MESSAGEREPLYASYNC = 18
};

struct req_lib_msg_queueopen {
	mar_req_header_t header;
	SaInvocationT invocation;
	SaNameT queueName;
	SaMsgQueueCreationAttributesT creationAttributes;
	int creationAttributesSet;
	SaMsgQueueOpenFlagsT openFlags;
	SaMsgQueueHandleT queueHandle;
	SaTimeT timeout;
	int async_call;
};

struct res_lib_msg_queueopen {
	mar_res_header_t header;
	SaMsgQueueHandleT queueHandle;
	mar_message_source_t source;
};

struct res_lib_msg_queueopenasync {
	mar_res_header_t header;
	SaInvocationT invocation;
	SaMsgQueueHandleT queueHandle;
	mar_message_source_t source;
};

struct req_lib_msg_queueclose {
	mar_req_header_t header;
	SaNameT queueName;
	SaMsgQueueHandleT queueHandle;
};

struct res_lib_msg_queueclose {
	mar_res_header_t header;
};

struct req_lib_msg_queuestatusget {
	mar_req_header_t header;
	SaNameT queueName;
};

struct res_lib_msg_queuestatusget {
	mar_res_header_t header;
	SaMsgQueueStatusT queueStatus;
};

struct req_lib_msg_queueunlink {
	mar_req_header_t header;
	SaNameT queueName;
};

struct res_lib_msg_queueunlink {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupcreate {
	mar_req_header_t header;
	SaNameT queueGroupName;
	SaMsgQueueGroupPolicyT queueGroupPolicy;
};

struct res_lib_msg_queuegroupcreate {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupinsert {
	mar_req_header_t header;
	SaNameT queueGroupName;
	SaNameT queueName;
};

struct res_lib_msg_queuegroupinsert {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupremove {
	mar_req_header_t header;
	SaNameT queueGroupName;
	SaNameT queueName;
};

struct res_lib_msg_queuegroupremove {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupdelete {
	mar_req_header_t header;
	SaNameT queueGroupName;
};

struct res_lib_msg_queuegroupdelete {
	mar_res_header_t header;
};
	
struct req_lib_msg_queuegrouptrack {
	mar_req_header_t header;
	SaNameT queueGroupName;
	SaUint8T trackFlags;
};

struct res_lib_msg_queuegrouptrack {
	mar_res_header_t header;
};

struct req_lib_msg_queuegrouptrackstop {
	mar_req_header_t header;
	SaNameT queueGroupName;
};

struct res_lib_msg_queuegrouptrackstop {
	mar_res_header_t header;
};

struct req_lib_msg_messagesend {
	mar_req_header_t header;
	SaInvocationT invocation;
	SaNameT destination;
	SaMsgMessageT message;
	SaTimeT timeout;
	SaMsgAckFlagsT ackFlags;
	int async_call;
};

struct res_lib_msg_messagesend {
	mar_res_header_t header;
};

struct res_lib_msg_messagesendasync {
	mar_res_header_t header;
};

struct req_lib_msg_messageget {
	mar_req_header_t header;
	SaNameT queueName;
	SaTimeT timeout;
};

struct res_lib_msg_messageget {
	mar_res_header_t header;
	SaTimeT sendTime;
	SaMsgSenderIdT senderId;
	SaMsgMessageT message;
};

struct req_lib_msg_messagecancel {
	mar_req_header_t header;
	SaNameT queueName;
};

struct res_lib_msg_messagecancel {
	mar_res_header_t header;
};

struct req_lib_msg_messagesendreceive {
	mar_req_header_t header;
	SaNameT destination;
	SaTimeT timeout;
	SaNameT queueName;
	SaMsgMessageT sendMessage;
};

struct res_lib_msg_messagesendreceive {
	mar_res_header_t header;
	SaTimeT replySendTime;
	SaMsgMessageT receiveMessage;
};

struct req_lib_msg_messagereply {
	mar_req_header_t header;
	SaInvocationT invocation;
	SaNameT queueName;
	SaNameT senderId;
	SaMsgMessageT replyMessage;
	SaTimeT timeout;
	SaMsgAckFlagsT ackFlags;
	int async_call;
};

struct res_lib_msg_messagereply {
	mar_res_header_t header;
};

struct res_lib_msg_messagereplyasync {
	mar_res_header_t header;
};

#endif /* IPC_MSG_H_DEFINED */
