/*
 * Copyright (c) 2009-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Christine Caulfield (ccaulfie@redhat.com)
 *          Fabio M. Di Nitto   (fdinitto@redhat.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
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
#ifndef IPC_VOTEQUORUM_H_DEFINED
#define IPC_VOTEQUORUM_H_DEFINED

#include <corosync/mar_gen.h>
#define VOTEQUORUM_MAX_QDEVICE_NAME_LEN 255

enum req_votequorum_types {
	MESSAGE_REQ_VOTEQUORUM_GETINFO = 0,
	MESSAGE_REQ_VOTEQUORUM_SETEXPECTED,
	MESSAGE_REQ_VOTEQUORUM_SETVOTES,
	MESSAGE_REQ_VOTEQUORUM_TRACKSTART,
	MESSAGE_REQ_VOTEQUORUM_TRACKSTOP,
	MESSAGE_REQ_VOTEQUORUM_QDEVICE_REGISTER,
	MESSAGE_REQ_VOTEQUORUM_QDEVICE_UNREGISTER,
	MESSAGE_REQ_VOTEQUORUM_QDEVICE_UPDATE,
	MESSAGE_REQ_VOTEQUORUM_QDEVICE_POLL,
	MESSAGE_REQ_VOTEQUORUM_QDEVICE_GETINFO
};

enum res_votequorum_types {
	MESSAGE_RES_VOTEQUORUM_STATUS = 0,
	MESSAGE_RES_VOTEQUORUM_GETINFO,
	MESSAGE_RES_VOTEQUORUM_QDEVICE_GETINFO,
	MESSAGE_RES_VOTEQUORUM_TRACKSTART,
	MESSAGE_RES_VOTEQUORUM_NOTIFICATION,
	MESSAGE_RES_VOTEQUORUM_EXPECTEDVOTES_NOTIFICATION
};

struct req_lib_votequorum_qdevice_register {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	char name[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
};

struct req_lib_votequorum_qdevice_unregister {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	char name[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
};

struct req_lib_votequorum_qdevice_update {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	char oldname[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
	char newname[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
};

struct req_lib_votequorum_qdevice_poll {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	char name[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
	int cast_vote;
};

struct req_lib_votequorum_qdevice_getinfo {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	int nodeid;
};

struct req_lib_votequorum_setvotes {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	unsigned int votes;
	int nodeid;
};

struct req_lib_votequorum_setexpected {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	unsigned int expected_votes;
};

struct req_lib_votequorum_trackstart {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	uint64_t context;
	unsigned int track_flags;
};

struct req_lib_votequorum_general {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
};

struct req_lib_votequorum_getinfo {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	int nodeid;
};

struct res_lib_votequorum_status {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

#define VOTEQUORUM_INFO_TWONODE                 1
#define VOTEQUORUM_INFO_QUORATE                 2
#define VOTEQUORUM_INFO_WAIT_FOR_ALL	        4
#define VOTEQUORUM_INFO_LAST_MAN_STANDING       8
#define VOTEQUORUM_INFO_AUTO_TIE_BREAKER       16
#define VOTEQUORUM_INFO_ALLOW_DOWNSCALE        32
#define VOTEQUORUM_INFO_QDEVICE_REGISTERED     64
#define VOTEQUORUM_INFO_QDEVICE_ALIVE         128
#define VOTEQUORUM_INFO_QDEVICE_CAST_VOTE     256

#define VOTEQUORUM_NODESTATE_MEMBER     1
#define VOTEQUORUM_NODESTATE_DEAD       2
#define VOTEQUORUM_NODESTATE_LEAVING    3

struct res_lib_votequorum_getinfo {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	unsigned int nodeid;
	unsigned int state;
	unsigned int votes;
	unsigned int expected_votes;
	unsigned int highest_expected;
	unsigned int total_votes;
	unsigned int quorum;
	unsigned int flags;
};

struct res_lib_votequorum_qdevice_getinfo {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	unsigned int votes;
	unsigned int alive;
	unsigned int cast_vote;
	char name[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
};

struct votequorum_node {
	mar_uint32_t nodeid;
	mar_uint32_t state;
};

struct res_lib_votequorum_notification {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint32_t quorate __attribute__((aligned(8)));
	mar_uint64_t context __attribute__((aligned(8)));
	mar_uint32_t node_list_entries __attribute__((aligned(8)));
	struct votequorum_node node_list[] __attribute__((aligned(8)));
};

struct res_lib_votequorum_expectedvotes_notification {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint64_t context __attribute__((aligned(8)));
	mar_uint32_t expected_votes __attribute__((aligned(8)));
};

#endif
