/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
 * Copyright (c) 2006 Ericsson AB.
 * Copyright (c) 2006 Sun Microsystems, Inc.
 *
 * All rights reserved.
 *
 * Author:  Steven Dake (sdake@redhat.com)
 *	    Hans Feldt
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
#include <stdarg.h>

#include "saAis.h"
#include "saAmf.h"

SaAmfHandleT handle;

SaAmfHealthcheckKeyT keyAmfInvoked = {
	.key = "amfInvoked",
	.keyLen = 10
};

SaAmfHealthcheckKeyT keyCompInvoked = {
	.key = "compInvoked",
	.keyLen = 11
};

SaNameT compNameGlobal;
int good_health = 0;
int good_health_limit = 0;

enum {
	FINALIZE = 0,
	UNREGISTER,
	ERROR_REPORT
};

#define die(format, args...) _die (__FILE__, __LINE__, format, ##args)

static void _die (char *file, int line, char *format, ...)  __attribute__((format(printf, 3, 4)));
static void _die (char *file, int line, char *format, ...)
{
	char buf[1024];
	va_list ap;

	sprintf (buf, "%d - %s:#%d - Error: '%s', exiting...\n",
			 (int)getpid(), file, line, format);

	va_start (ap, format);
	vfprintf (stderr, buf, ap);
	va_end(ap);

	exit (-1);
}

static void response (
	SaAmfHandleT handle, SaInvocationT invocation, SaAisErrorT error)
{
	SaAisErrorT result;

	do {
		result = saAmfResponse (handle, invocation, error);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			fprintf(stderr, "%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_OK) {
		die ("saAmfResponse failed %d", result);
	}
}

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

	if (healthcheck_no == good_health_limit ) {
        response (handle, invocation, SA_AIS_OK);
        res = saAmfHealthcheckStop (handle,
                                       &compNameGlobal,
                                       &keyAmfInvoked);
        printf ("healthcheck stop result %d (should be %d)\n", res, SA_AIS_OK);

        printf ("COMPONENT REPORTING ERROR %s\n", compNameGlobal.value);
		saAmfComponentErrorReport (handle, compName, 0, SA_AMF_COMPONENT_RESTART, 0);
        printf ("COMPONENT DONE REPORTING ERROR\n");
	} else {
		response (handle, invocation, SA_AIS_OK);
	}
}

void ComponentTerminateCallback (
	SaInvocationT invocation,
	const SaNameT *compName)
{
	printf ("ComponentTerminateCallback\n");
	response (handle, invocation, SA_AIS_OK);
	exit (0);
}

#if 0
    #include <sys/time.h>
    #define TRU "%d"
    #define TRS "%s" 
    #define TR(format,x) do {                               \
	struct timeval t;\
	gettimeofday(&t,NULL);                                   \
	printf("%s:%d: %s : %d : %u: %u :%s : " format "\n",\
		__FILE__,__LINE__,__FUNCTION__,             \
		(int)getpid(),(int)t.tv_sec, (int)t.tv_usec,#x,x);          \
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
	SaAmfHAStateT state;
	int res;
	int i;

	switch (haState) {
	case SA_AMF_HA_ACTIVE:
		printf ("PID %d: Component '%s' requested to enter hastate SA_AMF_ACTIVE"
				" for \n\tCSI '%s'\n",
			(int)getpid(), compName->value, csiDescriptor->csiName.value);
		response (handle, invocation, SA_AIS_OK);

		res = saAmfHAStateGet (handle, compName, &csiDescriptor->csiName, &state);
		if (res != SA_AIS_OK || haState != state) {
			fprintf (stderr, "saAmfHAStateGet failed: %d\n", res);
			exit (-1);
		}

		TR(TRU, csiDescriptor->csiAttr.number);
		for(i=0; i<csiDescriptor->csiAttr.number; i++) {

		    if( strcmp((char*)csiDescriptor->csiAttr.attr[i].attrName, "good_health_limit") == 0){
				good_health = strcmp((char*)csiDescriptor->csiAttr.attr[i].attrValue, "0") ? 0 : 1;
				good_health_limit = atoi((char*)csiDescriptor->csiAttr.attr[i].attrValue);
			
		    }
#if 0
		    TR(TRS,csiDescriptor->csiAttr.attr[i].attrName);
		    TR(TRS, csiDescriptor->csiAttr.attr[i].attrValue);
#endif
		} 

		TR(TRU, csiDescriptor->csiFlags);


		printSaNameT((SaNameT*) &csiDescriptor->csiStateDescriptor.activeDescriptor.activeCompName);
		TR(TRU, csiDescriptor->csiStateDescriptor.activeDescriptor.transitionDescriptor);

		break;  
         
	case SA_AMF_HA_STANDBY:
		printf ("PID %d: Component '%s' requested to enter hastate SA_AMF_STANDBY "
				"for \n\tCSI '%s'\n",
			(int)getpid(), compName->value, csiDescriptor->csiName.value);
		response (handle, invocation, SA_AIS_OK);
		
		TR(TRU,csiDescriptor->csiAttr.number);
		for(i=0; i<csiDescriptor->csiAttr.number; i++) {
		    if(!strcmp((char*)csiDescriptor->csiAttr.attr[i].attrName, "good_health") && 
		       !strcmp((char*)csiDescriptor->csiAttr.attr[i].attrValue, "true")){
			good_health = 1;
		    }
		    TR(TRS,csiDescriptor->csiAttr.attr[i].attrName);
		    TR(TRS,csiDescriptor->csiAttr.attr[i].attrValue);
		} 
		TR(TRU,csiDescriptor->csiFlags);

		printSaNameT((SaNameT*) &csiDescriptor->csiStateDescriptor.standbyDescriptor.activeCompName);
		TR(TRU,csiDescriptor->csiStateDescriptor.standbyDescriptor.standbyRank);

		break;
	case SA_AMF_HA_QUIESCED:
		printf ("%d: Component '%s' requested to enter hastate SA_AMF_HA_QUIESCED "
				"for \n\tCSI '%s'\n",
			(int)getpid(), compName->value, csiDescriptor->csiName.value);
		response (handle, invocation, SA_AIS_OK);
		break;
	case SA_AMF_HA_QUIESCING:
		break;
	default:
		break;
	}
}

void CSIRemoveCallback (
	SaInvocationT invocation,
	const SaNameT *compName,
	const SaNameT *csiName,
	SaAmfCSIFlagsT csiFlags)
{
	printf ("CSIRemoveCallback for component '");
	printSaNameT ((SaNameT *)compName);
	printf ("' in CSI '");
	printSaNameT ((SaNameT *)csiName);
	printf ("'\n");
	response (handle, invocation, SA_AIS_OK);
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
#if ! defined(TS_CLASS) && (defined(OPENAIS_BSD) || defined(OPENAIS_LINUX) || defined(OPENAIS_SOLARIS))
static struct sched_param sched_param;
#endif

void sigintr_handler (int signum) {
	stop = FINALIZE;
}

void sigusr1_handler (int signum) {
	stop = UNREGISTER;
}

void sigusr2_handler (int signum) {
	stop = ERROR_REPORT;
}

void write_pid (void) {
	char pid[256];
	char filename[256];
	int fd;
	int res;
	
	sprintf (filename,  "/var/run/openais_cleanup_%s", compNameGlobal.value);
	fd = open (filename, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU);
	if (fd == -1) {
		printf("%d: Failed using /var/run for pid file, using /tmp\n", (int)getpid());
		sprintf (filename,  "/tmp/openais_cleanup_%s", compNameGlobal.value);
		fd = open (filename, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU);
	}
	sprintf (pid, "%d", (int)getpid());
	res = write (fd, pid, strlen (pid));
	close (fd);
}

static SaSelectionObjectT comp_init ()
{
	char *name;
	char *env;
	int result;
	SaSelectionObjectT select_fd;
	SaAmfPmErrorsT pmErrors = (SA_AMF_PM_ZERO_EXIT | 
							   SA_AMF_PM_NON_ZERO_EXIT | 
							   SA_AMF_PM_ABNORMAL_END);

	name = getenv ("SA_AMF_COMPONENT_NAME");
	if (name == NULL) {
		die ("SA_AMF_COMPONENT_NAME missing");
	}

	if (strstr (name, "safComp=") == NULL ||
		strstr (name, "safSu=") == NULL ||
		strstr (name, "safSg=") == NULL ||
		strstr (name, "safApp=") == NULL) {
		die ("SA_AMF_COMPONENT_NAME value wrong");
	}

	printf("%d: Hello world from %s\n", (int)getpid(), name);

	env = getenv ("var1");
	if (env == NULL) {
		die ("var1 missing");
	}
	if (strcmp (env, "val1") != 0) {
		die ("var1 value wrong");
	}
	env = getenv ("var2");
	if (env == NULL) {
		die ("var2 wrong");
	}
	if (strcmp (env, "val2") != 0) {
		die ("var2 value wrong");
	}

	signal (SIGINT, sigintr_handler);
	signal (SIGUSR1, sigusr1_handler);
	signal (SIGUSR2, sigusr2_handler);

#if ! defined(TS_CLASS) && (defined(OPENAIS_BSD) || defined(OPENAIS_LINUX) || defined(OPENAIS_SOLARIS))

	sched_param.sched_priority = sched_get_priority_max(SCHED_RR);
	if (sched_param.sched_priority == -1) {
		fprintf (stderr, "%d: couldn't retrieve the maximum scheduling " \
			"priority supported by the Round-Robin class (%s)\n",
			(int)getpid(), strerror(errno));
	} else {
		result = sched_setscheduler (0, SCHED_RR, &sched_param);
		if (result == -1) {
			fprintf (stderr, "%d: couldn't set sched priority (%s)\n",
				(int)getpid(), strerror(errno));
		}
	}
#endif

	do {
		result = saAmfInitialize (&handle, &amfCallbacks, &version);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_OK) {
		die ("saAmfInitialize result is %d", result);
	}

	do {
		result = saAmfSelectionObjectGet (handle, &select_fd);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_OK) {
		die ("saAmfSelectionObjectGet failed %d", result);
	}

	do {
		result = saAmfComponentNameGet (handle, &compNameGlobal);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_OK) {
		die ("saAmfComponentNameGet failed %d", result);
	}
	write_pid ();

	do {
		result = saAmfHealthcheckStart (handle,
			&compNameGlobal,
			&keyAmfInvoked,
			SA_AMF_HEALTHCHECK_AMF_INVOKED,
			SA_AMF_COMPONENT_FAILOVER);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_OK) {
		die ("saAmfHealthcheckStart failed %d", result);
	}

	do {
		result = saAmfHealthcheckStart (handle,
			&compNameGlobal,
			&keyCompInvoked,
			SA_AMF_HEALTHCHECK_COMPONENT_INVOKED,
			SA_AMF_COMPONENT_FAILOVER);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_OK) {
		die ("saAmfHealthcheckStart failed %d", result);
	}

	{
		SaNameT badname;
		strcpy ((char*)badname.value, "badname");
		badname.length = 7;
		do {
			result = saAmfComponentRegister (handle, &badname, NULL);
			if (result == SA_AIS_ERR_TRY_AGAIN) {
				printf("%d: TRY_AGAIN received\n", (int)getpid());
				usleep (100000);
			}
		} while (result == SA_AIS_ERR_TRY_AGAIN);
		if (result != SA_AIS_ERR_INVALID_PARAM) {
			die ("saAmfComponentRegister failed %d", result);
		}
	}

	do {
		result = saAmfComponentRegister (handle, &compNameGlobal, NULL);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_OK) {
		die ("saAmfComponentRegister failed %d", result);
	}

	/*
	 * startup passive monitoring
	 */
	do {
		result = saAmfPmStart (handle,
			&compNameGlobal, getpid(), 1,
			pmErrors, 
			SA_AMF_COMPONENT_FAILOVER);

		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);

	/*
	 * Test already started healthcheck
	 */
	do {
		result = saAmfHealthcheckStart (handle,
			&compNameGlobal,
			&keyAmfInvoked,
			SA_AMF_HEALTHCHECK_AMF_INVOKED,
			SA_AMF_COMPONENT_FAILOVER);
		if (result == SA_AIS_ERR_TRY_AGAIN) {
			printf("%d: TRY_AGAIN received\n", (int)getpid());
			usleep (100000);
		}
	} while (result == SA_AIS_ERR_TRY_AGAIN);
	if (result != SA_AIS_ERR_EXIST) {
		die ("saAmfHealthcheckStart failed %d", result);
	}

	return select_fd;
}

static void handle_intr (void)
{
	SaAisErrorT result;

	switch (stop) {
		case FINALIZE:
			result = saAmfFinalize (handle);
			if (result != SA_AIS_OK) {
				die ("saAmfFinalize failed %d", result);
			}
			fprintf(stderr, "%d: %s exiting\n",
					(int)getpid(), compNameGlobal.value);
			exit (EXIT_SUCCESS);
			break;
		case UNREGISTER:
			fprintf(stderr, "%d: %s unregistering\n",
					(int)getpid(), compNameGlobal.value);
			result = saAmfComponentUnregister (
				handle, &compNameGlobal, NULL);
			if (result != SA_AIS_OK) {
				die ("saAmfComponentUnregister failed %d", result);
			}
			fprintf(stderr, "%d: waiting after unregister\n", (int)getpid());
			while (1) {
				sleep (100000000);
			}
			break;
		case ERROR_REPORT:
			fprintf(stderr, "%d: %s error reporting\n",
					(int)getpid(), compNameGlobal.value);
			result = saAmfComponentErrorReport (
				handle, &compNameGlobal, 0, SA_AMF_COMPONENT_RESTART, 0);
			if (result != SA_AIS_OK) {
				die ("saAmfComponentErrorReport failed %d", result);
			}
			fprintf(stderr, "%d: waiting after error report\n", (int)getpid());
			while (1) {
				sleep (100000000);
			}
			break;
		default:
			die ("unknown %d", stop);
			break;
	}
}

int main (int argc, char **argv)
{
	int result;
	SaSelectionObjectT select_fd;
	fd_set read_fds;
	struct timeval tv;

	select_fd = comp_init();
	FD_ZERO (&read_fds);

	do {
		tv.tv_sec = 2; /* related to value in amf.conf! */
		tv.tv_usec = 0;
		FD_SET (select_fd, &read_fds);
		result = select (select_fd + 1, &read_fds, 0, 0, &tv);
		if (result == -1) {
			if (errno == EINTR) {
				handle_intr ();
			} else {
				die ("select failed - %s", strerror (errno));
			}
		} else if (result > 0) {
			do {
				result = saAmfDispatch (handle, SA_DISPATCH_ALL);
				if (result == SA_AIS_ERR_TRY_AGAIN) {
					fprintf(stderr, "%d: TRY_AGAIN received\n", (int)getpid());
					usleep (100000);
				}
			} while (result == SA_AIS_ERR_TRY_AGAIN);

			if (result != SA_AIS_OK) {
				die ("saAmfDispatch failed %d", result);
			}
		} else {
            /* timeout */
			do {
				result = saAmfHealthcheckConfirm (handle, &compNameGlobal,
					&keyCompInvoked, SA_AIS_OK);
				if (result == SA_AIS_ERR_TRY_AGAIN) {
					fprintf(stderr, "%d: TRY_AGAIN received\n", (int)getpid());
					usleep (100000);
				}
			} while (result == SA_AIS_ERR_TRY_AGAIN);

			if (result != SA_AIS_OK) {
				die ("saAmfHealthcheckConfirm failed %d", result);
			}
		}
	} while (stop == 0);

	fprintf(stderr, "%d: exiting...\n", (int)getpid());
	exit (EXIT_SUCCESS);
}


