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
#ifndef IPC_LCK_H_DEFINED
#define IPC_LCK_H_DEFINED

#include "saAis.h"
#include "saLck.h"
#include "ipc_gen.h"

enum req_lib_lck_resource_types {
	MESSAGE_REQ_LCK_RESOURCEOPEN = 0,
	MESSAGE_REQ_LCK_RESOURCEOPENASYNC = 1,
	MESSAGE_REQ_LCK_RESOURCECLOSE = 2,
	MESSAGE_REQ_LCK_RESOURCELOCK = 3,
	MESSAGE_REQ_LCK_RESOURCELOCKASYNC = 4,
	MESSAGE_REQ_LCK_RESOURCEUNLOCK = 5,
	MESSAGE_REQ_LCK_RESOURCEUNLOCKASYNC = 6,
	MESSAGE_REQ_LCK_LOCKPURGE = 7,
};

enum res_lib_lck_resource_types {
	MESSAGE_RES_LCK_RESOURCEOPEN = 0,
	MESSAGE_RES_LCK_RESOURCEOPENASYNC = 1,
	MESSAGE_RES_LCK_RESOURCECLOSE = 2,
	MESSAGE_RES_LCK_RESOURCELOCK = 3,
	MESSAGE_RES_LCK_RESOURCELOCKASYNC = 4,
	MESSAGE_RES_LCK_RESOURCEUNLOCK = 5,
	MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC = 6,
	MESSAGE_RES_LCK_LOCKPURGE = 7,
	MESSAGE_RES_LCK_LOCKWAITERCALLBACK = 8
};

struct req_lib_lck_resourceopen {
	struct req_header header;
	SaInvocationT invocation;
	SaNameT lockResourceName;
	SaLckResourceOpenFlagsT resourceOpenFlags;
	SaLckResourceHandleT resourceHandle;
	SaTimeT timeout;
	int async_call;
};

struct res_lib_lck_resourceopen {
	struct res_header header;
	SaLckResourceHandleT resourceHandle;
	struct message_source source;
};

struct res_lib_lck_resourceopenasync {
	struct res_header header;
	SaInvocationT invocation;
	SaLckResourceHandleT resourceHandle;
	struct message_source source;
};

struct req_lib_lck_resourceclose {
	struct req_header header;
	SaNameT lockResourceName;
	SaLckResourceHandleT resourceHandle;
};

struct res_lib_lck_resourceclose {
	struct res_header header;
};

struct req_lib_lck_resourcelock {
	struct req_header header;
	SaNameT lockResourceName;
	SaInvocationT invocation;
	SaLckLockModeT lockMode;
	SaLckLockFlagsT lockFlags;
	SaLckWaiterSignalT waiterSignal;
	SaTimeT timeout;
	SaLckLockIdT lockId;
	int async_call;
	struct message_source source;
	SaLckResourceHandleT resourceHandle;
};

struct res_lib_lck_resourcelock {
	struct res_header header;
	SaLckLockStatusT lockStatus;
	void *resource_lock;
};

struct res_lib_lck_resourcelockasync {
	struct res_header header;
	SaLckLockStatusT lockStatus;
	SaLckLockIdT lockId;
	void *resource_lock;
	SaInvocationT invocation;
	SaLckResourceHandleT resourceHandle;
};

struct req_lib_lck_resourceunlock {
	struct req_header header;
	SaNameT lockResourceName;
	SaLckLockIdT lockId;
	SaInvocationT invocation;
	SaTimeT timeout;
	int async_call;
	void *resource_lock;
};

struct res_lib_lck_resourceunlock {
	struct res_header header;
};

struct res_lib_lck_resourceunlockasync {
	struct res_header header;
	SaInvocationT invocation;
};

struct req_lib_lck_lockpurge {
	struct req_header header;
	SaNameT lockResourceName;
};

struct res_lib_lck_lockpurge {
	struct res_header header;
};

struct res_lib_lck_lockwaitercallback {
	struct res_header header;
	SaLckWaiterSignalT waiter_signal;
	SaLckLockIdT lock_id;
	SaLckLockModeT mode_held;
	SaLckLockModeT mode_requested;
};

#endif /* IPC_LCK_H_DEFINED */
