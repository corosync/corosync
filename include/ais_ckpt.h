
/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
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

#include "ais_types.h"

#ifndef AIS_CKPT_H_DEFINED
#define AIS_CKPT_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

SaErrorT
saCkptInitialize (
	SaCkptHandleT *ckptHandle,
	const SaCkptCallbacksT *callbacks,
	const SaVersionT *version);

SaErrorT
saCkptSelectionObjectGet (
	const SaCkptHandleT *ckptHandle,
	SaSelectionObjectT *selectionObject);

SaErrorT
saCkptDispatch (
	const SaCkptHandleT *ckptHandle,
	SaDispatchFlagsT dispatchFlags);

SaErrorT
saCkptFinalize (
	const SaCkptHandleT *ckptHandle);

SaErrorT
saCkptCheckpointOpen (
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags,
	SaTimeT timeout,
	SaCkptCheckpointHandleT *checkpointHandle);

SaErrorT
saCkptCheckpointOpenAsync (
	const SaCkptHandleT *ckptHandle,
	SaInvocationT invocation,	
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags);

SaErrorT
saCkptCheckpointClose (
	const SaCkptCheckpointHandleT *checkpointHandle);

SaErrorT
saCkptCheckpointUnlink (
	const SaNameT *checkpointName);

SaErrorT
saCkptCheckpointRetentionDurationSet (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaTimeT retentionDuration);

SaErrorT
saCkptActiveCheckpointSet (
	const SaCkptCheckpointHandleT *checkpointHandle);

SaErrorT
saCkptCheckpointStatusGet (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptCheckpointStatusT *checkpointStatus);

SaErrorT
saCkptSectionCreate (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptSectionCreationAttributesT *sectionCreationAttributes,
	const void *initialData,
	SaUint32T initialDataSize);


SaErrorT
saCkptSectionDelete (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptSectionIdT *sectionId);

SaErrorT
saCkptSectionExpirationTimeSet (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptSectionIdT *sectionId,
	SaTimeT expirationTime);

SaErrorT
saCkptSectionIteratorInitialize (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptSectionsChosenT sectionsChosen,
	SaTimeT expirationTime,
	SaCkptSectionIteratorT *sectionIterator);

SaErrorT
saCkptSectionIteratorNext (
	SaCkptSectionIteratorT *sectionIterator,
	SaCkptSectionDescriptorT *sectionDescriptor);

SaErrorT
saCkptSectionIteratorFinalize (
	SaCkptSectionIteratorT *sectionIterator);

SaErrorT
saCkptCheckpointWrite (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex);

SaErrorT
saCkptSectionOverwrite (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptSectionIdT *secitonId,
	SaUint8T *dataBuffer,
	SaSizeT dataSize);

SaErrorT
saCkptCheckpointRead (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex);

SaErrorT
saCkptCheckpointSynchronize (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaTimeT timeout);

SaErrorT
saCkptCheckpointSynchronizeAsync (
	const SaCkptHandleT *ckptHandle,
	SaInvocationT invocation,
	const SaCkptCheckpointHandleT *checkpointHandle);

#ifdef __cplusplus
}
#endif

#endif /* AIS_CKPT_H_DEFINED */
