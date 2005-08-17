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
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>

#include "totem.h"
#include "totemsrp.h"
#include "aispoll.h"

totemsrp_handle totemsrp_handle_in;

void (*pg_deliver_fn) (
	struct in_addr source_addr,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required) = 0;

void (*pg_confchg_fn) (
	enum totem_configuration_type configuration_type,
	struct in_addr *member_list, int member_list_entries,
	struct in_addr *left_list, int left_list_entries,
	struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id) = 0;

void totemmrp_deliver_fn (
	struct in_addr source_addr,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required)
{
	pg_deliver_fn (source_addr, iovec, iov_len, endian_conversion_required);
}

void totemmrp_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct in_addr *member_list, int member_list_entries,
	struct in_addr *left_list, int left_list_entries,
	struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
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
	poll_handle poll_handle,
	totemsrp_handle *totemsrp_handle,
	struct totem_config *totem_config,

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
		struct memb_ring_id *ring_id))
{
	int result;
	pg_deliver_fn = deliver_fn;
	pg_confchg_fn = confchg_fn;

	result = totemsrp_initialize (
		poll_handle,
		&totemsrp_handle_in,
		totem_config,
		totemmrp_deliver_fn,
		totemmrp_confchg_fn);

	*totemsrp_handle = totemsrp_handle_in;

	return (result);
}

void totemmrp_finalize (void)
{
	totemsrp_finalize (totemsrp_handle_in);
}

/*
 * Multicast a message
 */
int totemmrp_mcast (
	struct iovec *iovec,
	int iov_len,
	int priority)
{
	return totemsrp_mcast (totemsrp_handle_in, iovec, iov_len, priority);
}

/*
 * Return number of available messages that can be queued
 */
int totemmrp_avail (void)
{
	return (totemsrp_avail (totemsrp_handle_in));
}

int totemmrp_callback_token_create (
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, void *),
	void *data)
{
	return totemsrp_callback_token_create (totemsrp_handle_in, handle_out, type, delete, callback_fn, data);
}

void totemmrp_callback_token_destroy (
	void **handle_out)
{
	totemsrp_callback_token_destroy (totemsrp_handle_in, handle_out);
}

void totemmrp_new_msg_signal (void) {
	totemsrp_new_msg_signal (totemsrp_handle_in);
}
