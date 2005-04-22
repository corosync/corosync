/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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

#ifndef SACKPT_H_DEFINED
#define SACKPT_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif


typedef SaUint64T SaCkptHandleT;

typedef SaUint64T SaCkptCheckpointHandleT;

typedef SaUint64T SaCkptSectionIterationHandleT;

#define SA_CKPT_WR_ALL_REPLICAS	0x01
#define SA_CKPT_WR_ACTIVE_REPLICA	0x2
#define SA_CKPT_WR_ACTIVE_REPLICA_WEAK	0x4
#define SA_CKPT_CHECKPOINT_COLLOCATED	0x8

typedef SaUint32T SaCkptCheckpointCreationFlagsT;

typedef struct {
	SaCkptCheckpointCreationFlagsT creationFlags;
	SaSizeT checkpointSize;
	SaTimeT retentionDuration;
	SaUint32T maxSections;
	SaSizeT maxSectionSize;
	SaSizeT maxSectionIdSize;
} SaCkptCheckpointCreationAttributesT;

#define SA_CKPT_CHECKPOINT_READ		0x1
#define SA_CKPT_CHECKPOINT_WRITE	0x2
#define SA_CKPT_CHECKPOINT_CREATE	0x4

typedef SaUint32T SaCkptCheckpointOpenFlagsT;

#define SA_CKPT_DEFAULT_SECTION_ID { 0, 0 }
#define SA_CKPT_GENERATED_SECTION_ID { 0, 0 }

typedef struct {
	SaUint16T idLen;
	SaUint8T *id;
} SaCkptSectionIdT;

typedef struct {
	SaCkptSectionIdT *sectionId;
	SaTimeT expirationTime;
} SaCkptSectionCreationAttributesT;

typedef enum {
	SA_CKPT_SECTION_VALID = 1,
	SA_CKPT_SECTION_CORRUPTED = 2
} SaCkptSectionStateT;

typedef struct {
	SaCkptSectionIdT sectionId;
	SaTimeT expirationTime;
	SaSizeT sectionSize;
	SaCkptSectionStateT sectionState;
	SaTimeT lastUpdate;
} SaCkptSectionDescriptorT;

typedef enum {
	SA_CKPT_SECTIONS_FOREVER = 1,
	SA_CKPT_SECTIONS_LEQ_EXPIRATION_TIME = 2,
	SA_CKPT_SECTIONS_GEQ_EXPIRATION_TIME = 3,
	SA_CKPT_SECTIONS_CORRUPTED = 4,
	SA_CKPT_SECTIONS_ANY = 5
} SaCkptSectionsChosenT;

typedef SaUint64T SaOffsetT;

typedef struct {
	SaCkptSectionIdT sectionId;
	void *dataBuffer;
	SaSizeT dataSize;
	SaOffsetT dataOffset;
	SaSizeT readSize;
} SaCkptIOVectorElementT;

typedef struct {
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaUint32T numberOfSections;
	SaUint32T memoryUsed;
} SaCkptCheckpointDescriptorT;

typedef void (*SaCkptCheckpointOpenCallbackT) (
	SaInvocationT invocation,	
	const SaCkptCheckpointHandleT checkpointHandle,
	SaAisErrorT error);

typedef void (*SaCkptCheckpointSynchronizeCallbackT) (
	SaInvocationT invocation,	
	SaAisErrorT error);

typedef struct {
	SaCkptCheckpointOpenCallbackT saCkptCheckpointOpenCallback;
	SaCkptCheckpointSynchronizeCallbackT saCkptCheckpointSynchronizeCallback;
} SaCkptCallbacksT;


SaAisErrorT
saCkptInitialize (
	SaCkptHandleT *ckptHandle,
	const SaCkptCallbacksT *callbacks,
	SaVersionT *version);

SaAisErrorT
saCkptSelectionObjectGet (
	SaCkptHandleT ckptHandle,
	SaSelectionObjectT *selectionObject);

SaAisErrorT
saCkptDispatch (
	SaCkptHandleT ckptHandle,
	SaDispatchFlagsT dispatchFlags);

SaAisErrorT
saCkptFinalize (
	SaCkptHandleT ckptHandle);

SaAisErrorT
saCkptCheckpointOpen (
	SaCkptHandleT ckptHandle,
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags,
	SaTimeT timeout,
	SaCkptCheckpointHandleT *checkpointHandle);

SaAisErrorT
saCkptCheckpointOpenAsync (
	SaCkptHandleT ckptHandle,
	SaInvocationT invocation,	
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags);

SaAisErrorT
saCkptCheckpointClose (
	SaCkptCheckpointHandleT checkpointHandle);

SaAisErrorT
saCkptCheckpointUnlink (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaNameT *checkpointName);

SaAisErrorT
saCkptCheckpointRetentionDurationSet (
	SaCkptCheckpointHandleT checkpointHandle,
	SaTimeT retentionDuration);

SaAisErrorT
saCkptActiveReplicaSet (
	const SaCkptCheckpointHandleT checkpointHandle);

SaAisErrorT
saCkptCheckpointStatusGet (
	SaCkptCheckpointHandleT checkpointHandle,
	SaCkptCheckpointDescriptorT *checkpointStatus);

SaAisErrorT
saCkptSectionCreate (
	SaCkptCheckpointHandleT checkpointHandle,
	SaCkptSectionCreationAttributesT *sectionCreationAttributes,
	const void *initialData,
	SaUint32T initialDataSize);


SaAisErrorT
saCkptSectionDelete (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptSectionIdT *sectionId);

SaAisErrorT
saCkptSectionExpirationTimeSet (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptSectionIdT *sectionId,
	SaTimeT expirationTime);

SaAisErrorT
saCkptSectionIterationInitialize (
	const SaCkptCheckpointHandleT checkpointHandle,
	SaCkptSectionsChosenT sectionsChosen,
	SaTimeT expirationTime,
	SaCkptSectionIterationHandleT *sectionIterationHandle);

SaAisErrorT
saCkptSectionIterationNext (
	SaCkptSectionIterationHandleT sectionIterationHandle,
	SaCkptSectionDescriptorT *sectionDescriptor);

SaAisErrorT
saCkptSectionIterationFinalize (
	SaCkptSectionIterationHandleT sectionIterationHandle);

SaAisErrorT
saCkptCheckpointWrite (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex);

SaAisErrorT
saCkptSectionOverwrite (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptSectionIdT *secitonId,
	const void *dataBuffer,
	SaSizeT dataSize);

SaAisErrorT
saCkptCheckpointRead (
	SaCkptCheckpointHandleT checkpointHandle,
	SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex);

SaAisErrorT
saCkptCheckpointSynchronize (
	SaCkptCheckpointHandleT checkpointHandle,
	SaTimeT timeout);

SaAisErrorT
saCkptCheckpointSynchronizeAsync (
	SaCkptHandleT ckptHandle,
	SaCkptCheckpointHandleT checkpointHandle,
	SaInvocationT invocation);

#ifdef __cplusplus
}
#endif

#endif /* SACKPT_H_DEFINED */
