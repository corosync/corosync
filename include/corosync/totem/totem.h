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
#include <libknet.h>
#include <corosync/hdb.h>
#include <corosync/totem/totemstats.h>

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define PROCESSOR_COUNT_MAX	16
#define MESSAGE_SIZE_MAX	1024*64
#define MESSAGE_QUEUE_MAX	512
#else
#define PROCESSOR_COUNT_MAX	384
#define MESSAGE_SIZE_MAX	1024*1024 /* (1MB) */
#define MESSAGE_QUEUE_MAX	((4 * MESSAGE_SIZE_MAX) / totem_config->net_mtu)
#endif /* HAVE_SMALL_MEMORY_FOOTPRINT */

#define FRAME_SIZE_MAX		KNET_MAX_PACKET_SIZE

/*
 * Estimation of required buffer size for totemudp and totemudpu - it should be at least
 *   sizeof(memb_join) + PROCESSOR_MAX * 2 * sizeof(srp_addr))
 * if we want to support PROCESSOR_MAX nodes, but because we don't have
 * srp_addr and memb_join, we have to use estimation.
 * TODO: Consider moving srp_addr/memb_join into totem headers instead of totemsrp.c
 */
#define UDP_RECEIVE_FRAME_SIZE_MAX     (PROCESSOR_COUNT_MAX * (INTERFACE_MAX * 2 * sizeof(struct totem_ip_address)) + 1024)

#define TRANSMITS_ALLOWED	16
#define SEND_THREADS_MAX	16

/* This must be <= KNET_MAX_LINK */
#define INTERFACE_MAX		8

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
	uint8_t  configured;
	int member_count;
	int knet_link_priority;
	int knet_ping_interval;
	int knet_ping_timeout;
	int knet_ping_precision;
	int knet_pong_count;
	int knet_transport;
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


/*
 * COrosync TOtem. Also used as an endian_detector.
 */
#define TOTEM_MH_MAGIC		0xC070
#define TOTEM_MH_VERSION	0x03

struct totem_message_header {
	unsigned short magic;
	char version;
	char type;
	char encapsulated;
	unsigned int nodeid;
	unsigned int target_nodeid;
} __attribute__((packed));

enum {
	TOTEM_PRIVATE_KEY_LEN_MIN = KNET_MIN_KEY_LEN,
	TOTEM_PRIVATE_KEY_LEN_MAX = KNET_MAX_KEY_LEN
};

enum { TOTEM_LINK_MODE_BYTES = 64 };

typedef enum {
	TOTEM_TRANSPORT_UDP = 0,
	TOTEM_TRANSPORT_UDPU = 1,
	TOTEM_TRANSPORT_KNET = 2
} totem_transport_t;

#define MEMB_RING_ID
struct memb_ring_id {
	unsigned int rep;
	unsigned long long seq;
} __attribute__((packed));

struct totem_config {
	int version;

	/*
	 * network
	 */
	struct totem_interface *interfaces;
	struct totem_interface *orig_interfaces; /* for reload */
	unsigned int node_id;
	unsigned int clear_node_high_bit;
	unsigned int knet_pmtud_interval;

	/*
	 * key information
	 */
	unsigned char private_key[TOTEM_PRIVATE_KEY_LEN_MAX];

	unsigned int private_key_len;

	/*
	 * Totem configuration parameters
	 */
	unsigned int token_timeout;

	unsigned int token_warning;

	unsigned int kick;

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

	char link_mode[TOTEM_LINK_MODE_BYTES];

	struct totem_logging_configuration totem_logging_configuration;

	unsigned int net_mtu;

	unsigned int threads;

	unsigned int heartbeat_failures_allowed;

	unsigned int max_network_delay;

	unsigned int window_size;

	unsigned int max_messages;

	const char *vsf_type;

	unsigned int broadcast_use;

	char *crypto_model;

	char *crypto_cipher_type;

	char *crypto_hash_type;

	char *knet_compression_model;

	uint32_t knet_compression_threshold;

	int knet_compression_level;

	totem_transport_t transport_number;

	unsigned int miss_count_const;

	int ip_version;

	void (*totem_memb_ring_id_create_or_load) (
	    struct memb_ring_id *memb_ring_id,
	    unsigned int nodeid);

	void (*totem_memb_ring_id_store) (
	    const struct memb_ring_id *memb_ring_id,
	    unsigned int nodeid);
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

#endif /* TOTEM_H_DEFINED */
