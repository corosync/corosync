/*
 * Copyright (c) 2008-2012 Red Hat, Inc.
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#include <corosync/corotypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief quorum_handle_t
 */
typedef uint64_t quorum_handle_t;

/**
 * @brief The quorum_notification_fn_t callback
 */
typedef void (*quorum_notification_fn_t) (
	quorum_handle_t handle,
	uint32_t quorate,
	uint64_t ring_seq,
	uint32_t view_list_entries,
	uint32_t *view_list
	);

/**
 * @brief The quorum_callbacks_t struct
 */
typedef struct {
	quorum_notification_fn_t quorum_notify_fn;
} quorum_callbacks_t;

#define QUORUM_FREE	0
#define QUORUM_SET	1

/**
 * @brief Create a new quorum connection
 * @param handle
 * @param callbacks
 * @param quorum_type
 * @return
 */
cs_error_t quorum_initialize (
	quorum_handle_t *handle,
	quorum_callbacks_t *callbacks,
	uint32_t *quorum_type);

/**
 * @brief Close the quorum handle
 * @param handle
 * @return
 */
cs_error_t quorum_finalize (
	quorum_handle_t handle);

/**
 * @brief Get a file descriptor on which to poll.
 *
 * @note quorum_handle_t is NOT a file descriptor and may not be used directly.
 *
 * @param handle
 * @param fd
 * @return
 */
cs_error_t quorum_fd_get (
	quorum_handle_t handle,
	int *fd);

/**
 * @brief Dispatch messages and configuration changes
 * @param handle
 * @param dispatch_types
 * @return
 */
cs_error_t quorum_dispatch (
	quorum_handle_t handle,
	cs_dispatch_flags_t dispatch_types);

/**
 * @brief Get quorum information.
 * @param handle
 * @param quorate
 * @return
 */
cs_error_t quorum_getquorate (
	quorum_handle_t handle,
	int *quorate);

/**
 * @brief Track node and quorum changes
 * @param handle
 * @param flags
 * @return
 */
cs_error_t quorum_trackstart (
	quorum_handle_t handle,
	unsigned int flags );

/**
 * @brief quorum_trackstop
 * @param handle
 * @return
 */
cs_error_t quorum_trackstop (
	quorum_handle_t handle);

/**
 * @brief quorum_context_set
 * @param handle
 * @param context
 * @return
 */
cs_error_t quorum_context_set (
	quorum_handle_t handle,
	const void *context);

/**
 * @brief quorum_context_get
 * @param handle
 * @param context
 * @return
 */
cs_error_t quorum_context_get (
	quorum_handle_t handle,
	const void **context);

#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_QUORUM_H_DEFINED */
