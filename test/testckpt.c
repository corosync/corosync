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
#include "saCkpt.h"
#include "sa_error.h"

#define SECONDS_TO_EXPIRE 4

int ckptinv;
void printSaNameT (SaNameT *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

SaVersionT version = { 'B', 1, 1 };

SaNameT checkpointName = { 5, "abra\0" };

SaCkptCheckpointCreationAttributesT checkpointCreationAttributes = {
	SA_CKPT_WR_ALL_REPLICAS,
	100000,
	5000000000LL,
	5,
	20000,
	10
};

SaCkptSectionIdT sectionId1 = {
	14,
	"section ID #1"
};

SaCkptSectionIdT sectionId2 = {
	14,
	"section ID #2"
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
			14,
			"section ID #1"
		},
		readBuffer1,
		sizeof (readBuffer1),
		0, 
		0
	},
	{
		{
			14,
			"section ID #2"
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
			14,
			"section ID #1"
		},
		data, /*"written data #1, this should extend past end of old section data", */
		DATASIZE, /*sizeof ("data #1, this should extend past end of old section data") + 1, */
		0, //5, 
		0
	}
#ifdef COMPILE_OUT
	{
		{
			14
			"section ID #2",
		},
		data, /*"written data #2, this should extend past end of old section data" */
		DATASIZE, /*sizeof ("written data #2, this should extend past end of old section data") + 1, */
		0, //3, 
		0
	}
#endif
};

SaCkptCallbacksT callbacks = {
 	0,
	0
};

int main (void) {
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT checkpointHandle;
	SaCkptCheckpointHandleT checkpointHandle2;
	SaCkptCheckpointHandleT checkpointHandleRead;
	SaCkptCheckpointDescriptorT checkpointStatus;
	SaCkptSectionIterationHandleT sectionIterator;
	SaCkptSectionDescriptorT sectionDescriptor;
	SaUint32T erroroneousVectorIndex = 0;
	SaErrorT error;
	struct timeval tv_start;
	struct timeval tv_end;
	struct timeval tv_elapsed;
	int sel_fd;
	
    error = saCkptInitialize (&ckptHandle, &callbacks, &version);
	
	error = saCkptCheckpointOpen (ckptHandle,
		&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);
	printf ("%s: initial open of checkpoint\n",
		get_test_output (error, SA_AIS_OK));

	
	error = saCkptSectionCreate (checkpointHandle,
        &sectionCreationAttributes1,
        "Initial Data #0",
        strlen ("Initial Data #0") + 1);

    printf ("%s: checkpoint section create\n",
        get_test_output (error, SA_AIS_OK));

	gettimeofday (&tv_start, 0);
	sectionCreationAttributes1.expirationTime = ((unsigned long long)(tv_start.tv_sec + SECONDS_TO_EXPIRE)) * ((unsigned long long)1000000000) + ((unsigned long long)(tv_start.tv_usec) * ((unsigned long long)1000));

	error = saCkptSectionExpirationTimeSet (checkpointHandle,
		&sectionId1,
		sectionCreationAttributes1.expirationTime);
	printf ("%s: checkpoint section expiration set\n",
		get_test_output (error, SA_AIS_OK));

printf ("Please wait, testing expiry of checkpoint sections.\n");
	do {
	error = saCkptCheckpointRead (checkpointHandle,
		ReadVectorElements,
		1,
		&erroroneousVectorIndex);
	} while (error != SA_ERR_NOT_EXIST);
	gettimeofday (&tv_end, NULL);
	timersub (&tv_end, &tv_start, &tv_elapsed);
	printf ("Elapsed Time to expiry is %ld.%ld (should be about %d seconds)\n", tv_elapsed.tv_sec, tv_elapsed.tv_usec, SECONDS_TO_EXPIRE);

	error = saCkptCheckpointRetentionDurationSet (checkpointHandle,
						      5000000000LL);
	printf ("%s: RetentionDurationSet\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes2,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);

	printf ("%s: Section creation\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointUnlink (ckptHandle, &checkpointName);
	printf ("%s: Unlinking checkpoint\n", 
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointOpen (ckptHandle,
		&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle2);
	printf ("%s: Opening unlinked checkpoint\n", 
		get_test_output (error, 7));

	error = saCkptCheckpointClose (checkpointHandle);
	printf ("%s: Closing checkpoint\n", 
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointOpen (ckptHandle,
		&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ,
		0,
		&checkpointHandleRead);

	printf ("%s: Open checkpoint read only\n",
		get_test_output (error, SA_AIS_OK));


	error = saCkptCheckpointOpen (ckptHandle,
		&checkpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);
	printf ("%s: open after unlink/close\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointRetentionDurationSet (checkpointHandle,
						      5000000000LL);
	printf ("%s: set checkpoint retention duration\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointStatusGet (checkpointHandle,
		&checkpointStatus);
	printf ("%s: Get checkpoint status\n",
		get_test_output (error, SA_AIS_OK));
	if (error == SA_OK) {
		printf ("Memory used %d in %d sections.\n", (int)checkpointStatus.memoryUsed,
			(int)checkpointStatus.numberOfSections);
	}
	
	error = saCkptSectionCreate (checkpointHandleRead,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("%s: Create checkpoint section on read only checkpoint\n",
		get_test_output (error, SA_AIS_ERR_ACCESS));

	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("%s: Create checkpoint section on writeable checkpoint\n",
		get_test_output (error, SA_AIS_OK));
		
	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("%s: Create checkpoint section when one already exists\n",
		get_test_output (error, 14));
		
	error = saCkptSectionDelete (checkpointHandle,
		&sectionId1);
	printf ("%s: deleting checkpoint handle\n",
		get_test_output (error, SA_AIS_OK));
		
	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("%s: replacing deleted checkpoint section\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptSectionCreate (checkpointHandle,
									&sectionCreationAttributes2,
									"Initial Data #2",
									strlen ("Initial Data #2") + 1);
	printf ("%s: creating section 2 \n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptSectionExpirationTimeSet (checkpointHandle,
		&sectionId2,
		SA_TIME_END);
	printf ("%s: Setting expiration time for section 2\n",
		get_test_output (error, SA_AIS_OK));
		
	error = saCkptSectionOverwrite (checkpointHandle,
		&sectionId1,
		"Overwrite Data #1",
		strlen ("Overwrite Data #1") + 1);
	printf ("%s: overwriting checkpoint section 1\n",
		get_test_output (error, SA_AIS_OK));

	/*
	 * Test checkpoint read
	 */
	memset (readBuffer1, 0, sizeof (readBuffer1));
	memset (readBuffer2, 0, sizeof (readBuffer2));

	error = saCkptCheckpointRead (checkpointHandle,
		ReadVectorElements,
		2,
		&erroroneousVectorIndex);
	printf ("%s: checkpoint read operation",
		get_test_output (error, SA_AIS_OK));
	printf ("Buffers after checkpoint read\n");
	printf (" buffer #1: '%s'\n", readBuffer1);
	printf (" buffer #2: '%s'\n", readBuffer2);

	for (ckptinv = 0; ckptinv < 10; ckptinv++) {
	/*
	 * Test checkpoint write
	 */
		error = saCkptCheckpointWrite (checkpointHandle,
			WriteVectorElements,
			2,
			&erroroneousVectorIndex);
		if (error != SA_OK) {
			printf ("Writing checkpoint loop %d\n", ckptinv);
				printf ("saCkptCheckpointWrite result %d (should be 1)\n", error);
		}
	}
	printf ("%s: Testing checkpoint writes\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointRead (checkpointHandle,
		ReadVectorElements,
		2,
		&erroroneousVectorIndex);
//	printf ("saCkptCheckpointRead result %d (should be 1)\n", error);
//	printf ("Buffers after checkpoint write are:\n");
//	printf (" buffer #1: '%s'\n", readBuffer1);
//	printf (" buffer #2: '%s'\n", readBuffer2);

	error = saCkptCheckpointStatusGet (checkpointHandle,
		&checkpointStatus);
	printf ("%s: get checkpoint status\n",
		get_test_output (error, SA_AIS_OK));
	if (error == SA_OK) {
		printf ("Memory used %d in %d sections.\n",
			(int)checkpointStatus.memoryUsed,
			(int)checkpointStatus.numberOfSections);
	}
	error = saCkptSectionIterationInitialize (checkpointHandle,
		0,
		0,
		&sectionIterator);
	printf ("%s: initialize section iterator\n",
		get_test_output (error, SA_AIS_OK));

	/*
	 * Iterate all sections
	 */
	do {
		error = saCkptSectionIterationNext (sectionIterator,
			&sectionDescriptor);
		printf ("%s: Get next section in iteartion\n",
			get_test_output (error, SA_AIS_OK));
		if (error == SA_OK) {
			printf ("Section '%s' expires %llx size %d state %x update %llx\n",
				sectionDescriptor.sectionId.id,
				(unsigned long long)sectionDescriptor.expirationTime,
				sectionDescriptor.sectionSize,
				sectionDescriptor.sectionState,
				(unsigned long long)sectionDescriptor.lastUpdate);
		}
	} while (error == SA_OK);
	printf ("The last iteration should fail\n");

	error = saCkptSectionIterationFinalize (sectionIterator);
	printf ("%s: Finalize iteration\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptSelectionObjectGet (ckptHandle, &sel_fd);
	printf ("%s: Retrieve selection object %d\n",
		get_test_output (error, SA_AIS_OK), sel_fd);

	error = saCkptFinalize (ckptHandle);
	printf ("%s: Finalize checkpoint\n",
		get_test_output (error, SA_AIS_OK));

	return (0);
}
