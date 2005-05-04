/*
 * Copyright (c) 2003-2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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

#include "totem.h"
#include "aispoll.h"

#define TOTEMSRP_PACKET_SIZE_MAX	1404

/*
 * Totem Single Ring Protocol
 * depends on poll abstraction, POSIX, IPV4
 */
/*
 * Initialize the logger
 */
void totemsrp_log_printf_init (
	void (*log_printf) (int , char *, ...),
	int log_level_security,
	int log_level_error,
	int log_level_warning,
	int log_level_notice,
	int log_level_debug);

/*
 * Initialize the group messaging interface
 */
int totemsrp_initialize (
	struct sockaddr_in *sockaddr_mcast,
	struct totem_interface *interfaces,
	int interface_count,
	poll_handle *poll_handle,
	unsigned char *private_key,
	int private_key_len,
	void *member_private,
	int member_private_len,

	void (*deliver_fn) (
		struct in_addr source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required),
	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		struct in_addr *member_list, int member_list_entries,
		struct in_addr *left_list, int left_list_entries,
		struct in_addr *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id),
	unsigned int *timeouts);

/*
 * Array location of various timeouts as
 * specified in openais.conf.  The last enum
 * specifies the size of the timeouts array and
 * needs to remain the last item in the list.
 */
enum {
	TOTEM_TOKEN,
	TOTEM_RETRANSMIT_TOKEN,
	TOTEM_JOIN,
	TOTEM_CONSENSUS,
	TOTEM_MERGE,
	TOTEM_DOWNCHECK,
	TOTEM_FAIL_RECV_CONST,

	MAX_TOTEM_TIMEOUTS	/* Last item */
} totem_timeout_types;

/*
 * Multicast a message
 */
int totemsrp_mcast (
	struct iovec *iovec,
	int iov_len,
	int priority);

/*
 * Return number of available messages that can be queued
 */
int totemsrp_avail (void);

int totemsrp_callback_token_create (
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, void *),
	void *data);

void totemsrp_callback_token_destroy (
	void **handle_out);

extern struct sockaddr_in config_mcast_addr;

#endif /* TOTEMSRP_H_DEFINED */
