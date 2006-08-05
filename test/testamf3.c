/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
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
#include <sys/un.h>
#include <sched.h>

#include "ais_types.h"
#include "saAmf.h"

SaAmfHandleT handle;

SaAmfHealthcheckKeyT key0 = {
	.key = "key1",
	.keyLen = 4
};
SaNameT compNameGlobal;

void printSaNameT (SaNameT *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

void setSanameT (SaNameT *name, char *str) {
	name->length = strlen (str);
	memcpy (name->value, str, name->length);
}

static int health_flag = -1;
static unsigned int healthcheck_count = 0;
static unsigned int healthcheck_no = 0;

int stop = 0;
void HealthcheckCallback (SaInvocationT invocation,
	const SaNameT *compName,
	SaAmfHealthcheckKeyT *healthcheckKey)
{
	SaErrorT res;

	healthcheck_no++;
	printf ("Healthcheck %u for key '%s' for component ",
		healthcheck_no, healthcheckKey->key);

	printSaNameT ((SaNameT *)compName);
	printf ("\n");
	res = saAmfResponse (handle, invocation, SA_AIS_OK);
	if (res != SA_OK) {
		printf ("response res is %d\n", res);
	}
	if (healthcheck_no == 20) {
		res = saAmfHealthcheckStop (handle, &compNameGlobal, &key0);
		stop = 1;
	}
	printf ("done res = %d\n", res);
}

#ifdef COMPILE_OUT

void ComponentTerminateCallback (
	SaInvocationT invocation,
	const SaNameT *compName)
{
	printf ("ComponentTerminateCallback\n");
}

#endif
void CSISetCallback (
	SaInvocationT invocation,
	const SaNameT *compName,
	SaAmfHAStateT haState,
	SaAmfCSIDescriptorT *csiDescriptor)
{
	int res;
	switch (haState) {
	case SA_AMF_HA_ACTIVE:
		printf ("CSISetCallback:"); 
		printf ("for CSI '");
		printSaNameT ((SaNameT *)&csiDescriptor->csiName);
		printf ("' for component ");
		printSaNameT ((SaNameT *)compName);
		printf ("'");
 		printf (" requested to enter hastate SA_AMF_ACTIVE.\n");
		res = saAmfResponse (handle, invocation, SA_AIS_OK);
		break;

	case SA_AMF_HA_STANDBY:
		printf ("CSISetCallback:"); 
		printf ("for CSI '");
		printSaNameT ((SaNameT *)compName);
		printf ("' for component ");
		printSaNameT ((SaNameT *)compName);
		printf ("'");
		printf (" requested to enter hastate SA_AMF_STANDBY.\n");
		saAmfResponse (handle, invocation, SA_AIS_OK);
		break;
	}
}

#ifdef COMPILE_OUT
void CSIRemoveCallback (
	SaInvocationT invocation,
	const SaNameT *compName,
	const SaNameT *csiName,
	const SaAmfCSIFlagsT *csiFlags)
{
	printf ("CSIRemoveCallback for component '");
	printSaNameT ((SaNameT *)compName);
	printf ("' in CSI '");
	printSaNameT ((SaNameT *)csiName);
	printf ("'\n");
	saAmfResponse (invocation, SA_OK);
}

void ProtectionGroupTrackCallback (
	const SaNameT *csiName,
	SaAmfProtectionGroupNotificationT *notificationBuffer,
	SaUint32T numberOfItems,
	SaUint32T numberOfMembers,
	SaErrorT error)
{
	int i;

	printf ("ProtectionGroupTrackCallback items %d members %d\n", (int)numberOfItems, (int)numberOfMembers);
	printf ("buffer is %p\n", notificationBuffer);
	for (i = 0; i < numberOfItems; i++) {
		printf ("component name");
		printSaNameT (&notificationBuffer[i].member.compName);
		printf ("\n");
		printf ("\treadiness state is %d\n",  notificationBuffer[i].member.readinessState);
		printf ("\thastate %d\n",  notificationBuffer[i].member.haState);
		printf ("\tchange is %d\n",  notificationBuffer[i].change);

	}
}

void ExternalComponentRestartCallback (
	const SaInvocationT invocation,
	const SaNameT *externalCompName)
{
	printf ("ExternalComponentRestartCallback\n");
}

void ExternalComponentControlCallback (
	const SaInvocationT invocation,
	const SaNameT *externalCompName,
	SaAmfExternalComponentActionT controlAction)
{
	printf ("ExternalComponentControlCallback\n");
}

void PendingOperationConfirmCallback (
	const SaInvocationT invocation,
	const SaNameT *compName,
	SaAmfPendingOperationFlagsT pendingOperationFlags)
{
	printf ("PendingOperationConfirmCallback\n");
}

void PendingOperationExpiredCallback (
	const SaNameT *compName,
	SaAmfPendingOperationFlagsT pendingOperationFlags)
{
	printf ("PendingOperationExpiredCallback\n");
}
#endif

SaAmfCallbacksT amfCallbacks = {
	.saAmfHealthcheckCallback = HealthcheckCallback,
	.saAmfCSISetCallback = CSISetCallback,
};

SaAmfCallbacksT amfCallbacks;

SaVersionT version = { 'B', 1, 1 };

#if ! defined(TS_CLASS) && (defined(OPENAIS_BSD) || defined(OPENAIS_LINUX) || defined(OPENAIS_SOLARIS))
static struct sched_param sched_param = {
    sched_priority: 99
};
#endif

void sigintr_handler (int signum) {
	exit (0);
}

int main (int argc, char **argv) {
	int result;
	SaSelectionObjectT select_fd;
	fd_set read_fds;
	extern char *optarg;
	extern int optind;
	int c;

	memset (&compNameGlobal, 0, sizeof (SaNameT));
	signal (SIGINT, sigintr_handler);
#if ! defined(TS_CLASS) && (defined(OPENAIS_BSD) || defined(OPENAIS_LINUX) || defined(OPENAIS_SOLARIS))
	result = sched_setscheduler (0, SCHED_RR, &sched_param);
	if (result == -1) {
		printf ("couldn't set sched priority\n");
 	}
#endif

	for (;;){
		c = getopt(argc,argv,"h:n:");
		if (c==-1) {
			break;
		}
		switch (c) {
		case 0 :
			break;
		case 'h':
			health_flag = 0;
			sscanf (optarg,"%ud" ,&healthcheck_count);
			break;
		case 'n':
	  		setSanameT (&compNameGlobal, optarg);
			break;
                default :
			break;
                }
	}

	result = saAmfInitialize (&handle, &amfCallbacks, &version);
	if (result != SA_OK) {
		printf ("initialize result is %d\n", result);
		exit (1);
	}

	FD_ZERO (&read_fds);
	saAmfSelectionObjectGet (handle, &select_fd);
	FD_SET (select_fd, &read_fds);
	if (compNameGlobal.length <= 0) {
		setSanameT (&compNameGlobal, "comp_a_in_su_2");
	}

	result = saAmfHealthcheckStart (handle,
		&compNameGlobal,
		&key0,
		SA_AMF_HEALTHCHECK_AMF_INVOKED,
		SA_AMF_COMPONENT_FAILOVER);
	printf ("start %d\n", result);

	result = saAmfHealthcheckStart (handle,
		&compNameGlobal,
		&key0,
		SA_AMF_HEALTHCHECK_AMF_INVOKED,
		SA_AMF_COMPONENT_FAILOVER);
	printf ("start %d\n", result);
	result = saAmfComponentRegister (handle, &compNameGlobal, NULL);
	printf ("register result is %d (should be 1)\n", result);

	do {
		select (select_fd + 1, &read_fds, 0, 0, 0);
		saAmfDispatch (handle, SA_DISPATCH_ALL);
	} while (result && stop == 0);

	sleep (5);
	result = saAmfHealthcheckStart (handle,
		&compNameGlobal,
		&key0,
		SA_AMF_HEALTHCHECK_AMF_INVOKED,
		SA_AMF_COMPONENT_FAILOVER);

	do {
		select (select_fd + 1, &read_fds, 0, 0, 0);
		saAmfDispatch (handle, SA_DISPATCH_ALL);
	} while (result);

	saAmfFinalize (handle);

	exit (0);
}
