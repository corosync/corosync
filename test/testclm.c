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

#include "ais_types.h"
#include "ais_clm.h"

void printSaClmNodeAddressT (SaClmNodeAddressT *nodeAddress) {
	int i;

	for (i = 0; i < nodeAddress->length; i++) {
		printf ("%d.", nodeAddress->value[i]);
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

	printf ("\tCluster name is ");
	printSaNameT (&clusterNode->clusterName);
	printf ("\n");

	printf ("\tMember is %d\n", clusterNode->member);

	printf ("\tTimestamp is %llx nanoseconds\n", clusterNode->bootTimestamp);
}

void NodeGetCallback (
	SaInvocationT invocation,
	SaClmClusterNodeT *clusterNode,
	SaErrorT error) 
{
	char buf[128];

//	if (invocation == 0x60) {
	sprintf (buf, "NodeGetCallback different machine invocation %x", invocation);
//	} else {
	sprintf (buf, "NodeGetCallback local machine %x", invocation);
//	}


	printSaClmClusterNodeT (buf, clusterNode);
}

void TrackCallback (
	SaClmClusterNotificationT *notificationBuffer,
	SaUint32T numberOfItems,
	SaUint32T numberOfMembers,
	SaUint64T viewNumber,
	SaErrorT error)
{
	int i;

	printf ("Calling track callback %p\n", notificationBuffer);
	for (i = 0; i < numberOfItems; i++) {
	switch (notificationBuffer[i].clusterChanges) {
	case SA_CLM_NODE_NO_CHANGE:
		printf ("NODE STATE NO CHANGE.\n");
		break;
	case SA_CLM_NODE_JOINED:
		printf ("NODE STATE JOINED.\n");
		break;
	case SA_CLM_NODE_LEFT:
		printf ("NODE STATE LEFT.\n");
		break;
	}
		printSaClmClusterNodeT ("TRACKING", &notificationBuffer[i].clusterNode);
	}
	printf ("Done calling trackCallback\n");
}

SaClmCallbacksT callbacks = {
	NodeGetCallback,
	TrackCallback
};

SaVersionT version = { 'A', 1, 1 };

void sigintr_handler (int signum) {
	exit (0);
}

int main (void) {
	SaClmHandleT handle;
	fd_set read_fds;
	int select_fd;
	int result;
	SaClmClusterNotificationT clusterNotificationBuffer[64];
	SaClmClusterNodeT clusterNode;

	signal (SIGINT, sigintr_handler);

	result = saClmInitialize (&handle, &callbacks, &version);
	if (result != SA_OK) {
		printf ("Could not initialize Cluster Membership API instance error %d\n", result);
		exit (1);
	}

	result = saClmClusterNodeGet (SA_CLM_LOCAL_NODE_ID, 0, &clusterNode);

	printf ("Result of saClmClusterNodeGet %d\n", result);

	printSaClmClusterNodeT ("saClmClusterNodeGet SA_CLM_LOCAL_NODE_ID result %d", &clusterNode);

	result = saClmClusterNodeGetAsync (&handle, 0x55, SA_CLM_LOCAL_NODE_ID, &clusterNode);
	printf ("result is %d\n", result);

	result = saClmClusterNodeGetAsync (&handle, 0x60, 0x6201a8c0, &clusterNode);
	printf ("result is %d\n", result);

	result = saClmClusterNodeGetAsync (&handle, 0x59, SA_CLM_LOCAL_NODE_ID, &clusterNode);
	printf ("result is %d\n", result);

	result = saClmClusterNodeGetAsync (&handle, 0x57, SA_CLM_LOCAL_NODE_ID, &clusterNode);
	printf ("result is %d\n", result);

	saClmClusterTrackStart (&handle, SA_TRACK_CURRENT | SA_TRACK_CHANGES_ONLY, clusterNotificationBuffer, 64);

	saClmSelectionObjectGet (&handle, &select_fd);

printf ("select fd is %d\n", select_fd);
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
		saClmDispatch (&handle, SA_DISPATCH_ALL);
	} while (result);

	result = saClmClusterTrackStop (&handle);
	printf ("TrackStop result is %d (should be 1)\n", result);

	result = saClmFinalize (&handle);
	printf ("Finalize  result is %d (should be 1)\n", result);
	return (0);
}
