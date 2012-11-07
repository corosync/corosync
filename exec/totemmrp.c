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

#include <config.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>

#include <corosync/totem/totem.h>
#include <qb/qbloop.h>

#include "totemmrp.h"
#include "totemsrp.h"

void *totemsrp_context;

void totemmrp_deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required);

void totemmrp_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

void (*pg_deliver_fn) (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required) = 0;

void (*pg_confchg_fn) (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id) = 0;

void totemmrp_deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required)
{
	pg_deliver_fn (nodeid, msg, msg_len, endian_conversion_required);
}

void totemmrp_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	pg_confchg_fn (configuration_type,
		member_list, member_list_entries,
		left_list, left_list_entries,
		joined_list, joined_list_entries,
		ring_id);
}

/*
 * Initialize the totem multiple ring protocol
 */
int totemmrp_initialize (
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
		int waiting_trans_ack))
{
	int result;
	pg_deliver_fn = deliver_fn;
	pg_confchg_fn = confchg_fn;

	stats->mrp = calloc (sizeof(totemmrp_stats_t), 1);
	result = totemsrp_initialize (
		poll_handle,
		&totemsrp_context,
		totem_config,
		stats->mrp,
		totemmrp_deliver_fn,
		totemmrp_confchg_fn,
		waiting_trans_ack_cb_fn);

	return (result);
}

void totemmrp_finalize (void)
{
	totemsrp_finalize (totemsrp_context);
}

/*
 * Multicast a message
 */
int totemmrp_mcast (
	struct iovec *iovec,
	unsigned int iov_len,
	int priority)
{
	return totemsrp_mcast (totemsrp_context, iovec, iov_len, priority);
}

/*
 * Return number of available messages that can be queued
 */
int totemmrp_avail (void)
{
	return (totemsrp_avail (totemsrp_context));
}

int totemmrp_callback_token_create (
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data)
{
	return totemsrp_callback_token_create (totemsrp_context, handle_out, type, delete, callback_fn, data);
}

void totemmrp_callback_token_destroy (
	void *handle_out)
{
	totemsrp_callback_token_destroy (totemsrp_context, handle_out);
}

void totemmrp_event_signal (enum totem_event_type type, int value)
{
	totemsrp_event_signal (totemsrp_context, type, value);
}

int totemmrp_ifaces_get (
	unsigned int nodeid,
	struct totem_ip_address *interfaces,
	unsigned int interfaces_size,
	char ***status,
	unsigned int *iface_count)
{
	int res;

	res = totemsrp_ifaces_get (
		totemsrp_context,
		nodeid,
		interfaces,
		interfaces_size,
		status,
		iface_count);

	return (res);
}

int totemmrp_crypto_set (
	const char *cipher_type,
	const char *hash_type)
{
	return totemsrp_crypto_set (totemsrp_context,
				    cipher_type,
				    hash_type);
}

unsigned int totemmrp_my_nodeid_get (void)
{
	return (totemsrp_my_nodeid_get (totemsrp_context));
}

int totemmrp_my_family_get (void)
{
	return (totemsrp_my_family_get (totemsrp_context));
}

extern int totemmrp_ring_reenable (void)
{
	int res;

	res = totemsrp_ring_reenable (
		totemsrp_context);

	return (res);
}

extern void totemmrp_service_ready_register (
        void (*totem_service_ready) (void))
{
	totemsrp_service_ready_register (
		totemsrp_context,
		totem_service_ready);
}

int totemmrp_member_add (
        const struct totem_ip_address *member,
        int ring_no)
{
	int res;

	res = totemsrp_member_add (totemsrp_context, member, ring_no);

	return (res);
}

int totemmrp_member_remove (
       const struct totem_ip_address *member,
        int ring_no)
{
	int res;

	res = totemsrp_member_remove (totemsrp_context, member, ring_no);

	return (res);
}

void totemmrp_threaded_mode_enable (void)
{
	totemsrp_threaded_mode_enable (totemsrp_context);
}

void totemmrp_trans_ack (void)
{
	totemsrp_trans_ack (totemsrp_context);
}
