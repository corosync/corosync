/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2012 Red Hat, Inc.
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

/**
 * @file
 *
 * @warning DO NOT USE SYMBOLS IN THIS FILE
 */

#ifndef MAIN_H_DEFINED
#define MAIN_H_DEFINED

#define TRUE 1
#define FALSE 0
#include <corosync/corotypes.h>
#include <corosync/hdb.h>
#include <qb/qbloop.h>
#include <corosync/totem/totempg.h>
#include <corosync/icmap.h>
#include <corosync/coroapi.h>

extern unsigned long long *(*main_clm_get_by_nodeid) (unsigned int node_id);

extern int main_mcast (
	const struct iovec *iovec,
	unsigned int iov_len,
	unsigned int guarantee);

extern void message_source_set (mar_message_source_t *source, void *conn);

extern int message_source_is_local (const mar_message_source_t *source);

extern void corosync_shutdown_request (void);

extern void corosync_state_dump (void);

extern qb_loop_t *cs_poll_handle_get (void);

extern int cs_poll_dispatch_add (qb_loop_t * handle,
		int fd,
		int events,
		void *data,

		int (*dispatch_fn) (int fd,
			int revents,
			void *data));

extern int cs_poll_dispatch_delete (
		qb_loop_t * handle,
		int fd);


extern int corosync_sending_allowed (
	unsigned int service,
	unsigned int id,
	const void *msg,
	void *sending_allowed_private_data);

extern void corosync_sending_allowed_release (void *sending_allowed_private_data);

extern void corosync_recheck_the_q_level(void *data);

extern void cs_ipcs_init(void);

extern const char *cs_ipcs_service_init(struct corosync_service_engine *service);

extern void cs_ipcs_stats_update(void);

extern int32_t cs_ipcs_service_destroy(int32_t service_id);

extern int32_t cs_ipcs_q_level_get(void);

extern int cs_ipcs_dispatch_send(void *conn, const void *msg, size_t mlen);
extern int cs_ipcs_dispatch_iov_send (void *conn,
	const struct iovec *iov,
	unsigned int iov_len);

extern int cs_ipcs_response_send(void *conn, const void *msg, size_t mlen);
extern int cs_ipcs_response_iov_send (void *conn,
	const struct iovec *iov,
	unsigned int iov_len);

extern void cs_ipcs_sync_state_changed(int32_t sync_in_process);

extern void *cs_ipcs_private_data_get(void *conn);

extern void cs_ipc_refcnt_inc(void *conn);

extern void cs_ipc_refcnt_dec(void *conn);

extern void cs_ipc_allow_connections(int32_t allow);

int coroparse_configparse (icmap_map_t config_map, const char **error_string);

#endif /* MAIN_H_DEFINED */
