/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2007, 2009 Red Hat, Inc.
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
 * Totem Network interface - also does encryption/decryption
 *
 * depends on poll abstraction, POSIX, IPV4
 */

#ifndef TOTEMNET_H_DEFINED
#define TOTEMNET_H_DEFINED

#include <sys/types.h>
#include <sys/socket.h>

#include <corosync/totem/totem.h>

#define TOTEMNET_NOFLUSH	0
#define TOTEMNET_FLUSH		1

/**
 * Create an instance
 */
extern int totemnet_initialize (
	qb_loop_t *poll_handle,
	void **net_context,
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

extern void *totemnet_buffer_alloc (void *net_context);

extern void totemnet_buffer_release (void *net_context, void *ptr);

extern int totemnet_processor_count_set (
	void *net_context,
	int processor_count);

extern int totemnet_token_send (
	void *net_context,
	const void *msg,
	unsigned int msg_len);

extern int totemnet_mcast_flush_send (
	void *net_context,
	const void *msg,
	unsigned int msg_len);

extern int totemnet_mcast_noflush_send (
	void *net_context,
	const void *msg,
	unsigned int msg_len);

extern int totemnet_recv_flush (void *net_context);

extern int totemnet_send_flush (void *net_context);

extern int totemnet_iface_check (void *net_context);

extern int totemnet_finalize (void *net_context);

extern int totemnet_net_mtu_adjust (void *net_context, struct totem_config *totem_config);

extern const char *totemnet_iface_print (void *net_context);

extern int totemnet_iface_get (
	void *net_context,
	struct totem_ip_address *addr);

extern int totemnet_token_target_set (
	void *net_context,
	const struct totem_ip_address *token_target);

extern int totemnet_crypto_set (
	void *net_context,
	const char *cipher_type,
	const char *hash_type);

extern int totemnet_recv_mcast_empty (
	void *net_context);

extern int totemnet_member_add (
	void *net_context,
	const struct totem_ip_address *member);

extern int totemnet_member_remove (
	void *net_context,
	const struct totem_ip_address *member);

extern int totemnet_member_set_active (
	void *net_context,
	const struct totem_ip_address *member,
	int active);

#endif /* TOTEMNET_H_DEFINED */
