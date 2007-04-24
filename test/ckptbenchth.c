#define _BSD_SOURCE
/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <pthread.h>
#include <assert.h>
#include <signal.h>

#include "saAis.h"
#include "saCkpt.h"
#include "sa_error.h"

#ifdef OPENAIS_SOLARIS
#define timersub(a, b, result)						\
    do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;		\
	if ((result)->tv_usec < 0) {					\
	    --(result)->tv_sec;						\
	    (result)->tv_usec += 1000000;				\
	}								\
    } while (0)
#endif

int alarm_notice = 0;

void fail_on_error(SaAisErrorT error, char* opName) {
	if (error != SA_AIS_OK) {
        printf ("%s: result %s\n", opName, get_sa_error_b(error));
        exit (1);
	}
}

void printSaNameT (SaNameT *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

SaVersionT version = { 'B', 1, 1 };

SaCkptCallbacksT callbacks = {
    0,
    0
};

SaCkptCheckpointCreationAttributesT checkpointCreationAttributes = {
	.creationFlags =	SA_CKPT_WR_ALL_REPLICAS,
	.checkpointSize =	100000,
	.retentionDuration =	0,
	.maxSections =		5,
	.maxSectionSize =	150000,
	.maxSectionIdSize =	15
};

SaCkptSectionIdT sectionId1 = {
	14,
	(SaUint8T *) "section ID #1"
};

SaCkptSectionIdT sectionId2 = {
	14,
	(SaUint8T *) "section ID #2"
};
SaCkptSectionCreationAttributesT sectionCreationAttributes1 = {
	&sectionId1,
	0xFFFFFFFF
};

SaCkptSectionCreationAttributesT sectionCreationAttributes2 = {
	&sectionId2,
	0xFFFFFFFF
};

char readBuffer1[1025];

char readBuffer2[1025];

SaCkptIOVectorElementT ReadVectorElements[] = {
	{
		{
			14,
			(SaUint8T *) "section ID #1"
		},
		readBuffer1,
		sizeof (readBuffer1),
		0, 
		0
	},
	{
		{
			14,
			(SaUint8T *) "section ID #2"
		},
		readBuffer2,
		sizeof (readBuffer2),
		0, 
		0
	}
};

#define DATASIZE 1000
#define LOOPS 5000

char data[500000];
SaCkptIOVectorElementT WriteVectorElements[] = {
	{
		{
			14,
			(SaUint8T *) "section ID #1"
		},
		data, /*"written data #1, this should extend past end of old section data", */
		DATASIZE, /*sizeof ("data #1, this should extend past end of old section data") + 1, */
		0, //5, 
		0
	}
#ifdef COMPILE_OUT
	{
		{
			14,
			(SaUint8T *) "section ID #2"
		},
		data, /*"written data #2, this should extend past end of old section data" */
		DATASIZE, /*sizeof ("written data #2, this should extend past end of old section data") + 1, */
		0, //3, 
		0
	}
#endif
};

int runs = 0;

struct threaddata {
	SaCkptHandleT ckpt_handle;
	SaCkptCheckpointHandleT checkpoint_handle;
	int write_size;
	int thread;
	pthread_attr_t thread_attr;
	pthread_t thread_id;
	int written;
};

extern void pthread_exit(void *) __attribute__((noreturn));

void *benchmark_thread (void *arg) 
{
	
	SaCkptCheckpointHandleT checkpoint_handle;
	SaCkptHandleT ckpt_handle;
	int write_size;
	SaAisErrorT error;
	SaUint32T erroroneousVectorIndex = 0;
	struct threaddata *td = (struct threaddata *)arg;

	checkpoint_handle = td->checkpoint_handle;
	ckpt_handle = td->ckpt_handle;
	write_size = td->write_size;

	WriteVectorElements[0].dataSize = write_size;

	do {
		/*
		 * Test checkpoint write
		 */
		do {
		error = saCkptCheckpointWrite (checkpoint_handle,
			WriteVectorElements,
			1,
			&erroroneousVectorIndex);
		} while (error == SA_AIS_ERR_TRY_AGAIN);
		fail_on_error(error, "saCkptCheckpointWrite");
		td->written += 1;
	} while (alarm_notice == 0);
	pthread_exit (0);
}


void threaded_bench (
	SaCkptHandleT *ckpt_handles,
	SaCkptCheckpointHandleT *checkpoint_handles,
	int threads,
	int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct threaddata td[100];
	int i;
	int res;
	int written = 0;

	runs = threads;
	gettimeofday (&tv1, NULL);

	for (i = 0; i < threads; i++) {
		td[i].ckpt_handle = ckpt_handles[i];
		td[i].checkpoint_handle = checkpoint_handles[i];
		td[i].write_size = write_size;
		td[i].thread = i;
		td[i].written = 0;
		pthread_attr_init (&td[i].thread_attr);
		pthread_attr_setstacksize (&td[i].thread_attr, 16384);
		pthread_attr_setdetachstate (&td[i].thread_attr, PTHREAD_CREATE_JOINABLE);

		res = pthread_create (&td[i].thread_id, &td[i].thread_attr,
			benchmark_thread, (void *)&td[i]);
	}

	for (i = 0; i < threads; i++) {
		pthread_join (td[i].thread_id, NULL);
		written += td[i].written;
	}
	alarm_notice = 0;

	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d Writes ", written);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ", 
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)written) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n", 
		((float)written) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}

SaNameT checkpointName;

#define CHECKPOINT_THREADS_START 25
#define CHECKPOINT_THREADS_MAX 500
 
void sigalrm_handler (int num)
{
	alarm_notice = 1;
}


int main (void) {
	SaCkptHandleT ckpt_handles[CHECKPOINT_THREADS_MAX];
	SaCkptCheckpointHandleT checkpoint_handles[CHECKPOINT_THREADS_MAX];
	SaAisErrorT error;
	int size;
	int i, j;
	
	signal (SIGALRM, sigalrm_handler); 

	printf ("Creating (%d) checkpoints.\n", CHECKPOINT_THREADS_MAX);
	/*
	 * Create CHECPOINT_THREADS_MAX checkpoints
	 */
	for (i  = 0; i < CHECKPOINT_THREADS_MAX; i++) {
		sprintf ((char *)checkpointName.value, "checkpoint (%d)", i);
		checkpointName.length = strlen ((char *)checkpointName.value);
		do {
			error = saCkptInitialize (&ckpt_handles[i], &callbacks, &version);
		} while (error == SA_AIS_ERR_TRY_AGAIN);
		assert (error == SA_AIS_OK);

		do {
		error = saCkptCheckpointOpen (ckpt_handles[i],
			&checkpointName,
			&checkpointCreationAttributes,
			SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
			SA_TIME_END,
			&checkpoint_handles[i]);
		} while (error == SA_AIS_ERR_TRY_AGAIN);
		assert (error == SA_AIS_OK);

		do {
			error = saCkptSectionCreate (checkpoint_handles[i],
				&sectionCreationAttributes1,
				"Initial Data #0",
				strlen ("Initial Data #0") + 1);
		} while (error == SA_AIS_ERR_TRY_AGAIN);
		assert (error == SA_AIS_OK);

		do {
			error = saCkptSectionCreate (checkpoint_handles[i],
				&sectionCreationAttributes2,
				"Initial Data #0",
				strlen ("Initial Data #0") + 1);
		} while (error == SA_AIS_ERR_TRY_AGAIN);
		assert (error == SA_AIS_OK);
	}

	for (i = CHECKPOINT_THREADS_START; i < CHECKPOINT_THREADS_MAX; i++) {	/* i threads */
		printf ("Starting benchmark with (%d) threads.\n", i);
		size = 10000; /* initial size */
		for (j = 0; j < 5; j++) { /* number of runs with i threads */
			alarm (10);
			threaded_bench (ckpt_handles, checkpoint_handles, i, size);
			size += 1000;
		}
	}
	return (0);
}
