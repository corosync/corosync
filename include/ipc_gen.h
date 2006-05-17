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

#include <netinet/in.h>
#include "../exec/totemip.h"

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

struct req_header {
	int size;
	int id;
} __attribute__((packed));

struct res_header {
	int size;
	int id;
	SaAisErrorT error;
};

#ifdef CMPILE_OUT
// TODO REMOVE THIS
enum req_init_types_a {
    MESSAGE_REQ_EVS_INIT,
    MESSAGE_REQ_CLM_INIT,
    MESSAGE_REQ_AMF_INIT,
    MESSAGE_REQ_CKPT_INIT,
    MESSAGE_REQ_CKPT_CHECKPOINT_INIT,
    MESSAGE_REQ_CKPT_SECTIONITERATOR_INIT,
    MESSAGE_REQ_EVT_INIT
};
#endif

struct req_lib_resdis_init {
	int size;
	int id;
	int service;
};

struct req_lib_response_init {
	struct req_lib_resdis_init resdis_header;
};

struct req_lib_dispatch_init {
	struct req_lib_resdis_init resdis_header;
	unsigned long conn_info;
};

struct req_lib_init {
	struct res_header header;
};

struct res_lib_init {
	struct res_header header;
};

struct res_lib_response_init {
	struct res_header header;
	unsigned long conn_info;
};

struct res_lib_dispatch_init {
	struct res_header header;
};
struct message_source {
	struct totem_ip_address addr;
	void *conn;
} __attribute__((packed));

#endif /* IPC_GEN_H_DEFINED */
