/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2011 Red Hat, Inc.
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
#ifndef TOTEMKNET_H_DEFINED
#define TOTEMKNET_H_DEFINED

#include <sys/types.h>
#include <sys/socket.h>
#include <qb/qbloop.h>

#include <corosync/totem/totem.h>

/**
 * Create an instance
 */
extern int totemknet_initialize (
	qb_loop_t *poll_handle,
	void **knet_context,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	void *context,

	int (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len,
		const struct sockaddr_storage *system_from),

	int (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address,
		unsigned int ring_no),

	void (*mtu_changed) (
		void *context,
		int net_mtu),

	void (*target_set_completed) (
		void *context));

extern void *totemknet_buffer_alloc (void);

extern void totemknet_buffer_release (void *ptr);

extern int totemknet_processor_count_set (
	void *knet_context,
	int processor_count);

extern int totemknet_token_send (
	void *knet_context,
	const void *msg,
	unsigned int msg_len);

extern int totemknet_mcast_flush_send (
	void *knet_context,
	const void *msg,
	unsigned int msg_len);

extern int totemknet_mcast_noflush_send (
	void *knet_context,
	const void *msg,
	unsigned int msg_len);

extern int totemknet_recv_flush (void *knet_context);

extern int totemknet_send_flush (void *knet_context);

extern int totemknet_iface_check (void *knet_context);

extern int totemknet_finalize (void *knet_context);

extern void totemknet_net_mtu_adjust (void *knet_context, struct totem_config *totem_config);

extern int totemknet_nodestatus_get (void *knet_context, unsigned int nodeid,
				     struct totem_node_status *node_status);

extern int totemknet_ifaces_get (void *net_context,
	char ***status,
	unsigned int *iface_count);

extern int totemknet_iface_set (void *net_context,
	const struct totem_ip_address *local_addr,
	unsigned short ip_port,
	unsigned int iface_no);

extern int totemknet_token_target_set (
	void *knet_context,
	unsigned int nodeid);

extern int totemknet_crypto_set (
	void *knet_context,
	const char *cipher_type,
	const char *hash_type);

extern int totemknet_recv_mcast_empty (
	void *knet_context);

extern int totemknet_member_add (
	void *knet_context,
	const struct totem_ip_address *local,
	const struct totem_ip_address *member,
	int ring_no);

extern int totemknet_member_remove (
	void *knet_context,
	const struct totem_ip_address *member,
	int ring_no);

extern int totemknet_member_set_active (
	void *knet_context,
	const struct totem_ip_address *member_ip,
	int active);

extern int totemknet_reconfigure (
	void *knet_context,
	struct totem_config *totem_config);

extern int totemknet_crypto_reconfigure_phase (
	void *knet_context,
	struct totem_config *totem_config,
	cfg_message_crypto_reconfig_phase_t phase);

extern void totemknet_stats_clear (
	void *knet_context);

extern void totemknet_configure_log_level (void);

#endif /* TOTEMKNET_H_DEFINED */
