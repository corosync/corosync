/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2012 Red Hat, Inc.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * All rights reserved.
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
#ifndef TOTEM_H_DEFINED
#define TOTEM_H_DEFINED
#include "totemip.h"
#include <corosync/hdb.h>

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define PROCESSOR_COUNT_MAX	16
#define MESSAGE_SIZE_MAX	1024*64
#define MESSAGE_QUEUE_MAX	512
#else
#define PROCESSOR_COUNT_MAX	384
#define MESSAGE_SIZE_MAX	1024*1024 /* (1MB) */
#define MESSAGE_QUEUE_MAX	((4 * MESSAGE_SIZE_MAX) / totem_config->net_mtu)
#endif /* HAVE_SMALL_MEMORY_FOOTPRINT */

#define FRAME_SIZE_MAX		10000
#define TRANSMITS_ALLOWED	16
#define SEND_THREADS_MAX	16
#define INTERFACE_MAX		2

#define BIND_MAX_RETRIES	10
#define BIND_RETRIES_INTERVAL	100

/**
 * Maximum number of continuous gather states
 */
#define MAX_NO_CONT_GATHER	3
/*
 * Maximum number of continuous failures get from sendmsg call
 */
#define MAX_NO_CONT_SENDMSG_FAILURES	30

struct totem_interface {
	struct totem_ip_address bindnet;
	struct totem_ip_address boundto;
	struct totem_ip_address mcast_addr;
	uint16_t ip_port;
	uint16_t ttl;
	int member_count;
	struct totem_ip_address member_list[PROCESSOR_COUNT_MAX];
};

struct totem_logging_configuration {
	void (*log_printf) (
		int level,
		int subsys,
		const char *function_name,
		const char *file_name,
		int file_line,
		const char *format,
		...) __attribute__((format(printf, 6, 7)));

	int log_level_security;
	int log_level_error;
	int log_level_warning;
	int log_level_notice;
	int log_level_debug;
	int log_level_trace;
	int log_subsys_id;
};

enum { TOTEM_PRIVATE_KEY_LEN = 128 };
enum { TOTEM_RRP_MODE_BYTES = 64 };

typedef enum {
	TOTEM_TRANSPORT_UDP = 0,
	TOTEM_TRANSPORT_UDPU = 1,
	TOTEM_TRANSPORT_RDMA = 2
} totem_transport_t;

#define MEMB_RING_ID
struct memb_ring_id {
	struct totem_ip_address rep;
	unsigned long long seq;
} __attribute__((packed));

struct totem_config {
	int version;

	/*
	 * network
	 */
	struct totem_interface *interfaces;
	unsigned int interface_count;
	unsigned int node_id;
	unsigned int clear_node_high_bit;

	/*
	 * key information
	 */
	unsigned char private_key[TOTEM_PRIVATE_KEY_LEN];

	unsigned int private_key_len;

	/*
	 * Totem configuration parameters
	 */
	unsigned int token_timeout;

	unsigned int token_retransmit_timeout;

	unsigned int token_hold_timeout;

	unsigned int token_retransmits_before_loss_const;

	unsigned int join_timeout;

	unsigned int send_join_timeout;

	unsigned int consensus_timeout;

	unsigned int merge_timeout;

	unsigned int downcheck_timeout;

	unsigned int fail_to_recv_const;

	unsigned int seqno_unchanged_const;

	unsigned int rrp_token_expired_timeout;

	unsigned int rrp_problem_count_timeout;

	unsigned int rrp_problem_count_threshold;

	unsigned int rrp_problem_count_mcast_threshold;

	unsigned int rrp_autorecovery_check_timeout;

	char rrp_mode[TOTEM_RRP_MODE_BYTES];

	struct totem_logging_configuration totem_logging_configuration;

	unsigned int net_mtu;

	unsigned int threads;

	unsigned int heartbeat_failures_allowed;

	unsigned int max_network_delay;

	unsigned int window_size;

	unsigned int max_messages;

	const char *vsf_type;

	unsigned int broadcast_use;

	char *crypto_cipher_type;

	char *crypto_hash_type;

	totem_transport_t transport_number;

	unsigned int miss_count_const;

	int ip_version;

	void (*totem_memb_ring_id_create_or_load) (
	    struct memb_ring_id *memb_ring_id,
	    const struct totem_ip_address *addr);

	void (*totem_memb_ring_id_store) (
	    const struct memb_ring_id *memb_ring_id,
	    const struct totem_ip_address *addr);
};

#define TOTEM_CONFIGURATION_TYPE
enum totem_configuration_type {
	TOTEM_CONFIGURATION_REGULAR,
	TOTEM_CONFIGURATION_TRANSITIONAL
};

#define TOTEM_CALLBACK_TOKEN_TYPE
enum totem_callback_token_type {
	TOTEM_CALLBACK_TOKEN_RECEIVED = 1,
	TOTEM_CALLBACK_TOKEN_SENT = 2
};

enum totem_event_type {
	TOTEM_EVENT_DELIVERY_CONGESTED,
	TOTEM_EVENT_NEW_MSG,
};

typedef struct {
	int is_dirty;
	time_t last_updated;
} totem_stats_header_t;

typedef struct {
	totem_stats_header_t hdr;
	uint32_t iface_changes;
} totemnet_stats_t;

typedef struct {
	totem_stats_header_t hdr;
	totemnet_stats_t *net;
	char *algo_name;
	uint8_t *faulty;
	uint32_t interface_count;
} totemrrp_stats_t;


typedef struct {
	uint32_t rx;
	uint32_t tx;
	int backlog_calc;
} totemsrp_token_stats_t;

typedef struct {
	totem_stats_header_t hdr;
	totemrrp_stats_t *rrp;
	uint64_t orf_token_tx;
	uint64_t orf_token_rx;
	uint64_t memb_merge_detect_tx;
	uint64_t memb_merge_detect_rx;
	uint64_t memb_join_tx;
	uint64_t memb_join_rx;
	uint64_t mcast_tx;
	uint64_t mcast_retx;
	uint64_t mcast_rx;
	uint64_t memb_commit_token_tx;
	uint64_t memb_commit_token_rx;
	uint64_t token_hold_cancel_tx;
	uint64_t token_hold_cancel_rx;
	uint64_t operational_entered;
	uint64_t operational_token_lost;
	uint64_t gather_entered;
	uint64_t gather_token_lost;
	uint64_t commit_entered;
	uint64_t commit_token_lost;
	uint64_t recovery_entered;
	uint64_t recovery_token_lost;
	uint64_t consensus_timeouts;
	uint64_t rx_msg_dropped;
	uint32_t continuous_gather;
	uint32_t continuous_sendmsg_failures;

	int earliest_token;
	int latest_token;
#define TOTEM_TOKEN_STATS_MAX 100
	totemsrp_token_stats_t token[TOTEM_TOKEN_STATS_MAX];

} totemsrp_stats_t;

 
 #define TOTEM_CONFIGURATION_TYPE

typedef struct {
	totem_stats_header_t hdr;
	totemsrp_stats_t *srp;
} totemmrp_stats_t;

typedef struct {
	totem_stats_header_t hdr;
	totemmrp_stats_t *mrp;
	uint32_t msg_reserved;
	uint32_t msg_queue_avail;
} totempg_stats_t;

#endif /* TOTEM_H_DEFINED */
