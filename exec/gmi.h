/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
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
#ifndef GMI_H_DEFINED
#define GMI_H_DEFINED

#include "aispoll.h"

#define MESSAGE_SIZE_MAX	256000

#define GMI_PRIO_RECOVERY	0
#define GMI_PRIO_HIGH		1
#define GMI_PRIO_MED		2
#define GMI_PRIO_LOW		3

typedef int gmi_join_handle;

struct gmi_groupname {
	char groupname[16];
};

poll_handle *gmi_poll_handle;

/*
 * Group messaging interface
 * depends on poll abstraction, POSIX, IPV4 or IPV6
 */
/*
 * Initialize the logger
 */
void gmi_log_printf_init (
	void (*log_printf) (int , char *, ...),
	int log_level_security,
	int log_level_error,
	int log_level_warning,
	int log_level_notice,
	int log_level_debug);

/*
 * Initialize the group messaging interface
 */
int gmi_init (
	struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	poll_handle *poll_handle,
	struct sockaddr_in *bound_to,
	unsigned char *private_key,
	int private_key_len);


/*
 * Join a multicast group
 */
int gmi_join (
	struct gmi_groupname *groupname, 
	void (*deliver_fn) (
		struct gmi_groupname *groupname,
		struct in_addr source_addr,
		struct iovec *iovec,
		int iov_len),
	void (*confchg_fn) (
		struct sockaddr_in *member_list, int member_list_entries,
		struct sockaddr_in *left_list, int left_list_entries,
		struct sockaddr_in *joined_list, int joined_list_entries),
	gmi_join_handle *handle_out);

/*
 * Leave a multicast group
 */
int gmi_leave (
	gmi_join_handle handle_join);

/*
 * Multicast a message
 */
int gmi_mcast (
	struct gmi_groupname *groupname,
	struct iovec *iovec,
	int iov_len,
	int priority);

/*
 * Determine if a message of msg_size could be queued
 */
int gmi_send_ok (
	int priority,
	int msg_size);

#endif /* GMI_H_DEFINED */
