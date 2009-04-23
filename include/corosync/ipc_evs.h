/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#ifndef IPC_EVS_H_DEFINED
#define IPC_EVS_H_DEFINED

#include <inttypes.h>
#include <corosync/corotypes.h>
#include <corosync/mar_gen.h>

enum req_lib_evs_types {
	MESSAGE_REQ_EVS_JOIN = 0,
	MESSAGE_REQ_EVS_LEAVE = 1,
	MESSAGE_REQ_EVS_MCAST_JOINED = 2,
	MESSAGE_REQ_EVS_MCAST_GROUPS = 3,
	MESSAGE_REQ_EVS_MEMBERSHIP_GET = 4
};

enum res_lib_evs_types {
	MESSAGE_RES_EVS_DELIVER_CALLBACK = 0,
	MESSAGE_RES_EVS_CONFCHG_CALLBACK = 1,
	MESSAGE_RES_EVS_JOIN = 2,
	MESSAGE_RES_EVS_LEAVE = 3,
	MESSAGE_RES_EVS_MCAST_JOINED = 4,
	MESSAGE_RES_EVS_MCAST_GROUPS = 5,
	MESSAGE_RES_EVS_MEMBERSHIP_GET = 6
};

struct res_evs_deliver_callback {
	coroipc_response_header_t header;
	unsigned int local_nodeid;
	size_t msglen;
	char msg[0];
};

struct res_evs_confchg_callback {
	coroipc_response_header_t header;
	size_t member_list_entries;
	size_t left_list_entries;
	size_t joined_list_entries;
	unsigned int member_list[PROCESSOR_COUNT_MAX];
	unsigned int left_list[PROCESSOR_COUNT_MAX];
	unsigned int joined_list[PROCESSOR_COUNT_MAX];
};

struct req_lib_evs_join {
	coroipc_response_header_t header;
	size_t group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_join {
	coroipc_response_header_t header;
};

struct req_lib_evs_leave {
	coroipc_response_header_t header;
	size_t group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_leave {
	coroipc_response_header_t header;
};

struct req_lib_evs_mcast_joined {
	coroipc_response_header_t header;
	evs_guarantee_t guarantee;
	size_t msg_len;
	char msg[0];
};

struct res_lib_evs_mcast_joined {
	coroipc_response_header_t header;
};

struct req_lib_evs_mcast_groups {
	coroipc_response_header_t header;
	evs_guarantee_t guarantee;
	size_t msg_len;
	size_t group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_mcast_groups {
	coroipc_response_header_t header;
};


struct req_exec_evs_mcast {
	coroipc_request_header_t header;
	size_t group_entries;
	size_t msg_len;
	struct evs_group groups[0];
	/* data goes here */
};

struct req_lib_evs_membership_get {
	coroipc_request_header_t header;
};

struct res_lib_evs_membership_get {
	coroipc_response_header_t header;
	unsigned int local_nodeid;
	unsigned int member_list[PROCESSOR_COUNT_MAX];
	size_t member_list_entries;
};
#endif /* IPC_EVS_H_DEFINED */
