/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#ifndef IPC_EVS_H_DEFINED
#define IPC_EVS_H_DEFINED

//#include <netinet/in6.h>
#include "saAis.h"
#include "evs.h"
#include "ipc_gen.h"

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
	struct res_header header;
	struct evs_address evs_address;
	int msglen;
	char msg[0];
};

struct res_evs_confchg_callback {
	struct res_header header;
	int member_list_entries;
	int left_list_entries;
	int joined_list_entries;
	struct evs_address member_list[16];
	struct evs_address left_list[16];
	struct evs_address joined_list[16];
};

struct req_lib_evs_join {
	struct res_header header;
	int group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_join {
	struct res_header header;
};

struct req_lib_evs_leave {
	struct res_header header;
	int group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_leave {
	struct res_header header;
};

struct req_lib_evs_mcast_joined {
	struct res_header header;
	evs_guarantee_t guarantee;
	int msg_len;
	char msg[0];
};

struct res_lib_evs_mcast_joined {
	struct res_header header;
};

struct req_lib_evs_mcast_groups {
	struct res_header header;
	evs_guarantee_t guarantee;
	int msg_len;
	int group_entries;
	struct evs_group groups[0];
};

struct res_lib_evs_mcast_groups {
	struct res_header header;
};


struct req_exec_evs_mcast {
	struct req_header header;
	int group_entries;
	int msg_len;
	struct evs_group groups[0];
	/* data goes here */
};

struct req_lib_evs_membership_get {
	struct req_header header;
};

struct res_lib_evs_membership_get {
	struct res_header header;
	struct in_addr local_addr;
	struct in_addr member_list[16];
	int member_list_entries;
};
#endif /* IPC_EVS_H_DEFINED */
