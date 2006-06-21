/*
 * Copyright (c) 2006 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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
#ifndef IPC_CPG_H_DEFINED
#define IPC_CPG_H_DEFINED

#include <netinet/in.h>
#include "saAis.h"
#include "ipc_gen.h"

enum req_cpg_types {
	MESSAGE_REQ_CPG_JOIN = 0,
	MESSAGE_REQ_CPG_LEAVE = 1,
	MESSAGE_REQ_CPG_MCAST = 2,
	MESSAGE_REQ_CPG_MEMBERSHIP = 3,
	MESSAGE_REQ_CPG_TRACKSTART = 4,
	MESSAGE_REQ_CPG_TRACKSTOP = 5
};

enum res_cpg_types {
	MESSAGE_RES_CPG_JOIN = 0,
	MESSAGE_RES_CPG_LEAVE = 1,
	MESSAGE_RES_CPG_MCAST = 2,
	MESSAGE_RES_CPG_MEMBERSHIP = 3,
	MESSAGE_RES_CPG_CONFCHG_CALLBACK = 4,
	MESSAGE_RES_CPG_DELIVER_CALLBACK = 5,
	MESSAGE_RES_CPG_TRACKSTART = 6,
	MESSAGE_RES_CPG_TRACKSTOP = 7
};

enum lib_cpg_confchg_reason {
	CONFCHG_CPG_REASON_JOIN = 1,
	CONFCHG_CPG_REASON_LEAVE = 2,
	CONFCHG_CPG_REASON_NODEDOWN = 3,
	CONFCHG_CPG_REASON_NODEUP = 4,
	CONFCHG_CPG_REASON_PROCDOWN = 5
};

#ifndef CPG_MAX_NAME_LENGTH
#define CPG_MAX_NAME_LENGTH 128
struct cpg_name {
	uint32_t length;
	char value[CPG_MAX_NAME_LENGTH];
};
#endif

struct req_lib_cpg_join {
	mar_req_header_t header;
	struct cpg_name group_name;
	pid_t pid;
};

struct res_lib_cpg_join {
	mar_res_header_t header;
};

struct req_lib_cpg_trackstart {
	mar_req_header_t header;
	struct cpg_name group_name;
	pid_t pid;
};

struct res_lib_cpg_trackstart {
	mar_res_header_t header;
};

struct req_lib_cpg_trackstop {
	mar_req_header_t header;
	struct cpg_name group_name;
	pid_t pid;
};

struct res_lib_cpg_trackstop {
	mar_res_header_t header;
};

struct req_lib_cpg_mcast {
	mar_res_header_t header;
	uint32_t guarantee;
	uint32_t msglen;
	char message[];
};

/* Message from another node */
struct res_lib_cpg_deliver_callback {
	mar_res_header_t header;
	struct cpg_name group_name;
	uint32_t msglen;
	uint32_t nodeid;
	uint32_t pid;
	char message[];
};

/* Notifications & join return a list of these */
struct cpg_groupinfo {
	uint32_t nodeid;
	uint32_t pid;
	uint32_t reason; /* How joined or left */
};


struct req_lib_cpg_membership {
	mar_req_header_t header;
	struct cpg_name group_name;
};

struct res_lib_cpg_confchg_callback {
	mar_res_header_t header;
	struct cpg_name group_name;
	uint32_t member_list_entries;
	uint32_t joined_list_entries;
	uint32_t left_list_entries;
	struct cpg_groupinfo member_list[];
//	struct cpg_groupinfo left_list[];
//	struct cpg_groupinfo joined_list[];
};

struct req_lib_cpg_leave {
	mar_req_header_t header;
	struct cpg_name group_name;
	pid_t pid;
};

struct res_lib_cpg_leave {
	mar_res_header_t header;
};


#endif /* IPC_CPG_H_DEFINED */
