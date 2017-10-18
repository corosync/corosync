/*
 * Copyright (c) 2016 Red Hat, Inc.
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
#include <sys/types.h>
#include <string.h>

#include "qnetd-log.h"
#include "qnetd-cluster-list.h"
#include "qnetd-algo-utils.h"
#include "utils.h"

/*
 * Returns -1 if any node that is supposedly in the same cluster partition
 * as us has a different ring_id.
 * If this happens it simply means that qnetd does not yet have the full current view
 * of the cluster and should wait until all of the ring_ids in this membership list match up
 */
int
qnetd_algo_all_ring_ids_match(struct qnetd_client *client, const struct tlv_ring_id *ring_id)
{
	struct node_list_entry *node_info;
 	struct qnetd_client *other_client;

	TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
		int in_our_partition = 0;

		if (other_client == client) {
			continue; /* We've seen our membership list */
		}
		qnetd_log(LOG_DEBUG, "algo-util: all_ring_ids_match: seen nodeid %d (client %p) ring_id (" UTILS_PRI_RING_ID ")", other_client->node_id, other_client, other_client->last_ring_id.node_id, other_client->last_ring_id.seq);

		/* Look down our node list and see if this client is known to us */
		TAILQ_FOREACH(node_info, &client->last_membership_node_list, entries) {
			if (node_info->node_id == other_client->node_id) {
				in_our_partition = 1;
			}
		}

		if (in_our_partition == 0) {
			/*
			 * Also try to look from the other side to see if we are
			 * not in the other node's membership list.
			 * Because if so it may mean the membership lists are not equal
			 */
			TAILQ_FOREACH(node_info, &other_client->last_membership_node_list, entries) {
				if (node_info->node_id == client->node_id) {
					in_our_partition = 1;
				}
			}
		}

		/*
		 * If the other nodes on our side of a partition have a different ring ID then
		 * we need to wait until they have all caught up before making a decision
		 */
		if (in_our_partition && !tlv_ring_id_eq(ring_id, &other_client->last_ring_id)) {
			qnetd_log(LOG_DEBUG, "algo-util: nodeid %d in our partition has different ring_id (" UTILS_PRI_RING_ID ") to us (" UTILS_PRI_RING_ID ")", other_client->node_id, other_client->last_ring_id.node_id, other_client->last_ring_id.seq, ring_id->node_id, ring_id->seq);
			return (-1); /* ring IDs don't match */
		}
	}

	return (0);
}

struct qnetd_algo_partition *
qnetd_algo_find_partition(partitions_list_t *partitions_list, const struct tlv_ring_id *ring_id)
{
	struct qnetd_algo_partition *cur_partition;

	TAILQ_FOREACH(cur_partition, partitions_list, entries) {
		if (tlv_ring_id_eq(&cur_partition->ring_id, ring_id)) {
			return (cur_partition);
		}
	}

	return (NULL);
}

int
qnetd_algo_create_partitions(struct qnetd_client *client, partitions_list_t *partitions_list, const struct tlv_ring_id *ring_id)
{
 	struct qnetd_client *other_client;
	int num_partitions = 0;

	TAILQ_FOREACH(other_client, &client->cluster->client_list, cluster_entries) {
		struct qnetd_algo_partition *partition;

		if (other_client->last_ring_id.seq == 0){
			continue; /* not initialised yet */
		}
		partition = qnetd_algo_find_partition(partitions_list, &other_client->last_ring_id);
		if (!partition) {
			partition = malloc(sizeof(struct qnetd_algo_partition));
			if (!partition) {
				return (-1);
			}
			partition->num_nodes = 0;
			partition->score = 0;
			memcpy(&partition->ring_id, &other_client->last_ring_id, sizeof(*ring_id));
			num_partitions++;
			TAILQ_INSERT_TAIL(partitions_list, partition, entries);
		}
		partition->num_nodes++;

		/*
		 * Score is computer similar way as in the ffsplit algorithm
		 */
		partition->score++;
		if (other_client->last_heuristics == TLV_HEURISTICS_PASS) {
			partition->score++;
		} else if (other_client->last_heuristics == TLV_HEURISTICS_FAIL) {
			partition->score--;
		}

	}

	return (num_partitions);
}


void
qnetd_algo_free_partitions(partitions_list_t *partitions_list)
{
	struct qnetd_algo_partition *cur_partition;
	struct qnetd_algo_partition *partition_next;

	cur_partition = TAILQ_FIRST(partitions_list);

	while (cur_partition != NULL) {
		partition_next = TAILQ_NEXT(cur_partition, entries);

		free(cur_partition);

		cur_partition = partition_next;
	}

	TAILQ_INIT(partitions_list);
}

void
qnetd_algo_dump_partitions(partitions_list_t *partitions_list)
{
	struct qnetd_algo_partition *partition;

	TAILQ_FOREACH(partition, partitions_list, entries) {
		qnetd_log(LOG_DEBUG, "algo-util: partition (" UTILS_PRI_RING_ID ") (%p) has %d nodes",
			  partition->ring_id.node_id, partition->ring_id.seq, partition, partition->num_nodes);
	}
}
