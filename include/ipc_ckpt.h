/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
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

#include "saAis.h"
#include "saCkpt.h"
#include "ipc_gen.h"
#include "mar_ckpt.h"

enum req_lib_ckpt_checkpoint_types {
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPEN = 0,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTCLOSE = 1,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTUNLINK = 2,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET = 3,
	MESSAGE_REQ_CKPT_ACTIVEREPLICASET = 4,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET = 5,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONCREATE = 6,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONDELETE = 7,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET = 8,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONWRITE = 9,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONOVERWRITE = 10,
	MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONREAD = 11,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE = 12,
	MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC = 13,
	MESSAGE_REQ_CKPT_SECTIONITERATIONINITIALIZE = 14,
	MESSAGE_REQ_CKPT_SECTIONITERATIONFINALIZE = 15,
	MESSAGE_REQ_CKPT_SECTIONITERATIONNEXT = 16
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
	MESSAGE_RES_CKPT_SECTIONITERATIONINITIALIZE = 15,
	MESSAGE_RES_CKPT_SECTIONITERATIONFINALIZE = 16,
	MESSAGE_RES_CKPT_SECTIONITERATIONNEXT = 17
};

struct req_lib_ckpt_checkpointopen {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_ckpt_checkpoint_creation_attributes_t checkpoint_creation_attributes __attribute__((aligned(8)));
	int checkpoint_creation_attributes_set __attribute__((aligned(8)));
	mar_ckpt_checkpoint_open_flags_t checkpoint_open_flags __attribute__((aligned(8)));
	mar_ckpt_checkpoint_handle_t checkpoint_handle __attribute__((aligned(8)));
	SaAisErrorT fail_with_error __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_uint32_t async_call __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointopen {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointopenasync {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_ckpt_checkpoint_handle_t checkpoint_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_checkpointclose {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointclose {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_checkpointunlink {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointunlink {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_checkpointretentiondurationset {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_time_t retention_duration __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointretentiondurationset {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_activereplicaset {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_activereplicaset {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_checkpointstatusget {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointstatusget {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_ckpt_checkpoint_descriptor_t checkpoint_descriptor __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectioncreate {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
	mar_uint32_t initial_data_size __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectioncreate {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectiondelete {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectiondelete {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectionexpirationtimeset {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectionexpirationtimeset {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectioniterationinitialize {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_ckpt_sections_chosen_t sections_chosen __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectioniterationinitialize {
	mar_res_header_t header __attribute__((aligned(8)));
	unsigned int iteration_handle __attribute__((aligned(8)));
	mar_size_t max_section_id_size;
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectioniterationfinalize {
	mar_req_header_t header __attribute__((aligned(8)));
	unsigned int iteration_handle __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectioniterationfinalize {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectioniterationnext {
	mar_req_header_t header __attribute__((aligned(8)));
	unsigned int iteration_handle __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectioniterationnext {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_ckpt_section_descriptor_t section_descriptor __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectionwrite {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectionwrite {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_sectionoverwrite {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_uint32_t data_size __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectionoverwrite {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));
	
struct req_lib_ckpt_sectionread {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_sectionread {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_size_t data_read __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_checkpointsynchronize {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointsynchronize {
	mar_res_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_ckpt_checkpointsynchronizeasync {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_ckpt_checkpointsynchronizeasync {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

#endif /* IPC_CKPT_H_DEFINED */
