/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#ifndef _QNETD_CLIENT_H_
#define _QNETD_CLIENT_H_

#include <sys/types.h>

#include <sys/queue.h>
#include <inttypes.h>

#include <nspr.h>
#include "dynar.h"
#include "tlv.h"
#include "send-buffer-list.h"
#include "node-list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qnetd_client {
	PRFileDesc *socket;
	PRNetAddr addr;
	char *addr_str;
	struct dynar receive_buffer;
	struct send_buffer_list send_buffer_list;
	size_t msg_already_received_bytes;
	int skipping_msg;	/* When incorrect message was received skip it */
	int tls_started;	/* Set after TLS started */
	int tls_peer_certificate_verified;	/* Certificate is verified only once */
	int preinit_received;
	int init_received;
	char *cluster_name;
	size_t cluster_name_len;
	uint8_t node_id_set;
	uint32_t node_id;
	enum tlv_decision_algorithm_type decision_algorithm;
	struct tlv_tie_breaker tie_breaker;
	uint32_t heartbeat_interval;
	enum tlv_reply_error_code skipping_msg_reason;
	void *algorithm_data;
	struct node_list configuration_node_list;
	uint8_t config_version_set;
	uint64_t config_version;
	struct node_list last_membership_node_list;
	struct node_list last_quorum_node_list;
	struct tlv_ring_id last_ring_id;
	struct qnetd_cluster *cluster;
	struct qnetd_cluster_list *cluster_list;
	struct timer_list *main_timer_list;
	struct timer_list_entry *algo_timer;
	uint32_t algo_timer_vote_info_msq_seq_number;
	int schedule_disconnect;
	uint32_t dpd_time_since_last_check;
	uint32_t dpd_msg_received_since_last_check;
	enum tlv_vote last_sent_vote;
	enum tlv_vote last_sent_ack_nack_vote;
	enum tlv_heuristics last_membership_heuristics; /* Passed in membership node list */
	enum tlv_heuristics last_regular_heuristics; /* Passed in heuristics change callback */
	enum tlv_heuristics last_heuristics; /* Latest heuristics both membership and regular */
	TAILQ_ENTRY(qnetd_client) entries;
	TAILQ_ENTRY(qnetd_client) cluster_entries;
};

extern void		qnetd_client_init(struct qnetd_client *client, PRFileDesc *sock,
    PRNetAddr *addr, char *addr_str, size_t max_receive_size, size_t max_send_buffers,
    size_t max_send_size, struct timer_list *main_timer_list);

extern void		qnetd_client_destroy(struct qnetd_client *client);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_CLIENT_H_ */
