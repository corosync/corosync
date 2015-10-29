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

#ifndef COROSYNC_VOTEQUORUM_H_DEFINED
#define COROSYNC_VOTEQUORUM_H_DEFINED

#include <corosync/corotypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief votequorum_handle_t
 */
typedef uint64_t votequorum_handle_t;

#define VOTEQUORUM_INFO_TWONODE                  1
#define VOTEQUORUM_INFO_QUORATE                  2
#define VOTEQUORUM_INFO_WAIT_FOR_ALL             4
#define VOTEQUORUM_INFO_LAST_MAN_STANDING        8
#define VOTEQUORUM_INFO_AUTO_TIE_BREAKER         16
#define VOTEQUORUM_INFO_ALLOW_DOWNSCALE          32
#define VOTEQUORUM_INFO_QDEVICE_REGISTERED       64
#define VOTEQUORUM_INFO_QDEVICE_ALIVE            128
#define VOTEQUORUM_INFO_QDEVICE_CAST_VOTE        256
#define VOTEQUORUM_INFO_QDEVICE_MASTER_WINS      512

#define VOTEQUORUM_QDEVICE_NODEID                0
#define VOTEQUORUM_QDEVICE_MAX_NAME_LEN          255
#define VOTEQUORUM_QDEVICE_DEFAULT_TIMEOUT       10000
#define VOTEQUORUM_QDEVICE_DEFAULT_SYNC_TIMEOUT  30000

#define VOTEQUORUM_NODESTATE_MEMBER              1
#define VOTEQUORUM_NODESTATE_DEAD                2
#define VOTEQUORUM_NODESTATE_LEAVING             3

/** @} */

/**
 * @brief The votequorum_info struct
 */
struct votequorum_info {
	unsigned int node_id;
	unsigned int node_state;
	unsigned int node_votes;
	unsigned int node_expected_votes;
	unsigned int highest_expected;
	unsigned int total_votes;
	unsigned int quorum;
	unsigned int flags;
	unsigned int qdevice_votes;
	char qdevice_name[VOTEQUORUM_QDEVICE_MAX_NAME_LEN];
};

/**
 * @brief The votequorum_node_t struct
 */
typedef struct {
	uint32_t nodeid;
	uint32_t state;
} votequorum_node_t;

/**
 * @brief The votequorum_ring_id_t struct
 */
typedef struct {
	uint32_t nodeid;
	uint64_t seq;
} votequorum_ring_id_t;

/**
 * @brief The votequorum_quorum_notification_fn_t callback
 */
typedef void (*votequorum_quorum_notification_fn_t) (
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t quorate,
	uint32_t node_list_entries,
	votequorum_node_t node_list[]);

typedef void (*votequorum_nodelist_notification_fn_t) (
	votequorum_handle_t handle,
	uint64_t context,
	votequorum_ring_id_t ring_id,
	uint32_t node_list_entries,
	uint32_t node_list[]);

/**
 * @brief The votequorum_expectedvotes_notification_fn_t callback
 */
typedef void (*votequorum_expectedvotes_notification_fn_t) (
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t expected_votes);

/**
 * @brief The votequorum_callbacks_t struct
 */
typedef struct {
	votequorum_quorum_notification_fn_t votequorum_quorum_notify_fn;
	votequorum_expectedvotes_notification_fn_t votequorum_expectedvotes_notify_fn;
	votequorum_nodelist_notification_fn_t votequorum_nodelist_notify_fn;
} votequorum_callbacks_t;

/**
 * @brief Create a new quorum connection
 * @param handle
 * @param callbacks
 * @return
 */
cs_error_t votequorum_initialize (
	votequorum_handle_t *handle,
	votequorum_callbacks_t *callbacks);

/**
 * @brief Close the quorum handle
 * @param handle
 * @return
 */
cs_error_t votequorum_finalize (
	votequorum_handle_t handle);

/**
 * @brief Dispatch messages and configuration changes
 * @param handle
 * @param dispatch_types
 * @return
 */
cs_error_t votequorum_dispatch (
	votequorum_handle_t handle,
	cs_dispatch_flags_t dispatch_types);

/**
 * @brief Get a file descriptor on which to poll.
 *
 * @note votequorum_handle_t is NOT a file descriptor and may not be used directly.
 *
 * @param handle
 * @param fd
 * @return
 */
cs_error_t votequorum_fd_get (
	votequorum_handle_t handle,
	int *fd);

/**
 * @brief Get quorum information.
 * @param handle
 * @param nodeid
 * @param info
 * @return
 */
cs_error_t votequorum_getinfo (
	votequorum_handle_t handle,
	unsigned int nodeid,
	struct votequorum_info *info);

/**
 * @brief set expected_votes
 * @param handle
 * @param expected_votes
 * @return
 */
cs_error_t votequorum_setexpected (
	votequorum_handle_t handle,
	unsigned int expected_votes);

/**
 * @brief set votes for a node
 * @param handle
 * @param nodeid
 * @param votes
 * @return
 */
cs_error_t votequorum_setvotes (
	votequorum_handle_t handle,
	unsigned int nodeid,
	unsigned int votes);

/**
 * @brief Track node and quorum changes
 * @param handle
 * @param context
 * @param flags
 * @return
 */
cs_error_t votequorum_trackstart (
	votequorum_handle_t handle,
	uint64_t context,
        unsigned int flags);

/**
 * @brief votequorum_trackstop
 * @param handle
 * @return
 */
cs_error_t votequorum_trackstop (
	votequorum_handle_t handle);

/**
 * @brief Save and retrieve private data/context
 * @param handle
 * @param context
 * @return
 */
cs_error_t votequorum_context_get (
	votequorum_handle_t handle,
	void **context);

/**
 * @brief votequorum_context_set
 * @param handle
 * @param context
 * @return
 */
cs_error_t votequorum_context_set (
	votequorum_handle_t handle,
	void *context);

/**
 * @brief Register a quorum device
 *
 * it will be DEAD until polled
 *
 * @param handle
 * @param name
 * @return
 */
cs_error_t votequorum_qdevice_register (
	votequorum_handle_t handle,
	const char *name);

/**
 * @brief Unregister a quorum device
 * @param handle
 * @param name
 * @return
 */
cs_error_t votequorum_qdevice_unregister (
	votequorum_handle_t handle,
	const char *name);

/**
 * @brief Update registered name of a quorum device
 * @param handle
 * @param oldname
 * @param newname
 * @return
 */
cs_error_t votequorum_qdevice_update (
	votequorum_handle_t handle,
	const char *oldname,
	const char *newname);

/**
 * @brief Poll a quorum device
 * @param handle
 * @param name
 * @param cast_vote
 * @param ring_id
 * @return
 */
cs_error_t votequorum_qdevice_poll (
	votequorum_handle_t handle,
	const char *name,
	unsigned int cast_vote,
	votequorum_ring_id_t ring_id);

/**
 * @brief Allow qdevice to tell votequorum if master_wins can be enabled or not
 * @param handle
 * @param name
 * @param allow
 * @return
 */
cs_error_t votequorum_qdevice_master_wins (
	votequorum_handle_t handle,
	const char *name,
	unsigned int allow);

#ifdef __cplusplus
}
#endif
#endif /* COROSYNC_VOTEQUORUM_H_DEFINED */
