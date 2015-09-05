/*
 * Copyright (c) 2006-2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfi@redhat.com)
 * Author: Jan Friesse (jfriesse@redhat.com)
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
#ifndef COROSYNC_CPG_H_DEFINED
#define COROSYNC_CPG_H_DEFINED

#include <netinet/in.h>
#include <corosync/corotypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup cpg_corosync
 *
 * @{
 */
typedef uint64_t cpg_handle_t;

typedef uint64_t cpg_iteration_handle_t;

typedef enum {
	CPG_TYPE_UNORDERED, /**< not implemented */
	CPG_TYPE_FIFO,      /**< same as agreed */
	CPG_TYPE_AGREED,
	CPG_TYPE_SAFE       /**< not implemented */
} cpg_guarantee_t;

typedef enum {
	CPG_FLOW_CONTROL_DISABLED, /**< flow control is disabled - new messages may be sent */
	CPG_FLOW_CONTROL_ENABLED   /**< flow control is enabled - new messages should not be sent */
} cpg_flow_control_state_t;


typedef enum {
	CPG_REASON_JOIN = 1,
	CPG_REASON_LEAVE = 2,
	CPG_REASON_NODEDOWN = 3,
	CPG_REASON_NODEUP = 4,
	CPG_REASON_PROCDOWN = 5
} cpg_reason_t;

typedef enum {
	CPG_ITERATION_NAME_ONLY = 1,
	CPG_ITERATION_ONE_GROUP = 2,
	CPG_ITERATION_ALL = 3,
} cpg_iteration_type_t;

typedef enum {
	CPG_MODEL_V1 = 1,
} cpg_model_t;

struct cpg_address {
	uint32_t nodeid;
	uint32_t pid;
	uint32_t reason;
};

#define CPG_MAX_NAME_LENGTH 128
struct cpg_name {
	uint32_t length;
	char value[CPG_MAX_NAME_LENGTH];
};

#define CPG_MEMBERS_MAX 128

struct cpg_iteration_description_t {
	struct cpg_name group;
	uint32_t nodeid;
	uint32_t pid;
};

struct cpg_ring_id {
	uint32_t nodeid;
	uint64_t seq;
};

typedef void (*cpg_deliver_fn_t) (
	cpg_handle_t handle,
	const struct cpg_name *group_name,
	uint32_t nodeid,
	uint32_t pid,
	/**
	 * Unlike many "msg" pointers, this one is deliberately *not*
	 * declared const in order to permit in-place endian conversion.
	 */
	void *msg,
	size_t msg_len);

typedef void (*cpg_confchg_fn_t) (
	cpg_handle_t handle,
	const struct cpg_name *group_name,
	const struct cpg_address *member_list, size_t member_list_entries,
	const struct cpg_address *left_list, size_t left_list_entries,
	const struct cpg_address *joined_list, size_t joined_list_entries);

typedef void (*cpg_totem_confchg_fn_t) (
	cpg_handle_t handle,
	struct cpg_ring_id ring_id,
	uint32_t member_list_entries,
	const uint32_t *member_list);

typedef struct {
	cpg_deliver_fn_t cpg_deliver_fn;
	cpg_confchg_fn_t cpg_confchg_fn;
} cpg_callbacks_t;

typedef struct {
	cpg_model_t model;
} cpg_model_data_t;

#define CPG_MODEL_V1_DELIVER_INITIAL_TOTEM_CONF 0x01

typedef struct {
	cpg_model_t model;
	cpg_deliver_fn_t cpg_deliver_fn;
	cpg_confchg_fn_t cpg_confchg_fn;
	cpg_totem_confchg_fn_t cpg_totem_confchg_fn;
	unsigned int flags;
} cpg_model_v1_data_t;


/** @} */

/**
 * Create a new cpg connection
 */
cs_error_t cpg_initialize (
	cpg_handle_t *handle,
	cpg_callbacks_t *callbacks);

/**
 * Create a new cpg connection, initialize with model
 */
cs_error_t cpg_model_initialize (
	cpg_handle_t *handle,
	cpg_model_t model,
	cpg_model_data_t *model_data,
	void *context);

/**
 * Close the cpg handle
 */
cs_error_t cpg_finalize (
	cpg_handle_t handle);

/**
 * Get a file descriptor on which to poll.
 *
 * cpg_handle_t is NOT a file descriptor and may not be used directly.
 */
cs_error_t cpg_fd_get (
	cpg_handle_t handle,
	int *fd);

/**
 * Get maximum size of a message that will not be fragmented
 */
cs_error_t cpg_max_atomic_msgsize_get (
	cpg_handle_t handle,
	uint32_t *size);

/**
 * Get contexts for a CPG handle
 */
cs_error_t cpg_context_get (
	cpg_handle_t handle,
	void **context);

/**
 * Set contexts for a CPG handle
 */
cs_error_t cpg_context_set (
	cpg_handle_t handle,
	void *context);


/**
 * Dispatch messages and configuration changes
 */
cs_error_t cpg_dispatch (
	cpg_handle_t handle,
	cs_dispatch_flags_t dispatch_types);

/**
 * Join one or more groups.
 *
 * messages multicasted with cpg_mcast_joined will be sent to every
 * group that has been joined on handle handle.  Any message multicasted
 * to a group that has been previously joined will be delivered in cpg_dispatch
 */
cs_error_t cpg_join (
	cpg_handle_t handle,
	const struct cpg_name *group);

/**
 * Leave one or more groups
 */
cs_error_t cpg_leave (
	cpg_handle_t handle,
	const struct cpg_name *group);

/**
 * Multicast to groups joined with cpg_join.
 *
 * @param handle
 * @param guarantee
 * @param iovec This iovec will be multicasted to all groups joined with
 *              the cpg_join interface for handle.
 * @param iov_len
 */
cs_error_t cpg_mcast_joined (
	cpg_handle_t handle,
	cpg_guarantee_t guarantee,
	const struct iovec *iovec,
	unsigned int iov_len);

/**
 * Get membership information from cpg
 */
cs_error_t cpg_membership_get (
	cpg_handle_t handle,
	struct cpg_name *groupName,
	struct cpg_address *member_list,
	int *member_list_entries);

cs_error_t cpg_local_get (
	cpg_handle_t handle,
	unsigned int *local_nodeid);

cs_error_t cpg_flow_control_state_get (
	cpg_handle_t handle,
	cpg_flow_control_state_t *flow_control_enabled);

cs_error_t cpg_zcb_alloc (
	cpg_handle_t handle,
	size_t size,
	void **buffer);

cs_error_t cpg_zcb_free (
	cpg_handle_t handle,
	void *buffer);

cs_error_t cpg_zcb_mcast_joined (
	cpg_handle_t handle,
	cpg_guarantee_t guarantee,
	void *msg,
	size_t msg_len);

/**
 * Iteration
 */
cs_error_t cpg_iteration_initialize(
	cpg_handle_t handle,
	cpg_iteration_type_t iteration_type,
	const struct cpg_name *group,
	cpg_iteration_handle_t *cpg_iteration_handle);

cs_error_t cpg_iteration_next(
	cpg_iteration_handle_t handle,
	struct cpg_iteration_description_t *description);

cs_error_t cpg_iteration_finalize (
	cpg_iteration_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_CPG_H_DEFINED */
