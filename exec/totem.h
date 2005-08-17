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
#ifndef TOTEM_H_DEFINED
#define TOTEM_H_DEFINED

#define MESSAGE_SIZE_MAX			256000
#define PROCESSOR_COUNT_MAX			16
#define FRAME_SIZE_MAX				9000
#define SEND_THREADS_MAX			16

/*
 * Array location of various timeouts as
 * specified in openais.conf.  The last enum
 * specifies the size of the timeouts array and
 * needs to remain the last item in the list.
 */
enum {
	TOTEM_RETRANSMITS_BEFORE_LOSS,
	TOTEM_TOKEN,
	TOTEM_RETRANSMIT_TOKEN,
	TOTEM_HOLD_TOKEN,
	TOTEM_JOIN,
	TOTEM_CONSENSUS,
	TOTEM_MERGE,
	TOTEM_DOWNCHECK,
	TOTEM_FAIL_RECV_CONST,

	MAX_TOTEM_TIMEOUTS	/* Last item */
} totem_timeout_types;

struct totem_interface {
	struct sockaddr_in bindnet;
	struct sockaddr_in boundto;
};

struct totem_logging_configuration {
	void (*log_printf) (int, char *, ...);
	int log_level_security;
	int log_level_error;
	int log_level_warning;
	int log_level_notice;
	int log_level_debug;
};

struct totem_config {
	int version;

	/*
	 * network
	 */
	struct totem_interface *interfaces;
	int interface_count;
	struct sockaddr_in mcast_addr;

	/*
	 * key information
	 */
	unsigned char private_key[128];

	int private_key_len;

	/*
	 * Totem configuration parameters
	 */
	unsigned int token_timeout;

	unsigned int token_retransmit_timeout;

	unsigned int token_hold_timeout;

	unsigned int token_retransmits_before_loss_const;

	unsigned int join_timeout;

	unsigned int consensus_timeout;

	unsigned int merge_timeout;

	unsigned int downcheck_timeout;

	unsigned int fail_to_recv_const;

	unsigned int seqno_unchanged_const;

	struct totem_logging_configuration totem_logging_configuration;

	unsigned int secauth;

	unsigned int net_mtu;

	unsigned int threads;
};

enum totem_configuration_type {
	TOTEM_CONFIGURATION_REGULAR,
	TOTEM_CONFIGURATION_TRANSITIONAL	
};

enum totem_callback_token_type {
	TOTEM_CALLBACK_TOKEN_RECEIVED = 1,
	TOTEM_CALLBACK_TOKEN_SENT = 2
};

struct memb_ring_id {
	struct in_addr rep;
	unsigned long long seq;
} __attribute__((packed));


#endif /* TOTEM_H_DEFINED */
