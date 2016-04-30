/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2013 Red Hat, Inc.
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
#ifndef COROSYNC_CFG_H_DEFINED
#define COROSYNC_CFG_H_DEFINED

#include <netinet/in.h>
#include <corosync/corotypes.h>

typedef uint64_t corosync_cfg_handle_t;

/**
 * Shutdown types.
 */
typedef enum {
	/**
	 * REQUEST is the normal shutdown.
	 *        Other daemons will be consulted.
	 */
	COROSYNC_CFG_SHUTDOWN_FLAG_REQUEST = 0,
	/**
	 * REGARDLESS will tell other daemons but ignore their opinions.
	 */
	COROSYNC_CFG_SHUTDOWN_FLAG_REGARDLESS = 1,
	/**
	 * IMMEDIATE will shut down straight away
	 *        (but still tell other nodes).
	 */
	COROSYNC_CFG_SHUTDOWN_FLAG_IMMEDIATE = 2,
} corosync_cfg_shutdown_flags_t;

/**
 * @brief enum corosync_cfg_shutdown_reply_flags_t
 */
typedef enum {
	COROSYNC_CFG_SHUTDOWN_FLAG_NO = 0,
	COROSYNC_CFG_SHUTDOWN_FLAG_YES = 1,
} corosync_cfg_shutdown_reply_flags_t;

/**
 * @brief corosync_cfg_shutdown_callback_t callback
 */
typedef void (*corosync_cfg_shutdown_callback_t) (
	corosync_cfg_handle_t cfg_handle,
	corosync_cfg_shutdown_flags_t flags);

/**
 * @brief struct corosync_cfg_shutdown_callback_t
 */
typedef struct {
	corosync_cfg_shutdown_callback_t corosync_cfg_shutdown_callback;
} corosync_cfg_callbacks_t;

/**
 * A node address. This is a complete sockaddr_in[6]
 *
 * To explain:
 *  If you cast cna_address to a 'struct sockaddr', the sa_family field
 *  will be AF_INET or AF_INET6. Armed with that knowledge you can then
 *  cast it to a sockaddr_in or sockaddr_in6 and pull out the address.
 *  No other sockaddr fields are valid.
 *  Also, you must ignore any part of the sockaddr beyond the length supplied
 */
typedef struct
{
	int address_length; /**< @todo FIXME: set but never used */
	char address[sizeof(struct sockaddr_in6)];
} corosync_cfg_node_address_t;

/*
 * Interfaces
 */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief corosync_cfg_initialize
 * @param cfg_handle
 * @param cfg_callbacks
 * @return
 */
cs_error_t
corosync_cfg_initialize (
	corosync_cfg_handle_t *cfg_handle,
	const corosync_cfg_callbacks_t *cfg_callbacks);

/**
 * @brief corosync_cfg_fd_get
 * @param cfg_handle
 * @param selection_fd
 * @return
 */
cs_error_t
corosync_cfg_fd_get (
	corosync_cfg_handle_t cfg_handle,
	int32_t *selection_fd);

/**
 * @brief corosync_cfg_dispatch
 * @param cfg_handle
 * @param dispatch_flags
 * @return
 */
cs_error_t
corosync_cfg_dispatch (
	corosync_cfg_handle_t cfg_handle,
	cs_dispatch_flags_t dispatch_flags);

/**
 * @brief corosync_cfg_finalize
 * @param cfg_handle
 * @return
 */
cs_error_t
corosync_cfg_finalize (
	corosync_cfg_handle_t cfg_handle);

/**
 * @brief corosync_cfg_ring_status_get
 * @param cfg_handle
 * @param interface_names
 * @param status
 * @param interface_count
 * @return
 */
cs_error_t
corosync_cfg_ring_status_get (
	corosync_cfg_handle_t cfg_handle,
	char ***interface_names,
	char ***status,
	unsigned int *interface_count);

/**
 * @brief corosync_cfg_ring_reenable
 * @param cfg_handle
 * @return
 */
cs_error_t
corosync_cfg_ring_reenable (
	corosync_cfg_handle_t cfg_handle);

/**
 * @brief corosync_cfg_kill_node
 * @param cfg_handle
 * @param nodeid
 * @param reason
 * @return
 */
cs_error_t
corosync_cfg_kill_node (
	corosync_cfg_handle_t cfg_handle,
	unsigned int nodeid,
	const char *reason);

/**
 * @brief corosync_cfg_try_shutdown
 * @param cfg_handle
 * @param flags
 * @return
 */
cs_error_t
corosync_cfg_try_shutdown (
	corosync_cfg_handle_t cfg_handle,
	corosync_cfg_shutdown_flags_t flags);

/**
 * @brief corosync_cfg_replyto_shutdown
 * @param cfg_handle
 * @param flags
 * @return
 */
cs_error_t
corosync_cfg_replyto_shutdown (
	corosync_cfg_handle_t cfg_handle,
	corosync_cfg_shutdown_reply_flags_t flags);

/**
 * @brief corosync_cfg_get_node_addrs
 * @param cfg_handle
 * @param nodeid
 * @param max_addrs
 * @param num_addrs
 * @param addrs
 * @return
 */
cs_error_t
corosync_cfg_get_node_addrs (
	corosync_cfg_handle_t cfg_handle,
	int nodeid,
	size_t max_addrs,
	int *num_addrs,
	corosync_cfg_node_address_t *addrs);

/**
 * @brief corosync_cfg_local_get
 * @param handle
 * @param local_nodeid
 * @return
 */
cs_error_t
corosync_cfg_local_get (
	corosync_cfg_handle_t handle,
	unsigned int *local_nodeid);

/**
 * @brief corosync_cfg_reload_config
 * @param handle
 * @return
 */
cs_error_t corosync_cfg_reload_config (
	corosync_cfg_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_CFG_H_DEFINED */
