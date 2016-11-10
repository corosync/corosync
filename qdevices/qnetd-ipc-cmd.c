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

#include "dynar-str.h"
#include "qnetd-ipc-cmd.h"
#include "qnetd-log.h"
#include "utils.h"

int
qnetd_ipc_cmd_status(struct qnetd_instance *instance, struct dynar *outbuf, int verbose)
{

	if (dynar_str_catf(outbuf, "QNetd address:\t\t\t%s:%"PRIu16"\n",
	    (instance->host_addr != NULL ? instance->host_addr : "*"), instance->host_port) == -1) {
		return (-1);
	}

	if (dynar_str_catf(outbuf, "TLS:\t\t\t\t%s%s\n",
	    tlv_tls_supported_to_str(instance->tls_supported),
	    ((instance->tls_supported != TLV_TLS_UNSUPPORTED && instance->tls_client_cert_required) ?
	    " (client certificate required)" : "")) == -1) {
		return (-1);
	}

	if (dynar_str_catf(outbuf, "Connected clients:\t\t%zu\n",
	    qnetd_client_list_no_clients(&instance->clients)) == -1) {
		return (-1);
	}

	if (dynar_str_catf(outbuf, "Connected clusters:\t\t%zu\n",
	    qnetd_cluster_list_size(&instance->clusters)) == -1) {
		return (-1);
	}

	if (!verbose) {
		return (0);
	}

	if (instance->max_clients != 0) {
		if (dynar_str_catf(outbuf, "Maximum allowed clients:\t%zu\n",
		    instance->max_clients) == -1) {
			return (-1);
		}
	}

	if (dynar_str_catf(outbuf, "Maximum send/receive size:\t%zu/%zu bytes\n",
	    instance->advanced_settings->max_client_send_size,
	    instance->advanced_settings->max_client_receive_size) == -1) {
		return (-1);
	}

	return (0);
}

static int
qnetd_ipc_cmd_add_tie_breaker(const struct qnetd_client *client,
    struct dynar *outbuf)
{

	if (dynar_str_catf(outbuf, "    Tie-breaker:\t") == -1) {
		return (0);
	}

	switch (client->tie_breaker.mode) {
	case TLV_TIE_BREAKER_MODE_LOWEST:
		if (dynar_str_catf(outbuf, "Node with lowest node ID") == -1) {
			return (0);
		}
		break;
	case TLV_TIE_BREAKER_MODE_HIGHEST:
		if (dynar_str_catf(outbuf, "Node with highest node ID") == -1) {
			return (0);
		}
		break;
	case TLV_TIE_BREAKER_MODE_NODE_ID:
		if (dynar_str_catf(outbuf, "Node with node ID "UTILS_PRI_NODE_ID,
		    client->tie_breaker.node_id) == -1) {
			return (0);
		}
		break;
	}

	return (dynar_str_catf(outbuf, "\n") != -1);
}

static int
qnetd_ipc_cmd_list_add_node_list(struct dynar *outbuf, int verbose, const struct node_list *nlist)
{
	struct node_list_entry *node_info;
	int i;

	i = 0;

	TAILQ_FOREACH(node_info, nlist, entries) {
		if (i != 0) {
			if (dynar_str_catf(outbuf, ", ") == -1) {
				return (-1);
			}
		}

		if (dynar_str_catf(outbuf, UTILS_PRI_NODE_ID, node_info->node_id) == -1) {
			return (-1);
		}

		if (node_info->data_center_id != 0) {
			if (dynar_str_catf(outbuf, " (" UTILS_PRI_DATACENTER_ID ")",
			    node_info->data_center_id) == -1) {
				return (-1);
			}

		}

		i++;
	}

	return (0);
}

static int
qnetd_ipc_cmd_list_add_client_info(const struct qnetd_client *client, struct dynar *outbuf,
    int verbose, size_t client_no)
{

	if (dynar_str_catf(outbuf, "    Node ID "UTILS_PRI_NODE_ID":\n",
	    client->node_id) == -1) {
		return (-1);
	}

	if (dynar_str_catf(outbuf, "        Client address:\t\t%s\n",
	    client->addr_str) == -1) {
		return (-1);
	}

	if (verbose) {
		if (dynar_str_catf(outbuf, "        HB interval:\t\t%"PRIu32"ms\n",
		    client->heartbeat_interval) == -1) {
			return (-1);
		}
	}

	if (client->config_version_set) {
		if (dynar_str_catf(outbuf, "        Configuration version:\t"
		    UTILS_PRI_CONFIG_VERSION"\n", client->config_version) == -1) {
			return (-1);
		}
	}

	if (!node_list_is_empty(&client->configuration_node_list)) {
		if ((dynar_str_catf(outbuf, "        Configured node list:\t") == -1) ||
		    (qnetd_ipc_cmd_list_add_node_list(outbuf, verbose,
		    &client->configuration_node_list) == -1) ||
		    (dynar_str_catf(outbuf, "\n") == -1)) {
			return (-1);
		}
	}

	if (verbose) {
		if (dynar_str_catf(outbuf, "        Ring ID:\t\t"UTILS_PRI_RING_ID"\n",
		    client->last_ring_id.node_id, client->last_ring_id.seq) == -1) {
			return (-1);
		}
	}

	if (!node_list_is_empty(&client->last_membership_node_list)) {
		if ((dynar_str_catf(outbuf, "        Membership node list:\t") == -1) ||
		    (qnetd_ipc_cmd_list_add_node_list(outbuf, verbose,
		    &client->last_membership_node_list) == -1) ||
		    (dynar_str_catf(outbuf, "\n") == -1)) {
			return (-1);
		}
	}

	if (client->last_heuristics != TLV_HEURISTICS_UNDEFINED || verbose) {
		if (dynar_str_catf(outbuf, "        Heuristics:\t\t%s",
		    tlv_heuristics_to_str(client->last_heuristics)) == -1) {
			return (-1);
		}

		if (verbose) {
			if (dynar_str_catf(outbuf, " (membership: %s, regular: %s)",
			    tlv_heuristics_to_str(client->last_membership_heuristics),
			    tlv_heuristics_to_str(client->last_regular_heuristics)) == -1) {
				return (-1);
			}
		}

		if (dynar_str_catf(outbuf, "\n") == -1) {
			return (-1);
		}
	}

	if (verbose) {
		if (dynar_str_catf(outbuf, "        TLS active:\t\t%s",
		    (client->tls_started ? "Yes" : "No")) == -1) {
			return (-1);
		}

		if (client->tls_started && client->tls_peer_certificate_verified) {
			if (dynar_str_catf(outbuf, " (client certificate verified)") == -1) {
				return (-1);
			}
		}

		if (dynar_str_catf(outbuf, "\n") == -1) {
			return (-1);
		}
	}

	if (client->last_sent_vote != TLV_VOTE_UNDEFINED) {
		if (dynar_str_catf(outbuf, "        Vote:\t\t\t%s",
		    tlv_vote_to_str(client->last_sent_vote)) == -1) {
			return (-1);
		}

		if (client->last_sent_ack_nack_vote != TLV_VOTE_UNDEFINED) {
			if (dynar_str_catf(outbuf, " (%s)",
			    tlv_vote_to_str(client->last_sent_ack_nack_vote)) == -1) {
				return (-1);
			}
		}

		if (dynar_str_catf(outbuf, "\n") == -1) {
			return (-1);
		}
	}

	return (0);
}

int
qnetd_ipc_cmd_list(struct qnetd_instance *instance, struct dynar *outbuf, int verbose,
    const char *cluster_name)
{
	struct qnetd_cluster *cluster;
	struct qnetd_client *client;
	size_t cluster_no, client_no;

	cluster_no = 0;
	TAILQ_FOREACH(cluster, &instance->clusters, entries) {
		if (cluster_name != NULL && strcmp(cluster_name, "") != 0 &&
		    strcmp(cluster_name, cluster->cluster_name) != 0) {
			continue;
		}

		if (dynar_str_catf(outbuf, "Cluster \"%s\":\n", cluster->cluster_name) == -1) {
			return (-1);
		}

		client_no = 0;

		TAILQ_FOREACH(client, &cluster->client_list, cluster_entries) {
			if (client_no == 0) {
				if (dynar_str_catf(outbuf, "    Algorithm:\t\t%s\n",
				    tlv_decision_algorithm_type_to_str(
				    client->decision_algorithm)) == -1) {
					return (-1);
				}

				if (!qnetd_ipc_cmd_add_tie_breaker(client, outbuf)) {
					return (-1);
				}
			}

			if (qnetd_ipc_cmd_list_add_client_info(client, outbuf, verbose,
			    client_no) == -1) {
				return (-1);
			}

			client_no++;
                }

                cluster_no++;
	}

	return (0);
}
