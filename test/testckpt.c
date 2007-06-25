#define _BSD_SOURCE
/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@sdake.com)
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <sys/time.h>

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

#define SECONDS_TO_EXPIRE 5

int ckptinv;
SaInvocationT open_invocation = 16;
void printSaNameT (SaNameT *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

SaVersionT version = { 'B', 1, 1 };

SaNameT defaultCheckpointName = { 8, "defaults" };

SaNameT sectionsCheckpointName = { 8, "sections" };

SaCkptCheckpointCreationAttributesT checkpointCreationAttributes = {
	.creationFlags = SA_CKPT_WR_ALL_REPLICAS,
	.checkpointSize = 100000,
	.retentionDuration = 5000000000LL,
	.maxSections = 1,
	.maxSectionSize = 200000,
	.maxSectionIdSize = 20
};

SaCkptSectionIdT sectionId1 = {
	13,
	(SaUint8T *) "section ID #1"
};

SaCkptSectionIdT sectionId2 = {
	13,
	(SaUint8T *) "section ID #2"
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

char default_read_buffer[1025];

SaCkptIOVectorElementT ReadVectorElements[] = {
	{
		{
			13,
			(SaUint8T *) "section ID #1"
		},
		readBuffer1,
		sizeof (readBuffer1),
		0, 
		0
	},
	{
		{
			13,
			(SaUint8T *) "section ID #2"
		},
		readBuffer2,
		sizeof (readBuffer2),
		0, 
		0
	}
};

SaCkptIOVectorElementT default_read_vector[] = {
	{
		SA_CKPT_DEFAULT_SECTION_ID,
        default_read_buffer, /*"written data #1, this should extend past end of old section data", */
        sizeof(default_read_buffer), /*sizeof ("data #1, this should extend past end of old section data") + 1, */
        0, //5,
        0
    }
};


#define DATASIZE 127000
char data1[DATASIZE];
char data2[DATASIZE];
char default_write_data[56];
SaCkptIOVectorElementT WriteVectorElements[] = {
	{
		{
			13,
			(SaUint8T *) "section ID #1"
		},
		data1, /*"written data #1, this should extend past end of old section data", */
		DATASIZE, /*sizeof ("data #1, this should extend past end of old section data") + 1, */
		0, //5, 
		0
	},
	{
		{
			13,
			(SaUint8T *) "section ID #2",
		},
		data2, /*"written data #2, this should extend past end of old section data" */
		DATASIZE, /*sizeof ("written data #2, this should extend past end of old section data") + 1, */
		0, //3, 
		0
	}
};

SaCkptIOVectorElementT default_write_vector[] = {
	{
		SA_CKPT_DEFAULT_SECTION_ID,
		default_write_data, /*"written data #1, this should extend past end of old section data", */	
		56, /*sizeof ("data #1, this should extend past end of old section data") + 1, */
		0, //5,
		0
	}
};

SaCkptCheckpointHandleT checkpointHandle;

void OpenCallBack (
    SaInvocationT invocation,
    const SaCkptCheckpointHandleT chckpointHandle,
    SaAisErrorT error) {
	
 	printf ("%s: This is a call back for open for invocation = %d\n",
		get_test_output (error, SA_AIS_OK), (int)invocation);	

	checkpointHandle = chckpointHandle;

}

SaCkptCallbacksT callbacks = {
 	&OpenCallBack,
	0
};

int main (void) {
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT checkpointHandle2;
	SaCkptCheckpointHandleT checkpointHandleRead;
	SaCkptCheckpointDescriptorT checkpointStatus;
	SaCkptSectionIterationHandleT sectionIterator;
	SaCkptSectionDescriptorT sectionDescriptor;
	SaUint32T erroroneousVectorIndex = 0;
	SaAisErrorT error;
	struct timeval tv_start;
	struct timeval tv_end;
	struct timeval tv_elapsed;
	SaSelectionObjectT sel_fd;
	fd_set read_set;
	int i;
	
	error = saCkptInitialize (&ckptHandle, &callbacks, &version);
	printf ("%s: checkpoint initialize\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointOpenAsync (ckptHandle,
		open_invocation,
		&defaultCheckpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE);
	printf ("%s: initial asynchronous open of checkpoint\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptSelectionObjectGet (ckptHandle, &sel_fd);

	printf ("%s: Retrieve selection object %llu\n",
	get_test_output (error, SA_AIS_OK), (unsigned long long)sel_fd);

	FD_SET (sel_fd, &read_set);
	select (sel_fd + 1, &read_set, 0, 0, 0);

	error = saCkptDispatch (ckptHandle, SA_DISPATCH_ALL);
	
	printf ("%s: Dispatch response for open async of checkpoint\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointClose (checkpointHandle);

	printf ("%s: Closing checkpoint\n", get_test_output (error, SA_AIS_OK));
	
	error = saCkptCheckpointOpen (ckptHandle,
		&defaultCheckpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);
	printf ("%s: initial open of checkpoint\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointRead (checkpointHandle,
		default_read_vector,
		1,
		&erroroneousVectorIndex);
	printf ("%s: Reading default checkpoint section before update\n",
		get_test_output (error, SA_AIS_OK));
	printf (" default_read_buffer:'%s'\n", default_read_buffer);

	memset (default_read_buffer, 0, sizeof (default_read_buffer));
	memcpy(default_write_data,
		"This is an update to the default section date, update#1", 56);
	error = saCkptCheckpointWrite (checkpointHandle,
		default_write_vector,
		1,
		&erroroneousVectorIndex);

	printf ("%s: Writing default checkpoint section with data '%s' \n",
		get_test_output (error, SA_AIS_OK), default_write_data);

	error = saCkptCheckpointRead (checkpointHandle,
		default_read_vector,
		1,
		&erroroneousVectorIndex);

	printf ("%s: Reading default checkpoint section \n",
		get_test_output (error, SA_AIS_OK));

	printf (" default_read_buffer:'%s'\n", default_read_buffer);
	
	error = saCkptCheckpointClose (checkpointHandle);

	checkpointCreationAttributes.maxSections = 5;
	error = saCkptCheckpointOpen (ckptHandle,
		&sectionsCheckpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle);

	printf ("%s: checkpoint create writeable\n",
		get_test_output (error, SA_AIS_OK));

	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);

	printf ("%s: checkpoint section create\n",
		get_test_output (error, SA_AIS_OK));

	gettimeofday (&tv_start, 0);
	sectionCreationAttributes1.expirationTime =
		 (((unsigned long long)(tv_start.tv_sec) + SECONDS_TO_EXPIRE) *
			1000000000ULL) +
		((unsigned long long)(tv_start.tv_usec) * 1000ULL);

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
sleep (1);
	} while (error != SA_AIS_ERR_NOT_EXIST);
	gettimeofday (&tv_end, NULL);

	/*
	 * avoid div by zero errors
	 */
	if (tv_elapsed.tv_usec == 0) {
		tv_elapsed.tv_usec = 1;
	}
	timersub (&tv_end, &tv_start, &tv_elapsed);
	printf ("Elapsed Time to expiry is %ld & %ld usec (should be about %d seconds)\n",
		tv_elapsed.tv_sec,
		tv_elapsed.tv_usec,
		SECONDS_TO_EXPIRE);

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

	error = saCkptCheckpointUnlink (ckptHandle, &sectionsCheckpointName);
	printf ("%s: Unlinking checkpoint\n", 
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointOpen (ckptHandle,
		&sectionsCheckpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
		0,
		&checkpointHandle2);
	printf ("%s: Opening unlinked checkpoint\n", 
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointClose (checkpointHandle);
	printf ("%s: Closing checkpoint\n", 
		get_test_output (error, SA_AIS_OK));

	error = saCkptCheckpointOpen (ckptHandle,
		&sectionsCheckpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ,
		0,
		&checkpointHandleRead);

	printf ("%s: Open checkpoint read only\n",
		get_test_output (error, SA_AIS_OK));


	error = saCkptCheckpointOpen (ckptHandle,
		&sectionsCheckpointName,
		&checkpointCreationAttributes,
		SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
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
	if (error == SA_AIS_OK) {
		printf ("Memory used %d in %d sections.\n", (int)checkpointStatus.memoryUsed,
			(int)checkpointStatus.numberOfSections);
	}
	
	error = saCkptSectionCreate (checkpointHandleRead,
		&sectionCreationAttributes1,
		"Initial Data #0",
		strlen ("Initial Data #0") + 1);
	printf ("%s: Create checkpoint section on read only checkpoint\n",
		get_test_output (error, SA_AIS_ERR_ACCESS));

	sectionCreationAttributes1.expirationTime = SA_TIME_END;

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
		get_test_output (error, SA_AIS_ERR_EXIST));
		
	error = saCkptSectionDelete (checkpointHandle,
		&sectionId1);
	printf ("%s: deleting section handle\n",
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
	printf ("%s: creating section 2 for first time\n",
		get_test_output (error, SA_AIS_OK));
	error = saCkptSectionCreate (checkpointHandle,
		&sectionCreationAttributes2,
		"Initial Data #2",
		strlen ("Initial Data #2") + 1);
	printf ("%s: creating section 2 for second time\n",
		get_test_output (error, SA_AIS_ERR_EXIST));

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
	printf ("%s: checkpoint read operation\n",
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
		if (error != SA_AIS_OK) {
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
	if (error == SA_AIS_OK) {
		printf ("Memory used %d in %d sections.\n",
			(int)checkpointStatus.memoryUsed,
			(int)checkpointStatus.numberOfSections);
	}
	printf ("iterating all sections 5 times\n");
	/*
	 * iterate all sections 5 times
	 */
	for (i = 0; i < 5; i++) {
	error = saCkptSectionIterationInitialize (checkpointHandle,
		SA_CKPT_SECTIONS_ANY,
		0,
		&sectionIterator);
	printf ("%s: initialize section iterator\n",
		get_test_output (error, SA_AIS_OK));


	/*
	 * Iterate all sections
	 */
	do {
		printf ("Starting iteration\n");
		error = saCkptSectionIterationNext (sectionIterator,
			&sectionDescriptor);
		if (error == SA_AIS_ERR_NO_SECTIONS) {
			printf ("No more sections to iterate\n");
		} else {
			printf ("%s: Get next section in iteration\n",
				get_test_output (error, SA_AIS_OK));
		}
		if (error == SA_AIS_OK) {
			printf ("Section '%s' expires %llx size %llu state %x update %llx\n",
				sectionDescriptor.sectionId.id,
				(unsigned long long)sectionDescriptor.expirationTime,
				(unsigned long long)sectionDescriptor.sectionSize,
				sectionDescriptor.sectionState,
				(unsigned long long)sectionDescriptor.lastUpdate);
		}
	} while (error == SA_AIS_OK);

	error = saCkptSectionIterationFinalize (sectionIterator);
	printf ("%s: Finalize iteration\n",
		get_test_output (error, SA_AIS_OK));

	}

	error = saCkptSelectionObjectGet (ckptHandle, &sel_fd);

	error = saCkptFinalize (ckptHandle);
	printf ("%s: Finalize checkpoint\n",
		get_test_output (error, SA_AIS_OK));

	return (0);
}
