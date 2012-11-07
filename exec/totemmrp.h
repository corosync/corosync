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

/**
 * @file
 * Totem Single Ring Protocol
 *
 * depends on poll abstraction, POSIX, IPV4
 */

#ifndef TOTEMMRP_H_DEFINED
#define TOTEMMRP_H_DEFINED

#include <corosync/totem/totem.h>

/**
 * Initialize the logger
 */
extern void totemmrp_log_printf_init (
	void (*log_printf) (int , char *, ...),
	int log_level_security,
	int log_level_error,
	int log_level_warning,
	int log_level_notice,
	int log_level_debug);

/**
 * Initialize the group messaging interface
 */
extern int totemmrp_initialize (
	qb_loop_t *poll_handle,
	struct totem_config *totem_config,
	totempg_stats_t *stats,

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

extern void totemmrp_finalize (void);

/**
 * Multicast a message
 */
extern int totemmrp_mcast (
	struct iovec *iovec,
	unsigned int iov_len,
	int priority);

/**
 * Return number of available messages that can be queued
 */
extern int totemmrp_avail (void);

extern int totemmrp_callback_token_create (
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data);

extern void totemmrp_callback_token_destroy (
	void *handle_out);

extern void totemmrp_event_signal (enum totem_event_type type, int value);

extern int totemmrp_ifaces_get (
	unsigned int nodeid,
	struct totem_ip_address *interfaces,
	unsigned int interfaces_size,
	char ***status,
	unsigned int *iface_count);

extern unsigned int totemmrp_my_nodeid_get (void);

extern int totemmrp_my_family_get (void);

extern int totemmrp_crypto_set (const char *cipher_type, const char *hash_type);

extern int totemmrp_ring_reenable (void);

extern void totemmrp_service_ready_register (
        void (*totem_service_ready) (void));

extern int totemmrp_member_add (
	const struct totem_ip_address *member,
	int ring_no);

extern int totemmrp_member_remove (
	const struct totem_ip_address *member,
	int ring_no);

void totemmrp_threaded_mode_enable (void);

void totemmrp_trans_ack (void);

#endif /* TOTEMMRP_H_DEFINED */
