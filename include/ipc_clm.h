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
#ifndef IPC_CLM_H_DEFINED
#define IPC_CLM_H_DEFINED

#include <netinet/in.h>
#include "ais_types.h"
#include "saClm.h"
#include "ipc_gen.h"

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
	struct req_header header;
	SaUint8T trackFlags;
	int return_in_callback;
};

struct res_lib_clm_clustertrack {
	struct res_header header;
	SaUint64T viewNumber;
	SaUint32T numberOfItems;
	SaClmClusterNotificationT notification[32]; /* should be PROCESSOR_COUNT_MAX */
};

struct req_lib_clm_trackstop {
	struct req_header header;
	SaSizeT dataRead;
	SaErrorT error;
};

struct res_lib_clm_trackstop {
	struct res_header header;
};

struct req_lib_clm_nodeget {
	struct req_header header;
	SaInvocationT invocation;
	SaClmNodeIdT nodeId;
};

struct res_clm_nodeget {
	struct res_header header;
	SaInvocationT invocation;
	SaClmClusterNodeT clusterNode;
	int valid;
};

struct req_lib_clm_nodegetasync {
	struct req_header header;
	SaInvocationT invocation;
	SaClmNodeIdT nodeId;
};

struct res_clm_nodegetasync {
	struct res_header header;
};

struct res_clm_nodegetcallback {
	struct res_header header;
	SaInvocationT invocation;
	SaClmClusterNodeT clusterNode;
};

#endif /* IPC_CLM_H_DEFINED */
