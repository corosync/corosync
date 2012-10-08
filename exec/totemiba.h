/*
 * Copyright (c) 2009-2011 Red Hat, Inc.
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
#ifndef TOTEMIBA_H_DEFINED
#define TOTEMIBA_H_DEFINED

#include <sys/types.h>
#include <sys/socket.h>
#include <corosync/hdb.h>
#include <qb/qbloop.h>

#include <corosync/totem/totem.h>

/**
 * Create an instance
 */
extern int totemiba_initialize (
	qb_loop_t* qb_poll_handle,
	void **iba_handle,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	int interface_no,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address),

	void (*target_set_completed) (
		void *context));

extern void *totemiba_buffer_alloc (void);

extern void totemiba_buffer_release (void *ptr);

extern int totemiba_processor_count_set (
	void *iba_context,
	int processor_count);

extern int totemiba_token_send (
	void *iba_context,
	const void *msg,
	unsigned int msg_len);

extern int totemiba_mcast_flush_send (
	void *iba_context,
	const void *msg,
	unsigned int msg_len);

extern int totemiba_mcast_noflush_send (
	void *iba_context,
	const void *msg,
	unsigned int msg_len);

extern int totemiba_recv_flush (void *iba_context);

extern int totemiba_send_flush (void *iba_context);

extern int totemiba_iface_check (void *iba_context);

extern int totemiba_finalize (void *iba_context);

extern void totemiba_net_mtu_adjust (void *iba_context, struct totem_config *totem_config);

extern const char *totemiba_iface_print (void *iba_context);

extern int totemiba_iface_get (
	void *iba_context,
	struct totem_ip_address *addr);

extern int totemiba_token_target_set (
	void *iba_context,
	const struct totem_ip_address *token_target);

extern int totemiba_crypto_set (
	void *iba_context,
	const char *cipher_type,
	const char *hash_type);

extern int totemiba_recv_mcast_empty (
	void *iba_context);

#endif /* TOTEMIBA_H_DEFINED */
