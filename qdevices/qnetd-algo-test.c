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

#include <string.h>

#include "qnetd-algo-test.h"
#include "qnetd-log.h"
#include "qnetd-cluster-list.h"

static void
qnetd_algo_test_dump_cluster(struct qnetd_cluster *cluster)
{
	struct qnetd_client *client;

	qnetd_log(LOG_INFO, "algo-test:   Cluster dump:");
	TAILQ_FOREACH(client, &cluster->client_list, cluster_entries) {
		qnetd_log(LOG_INFO, "algo-test:     client = %p, node_id = %"PRIx32,
		    client, client->node_id);
	}
}

/*
 * Called right after client sent init message. This happens after initial accept of client,
 * tls handshake and sending basic information about cluster/client.
 * Known information:
 * - client->cluster_name (client->cluster_name_len)
 * - client->node_id (client->node_id_set = 1)
 * - client->decision_algorithm
 * - client->cluster
 *
 * Callback is designed mainly for allocating client->algorithm_data.
 *
 * client is initialized qnetd_client structure.
 *
 * Return TLV_REPLY_ERROR_CODE_NO_ERROR on success, different TLV_REPLY_ERROR_CODE_*
 * on failure (error is send back to client)
 */
enum tlv_reply_error_code
qnetd_algo_test_client_init(struct qnetd_client *client)
{
	int *algo_data;

	qnetd_log(LOG_INFO, "algo-test: New client connected");
	qnetd_log(LOG_INFO, "algo-test:   cluster name = %s", client->cluster_name);
	qnetd_log(LOG_INFO, "algo-test:   tls started = %u", client->tls_started);
	qnetd_log(LOG_INFO, "algo-test:   tls peer certificate verified = %u",
	    client->tls_peer_certificate_verified);
	qnetd_log(LOG_INFO, "algo-test:   node_id = %"PRIx32, client->node_id);
	qnetd_log(LOG_INFO, "algo-test:   pointer = %p", client);

	qnetd_algo_test_dump_cluster(client->cluster);

	client->algorithm_data = malloc(sizeof(int));
	if (client->algorithm_data == NULL) {
		return (-1);
	}

	algo_data = client->algorithm_data;
	*algo_data = 42;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static const char *
qnetd_algo_test_node_state_to_str(enum tlv_node_state node_state)
{
	switch (node_state) {
	case TLV_NODE_STATE_NOT_SET: return ("not set"); break;
	case TLV_NODE_STATE_MEMBER: return ("member"); break;
	case TLV_NODE_STATE_DEAD: return ("dead"); break;
	case TLV_NODE_STATE_LEAVING: return ("leaving"); break;
	default: return ("unhandled"); break;
	}

	return ("");
}


static void
qnetd_algo_dump_node_list(struct qnetd_client *client, const struct node_list *nodes)
{
	int *algo_data;
	struct node_list_entry *node_info;

	algo_data = client->algorithm_data;

	qnetd_log(LOG_INFO, "algo-test:   algo data = %u", *algo_data);

	TAILQ_FOREACH(node_info, nodes, entries) {
		qnetd_log(LOG_INFO, "algo-test:   node_id = %"PRIx32", "
		    "data_center_id = %"PRIx32", "
		    "node_state = %s", node_info->node_id, node_info->data_center_id,
		    qnetd_algo_test_node_state_to_str(node_info->node_state));
	}
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

	qnetd_log(LOG_INFO, "algo-test: Client %p (cluster %s, node_id %"PRIx32") "
	    "sent %s node list.", client, client->cluster_name, client->node_id,
	    (initial ? "initial" : "changed"));

	qnetd_log(LOG_INFO, "algo-test:   msg seq num %"PRIu32, msg_seq_num);

	if (config_version_set) {
		qnetd_log(LOG_INFO, "algo-test:   config version %"PRIu64, config_version);
	}

	qnetd_algo_dump_node_list(client, nodes);

	*result_vote = TLV_VOTE_ACK;

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
qnetd_algo_test_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct tlv_ring_id *ring_id, enum tlv_quorate quorate,
    const struct node_list *nodes, enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-test: Client %p (cluster %s, node_id %"PRIx32") "
	    "sent membership node list.", client, client->cluster_name, client->node_id);

	qnetd_log(LOG_INFO, "algo-test:   msg seq num %"PRIu32, msg_seq_num);

	if (config_version_set) {
		qnetd_log(LOG_INFO, "algo-test:   config version = %"PRIu64, config_version);
	}

	qnetd_log(LOG_INFO, "algo-test:   ring id = (%"PRIx32".%"PRIx64")",
	    ring_id->node_id, ring_id->seq);
	qnetd_log(LOG_INFO, "algo-test:   quorate = %u", quorate);

	qnetd_algo_dump_node_list(client, nodes);

	*result_vote = TLV_VOTE_ACK;

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

	qnetd_log(LOG_INFO, "algo-test: Client %p (cluster %s, node_id %"PRIx32") "
	    "disconnect", client, client->cluster_name, client->node_id);

	qnetd_log(LOG_INFO, "algo-test:   server going down %u", server_going_down);

	free(client->algorithm_data);
}

/*
 * Called after client sent ask for vote message. This is usually happening after server
 * replied TLV_VOTE_ASK_LATER.
 */
enum tlv_reply_error_code
qnetd_algo_test_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_vote *result_vote)
{

	qnetd_log(LOG_INFO, "algo-test: Client %p (cluster %s, node_id %"PRIx32") "
	    "asked for a vote", client, client->cluster_name, client->node_id);

	*result_vote = TLV_VOTE_ACK;

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

enum tlv_reply_error_code
qnetd_algo_test_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	qnetd_log(LOG_INFO, "algo-test: Client %p (cluster %s, node_id %"PRIx32") "
	    "replied back to vote info message", client, client->cluster_name, client->node_id);

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}
