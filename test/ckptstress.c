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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "saAis.h"
#include "saCkpt.h"

int ckptinv;

struct thread_data {
	int thread_no;
};

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

SaNameT checkpointName = { 5, "abra\0" };

SaCkptCheckpointCreationAttributesT checkpointCreationAttributes = {
	SA_CKPT_WR_ALL_REPLICAS,
	100000,
	0,
	5,
	20000,
	10
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

#define DATASIZE 250000
char data[DATASIZE];
SaCkptIOVectorElementT WriteVectorElements[] = {
	{
		{
			14,
			(SaUint8T *) "section ID #1"
		},
		data, /*"written data #1, this should extend past end of old section data", */
		DATASIZE, /*sizeof ("written data #1, this should extend past end of old section data") + 1, */
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

void *th_dispatch (void *arg)
{
	struct thread_data *td = (struct thread_data *)arg;
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT handle;
	SaAisErrorT error;
	int i;
	SaUint32T erroroneousVectorIndex = 0;

	error = saCkptInitialize (&ckptHandle, &callbacks, &version);

	error = saCkptCheckpointOpen (ckptHandle,
		&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&handle);
	for (i = 0; i < 1000; i++) {
		error = saCkptCheckpointWrite (handle,
			WriteVectorElements,
			1,
			&erroroneousVectorIndex);
		printf ("Thread %d: Attempt %d: error %d\n",
			td->thread_no, i, error);
		if (error != SA_AIS_OK) {
			printf ("Thread %d: Error from write.\n", td->thread_no);
		}
	}

    error = saCkptFinalize (ckptHandle);

	return (0);
}

int main (void) {
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT checkpointHandle;
	SaAisErrorT error;
	int i;
	pthread_t dispatch_thread;

	error = saCkptInitialize (&ckptHandle, &callbacks, &version);

	error = saCkptCheckpointOpen (ckptHandle,
		&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);
	printf ("first open result %d (should be 1)\n", error);

	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
printf ("create2 error is %d\n", error);

	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes2,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
printf ("create2 error is %d\n", error);

	for (i = 0; i < 40; i++) {
		struct thread_data *td;

		td = malloc (sizeof (struct thread_data));
		td->thread_no = i;
		pthread_create (&dispatch_thread, NULL, th_dispatch, td);
	}
	pthread_join (dispatch_thread, NULL);

    error = saCkptInitialize (&ckptHandle, &callbacks, &version);

	return (0);
}
