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


/*
 * This is a simple 'last man standing' algorithm for 2 node clusters
 *
 * If the node is the only one left in the cluster that can see the
 * qdevice server then we return a vote.
 *
 * If more than one node can see the qdevice server but the nodes can't
 * see each other then we return a vote to the lowest nodeID of the two
 *
 * If there are more than two nodes, then we don't return a vote.
 * this is not our job (any other ideas??)
 */

#include <sys/types.h>

#include <string.h>
#include <limits.h>

#include "qnetd-algo-lms.h"
#include "qnetd-log.h"
#include "qnetd-cluster-list.h"

struct qnetd_algo_lms_partition {
	struct tlv_ring_id ring_id;
	struct qnetd_client_list client_list;
	int num_nodes;
	TAILQ_ENTRY(qnetd_algo_lms_partition) entries;
};

struct qnetd_algo_lms_info {
	int num_config_nodes;
	enum tlv_vote last_result;
	struct tlv_ring_id ring_id;
	TAILQ_HEAD( ,qnetd_algo_lms_partition) partition_list;
};

static int rings_eq(const struct tlv_ring_id *ring_id1, const struct tlv_ring_id *ring_id2)
{
	if (ring_id1->node_id == ring_id2->node_id &&
	    ring_id1->seq == ring_id2->seq) {
		return 1;
	}
	else {
		return 0;
	}
}

static struct qnetd_algo_lms_partition *find_partition(struct qnetd_algo_lms_info *info, const struct tlv_ring_id *ring_id)
{
	struct qnetd_algo_lms_partition *cur_partition;

	TAILQ_FOREACH(cur_partition, &info->partition_list, entries) {
		if (rings_eq(&cur_partition->ring_id, ring_id)) {
			return cur_partition;
		}
		qnetd_log(LOG_DEBUG, "algo-lms: partition %d/%ld not matched to %d/%ld", ring_id->node_id, ring_id->seq, cur_partition->ring_id.node_id, cur_partition->ring_id.seq);
	}

	return NULL;
}

static int create_partitions(struct qnetd_client *client,
			     const struct tlv_ring_id *ring_id)
{
 	struct qnetd_client *other_client;
	struct qnetd_algo_lms_info *info = client->algorithm_data;
	int num_partitions = 0;

	TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
		struct qnetd_algo_lms_info *other_info = other_client->algorithm_data;
		struct qnetd_algo_lms_partition *partition;

		if (other_info->ring_id.seq == 0){
			continue; /* not initialised yet */
		}
		partition = find_partition(info, &other_info->ring_id);
		if (!partition) {
			partition = malloc(sizeof(struct qnetd_algo_lms_partition));
			if (!partition) {
				return -1;
			}
			partition->num_nodes = 0;
			memcpy(&partition->ring_id, &other_info->ring_id, sizeof(*ring_id));
			num_partitions++;
			TAILQ_INSERT_TAIL(&info->partition_list, partition, entries);
		}
		partition->num_nodes++;
		qnetd_log(LOG_DEBUG, "algo-lms: partition %d/%ld (%p) has %d nodes", ring_id->node_id, ring_id->seq, partition, partition->num_nodes);
	}
	return num_partitions;
}


static void free_partitions(struct qnetd_algo_lms_info *info)
{
	struct qnetd_algo_lms_partition *cur_partition;

	TAILQ_FOREACH(cur_partition, &info->partition_list, entries) {
		TAILQ_REMOVE(&info->partition_list, cur_partition, entries);
		free(cur_partition);
	}
}

/*
 * Returns -1 if any node that is supposedly in the same cluster partition
 * as us has a different ring_id.
 * If this happens it simply means that qnetd does not yet have the full current view
 * of the cluster and should wait until all of the ring_ids in this membership list match up
 */
static int ring_ids_match(struct qnetd_client *client, const struct tlv_ring_id *ring_id)
{
	struct node_list_entry *node_info;
 	struct qnetd_client *other_client;

	TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
		struct qnetd_algo_lms_info *other_info = other_client->algorithm_data;
		int seen_us = 0;

		if (other_client == client) {
			continue; /* We've seen our membership list */
		}

		/*
		 * If the other nodes on our side of a partition have a different ring ID then
		 * we need to wait until they have all caught up before making a decision
		 */
		TAILQ_FOREACH(node_info, &other_client->last_membership_node_list, entries) {
			if (node_info->node_state == TLV_NODE_STATE_MEMBER && node_info->node_id == client->node_id) {
				seen_us = 1;
			}
		}

		if (seen_us && !rings_eq(ring_id, &other_info->ring_id)) {
			qnetd_log(LOG_DEBUG, "algo-lms: nodeid %d has different ring_id (%d/%ld) to us (%d/%ld)", other_client->node_id, other_info->ring_id.node_id, other_info->ring_id.seq, ring_id->node_id, ring_id->seq);
			return -1; /* ring IDs don't match */
		}
	}
	return 0;
}

static enum tlv_reply_error_code do_lms_algorithm(struct qnetd_client *client,  enum tlv_vote *result_vote)
{
 	struct qnetd_client *other_client;
	struct qnetd_algo_lms_info *info = client->algorithm_data;
	struct qnetd_algo_lms_partition *cur_partition;
	struct qnetd_algo_lms_partition *largest_partition;
	int num_partitions;
	int joint_leader;

	if (ring_ids_match(client, &info->ring_id) == -1) {
		qnetd_log(LOG_DEBUG, "algo-lms: nodeid %d: ring ID %d/%ld not unique in this membership, waiting", client->node_id, info->ring_id.node_id, info->ring_id.seq);
		*result_vote = info->last_result = TLV_VOTE_ASK_LATER;
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	/* Create and count the number of separate partitions */
	if ( (num_partitions = create_partitions(client, &info->ring_id)) == -1) {
		qnetd_log(LOG_DEBUG, "algo-lms: Error creating partition list");
		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}

	/* Only 1 partition - let votequorum sort it out */
	if (num_partitions == 1) {
		qnetd_log(LOG_DEBUG, "algo-lms: Only 1 partition. This is votequorum's problem, not ours");
		free_partitions(info);
		*result_vote = info->last_result = TLV_VOTE_ACK;
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}


	/* If we're a newcomer and there is another active partition, then we must NACK
	 * to avoid quorum moving to us from already active nodes.
	 */
	if (info->last_result == 0) {
		TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
			struct qnetd_algo_lms_info *other_info = other_client->algorithm_data;
			if (!rings_eq(&info->ring_id, &other_info->ring_id) &&
			    other_info->last_result == TLV_VOTE_ACK) {
				free_partitions(info);

				/* Don't save NACK, we need to know subsequently if we haven't been voting */
				*result_vote = TLV_VOTE_NACK;
				qnetd_log(LOG_DEBUG, "algo-lms: we are a new partition and another active partition exists. NACK");
				return (TLV_REPLY_ERROR_CODE_NO_ERROR);
			}
		}
	}

	/* Find the largest partition */
	largest_partition = NULL;
	TAILQ_FOREACH(cur_partition, &info->partition_list, entries) {
		if (!largest_partition ||
		    largest_partition->num_nodes < cur_partition->num_nodes) {
			largest_partition = cur_partition;
		}
	}

	qnetd_log(LOG_DEBUG, "algo-lms: largest partition is %d/%ld with %d nodes", largest_partition->ring_id.node_id, largest_partition->ring_id.seq, largest_partition->num_nodes);

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
		if (rings_eq(&largest_partition->ring_id, &info->ring_id)) {
			qnetd_log(LOG_DEBUG, "algo-lms: We are in the largest partition. ACK\n");
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-lms: We are NOT in the largest partition. NACK\n");
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}
	}
	else {
		int low_node_id = INT_MAX;
		struct tlv_ring_id low_node_ring_id = {0LL, 0};
		/* Look for the lowest node ID */
		/* TODO: other tie-breakers */

		TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
			struct qnetd_algo_lms_info *other_info = other_client->algorithm_data;
			if (other_client->node_id < low_node_id) {
				low_node_id = other_client->node_id;
				memcpy(&low_node_ring_id, &other_info->ring_id, sizeof(struct tlv_ring_id));
				qnetd_log(LOG_DEBUG, "algo-lms: Looking for low node ID. found (%d/%ld)  %d", low_node_ring_id.node_id, low_node_ring_id.seq, low_node_id);
			}
		}
		if (rings_eq(&low_node_ring_id, &info->ring_id)) {
			qnetd_log(LOG_DEBUG, "algo-lms: We are in the same partition (%d/%ld) as low node id %d. ACK", low_node_ring_id.node_id, low_node_ring_id.seq, low_node_id);
			*result_vote = info->last_result = TLV_VOTE_ACK;
		}
		else {
			qnetd_log(LOG_DEBUG, "algo-lms: We are NOT in the same partition (%d/%ld) as low node id %d. NACK",  low_node_ring_id.node_id, low_node_ring_id.seq, low_node_id);
			*result_vote = info->last_result = TLV_VOTE_NACK;
		}
	}

	free_partitions(info);
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
 * on failure (error is send back to client)
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

	*result_vote = TLV_VOTE_ASK_LATER;
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
 * on failure (error is send back to client)
 */

enum tlv_reply_error_code
qnetd_algo_lms_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct tlv_ring_id *ring_id, enum tlv_quorate quorate,
    const struct node_list *nodes, enum tlv_vote *result_vote)
{
	struct qnetd_algo_lms_info *info = client->algorithm_data;

	/* Save this now */
	memcpy(&info->ring_id, ring_id, sizeof(*ring_id));
	qnetd_log(LOG_DEBUG, "\nalgo-lms: membership list from node %d partition %d/%ld", client->node_id, ring_id->node_id, ring_id->seq);

	return do_lms_algorithm(client, result_vote);
}

/*
 * Called after client disconnect. Client structure is still existing (and it's part
 * of a client->cluster), but it is destroyed (and removed from cluster) right after
 * this callback finishes. Callback is used mainly for destroing client->algorithm_data.
 */
void
qnetd_algo_lms_client_disconnect(struct qnetd_client *client, int server_going_down)
{
	qnetd_log(LOG_DEBUG, "\nalgo-lms: Client %p (cluster %s, node_id %"PRIx32") "
	    "disconnect", client, client->cluster_name, client->node_id);

	qnetd_log(LOG_INFO, "algo-lms:   server going down %u", server_going_down);

	free(client->algorithm_data);
}

/*
 * Called after client sent ask for vote message. This is usually happening after server
 * replied TLV_VOTE_ASK_LATER.
 */
enum tlv_reply_error_code
qnetd_algo_lms_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_vote *result_vote)
{
	qnetd_log(LOG_DEBUG, "\nalgo-lms: Client %p (cluster %s, node_id %"PRIx32") "
	    "asked for a vote", client, client->cluster_name, client->node_id);

	return do_lms_algorithm(client, result_vote);
}

enum tlv_reply_error_code
qnetd_algo_lms_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	qnetd_log(LOG_DEBUG, "\nalgo-lms: Client %p (cluster %s, node_id %"PRIx32") "
	    "replied back to vote info message", client, client->cluster_name, client->node_id);

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}


static struct qnetd_algorithm qnetd_algo_lms = {
	.init                          = qnetd_algo_lms_client_init,
	.config_node_list_received     = qnetd_algo_lms_config_node_list_received,
	.membership_node_list_received = qnetd_algo_lms_membership_node_list_received,
	.client_disconnect             = qnetd_algo_lms_client_disconnect,
	.ask_for_vote_received         = qnetd_algo_lms_ask_for_vote_received,
	.vote_info_reply_received      = qnetd_algo_lms_vote_info_reply_received,
};

enum tlv_reply_error_code qnetd_algo_lms_register()
{
	return qnetd_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_LMS, &qnetd_algo_lms);
}
