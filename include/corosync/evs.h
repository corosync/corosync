/* * Copyright (c) 2004 MontaVista Software, Inc.
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
#ifndef COROSYNC_EVS_H_DEFINED
#define COROSYNC_EVS_H_DEFINED

#include <inttypes.h>
#include <netinet/in.h>
#include <corosync/corotypes.h>
#include <corosync/hdb.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup corosync Other API services provided by corosync
 */
/**
 * @addtogroup evs_corosync
 *
 * @{
 */

typedef uint64_t evs_handle_t;

typedef enum {
	EVS_TYPE_UNORDERED, /* not implemented */
	EVS_TYPE_FIFO,		/* same as agreed */
	EVS_TYPE_AGREED,
	EVS_TYPE_SAFE		/* not implemented */
} evs_guarantee_t;

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

/** These are the things that get passed around */
struct evs_group {
	char key[32];
};

struct evs_ring_id {
	unsigned int nodeid;
	unsigned long long seq;
};

typedef void (*evs_deliver_fn_t) (
	hdb_handle_t handle, 
	unsigned int nodeid,
	const void *msg,
	size_t msg_len);

typedef void (*evs_confchg_fn_t) (
	hdb_handle_t handle, 
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct evs_ring_id *ring_id);

typedef struct {
	evs_deliver_fn_t evs_deliver_fn;
	evs_confchg_fn_t evs_confchg_fn;
} evs_callbacks_t;

/** @} */

/*
 * Create a new evs connection
 */
cs_error_t evs_initialize (
	evs_handle_t *handle,
	evs_callbacks_t *callbacks);

/*
 * Close the evs handle
 */
cs_error_t evs_finalize (
	evs_handle_t handle);

/*
 * Get a file descriptor on which to poll.  evs_handle_t is NOT a
 * file descriptor and may not be used directly.
 */
cs_error_t evs_fd_get (
	evs_handle_t handle,
	int *fd);

/*
 * Get and set contexts for a EVS handle
 */
cs_error_t evs_context_get (
	evs_handle_t handle,
	void **context);

cs_error_t evs_context_set (
	evs_handle_t handle,
	void *context);

/*
 * Dispatch messages and configuration changes
 */
cs_error_t evs_dispatch (
	evs_handle_t handle,
	cs_dispatch_flags_t dispatch_types);

/*
 * Join one or more groups.
 * messages multicasted with evs_mcast_joined will be sent to every
 * group that has been joined on handle handle.  Any message multicasted
 * to a group that has been previously joined will be delivered in evs_dispatch
 */
cs_error_t evs_join (
	evs_handle_t handle,
	const struct evs_group *groups,
	size_t group_cnt);

/*
 * Leave one or more groups
 */
cs_error_t evs_leave (
	evs_handle_t handle,
	const struct evs_group *groups,
	size_t group_cnt);

/*
 * Multicast to groups joined with evs_join.
 * The iovec described by iovec will be multicasted to all groups joined with
 * the evs_join interface for handle.
 */
cs_error_t evs_mcast_joined (
	evs_handle_t handle,
	evs_guarantee_t guarantee,
	const struct iovec *iovec,
	unsigned int iov_len);

/*
 * Multicast to specified groups.
 * Messages will be multicast to groups specified in the api call and not those
 * that have been joined (unless they are in the groups parameter).
 */
cs_error_t evs_mcast_groups (
	evs_handle_t handle,
	evs_guarantee_t guarantee,
	const struct evs_group *groups,
	size_t group_cnt,
	const struct iovec *iovec,
	unsigned int iov_len);

/*
 * Get membership information from evs
 */
cs_error_t evs_membership_get (
	evs_handle_t handle,
	unsigned int *local_nodeid,
	unsigned int *member_list,
	size_t *member_list_entries);

#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_EVS_H_DEFINED */
