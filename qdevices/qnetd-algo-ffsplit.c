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

#include "qnetd-algo-ffsplit.h"
#include "qnetd-log.h"
#include "qnetd-log-debug.h"
#include "qnetd-cluster-list.h"
#include "qnetd-cluster.h"
#include "qnetd-client-send.h"

enum qnetd_algo_ffsplit_cluster_state {
	QNETD_ALGO_FFSPLIT_CLUSTER_STATE_WAITING_FOR_CHANGE,
	QNETD_ALGO_FFSPLIT_CLUSTER_STATE_WAITING_FOR_STABLE_MEMBERSHIP,
	QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_NACKS,
	QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_ACKS,
};

struct qnetd_algo_ffsplit_cluster_data {
	enum qnetd_algo_ffsplit_cluster_state cluster_state;
	const struct node_list *quorate_partition_node_list;
};

enum qnetd_algo_ffsplit_client_state {
	QNETD_ALGO_FFSPLIT_CLIENT_STATE_WAITING_FOR_CHANGE,
	QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_NACK,
	QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_ACK,
};

struct qnetd_algo_ffsplit_client_data {
	enum qnetd_algo_ffsplit_client_state client_state;
	uint32_t vote_info_expected_seq_num;
};

enum tlv_reply_error_code
qnetd_algo_ffsplit_client_init(struct qnetd_client *client)
{
	struct qnetd_algo_ffsplit_cluster_data *cluster_data;
	struct qnetd_algo_ffsplit_client_data *client_data;

	if (qnetd_cluster_size(client->cluster) == 1) {
		cluster_data = malloc(sizeof(*cluster_data));
		if (cluster_data == NULL) {
			qnetd_log(LOG_ERR, "ffsplit: Can't initialize cluster data for client %s",
			    client->addr_str);

			return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
		}
		memset(cluster_data, 0, sizeof(*cluster_data));
		cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_WAITING_FOR_CHANGE;
		cluster_data->quorate_partition_node_list = NULL;

		client->cluster->algorithm_data = cluster_data;
	}

	client_data = malloc(sizeof(*client_data));
	if (client_data == NULL) {
		qnetd_log(LOG_ERR, "ffsplit: Can't initialize node data for client %s",
		    client->addr_str);

		return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
	}
	memset(client_data, 0, sizeof(*client_data));
	client_data->client_state = QNETD_ALGO_FFSPLIT_CLIENT_STATE_WAITING_FOR_CHANGE;
	client->algorithm_data = client_data;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static int
qnetd_algo_ffsplit_is_prefered_partition(const struct qnetd_client *client,
    const struct node_list *config_node_list, const struct node_list *membership_node_list)
{
	uint32_t prefered_node_id;
	struct node_list_entry *node_entry;
	int case_processed;

	prefered_node_id = 0;
	case_processed = 0;

	switch (client->tie_breaker.mode) {
	case TLV_TIE_BREAKER_MODE_LOWEST:
		node_entry = TAILQ_FIRST(config_node_list);

		prefered_node_id = node_entry->node_id;

		TAILQ_FOREACH(node_entry, config_node_list, entries) {
			if (node_entry->node_id < prefered_node_id) {
				prefered_node_id = node_entry->node_id;
			}
		}
		case_processed = 1;
		break;
	case TLV_TIE_BREAKER_MODE_HIGHEST:
		node_entry = TAILQ_FIRST(config_node_list);

		prefered_node_id = node_entry->node_id;

		TAILQ_FOREACH(node_entry, config_node_list, entries) {
			if (node_entry->node_id > prefered_node_id) {
				prefered_node_id = node_entry->node_id;
			}
		}
		case_processed = 1;
		break;
	case TLV_TIE_BREAKER_MODE_NODE_ID:
		prefered_node_id = client->tie_breaker.node_id;
		case_processed = 1;
		break;
	}

	if (!case_processed) {
		qnetd_log(LOG_CRIT, "qnetd_algo_ffsplit_is_prefered_partition unprocessed "
		    "tie_breaker.mode");
		exit(1);
	}

	return (node_list_find_node_id(membership_node_list, prefered_node_id) != NULL);
}

static int
qnetd_algo_ffsplit_is_membership_stable(const struct qnetd_client *client, int client_leaving,
    const struct tlv_ring_id *ring_id, const struct node_list *config_node_list,
    const struct node_list *membership_node_list)
{
	const struct qnetd_client *iter_client1, *iter_client2;
	const struct node_list *config_node_list1, *config_node_list2;
	const struct node_list *membership_node_list1, *membership_node_list2;
	const struct node_list_entry *iter_node1, *iter_node2;
	const struct node_list_entry *iter_node3, *iter_node4;
	const struct tlv_ring_id *ring_id1, *ring_id2;

	/*
	 * Test if all active clients share same config list.
	 */
	TAILQ_FOREACH(iter_client1, &client->cluster->client_list, cluster_entries) {
		TAILQ_FOREACH(iter_client2, &client->cluster->client_list, cluster_entries) {
			if (iter_client1 == iter_client2) {
				continue;
			}

			if (iter_client1->node_id == client->node_id) {
				if (client_leaving) {
					continue;
				}

				config_node_list1 = config_node_list;
			} else {
				config_node_list1 = &iter_client1->configuration_node_list;
			}

			if (iter_client2->node_id == client->node_id) {
				if (client_leaving) {
					continue;
				}

				config_node_list2 = config_node_list;
			} else {
				config_node_list2 = &iter_client2->configuration_node_list;
			}

			/*
			 * Walk thru all node ids in given config node list...
			 */
			TAILQ_FOREACH(iter_node1, config_node_list1, entries) {
				/*
				 * ... and try to find given node id in other list
				 */
				iter_node2 = node_list_find_node_id(config_node_list2, iter_node1->node_id);

				if (iter_node2 == NULL) {
					/*
					 * Node with iter_node1->node_id was not found in
					 * config_node_list2 -> lists doesn't match
					 */
					return (0);
				}
			}
		}
	}

	/*
	 * Test if same partitions share same ring ids and membership node list
	 */
	TAILQ_FOREACH(iter_client1, &client->cluster->client_list, cluster_entries) {
		if (iter_client1->node_id == client->node_id) {
			if (client_leaving) {
				continue;
			}

			membership_node_list1 = membership_node_list;
			ring_id1 = ring_id;
		} else {
			membership_node_list1 = &iter_client1->last_membership_node_list;
			ring_id1 = &iter_client1->last_ring_id;
		}

		/*
		 * Walk thru all memberships nodes
		 */
		TAILQ_FOREACH(iter_node1, membership_node_list1, entries) {
			/*
			 * try to find client with given node id
			 */
			iter_client2 = qnetd_cluster_find_client_by_node_id(client->cluster,
			    iter_node1->node_id);
			if (iter_client2 == NULL) {
				/*
				 * Client with given id is not connected
				 */
				continue;
			}

			if (iter_client2->node_id == client->node_id) {
				if (client_leaving) {
					continue;
				}

				membership_node_list2 = membership_node_list;
				ring_id2 = ring_id;
			} else {
				membership_node_list2 = &iter_client2->last_membership_node_list;
				ring_id2 = &iter_client2->last_ring_id;
			}

			/*
			 * Compare ring ids
			 */
			if (!tlv_ring_id_eq(ring_id1, ring_id2)) {
				return (0);
			}

			/*
			 * Now compare that membership node list equals, so walk thru all
			 * members ...
			 */
			TAILQ_FOREACH(iter_node3, membership_node_list1, entries) {
				/*
				 * ... and try to find given node id in other membership node list
				 */
				iter_node4 = node_list_find_node_id(membership_node_list2, iter_node3->node_id);

				if (iter_node4 == NULL) {
					/*
					 * Node with iter_node3->node_id was not found in
					 * membership_node_list2 -> lists doesn't match
					 */
					return (0);
				}
			}
		}
	}

	return (1);
}

static void
qnetd_algo_ffsplit_get_active_clients_in_partition_stats(const struct qnetd_client *client,
    const struct node_list *client_membership_node_list, enum tlv_heuristics client_heuristics,
    size_t *no_clients, size_t *no_heuristics_pass, size_t *no_heuristics_fail)
{
	const struct node_list_entry *iter_node;
	const struct qnetd_client *iter_client;
	enum tlv_heuristics iter_heuristics;

	*no_clients = 0;
	*no_heuristics_pass = 0;
	*no_heuristics_fail = 0;

	if (client == NULL || client_membership_node_list == NULL) {
		return ;
	}

	TAILQ_FOREACH(iter_node, client_membership_node_list, entries) {
		iter_client = qnetd_cluster_find_client_by_node_id(client->cluster,
		    iter_node->node_id);
		if (iter_client != NULL) {
			(*no_clients)++;

			if (iter_client == client) {
				iter_heuristics = client_heuristics;
			} else {
				iter_heuristics = iter_client->last_heuristics;
			}

			if (iter_heuristics == TLV_HEURISTICS_PASS) {
				(*no_heuristics_pass)++;
			} else if (iter_heuristics == TLV_HEURISTICS_FAIL) {
				(*no_heuristics_fail)++;
			}
		}
	}
}

/*
 * Compares two partitions. Return 1 if client1, config_node_list1, membership_node_list1 is
 * "better" than client2, config_node_list2, membership_node_list2
 */
static int
qnetd_algo_ffsplit_partition_cmp(const struct qnetd_client *client1,
    const struct node_list *config_node_list1, const struct node_list *membership_node_list1,
    enum tlv_heuristics heuristics_1,
    const struct qnetd_client *client2,
    const struct node_list *config_node_list2, const struct node_list *membership_node_list2,
    enum tlv_heuristics heuristics_2)
{
	size_t part1_active_clients, part2_active_clients;
	size_t part1_no_heuristics_pass, part2_no_heuristics_pass;
	size_t part1_no_heuristics_fail, part2_no_heuristics_fail;
	size_t part1_score, part2_score;

	int res;

	res = -1;

	if (node_list_size(config_node_list1) % 2 != 0) {
		/*
		 * Odd clusters never split into 50:50.
		 */
		if (node_list_size(membership_node_list1) > node_list_size(config_node_list1) / 2) {
			res = 1; goto exit_res;
		} else {
			res = 0; goto exit_res;
		}
	} else {
		if (node_list_size(membership_node_list1) > node_list_size(config_node_list1) / 2) {
			res = 1; goto exit_res;
		} else if (node_list_size(membership_node_list1) < node_list_size(config_node_list1) / 2) {
			res = 0; goto exit_res;
		}

		/*
		 * 50:50 split
		 */

		/*
		 * Check how many active clients are in partitions and heuristics results
		 */
		qnetd_algo_ffsplit_get_active_clients_in_partition_stats(client1,
		    membership_node_list1, heuristics_1, &part1_active_clients,
		    &part1_no_heuristics_pass, &part1_no_heuristics_fail);
		qnetd_algo_ffsplit_get_active_clients_in_partition_stats(client2,
		    membership_node_list2, heuristics_2, &part2_active_clients,
		    &part2_no_heuristics_pass, &part2_no_heuristics_fail);

		/*
		 * Partition can contain clients with one of 4 states:
		 * 1. Not-connected to qnetd (D)
		 * 2. Disabled heuristics (U)
		 * 3. Enabled heuristics with pass result (P)
		 * 4. Enabled heuristics with fail result (F)
		 *
		 * The question is, what partition should get vote is kind of hard with
		 * so much states. Following simple "score" seems to be good enough, but may
		 * be suboptimal in some cases. As and example let's say there are
		 * 2 partitions with 4 nodes each. Partition 1 looks like PDDD and partition 2 looks
		 * like FUUU. Partition 1 score is 1 + (1 - 0), partition 2 score is 4 + (0 - 1).
		 * Partition 2 wins eventho there is one processor with failed heuristics.
		 */
		part1_score = part1_active_clients + (part1_no_heuristics_pass - part1_no_heuristics_fail);
		part2_score = part2_active_clients + (part2_no_heuristics_pass - part2_no_heuristics_fail);

		if (part1_score > part2_score) {
			res = 1; goto exit_res;
		} else if (part1_score < part2_score) {
			res = 0; goto exit_res;
		}

		if (part1_active_clients > part2_active_clients) {
			res = 1; goto exit_res;
		} else if (part1_active_clients < part2_active_clients) {
			res = 0; goto exit_res;
		}

		/*
		 * Number of active clients in both partitions equals. Use tie-breaker.
		 */

		if (qnetd_algo_ffsplit_is_prefered_partition(client1, config_node_list1,
		    membership_node_list1)) {
			res = 1; goto exit_res;
		} else {
			res = 0; goto exit_res;
		}
	}

exit_res:
	if (res == -1) {
		qnetd_log(LOG_CRIT, "qnetd_algo_ffsplit_partition_cmp unhandled case");
		exit(1);
		/* NOTREACHED */
	}

	return (res);
}

/*
 * Select best partition for given client->cluster.
 * If there is no partition which could become quorate, NULL is returned
 */
static const struct node_list *
qnetd_algo_ffsplit_select_partition(const struct qnetd_client *client, int client_leaving,
    const struct node_list *config_node_list, const struct node_list *membership_node_list,
    enum tlv_heuristics client_heuristics)
{
	const struct qnetd_client *iter_client;
	const struct qnetd_client *best_client;
	const struct node_list *best_config_node_list, *best_membership_node_list;
	const struct node_list *iter_config_node_list, *iter_membership_node_list;
	enum tlv_heuristics iter_heuristics, best_heuristics;

	best_client = NULL;
	best_config_node_list = best_membership_node_list = NULL;
	best_heuristics = TLV_HEURISTICS_UNDEFINED;

	/*
	 * Get highest score
	 */
	TAILQ_FOREACH(iter_client, &client->cluster->client_list, cluster_entries) {
		if (iter_client->node_id == client->node_id) {
			if (client_leaving) {
				continue;
			}

			iter_config_node_list = config_node_list;
			iter_membership_node_list = membership_node_list;
			iter_heuristics = client_heuristics;
		} else {
			iter_config_node_list = &iter_client->configuration_node_list;
			iter_membership_node_list = &iter_client->last_membership_node_list;
			iter_heuristics = iter_client->last_heuristics;
		}

		if (qnetd_algo_ffsplit_partition_cmp(iter_client, iter_config_node_list,
		    iter_membership_node_list, iter_heuristics, best_client, best_config_node_list,
		    best_membership_node_list, best_heuristics) > 0) {
			best_client = iter_client;
			best_config_node_list = iter_config_node_list;
			best_membership_node_list = iter_membership_node_list;
			best_heuristics = iter_heuristics;
		}
	}

	return (best_membership_node_list);
}

/*
 * Update state of all nodes to match quorate_partition_node_list
 */
static void
qnetd_algo_ffsplit_update_nodes_state(struct qnetd_client *client, int client_leaving,
    const struct node_list *quorate_partition_node_list)
{
	const struct qnetd_client *iter_client;
	struct qnetd_algo_ffsplit_client_data *iter_client_data;

	TAILQ_FOREACH(iter_client, &client->cluster->client_list, cluster_entries) {
		iter_client_data = (struct qnetd_algo_ffsplit_client_data *)iter_client->algorithm_data;

		if (iter_client->node_id == client->node_id && client_leaving) {
			iter_client_data->client_state = QNETD_ALGO_FFSPLIT_CLIENT_STATE_WAITING_FOR_CHANGE;

			continue;
		}

		if (quorate_partition_node_list == NULL ||
		    node_list_find_node_id(quorate_partition_node_list, iter_client->node_id) == NULL) {
			iter_client_data->client_state = QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_NACK;
		} else {
			iter_client_data->client_state = QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_ACK;
		}
	}
}

/*
 * Send vote info. If client_leaving is set, client is ignored. if send_acks
 * is set, only ACK votes are sent (nodes in QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_ACK state),
 * otherwise only NACK votes are sent (nodes in QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_NACK state)
 *
 * Returns number of send votes
 */
static size_t
qnetd_algo_ffsplit_send_votes(struct qnetd_client *client, int client_leaving,
    const struct tlv_ring_id *ring_id, int send_acks)
{
	size_t sent_votes;
	struct qnetd_client *iter_client;
	struct qnetd_algo_ffsplit_client_data *iter_client_data;
	const struct tlv_ring_id *ring_id_to_send;
	enum tlv_vote vote_to_send;

	sent_votes = 0;

	TAILQ_FOREACH(iter_client, &client->cluster->client_list, cluster_entries) {
		if (iter_client->node_id == client->node_id) {
			if (client_leaving) {
				continue;
			}

			ring_id_to_send = ring_id;
		} else {
			ring_id_to_send = &iter_client->last_ring_id;
		}

		iter_client_data = (struct qnetd_algo_ffsplit_client_data *)iter_client->algorithm_data;
		vote_to_send = TLV_VOTE_UNDEFINED;

		if (send_acks) {
			if (iter_client_data->client_state == QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_ACK) {
				vote_to_send = TLV_VOTE_ACK;
			}
		} else {
			if (iter_client_data->client_state == QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_NACK) {
				vote_to_send = TLV_VOTE_NACK;
			}
		}

		if (vote_to_send != TLV_VOTE_UNDEFINED) {
			iter_client_data->vote_info_expected_seq_num++;
			sent_votes++;

			if (qnetd_client_send_vote_info(iter_client,
			    iter_client_data->vote_info_expected_seq_num, ring_id_to_send,
			    vote_to_send) == -1) {
				client->schedule_disconnect = 1;
			}
		}
	}

	return (sent_votes);
}

/*
 * Return number of clients in QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_ACK state if sending_acks is
 * set or number of nodes in QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_NACK state if sending_acks is
 * not set
 */
static size_t
qnetd_algo_ffsplit_no_clients_in_sending_state(struct qnetd_client *client, int sending_acks)
{
	size_t no_clients;
	struct qnetd_client *iter_client;
	struct qnetd_algo_ffsplit_client_data *iter_client_data;

	no_clients = 0;

	TAILQ_FOREACH(iter_client, &client->cluster->client_list, cluster_entries) {
		iter_client_data = (struct qnetd_algo_ffsplit_client_data *)iter_client->algorithm_data;

		if (sending_acks &&
		    iter_client_data->client_state == QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_ACK) {
			no_clients++;
		}

		if (!sending_acks &&
		    iter_client_data->client_state == QNETD_ALGO_FFSPLIT_CLIENT_STATE_SENDING_NACK) {
			no_clients++;
		}
	}

	return (no_clients);
}

static enum tlv_vote
qnetd_algo_ffsplit_do(struct qnetd_client *client, int client_leaving,
    const struct tlv_ring_id *ring_id, const struct node_list *config_node_list,
    const struct node_list *membership_node_list, enum tlv_heuristics client_heuristics)
{
	struct qnetd_algo_ffsplit_cluster_data *cluster_data;
	const struct node_list *quorate_partition_node_list;

	cluster_data = (struct qnetd_algo_ffsplit_cluster_data *)client->cluster->algorithm_data;

	cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_WAITING_FOR_STABLE_MEMBERSHIP;

	if (!qnetd_algo_ffsplit_is_membership_stable(client, client_leaving,
	    ring_id, config_node_list, membership_node_list)) {
		/*
		 * Wait until membership is stable
		 */
		qnetd_log(LOG_DEBUG, "ffsplit: Membership for cluster %s is not yet stable", client->cluster_name);

		return (TLV_VOTE_WAIT_FOR_REPLY);
	}

	qnetd_log(LOG_DEBUG, "ffsplit: Membership for cluster %s is now stable", client->cluster_name);

	quorate_partition_node_list = qnetd_algo_ffsplit_select_partition(client, client_leaving,
	    config_node_list, membership_node_list, client_heuristics);
	cluster_data->quorate_partition_node_list = quorate_partition_node_list;

	if (quorate_partition_node_list == NULL) {
		qnetd_log(LOG_DEBUG, "ffsplit: No quorate partition was selected");
	} else {
		qnetd_log(LOG_DEBUG, "ffsplit: Quorate partition selected");
		qnetd_log_debug_dump_node_list(client, quorate_partition_node_list);
	}

	qnetd_algo_ffsplit_update_nodes_state(client, client_leaving, quorate_partition_node_list);

	cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_NACKS;

	if (qnetd_algo_ffsplit_send_votes(client, client_leaving, ring_id, 0) == 0) {
		qnetd_log(LOG_DEBUG, "ffsplit: No client gets NACK");
		/*
		 * No one gets nack -> send acks
		 */
		cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_ACKS;

		if (qnetd_algo_ffsplit_send_votes(client, client_leaving, ring_id, 1) == 0) {
			qnetd_log(LOG_DEBUG, "ffsplit: No client gets ACK");
			/*
			 * No one gets acks -> finished
			 */
			cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_WAITING_FOR_CHANGE;
		}
	}

	return (TLV_VOTE_NO_CHANGE);
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial, enum tlv_vote *result_vote)
{

	if (node_list_size(nodes) == 0) {
		/*
		 * Empty node list shouldn't happen
		 */
		qnetd_log(LOG_ERR, "ffsplit: Received empty config node list for client %s",
			    client->addr_str);

		return (TLV_REPLY_ERROR_CODE_INVALID_CONFIG_NODE_LIST);
	}

	if (node_list_find_node_id(nodes, client->node_id) == NULL) {
		/*
		 * Current node is not in node list
		 */
		qnetd_log(LOG_ERR, "ffsplit: Received config node list without client %s",
			    client->addr_str);

		return (TLV_REPLY_ERROR_CODE_INVALID_CONFIG_NODE_LIST);
	}

	if (initial || node_list_size(&client->last_membership_node_list) == 0) {
		/*
		 * Initial node list -> membership is going to be send by client
		 */
		*result_vote = TLV_VOTE_ASK_LATER;
	} else {
		*result_vote = qnetd_algo_ffsplit_do(client, 0, &client->last_ring_id,
		    nodes, &client->last_membership_node_list, client->last_heuristics);
	}

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
qnetd_algo_ffsplit_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, const struct tlv_ring_id *ring_id,
    const struct node_list *nodes, enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	if (node_list_size(nodes) == 0) {
		/*
		 * Empty node list shouldn't happen
		 */
		qnetd_log(LOG_ERR, "ffsplit: Received empty membership node list for client %s",
			    client->addr_str);

		return (TLV_REPLY_ERROR_CODE_INVALID_MEMBERSHIP_NODE_LIST);
	}

	if (node_list_find_node_id(nodes, client->node_id) == NULL) {
		/*
		 * Current node is not in node list
		 */
		qnetd_log(LOG_ERR, "ffsplit: Received membership node list without client %s",
			    client->addr_str);

		return (TLV_REPLY_ERROR_CODE_INVALID_MEMBERSHIP_NODE_LIST);
	}

	if (node_list_size(&client->configuration_node_list) == 0) {
		/*
		 * Config node list not received -> it's going to be sent later
		 */
		*result_vote = TLV_VOTE_ASK_LATER;
	} else {
		*result_vote = qnetd_algo_ffsplit_do(client, 0, ring_id,
		    &client->configuration_node_list, nodes, heuristics);
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_quorum_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_quorate quorate, const struct node_list *nodes,
    enum tlv_vote *result_vote)
{

	/*
	 * Quorum node list is informative -> no change
	 */
	*result_vote = TLV_VOTE_NO_CHANGE;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

void
qnetd_algo_ffsplit_client_disconnect(struct qnetd_client *client, int server_going_down)
{

	(void)qnetd_algo_ffsplit_do(client, 1, &client->last_ring_id,
	    &client->configuration_node_list, &client->last_membership_node_list,
	    client->last_heuristics);

	free(client->algorithm_data);

	if (qnetd_cluster_size(client->cluster) == 1) {
		/*
		 * Last client in the cluster
		 */
		 free(client->cluster->algorithm_data);
	}
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_vote *result_vote)
{

	/*
	 * Ask for vote is not supported in current algorithm
	 */
	return (TLV_REPLY_ERROR_CODE_UNSUPPORTED_DECISION_ALGORITHM_MESSAGE);
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{
	struct qnetd_algo_ffsplit_cluster_data *cluster_data;
	struct qnetd_algo_ffsplit_client_data *client_data;

	cluster_data = (struct qnetd_algo_ffsplit_cluster_data *)client->cluster->algorithm_data;
	client_data = (struct qnetd_algo_ffsplit_client_data *)client->algorithm_data;

	if (client_data->vote_info_expected_seq_num != msg_seq_num) {
		qnetd_log(LOG_DEBUG, "ffsplit: Received old vote info reply from client %s",
		    client->addr_str);

		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	client_data->client_state = QNETD_ALGO_FFSPLIT_CLIENT_STATE_WAITING_FOR_CHANGE;

	if (cluster_data->cluster_state != QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_NACKS &&
	    cluster_data->cluster_state != QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_ACKS) {
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	if (cluster_data->cluster_state == QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_NACKS) {
		if (qnetd_algo_ffsplit_no_clients_in_sending_state(client, 0) == 0) {
			qnetd_log(LOG_DEBUG, "ffsplit: All NACK votes sent for cluster %s",
			     client->cluster_name);

			cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_SENDING_ACKS;

			if (qnetd_algo_ffsplit_send_votes(client, 0, &client->last_ring_id, 1) == 0) {
				qnetd_log(LOG_DEBUG, "ffsplit: No client gets ACK");
				/*
				 * No one gets acks -> finished
				 */
				cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_WAITING_FOR_CHANGE;
			}
		}
	} else {
		if (qnetd_algo_ffsplit_no_clients_in_sending_state(client, 1) == 0) {
			qnetd_log(LOG_DEBUG, "ffsplit: All ACK votes sent for cluster %s",
			     client->cluster_name);

			cluster_data->cluster_state = QNETD_ALGO_FFSPLIT_CLUSTER_STATE_WAITING_FOR_CHANGE;
		}
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_heuristics_change_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_heuristics heuristics, enum tlv_vote *result_vote)
{

	if (node_list_size(&client->configuration_node_list) == 0 ||
	    node_list_size(&client->last_membership_node_list) == 0) {
		/*
		 * Config or membership node list not received -> it's going to be sent later
		 */
		*result_vote = TLV_VOTE_ASK_LATER;
	} else {
		*result_vote = qnetd_algo_ffsplit_do(client, 0, &client->last_ring_id,
		    &client->configuration_node_list, &client->last_membership_node_list,
		    heuristics);
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_timer_callback(struct qnetd_client *client, int *reschedule_timer,
    int *send_vote, enum tlv_vote *result_vote)
{

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static struct qnetd_algorithm qnetd_algo_ffsplit = {
	.init				= qnetd_algo_ffsplit_client_init,
	.config_node_list_received	= qnetd_algo_ffsplit_config_node_list_received,
	.membership_node_list_received	= qnetd_algo_ffsplit_membership_node_list_received,
	.quorum_node_list_received	= qnetd_algo_ffsplit_quorum_node_list_received,
	.client_disconnect		= qnetd_algo_ffsplit_client_disconnect,
	.ask_for_vote_received		= qnetd_algo_ffsplit_ask_for_vote_received,
	.vote_info_reply_received	= qnetd_algo_ffsplit_vote_info_reply_received,
	.heuristics_change_received	= qnetd_algo_ffsplit_heuristics_change_received,
	.timer_callback			= qnetd_algo_ffsplit_timer_callback,
};

enum tlv_reply_error_code qnetd_algo_ffsplit_register()
{

	return (qnetd_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_FFSPLIT, &qnetd_algo_ffsplit));
}
