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
#include "saClm.h"

void printSaClmNodeAddressT (SaClmNodeAddressT *nodeAddress) {
	int i;

	printf ("family=%d - address=", nodeAddress->family);
	for (i = 0; i < nodeAddress->length; i++) {
		printf ("%c", nodeAddress->value[i]);
	}
}

void printSaNameT (SaNameT *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

void printSaClmClusterNodeT (char *description, SaClmClusterNodeT *clusterNode) {
	printf ("Node Information for %s\n", description);

	printf ("\tnode id is %x\n", (int)clusterNode->nodeId);

	printf ("\tnode address is ");
	printSaClmNodeAddressT (&clusterNode->nodeAddress);
	printf ("\n");

	printf ("\tNode name is ");
	printSaNameT (&clusterNode->nodeName);
	printf ("\n");

	printf ("\tMember is %d\n", clusterNode->member);

	printf ("\tTimestamp is %llx nanoseconds\n", (unsigned long long)clusterNode->bootTimestamp);
}

void NodeGetCallback (
	SaInvocationT invocation,
	const SaClmClusterNodeT *clusterNode,
	SaAisErrorT error) 
{
	char buf[128];

	if (error != SA_AIS_OK) {
		printf ("Node for invocation %llu not found (%d)\n",
			(unsigned long long)invocation, error);
	} else {
		sprintf (buf, "NODEGETCALLBACK %llu\n", (unsigned long long)invocation);
		printSaClmClusterNodeT (buf, (SaClmClusterNodeT *)clusterNode);
	}
}

void TrackCallback (
	const SaClmClusterNotificationBufferT *notificationBuffer,
	SaUint32T numberOfMembers,
	SaAisErrorT error)
{
	int i;

printf ("Track callback\n");
	printf ("Calling track callback %p\n", notificationBuffer);
	for (i = 0; i < numberOfMembers; i++) {
		switch (notificationBuffer->notification[i].clusterChange) {
		case SA_CLM_NODE_NO_CHANGE:
			printf ("NODE STATE NO CHANGE.\n");
			break;
		case SA_CLM_NODE_JOINED:
			printf ("NODE STATE JOINED.\n");
			break;
		case SA_CLM_NODE_LEFT:
			printf ("NODE STATE LEFT.\n");
			break;
		case SA_CLM_NODE_RECONFIGURED:
			printf ("NODE STATE RECONFIGURED.\n");
			break;
		}
		printSaClmClusterNodeT ("TRACKING",
			&notificationBuffer->notification[i].clusterNode);
	}
	printf ("Done calling trackCallback\n");
}

SaClmCallbacksT callbacks = {
	.saClmClusterNodeGetCallback	= NodeGetCallback,
	.saClmClusterTrackCallback		= TrackCallback
};

SaVersionT version = { 'B', 1, 1 };

void sigintr_handler (int signum) {
	exit (0);
}

int main (void) {
	SaClmHandleT handle;
	fd_set read_fds;
	SaSelectionObjectT select_fd;
	int result;
	SaClmClusterNotificationT clusterNotification[64];
	SaClmClusterNotificationBufferT clusterNotificationBuffer;
	SaClmClusterNodeT clusterNode;
	int i;

	clusterNotificationBuffer.notification = clusterNotification;
	clusterNotificationBuffer.numberOfItems = 64;

	signal (SIGINT, sigintr_handler);

	result = saClmInitialize (&handle, &callbacks, &version);
	if (result != SA_AIS_OK) {
		printf ("Could not initialize Cluster Membership API instance error %d\n", result);
		exit (1);
	}

	result = saClmClusterNodeGet (handle, SA_CLM_LOCAL_NODE_ID, SA_TIME_END, &clusterNode);

	printf ("Result of saClmClusterNodeGet %d\n", result);

	printSaClmClusterNodeT ("saClmClusterNodeGet SA_CLM_LOCAL_NODE_ID result %d", &clusterNode);

	result = saClmClusterNodeGetAsync (handle, 55, SA_CLM_LOCAL_NODE_ID);
	printf ("result is %d\n", result);

	result = saClmClusterNodeGetAsync (handle, 60, 0x6201a8c0);
	printf ("result is %d\n", result);

	result = saClmClusterNodeGetAsync (handle, 61, 0x6a01a8f0);
	printf ("result is %d\n", result);

	result = saClmClusterNodeGetAsync (handle, 59, SA_CLM_LOCAL_NODE_ID);
	printf ("result is %d\n", result);

	result = saClmClusterNodeGetAsync (handle, 57, SA_CLM_LOCAL_NODE_ID);
	printf ("result is %d\n", result);

	result = saClmClusterTrack (handle, SA_TRACK_CURRENT | SA_TRACK_CHANGES,
		&clusterNotificationBuffer);
	printf ("track result is %d\n", result);
	for (i = 0; i < clusterNotificationBuffer.numberOfItems; i++) {
		printSaClmClusterNodeT ("Results from SA_TRACK_CURRENT:",
			&clusterNotificationBuffer.notification[i].clusterNode);
	}

	saClmSelectionObjectGet (handle, &select_fd);

printf ("select fd is %llu\n", (unsigned long long)select_fd);
	FD_ZERO (&read_fds);
printf ("press the enter key to exit with track stop and finalize.\n");
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
printf ("done with select\n");
		saClmDispatch (handle, SA_DISPATCH_ALL);
	} while (result);

	result = saClmClusterTrackStop (handle);
	printf ("TrackStop result is %d (should be 1)\n", result);

	result = saClmFinalize (handle);
	printf ("Finalize  result is %d (should be 1)\n", result);
	return (0);
}
