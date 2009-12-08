/*
 * Copyright (c) 2009 Red Hat, Inc.
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
#ifndef COROIPC_IPC_H_DEFINED
#define COROIPC_IPC_H_DEFINED

#include <unistd.h>
#include "config.h"

/*
 * Darwin claims to support process shared synchronization
 * but it really does not.  The unistd.h header file is wrong.
 */
#if defined(COROSYNC_DARWIN) || defined(__UCLIBC__)
#undef _POSIX_THREAD_PROCESS_SHARED
#define _POSIX_THREAD_PROCESS_SHARED -1
#endif

#ifndef _POSIX_THREAD_PROCESS_SHARED
#define _POSIX_THREAD_PROCESS_SHARED -1
#endif

#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#endif

enum req_init_types {
	MESSAGE_REQ_RESPONSE_INIT = 0,
	MESSAGE_REQ_DISPATCH_INIT = 1
};

#define MESSAGE_REQ_CHANGE_EUID		1
#define MESSAGE_REQ_OUTQ_FLUSH		2

#define MESSAGE_RES_OUTQ_EMPTY         0
#define MESSAGE_RES_OUTQ_NOT_EMPTY     1
#define MESSAGE_RES_ENABLE_FLOWCONTROL 2
#define MESSAGE_RES_OUTQ_FLUSH_NR      3

struct control_buffer {
	unsigned int read;
	unsigned int write;
#if _POSIX_THREAD_PROCESS_SHARED > 0
	sem_t sem0;
	sem_t sem1;
	sem_t sem2;
#endif
};

enum res_init_types {
	MESSAGE_RES_INIT
};

typedef struct {
	int service __attribute__((aligned(8)));
	unsigned long long semkey __attribute__((aligned(8)));
	char control_file[64] __attribute__((aligned(8)));
	char request_file[64] __attribute__((aligned(8)));
	char response_file[64] __attribute__((aligned(8)));
	char dispatch_file[64] __attribute__((aligned(8)));
	size_t control_size __attribute__((aligned(8)));
	size_t request_size __attribute__((aligned(8)));
	size_t response_size __attribute__((aligned(8)));
	size_t dispatch_size __attribute__((aligned(8)));
} mar_req_setup_t __attribute__((aligned(8)));

typedef struct {
	int error __attribute__((aligned(8)));
} mar_res_setup_t __attribute__((aligned(8)));

typedef struct {
        uid_t euid __attribute__((aligned(8)));
        gid_t egid __attribute__((aligned(8)));
} mar_req_priv_change __attribute__((aligned(8)));

typedef struct {
	coroipc_response_header_t header __attribute__((aligned(8)));
	uint64_t conn_info __attribute__((aligned(8)));
} mar_res_lib_response_init_t __attribute__((aligned(8)));

typedef struct {
	coroipc_response_header_t header __attribute__((aligned(8)));
} mar_res_lib_dispatch_init_t __attribute__((aligned(8)));

typedef struct {
	uint32_t nodeid __attribute__((aligned(8)));
	void *conn __attribute__((aligned(8)));
} mar_message_source_t __attribute__((aligned(8)));

typedef struct {
        coroipc_request_header_t header __attribute__((aligned(8)));
        size_t map_size __attribute__((aligned(8)));
        char path_to_file[128] __attribute__((aligned(8)));
} mar_req_coroipcc_zc_alloc_t __attribute__((aligned(8)));

typedef struct {
        coroipc_request_header_t header __attribute__((aligned(8)));
        size_t map_size __attribute__((aligned(8)));
	uint64_t server_address __attribute__((aligned(8)));
} mar_req_coroipcc_zc_free_t __attribute__((aligned(8)));

typedef struct {
        coroipc_request_header_t header __attribute__((aligned(8)));
	uint64_t server_address __attribute__((aligned(8)));
} mar_req_coroipcc_zc_execute_t __attribute__((aligned(8)));

struct coroipcs_zc_header {
	int map_size;
	uint64_t server_address;
};

#define SOCKET_SERVICE_INIT	0xFFFFFFFF

#define ZC_ALLOC_HEADER		0xFFFFFFFF
#define ZC_FREE_HEADER		0xFFFFFFFE
#define ZC_EXECUTE_HEADER	0xFFFFFFFD

#endif /* COROIPC_IPC_H_DEFINED */
