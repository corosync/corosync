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

#include "qnetd-algorithm.h"
#include "qnetd-instance.h"
#include "qnetd-log.h"
#include "qnetd-log-debug.h"
#include "qnetd-client-send.h"
#include "msg.h"
#include "nss-sock.h"

#include "qnetd-client-msg-received.h"

/*
 *  0 - Success
 * -1 - Disconnect client
 * -2 - Error reply sent, but no need to disconnect client
 */
static int
qnetd_client_msg_received_check_tls(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	int check_certificate;
	int tls_required;
	CERTCertificate *peer_cert;
	int case_processed;

	check_certificate = 0;
	tls_required = 0;

	case_processed = 0;

	switch (instance->tls_supported) {
	case TLV_TLS_UNSUPPORTED:
		case_processed = 1;
		tls_required = 0;
		check_certificate = 0;
		break;
	case TLV_TLS_SUPPORTED:
		case_processed = 1;
		tls_required = 0;

		if (client->tls_started && instance->tls_client_cert_required &&
		    !client->tls_peer_certificate_verified) {
			check_certificate = 1;
		}
		break;
	case TLV_TLS_REQUIRED:
		case_processed = 1;
		tls_required = 1;

		if (instance->tls_client_cert_required && !client->tls_peer_certificate_verified) {
			check_certificate = 1;
		}
		break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when new
	 * tls supported is added
	 */
	}

	if (!case_processed) {
		qnetd_log(LOG_ERR, "Unhandled instance tls supported %u", instance->tls_supported);
		exit(1);
	}

	if (tls_required && !client->tls_started) {
		qnetd_log(LOG_ERR, "TLS is required but doesn't started yet. "
		    "Sending back error message");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_TLS_REQUIRED) != 0) {
			return (-1);
		}

		return (-2);
	}

	if (check_certificate) {
		peer_cert = SSL_PeerCertificate(client->socket);

		if (peer_cert == NULL) {
			qnetd_log(LOG_ERR, "Client doesn't sent valid certificate. "
			    "Disconnecting client");

			return (-1);
		}

		if (CERT_VerifyCertName(peer_cert, client->cluster_name) != SECSuccess) {
			qnetd_log(LOG_ERR, "Client doesn't sent certificate with valid CN. "
			    "Disconnecting client");

			CERT_DestroyCertificate(peer_cert);

			return (-1);
		}

		CERT_DestroyCertificate(peer_cert);

		client->tls_peer_certificate_verified = 1;
	}

	return (0);
}

static int
qnetd_client_msg_received_preinit(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	struct send_buffer_list_entry *send_buffer;

	if (msg->cluster_name == NULL) {
		qnetd_log(LOG_ERR, "Received preinit message without cluster name. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
			return (-1);
		}

		return (0);
	}

	client->cluster_name = malloc(msg->cluster_name_len + 1);
	if (client->cluster_name == NULL) {
		qnetd_log(LOG_ERR, "Can't allocate cluster name. Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_INTERNAL_ERROR) != 0) {
			return (-1);
		}

		return (0);
	}
	memset(client->cluster_name, 0, msg->cluster_name_len + 1);
	memcpy(client->cluster_name, msg->cluster_name, msg->cluster_name_len);

	client->cluster_name_len = msg->cluster_name_len;
	client->preinit_received = 1;

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc preinit reply msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_preinit_reply(&send_buffer->buffer, msg->seq_number_set, msg->seq_number,
	    instance->tls_supported, instance->tls_client_cert_required) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc preinit reply msg. "
		    "Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);

		return (-1);
	};

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

static int
qnetd_client_msg_received_unexpected_msg(struct qnetd_client *client,
    const struct msg_decoded *msg, const char *msg_str)
{

	qnetd_log(LOG_ERR, "Received %s message. Sending back error message", msg_str);

	if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
	    TLV_REPLY_ERROR_CODE_UNEXPECTED_MESSAGE) != 0) {
		return (-1);
	}

	return (0);
}

static int
qnetd_client_msg_received_preinit_reply(struct qnetd_instance *instance,
    struct qnetd_client *client, const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "preinit reply"));
}

static int
qnetd_client_msg_received_starttls(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	PRFileDesc *new_pr_fd;

	if (!client->preinit_received) {
		qnetd_log(LOG_ERR, "Received starttls before preinit message. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_PREINIT_REQUIRED) != 0) {
			return (-1);
		}

		return (0);
	}

	if ((new_pr_fd = nss_sock_start_ssl_as_server(client->socket, instance->server.cert,
	    instance->server.private_key, instance->tls_client_cert_required, 0, NULL)) == NULL) {
		qnetd_log_nss(LOG_ERR, "Can't start TLS. Disconnecting client.");

		return (-1);
	}

	client->tls_started = 1;
	client->tls_peer_certificate_verified = 0;
	client->socket = new_pr_fd;

	return (0);
}

static int
qnetd_client_msg_received_server_error(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "server error"));
}

/*
 * Checks if new client send information are valid. It means:
 * - in cluster is no duplicate node with same nodeid
 * - it has same tie_breaker as other nodes in cluster
 * - it has same algorithm as other nodes in cluster
 */
static enum tlv_reply_error_code
qnetd_client_msg_received_init_check_new_client(struct qnetd_instance *instance,
    struct qnetd_client *new_client)
{
	struct qnetd_cluster *cluster;
	struct qnetd_client *client;

	cluster = qnetd_cluster_list_find_by_name(&instance->clusters, new_client->cluster_name,
	    new_client->cluster_name_len);

	if (cluster == NULL) {
		return (TLV_REPLY_ERROR_CODE_NO_ERROR);
	}

	TAILQ_FOREACH(client, &cluster->client_list, cluster_entries) {
		if (!tlv_tie_breaker_eq(&new_client->tie_breaker, &client->tie_breaker)) {
			qnetd_log(LOG_ERR, "Received init message contains tie-breaker which "
			    "differs from rest of cluster. Sending error reply");

			return (TLV_REPLY_ERROR_CODE_TIE_BREAKER_DIFFERS_FROM_OTHER_NODES);
		}

		if (new_client->decision_algorithm != client->decision_algorithm) {
			qnetd_log(LOG_ERR, "Received init message contains algorithm which "
			    "differs from rest of cluster. Sending error reply");

			return (TLV_REPLY_ERROR_CODE_ALGORITHM_DIFFERS_FROM_OTHER_NODES);
		}

		if (new_client->node_id == client->node_id) {
			qnetd_log(LOG_ERR, "Received init message contains node id which is "
			    "duplicate of other node in cluster. Sending error reply");

			return (TLV_REPLY_ERROR_CODE_DUPLICATE_NODE_ID);
		}
	}

	return (TLV_REPLY_ERROR_CODE_NO_ERROR);
}

static int
qnetd_client_msg_received_init(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	int res;
	size_t zi;
	enum msg_type *supported_msgs;
	size_t no_supported_msgs;
	enum tlv_opt_type *supported_opts;
	size_t no_supported_opts;
	struct send_buffer_list_entry *send_buffer;
	enum tlv_reply_error_code reply_error_code;
	struct qnetd_cluster *cluster;

	supported_msgs = NULL;
	supported_opts = NULL;
	no_supported_msgs = 0;
	no_supported_opts = 0;

	reply_error_code = TLV_REPLY_ERROR_CODE_NO_ERROR;

	if ((res = qnetd_client_msg_received_check_tls(instance, client, msg)) != 0) {
		return (res == -1 ? -1 : 0);
	}

	if (!client->preinit_received) {
		qnetd_log(LOG_ERR, "Received init before preinit message. Sending error reply.");

		reply_error_code = TLV_REPLY_ERROR_CODE_PREINIT_REQUIRED;
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR && !msg->node_id_set) {
		qnetd_log(LOG_ERR, "Received init message without node id set. "
		    "Sending error reply.");

		reply_error_code = TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION;
	} else {
		client->node_id_set = 1;
		client->node_id = msg->node_id;
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR && !msg->ring_id_set) {
		qnetd_log(LOG_ERR, "Received init message without ring id set. "
		    "Sending error reply.");

		reply_error_code = TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION;
	} else {
		memcpy(&client->last_ring_id, &msg->ring_id, sizeof(struct tlv_ring_id));
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR && !msg->heartbeat_interval_set) {
		qnetd_log(LOG_ERR, "Received init message without heartbeat interval set. "
		    "Sending error reply.");

		reply_error_code = TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION;
	} else {
		if (msg->heartbeat_interval < instance->advanced_settings->heartbeat_interval_min ||
		    msg->heartbeat_interval > instance->advanced_settings->heartbeat_interval_max) {
			qnetd_log(LOG_ERR, "Client requested invalid heartbeat interval %u. "
			    "Sending error reply.", msg->heartbeat_interval);

			reply_error_code = TLV_REPLY_ERROR_CODE_INVALID_HEARTBEAT_INTERVAL;
		} else {
			client->heartbeat_interval = msg->heartbeat_interval;
		}
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR && !msg->tie_breaker_set) {
		qnetd_log(LOG_ERR, "Received init message without tie-breaker set. "
		    "Sending error reply.");

		reply_error_code = TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION;
	} else {
		memcpy(&client->tie_breaker, &msg->tie_breaker, sizeof(msg->tie_breaker));
	}

	if (msg->supported_messages != NULL) {
		/*
		 * Client sent supported messages. For now this is ignored but in the future
		 * this may be used to ensure backward compatibility.
		 */
/*
		for (i = 0; i < msg->no_supported_messages; i++) {
			qnetd_log(LOG_DEBUG, "Client supports %u message",
			    (int)msg->supported_messages[i]);
		}
*/

		/*
		 * Sent back supported messages
		 */
		msg_get_supported_messages(&supported_msgs, &no_supported_msgs);
	}

	if (msg->supported_options != NULL) {
		/*
		 * Client sent supported options. For now this is ignored but in the future
		 * this may be used to ensure backward compatibility.
		 */
/*
		for (i = 0; i < msg->no_supported_options; i++) {
			qnetd_log(LOG_DEBUG, "Client supports %u option",
			    (int)msg->supported_messages[i]);
		}
*/

		/*
		 * Send back supported options
		 */
		tlv_get_supported_options(&supported_opts, &no_supported_opts);
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR && !msg->decision_algorithm_set) {
		qnetd_log(LOG_ERR, "Received init message without decision algorithm. "
		    "Sending error reply.");

		reply_error_code = TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION;
	} else {
		/*
		 * Check if decision algorithm requested by client is supported
		 */
		res = 0;

		for (zi = 0; zi < QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE && !res; zi++) {
			if (qnetd_static_supported_decision_algorithms[zi] ==
			    msg->decision_algorithm) {
				res = 1;
			}
		}

		if (!res) {
			qnetd_log(LOG_ERR, "Client requested unsupported decision algorithm %u. "
			    "Sending error reply.", msg->decision_algorithm);

			reply_error_code = TLV_REPLY_ERROR_CODE_UNSUPPORTED_DECISION_ALGORITHM;
		}

		client->decision_algorithm = msg->decision_algorithm;
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR) {
		reply_error_code = qnetd_client_msg_received_init_check_new_client(instance,
		    client);
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR) {
		cluster = qnetd_cluster_list_add_client(&instance->clusters, client);
		if (cluster == NULL) {
			qnetd_log(LOG_ERR, "Can't add client to cluster list. "
			    "Sending error reply.");

			reply_error_code = TLV_REPLY_ERROR_CODE_INTERNAL_ERROR;
		} else {
			client->cluster = cluster;
			client->cluster_list = &instance->clusters;
		}
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qnetd_log_debug_new_client_connected(client);

		reply_error_code = qnetd_algorithm_client_init(client);
	}

	if (reply_error_code == TLV_REPLY_ERROR_CODE_NO_ERROR) {
		/*
		 * Correct init received
		 */
		client->init_received = 1;
	} else {
		qnetd_log(LOG_ERR, "Algorithm returned error code. Sending error reply.");
	}

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc init reply msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_init_reply(&send_buffer->buffer, msg->seq_number_set, msg->seq_number,
	    reply_error_code,
	    supported_msgs, no_supported_msgs, supported_opts, no_supported_opts,
	    instance->advanced_settings->max_client_receive_size,
	    instance->advanced_settings->max_client_send_size,
	    qnetd_static_supported_decision_algorithms,
	    QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc init reply msg. Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

static int
qnetd_client_msg_received_init_reply(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "init reply"));
}

static int
qnetd_client_msg_received_set_option_reply(struct qnetd_instance *instance,
    struct qnetd_client *client, const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "set option reply"));
}

static int
qnetd_client_msg_received_set_option(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	int res;
	struct send_buffer_list_entry *send_buffer;

	if ((res = qnetd_client_msg_received_check_tls(instance, client, msg)) != 0) {
		return (res == -1 ? -1 : 0);
	}

	if (!client->init_received) {
		qnetd_log(LOG_ERR, "Received set option message before init message. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_INIT_REQUIRED) != 0) {
			return (-1);
		}

		return (0);
	}

	if (msg->heartbeat_interval_set) {
		/*
		 * Check if heartbeat interval is valid
		 */
		if (msg->heartbeat_interval < instance->advanced_settings->heartbeat_interval_min ||
		    msg->heartbeat_interval > instance->advanced_settings->heartbeat_interval_max) {
			qnetd_log(LOG_ERR, "Client requested invalid heartbeat interval %u. "
			    "Sending error reply.", msg->heartbeat_interval);

			if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
			    TLV_REPLY_ERROR_CODE_INVALID_HEARTBEAT_INTERVAL) != 0) {
				return (-1);
			}

			return (0);
		}

		client->heartbeat_interval = msg->heartbeat_interval;
	}

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc set option reply msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_set_option_reply(&send_buffer->buffer, msg->seq_number_set, msg->seq_number,
	    client->heartbeat_interval) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc set option reply msg. "
		    "Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

static int
qnetd_client_msg_received_echo_reply(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "echo reply"));
}

static int
qnetd_client_msg_received_echo_request(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg, const struct dynar *msg_orig)
{
	int res;
	struct send_buffer_list_entry *send_buffer;

	if ((res = qnetd_client_msg_received_check_tls(instance, client, msg)) != 0) {
		return (res == -1 ? -1 : 0);
	}

	if (!client->init_received) {
		qnetd_log(LOG_ERR, "Received echo request before init message. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_INIT_REQUIRED) != 0) {
			return (-1);
		}

		return (0);
	}

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc echo reply msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_echo_reply(&send_buffer->buffer, msg_orig) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc echo reply msg. Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

static int
qnetd_client_msg_received_node_list(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	int res;
	struct send_buffer_list_entry *send_buffer;
	enum tlv_reply_error_code reply_error_code;
	enum tlv_vote result_vote;
	int case_processed;

	reply_error_code = TLV_REPLY_ERROR_CODE_NO_ERROR;

	if ((res = qnetd_client_msg_received_check_tls(instance, client, msg)) != 0) {
		return (res == -1 ? -1 : 0);
	}

	if (!client->init_received) {
		qnetd_log(LOG_ERR, "Received node list message before init message. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_INIT_REQUIRED) != 0) {
			return (-1);
		}

		return (0);
	}

	if (!msg->node_list_type_set) {
		qnetd_log(LOG_ERR, "Received node list message without node list type set. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
			return (-1);
		}

		return (0);
	}

	if (!msg->seq_number_set) {
		qnetd_log(LOG_ERR, "Received node list message without seq number set. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
			return (-1);
		}

		return (0);
	}

	result_vote = TLV_VOTE_NO_CHANGE;

	case_processed = 0;
	switch (msg->node_list_type) {
	case TLV_NODE_LIST_TYPE_INITIAL_CONFIG:
	case TLV_NODE_LIST_TYPE_CHANGED_CONFIG:
		case_processed = 1;
		qnetd_log_debug_config_node_list_received(client, msg->seq_number,
		    msg->config_version_set, msg->config_version, &msg->nodes,
		    (msg->node_list_type == TLV_NODE_LIST_TYPE_INITIAL_CONFIG));

		reply_error_code = qnetd_algorithm_config_node_list_received(client,
		    msg->seq_number, msg->config_version_set, msg->config_version,
		    &msg->nodes,
		    (msg->node_list_type == TLV_NODE_LIST_TYPE_INITIAL_CONFIG),
		    &result_vote);
		break;
	case TLV_NODE_LIST_TYPE_MEMBERSHIP:
		case_processed = 1;
		if (!msg->ring_id_set) {
			qnetd_log(LOG_ERR, "Received node list message without ring id number set. "
			    "Sending error reply.");

			if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
			    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
				return (-1);
			}

			return (0);
		}

		qnetd_log_debug_membership_node_list_received(client, msg->seq_number, &msg->ring_id,
		    msg->heuristics, &msg->nodes);

		reply_error_code = qnetd_algorithm_membership_node_list_received(client,
		    msg->seq_number, &msg->ring_id, &msg->nodes, msg->heuristics, &result_vote);
		break;
	case TLV_NODE_LIST_TYPE_QUORUM:
		case_processed = 1;
		if (!msg->quorate_set) {
			qnetd_log(LOG_ERR, "Received quorum list message without quorate set. "
			    "Sending error reply.");

			if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
			    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
				return (-1);
			}

			return (0);
		}

		qnetd_log_debug_quorum_node_list_received(client, msg->seq_number,msg->quorate,
		    &msg->nodes);

		reply_error_code = qnetd_algorithm_quorum_node_list_received(client,
		    msg->seq_number,msg->quorate, &msg->nodes, &result_vote);
		break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when new
	 * node list type is added
	 */
	}

	if (!case_processed) {
		qnetd_log(LOG_ERR, "qnetd_client_msg_received_node_list fatal error. "
		    "Unhandled node_list_type");
		exit(1);
	}

	if (reply_error_code != TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qnetd_log(LOG_ERR, "Algorithm returned error code. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    reply_error_code) != 0) {
			return (-1);
		}

		return (0);
	} else {
		qnetd_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	/*
	 * Store node list for future use
	 */
	case_processed = 0;
	switch (msg->node_list_type) {
	case TLV_NODE_LIST_TYPE_INITIAL_CONFIG:
	case TLV_NODE_LIST_TYPE_CHANGED_CONFIG:
		case_processed = 1;
		node_list_free(&client->configuration_node_list);
		if (node_list_clone(&client->configuration_node_list, &msg->nodes) == -1) {
			qnetd_log(LOG_ERR, "Can't alloc config node list clone. "
			    "Disconnecting client connection.");

			return (-1);
		}
		client->config_version_set = msg->config_version_set;
		client->config_version = msg->config_version;

		break;
	case TLV_NODE_LIST_TYPE_MEMBERSHIP:
		case_processed = 1;
		node_list_free(&client->last_membership_node_list);
		if (node_list_clone(&client->last_membership_node_list, &msg->nodes) == -1) {
			qnetd_log(LOG_ERR, "Can't alloc membership node list clone. "
			    "Disconnecting client connection.");

			return (-1);
		}
		memcpy(&client->last_ring_id, &msg->ring_id, sizeof(struct tlv_ring_id));
		client->last_membership_heuristics = msg->heuristics;
		client->last_heuristics = msg->heuristics;
		break;
	case TLV_NODE_LIST_TYPE_QUORUM:
		case_processed = 1;
		node_list_free(&client->last_quorum_node_list);
		if (node_list_clone(&client->last_quorum_node_list, &msg->nodes) == -1) {
			qnetd_log(LOG_ERR, "Can't alloc quorum node list clone. "
			    "Disconnecting client connection.");

			return (-1);
		}
		break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when new
	 * node list type is added
	 */
	}

	if (!case_processed) {
		qnetd_log(LOG_ERR, "qnetd_client_msg_received_node_list fatal error. "
		    "Unhandled node_list_type");
		exit(1);
	}

	/*
	 * Store result vote
	 */
	client->last_sent_vote = result_vote;
	if (result_vote == TLV_VOTE_ACK || result_vote == TLV_VOTE_NACK) {
		client->last_sent_ack_nack_vote = result_vote;
	}

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc node list reply msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_node_list_reply(&send_buffer->buffer, msg->seq_number, msg->node_list_type,
	    &client->last_ring_id, result_vote) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc node list reply msg. "
		    "Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

static int
qnetd_client_msg_received_node_list_reply(struct qnetd_instance *instance,
    struct qnetd_client *client, const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "node list reply"));
}

static int
qnetd_client_msg_received_ask_for_vote(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	int res;
	struct send_buffer_list_entry *send_buffer;
	enum tlv_reply_error_code reply_error_code;
	enum tlv_vote result_vote;

	reply_error_code = TLV_REPLY_ERROR_CODE_NO_ERROR;

	if ((res = qnetd_client_msg_received_check_tls(instance, client, msg)) != 0) {
		return (res == -1 ? -1 : 0);
	}

	if (!client->init_received) {
		qnetd_log(LOG_ERR, "Received ask for vote message before init message. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_INIT_REQUIRED) != 0) {
			return (-1);
		}

		return (0);
	}

	if (!msg->seq_number_set) {
		qnetd_log(LOG_ERR, "Received ask for vote message without seq number set. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
			return (-1);
		}

		return (0);
	}

	qnetd_log_debug_ask_for_vote_received(client, msg->seq_number);

	reply_error_code = qnetd_algorithm_ask_for_vote_received(client, msg->seq_number,
	    &result_vote);

	if (reply_error_code != TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qnetd_log(LOG_ERR, "Algorithm returned error code. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    reply_error_code) != 0) {
			return (-1);
		}

		return (0);
	} else {
		qnetd_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	/*
	 * Store result vote
	 */
	client->last_sent_vote = result_vote;
	if (result_vote == TLV_VOTE_ACK || result_vote == TLV_VOTE_NACK) {
		client->last_sent_ack_nack_vote = result_vote;
	}

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc ask for vote reply msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_ask_for_vote_reply(&send_buffer->buffer, msg->seq_number,
	    &client->last_ring_id, result_vote) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc ask for vote reply msg. "
		    "Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

static int
qnetd_client_msg_received_ask_for_vote_reply(struct qnetd_instance *instance,
    struct qnetd_client *client, const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "ask for vote reply"));
}

static int
qnetd_client_msg_received_vote_info(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "vote info"));
}

static int
qnetd_client_msg_received_vote_info_reply(struct qnetd_instance *instance,
    struct qnetd_client *client, const struct msg_decoded *msg)
{
	int res;
	enum tlv_reply_error_code reply_error_code;

	reply_error_code = TLV_REPLY_ERROR_CODE_NO_ERROR;

	if ((res = qnetd_client_msg_received_check_tls(instance, client, msg)) != 0) {
		return (res == -1 ? -1 : 0);
	}

	if (!client->init_received) {
		qnetd_log(LOG_ERR, "Received vote info reply before init message. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_INIT_REQUIRED) != 0) {
			return (-1);
		}

		return (0);
	}

	if (!msg->seq_number_set) {
		qnetd_log(LOG_ERR, "Received vote info reply message without seq number set. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
			return (-1);
		}

		return (0);
	}

	qnetd_log_debug_vote_info_reply_received(client, msg->seq_number);

	reply_error_code = qnetd_algorithm_vote_info_reply_received(client, msg->seq_number);

	if (reply_error_code != TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qnetd_log(LOG_ERR, "Algorithm returned error code. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    reply_error_code) != 0) {
			return (-1);
		}

		return (0);
	}

	return (0);
}

static int
qnetd_client_msg_received_heuristics_change(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{
	int res;
	struct send_buffer_list_entry *send_buffer;
	enum tlv_reply_error_code reply_error_code;
	enum tlv_vote result_vote;

	reply_error_code = TLV_REPLY_ERROR_CODE_NO_ERROR;

	if ((res = qnetd_client_msg_received_check_tls(instance, client, msg)) != 0) {
		return (res == -1 ? -1 : 0);
	}

	if (!client->init_received) {
		qnetd_log(LOG_ERR, "Received heuristics change message before init message. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_INIT_REQUIRED) != 0) {
			return (-1);
		}

		return (0);
	}

	if (!msg->seq_number_set || msg->heuristics == TLV_HEURISTICS_UNDEFINED) {
		qnetd_log(LOG_ERR, "Received heuristics change message without seq number set or "
		    "with undefined heuristics. Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION) != 0) {
			return (-1);
		}

		return (0);
	}

	qnetd_log_debug_heuristics_change_received(client, msg->seq_number, msg->heuristics);

	reply_error_code = qnetd_algorithm_heuristics_change_received(client, msg->seq_number,
	    msg->heuristics, &result_vote);

	if (reply_error_code != TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qnetd_log(LOG_ERR, "Algorithm returned error code. "
		    "Sending error reply.");

		if (qnetd_client_send_err(client, msg->seq_number_set, msg->seq_number,
		    reply_error_code) != 0) {
			return (-1);
		}

		return (0);
	} else {
		qnetd_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	/*
	 * Store result vote and heuristics result
	 */
	client->last_sent_vote = result_vote;
	if (result_vote == TLV_VOTE_ACK || result_vote == TLV_VOTE_NACK) {
		client->last_sent_ack_nack_vote = result_vote;
	}
	client->last_regular_heuristics = msg->heuristics;
	client->last_heuristics = msg->heuristics;

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc heuristics change reply msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_heuristics_change_reply(&send_buffer->buffer, msg->seq_number,
	    &client->last_ring_id, msg->heuristics, result_vote) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc heuristics change reply msg. "
		    "Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

static int
qnetd_client_msg_received_heuristics_change_reply(struct qnetd_instance *instance, struct qnetd_client *client,
    const struct msg_decoded *msg)
{

	return (qnetd_client_msg_received_unexpected_msg(client, msg, "heuristics change reply"));
}

int
qnetd_client_msg_received(struct qnetd_instance *instance, struct qnetd_client *client)
{
	struct msg_decoded msg;
	int res;
	int ret_val;
	int msg_processed;

	client->dpd_msg_received_since_last_check = 1;

	msg_decoded_init(&msg);

	res = msg_decode(&client->receive_buffer, &msg);
	if (res != 0) {
		/*
		 * Error occurred. Send server error.
		 */
		qnetd_log_msg_decode_error(res);
		qnetd_log(LOG_INFO, "Sending back error message");

		if (qnetd_client_send_err(client, msg.seq_number_set, msg.seq_number,
		    TLV_REPLY_ERROR_CODE_ERROR_DECODING_MSG) != 0) {
			return (-1);
		}

		return (0);
	}

	ret_val = 0;

	msg_processed = 0;
	switch (msg.type) {
	case MSG_TYPE_PREINIT:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_preinit(instance, client, &msg);
		break;
	case MSG_TYPE_PREINIT_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_preinit_reply(instance, client, &msg);
		break;
	case MSG_TYPE_STARTTLS:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_starttls(instance, client, &msg);
		break;
	case MSG_TYPE_INIT:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_init(instance, client, &msg);
		break;
	case MSG_TYPE_INIT_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_init_reply(instance, client, &msg);
		break;
	case MSG_TYPE_SERVER_ERROR:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_server_error(instance, client, &msg);
		break;
	case MSG_TYPE_SET_OPTION:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_set_option(instance, client, &msg);
		break;
	case MSG_TYPE_SET_OPTION_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_set_option_reply(instance, client, &msg);
		break;
	case MSG_TYPE_ECHO_REQUEST:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_echo_request(instance, client, &msg,
		    &client->receive_buffer);
		break;
	case MSG_TYPE_ECHO_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_echo_reply(instance, client, &msg);
		break;
	case MSG_TYPE_NODE_LIST:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_node_list(instance, client, &msg);
		break;
	case MSG_TYPE_NODE_LIST_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_node_list_reply(instance, client, &msg);
		break;
	case MSG_TYPE_ASK_FOR_VOTE:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_ask_for_vote(instance, client, &msg);
		break;
	case MSG_TYPE_ASK_FOR_VOTE_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_ask_for_vote_reply(instance, client, &msg);
		break;
	case MSG_TYPE_VOTE_INFO:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_vote_info(instance, client, &msg);
		break;
	case MSG_TYPE_VOTE_INFO_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_vote_info_reply(instance, client, &msg);
		break;
	case MSG_TYPE_HEURISTICS_CHANGE:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_heuristics_change(instance, client, &msg);
		break;
	case MSG_TYPE_HEURISTICS_CHANGE_REPLY:
		msg_processed = 1;
		ret_val = qnetd_client_msg_received_heuristics_change_reply(instance, client,
		    &msg);
		break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when new
	 * msg type is added.
	 */
	}

	if (!msg_processed) {
		qnetd_log(LOG_ERR, "Unsupported message %u received from client. "
		    "Sending back error message", msg.type);

		if (qnetd_client_send_err(client, msg.seq_number_set, msg.seq_number,
		    TLV_REPLY_ERROR_CODE_UNSUPPORTED_MESSAGE) != 0) {
			ret_val = -1;
		}
	}

	msg_decoded_destroy(&msg);

	return (ret_val);
}
