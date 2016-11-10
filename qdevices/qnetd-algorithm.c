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

#include <sys/types.h>

#include "qnet-config.h"
#include "qnetd-algorithm.h"
#include "qnetd-algo-test.h"
#include "qnetd-algo-ffsplit.h"
#include "qnetd-algo-2nodelms.h"
#include "qnetd-algo-lms.h"
#include "qnetd-log.h"

static struct qnetd_algorithm *qnetd_algorithm_array[QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE];

enum tlv_reply_error_code
qnetd_algorithm_client_init(struct qnetd_client *client)
{
	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_client_init unhandled decision algorithm");

		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->init(client));
}

enum tlv_reply_error_code
qnetd_algorithm_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial, enum tlv_vote *result_vote)
{
	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_config_node_list_received unhandled "
		    "decision algorithm");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->config_node_list_received(
		client, msg_seq_num,
		config_version_set, config_version, nodes, initial, result_vote));
}

enum tlv_reply_error_code
qnetd_algorithm_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, const struct tlv_ring_id *ring_id,
    const struct node_list *nodes, enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_membership_node_list_received unhandled "
		    "decision algorithm");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->membership_node_list_received(
		client, msg_seq_num,
		ring_id, nodes, heuristics, result_vote));
}

enum tlv_reply_error_code
qnetd_algorithm_quorum_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_quorate quorate,
    const struct node_list *nodes, enum tlv_vote *result_vote)
{

	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "algorithm_quorum_node_list_received unhandled "
		    "decision algorithm");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->quorum_node_list_received(
		client, msg_seq_num, quorate, nodes, result_vote));
}

void
qnetd_algorithm_client_disconnect(struct qnetd_client *client, int server_going_down)
{

	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_client_disconnect unhandled decision "
		    "algorithm");
		return;
	}

	qnetd_algorithm_array[client->decision_algorithm]->client_disconnect(client, server_going_down);
}

enum tlv_reply_error_code
qnetd_algorithm_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_vote *result_vote)
{

	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_ask_for_vote_received unhandled "
		    "decision algorithm");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->ask_for_vote_received(
		client, msg_seq_num, result_vote));
}

enum tlv_reply_error_code
qnetd_algorithm_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_vote_info_reply_received unhandled decision algorithm");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->vote_info_reply_received(
		client, msg_seq_num));

}

enum tlv_reply_error_code
qnetd_algorithm_heuristics_change_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_ask_for_vote_received unhandled "
		    "decision algorithm");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->heuristics_change_received(
		client, msg_seq_num, heuristics, result_vote));
}

enum tlv_reply_error_code
qnetd_algorithm_timer_callback(struct qnetd_client *client, int *reschedule_timer,
    int *send_vote, enum tlv_vote *result_vote)
{

	if (client->decision_algorithm >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE ||
	    qnetd_algorithm_array[client->decision_algorithm] == NULL) {
		qnetd_log(LOG_CRIT, "qnetd_algorithm_timer_callback unhandled decision algorithm");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	return (qnetd_algorithm_array[client->decision_algorithm]->timer_callback(
		client, reschedule_timer, send_vote, result_vote));
}

int
qnetd_algorithm_register(enum tlv_decision_algorithm_type algorithm_number,
    struct qnetd_algorithm *algorithm)
{

	if (algorithm_number >= QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE) {
		qnetd_log(LOG_CRIT, "Failed to register unsupported decision algorithm %u",
		    algorithm_number);
		return (-1);
	}

	if (qnetd_algorithm_array[algorithm_number] != NULL) {
		qnetd_log(LOG_CRIT, "Failed to register decision algorithm %u, "
		"it's already registered.", algorithm_number);
		return (-1);
	}

	qnetd_algorithm_array[algorithm_number] = algorithm;

	return (0);
}

int
qnetd_algorithm_register_all(void)
{

	if (qnetd_algo_test_register() != 0) {
		qnetd_log(LOG_CRIT, "Failed to register decision algorithm 'test'");
		return (-1);
	}
	if (qnetd_algo_ffsplit_register() != 0) {
		qnetd_log(LOG_CRIT, "Failed to register decision algorithm 'ffsplit'");
		return (-1);
	}
	if (qnetd_algo_2nodelms_register() != 0) {
		qnetd_log(LOG_CRIT, "Failed to register decision algorithm '2nodelms'");
		return (-1);
	}
	if (qnetd_algo_lms_register() != 0) {
		qnetd_log(LOG_CRIT, "Failed to register decision algorithm 'lms'");
		return (-1);
	}

	return (0);
}
