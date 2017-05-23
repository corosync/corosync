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

/**
 * @brief The totempg_group struct
 */
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

/**
 * @brief totempg_finalize
 */
extern void totempg_finalize (void);

/**
 * @brief totempg_callback_token_create
 * @param handle_out
 * @param totem_callback_token_type
 * @param delete
 * @param callback_fn
 * @param data
 */
extern int totempg_callback_token_create (void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data);

/**
 * @brief totempg_callback_token_destroy
 * @param handle
 */
extern void totempg_callback_token_destroy (void *handle);

/**
 * @brief Initialize a groups instance
 * @param instance
 * @return
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

/**
 * @brief totempg_groups_finalize
 * @param instance
 * @return
 */
extern int totempg_groups_finalize (void *instance);

/**
 * @brief totempg_groups_join
 * @param instance
 * @param groups
 * @param group_cnt
 * @return
 */
extern int totempg_groups_join (
	void *instance,
	const struct totempg_group *groups,
	size_t group_cnt);

/**
 * @brief totempg_groups_leave
 * @param instance
 * @param groups
 * @param group_cnt
 * @return
 */
extern int totempg_groups_leave (
	void *instance,
	const struct totempg_group *groups,
	size_t group_cnt);

/**
 * @brief totempg_groups_mcast_joined
 * @param instance
 * @param iovec
 * @param iov_len
 * @param guarantee
 * @return
 */
extern int totempg_groups_mcast_joined (
	void *instance,
	const struct iovec *iovec,
	unsigned int iov_len,
	int guarantee);

/**
 * @brief totempg_groups_joined_reserve
 * @param instance
 * @param iovec
 * @param iov_len
 * @return
 */
extern int totempg_groups_joined_reserve (
	void *instance,
	const struct iovec *iovec,
	unsigned int iov_len);

/**
 * @brief totempg_groups_joined_release
 * @param msg_count
 * @return
 */
extern int totempg_groups_joined_release (
	int msg_count);

/**
 * @brief totempg_groups_mcast_groups
 * @param instance
 * @param guarantee
 * @param groups
 * @param groups_cnt
 * @param iovec
 * @param iov_len
 * @return
 */
extern int totempg_groups_mcast_groups (
	void *instance,
	int guarantee,
	const struct totempg_group *groups,
	size_t groups_cnt,
	const struct iovec *iovec,
	unsigned int iov_len);

/**
 * @brief totempg_groups_send_ok_groups
 * @param instance
 * @param groups
 * @param groups_cnt
 * @param iovec
 * @param iov_len
 * @return
 */
extern int totempg_groups_send_ok_groups (
	void *instance,
	const struct totempg_group *groups,
	size_t groups_cnt,
	const struct iovec *iovec,
	unsigned int iov_len);

/**
 * @brief totempg_ifaces_get
 * @param nodeid
 * @param interfaces
 * @param interfaces_size
 * @param status
 * @param iface_count
 * @return
 */
extern int totempg_ifaces_get (
	unsigned int nodeid,
        struct totem_ip_address *interfaces,
        unsigned int interfaces_size,
	char ***status,
        unsigned int *iface_count);

/**
 * @brief totempg_get_stats
 * @return
 */
extern void* totempg_get_stats (void);

/**
 * @brief totempg_event_signal
 * @param type
 * @param value
 */
void totempg_event_signal (enum totem_event_type type, int value);

/**
 * @brief totempg_ifaces_print
 * @param nodeid
 * @return
 */
extern const char *totempg_ifaces_print (unsigned int nodeid);

/**
 * @brief totempg_my_nodeid_get
 * @return
 */
extern unsigned int totempg_my_nodeid_get (void);

/**
 * @brief totempg_my_family_get
 * @return
 */
extern int totempg_my_family_get (void);

/**
 * @brief totempg_crypto_set
 * @param cipher_type
 * @param hash_type
 * @return
 */
extern int totempg_crypto_set (const char *cipher_type, const char *hash_type);

/**
 * @brief totempg_ring_reenable
 * @return
 */
extern int totempg_ring_reenable (void);

/**
 * @brief totempg_service_ready_register
 */
extern void totempg_service_ready_register (
	void (*totem_service_ready) (void));

/**
 * @brief totempg_member_add
 * @param member
 * @param ring_no
 * @return
 */
extern int totempg_member_add (
	const struct totem_ip_address *member,
	int ring_no);

/**
 * @brief totempg_member_remove
 * @param member
 * @param ring_no
 * @return
 */
extern int totempg_member_remove (
	const struct totem_ip_address *member,
	int ring_no);

/**
 * @brief The totem_q_level enum
 */
enum totem_q_level {
	TOTEM_Q_LEVEL_LOW,
	TOTEM_Q_LEVEL_GOOD,
	TOTEM_Q_LEVEL_HIGH,
	TOTEM_Q_LEVEL_CRITICAL
};

/**
 * @brief totempg_check_q_level
 * @param instance
 */
void totempg_check_q_level(void *instance);

/**
 * @brief totem_queue_level_changed_fn
 */
typedef void (*totem_queue_level_changed_fn) (enum totem_q_level level);

/**
 * @brief totempg_queue_level_register_callback
 */
extern void totempg_queue_level_register_callback (totem_queue_level_changed_fn);

/**
 * @brief totempg_threaded_mode_enable
 */
extern void totempg_threaded_mode_enable (void);

/**
 * @brief totempg_trans_ack
 */
extern void totempg_trans_ack (void);

#ifdef __cplusplus
}
#endif

#endif /* TOTEMPG_H_DEFINED */
