/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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
#ifndef IPC_CLM_H_DEFINED
#define IPC_CLM_H_DEFINED

#include <netinet/in.h>
#include <corosync/ipc_gen.h>
#include "saAis.h"
#include "saClm.h"
#include "mar_clm.h"

enum req_clm_types {
	MESSAGE_REQ_CLM_TRACKSTART = 0,
	MESSAGE_REQ_CLM_TRACKSTOP = 1,
	MESSAGE_REQ_CLM_NODEGET = 2,
	MESSAGE_REQ_CLM_NODEGETASYNC = 3
};

enum res_clm_types {
	MESSAGE_RES_CLM_TRACKCALLBACK = 0,
	MESSAGE_RES_CLM_TRACKSTART = 1,
	MESSAGE_RES_CLM_TRACKSTOP = 2,
	MESSAGE_RES_CLM_NODEGET = 3,
	MESSAGE_RES_CLM_NODEGETASYNC = 4,
	MESSAGE_RES_CLM_NODEGETCALLBACK = 5
};

struct req_lib_clm_clustertrack {
	mar_req_header_t header __attribute__((aligned(8)));
	unsigned char track_flags __attribute__((aligned(8)));
	int return_in_callback __attribute__((aligned(8)));
};

struct res_lib_clm_clustertrack {
	mar_res_header_t header __attribute__((aligned(8)));
	unsigned long long view __attribute__((aligned(8)));
	unsigned int number_of_items __attribute__((aligned(8)));
	mar_clm_cluster_notification_t notification[PROCESSOR_COUNT_MAX] __attribute__((aligned(8)));
};

struct req_lib_clm_trackstop {
	mar_req_header_t header __attribute__((aligned(8)));
	unsigned long long data_read __attribute__((aligned(8)));
	SaAisErrorT error __attribute__((aligned(8)));
};

struct res_lib_clm_trackstop {
	mar_res_header_t header __attribute__((aligned(8)));
};

struct req_lib_clm_nodeget {
	mar_req_header_t header __attribute__((aligned(8)));
	unsigned long long invocation __attribute__((aligned(8)));
	unsigned int node_id __attribute__((aligned(8)));
};

struct res_clm_nodeget {
	mar_res_header_t header __attribute__((aligned(8)));
	unsigned long long invocation __attribute__((aligned(8)));
	mar_clm_cluster_node_t cluster_node __attribute__((aligned(8)));
	int valid __attribute__((aligned(8)));
};

struct req_lib_clm_nodegetasync {
	mar_req_header_t header __attribute__((aligned(8)));
	unsigned long long invocation __attribute__((aligned(8)));
	unsigned int node_id __attribute__((aligned(8)));
};

struct res_clm_nodegetasync {
	mar_res_header_t header __attribute__((aligned(8)));
};

struct res_clm_nodegetcallback {
	mar_res_header_t header __attribute__((aligned(8)));
	SaInvocationT invocation __attribute__((aligned(8)));
	mar_clm_cluster_node_t cluster_node __attribute__((aligned(8)));
};

#endif /* IPC_CLM_H_DEFINED */
