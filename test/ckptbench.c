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
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ais_types.h"
#include "saCkpt.h"

int alarm_notice;

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
        .creationFlags =        SA_CKPT_WR_ALL_REPLICAS,
        .checkpointSize =       250000,
        .retentionDuration =    0,
        .maxSections =          5,
        .maxSectionSize =       250000,
        .maxSectionIdSize =     10
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

#define DATASIZE 200000
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

void ckpt_benchmark (SaCkptCheckpointHandleT checkpointHandle,
	int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	SaUint32T erroroneousVectorIndex = 0;
	SaErrorT error;
	int write_count = 0;

	alarm_notice = 0;
	alarm (10);
	WriteVectorElements[0].dataSize = write_size;

	gettimeofday (&tv1, NULL);
	do {
		/*
		 * Test checkpoint write
		 */
retry:
		error = saCkptCheckpointWrite (checkpointHandle,
			WriteVectorElements,
			1,
			&erroroneousVectorIndex);
		if (error == SA_AIS_ERR_TRY_AGAIN) {
			goto retry;
		}
		if (error != SA_OK) {
			printf ("saCkptCheckpointWrite result %d (should be 1)\n", error);
			exit (1);
		}
		write_count += 1;
	} while (alarm_notice == 0);
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d Writes ", write_count);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ", 
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)write_count) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n", 
		((float)write_count) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}

void sigalrm_handler (int num)
{
	alarm_notice = 1;
}

int main (void) {
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT checkpointHandle;
	SaErrorT error;
	int size;
	int i;
	
	signal (SIGALRM, sigalrm_handler);

    error = saCkptInitialize (&ckptHandle, &callbacks, &version);

	error = saCkptCheckpointOpen (ckptHandle,
		&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);
	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes2,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);

	size = 200000;

	for (i = 0; i < 50; i++) { /* number of repetitions - up to 50k */
		ckpt_benchmark (checkpointHandle, size);
		size += 1000;
	}

    error = saCkptFinalize (ckptHandle);
	return (0);
}
