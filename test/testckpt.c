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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <sys/time.h>

#include "ais_types.h"
#include "ais_ckpt.h"

#define SECONDS_TO_EXPIRE 4

int ckptinv;
void printSaNameT (SaNameT *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

SaVersionT version = { 'A', 1, 1 };

SaNameT checkpointName = { 5, "abra\0" };

SaCkptCheckpointCreationAttributesT checkpointCreationAttributes = {
	SA_CKPT_WR_ALL_REPLICAS,
	100000,
	5000000000,
	5,
	20000,
	10
};

SaCkptSectionIdT sectionId1 = {
	"section ID #1",
	14
};

SaCkptSectionIdT sectionId2 = {
	"section ID #2",
	14
};
SaCkptSectionCreationAttributesT sectionCreationAttributes1 = {
	&sectionId1,
	SA_TIME_END
};

SaCkptSectionCreationAttributesT sectionCreationAttributes2 = {
	&sectionId2,
	SA_TIME_END
};

char readBuffer1[1025];

char readBuffer2[1025];

SaCkptIOVectorElementT ReadVectorElements[] = {
	{
		{
			"section ID #1",
			14
		},
		readBuffer1,
		sizeof (readBuffer1),
		0, 
		0
	},
	{
		{
			"section ID #2",
			14
		},
		readBuffer2,
		sizeof (readBuffer2),
		0, 
		0
	}
};

#define DATASIZE 127000
char data[DATASIZE];
SaCkptIOVectorElementT WriteVectorElements[] = {
	{
		{
			"section ID #1",
			14
		},
		data, /*"written data #1, this should extend past end of old section data", */
		DATASIZE, /*sizeof ("data #1, this should extend past end of old section data") + 1, */
		0, //5, 
		0
	}
#ifdef COMPILE_OUT
	{
		{
			"section ID #2",
			14
		},
		data, /*"written data #2, this should extend past end of old section data" */
		DATASIZE, /*sizeof ("written data #2, this should extend past end of old section data") + 1, */
		0, //3, 
		0
	}
#endif
};

int main (void) {
	SaCkptCheckpointHandleT checkpointHandle;
	SaCkptCheckpointHandleT checkpointHandle2;
	SaCkptCheckpointHandleT checkpointHandleRead;
	SaCkptCheckpointStatusT checkpointStatus;
	SaCkptSectionIteratorT sectionIterator;
	SaCkptSectionDescriptorT sectionDescriptor;
	SaUint32T erroroneousVectorIndex = 0;
	SaErrorT error;
	struct timeval tv_start;
	struct timeval tv_end;
	struct timeval tv_elapsed;
	
	error = saCkptCheckpointOpen (&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);
	printf ("first open result %d (should be 1)\n", error);

	gettimeofday (&tv_start, 0);
	sectionCreationAttributes1.expirationTime = ((unsigned long long)(tv_start.tv_sec + SECONDS_TO_EXPIRE)) * ((unsigned long long)1000000000) + ((unsigned long long)(tv_start.tv_usec) * ((unsigned long long)1000));

	error = saCkptSectionCreate (&checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);

printf ("create1 error is %d\n", error);
printf ("Please wait, testing expiry of checkpoint sections.\n");
	do {
	error = saCkptCheckpointRead (&checkpointHandle,
		ReadVectorElements,
		1,
		&erroroneousVectorIndex);
	} while (error != SA_ERR_NOT_EXIST);
	gettimeofday (&tv_end, NULL);
	timersub (&tv_end, &tv_start, &tv_elapsed);
	printf ("Elapsed Time to expiry is %d.%d (should be about %d seconds)\n", tv_elapsed.tv_sec, tv_elapsed.tv_usec, SECONDS_TO_EXPIRE);

	error = saCkptCheckpointRetentionDurationSet (&checkpointHandle,
		5000000000);
	printf ("RetentionDurationSet is %d\n", error);
	exit (1);

	error = saCkptSectionCreate (&checkpointHandle,
		&sectionCreationAttributes2,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
printf ("create2 error is %d\n", error);
	printf ("saCkptSectionCreate result %d (should be 1)\n", error);
#ifdef cmpout
for (ckptinv = 0; ckptinv < 500000; ckptinv++) {
printf ("Writing checkpoint loop %d\n", ckptinv);
	/*
	 * Test checkpoint write
	 */
	error = saCkptCheckpointWrite (&checkpointHandle,
		WriteVectorElements,
		1,
		&erroroneousVectorIndex);
if (error != SA_OK) {
	printf ("saCkptCheckpointWrite result %d (should be 1)\n", error);
	exit (1);
}
}
exit (1);
#endif

	error = saCkptCheckpointUnlink (&checkpointName);
	printf ("unlink result %d (should be 1)\n", error);

	error = saCkptCheckpointOpen (&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle2);
	printf ("open after unlink result %d (should be 7)\n", error);

	error = saCkptCheckpointClose (&checkpointHandle);
	printf ("close result %d (should be 1)\n", error);

	error = saCkptCheckpointOpen (&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ,
		0,
		&checkpointHandleRead);
	printf ("read only open result %d (should be 1)\n", error);

	error = saCkptCheckpointOpen (&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);
	printf ("open after unlink/close result %d (should be 1)\n", error);

	error = saCkptCheckpointRetentionDurationSet (&checkpointHandle,
		5000000000);
printf ("Retention duration set error is %d\n", error);
	printf ("set checkpoint retention duration result %d (should be 1)\n", error);

	error = saCkptCheckpointStatusGet (&checkpointHandle,
		&checkpointStatus);
	printf ("saCkptCheckpointStatusGet result %d (should be 1)\n", error);
	if (error == SA_OK) {
		printf ("Memory used %d in %d sections.\n", (int)checkpointStatus.memoryUsed,
			(int)checkpointStatus.numberOfSections);
	}
	
	error = saCkptSectionCreate (&checkpointHandleRead,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("saCkptSectionCreate result %d (should be 11)\n", error);

	error = saCkptSectionCreate (&checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("saCkptSectionCreate result %d (should be 1)\n", error);
		
	error = saCkptSectionCreate (&checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("saCkptSectionCreate result %d (should be 14)\n", error);
		
#ifdef COMPILE_OUT
	error = saCkptSectionDelete (&checkpointHandle,
		&sectionId1);
	printf ("saCkptSectionDelete result %d (should be 1)\n", error);
		
	error = saCkptSectionCreate (&checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("saCkptSectionCreate result %d (should be 1)\n", error);
#endif

	error = saCkptSectionExpirationTimeSet (&checkpointHandle,
		&sectionId2,
		SA_TIME_END);
	printf ("saCkptSectionExpirationTimeSet result %d (should be 1)\n", error);
		

	error = saCkptSectionOverwrite (&checkpointHandle,
		&sectionId1,
		"Overwrite Data #1",
		strlen ("Overwrite Data #1") + 1);
	printf ("saCkptSectionOverwrite result %d (should be 1)\n", error);

	/*
	 * Test checkpoint read
	 */
	memset (readBuffer1, 0, sizeof (readBuffer1));
	memset (readBuffer2, 0, sizeof (readBuffer2));
	error = saCkptSectionCreate (&checkpointHandle,
		&sectionCreationAttributes2,
		"Initial Data #2",
		strlen ("Initial Data #2") + 1);
	printf ("saCkptSectionCreate result %d (should be 1)\n", error);

	error = saCkptCheckpointRead (&checkpointHandle,
		ReadVectorElements,
		2,
		&erroroneousVectorIndex);
	printf ("saCkptCheckpointRead result %d (should be 1)\n", error);
	printf ("Buffers after checkpoint read\n");
	printf (" buffer #1: '%s'\n", readBuffer1);
	printf (" buffer #2: '%s'\n", readBuffer2);

//sleep (20);
#ifdef COMPILE_OUT
for (ckptinv = 0; ckptinv < 2000; ckptinv++) {
	/*
	 * Test checkpoint write
	 */
	error = saCkptCheckpointWrite (&checkpointHandle,
		WriteVectorElements,
		2,
		&erroroneousVectorIndex);
if (error != SA_OK) {
printf ("Writing checkpoint loop %d\n", ckptinv);
	printf ("saCkptCheckpointWrite result %d (should be 1)\n", error);
exit (1);
}
}
exit (1);
#endif
	error = saCkptCheckpointRead (&checkpointHandle,
		ReadVectorElements,
		2,
		&erroroneousVectorIndex);
//	printf ("saCkptCheckpointRead result %d (should be 1)\n", error);
//	printf ("Buffers after checkpoint write are:\n");
//	printf (" buffer #1: '%s'\n", readBuffer1);
//	printf (" buffer #2: '%s'\n", readBuffer2);

	error = saCkptCheckpointStatusGet (&checkpointHandle,
		&checkpointStatus);
	printf ("saCkptCheckpointStatusGet result %d (should be 1)\n", error);
	if (error == SA_OK) {
		printf ("Memory used %d in %d sections.\n", (int)checkpointStatus.memoryUsed,
			(int)checkpointStatus.numberOfSections);
	}
	error = saCkptSectionIteratorInitialize (&checkpointHandle,
		0,
		0,
		&sectionIterator);
	printf ("saCkptSectionIteratorInitialize result %d (should be 1)\n", error);

	/*
	 * Iterate all sections
	 */
	do {
		error = saCkptSectionIteratorNext (&sectionIterator,
			&sectionDescriptor);
		printf ("saCkptSectionIteratorNext result %d (should be 1)\n", error);
		if (error == SA_OK) {
			printf ("Section '%s' expires %llx size %d state %x update %llx\n",
				sectionDescriptor.sectionId.id,
				sectionDescriptor.expirationTime,
				sectionDescriptor.sectionSize,
				sectionDescriptor.sectionState,
				sectionDescriptor.lastUpdate);
		}
	} while (error == SA_OK);

	error = saCkptSectionIteratorFinalize (&sectionIterator);
	printf ("saCkptSectionIteratorFinalize result %d (should be 1)\n", error);
	return (0);
}
