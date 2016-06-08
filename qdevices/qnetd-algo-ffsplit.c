/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
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
#include "qnetd-cluster-list.h"
#include "qnetd-cluster.h"

struct ffsplit_cluster_data {
	uint8_t leader_set;
	uint32_t leader_id;
};

enum tlv_reply_error_code
qnetd_algo_ffsplit_client_init(struct qnetd_client *client)
{
	struct ffsplit_cluster_data *cluster_data;

	if (qnetd_cluster_size(client->cluster) == 1) {
		cluster_data = malloc(sizeof(struct ffsplit_cluster_data));
		if (cluster_data == NULL) {
			qnetd_log(LOG_ERR, "ffsplit: Can't initialize cluster data for client %s",
			    client->addr_str);

			return (TLV_REPLY_ERROR_CODE_INTERNAL_ERROR);
		}
		memset(cluster_data, 0, sizeof(*cluster_data));

		client->cluster->algorithm_data = cluster_data;
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static int
qnetd_algo_ffsplit_is_prefered_partition(struct qnetd_client *client,
    const struct node_list *config_node_list, const struct node_list *membership_node_list)
{
	uint32_t prefered_node_id;
	struct node_list_entry *node_entry;

	switch (client->tie_breaker.mode) {
	case TLV_TIE_BREAKER_MODE_LOWEST:
		node_entry = TAILQ_FIRST(config_node_list);

		prefered_node_id = node_entry->node_id;

		TAILQ_FOREACH(node_entry, config_node_list, entries) {
			if (node_entry->node_id < prefered_node_id) {
				prefered_node_id = node_entry->node_id;
			}
		}
		break;
	case TLV_TIE_BREAKER_MODE_HIGHEST:
		node_entry = TAILQ_FIRST(config_node_list);

		prefered_node_id = node_entry->node_id;

		TAILQ_FOREACH(node_entry, config_node_list, entries) {
			if (node_entry->node_id > prefered_node_id) {
				prefered_node_id = node_entry->node_id;
			}
		}
		break;
	case TLV_TIE_BREAKER_MODE_NODE_ID:
		prefered_node_id = client->tie_breaker.node_id;
		break;
	}

	return (node_list_find_node_id(membership_node_list, prefered_node_id) != NULL);
}

static enum tlv_vote
qnetd_algo_ffsplit_do(struct qnetd_client *client, const struct node_list *config_node_list,
    const struct node_list *membership_node_list)
{
	struct ffplist_cluster_data *cluster_data;

	cluster_data = (struct ffplist_cluster_data *)client->cluster->algorithm_data;

	if (node_list_size(config_node_list) % 2 != 0) {
		/*
		 * Odd clusters never split into 50:50.
		 */
		if (node_list_size(membership_node_list) > node_list_size(config_node_list) / 2) {
			return (TLV_VOTE_ACK);
		} else {
			return (TLV_VOTE_NACK);
		}
	} else {
		if (node_list_size(membership_node_list) > node_list_size(config_node_list) / 2) {
			return (TLV_VOTE_ACK);
		} else if (node_list_size(membership_node_list) < node_list_size(config_node_list) / 2) {
			return (TLV_VOTE_NACK);
		} else {
			/*
			 * 50:50 split
			 */
			if (qnetd_algo_ffsplit_is_prefered_partition(client, config_node_list,
			    membership_node_list)) {
				return (TLV_VOTE_ACK);
			} else {
				return (TLV_VOTE_NACK);
			}
		}
	}
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
		*result_vote = qnetd_algo_ffsplit_do(client, nodes, &client->last_membership_node_list);
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
    const struct node_list *nodes, enum tlv_vote *result_vote)
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
		*result_vote = qnetd_algo_ffsplit_do(client, &client->configuration_node_list, nodes);
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

	return (TLV_REPLY_ERROR_CODE_UNSUPPORTED_DECISION_ALGORITHM_MESSAGE);
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	return (TLV_REPLY_ERROR_CODE_UNSUPPORTED_DECISION_ALGORITHM_MESSAGE);
}

enum tlv_reply_error_code
qnetd_algo_ffsplit_timer_callback(struct qnetd_client *client, int *reschedule_timer,
    int *send_vote, enum tlv_vote *result_vote)
{

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static struct qnetd_algorithm qnetd_algo_ffsplit = {
	.init                          = qnetd_algo_ffsplit_client_init,
	.config_node_list_received     = qnetd_algo_ffsplit_config_node_list_received,
	.membership_node_list_received = qnetd_algo_ffsplit_membership_node_list_received,
	.quorum_node_list_received     = qnetd_algo_ffsplit_quorum_node_list_received,
	.client_disconnect             = qnetd_algo_ffsplit_client_disconnect,
	.ask_for_vote_received         = qnetd_algo_ffsplit_ask_for_vote_received,
	.vote_info_reply_received      = qnetd_algo_ffsplit_vote_info_reply_received,
	.timer_callback                = qnetd_algo_ffsplit_timer_callback,
};

enum tlv_reply_error_code qnetd_algo_ffsplit_register()
{

	return (qnetd_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_FFSPLIT, &qnetd_algo_ffsplit));
}
