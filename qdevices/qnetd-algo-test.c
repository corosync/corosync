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

#include <string.h>

#include "qnetd-algo-test.h"
#include "qnetd-log.h"
#include "qnetd-cluster-list.h"
#include "qnetd-client-send.h"
#include "qnetd-log-debug.h"
#include "qnetd-client-algo-timer.h"
#include "utils.h"

/*
 * Called right after client sent init message. This happens after initial accept of client,
 * tls handshake and sending basic information about cluster/client.
 * Known information:
 * - client->cluster_name (client->cluster_name_len)
 * - client->node_id (client->node_id_set = 1)
 * - client->decision_algorithm
 * - client->cluster
 * - client->last_ring_id
 *
 * Callback is designed mainly for allocating client->algorithm_data. It's also already
 * part of the cluster, so can access (alloc) client->cluster->algorithm_data.
 *
 * client is initialized qnetd_client structure.
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is sent back to client)
 */
enum tlv_reply_error_code
qnetd_algo_test_client_init(struct qnetd_client *client)
{
	int *algo_data;

	qnetd_log(LOG_WARNING, "algo-test: Client %s (cluster = '%s', node_id = "
	    UTILS_PRI_NODE_ID") initiated test algorithm. It's not recommended to use test "
	    "algorithm because it can create multiple quorate partitions!", client->addr_str,
	    client->cluster_name, client->node_id);

	qnetd_log(LOG_INFO, "algo-test: client_init");

	client->algorithm_data = malloc(sizeof(int));
	if (client->algorithm_data == NULL) {
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	algo_data = client->algorithm_data;
	*algo_data = 42;

	if (qnetd_cluster_size(client->cluster) == 1) {
		/*
		 * First client in the cluster
		 */
		qnetd_log(LOG_INFO, "algo-test: Initializing cluster->algorithm data");

		client->cluster->algorithm_data = malloc(sizeof(int));
		if (client->cluster->algorithm_data == NULL) {
			return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
		}

		algo_data = client->cluster->algorithm_data;
		*algo_data = 42;
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}


/*
 * Called after client sent configuration node list
 * All client fields are already set. Nodes is actual node list, initial is used
 * for distrinquish between initial node list and changed node list.
 * msg_seq_num is 32-bit number set by client. If client sent config file version,
 * config_version_set is set to 1 and config_version contains valid config file version.
 *
 * Function has to return result_vote. This can be one of ack/nack, ask_later (client
 * should ask later for a vote) or wait_for_reply (client should wait for reply).
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is send back to client)
 */
enum tlv_reply_error_code
qnetd_algo_test_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial, enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-test: node_list_received");

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called after client sent membership node list.
 * All client fields are already set. Nodes is actual node list.
 * msg_seq_num is 32-bit number set by client.
 * ring_id is copied from client votequorum callback.
 * heuristics is result of client heuristics (or TLV_HEURISTICS_UNDEFINED if heuristics
 *  are disabled or not supported by client)
 *
 * Function has to return result_vote. This can be one of ack/nack, ask_later (client
 * should ask later for a vote) or wait_for_reply (client should wait for reply).
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is sent back to client)
 */

enum tlv_reply_error_code
qnetd_algo_test_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, const struct tlv_ring_id *ring_id,
    const struct node_list *nodes, enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-test: membership_node_list_received");

	*result_vote = TLV_VOTE_ACK;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called after client sent quorum node list.
 * All client fields are already set. Nodes is actual node list.
 * msg_seq_num is 32-bit number set by client.
 * quorate is copied from client votequorum callback.
 * Function is just informative. If client vote is required to change, it's possible
 * to use qnetd_client_send_vote_info.
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is sent back to client)
 */
enum tlv_reply_error_code
qnetd_algo_test_quorum_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_quorate quorate, const struct node_list *nodes,
    enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-test: quorum_node_list_received");

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called after client disconnect. Client structure is still existing (and it's part
 * of a client->cluster), but it is destroyed (and removed from cluster) right after
 * this callback finishes. Callback is used mainly for destroing client->algorithm_data.
 */
void
qnetd_algo_test_client_disconnect(struct qnetd_client *client, int server_going_down)
{

	qnetd_log(LOG_INFO, "algo-test: client_disconnect");

	free(client->algorithm_data);

	if (qnetd_cluster_size(client->cluster) == 1) {
		/*
		 * Last client in the cluster
		 */
		qnetd_log(LOG_INFO, "algo-test: Finalizing cluster->algorithm data");

		free(client->cluster->algorithm_data);
	}
}

/*
 * Called after client sent ask for vote message. This is usually happening after server
 * replied TLV_VOTE_ASK_LATER.
 */
enum tlv_reply_error_code
qnetd_algo_test_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-test: ask_for_vote_received");

	*result_vote = TLV_VOTE_ACK;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_test_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	qnetd_log(LOG_INFO, "algo-test: vote_info_reply_received");

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called after client sent heuristics change message.
 * heuristics is result of client regular heuristics (cannot be TLV_HEURISTICS_UNDEFINED)
 * Variables client->last_regular_heuristics and client->last_heuristics are updated after
 * the call.
 */
enum tlv_reply_error_code
qnetd_algo_test_heuristics_change_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-test: heuristics_change_received");

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called as a result of qnetd_client_algo_timer_schedule function call after timeout expires.
 *
 * If send_vote is set by callback to non zero value, result_vote must also be set and such vote is
 * sent to client. Result_vote is ignored if send_vote = 0 (default).
 *
 * If reschedule timer (default value = 0) is set to non zero value, callback is called again later
 * with same timeout as originaly created.
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is sent back to client)
 */
enum tlv_reply_error_code
qnetd_algo_test_timer_callback(struct qnetd_client *client, int *reschedule_timer,
    int *send_vote, enum tlv_vote *result_vote)
{

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static struct qnetd_algorithm qnetd_algo_test = {
	.init				= qnetd_algo_test_client_init,
	.config_node_list_received	= qnetd_algo_test_config_node_list_received,
	.membership_node_list_received	= qnetd_algo_test_membership_node_list_received,
	.quorum_node_list_received	= qnetd_algo_test_quorum_node_list_received,
	.client_disconnect		= qnetd_algo_test_client_disconnect,
	.ask_for_vote_received		= qnetd_algo_test_ask_for_vote_received,
	.vote_info_reply_received	= qnetd_algo_test_vote_info_reply_received,
	.heuristics_change_received	= qnetd_algo_test_heuristics_change_received,
	.timer_callback			= qnetd_algo_test_timer_callback,
};

enum tlv_reply_error_code qnetd_algo_test_register()
{

	return (qnetd_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_TEST, &qnetd_algo_test));
}
