/*
 * Copyright (c) 2003-2005 MontaVista Software, Inc.
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
#ifndef TOTEMSRP_H_DEFINED
#define TOTEMSRP_H_DEFINED

#include <corosync/totem/totem.h>
#include <corosync/totem/coropoll.h>

/*
 * Totem Single Ring Protocol
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create a protocol instance
 */
int totemsrp_initialize (
	hdb_handle_t poll_handle,
	hdb_handle_t *handle,
	struct totem_config *totem_config,

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
		const struct memb_ring_id *ring_id));

void totemsrp_finalize (hdb_handle_t handle);

/*
 * Multicast a message
 */
int totemsrp_mcast (
	hdb_handle_t handle,
	struct iovec *iovec,
	unsigned int iov_len,
	int priority);

/*
 * Return number of available messages that can be queued
 */
int totemsrp_avail (hdb_handle_t handle);

int totemsrp_callback_token_create (
	hdb_handle_t handle,
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data);

void totemsrp_callback_token_destroy (
	hdb_handle_t handle,
	void **handle_out);

int totemsrp_new_msg_signal (hdb_handle_t handle);

extern void totemsrp_net_mtu_adjust (struct totem_config *totem_config);

extern int totemsrp_ifaces_get (
	hdb_handle_t handle,
	unsigned int nodeid,
	struct totem_ip_address *interfaces,
	char ***status,
	unsigned int *iface_count);

extern unsigned int totemsrp_my_nodeid_get (
	hdb_handle_t handle);

extern int totemsrp_my_family_get (
	hdb_handle_t handle);

extern int totemsrp_crypto_set (
	hdb_handle_t handle,
	unsigned int type);

extern int totemsrp_ring_reenable (
	hdb_handle_t handle);

#endif /* TOTEMSRP_H_DEFINED */
