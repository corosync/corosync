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

#ifndef _QNETD_LOG_DEBUG_H_
#define _QNETD_LOG_DEBUG_H_

#include "qnetd-client.h"
#include "qnetd-cluster-list.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void		qnetd_log_debug_dump_cluster(struct qnetd_cluster *cluster);

extern void		qnetd_log_debug_new_client_connected(struct qnetd_client *client);

extern void		qnetd_log_debug_dump_node_list(struct qnetd_client *client,
    const struct node_list *nodes);

extern void		qnetd_log_debug_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial);

extern void		qnetd_log_debug_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, const struct tlv_ring_id *ring_id,
    enum tlv_heuristics heuristics, const struct node_list *nodes);

extern void		qnetd_log_debug_quorum_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_quorate quorate, const struct node_list *nodes);

extern void		qnetd_log_debug_client_disconnect(struct qnetd_client *client,
    int server_going_down);

extern void		qnetd_log_debug_ask_for_vote_received(struct qnetd_client *client,
    uint32_t msg_seq_num);

extern void		qnetd_log_debug_vote_info_reply_received(struct qnetd_client *client,
    uint32_t msg_seq_num);

extern void		qnetd_log_debug_send_vote_info(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_vote vote);

extern void		qnetd_log_debug_heuristics_change_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_heuristics heuristics);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_LOG_DEBUG_H_ */
