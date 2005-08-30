/*
 * Copyright (c) 2005 Ericsson AB
 *
 * All rights reserved.
 *
 * Author: Rabbe Fogelholm (rabbe.fogelholm@ericsson.com)
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
 
/*
 * testclm2.c
 * 
 * Simple program to test cluster membership on an SA Forum platform.
 * The program expects one command-line argument which is "query"
 * or "callback". "Query" means that a single saClmClusterTrack call
 * is to be made. "Callback" means that callbacks are wanted when
 * there are changes in cluster membership. At least a 2-node cluster
 * is required to test this program mode.
 * 
 * Tested on platforms:
 *   Gentoo Linux 2005-08 (build)
 *   Fedora Core 4 (build and run)
 * 
 * Change history:
 *   2005-08-28 Rabbe Fogelholm:
 *     Initial version
 *   2005-08-30 Rabbe Fogelholm:
 *     Added call to saClmClusterTrackStop()
 *     Possible to test SA_TRACK_CHANGES_ONLY
 *     Improved diagnostics
 */ 


#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <saClm.h>


#define PGM_NAME "testclm2"

#define MODE_QUERY 1
#define MODE_CALLBACK 2
#define MODE_UNKNOWN -1


SaClmHandleT handle;

int mode = MODE_UNKNOWN;


void interruptAction();

void usage();

void clusterTrack(const SaClmClusterNotificationBufferT *, SaUint32T, SaAisErrorT);

int apiCall(char *, SaAisErrorT);

char *decodeStatus(int);

void printBoolean(SaBoolT);

void printName(SaNameT *);

void printAddress(SaClmNodeAddressT *);

void printCluster(const SaClmClusterNotificationBufferT *);

void printDate(SaTimeT);

char *decodeClusterChange(int);


int main(int argc, char *argv[])
{
	struct sigaction act;
	act.sa_handler = interruptAction;
	int status;
	if ((status = sigaction(SIGINT, &act, NULL)) != 0)
	{
		printf("sigaction returned: %d\n", status);
	}
	
	if (argc != 2 && argc != 3)
	{
		usage();
		return 1;
	}
	
	mode =
		strcmp(argv[1], "query") == 0     ? MODE_QUERY :
		strcmp(argv[1], "callback") == 0  ? MODE_CALLBACK :
		MODE_UNKNOWN;
	if (mode == MODE_UNKNOWN)
	{
		usage();
		return 1;
	}
	
	if (mode == MODE_CALLBACK && argc != 3)
	{
		usage();
		return 1;
	}
	int trackingMode =
		mode == MODE_QUERY                                     ? SA_TRACK_CURRENT :
		mode == MODE_CALLBACK && strcmp(argv[2], "full") == 0  ? SA_TRACK_CHANGES :
		mode == MODE_CALLBACK && strcmp(argv[2], "delta") == 0 ? SA_TRACK_CHANGES_ONLY :
		-1;
	if (trackingMode == -1)
	{
		usage();
		return 1;
	}

	SaClmCallbacksT callbacks;
	callbacks.saClmClusterNodeGetCallback = NULL;
	callbacks.saClmClusterTrackCallback = (SaClmClusterTrackCallbackT)clusterTrack;
	
	SaVersionT version;
	version.releaseCode = 'B';
	version.majorVersion = 1;
	version.minorVersion = 0;
	
	
	if (! apiCall("Initialize", saClmInitialize(&handle ,&callbacks, &version)))
		return 1;

	printf("AIS version supported: %c.%d.%d\n",
		version.releaseCode, version.majorVersion, version.minorVersion);
	
	if (mode == MODE_QUERY)
	{
		SaClmClusterNotificationBufferT buffer = {123456789, 123456789, NULL};
		apiCall("ClusterTrack", saClmClusterTrack(handle, trackingMode, &buffer));
		printCluster(&buffer);
		free(buffer.notification);
	}
	
	if (mode == MODE_CALLBACK)
	{
		printf("(type ctrl-C to finish)\n");
		apiCall("ClusterTrack", saClmClusterTrack(handle, trackingMode, NULL));
		while (1)
		{
			apiCall("Dispatch", saClmDispatch(handle, SA_DISPATCH_ONE));
			printf("sleep 1 sec\n");
			sleep(1);
		}
	}

	apiCall("Finalize", saClmFinalize(handle));
	return 0;
}


void interruptAction()
{
	fprintf(stderr, "SIGINT signal caught\n");
	if (mode == MODE_CALLBACK)
	{
		apiCall("ClusterTrackStop", handle);
	}
	apiCall("Finalize", saClmFinalize(handle));
	exit(0);
}


void usage()
{
		fprintf(stderr, "%s: usage is:\n", PGM_NAME);
		fprintf(stderr, "  membership query           Query for membership once\n");
		fprintf(stderr, "  membership callback full   Callback on membership change, full report\n");
		fprintf(stderr, "  membership callback delta  Callback on membership change, delta report\n");
}


void clusterTrack(
	const SaClmClusterNotificationBufferT *buffer,
	SaUint32T numberOfMembers,
	SaAisErrorT error)
{
	apiCall("clusterTrack callback", error);
	printf("number of members: %d\n\n", numberOfMembers);
	printCluster(buffer);
}


int apiCall(char *call, SaAisErrorT code)
{
	char *s = decodeStatus(code);
	printf("called: %s, status: %s", call, s);
	if (strcmp(s, "unknown error code") == 0)
	{
		printf(": %d\n\n", code);
	}
	else
	{
		printf("\n\n");
	}
	return code == SA_AIS_OK ? 1 : 0;
}


char *decodeStatus(int code)
{
 	return  
  	  code == SA_AIS_OK                      ? "successful"                                                  :
  	  code == SA_AIS_ERR_LIBRARY             ? "error in library, cannot be used anymore"                    :
  	  code == SA_AIS_ERR_VERSION             ? "version incompatibility"                                     :
	  code == SA_AIS_ERR_INIT                ? "callback function has not been supplied"                     :
	  code == SA_AIS_ERR_TIMEOUT             ? "timeout occurred, call may or may not have succeeded"        :
	  code == SA_AIS_ERR_TRY_AGAIN           ? "service cannot be provided now, try later"                   :
	  code == SA_AIS_ERR_INVALID_PARAM       ? "a parameter is not set correctly"                            :
	  code == SA_AIS_ERR_NO_MEMORY           ? "out of memory"                                               :
	  code == SA_AIS_ERR_BAD_HANDLE          ? "handle is invalid"                                           :
  	  code == SA_AIS_ERR_BUSY                ? "resource already in use"                                     :
  	  code == SA_AIS_ERR_ACCESS              ? "access denied"                                               :
  	  code == SA_AIS_ERR_NOT_EXIST           ? "entity does not exist"                                       :
  	  code == SA_AIS_ERR_NAME_TOO_LONG       ? "name too long"                                               :
  	  code == SA_AIS_ERR_EXIST               ? "entity already exists"                                       :
  	  code == SA_AIS_ERR_NO_SPACE            ? "buffer space is not sufficient"                              :
  	  code == SA_AIS_ERR_INTERRUPT           ? "request canceled by timeout or other interrupt"              :
  	  code == SA_AIS_ERR_NAME_NOT_FOUND      ? "name not found"                                              :
  	  code == SA_AIS_ERR_NOT_SUPPORTED       ? "requested function is not supported"                         :
  	  code == SA_AIS_ERR_BAD_OPERATION       ? "requested operation is not allowed"                          :
  	  code == SA_AIS_ERR_FAILED_OPERATION    ? "healthcheck unsuccessful, error callback done"               :
	  code == SA_AIS_ERR_NO_RESOURCES        ? "insufficient resources other than memory"                    :
  	  code == SA_AIS_ERR_MESSAGE_ERROR       ? "a communication error occurred"                              :
  	  code == SA_AIS_ERR_QUEUE_FULL          ? "destination queue is full"                                   :
  	  code == SA_AIS_ERR_QUEUE_NOT_AVAILABLE ? "destination queue not available"                             :
  	  code == SA_AIS_ERR_BAD_FLAGS           ? "flags are invalid"                                           :
  	  code == SA_AIS_ERR_TOO_BIG             ? "value larger than maximum permitted"                         :
  	  code == SA_AIS_ERR_NO_SECTIONS         ? "no sections matching spec in SectionIteratorInitialize call" :
  	                                           "unknown error code";
}


void printBoolean(SaBoolT b)
{
	printf("%s\n", b ? "true" : "false");
}


void printName(SaNameT *name)
{
	int i;
	for (i=0; i<name->length; i++) printf("%c", name->value[i]);
	printf("\n");
}


void printAddress(SaClmNodeAddressT *nodeAddress)
{
	if (nodeAddress->family == SA_CLM_AF_INET6)
	{
		printf("sorry, cannot decode IPv6 yet\n");
	}
	else if (nodeAddress->length == 4)
	{
		// we may get here due to defect 833, see
		// http://www.osdl.org/developer_bugzilla/show_bug.cgi?id=833 for details
		printf("%d.%d.%d.%d\n",
			nodeAddress->value[0],
			nodeAddress->value[1],
			nodeAddress->value[2],
			nodeAddress->value[3]);
	}
	else
	{		
		int k;
		for (k = 0; k < nodeAddress->length; k++)
		{
			printf("%c", nodeAddress->value[k]);
		}
		printf("\n");
	}
}


void printCluster(const SaClmClusterNotificationBufferT *buffer)
{
	printf("  view number: %llu\n", buffer->viewNumber);
	printf("  number of items: %u\n\n",  buffer->numberOfItems);
	int j; for (j=0; j<buffer->numberOfItems; j++)
	{
		printf("    node index within sequence: %d\n", j);
		printf("    cluster node: %u\n", buffer->notification[j].clusterNode.nodeId);
		printf("    address: "); printAddress(& buffer->notification[j].clusterNode.nodeAddress);
		printf("    name: "); printName(&(buffer->notification[j].clusterNode.nodeName));
		printf("    member: "); printBoolean(buffer->notification[j].clusterNode.member);
		printf("    booted: "); printDate(buffer->notification[j].clusterNode.bootTimestamp);
		printf("    initial view number: %llu\n", buffer->notification[j].clusterNode.initialViewNumber);
		printf("    cluster change: %s\n", decodeClusterChange(buffer->notification[j].clusterChange));
		printf("\n");
	}
}


void printDate(SaTimeT nanoseconds)
{
	time_t tt = nanoseconds/SA_TIME_ONE_SECOND;
	struct tm *decodedTime = localtime(&tt);
	
	printf("%d-%02d-%02d %02d:%02d:%02d\n",
		decodedTime->tm_year + 1900,
		decodedTime->tm_mon + 1, 
		decodedTime->tm_mday,
		decodedTime->tm_hour,
		decodedTime->tm_min,
		decodedTime->tm_sec);
}


char *decodeClusterChange(int code)
{
 	return  
  	  code == SA_CLM_NODE_NO_CHANGE     ? "node has not changed"           :
  	  code == SA_CLM_NODE_JOINED        ? "node has joined the cluster"    :
  	  code == SA_CLM_NODE_LEFT          ? "node has left the cluster"      :
	  code == SA_CLM_NODE_RECONFIGURED  ? "node has been reconfigured"     :
  	                                      "unknown type of node change";
}
