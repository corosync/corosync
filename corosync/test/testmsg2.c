/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Ryan O'Hara (rohara@redhat.com)
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

void QueueOpenCallback (
	SaInvocationT invocation,
	SaMsgQueueHandleT queueHandle,
	SaAisErrorT error)
{
	/* DEBUG */
	printf ("[TEST]: testmsg2 (QueueOpenCallback)\n");
	printf ("[TEST]: \t { invocation = %llx }\n",
		(unsigned long long)(invocation));
	printf ("[TEST]: \t { queueHandle = %llx }\n",
		(unsigned long long)(queueHandle));
	printf ("[TEST]: \t { error = %u }\n",
		(unsigned int)(error));
}

void QueueGroupTrackCallback (
	const SaNameT *queueGroupName,
	const SaMsgQueueGroupNotificationBufferT *notificationBuffer,
	SaUint32T numberOfMembers,
	SaAisErrorT error)
{
	int i = 0;

	/* DEBUG */
	printf ("[TEST]: testmsg2 (QueueGroupTrackCallback)\n");
	printf ("[TEST]: \t { queueGroupName = %s }\n",
		(char *)(queueGroupName->value));
	printf ("[TEST]: \t { numberOfMembers = %u }\n",
		(unsigned int)(numberOfMembers));
	printf ("[TEST]: \t { error = %u }\n",
		(unsigned int)(error));
	printf ("[TEST]: \t { notificationBuffer->numberOfItems = %u }\n",
		(unsigned int)(notificationBuffer->numberOfItems));

	for (i = 0; i < notificationBuffer->numberOfItems; i++) {
		printf ("[TEST]: \t { item #%d => %s (%u) }\n", i,
			(char *)(notificationBuffer->notification[i].member.queueName.value),
			(unsigned int)(notificationBuffer->notification[i].change));
	}
}

void MessageDeliveredCallback (
	SaInvocationT invocation,
	SaAisErrorT error)
{
	/* DEBUG */
	printf ("[TEST]: testmsg2 (MessageDeliveredCallback)\n");
	printf ("[TEST]: \t { invocation = %llx }\n",
		(unsigned long long)(invocation));
	printf ("[TEST]: \t { error = %u }\n",
		(unsigned int)(error));
}

void MessageReceivedCallback (
	SaMsgQueueHandleT queueHandle)
{
	/* DEBUG */
	printf ("[TEST]: testmsg2 (MessageReceivedCallback)\n");
	printf ("[TEST]: \t { queueHandle = %llx }\n",
		(unsigned long long)(queueHandle));
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

int main (void)
{
	int result;

	SaSelectionObjectT select_obj;

	SaMsgHandleT handle;

	SaMsgQueueHandleT queue_handle_a;
	SaMsgQueueHandleT queue_handle_b;
	SaMsgQueueHandleT queue_handle_c;

	SaMsgQueueHandleT queue_handle_x;
	SaMsgQueueHandleT queue_handle_y;
	SaMsgQueueHandleT queue_handle_z;

	SaNameT queue_name_a;
	SaNameT queue_name_b;
	SaNameT queue_name_c;

	SaNameT queue_name_x;
	SaNameT queue_name_y;
	SaNameT queue_name_z;

	SaNameT queue_group_one;
	SaNameT queue_group_two;

	setSaNameT(&queue_name_a, "QUEUE_A");
	setSaNameT(&queue_name_b, "QUEUE_B");
	setSaNameT(&queue_name_c, "QUEUE_C");

	setSaNameT(&queue_name_x, "QUEUE_X");
	setSaNameT(&queue_name_y, "QUEUE_Y");
	setSaNameT(&queue_name_z, "QUEUE_Z");

	setSaNameT(&queue_group_one, "GROUP_ONE");
	setSaNameT(&queue_group_two, "GROUP_TWO");

	result = saMsgInitialize (&handle, &callbacks, &version);

	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saMsgInitialize\n", result);
		exit (1);
	}

	saMsgSelectionObjectGet (handle, &select_obj);

	/*
	* Create message queues
	*/

	result = saMsgQueueOpen (handle, &queue_name_a, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_a);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_a.value));

	result = saMsgQueueOpen (handle, &queue_name_b, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_b);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_b.value));

	result = saMsgQueueOpen (handle, &queue_name_c, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_c);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_c.value));

	result = saMsgQueueOpen (handle, &queue_name_x, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_x);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_x.value));

	result = saMsgQueueOpen (handle, &queue_name_y, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_y);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_y.value));

	result = saMsgQueueOpen (handle, &queue_name_z, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_z);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_z.value));

	/*
	* Create queue groups
	*/

	result = saMsgQueueGroupCreate (handle, &queue_group_one,
					SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
	printf ("[DEBUG]: (%d) saMsgQueueGroupCreate { %s }\n",
		result, (char *)(queue_group_one.value));

	result = saMsgQueueGroupCreate (handle, &queue_group_two,
					SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
	printf ("[DEBUG]: (%d) saMsgQueueGroupCreate { %s }\n",
		result, (char *)(queue_group_two.value));

	/*
	* Track GROUP_ONE with SA_TRACK_CHANGES
	*/

	result = saMsgQueueGroupTrack (handle, &queue_group_one,
				       SA_TRACK_CHANGES, NULL);
	printf ("[DEBUG]: (%d) saMsgQueueGroupTrack { %s }\n",
		result, (char *)(queue_group_one.value));

	/*
	* Track GROUP_TWO with SA_TRACK_CHANGES_ONLY
	*/

	result = saMsgQueueGroupTrack (handle, &queue_group_two,
				       SA_TRACK_CHANGES_ONLY, NULL);
	printf ("[DEBUG]: (%d) saMsgQueueGroupTrack { %s }\n",
		result, (char *)(queue_group_two.value));

	/*
	* Add queues to GROUP_ONE
	*/

	result = saMsgQueueGroupInsert (handle, &queue_group_one, &queue_name_a);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(queue_group_one.value), (char *)(queue_name_a.value));

	result = saMsgQueueGroupInsert (handle, &queue_group_one, &queue_name_b);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(queue_group_one.value), (char *)(queue_name_b.value));

	result = saMsgQueueGroupInsert (handle, &queue_group_one, &queue_name_c);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(queue_group_one.value), (char *)(queue_name_c.value));

	/*
	* Add queues to GROUP_TWO
	*/

	result = saMsgQueueGroupInsert (handle, &queue_group_two, &queue_name_x);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(queue_group_two.value), (char *)(queue_name_x.value));

	result = saMsgQueueGroupInsert (handle, &queue_group_two, &queue_name_y);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(queue_group_two.value), (char *)(queue_name_y.value));

	result = saMsgQueueGroupInsert (handle, &queue_group_two, &queue_name_z);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(queue_group_two.value), (char *)(queue_name_z.value));

	/*
	* Track queue groups with SA_TRACK_CURRENT
	*/

	result = saMsgQueueGroupTrack (handle, &queue_group_one,
				       SA_TRACK_CURRENT, NULL);
	printf ("[DEBUG]: (%d) saMsgQueueGroupTrack { %s }\n",
		result, (char *)(queue_group_one.value));

	result = saMsgQueueGroupTrack (handle, &queue_group_two,
				       SA_TRACK_CURRENT, NULL);
	printf ("[DEBUG]: (%d) saMsgQueueGroupTrack { %s }\n",
		result, (char *)(queue_group_two.value));

	/*
	* Dispatch callbacks
	*/

	result = saMsgDispatch (handle, SA_DISPATCH_ALL);
	printf ("[DEBUG]: (%d) saMsgDispatch\n", result);

	/*
	* Remove queues from GROUP_ONE
	*/

	result = saMsgQueueGroupRemove (handle, &queue_group_one, &queue_name_a);
	printf ("[DEBUG]: (%d) saMsgQueueGroupRemove { group: %s - queue: %s }\n",
		result, (char *)(queue_group_one.value), (char *)(queue_name_a.value));

	result = saMsgQueueGroupRemove (handle, &queue_group_one, &queue_name_b);
	printf ("[DEBUG]: (%d) saMsgQueueGroupRemove { group: %s - queue: %s }\n",
		result, (char *)(queue_group_one.value), (char *)(queue_name_b.value));

	result = saMsgQueueGroupRemove (handle, &queue_group_one, &queue_name_c);
	printf ("[DEBUG]: (%d) saMsgQueueGroupRemove { group: %s - queue: %s }\n",
		result, (char *)(queue_group_one.value), (char *)(queue_name_c.value));

	/*
	* Remove queues from GROUP_TWO
	*/

	result = saMsgQueueGroupRemove (handle, &queue_group_two, &queue_name_x);
	printf ("[DEBUG]: (%d) saMsgQueueGroupRemove { group: %s - queue: %s }\n",
		result, (char *)(queue_group_two.value), (char *)(queue_name_x.value));

	result = saMsgQueueGroupRemove (handle, &queue_group_two, &queue_name_y);
	printf ("[DEBUG]: (%d) saMsgQueueGroupRemove { group: %s - queue: %s }\n",
		result, (char *)(queue_group_two.value), (char *)(queue_name_y.value));

	result = saMsgQueueGroupRemove (handle, &queue_group_two, &queue_name_z);
	printf ("[DEBUG]: (%d) saMsgQueueGroupRemove { group: %s - queue: %s }\n",
		result, (char *)(queue_group_two.value), (char *)(queue_name_z.value));

	/*
	* Dispatch callbacks
	*/

	result = saMsgDispatch (handle, SA_DISPATCH_ALL);
	printf ("[DEBUG]: (%d) saMsgDispatch\n", result);

	/*
	* Stop tracking GROUP_ONE
	*/

	result = saMsgQueueGroupTrackStop (handle, &queue_group_one);
	printf ("[DEBUG]: (%d) saMsgQueueGroupTrackStop { %s }\n",
		result, (char *)(queue_group_one.value));

	/*
	* Stop tracking GROUP_TWO
	*/

	result = saMsgQueueGroupTrackStop (handle, &queue_group_two);
	printf ("[DEBUG]: (%d) saMsgQueueGroupTrackStop { %s }\n",
		result, (char *)(queue_group_two.value));

	/*
	* Close message queues
	*/

	result = saMsgQueueClose (queue_handle_a);
	printf ("[DEBUG]: (%d) saMsgQueueClose { %llx }\n",
		result, ((unsigned long long) queue_handle_a));

	result = saMsgQueueClose (queue_handle_b);
	printf ("[DEBUG]: (%d) saMsgQueueClose { %llx }\n",
		result, ((unsigned long long) queue_handle_b));

	result = saMsgQueueClose (queue_handle_c);
	printf ("[DEBUG]: (%d) saMsgQueueClose { %llx }\n",
		result, ((unsigned long long) queue_handle_c));

	result = saMsgQueueClose (queue_handle_x);
	printf ("[DEBUG]: (%d) saMsgQueueClose { %llx }\n",
		result, ((unsigned long long) queue_handle_x));

	result = saMsgQueueClose (queue_handle_y);
	printf ("[DEBUG]: (%d) saMsgQueueClose { %llx }\n",
		result, ((unsigned long long) queue_handle_y));

	result = saMsgQueueClose (queue_handle_z);
	printf ("[DEBUG]: (%d) saMsgQueueClose { %llx }\n",
		result, ((unsigned long long) queue_handle_z));

	/*
	 * Delete queue groups
	 */

	result = saMsgQueueGroupDelete (handle, &queue_group_one);
	printf ("[DEBUG]: (%d) saMsgQueueGroupDelete { %s }\n",
		result, (char *)(queue_group_one.value));

	result = saMsgQueueGroupDelete (handle, &queue_group_two);
	printf ("[DEBUG]: (%d) saMsgQueueGroupDelete { %s }\n",
		result, (char *)(queue_group_two.value));

	result = saMsgFinalize (handle);

	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saMsgFinalize\n", result);
		exit (1);
	}

	return (0);
}
