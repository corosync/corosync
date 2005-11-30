/*
 * Copyright (c) 2004 MontaVista Software, Inc.
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
#ifndef OPENAIS_EVS_H_DEFINED
#define OPENAIS_EVS_H_DEFINED

#include <netinet/in.h>

typedef uint64_t evs_handle_t;

typedef enum {
	EVS_DISPATCH_ONE,
	EVS_DISPATCH_ALL,
	EVS_DISPATCH_BLOCKING
} evs_dispatch_t;

typedef enum {
	EVS_TYPE_UNORDERED, /* not implemented */
	EVS_TYPE_FIFO,		/* same as agreed */
	EVS_TYPE_AGREED,
	EVS_TYPE_SAFE		/* not implemented */
} evs_guarantee_t;

typedef enum {
	EVS_OK = 1,
	EVS_ERR_LIBRARY = 2,
	EVS_ERR_TIMEOUT = 5,
	EVS_ERR_TRY_AGAIN = 6,
	EVS_ERR_INVALID_PARAM = 7,
	EVS_ERR_NO_MEMORY = 8,
	EVS_ERR_BAD_HANDLE = 9,
	EVS_ERR_ACCESS = 11,
	EVS_ERR_NOT_EXIST = 12,
	EVS_ERR_EXIST = 14,
	EVS_ERR_NOT_SUPPORTED = 20,
	EVS_ERR_SECURITY = 29,
	EVS_ERR_TOO_MANY_GROUPS=30
} evs_error_t;

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

/* These are the things that get passed around */
struct evs_address {
	unsigned int nodeid;
	unsigned short family;
	unsigned char addr[TOTEMIP_ADDRLEN];
};

struct evs_group {
	char key[32];
};

typedef void (*evs_deliver_fn_t) (
	struct evs_address *source_addr,
	void *msg,
	int msg_len);

typedef void (*evs_confchg_fn_t) (
	struct evs_address *member_list, int member_list_entries,
	struct evs_address *left_list, int left_list_entries,
	struct evs_address *joined_list, int joined_list_entries);

typedef struct {
	evs_deliver_fn_t evs_deliver_fn;
	evs_confchg_fn_t evs_confchg_fn;
} evs_callbacks_t;

/*
 * Create a new evs connection
 */
evs_error_t evs_initialize (
	evs_handle_t *handle,
	evs_callbacks_t *callbacks);

/*
 * Close the evs handle
 */
evs_error_t evs_finalize (
	evs_handle_t handle);

/*
 * Get a file descriptor on which to poll.  evs_handle_t is NOT a
 * file descriptor and may not be used directly.
 */
evs_error_t evs_fd_get (
	evs_handle_t handle,
	int *fd);

/*
 * Dispatch messages and configuration changes
 */
evs_error_t evs_dispatch (
	evs_handle_t handle,
	evs_dispatch_t dispatch_types);

/*
 * Join one or more groups.
 * messages multicasted with evs_mcast_joined will be sent to every
 * group that has been joined on handle handle.  Any message multicasted
 * to a group that has been previously joined will be delivered in evs_dispatch
 */
evs_error_t evs_join (
	evs_handle_t handle,
	struct evs_group *groups,
	int group_cnt);

/*
 * Leave one or more groups
 */
evs_error_t evs_leave (
	evs_handle_t handle,
	struct evs_group *groups,
	int group_cnt);

/*
 * Multicast to groups joined with evs_join.
 * The iovec described by iovec will be multicasted to all groups joined with
 * the evs_join interface for handle.
 */
evs_error_t evs_mcast_joined (
	evs_handle_t handle,
	evs_guarantee_t guarantee,
	struct iovec *iovec,
	int iov_len);

/*
 * Multicast to specified groups.
 * Messages will be multicast to groups specified in the api call and not those
 * that have been joined (unless they are in the groups parameter).
 */
evs_error_t evs_mcast_groups (
	evs_handle_t handle,
	evs_guarantee_t guarantee,
	struct evs_group *groups,
	int group_cnt,
	struct iovec *iovec,
	int iov_len);

/*
 * Get membership information from evs
 */
evs_error_t evs_membership_get (
	evs_handle_t handle,
	struct evs_address *local_addr,
	struct evs_address *member_list,
	int *member_list_entries);

#endif /* OPENAIS_EVS_H_DEFINED */
