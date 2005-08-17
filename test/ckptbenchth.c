#define _BSD_SOURCE
/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <pthread.h>
#include <assert.h>

#include "saAis.h"
#include "saCkpt.h"

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
	.maxSectionIdSize =	10
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
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT checkpointHandle;
	int write_count;
	int write_size;
	int thread;
};

void *benchmark_thread (void *arg) 
{
	
	SaCkptCheckpointHandleT checkpointHandle;
	SaCkptHandleT ckptHandle;
	int write_count;
	int write_size;
	SaErrorT error;
	SaUint32T erroroneousVectorIndex = 0;
	struct threaddata *td = (struct threaddata *)arg;
	int ckptinv;

	checkpointHandle = td->checkpointHandle;
	ckptHandle = td->ckptHandle;
	write_count = td->write_count;
	write_size = td->write_size;

	WriteVectorElements[0].dataSize = write_size;

	for (ckptinv = 0; ckptinv < write_count; ckptinv++) {
		/*
		 * Test checkpoint write
		 */
		do {
		error = saCkptCheckpointWrite (checkpointHandle,
			WriteVectorElements,
			1,
			&erroroneousVectorIndex);
		} while (error == SA_ERR_TRY_AGAIN);
		if (error != SA_OK) {
			printf ("saCkptCheckpointWrite result %d (should be 1)\n", error);
			exit (1);
		}
	}
	pthread_exit (0);
}


void threaded_bench (SaCkptHandleT *ckptHandles, SaCkptCheckpointHandleT *checkpointHandles,
	int threads, int write_count, int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct threaddata td[100];
	int i;
	pthread_t threadt[100];
	int res;

	runs = threads;
	gettimeofday (&tv1, NULL);

	for (i = 0; i < threads; i++) {
		td[i].ckptHandle = ckptHandles[i];
		td[i].checkpointHandle = checkpointHandles[i];
		td[i].write_count = write_count;
		td[i].write_size = write_size;
		td[i].thread = i;

		res = pthread_create (&threadt[i], NULL, benchmark_thread, (void *)&td[i]);
	}

	for (i = 0; i < threads; i++) {
		pthread_join (threadt[i], NULL);
	}

	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d Writes ", write_count * threads);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ", 
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)write_count * (float)threads) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n", 
		((float)write_count * (float)threads) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}

SaNameT checkpointName = { 12, "abra\0" };

#define CHECKPOINT_THREADS 50
int main (void) {
	SaCkptHandleT ckptHandles[500];
	SaCkptCheckpointHandleT checkpointHandles[500];
	SaErrorT error;
	int size;
	int count;
	int i, j;
	
	/*
	 * Create CHECPOINT_THREADS checkpoints
	 */
	for (i  = 0; i < CHECKPOINT_THREADS; i++) {
		sprintf ((char *)checkpointName.value, "checkpoint%d \n", i);
		error = saCkptInitialize (&ckptHandles[i], &callbacks, &version);
		assert (error == SA_AIS_OK);

		error = saCkptCheckpointOpen (ckptHandles[i],
			&checkpointName,
			&checkpointCreationAttributes,
			SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
			SA_TIME_END,
			&checkpointHandles[i]);
		assert (error == SA_AIS_OK);

		error = saCkptSectionCreate (checkpointHandles[i],
			&sectionCreationAttributes1,
			"Initial Data #0",
			strlen ("Initial Data #0") + 1);
		assert (error == SA_AIS_OK);

		error = saCkptSectionCreate (checkpointHandles[i],
			&sectionCreationAttributes2,
			"Initial Data #0",
			strlen ("Initial Data #0") + 1);
		assert (error == SA_AIS_OK);
	}

	for (i = 25; i < CHECKPOINT_THREADS; i++) {	/* i threads */
		count = 3000; /* initial write count */
		size = 10000; /* initial size */
		printf ("THREADS %d\n", i);
		for (j = 0; j < 5; j++) { /* number of runs with i threads */
			threaded_bench (ckptHandles, checkpointHandles, i, count, size);
			/*
			 * Adjust count to 95% of previous count
			 * adjust size upwards by 1500
			 * This keeps the run times similiar
			 */
			count = (((float)count) * 0.95);
			size += 1000;
		}
	}
	return (0);
}
