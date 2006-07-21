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
#ifndef IPC_GEN_H_DEFINED
#define IPC_GEN_H_DEFINED

#include "mar_gen.h"

enum service_types {
	EVS_SERVICE = 0,
	CLM_SERVICE = 1,
	AMF_SERVICE = 2,
	CKPT_SERVICE = 3,
	EVT_SERVICE = 4,
	LCK_SERVICE = 5,
	MSG_SERVICE = 6,
	CFG_SERVICE = 7,
	CPG_SERVICE = 8
};

enum req_init_types {
	MESSAGE_REQ_RESPONSE_INIT = 0,
	MESSAGE_REQ_DISPATCH_INIT = 1
};

enum res_init_types {
	MESSAGE_RES_INIT
};

typedef struct {
	int size __attribute__((aligned(8)));
	int id __attribute__((aligned(8)));
} mar_req_header_t __attribute__((aligned(8)));

static inline void swab_mar_req_header_t (mar_req_header_t *to_swab)
{
	swab_mar_int32_t (&to_swab->size);
	swab_mar_int32_t (&to_swab->id);
}

typedef struct {
	int size; __attribute__((aligned(8))) 
	int id __attribute__((aligned(8)));
	SaAisErrorT error __attribute__((aligned(8)));
} mar_res_header_t __attribute__((aligned(8)));

typedef struct {
	int size __attribute__((aligned(8)));
	int id __attribute__((aligned(8)));
	int service __attribute__((aligned(8)));
} mar_req_lib_resdis_init_t __attribute__((aligned(8)));

typedef struct {
	mar_req_lib_resdis_init_t resdis_header __attribute__((aligned(8)));
} mar_req_lib_response_init_t __attribute__((aligned(8)));

typedef struct {
	mar_req_lib_resdis_init_t resdis_header __attribute__((aligned(8)));
	mar_uint64_t conn_info __attribute__((aligned(8)));
} mar_req_lib_dispatch_init_t __attribute__((aligned(8)));

typedef struct {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_uint64_t conn_info __attribute__((aligned(8)));
} mar_res_lib_response_init_t __attribute__((aligned(8)));

typedef struct {
	mar_res_header_t header __attribute__((aligned(8)));
} mar_res_lib_dispatch_init_t __attribute__((aligned(8)));

typedef struct {
	mar_uint32_t nodeid __attribute__((aligned(8)));
	void *conn __attribute__((aligned(8)));
} mar_message_source_t __attribute__((aligned(8)));

static inline void swab_mar_message_source_t (mar_message_source_t *to_swab)
{
	swab_mar_uint32_t (&to_swab->nodeid);
	/*
	 * if it is from a byteswapped machine, then we can safely
	 * ignore its conn info data structure since this is only
	 * local to the machine
	 */
	to_swab->conn = NULL;
}

#endif /* IPC_GEN_H_DEFINED */
