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
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sched.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "saAis.h"
#include "saAmf.h"

SaAmfHandleT handle;

SaAmfHealthcheckKeyT key0 = {
	.key = "key1",
	.keyLen = 4
};
SaNameT compNameGlobal;
int good_health = 0;
int good_health_limit = 0;

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

static unsigned int healthcheck_no = 0;

int stop = 0;

void HealthcheckCallback (SaInvocationT invocation,
	const SaNameT *compName,
	SaAmfHealthcheckKeyT *healthcheckKey)
{
	SaAisErrorT res;

	if( !good_health && healthcheck_no++);


/*
	printf ("Healthcheck %u for key '%s' for component ",
		healthcheck_no, healthcheckKey->key);

	printSaNameT ((SaNameT *)compName);
	printf ("\n");
*/
	if (healthcheck_no == good_health_limit ) {
printf ("COMPONENT REPORTING ERROR %s\n", compNameGlobal.value);
		saAmfComponentErrorReport (handle, compName, 0, SA_AMF_COMPONENT_RESTART, 0);
		res = saAmfResponse (handle, invocation, SA_AIS_OK);
printf ("COMPONENT DONE REPORTING ERROR\n");
	} else {
		res = saAmfResponse (handle, invocation, SA_AIS_OK);
	}

/*
	if (healthcheck_no < good_health_limit) {
		res = saAmfResponse (handle, invocation, SA_AIS_OK);
	}
*/
/*
	if (healthcheck_no == good_health_limit) {
		res = saAmfHealthcheckStop (handle, &compNameGlobal, &key0);
		stop = 1;
	}
*/
}

void ComponentTerminateCallback (
	SaInvocationT invocation,
	const SaNameT *compName)
{
	printf ("ComponentTerminateCallback\n");
	saAmfResponse (handle, invocation, SA_AIS_OK);
	exit (0);
}

#if 1
    #include <sys/time.h>
    #define TRU "%d"
    #define TRS "%s" 
    #define TR(format,x) do {                               \
	struct timeval t;\
	gettimeofday(&t,NULL);                                   \
	printf("%s:%d: %s : %d : %u: %u :%s : " format "\n",\
		__FILE__,__LINE__,__FUNCTION__,             \
		getpid(),(int)t.tv_sec, (int)t.tv_usec,#x,x);          \
    }while(0)
#else
    #define TRU "%d"
    #define TRS "%s" 
    #define TR(format,x)
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
		int i;
		TR(TRU, csiDescriptor->csiAttr.number);
		for(i=0; i<csiDescriptor->csiAttr.number; i++) {

		    if( strcmp(csiDescriptor->csiAttr.attr[i].attrName, "good_health_limit") == 0){
			good_health = strcmp(csiDescriptor->csiAttr.attr[i].attrValue, "0") ? 0 : 1;
			good_health_limit = atoi(csiDescriptor->csiAttr.attr[i].attrValue);
			
		    }

 
		    TR(TRS,csiDescriptor->csiAttr.attr[i].attrName);
		    TR(TRS, csiDescriptor->csiAttr.attr[i].attrValue);
		} 

		TR(TRU, csiDescriptor->csiFlags);


		printSaNameT((SaNameT*) &csiDescriptor->csiStateDescriptor.activeDescriptor.activeCompName);
		TR(TRU, csiDescriptor->csiStateDescriptor.activeDescriptor.transitionDescriptor);

		break;  
         
	case SA_AMF_HA_STANDBY:
		printf ("CSISetCallback:"); 
		printf ("for CSI '");
		printSaNameT ((SaNameT *)&csiDescriptor->csiName);
		printf ("' for component ");
		printSaNameT ((SaNameT *)compName);
		printf ("'");
		printf (" requested to enter hastate SA_AMF_STANDBY.\n");
		res = saAmfResponse (handle, invocation, SA_AIS_OK);
		
		TR(TRU,csiDescriptor->csiAttr.number);
		for(i=0; i<csiDescriptor->csiAttr.number; i++) {
		    if(!strcmp(csiDescriptor->csiAttr.attr[i].attrName, "good_health") && 
		       !strcmp(csiDescriptor->csiAttr.attr[i].attrValue, "true")){
			good_health = 1;
		    }
		    TR(TRS,csiDescriptor->csiAttr.attr[i].attrName);
		    TR(TRS,csiDescriptor->csiAttr.attr[i].attrValue);
		} 
		printf("%s:%d:%s:%d\n",__FILE__,__LINE__,__FUNCTION__,res);
		TR(TRU,csiDescriptor->csiFlags);

		printSaNameT((SaNameT*) &csiDescriptor->csiStateDescriptor.standbyDescriptor.activeCompName);
		TR(TRU,csiDescriptor->csiStateDescriptor.standbyDescriptor.standbyRank);

		break;
	case SA_AMF_HA_QUIESCED:
		break;
	case SA_AMF_HA_QUIESCING:
		break;
	}
}

void CSIRemoveCallback (
	SaInvocationT invocation,
	const SaNameT *compName,
	const SaNameT *csiName,
	SaAmfCSIFlagsT csiFlags)
{
	int res;

	printf ("CSIRemoveCallback for component '");
	printSaNameT ((SaNameT *)compName);
	printf ("' in CSI '");
	printSaNameT ((SaNameT *)csiName);
	printf ("'\n");
	res = saAmfResponse (handle, invocation, SA_AIS_OK);
}

#ifdef COMPILE_OUT
void ProtectionGroupTrackCallback (
	const SaNameT *csiName,
	SaAmfProtectionGroupNotificationT *notificationBuffer,
	SaUint32T numberOfItems,
	SaUint32T numberOfMembers,
	SaAisErrorT error)
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

#endif

SaAmfCallbacksT amfCallbacks = {
	.saAmfHealthcheckCallback = HealthcheckCallback,
	.saAmfComponentTerminateCallback = ComponentTerminateCallback,
	.saAmfCSISetCallback = CSISetCallback,
	.saAmfCSIRemoveCallback = CSIRemoveCallback,
};

SaAmfCallbacksT amfCallbacks;

SaVersionT version = { 'B', 1, 1 };

#if defined(OPENAIS_BSD) || defined(OPENAIS_LINUX)
static struct sched_param sched_param = {
    sched_priority: 99
};
#endif

void sigintr_handler (int signum) {
	exit (0);
}
 
void write_pid (void) {
	char pid[256];
	char filename[256];
	int fd;
	int res;
	
	sprintf (filename,  "/var/run/openais_cleanup_%s", compNameGlobal.value);
	fd = open (filename, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU);
	if (fd == -1) {
		printf("Failed using /var/run for pid file, using /tmp\n");
		sprintf (filename,  "/tmp/openais_cleanup_%s", compNameGlobal.value);
		fd = open (filename, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU);
	}
	sprintf (pid, "%d", getpid());
	res = write (fd, pid, strlen (pid));
	close (fd);
}

int main (int argc, char **argv) {
	int result;
	SaSelectionObjectT select_fd;
	fd_set read_fds;
	extern char *optarg;
	extern int optind;

	signal (SIGINT, sigintr_handler);
#if defined(OPENAIS_BSD) || defined(OPENAIS_LINUX)
	result = sched_setscheduler (0, SCHED_RR, &sched_param);
	if (result == -1) {
		printf ("couldn't set sched priority\n");
 	}
#endif

	result = saAmfInitialize (&handle, &amfCallbacks, &version);
	if (result != SA_AIS_OK) {
		printf ("initialize result is %d\n", result);
		exit (1);
	}

	FD_ZERO (&read_fds);
	saAmfSelectionObjectGet (handle, &select_fd);
	FD_SET (select_fd, &read_fds);
	saAmfComponentNameGet (handle, &compNameGlobal);
	write_pid ();
	

	result = saAmfHealthcheckStart (handle,
		&compNameGlobal,
		&key0,
		SA_AMF_HEALTHCHECK_AMF_INVOKED,
		SA_AMF_COMPONENT_FAILOVER);
	printf ("healthcheck start result %d (should be 1)\n", result);

	/*
	 * Test already started healthcheck
	 */
	result = saAmfHealthcheckStart (handle,
		&compNameGlobal,
		&key0,
		SA_AMF_HEALTHCHECK_AMF_INVOKED,
		SA_AMF_COMPONENT_FAILOVER);
	printf ("healthcheck start result %d (should be 14)\n", result);

	result = saAmfComponentRegister (handle, &compNameGlobal, NULL);
	printf ("register result is %d (should be 1)\n", result);

	do {
		select (select_fd + 1, &read_fds, 0, 0, 0);
		saAmfDispatch (handle, SA_DISPATCH_ALL);
	} while (result && stop == 0);

	printf ("healthchecks stopped for 5 seconds\n");
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
