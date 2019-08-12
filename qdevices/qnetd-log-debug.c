/*
 * Copyright (c) 2015-2019 Red Hat, Inc.
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

#include "log.h"
#include "log-common.h"
#include "qnetd-log-debug.h"
#include "utils.h"

void
qnetd_log_debug_dump_cluster(struct qnetd_cluster *cluster)
{
	struct qnetd_client *client;

	log(LOG_DEBUG, "  cluster dump:");
	TAILQ_FOREACH(client, &cluster->client_list, cluster_entries) {
		log(LOG_DEBUG, "    client = %s, node_id = "UTILS_PRI_NODE_ID,
		    client->addr_str, client->node_id);
	}
}

void
qnetd_log_debug_new_client_connected(struct qnetd_client *client)
{

	log(LOG_DEBUG, "New client connected");
	log(LOG_DEBUG, "  cluster name = %s", client->cluster_name);
	log(LOG_DEBUG, "  tls started = %u", client->tls_started);
	log(LOG_DEBUG, "  tls peer certificate verified = %u",
	    client->tls_peer_certificate_verified);
	log(LOG_DEBUG, "  node_id = "UTILS_PRI_NODE_ID, client->node_id);
	log(LOG_DEBUG, "  pointer = %p", client);
	log(LOG_DEBUG, "  addr_str = %s", client->addr_str);
	log(LOG_DEBUG, "  ring id = (" UTILS_PRI_RING_ID ")", client->last_ring_id.node_id,
	    client->last_ring_id.seq);

	qnetd_log_debug_dump_cluster(client->cluster);
}

void
qnetd_log_debug_config_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, int config_version_set, uint64_t config_version,
    const struct node_list *nodes, int initial)
{

	log(LOG_DEBUG, "Client %s (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "sent %s node list.", client->addr_str, client->cluster_name, client->node_id,
	    (initial ? "initial" : "changed"));

	log(LOG_DEBUG, "  msg seq num = "UTILS_PRI_MSG_SEQ, msg_seq_num);

	if (config_version_set) {
		log(LOG_DEBUG, "  config version = " UTILS_PRI_CONFIG_VERSION, config_version);
	}

	log_common_debug_dump_node_list(nodes);
}

void
qnetd_log_debug_membership_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, const struct tlv_ring_id *ring_id,
    enum tlv_heuristics heuristics, const struct node_list *nodes)
{
	log(LOG_DEBUG, "Client %s (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "sent membership node list.", client->addr_str, client->cluster_name, client->node_id);

	log(LOG_DEBUG, "  msg seq num = "UTILS_PRI_MSG_SEQ, msg_seq_num);

	log(LOG_DEBUG, "  ring id = (" UTILS_PRI_RING_ID ")", ring_id->node_id, ring_id->seq);

	log(LOG_DEBUG, "  heuristics = %s ", tlv_heuristics_to_str(heuristics));

	log_common_debug_dump_node_list(nodes);
}

void
qnetd_log_debug_quorum_node_list_received(struct qnetd_client *client,
    uint32_t msg_seq_num, enum tlv_quorate quorate, const struct node_list *nodes)
{

	log(LOG_DEBUG, "Client %s (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "sent quorum node list.", client->addr_str, client->cluster_name, client->node_id);

	log(LOG_DEBUG, "  msg seq num = "UTILS_PRI_MSG_SEQ, msg_seq_num);
	log(LOG_DEBUG, "  quorate = %u", quorate);

	log_common_debug_dump_node_list(nodes);
}

void
qnetd_log_debug_client_disconnect(struct qnetd_client *client, int server_going_down)
{

	log(LOG_DEBUG, "Client %s (init_received %u, cluster %s, node_id "
	    UTILS_PRI_NODE_ID") disconnect%s", client->addr_str, client->init_received,
	    client->cluster_name, client->node_id,
	    (server_going_down ? " (server is going down)" : ""));
}

void
qnetd_log_debug_ask_for_vote_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	log(LOG_DEBUG, "Client %s (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "asked for a vote", client->addr_str, client->cluster_name, client->node_id);
	log(LOG_DEBUG, "  msg seq num = "UTILS_PRI_MSG_SEQ, msg_seq_num);
}

void
qnetd_log_debug_vote_info_reply_received(struct qnetd_client *client, uint32_t msg_seq_num)
{

	log(LOG_DEBUG, "Client %s (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "replied back to vote info message", client->addr_str, client->cluster_name,
	    client->node_id);
	log(LOG_DEBUG, "  msg seq num = "UTILS_PRI_MSG_SEQ, msg_seq_num);
}

void
qnetd_log_debug_send_vote_info(struct qnetd_client *client, uint32_t msg_seq_num, enum tlv_vote vote)
{

	log(LOG_DEBUG, "Sending vote info to client %s (cluster %s, node_id "UTILS_PRI_NODE_ID") ",
	    client->addr_str, client->cluster_name, client->node_id);
	log(LOG_DEBUG, "  msg seq num = "UTILS_PRI_MSG_SEQ, msg_seq_num);
	log(LOG_DEBUG, "  vote = %s", tlv_vote_to_str(vote));
}

void
qnetd_log_debug_heuristics_change_received(struct qnetd_client *client, uint32_t msg_seq_num,
    enum tlv_heuristics heuristics)
{

	log(LOG_DEBUG, "Client %s (cluster %s, node_id "UTILS_PRI_NODE_ID") "
	    "sent heuristics change", client->addr_str, client->cluster_name, client->node_id);
	log(LOG_DEBUG, "  msg seq num = "UTILS_PRI_MSG_SEQ, msg_seq_num);
	log(LOG_DEBUG, "  heuristics = %s", tlv_heuristics_to_str(heuristics));
}
