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

#ifndef _QNETD_ALGO_FFSPLIT_H_
#define _QNETD_ALGO_FFSPLIT_H_

#include "qnetd-algorithm.h"

#ifdef __cplusplus
extern "C" {
#endif

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_client_init(struct qnetd_client *client);

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_config_node_list_received(
    struct qnetd_client *client, uint32_t msg_seq_num, int config_version_set,
    uint64_t config_version, const struct node_list *nodes, int initial,
    enum tlv_vote *result_vote);

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_membership_node_list_received(
    struct qnetd_client *client, uint32_t msg_seq_num,
    const struct tlv_ring_id *ring_id,
    const struct node_list *nodes, enum tlv_heuristics heuristics, enum tlv_vote *result_vote);

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_quorum_node_list_received(
    struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_quorate quorate, const struct node_list *nodes, enum tlv_vote *result_vote);

extern void				qnetd_algo_ffsplit_client_disconnect(
    struct qnetd_client *client, int server_going_down);

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_ask_for_vote_received(
    struct qnetd_client *client, uint32_t msg_seq_num, enum tlv_vote *result_vote);

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_vote_info_reply_received(
    struct qnetd_client *client, uint32_t msg_seq_num);

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_heuristics_change_received(
    struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_heuristics heuristics, enum tlv_vote *result_vote);

extern enum tlv_reply_error_code	qnetd_algo_ffsplit_timer_callback(
    struct qnetd_client *client, int *reschedule_timer,
    int *send_vote, enum tlv_vote *result_vote);

extern enum tlv_reply_error_code qnetd_algo_ffsplit_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_ALGO_FFSPLIT_H_ */
