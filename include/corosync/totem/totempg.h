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

#ifndef TOTEMPG_H_DEFINED
#define TOTEMPG_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <netinet/in.h>
#include "totem.h"
#include <qb/qbloop.h>

struct totempg_group {
	const void *group;
	size_t group_len;
};

#define TOTEMPG_AGREED			0
#define TOTEMPG_SAFE			1

/**
 * Initialize the totem process groups abstraction
 */
extern int totempg_initialize (
	qb_loop_t* poll_handle,
	struct totem_config *totem_config
);

extern void totempg_finalize (void);

extern int totempg_callback_token_create (void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data);

extern void totempg_callback_token_destroy (void *handle);

/**
 * Initialize a groups instance
 */
extern int totempg_groups_initialize (
	void **instance,

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

extern int totempg_groups_finalize (void *instance);

extern int totempg_groups_join (
	void *instance,
	const struct totempg_group *groups,
	size_t group_cnt);

extern int totempg_groups_leave (
	void *instance,
	const struct totempg_group *groups,
	size_t group_cnt);

extern int totempg_groups_mcast_joined (
	void *instance,
	const struct iovec *iovec,
	unsigned int iov_len,
	int guarantee);

extern int totempg_groups_joined_reserve (
	void *instance,
	const struct iovec *iovec,
	unsigned int iov_len);

extern int totempg_groups_joined_release (
	int msg_count);

extern int totempg_groups_mcast_groups (
	void *instance,
	int guarantee,
	const struct totempg_group *groups,
	size_t groups_cnt,
	const struct iovec *iovec,
	unsigned int iov_len);

extern int totempg_groups_send_ok_groups (
	void *instance,
	const struct totempg_group *groups,
	size_t groups_cnt,
	const struct iovec *iovec,
	unsigned int iov_len);

extern int totempg_ifaces_get (
	unsigned int nodeid,
        struct totem_ip_address *interfaces,
        unsigned int interfaces_size,
	char ***status,
        unsigned int *iface_count);

extern void* totempg_get_stats (void);

void totempg_event_signal (enum totem_event_type type, int value);

extern const char *totempg_ifaces_print (unsigned int nodeid);

extern unsigned int totempg_my_nodeid_get (void);

extern int totempg_my_family_get (void);

extern int totempg_crypto_set (const char *cipher_type, const char *hash_type);

extern int totempg_ring_reenable (void);

extern void totempg_service_ready_register (
	void (*totem_service_ready) (void));

extern int totempg_member_add (
	const struct totem_ip_address *member,
	int ring_no);

extern int totempg_member_remove (
	const struct totem_ip_address *member,
	int ring_no);

enum totem_q_level {
	TOTEM_Q_LEVEL_LOW,
	TOTEM_Q_LEVEL_GOOD,
	TOTEM_Q_LEVEL_HIGH,
	TOTEM_Q_LEVEL_CRITICAL
};

void totempg_check_q_level(void *instance);

typedef void (*totem_queue_level_changed_fn) (enum totem_q_level level);
extern void totempg_queue_level_register_callback (totem_queue_level_changed_fn);

extern void totempg_threaded_mode_enable (void);

extern void totempg_trans_ack (void);

#ifdef __cplusplus
}
#endif

#endif /* TOTEMPG_H_DEFINED */
