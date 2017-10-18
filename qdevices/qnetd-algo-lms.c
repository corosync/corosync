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
 * This is a 'last man standing' algorithm for 2+ node clusters
 *
 * If the node is the only one left in the cluster that can see the
 * qdevice server then we return a vote.
 *
 * If more than one node can see the qdevice server but some nodes can't
 * see each other then we divide the cluster up into 'partitions' based on
 * their ring_id and return a vote to nodes in the partition that contains
 * a nominated nodeid. (lowest, highest, etc)
 *
 */

#include <sys/types.h>
#include <sys/queue.h>

#include <string.h>
#include <limits.h>

#include "qnetd-algo-lms.h"
#include "qnetd-log.h"
#include "qnetd-cluster-list.h"
#include "qnetd-algo-utils.h"
#include "qnetd-client-algo-timer.h"
#include "utils.h"

struct qnetd_algo_lms_info {
	int num_config_nodes;
	enum tlv_vote last_result;
	partitions_list_t partition_list;
};

static enum tlv_reply_error_code do_lms_algorithm(struct qnetd_client *client, const struct tlv_ring_id *cur_ring_id, enum tlv_vote *result_vote)
{
 	struct qnetd_client *other_client;
	struct qnetd_algo_lms_info *info = client->algorithm_data;
	struct qnetd_algo_partition *cur_partition;
	struct qnetd_algo_partition *largest_partition;
	struct qnetd_algo_partition *best_score_partition;
	const struct tlv_ring_id *ring_id = cur_ring_id;
	int num_partitions;
	int joint_leader;

	/* We are running the algorithm, don't do it again unless we say so */
	qnetd_client_algo_timer_abort(client);

	if (qnetd_algo_all_ring_ids_match(client, ring_id) == -1) {
		qnetd_log(LOG_DEBUG, "algo-lms: nodeid %d: ring ID (" UTILS_PRI_RING_ID ") not unique in this membership, waiting",
			  client->node_id, ring_id->node_id, ring_id->seq);

		qnetd_client_algo_timer_schedule(client);
		*result_vote = info->last_result = TLV_VOTE_WAIT_FOR_REPLY;
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	/* Create and count the number of separate partitions */
	if ( (num_partitions = qnetd_algo_create_partitions(client, &info->partition_list, ring_id)) == -1) {
		qnetd_log(LOG_DEBUG, "algo-lms: Error creating partition list");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	/* This can happen if we are first on the block */
	if (num_partitions == 0) {
		qnetd_log(LOG_DEBUG, "algo-lms: No partitions found");

		qnetd_client_algo_timer_schedule(client);
		*result_vote = info->last_result = TLV_VOTE_WAIT_FOR_REPLY;
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	qnetd_algo_dump_partitions(&info->partition_list);

	/* Only 1 partition - let votequorum sort it out */
	if (num_partitions == 1) {
		qnetd_log(LOG_DEBUG, "algo-lms: Only 1 partition. This is votequorum's problem, not ours");
		qnetd_algo_free_partitions(&info->partition_list);
		*result_vote = info->last_result = TLV_VOTE_ACK;
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}


	/* If we're a newcomer and there is another active partition, then we must NACK
	 * to avoid quorum moving to us from already active nodes.
	 */
	if (info->last_result == 0) {
		TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
			struct qnetd_algo_lms_info *other_info = other_client->algorithm_data;
			if (!tlv_ring_id_eq(ring_id, &other_client->last_ring_id) &&
			    other_info->last_result == TLV_VOTE_ACK) {
				qnetd_algo_free_partitions(&info->partition_list);

				/* Don't save NACK, we need to know subsequently if we haven't been voting */
				*result_vote = TLV_VOTE_NACK;
				qnetd_log(LOG_DEBUG, "algo-lms: we are a new partition and another active partition exists. NACK");
				return (TLV_REPLY_ERROR_CODE_NO_ERROR);
			}
		}
	}

	/*
	 * Find the partition with highest score
	 */
	best_score_partition = NULL;
	TAILQ_FOREACH(cur_partition, &info->partition_list, entries) {
		if (!best_score_partition ||
		    best_score_partition->score < cur_partition->score) {
			best_score_partition = cur_partition;
		}
	}
	qnetd_log(LOG_DEBUG, "algo-lms: best score partition is (" UTILS_PRI_RING_ID ") with score %d",
		  best_score_partition->ring_id.node_id, best_score_partition->ring_id.seq, best_score_partition->score);

	/* Now check if it's really the highest score, and not just the joint-highest */
	joint_leader = 0;
	TAILQ_FOREACH(cur_partition, &info->partition_list, entries) {
		if (best_score_partition != cur_partition &&
		    best_score_partition->score == cur_partition->score) {
			joint_leader = 1;
		}
	}

	if (!joint_leader) {
		/* Partition with highest score is unique, allow us to run if we're in that partition. */
		if (tlv_ring_id_eq(&best_score_partition->ring_id, ring_id)) {
			qnetd_log(LOG_DEBUG, "algo-lms: We are in the best score partition. ACK");
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-lms: We are NOT in the best score partition. NACK");
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}

		qnetd_algo_free_partitions(&info->partition_list);

		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	/*
	 * There are multiple partitions with same score. Find the largest partition
	 */
	largest_partition = NULL;
	TAILQ_FOREACH(cur_partition, &info->partition_list, entries) {
		if (!largest_partition ||
		    largest_partition->num_nodes < cur_partition->num_nodes) {
			largest_partition = cur_partition;
		}
	}

	qnetd_log(LOG_DEBUG, "algo-lms: largest partition is (" UTILS_PRI_RING_ID ") with %d nodes",
		  largest_partition->ring_id.node_id, largest_partition->ring_id.seq, largest_partition->num_nodes);

	/* Now check if it's really the largest, and not just the joint-largest */
	joint_leader = 0;
	TAILQ_FOREACH(cur_partition, &info->partition_list, entries) {
		if (largest_partition != cur_partition &&
		    largest_partition->num_nodes == cur_partition->num_nodes) {
			joint_leader = 1;
		}
	}

	if (!joint_leader) {
		/* Largest partition is unique, allow us to run if we're in that partition. */
		if (tlv_ring_id_eq(&largest_partition->ring_id, ring_id)) {
			qnetd_log(LOG_DEBUG, "algo-lms: We are in the largest partition. ACK");
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-lms: We are NOT in the largest partition. NACK");
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}
	}
	else {
		uint32_t tb_node_id;
		struct tlv_ring_id tb_node_ring_id = {0LL, 0};

		/* Look for the tie-breaker node */
		if (client->tie_breaker.mode == TLV_TIE_BREAKER_MODE_LOWEST) {
			tb_node_id = INT_MAX;
		}
		else if (client->tie_breaker.mode == TLV_TIE_BREAKER_MODE_HIGHEST) {
			tb_node_id = 0;
		}
		else if (client->tie_breaker.mode == TLV_TIE_BREAKER_MODE_NODE_ID) {
			tb_node_id = client->tie_breaker.node_id;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-lms: denied vote because tie-breaker option is invalid: %d",
				  client->tie_breaker.mode);
			tb_node_id = -1;
		}

		/* Find the tie_breaker node */
		TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
			switch (client->tie_breaker.mode) {

			case TLV_TIE_BREAKER_MODE_LOWEST:
				if (other_client->node_id < tb_node_id) {
					tb_node_id = other_client->node_id;
					memcpy(&tb_node_ring_id, &other_client->last_ring_id, sizeof(struct tlv_ring_id));
					qnetd_log(LOG_DEBUG, "algo-lms: Looking for low node ID. found %d (" UTILS_PRI_RING_ID ")",
						  tb_node_id, tb_node_ring_id.node_id, tb_node_ring_id.seq);
				}
			break;

			case TLV_TIE_BREAKER_MODE_HIGHEST:
				if (other_client->node_id > tb_node_id) {
					tb_node_id = other_client->node_id;
					memcpy(&tb_node_ring_id, &other_client->last_ring_id, sizeof(struct tlv_ring_id));
					qnetd_log(LOG_DEBUG, "algo-lms: Looking for high node ID. found %d (" UTILS_PRI_RING_ID ")",
						  tb_node_id, tb_node_ring_id.node_id, tb_node_ring_id.seq);
				}
			break;
			case TLV_TIE_BREAKER_MODE_NODE_ID:
				if (client->tie_breaker.node_id == client->node_id) {
					memcpy(&tb_node_ring_id, &other_client->last_ring_id, sizeof(struct tlv_ring_id));
					qnetd_log(LOG_DEBUG, "algo-lms: Looking for nominated node ID. found %d (" UTILS_PRI_RING_ID ")",
						  tb_node_id, tb_node_ring_id.node_id, tb_node_ring_id.seq);

				}
				break;
			default:
				qnetd_log(LOG_DEBUG, "algo-lms: denied vote because tie-breaker option is invalid: %d",
					  client->tie_breaker.mode);
				memset(&tb_node_ring_id, 0, sizeof(struct tlv_ring_id));
			}
		}

		if (client->node_id == tb_node_id || tlv_ring_id_eq(&tb_node_ring_id, ring_id)) {
			qnetd_log(LOG_DEBUG, "algo-lms: We are in the same partition (" UTILS_PRI_RING_ID ") as tie-breaker node id %d. ACK",
				  tb_node_ring_id.node_id, tb_node_ring_id.seq, tb_node_id);
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-lms: We are NOT in the same partition (" UTILS_PRI_RING_ID ") as tie-breaker node id %d. NACK",
				  tb_node_ring_id.node_id, tb_node_ring_id.seq, tb_node_id);
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}
	}

	qnetd_algo_free_partitions(&info->partition_list);
	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_lms_client_init(struct qnetd_client *client)
{
	struct qnetd_algo_lms_info *info;

	info = malloc(sizeof(struct qnetd_algo_lms_info));
	if (!info) {
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	memset(info, 0, sizeof(*info));
	client->algorithm_data = info;
	info->last_result = 0; /* status unknown, or NEW */
	TAILQ_INIT(&info->partition_list);
	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * We got the config node list. Simply count the number of available nodes
 * and wait for the quorum list.
 */
enum tlv_reply_error_code
qnetd_algo_lms_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial, enum tlv_vote *result_vote)
{
	struct node_list_entry *node_info;
	struct qnetd_algo_lms_info *info = client->algorithm_data;
	int node_count = 0;

	TAILQ_FOREACH(node_info, nodes, entries) {
		node_count++;
	}
	info->num_config_nodes = node_count;
	qnetd_log(LOG_DEBUG, "algo-lms: cluster %s config_list has %d nodes", client->cluster_name, node_count);

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

/*
 * membership node list. This is where we get to work.
 */

enum tlv_reply_error_code
qnetd_algo_lms_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, const struct tlv_ring_id *ring_id,
    const struct node_list *nodes, enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{
	qnetd_log(LOG_DEBUG, " ");
	qnetd_log(LOG_DEBUG, "algo-lms: membership list from node %d partition (" UTILS_PRI_RING_ID ")", client->node_id, ring_id->node_id, ring_id->seq);

	return do_lms_algorithm(client, ring_id, result_vote);
}

/*
 * The quorum node list is received after corosync has decided which nodes are in the cluster.
 * We run our algorithm again to be sure that things still match. By this time we will (or should)
 * all know the current ring_id (not guaranteed when the membership list is received). So this
 * might be the most reliable return.
 */
enum tlv_reply_error_code
qnetd_algo_lms_quorum_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_quorate quorate, const struct node_list *nodes, enum tlv_vote *result_vote)
{
	qnetd_log(LOG_DEBUG, " ");
	qnetd_log(LOG_DEBUG, "algo-lms: quorum node list from node %d partition (" UTILS_PRI_RING_ID ")", client->node_id, client->last_ring_id.node_id, client->last_ring_id.seq);
	return do_lms_algorithm(client, &client->last_ring_id, result_vote);
}

/*
 * Called after client disconnect. Client structure is still existing (and it's part
 * of a client->cluster), but it is destroyed (and removed from cluster) right after
 * this callback finishes. Callback is used mainly for destroing client->algorithm_data.
 */
void
qnetd_algo_lms_client_disconnect(struct qnetd_client *client, int server_going_down)
{
	qnetd_log(LOG_DEBUG, "algo-lms: Client %p (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "disconnect", client, client->cluster_name, client->node_id);

	qnetd_log(LOG_INFO, "algo-lms:   server going down %u", server_going_down);

	free(client->algorithm_data);
}

/*
 * Called after client sent ask for vote message. This is usually happening after server
 * replied TLV_VOTE_WAIT_FOR_REPLY.
 */
enum tlv_reply_error_code
qnetd_algo_lms_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_vote *result_vote)
{
	qnetd_log(LOG_DEBUG, " ");
	qnetd_log(LOG_DEBUG, "algo-lms: Client %p (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "asked for a vote", client, client->cluster_name, client->node_id);

	return do_lms_algorithm(client, &client->last_ring_id, result_vote);
}

enum tlv_reply_error_code
qnetd_algo_lms_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{
	qnetd_log(LOG_DEBUG, "algo-lms: Client %p (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "replied back to vote info message", client, client->cluster_name, client->node_id);

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_lms_heuristics_change_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-lms: heuristics change is not supported.");

	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_lms_timer_callback(struct qnetd_client *client, int *reschedule_timer,
    int *send_vote, enum tlv_vote *result_vote)
{
	enum tlv_reply_error_code ret;

	qnetd_log(LOG_DEBUG, "algo-lms: Client %p (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "Timer callback", client, client->cluster_name, client->node_id);

	ret = do_lms_algorithm(client, &client->last_ring_id, result_vote);

	if (ret == TLV_REPLY_ERROR_CODE_NO_ERROR &&
	    (*result_vote == TLV_VOTE_ACK || *result_vote == TLV_VOTE_NACK)) {
		*send_vote = 1;
	}

	if (ret == TLV_REPLY_ERROR_CODE_NO_ERROR &&
	    *result_vote == TLV_VOTE_WAIT_FOR_REPLY) {
		/*
		 * Reschedule was called in the do_lms_algorithm but algo_timer is
		 * not stack based so there can only be one. So if do_lms aborted
		 * the active timer, and scheduled it again the timer would be aborted
		 * if reschedule_timer was not set.
		 */
		*reschedule_timer = 1;
	}

	return ret;
}

static struct qnetd_algorithm qnetd_algo_lms = {
	.init				= qnetd_algo_lms_client_init,
	.config_node_list_received	= qnetd_algo_lms_config_node_list_received,
	.membership_node_list_received	= qnetd_algo_lms_membership_node_list_received,
	.quorum_node_list_received	= qnetd_algo_lms_quorum_node_list_received,
	.client_disconnect		= qnetd_algo_lms_client_disconnect,
	.ask_for_vote_received		= qnetd_algo_lms_ask_for_vote_received,
	.vote_info_reply_received	= qnetd_algo_lms_vote_info_reply_received,
	.heuristics_change_received	= qnetd_algo_lms_heuristics_change_received,
	.timer_callback			= qnetd_algo_lms_timer_callback,
};

enum tlv_reply_error_code qnetd_algo_lms_register()
{
	return qnetd_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_LMS, &qnetd_algo_lms);
}
