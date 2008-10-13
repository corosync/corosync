/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfi@redhat.com)
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
#ifndef COROSYNC_QUORUM_H_DEFINED
#define COROSYNC_QUORUM_H_DEFINED

typedef uint64_t quorum_handle_t;


typedef enum {
	QUORUM_OK = 1,
	QUORUM_ERR_LIBRARY = 2,
	QUORUM_ERR_TIMEOUT = 5,
	QUORUM_ERR_TRY_AGAIN = 6,
	QUORUM_ERR_INVALID_PARAM = 7,
	QUORUM_ERR_NO_MEMORY = 8,
	QUORUM_ERR_BAD_HANDLE = 9,
	QUORUM_ERR_ACCESS = 11,
	QUORUM_ERR_NOT_EXIST = 12,
	QUORUM_ERR_EXIST = 14,
	QUORUM_ERR_NOT_SUPPORTED = 20,
	QUORUM_ERR_SECURITY = 29
} quorum_error_t;

typedef enum {
	QUORUM_DISPATCH_ONE,
	QUORUM_DISPATCH_ALL,
	QUORUM_DISPATCH_BLOCKING
} quorum_dispatch_t;

typedef struct {
	uint32_t nodeid;
	uint32_t state;
} quorum_node_t;


typedef void (*quorum_notification_fn_t) (
	quorum_handle_t handle,
	uint32_t quorate,
	uint64_t ring_seq,
	uint32_t view_list_entries,
	uint32_t *view_list
	);

typedef struct {
	quorum_notification_fn_t quorum_notify_fn;
} quorum_callbacks_t;


/*
 * Create a new quorum connection
 */
quorum_error_t quorum_initialize (
	quorum_handle_t *handle,
	quorum_callbacks_t *callbacks);

/*
 * Close the quorum handle
 */
quorum_error_t quorum_finalize (
	quorum_handle_t handle);


/*
 * Dispatch messages and configuration changes
 */
quorum_error_t quorum_dispatch (
	quorum_handle_t handle,
	quorum_dispatch_t dispatch_types);


/*
 * Get quorum information.
 */
quorum_error_t quorum_getquorate (
	quorum_handle_t handle,
	int *quorate);

/* Track node and quorum changes */
quorum_error_t quorum_trackstart (
	quorum_handle_t handle,
	unsigned int flags );

quorum_error_t quorum_trackstop (
	quorum_handle_t handle);


#endif /* COROSYNC_QUORUM_H_DEFINED */
