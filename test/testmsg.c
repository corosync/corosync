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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "saAis.h"
#include "saMsg.h"

void QueueOpenCallback (
	SaInvocationT invocation,
	SaMsgQueueHandleT queueHandle,
	SaAisErrorT error)
{
}

void QueueGroupTrackCallback (
	const SaNameT *queueGroupName,
	const SaMsgQueueGroupNotificationBufferT *notificationBuffer,
	SaUint32T numberOfMembers,
	SaAisErrorT error)
{
}

void MessageDeliveredCallback (
	SaInvocationT invocation,
	SaAisErrorT error)
{
}

void MessageReceivedCallback (
	SaMsgQueueHandleT queueHandle)
{
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
	{128000, 128000, 128000},
	SA_TIME_END
};

void setSaNameT (SaNameT *name, char *str) {
	name->length = strlen (str);
	memcpy (name->value, str, name->length);
}

void sigintr_handler (int signum) {
	exit (0);
}

int main (void) {
	SaMsgHandleT handle;
	SaMsgQueueHandleT queue_handle;
	fd_set read_fds;
	SaSelectionObjectT select_fd;
	int result;
	SaNameT queue_name;
	SaNameT queue_group_name;

	signal (SIGINT, sigintr_handler);

	result = saMsgInitialize (&handle, &callbacks, &version);
	if (result != SA_OK) {
		printf ("Could not initialize Cluster Membership API instance error %d\n", result);
		exit (1);
	}

	saMsgSelectionObjectGet (handle, &select_fd);

	setSaNameT (&queue_name, "queue");

	result = saMsgQueueOpen (handle,
		&queue_name,
		&creation_attributes,
		SA_MSG_QUEUE_CREATE,
		SA_TIME_END,
		&queue_handle);
	printf ("saMsgQueueOpen result is %d (should be 1)\n", result);

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

	result = saMsgQueueGroupRemove (
		handle,
		&queue_group_name,
		&queue_name);
	printf ("saMsgQueueGroupRemove result is %d (should be 1)\n", result);

	result = saMsgQueueGroupDelete (handle,
		&queue_group_name);
	printf ("saMsgQueueGroupDelete result is %d (should be 1)\n", result);

	result = saMsgQueueClose (queue_handle);
	printf ("saMsgQueueClose result is %d (should be 1)\n", result);

	result = saMsgFinalize (handle);
	printf ("Finalize result is %d (should be 1)\n", result);
	return (0);
}
