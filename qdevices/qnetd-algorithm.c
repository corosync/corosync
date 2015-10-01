/*
 * Copyright (c) 2015 Red Hat, Inc.
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

#include <err.h>

#include "qnetd-algorithm.h"
#include "qnetd-algo-test.h"

enum tlv_reply_error_code
qnetd_algorithm_client_init(struct qnetd_client *client)
{

	switch (client->decision_algorithm) {
	case TLV_DECISION_ALGORITHM_TYPE_TEST:
		return (qnetd_algo_test_client_init(client));
		break;
	default:
		errx(1, "qnetd_algorithm_client_init unhandled decision algorithm");
		break;
	}

	return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
}

enum tlv_reply_error_code
qnetd_algorithm_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial, enum tlv_vote *result_vote)
{

	switch (client->decision_algorithm) {
	case TLV_DECISION_ALGORITHM_TYPE_TEST:
		return (qnetd_algo_test_config_node_list_received(client, msg_seq_num,
		    config_version_set, config_version, nodes, initial, result_vote));
		break;
	default:
		errx(1, "qnetd_algorithm_config_node_list_received unhandled "
		    "decision algorithm");
		break;
	}

	return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);

}

enum tlv_reply_error_code
qnetd_algorithm_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct tlv_ring_id *ring_id, enum tlv_quorate quorate,
    const struct node_list *nodes, enum tlv_vote *result_vote)
{

	switch (client->decision_algorithm) {
	case TLV_DECISION_ALGORITHM_TYPE_TEST:
		return (qnetd_algo_test_membership_node_list_received(client, msg_seq_num,
		    config_version_set, config_version, ring_id, quorate, nodes, result_vote));
		break;
	default:
		errx(1, "qnetd_algorithm_membership_node_list_received unhandled "
		    "decision algorithm");
		break;
	}

	return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
}

void
qnetd_algorithm_client_disconnect(struct qnetd_client *client, int server_going_down)
{

	switch (client->decision_algorithm) {
	case TLV_DECISION_ALGORITHM_TYPE_TEST:
		qnetd_algo_test_client_disconnect(client, server_going_down);
		break;
	default:
		errx(1, "qnetd_algorithm_client_disconnect unhandled decision algorithm");
		break;
	}
}
