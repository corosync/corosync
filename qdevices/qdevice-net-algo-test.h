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

#ifndef _QDEVICE_NET_ALGO_TEST_H_
#define _QDEVICE_NET_ALGO_TEST_H_

#include "qdevice-net-algorithm.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int	qdevice_net_algo_test_init(struct qdevice_net_instance *instance);

extern int	qdevice_net_algo_test_connected(struct qdevice_net_instance *instance,
    enum tlv_heuristics *heuristics, int *send_config_node_list, int *send_membership_node_list,
    int *send_quorum_node_list, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_config_node_list_changed(
    struct qdevice_net_instance *instance, const struct node_list *nlist,
    int config_version_set, uint64_t config_version, int *send_node_list, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_votequorum_node_list_notify(
    struct qdevice_net_instance *instance, const struct tlv_ring_id *ring_id,
    uint32_t node_list_entries, uint32_t node_list[], int *pause_cast_vote_timer,
    enum tlv_vote *vote);

extern int	qdevice_net_algo_test_votequorum_node_list_heuristics_notify(
    struct qdevice_net_instance *instance, const struct tlv_ring_id *ring_id,
    uint32_t node_list_entries, uint32_t node_list[], int *send_node_list, enum tlv_vote *vote,
    enum tlv_heuristics *heuristics);

extern int	qdevice_net_algo_test_votequorum_quorum_notify(
    struct qdevice_net_instance *instance, uint32_t quorate, uint32_t node_list_entries,
    votequorum_node_t node_list[], int *send_node_list, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_votequorum_expected_votes_notify(
    struct qdevice_net_instance *instance, uint32_t expected_votes, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_config_node_list_reply_received(
    struct qdevice_net_instance *instance, uint32_t seq_number, int initial,
    const struct tlv_ring_id *ring_id, int ring_id_is_valid, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_membership_node_list_reply_received(
    struct qdevice_net_instance *instance, uint32_t seq_number, const struct tlv_ring_id *ring_id,
    int ring_id_is_valid, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_quorum_node_list_reply_received(
    struct qdevice_net_instance *instance, uint32_t seq_number,
    const struct tlv_ring_id *ring_id, int ring_id_is_valid, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_ask_for_vote_reply_received(
    struct qdevice_net_instance *instance, uint32_t seq_number,
    const struct tlv_ring_id *ring_id, int ring_id_is_valid, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_vote_info_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote);

extern int	qdevice_net_algo_test_echo_reply_received(
    struct qdevice_net_instance *instance, uint32_t seq_number, int is_expected_seq_number);

extern int	qdevice_net_algo_test_echo_reply_not_received(
    struct qdevice_net_instance *instance);

extern int	qdevice_net_algo_test_heuristics_change(struct qdevice_net_instance *instance,
    enum tlv_heuristics *heuristics, int *send_msg, enum tlv_vote *vote);

extern int	qdevice_net_algo_test_heuristics_change_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid, enum tlv_heuristics heuristics,
    enum tlv_vote *vote);

extern int	qdevice_net_algo_test_disconnected(struct qdevice_net_instance *instance,
    enum qdevice_net_disconnect_reason disconnect_reason, int *try_reconnect, enum tlv_vote *vote);

extern void	qdevice_net_algo_test_destroy(struct qdevice_net_instance *instance);

extern int	qdevice_net_algo_test_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_NET_ALGO_TEST_H_ */
