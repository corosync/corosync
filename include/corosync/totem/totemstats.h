/*
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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
#ifndef TOTEMSTATS_H_DEFINED
#define TOTEMSTATS_H_DEFINED

typedef struct {
	int is_dirty;
	time_t last_updated;
} totem_stats_header_t;

typedef struct {
	totem_stats_header_t hdr;
	uint32_t iface_changes;
} totemnet_stats_t;

typedef struct {
	uint32_t rx;
	uint32_t tx;
	int backlog_calc;
} totemsrp_token_stats_t;

typedef struct {
	totem_stats_header_t hdr;
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

	uint8_t  firewall_enabled_or_nic_failure;
	uint32_t mtt_rx_token;
	uint32_t avg_token_workload;
	uint32_t avg_backlog_calc;

	int earliest_token;
	int latest_token;
#define TOTEM_TOKEN_STATS_MAX 100
	totemsrp_token_stats_t token[TOTEM_TOKEN_STATS_MAX];

} totemsrp_stats_t;

typedef struct {
	totem_stats_header_t hdr;
	totemsrp_stats_t *srp;
	uint32_t msg_reserved;
	uint32_t msg_queue_avail;
} totempg_stats_t;


extern int totemknet_link_get_status (
	knet_node_id_t node, uint8_t link,
	struct knet_link_status *status);

void stats_knet_add_member(knet_node_id_t nodeid, uint8_t link);

void stats_knet_del_member(knet_node_id_t nodeid, uint8_t link);


#endif /* TOTEMSTATS_H_DEFINED */
