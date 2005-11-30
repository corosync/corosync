/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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
#ifndef TOTEMRRP_H_DEFINED
#define TOTEMRRP_H_DEFINED

#include <sys/types.h>
#include <sys/socket.h>

#include "totem.h"
#include "aispoll.h"

typedef unsigned int totemrrp_handle;

#define TOTEMRRP_NOFLUSH	0
#define TOTEMRRP_FLUSH		1

/*
 * Totem Network interface - also does encryption/decryption
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
extern int totemrrp_initialize (
	poll_handle poll_handle,
	totemrrp_handle *handle,
	struct totem_config *totem_config,
	void *context,

	void (*deliver_fn) (
		void *context,
		struct totem_ip_address *system_from,
		void *msg,
		int msg_len),

	void (*iface_change_fn) (
		void *context,
		struct totem_ip_address *iface_addr),

	void (*token_seqid_get) (
		void *msg,
		unsigned int *seqid,
		unsigned int *token_is));

extern int totemrrp_processor_count_set (
	totemrrp_handle handle,
	int processor_count);

extern int totemrrp_token_send (
	totemrrp_handle handle,
	struct totem_ip_address *system_to,
	void *msg,
	int msg_len);

extern int totemrrp_mcast_noflush_send (
	totemrrp_handle handle,
	struct iovec *iovec,
	int iov_len);

extern int totemrrp_mcast_flush_send (
	totemrrp_handle handle,
	void *msg,
	int msg_len);

extern int totemrrp_recv_flush (totemrrp_handle handle);

extern int totemrrp_send_flush (totemrrp_handle handle);

extern int totemrrp_iface_check (totemrrp_handle handle);

extern int totemrrp_finalize (totemrrp_handle handle);

#endif /* TOTEMRRP_H_DEFINED */
