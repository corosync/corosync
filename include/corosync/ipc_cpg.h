/*
 * Copyright (c) 2006-2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
 * Author: Jan Friesse (jfriesse@redhat.com)
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
#include <corosync/corotypes.h>

enum req_cpg_types {
	MESSAGE_REQ_CPG_JOIN = 0,
	MESSAGE_REQ_CPG_LEAVE = 1,
	MESSAGE_REQ_CPG_MCAST = 2,
	MESSAGE_REQ_CPG_MEMBERSHIP = 3,
	MESSAGE_REQ_CPG_LOCAL_GET = 4,
};

enum res_cpg_types {
	MESSAGE_RES_CPG_JOIN = 0,
	MESSAGE_RES_CPG_LEAVE = 1,
	MESSAGE_RES_CPG_MCAST = 2,
	MESSAGE_RES_CPG_MEMBERSHIP = 3,
	MESSAGE_RES_CPG_CONFCHG_CALLBACK = 4,
	MESSAGE_RES_CPG_DELIVER_CALLBACK = 5,
	MESSAGE_RES_CPG_FLOW_CONTROL_STATE_SET = 6,
	MESSAGE_RES_CPG_LOCAL_GET = 7,
	MESSAGE_RES_CPG_FLOWCONTROL_CALLBACK = 8
};

enum lib_cpg_confchg_reason {
	CONFCHG_CPG_REASON_JOIN = 1,
	CONFCHG_CPG_REASON_LEAVE = 2,
	CONFCHG_CPG_REASON_NODEDOWN = 3,
	CONFCHG_CPG_REASON_NODEUP = 4,
	CONFCHG_CPG_REASON_PROCDOWN = 5
};

struct req_lib_cpg_join {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
};

struct res_lib_cpg_join {
	coroipc_response_header_t header __attribute__((aligned(8)));
};

struct req_lib_cpg_trackstart {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
};

struct res_lib_cpg_trackstart {
	coroipc_response_header_t header __attribute__((aligned(8)));
};

struct req_lib_cpg_trackstop {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
};

struct res_lib_cpg_trackstop {
	coroipc_response_header_t header __attribute__((aligned(8)));
};

struct req_lib_cpg_local_get {
	coroipc_request_header_t header __attribute__((aligned(8)));
};

struct res_lib_cpg_local_get {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t local_nodeid __attribute__((aligned(8)));
};

struct req_lib_cpg_mcast {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t guarantee __attribute__((aligned(8)));
	mar_uint32_t msglen __attribute__((aligned(8)));
	mar_uint8_t message[] __attribute__((aligned(8)));
};

struct res_lib_cpg_mcast {
	coroipc_response_header_t header __attribute__((aligned(8)));
};

/* Message from another node */
struct res_lib_cpg_deliver_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t msglen __attribute__((aligned(8)));
	mar_uint32_t nodeid __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_uint8_t message[] __attribute__((aligned(8)));
};

struct res_lib_cpg_flowcontrol_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t flow_control_state __attribute__((aligned(8)));
};

struct req_lib_cpg_membership {
	coroipc_request_header_t header __attribute__((aligned(8)));
//	mar_cpg_name_t group_name __attribute__((aligned(8)));
};

struct res_lib_cpg_confchg_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t member_list_entries __attribute__((aligned(8)));
	mar_uint32_t joined_list_entries __attribute__((aligned(8)));
	mar_uint32_t left_list_entries __attribute__((aligned(8)));
	mar_cpg_address_t member_list[];
//	struct cpg_address left_list[];
//	struct cpg_address joined_list[];
};

struct req_lib_cpg_leave {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
};

struct res_lib_cpg_leave {
	coroipc_response_header_t header __attribute__((aligned(8)));
};

#endif /* IPC_CPG_H_DEFINED */
