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

#ifndef TOTEMRRP_H_DEFINED
#define TOTEMRRP_H_DEFINED

#include <sys/types.h>
#include <sys/socket.h>
#include <qb/qbloop.h>
#include <corosync/totem/totem.h>

#define TOTEMRRP_NOFLUSH	0
#define TOTEMRRP_FLUSH		1

/*
 * SRP address. Used mainly in totemsrp.c, but we need it here to inform RRP about
 * membership change.
 */
struct srp_addr {
	uint8_t no_addrs;
	struct totem_ip_address addr[INTERFACE_MAX];
};

/**
 * Create an instance
 */
extern int totemrrp_initialize (
	qb_loop_t *poll_handle,
	void **rrp_context,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_addr,
		unsigned int iface_no),

	void (*token_seqid_get) (
		const void *msg,
		unsigned int *seqid,
		unsigned int *token_is),

	unsigned int (*msgs_missing) (void),

	void (*target_set_completed) (
		void *context)
	);

extern void *totemrrp_buffer_alloc (
	void *rrp_context);

extern void totemrrp_buffer_release (
	void *rrp_context,
	void *ptr);

extern int totemrrp_processor_count_set (
	void *rrp_context,
	unsigned int processor_count);

extern int totemrrp_token_send (
	void *rrp_context,
	const void *msg,
	unsigned int msg_len);

extern int totemrrp_mcast_noflush_send (
	void *rrp_context,
	const void *msg,
	unsigned int msg_len);

extern int totemrrp_mcast_flush_send (
	void *rrp_context,
	const void *msg,
	unsigned int msg_len);

extern int totemrrp_recv_flush (
	void *rrp_context);

extern int totemrrp_send_flush (
	void *rrp_context);

extern int totemrrp_token_target_set (
	void *rrp_context,
	struct totem_ip_address *target,
	unsigned int iface_no);

extern int totemrrp_iface_check (void *rrp_context);

extern int totemrrp_finalize (void *rrp_context);

extern int totemrrp_ifaces_get (
	void *rrp_context,
	char ***status,
	unsigned int *iface_count);

extern int totemrrp_crypto_set (
	void *rrp_context,
	const char *cipher_type,
	const char *hash_type);

extern int totemrrp_ring_reenable (
	void *rrp_context,
	unsigned int iface_no);

extern int totemrrp_mcast_recv_empty (
	void *rrp_context);

extern int totemrrp_member_add (
        void *net_context,
        const struct totem_ip_address *member,
	int iface_no);

extern int totemrrp_member_remove (
        void *net_context,
        const struct totem_ip_address *member,
	int iface_no);

extern void totemrrp_membership_changed (
	void *rrp_context,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

#endif /* TOTEMRRP_H_DEFINED */
