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
#ifndef IPC_CKPT_H_DEFINED
#define IPC_CKPT_H_DEFINED

#include "../include/ipc_gen.h"
#include "../include/ais_types.h"
#include "../include/saCkpt.h"
#include "../exec/totemsrp.h"
#include "../exec/ckpt.h"

enum req_lib_ckpt_checkpoint_types {
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPEN = 0,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC = 1,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTCLOSE = 2,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTUNLINK = 3,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET = 4,
	MESSAGE_REQ_CKPT_ACTIVEREPLICASET = 5,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET = 6,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONCREATE = 7,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONDELETE = 8,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET = 9,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONWRITE = 10,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONOVERWRITE = 11,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONREAD = 12,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE = 13,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC = 14,
	MESSAGE_REQ_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE = 15,
	MESSAGE_REQ_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT = 16
};

enum res_lib_ckpt_checkpoint_types {
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN = 0,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC = 1,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE = 2,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK = 3,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET = 4,
	MESSAGE_RES_CKPT_ACTIVEREPLICASET = 5,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET = 6,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE = 7,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE = 8,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET = 9,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE = 10,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE = 11,
	MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD = 12,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE = 13,
	MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC = 14,
	MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE = 15,
	MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT = 16
};

struct req_exec_ckpt_checkpointclose {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
};

struct req_exec_ckpt_checkpointretentiondurationset {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	SaTimeT retentionDuration;
};

struct req_exec_ckpt_checkpointretentiondurationexpire {
	struct req_header header;
	SaNameT checkpointName;
};

struct req_lib_ckpt_checkpointopen {
	struct req_header header;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags;
};

struct res_lib_ckpt_checkpointopen {
	struct res_header header;
};

struct req_exec_ckpt_checkpointopen {
	struct req_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointopen req_lib_ckpt_checkpointopen;
	SaCkptCheckpointHandleT checkpointHandle;
	SaInvocationT invocation;
};


struct req_lib_ckpt_checkpointopenasync {
	struct req_header header;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags;
	SaCkptCheckpointHandleT checkpointHandle;
	SaInvocationT invocation;
};

struct res_lib_ckpt_checkpointopenasync {
	struct res_header header;
	SaCkptCheckpointHandleT checkpointHandle;
	SaInvocationT invocation;
};

struct req_lib_ckpt_checkpointclose {
	struct req_header header;
	SaNameT checkpointName;
};

struct res_lib_ckpt_checkpointclose {
	struct res_header header;
};

struct req_lib_ckpt_checkpointunlink {
	struct req_header header;
	SaNameT checkpointName;
};

struct res_lib_ckpt_checkpointunlink {
	struct res_header header;
};

struct req_exec_ckpt_checkpointunlink {
	struct req_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointunlink req_lib_ckpt_checkpointunlink;
};

struct req_lib_ckpt_checkpointretentiondurationset {
	struct req_header header;
	SaNameT checkpointName;
	SaTimeT retentionDuration;
};
struct res_lib_ckpt_checkpointretentiondurationset {
	struct res_header header;
};

struct req_lib_ckpt_activereplicaset {
	struct req_header header;
	SaNameT checkpointName;
};

struct res_lib_ckpt_activereplicaset {
	struct res_header header;
};

struct req_lib_ckpt_checkpointstatusget {
	struct req_header header;
	SaNameT checkpointName;
};

struct res_lib_ckpt_checkpointstatusget {
	struct res_header header;
	SaCkptCheckpointDescriptorT checkpointDescriptor;
};

struct req_lib_ckpt_sectioncreate {
	struct req_header header;
	SaNameT checkpointName;
	SaUint32T idLen;
	SaTimeT expirationTime;
	SaUint32T initialDataSize;
};

struct res_lib_ckpt_sectioncreate {
	struct res_header header;
};

struct req_exec_ckpt_sectioncreate {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectioncreate req_lib_ckpt_sectioncreate; /* this must be last */
};

struct req_lib_ckpt_sectiondelete {
	struct req_header header;
	SaNameT checkpointName;
	SaUint32T idLen;
};

struct res_lib_ckpt_sectiondelete {
	struct res_header header;
};
struct req_exec_ckpt_sectiondelete {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectiondelete req_lib_ckpt_sectiondelete; /* this must be last */
};

struct req_lib_ckpt_sectionexpirationtimeset {
	struct req_header header;
	SaNameT checkpointName;
	SaUint32T idLen;
	SaTimeT expirationTime;
};

struct res_lib_ckpt_sectionexpirationtimeset {
	struct res_header header;
};

struct req_exec_ckpt_sectionexpirationtimeset {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionexpirationtimeset req_lib_ckpt_sectionexpirationtimeset;
};

struct req_lib_ckpt_sectioniteratorinitialize {
	struct req_header header;
	SaNameT checkpointName;
	SaCkptSectionsChosenT sectionsChosen;
	SaTimeT expirationTime;
};

struct res_lib_ckpt_sectioniteratorinitialize {
	struct res_header header;
};

struct req_lib_ckpt_sectioniteratornext {
	struct req_header header;
};

struct res_lib_ckpt_sectioniteratornext {
	struct res_header header;
	SaCkptSectionDescriptorT sectionDescriptor;
};

struct req_lib_ckpt_sectionwrite {
	struct req_header header;
	SaNameT checkpointName;
	SaUint32T idLen;
	SaOffsetT dataOffset;
	SaOffsetT dataSize;
};

struct res_lib_ckpt_sectionwrite {
	struct res_header header;
};

struct req_exec_ckpt_sectionwrite {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionwrite req_lib_ckpt_sectionwrite;
};

struct req_lib_ckpt_sectionoverwrite {
	struct req_header header;
	SaNameT checkpointName;
	SaUint32T idLen;
	SaUint32T dataSize;
};

struct res_lib_ckpt_sectionoverwrite {
	struct res_header header;
};
	
struct req_exec_ckpt_sectionoverwrite {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionoverwrite req_lib_ckpt_sectionoverwrite;
};

struct req_lib_ckpt_sectionread {
	struct req_header header;
	SaNameT checkpointName;
	SaUint32T idLen;
	SaOffsetT dataOffset;
	SaOffsetT dataSize;
};

struct res_lib_ckpt_sectionread {
	struct res_header header;
	SaSizeT dataRead;
};

struct req_exec_ckpt_sectionread {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionread req_lib_ckpt_sectionread;
};

struct req_lib_ckpt_checkpointsynchronize {
	struct req_header header;
};

struct res_lib_ckpt_checkpointsynchronize {
	struct res_header header;
};

struct req_lib_ckpt_checkpointsynchronizeasync {
	struct req_header header;
	SaInvocationT invocation;
};

struct res_lib_ckpt_checkpointsynchronizeasync {
	struct res_header header;
};

struct req_exec_ckpt_synchronize_state {
	struct req_header header;
	struct memb_ring_id previous_ring_id;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptSectionDescriptorT sectionDescriptor;	
	struct in_addr source_addr;
	struct ckpt_refcnt ckpt_refcount[PROCESSOR_COUNT_MAX];
};

struct req_exec_ckpt_synchronize_section {
	struct req_header header;
	struct memb_ring_id previous_ring_id;
	SaNameT checkpointName;
	SaCkptSectionIdT sectionId;	
	SaUint32T dataOffSet;
	SaUint32T dataSize;	
};



#endif /* IPC_CKPT_H_DEFINED */
