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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "saAis.h"
#include "saMsg.h"

SaMsgQueueHandleT async_handle;

void QueueOpenCallback (
	SaInvocationT invocation,
	SaMsgQueueHandleT queueHandle,
	SaAisErrorT error)
{
	/* DEBUG */
	printf ("[DEBUG]: testmsg (QueueOpenCallback)\n");
	printf ("[DEBUG]: \t { queueHandle = %llx }\n",
		(unsigned long long) queueHandle);

	async_handle = queueHandle;
}

void QueueGroupTrackCallback (
	const SaNameT *queueGroupName,
	const SaMsgQueueGroupNotificationBufferT *notificationBuffer,
	SaUint32T numberOfMembers,
	SaAisErrorT error)
{
	/* DEBUG */
	printf ("[DEBUG]: testmsg (QueueGroupTrackCallback)\n");
}

void MessageDeliveredCallback (
	SaInvocationT invocation,
	SaAisErrorT error)
{
	/* DEBUG */
	printf ("[DEBUG]: testmsg (MessageDeliveredCallback)\n");
	printf ("[DEBUG]: \t { invocation = %llx }\n",
		(unsigned long long) invocation);
}

void MessageReceivedCallback (
	SaMsgQueueHandleT queueHandle)
{
	/* DEBUG */
	printf ("[DEBUG]: testmsg (MessageReceivedCallback)\n");
}

SaMsgCallbacksT callbacks = {
	.saMsgQueueOpenCallback		= QueueOpenCallback,
	.saMsgQueueGroupTrackCallback	= QueueGroupTrackCallback,
	.saMsgMessageDeliveredCallback	= MessageDeliveredCallback,
	.saMsgMessageReceivedCallback	= MessageReceivedCallback
};

SaVersionT version = { 'B', 1, 1 };

SaMsgQueueCreationAttributesT creation_attributes = {
	SA_MSG_QUEUE_PERSISTENT,
	{ 128000, 128000, 128000 },
	SA_TIME_END
};

void setSaNameT (SaNameT *name, char *str) {
	name->length = strlen (str);
	strcpy (name->value, str);
}

void setSaMsgMessageT (SaMsgMessageT *message, char *data) {
	message->type = 1;
	message->version = 2;
	message->size = strlen (data) + 1;
	message->senderName = NULL;
	message->data = strdup (data);
	message->priority = 0;
}

void sigintr_handler (int signum) {
	exit (0);
}

int main (void) {
	SaMsgHandleT handle;
	SaMsgMessageT message;
	SaMsgQueueHandleT queue_handle;
	SaSelectionObjectT select_fd;
	SaInvocationT invocation = 3;

	fd_set read_fds;
	int result;

	SaNameT async_name;
	SaNameT queue_name;
	SaNameT queue_group_name;
	SaTimeT time;
	SaMsgSenderIdT id;
	SaMsgMessageT msg_a;
	SaMsgMessageT msg_b;
	SaMsgMessageT msg_c;

	memset (&msg_a, 0, sizeof (SaMsgMessageT));
	memset (&msg_b, 0, sizeof (SaMsgMessageT));
	memset (&msg_c, 0, sizeof (SaMsgMessageT));

	signal (SIGINT, sigintr_handler);

	result = saMsgInitialize (&handle, &callbacks, &version);
	if (result != SA_AIS_OK) {
		printf ("Could not initialize Cluster Membership API instance error %d\n", result);
		exit (1);
	}

	saMsgSelectionObjectGet (handle, &select_fd);

	setSaNameT (&async_name, "async");
	setSaNameT (&queue_name, "queue");

	result = saMsgQueueOpen (handle,
		&queue_name,
		&creation_attributes,
		SA_MSG_QUEUE_CREATE,
		SA_TIME_END,
		&queue_handle);
	printf ("saMsgQueueOpen result is %d (should be 1)\n", result);
	printf ("saMsgQueueOpen { queue_handle = %llx }\n", queue_handle);

	result = saMsgQueueOpenAsync (handle,
				      invocation,
				      &async_name,
				      &creation_attributes,
				      SA_MSG_QUEUE_CREATE);
	printf ("saMsgQueueOpenAsync result is %d (should be 1)\n", result);
	printf ("saMsgQueueOpen { async_handle = %llx }\n", async_handle);

	setSaNameT (&queue_group_name, "queue_group");

	result = saMsgQueueGroupCreate (
		handle,
		&queue_group_name,
		SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
	printf ("saMsgQueueGroupCreate result is %d (should be 1)\n", result);

	result = saMsgQueueGroupCreate (
		handle,
		&queue_group_name,
		SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
	printf ("saMsgQueueGroupCreate result is %d (should be 14)\n", result);

	result = saMsgQueueGroupInsert (
		handle,
		&queue_group_name,
		&queue_name);
	printf ("saMsgQueueGroupInsert result is %d (should be 1)\n", result);

	saMsgDispatch (handle, SA_DISPATCH_ALL);

	/*
	FD_ZERO (&read_fds);
	do {
		FD_SET (select_fd, &read_fds);
		FD_SET (STDIN_FILENO, &read_fds);
		result = select (select_fd + 1, &read_fds, 0, 0, 0);
		if (result == -1) {
			perror ("select\n");
		}
		if (FD_ISSET (STDIN_FILENO, &read_fds)) {
			break;
		}
		saMsgDispatch (handle, SA_DISPATCH_ALL);
	} while (result);
	*/

	setSaMsgMessageT (&message, "test_msg_01");
	result = saMsgMessageSend (handle, &queue_name, &message, SA_TIME_ONE_SECOND);
	printf ("saMsgMessageSend [1] result is %d (should be 1)\n", result);

	setSaMsgMessageT (&message, "test_msg_02");
	result = saMsgMessageSend (handle, &queue_name, &message, SA_TIME_ONE_SECOND);
	printf ("saMsgMessageSend [2] result is %d (should be 1)\n", result);

	setSaMsgMessageT (&message, "test_msg_03");
	result = saMsgMessageSend (handle, &queue_name, &message, SA_TIME_ONE_SECOND);
	printf ("saMsgMessageSend [3] result is %d (should be 1)\n", result);

	setSaMsgMessageT (&message, "test_msg_04");
	result = saMsgMessageSendAsync (handle, invocation, &queue_name, &message,
		SA_MSG_MESSAGE_DELIVERED_ACK);
	printf ("saMsgMessageSendAsync [4] result is %d (should be 1)\n", result);

	setSaMsgMessageT (&message, "test_msg_05");
	result = saMsgMessageSendAsync (handle, invocation, &queue_name, &message,
		SA_MSG_MESSAGE_DELIVERED_ACK);
	printf ("saMsgMessageSendAsync [5] result is %d (should be 1)\n", result);

	saMsgDispatch (handle, SA_DISPATCH_ALL);

	result = saMsgMessageGet (queue_handle, &msg_a, &time, &id, SA_TIME_ONE_MINUTE);
	printf ("saMsgMessageGet [a] result is %d (should be 1)\n", result);

	result = saMsgMessageGet (queue_handle, &msg_b, &time, &id, SA_TIME_ONE_MINUTE);
	printf ("saMsgMessageGet [b] result is %d (should be 1)\n", result);

	result = saMsgMessageGet (queue_handle, &msg_c, &time, &id, SA_TIME_ONE_MINUTE);
	printf ("saMsgMessageGet [c] result is %d (should be 1)\n", result);

	printf ("saMsgMessageGet { (a) data = %s }\n", (char *)(msg_a.data));
	printf ("saMsgMessageGet { (b) data = %s }\n", (char *)(msg_b.data));
	printf ("saMsgMessageGet { (c) data = %s }\n", (char *)(msg_c.data));

	result = saMsgQueueGroupRemove (handle,	&queue_group_name, &queue_name);
	printf ("saMsgQueueGroupRemove result is %d (should be 1)\n", result);

	result = saMsgQueueGroupDelete (handle,	&queue_group_name);
	printf ("saMsgQueueGroupDelete result is %d (should be 1)\n", result);

	printf ("saMsgQueueClose { queue_handle = %llx }\n", queue_handle);
	result = saMsgQueueClose (queue_handle);
	printf ("saMsgQueueClose result is %d (should be 1)\n", result);

	printf ("saMsgQueueClose { async_handle = %llx }\n", async_handle);
	result = saMsgQueueClose (async_handle);
	printf ("saMsgQueueClose result is %d (should be 1)\n", result);

	result = saMsgFinalize (handle);
	printf ("Finalize result is %d (should be 1)\n", result);
	return (0);
}
