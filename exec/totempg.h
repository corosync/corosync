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

#ifndef TOTEMPG_H_DEFINED
#define TOTEMPG_H_DEFINED

#include <netinet/in.h>
#include "aispoll.h"
#include "totem.h"

typedef unsigned int totempg_groups_handle;

struct totempg_group {
	void *group;
	int group_len;
};

#define TOTEMPG_AGREED			0
#define TOTEMPG_SAFE			1

/*
 * Totem Single Ring Protocol
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Initialize the totem process groups abstraction
 */
extern int totempg_initialize (
	poll_handle poll_handle,
	struct totem_config *totem_config
);

extern void totempg_finalize (void);

extern int totempg_callback_token_create (void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, void *),
	void *data);

extern void totempg_callback_token_destroy (void *handle);

/*
 * Initialize a groups instance
 */
extern int totempg_groups_initialize (
	totempg_groups_handle *handle,

	void (*deliver_fn) (
		unsigned int nodeid,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required),

	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		unsigned int *member_list, int member_list_entries,
		unsigned int *left_list, int left_list_entries,
		unsigned int *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id));

extern int totempg_groups_finalize (
	totempg_groups_handle handle);

extern int totempg_groups_join (
	totempg_groups_handle handle,
	struct totempg_group *groups,
	int gruop_cnt);

extern int totempg_groups_leave (
	totempg_groups_handle handle,
	struct totempg_group *groups,
	int gruop_cnt);

extern int totempg_groups_mcast_joined (
	totempg_groups_handle handle,
	struct iovec *iovec,
	int iov_len,
	int guarantee);

extern int totempg_groups_send_ok_joined (
	totempg_groups_handle handle,
	struct iovec *iovec,
	int iov_len);
	
extern int totempg_groups_mcast_groups (
	totempg_groups_handle handle,
	int guarantee,
	struct totempg_group *groups,
	int groups_cnt,
	struct iovec *iovec,
	int iov_len);

extern int totempg_groups_send_ok_groups (
	totempg_groups_handle handle,
	struct totempg_group *groups,
	int groups_cnt,
	struct iovec *iovec,
	int iov_len);
	
extern int totempg_ifaces_get (
	unsigned int nodeid,
        struct totem_ip_address *interfaces,
        unsigned int *iface_count);

extern char *totempg_ifaces_print (unsigned int nodeid);

#endif /* TOTEMPG_H_DEFINED */
