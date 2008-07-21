#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <vector.h>
#include <iostream>


#include "saAis.h"
#include "saCkpt.h"


SaVersionT version = { 'B', 1, 1 };

SaCkptCallbacksT callbacks = {
    0,
    0
};

SaCkptCheckpointCreationAttributesT checkpointCreationAttributes = {
        SA_CKPT_WR_ALL_REPLICAS,
        100000,
        5000000000LL,
        5,
        20000,
        10
};

SaCkptSectionIdT sectionId = {
        14,
        (SaUint8T*)"section ID #1"
};

SaCkptSectionCreationAttributesT sectionCreationAttributes = {
        &sectionId,
        SA_TIME_END
};

char* getPayload(int psize) {
        int i;
        char* retVal = new char[psize];
        if (retVal == NULL)
        {
                return NULL;
        }
        for (i = 0; i < psize; i++)
        {
                if (i == (psize - 1)) {
                        *retVal = '\0';
                        retVal++;
                        continue;
                }
                *retVal = 'z';
                retVal++;
        }
        retVal = retVal - psize;
        return retVal;
}

SaCkptCheckpointHandleT WriteCheckpointHandle;

static long sendCount = 0;
void process_message()
{
        struct timeval tv;
        long t1;
        long t2;
	long told;
        SaCkptIOVectorElementT writeElement; // KJS

        SaUint32T erroroneousVectorIndex = 0;
        SaAisErrorT error;
        
        writeElement.sectionId = sectionId;
        writeElement.dataBuffer = getPayload(200); 
        writeElement.dataSize = 200;
        writeElement.dataOffset = 0;
        writeElement.readSize = 0;

        gettimeofday(&tv, NULL);
        t1 = tv.tv_usec;
	told = tv.tv_sec;
        
        do {
                error = saCkptCheckpointWrite (WriteCheckpointHandle,
                                               &writeElement,
                                               1,
                                               &erroroneousVectorIndex);

                if (error != SA_AIS_OK) {
                        fprintf(stderr,"saCkptCheckpointWrite result %d (should be 1)\n", error);
                }
                sendCount++;
                fprintf(stderr,"sendCount = %d",(int)sendCount);
        } while (error == SA_AIS_ERR_TRY_AGAIN);

        gettimeofday(&tv, NULL);
        t2 = tv.tv_usec;        
        fprintf(stderr," ,RTT::%d.%d\n",(long)tv.tv_sec - told, t2-t1);
}

int main () {
	SaAisErrorT error;
	SaNameT* WriteCheckpointName = (SaNameT*) malloc(sizeof(SaNameT));
        char name[10];
        SaCkptHandleT ckptHandle;

        sprintf(name,"ckpt%d",1);
        int namelen = strlen(name) + 1;
        memcpy(WriteCheckpointName->value, name, namelen);
        WriteCheckpointName->length = namelen;

	error = saCkptInitialize (&ckptHandle, &callbacks, &version);

        error = saCkptCheckpointOpen (
			ckptHandle,
			WriteCheckpointName,
                        &checkpointCreationAttributes,
                        SA_CKPT_CHECKPOINT_WRITE,
                        1000000000, /* 1 Second */
                        &WriteCheckpointHandle);

        if (error != SA_AIS_OK) {
                fprintf(stderr,"saCkptCheckpointOpen result %d (should be 1)\n", error);
                return error;
        }

        error = saCkptSectionCreate (	WriteCheckpointHandle,
                                        &sectionCreationAttributes,
                                        "Initial Data #0",
                                         strlen ("Initial Data #0") + 1);
        if (error != SA_AIS_OK) {
                fprintf(stderr,"saCkptSectionCreate result = %d\n", error);
                return error;
        }
        
	struct timespec tv;
	tv.tv_sec = 0;
	tv.tv_nsec = 15000000; //15 milliseconds
	
	while(1) {
		process_message();
		nanosleep(&tv,NULL);
	}
	
	return 1;

}
