/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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


/*
 * This is a simple 'last man standing' algorithm for 2 node clusters
 *
 * If the node is the only one left in the cluster that can see the
 * qdevice server then we return a vote.
 *
 * If more than one node can see the qdevice server but the nodes can't
 * see each other then we return a vote to the nominated tie_breaker node
 *
 * If there are more than two nodes, then we don't return a vote.
 * this is not our job.
 */

#include <sys/types.h>

#include <string.h>
#include <limits.h>

#include "qnetd-algo-2nodelms.h"
#include "qnetd-log.h"
#include "qnetd-cluster-list.h"
#include "qnetd-algo-utils.h"
#include "utils.h"

struct qnetd_algo_2nodelms_info {
	int num_config_nodes;
	enum tlv_vote last_result;
};

enum tlv_reply_error_code
qnetd_algo_2nodelms_client_init(struct qnetd_client *client)
{
	struct qnetd_algo_2nodelms_info *info;

	info = malloc(sizeof(struct qnetd_algo_2nodelms_info));
	if (!info) {
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}
	client->algorithm_data = info;
	info->last_result = 0;
	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called after client sent configuration node list
 * All client fields are already set. Nodes is actual node list, initial is used
 * to distinquish between initial node list and changed node list.
 * msg_seq_num is 32-bit number set by client. If client sent config file version,
 * config_version_set is set to 1 and config_version contains valid config file version.
 *
 * Function has to return result_vote. This can be one of ack/nack, ask_later (client
 * should ask later for a vote) or wait_for_reply (client should wait for reply).
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is sent back to client)
 */
enum tlv_reply_error_code
qnetd_algo_2nodelms_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial, enum tlv_vote *result_vote)
{
	struct node_list_entry *node_info;
	struct qnetd_algo_2nodelms_info *info = client->algorithm_data;
	int node_count = 0;

	/* Check this is a 2 node cluster */
	TAILQ_FOREACH(node_info, nodes, entries) {
		node_count++;
	}
	info->num_config_nodes = node_count;
	qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s config_list has %d nodes", client->cluster_name, node_count);

	if (node_count != 2) {
		qnetd_log(LOG_INFO, "algo-2nodelms: cluster %s does not have 2 configured nodes, it has %d", client->cluster_name, node_count);

		*result_vote = TLV_VOTE_NACK;
		return (TLV_REPLY_ERROR_CODE_UNSUPPORTED_DECISION_ALGORITHM);
	}

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called after client sent membership node list.
 * All client fields are already set. Nodes is actual node list.
 * msg_seq_num is 32-bit number set by client. If client sent config file version,
 * config_version_set is set to 1 and config_version contains valid config file version.
 * ring_id and quorate are copied from client votequorum callback.
 *
 * Function has to return result_vote. This can be one of ack/nack, ask_later (client
 * should ask later for a vote) or wait_for_reply (client should wait for reply).
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is sent back to client)
 */

enum tlv_reply_error_code
qnetd_algo_2nodelms_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, const struct tlv_ring_id *ring_id,
    const struct node_list *nodes, enum tlv_heuristics heuristics,
    enum tlv_vote *result_vote)
{
	struct node_list_entry *node_info;
 	struct qnetd_client *other_client;
	struct qnetd_algo_2nodelms_info *info = client->algorithm_data;
	int node_count = 0;
	uint32_t low_node_id = UINT32_MAX;
	uint32_t high_node_id = 0;
	enum tlv_heuristics other_node_heuristics;

	/* If we're a newcomer and there is another active partition, then we must NACK
	 * to avoid quorum moving to us from already active nodes.
	 */
	if (info->last_result == 0) {
		TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
			struct qnetd_algo_2nodelms_info *other_info = other_client->algorithm_data;
			if (!tlv_ring_id_eq(ring_id, &other_client->last_ring_id) &&
			    other_info->last_result == TLV_VOTE_ACK) {

				/* Don't save NACK, we need to know subsequently if we haven't been voting */
				*result_vote = TLV_VOTE_NACK;
				qnetd_log(LOG_DEBUG, "algo-2nodelms: we are a new partition and another active partition exists. NACK");
				return (TLV_REPLY_ERROR_CODE_NO_ERROR);
			}
		}
	}

	/* If both nodes are present, then we're OK. return a vote */
	TAILQ_FOREACH(node_info, nodes, entries) {
		node_count++;
	}

	qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s (client %p nodeid "UTILS_PRI_NODE_ID") membership list has %d member nodes (ring ID "UTILS_PRI_RING_ID")", client->cluster_name, client, client->node_id, node_count, ring_id->node_id, ring_id->seq);

	if (node_count == 2) {
		qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s running normally. Both nodes active", client->cluster_name);
		*result_vote = info->last_result = TLV_VOTE_ACK;
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	/* Now look for other clients connected from this cluster that can't see us any more */
	node_count = 0;
	other_node_heuristics = TLV_HEURISTICS_UNDEFINED;
	TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
		node_count++;

		qnetd_log(LOG_DEBUG, "algo-2nodelms: seen nodeid "UTILS_PRI_NODE_ID" on client %p (ring ID "UTILS_PRI_RING_ID")", other_client->node_id, other_client, other_client->last_ring_id.node_id, other_client->last_ring_id.seq);
		if (other_client->node_id < low_node_id) {
			low_node_id = other_client->node_id;
		}
		if (other_client->node_id > high_node_id) {
			high_node_id = other_client->node_id;
		}
		if (other_client != client) {
			other_node_heuristics = other_client->last_heuristics;
		}
	}
	qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s %d nodes running independently", client->cluster_name, node_count);

	/* Only 1 node alive .. allow it to continue */
	if (node_count == 1) {
		qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s running on 'last-man'", client->cluster_name);
		*result_vote = info->last_result = TLV_VOTE_ACK;
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	/*
	 * Both nodes are alive.
	 * Check their heuristics.
	 */
	if (tlv_heuristics_cmp(heuristics, other_node_heuristics) > 0) {
		*result_vote = info->last_result = TLV_VOTE_ACK;

		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	} else if (tlv_heuristics_cmp(heuristics, other_node_heuristics) < 0) {
		*result_vote = info->last_result = TLV_VOTE_NACK;

		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	/* Heuristics are equal -> Only give a vote to the nominated tie-breaker node */
	switch (client->tie_breaker.mode) {

	case TLV_TIE_BREAKER_MODE_LOWEST:
		if (client->node_id == low_node_id) {
			qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s running on low node-id %d", client->cluster_name, low_node_id);
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s node-id %d denied vote because low nodeid %d is active", client->cluster_name, client->node_id, low_node_id);
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}
		break;
	case TLV_TIE_BREAKER_MODE_HIGHEST:
		if (client->node_id == high_node_id) {
			qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s running on high node-id %d", client->cluster_name, high_node_id);
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s node-id %d denied vote because high nodeid %d is active", client->cluster_name, client->node_id, high_node_id);
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}
		break;
	case TLV_TIE_BREAKER_MODE_NODE_ID:
		if (client->node_id == client->tie_breaker.node_id) {
			qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s running on nominated tie-breaker node %d", client->cluster_name, client->tie_breaker.node_id);
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s node-id %d denied vote because nominated tie-breaker nodeid %d is active", client->cluster_name, client->node_id, client->tie_breaker.node_id);
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}
		break;
	default:
		qnetd_log(LOG_DEBUG, "algo-2nodelms: cluster %s node-id %d denied vote because tie-breaker option is invalid: %d", client->cluster_name, client->node_id, client->tie_breaker.mode);
		*result_vote = info->last_result = TLV_VOTE_NACK;
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_2nodelms_quorum_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_quorate quorate, const struct node_list *nodes,
    enum tlv_vote *result_vote)
{

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * Called after client disconnect. Client structure is still existing (and it's part
 * of a client->cluster), but it is destroyed (and removed from cluster) right after
 * this callback finishes. Callback is used mainly for destroing client->algorithm_data.
 */
void
qnetd_algo_2nodelms_client_disconnect(struct qnetd_client *client, int server_going_down)
{
	qnetd_log(LOG_INFO, "algo-2nodelms: Client %p (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "disconnect", client, client->cluster_name, client->node_id);

	qnetd_log(LOG_INFO, "algo-2nodelms:   server going down %u", server_going_down);

	free(client->algorithm_data);
}

/*
 * Called after client sent ask for vote message. This is usually happening after server
 * replied TLV_VOTE_ASK_LATER.
 */
enum tlv_reply_error_code
qnetd_algo_2nodelms_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_vote *result_vote)
{
	struct qnetd_algo_2nodelms_info *info = client->algorithm_data;

	qnetd_log(LOG_INFO, "algo-2nodelms: Client %p (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "asked for a vote", client, client->cluster_name, client->node_id);

	if (info->last_result == 0) {
		*result_vote =	TLV_VOTE_ASK_LATER;
	}
	else {
		*result_vote =	info->last_result;
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_2nodelms_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	qnetd_log(LOG_INFO, "algo-2nodelms: Client %p (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "replied back to vote info message", client, client->cluster_name, client->node_id);

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_2nodelms_heuristics_change_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-2nodelms: heuristics change is not supported.");

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_2nodelms_timer_callback(struct qnetd_client *client, int *reschedule_timer,
    int *send_vote, enum tlv_vote *result_vote)
{

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static struct qnetd_algorithm qnetd_algo_2nodelms = {
	.init				= qnetd_algo_2nodelms_client_init,
	.config_node_list_received	= qnetd_algo_2nodelms_config_node_list_received,
	.membership_node_list_received	= qnetd_algo_2nodelms_membership_node_list_received,
	.quorum_node_list_received	= qnetd_algo_2nodelms_quorum_node_list_received,
	.client_disconnect		= qnetd_algo_2nodelms_client_disconnect,
	.ask_for_vote_received		= qnetd_algo_2nodelms_ask_for_vote_received,
	.vote_info_reply_received	= qnetd_algo_2nodelms_vote_info_reply_received,
	.heuristics_change_received	= qnetd_algo_2nodelms_heuristics_change_received,
	.timer_callback			= qnetd_algo_2nodelms_timer_callback,
};

enum tlv_reply_error_code qnetd_algo_2nodelms_register()
{
	return qnetd_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_2NODELMS, &qnetd_algo_2nodelms);
}
