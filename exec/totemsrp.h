/*
 * Copyright (c) 2003-2005 MontaVista Software, Inc.
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

/**
 * @file
 * Totem Single Ring Protocol
 *
 * depends on poll abstraction, POSIX, IPV4
 */

#ifndef TOTEMSRP_H_DEFINED
#define TOTEMSRP_H_DEFINED

#include <corosync/totem/totem.h>
#include <qb/qbloop.h>

/**
 * Create a protocol instance
 */
int totemsrp_initialize (
	qb_loop_t *poll_handle,
	void **srp_context,
	struct totem_config *totem_config,
	totemmrp_stats_t *stats,

	void (*deliver_fn) (
		unsigned int nodeid,
		const void *msg,
		unsigned int msg_len,
		int endian_conversion_required),
	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		const unsigned int *member_list, size_t member_list_entries,
		const unsigned int *left_list, size_t left_list_entries,
		const unsigned int *joined_list, size_t joined_list_entries,
		const struct memb_ring_id *ring_id),
	void (*waiting_trans_ack_cb_fn) (
		int waiting_trans_ack));

void totemsrp_finalize (void *srp_context);

/**
 * Multicast a message
 */
int totemsrp_mcast (
	void *srp_context,
	struct iovec *iovec,
	unsigned int iov_len,
	int priority);

/**
 * Return number of available messages that can be queued
 */
int totemsrp_avail (void *srp_context);

int totemsrp_callback_token_create (
	void *srp_context,
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data);

void totemsrp_callback_token_destroy (
	void *srp_context,
	void **handle_out);

void totemsrp_event_signal (void *srp_context, enum totem_event_type type, int value);

extern void totemsrp_net_mtu_adjust (struct totem_config *totem_config);

extern int totemsrp_ifaces_get (
	void *srp_context,
	unsigned int nodeid,
	struct totem_ip_address *interfaces,
	unsigned int interfaces_size,
	char ***status,
	unsigned int *iface_count);

extern unsigned int totemsrp_my_nodeid_get (
	void *srp_context);

extern int totemsrp_my_family_get (
	void *srp_context);

extern int totemsrp_crypto_set (
	void *srp_context,
	const char *cipher_type,
	const char *hash_type);

extern int totemsrp_ring_reenable (
	void *srp_context);

void totemsrp_service_ready_register (
	void *srp_context,
	void (*totem_service_ready) (void));

extern int totemsrp_member_add (
	void *srp_context,
	const struct totem_ip_address *member,
	int ring_no);
	
extern int totemsrp_member_remove (
	void *srp_context,
	const struct totem_ip_address *member,
	int ring_no);
	
void totemsrp_threaded_mode_enable (
	void *srp_context);

void totemsrp_trans_ack (
	void *srp_context);

#endif /* TOTEMSRP_H_DEFINED */
