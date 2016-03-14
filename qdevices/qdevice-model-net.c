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


#include <config.h>

#include "qdevice-model.h"
#include "qdevice-model-net.h"
#include "qdevice-log.h"
#include "qdevice-net-cast-vote-timer.h"
#include "qdevice-net-instance.h"
#include "qdevice-net-algorithm.h"
#include "qdevice-net-poll.h"
#include "qdevice-net-send.h"
#include "qdevice-net-votequorum.h"
#include "qnet-config.h"
#include "nss-sock.h"

int
qdevice_model_net_init(struct qdevice_instance *instance)
{

	struct qdevice_net_instance *net_instance;

	qdevice_log(LOG_DEBUG, "Initializing qdevice_net_instance");
	if (qdevice_net_instance_init_from_cmap(instance) != 0) {
		return (-1);
	}

	net_instance = instance->model_data;

	qdevice_log(LOG_DEBUG, "Registering algorithms");
	if (qdevice_net_algorithm_register_all() != 0) {
		return (-1);
	}

	qdevice_log(LOG_DEBUG, "Initializing NSS");
	if (nss_sock_init_nss((net_instance->tls_supported != TLV_TLS_UNSUPPORTED ?
	    (char *)QDEVICE_NET_NSS_DB_DIR : NULL)) != 0) {
		qdevice_log_nss(LOG_ERR, "Can't init nss");
		return (-1);
	}

	if (qdevice_net_cast_vote_timer_update(net_instance, TLV_VOTE_ASK_LATER) != 0) {
		qdevice_log(LOG_ERR, "Can't update cast vote timer");
		return (-1);
	}

	if (qdevice_net_algorithm_init(net_instance) != 0) {
		qdevice_log(LOG_ERR, "Algorithm init failed");
		return (-1);
	}

	return (0);
}

int
qdevice_model_net_destroy(struct qdevice_instance *instance)
{
	struct qdevice_net_instance *net_instance;

	net_instance = instance->model_data;

	qdevice_log(LOG_DEBUG, "Destroying algorithm");
	qdevice_net_algorithm_destroy(net_instance);

	qdevice_log(LOG_DEBUG, "Destroying qdevice_net_instance");
	qdevice_net_instance_destroy(net_instance);

	qdevice_log(LOG_DEBUG, "Shutting down NSS");
	SSL_ClearSessionCache();

	if (NSS_Shutdown() != SECSuccess) {
		qdevice_log_nss(LOG_WARNING, "Can't shutdown NSS");
	}

	PR_Cleanup();

	free(net_instance);

	return (0);
}

static int
qdevice_model_net_timer_connect_timeout(void *data1, void *data2)
{
	struct qdevice_net_instance *instance;

	instance = (struct qdevice_net_instance *)data1;

	qdevice_log(LOG_ERR, "Connect timeout");

	instance->schedule_disconnect = 1;

	instance->connect_timer = NULL;
	instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_CONNECT_TO_THE_SERVER;

	return (0);
}

int
qdevice_model_net_run(struct qdevice_instance *instance)
{
	struct qdevice_net_instance *net_instance;
	int try_connect;
	int res;

	net_instance = instance->model_data;

	qdevice_log(LOG_DEBUG, "Executing qdevice-net");

	try_connect = 1;
	while (try_connect) {
		net_instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT;
		net_instance->socket = NULL;

		net_instance->connect_timer = timer_list_add(&net_instance->main_timer_list,
			net_instance->connect_timeout, qdevice_model_net_timer_connect_timeout,
			(void *)net_instance, NULL);

		if (net_instance->connect_timer == NULL) {
			qdevice_log(LOG_CRIT, "Can't schedule connect timer");

			try_connect = 0;
			break ;
		}

		qdevice_log(LOG_DEBUG, "Trying connect to qnetd server %s:%u (timeout = %u)",
		    net_instance->host_addr, net_instance->host_port, net_instance->connect_timeout);

		res = nss_sock_non_blocking_client_init(net_instance->host_addr,
		    net_instance->host_port, PR_AF_UNSPEC, &net_instance->non_blocking_client);
		if (res == -1) {
			qdevice_log_nss(LOG_ERR, "Can't initialize non blocking client connection");
		}

		res = nss_sock_non_blocking_client_try_next(&net_instance->non_blocking_client);
		if (res == -1) {
			qdevice_log_nss(LOG_ERR, "Can't connect to qnetd host");
			nss_sock_non_blocking_client_destroy(&net_instance->non_blocking_client);
		}

		while (qdevice_net_poll(net_instance) == 0) {
		};

		if (net_instance->connect_timer != NULL) {
			timer_list_delete(&net_instance->main_timer_list, net_instance->connect_timer);
			net_instance->connect_timer = NULL;
		}

		if (net_instance->echo_request_timer != NULL) {
			timer_list_delete(&net_instance->main_timer_list, net_instance->echo_request_timer);
			net_instance->echo_request_timer = NULL;
		}

		try_connect = qdevice_net_disconnect_reason_try_reconnect(net_instance->disconnect_reason);

		if (qdevice_net_algorithm_disconnected(net_instance,
		    net_instance->disconnect_reason, &try_connect) != 0) {
			qdevice_log(LOG_ERR, "Algorithm returned error, force exit");
			return (-1);
		}

		if (net_instance->disconnect_reason ==
		    QDEVICE_NET_DISCONNECT_REASON_COROSYNC_CONNECTION_CLOSED) {
			try_connect = 0;
		}

		if (net_instance->socket != NULL) {
			if (PR_Close(net_instance->socket) != PR_SUCCESS) {
				qdevice_log_nss(LOG_WARNING, "Unable to close connection");
			}
			net_instance->socket = NULL;
		}

		if (!net_instance->non_blocking_client.destroyed) {
			nss_sock_non_blocking_client_destroy(&net_instance->non_blocking_client);
		}

		if (net_instance->non_blocking_client.socket != NULL) {
			if (PR_Close(net_instance->non_blocking_client.socket) != PR_SUCCESS) {
				qdevice_log_nss(LOG_WARNING, "Unable to close non-blocking client connection");
			}
			net_instance->non_blocking_client.socket = NULL;
		}

		qdevice_net_instance_clean(net_instance);
	}

	return (0);
}

/*
 * Called when cmap reload (or nodelist) was requested.
 *
 * nlist is node list
 * config_version is valid only if config_version_set != 0
 *
 * Should return 0 if processing should continue or -1 to call exit
 */
int
qdevice_model_net_config_node_list_changed(struct qdevice_instance *instance,
    const struct node_list *nlist, int config_version_set, uint64_t config_version)
{
	struct qdevice_net_instance *net_instance;
	int send_node_list;
	enum tlv_vote vote;

	net_instance = instance->model_data;

	if (net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		/*
		 * Nodelist changed, but connection to qnetd not initiated yet.
		 */
		send_node_list = 0;

		if (net_instance->cast_vote_timer_vote == TLV_VOTE_ACK) {
			vote = TLV_VOTE_NACK;
		} else {
			vote = TLV_VOTE_NO_CHANGE;
		}
	} else {
		send_node_list = 1;
		vote = TLV_VOTE_NO_CHANGE;
	}

	if (qdevice_net_algorithm_config_node_list_changed(net_instance, nlist, config_version_set,
	    config_version, &send_node_list, &vote) != 0) {
		qdevice_log(LOG_ERR, "Algorithm returned error, force exit");
		return (-1);
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm decided to %s node list and result vote is %s",
		    (send_node_list ? "send" : "not send"), tlv_vote_to_str(vote));
	}

	if (qdevice_net_cast_vote_timer_update(net_instance, vote) != 0) {
		qdevice_log(LOG_CRIT, "qdevice_model_net_config_node_list_changed fatal error. "
				" Can't update cast vote timer vote");
		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		net_instance->schedule_disconnect = 1;
	}

	if (send_node_list) {
		if (qdevice_net_send_config_node_list(net_instance, nlist, config_version_set,
		    config_version, 0) != 0) {
			net_instance->schedule_disconnect = 1;
			net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
		}
	}

	return (0);
}

/*
 * Called when cmap reload (or nodelist) was requested, but it was not possible to
 * get node list.
 *
 * Should return 0 if processing should continue or -1 to call exit
 */
int
qdevice_model_net_get_config_node_list_failed(struct qdevice_instance *instance)
{
	struct qdevice_net_instance *net_instance;

	net_instance = instance->model_data;

	net_instance->schedule_disconnect = 1;
	net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;

	return (0);
}

int
qdevice_model_net_votequorum_quorum_notify(struct qdevice_instance *instance,
    uint32_t quorate, uint32_t node_list_entries, votequorum_node_t node_list[])
{
	struct qdevice_net_instance *net_instance;
	int send_node_list;
	enum tlv_vote vote;

	net_instance = instance->model_data;

	if (net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		/*
		 * Nodelist changed, but connection to qnetd not initiated yet.
		 */
		send_node_list = 0;

		if (net_instance->cast_vote_timer_vote == TLV_VOTE_ACK) {
			vote = TLV_VOTE_NACK;
		} else {
			vote = TLV_VOTE_NO_CHANGE;
		}
	} else {
		send_node_list = 1;
		vote = TLV_VOTE_NO_CHANGE;
	}

	if (qdevice_net_algorithm_votequorum_quorum_notify(net_instance, quorate,
	    node_list_entries, node_list, &send_node_list, &vote) != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_VOTEQUORUM_QUORUM_NOTIFY_ERR;
		net_instance->schedule_disconnect = 1;
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm decided to %s list and result vote is %s",
		    (send_node_list ? "send" : "not send"), tlv_vote_to_str(vote));
	}

	if (qdevice_net_cast_vote_timer_update(net_instance, vote) != 0) {
		qdevice_log(LOG_CRIT, "qdevice_model_net_votequorum_quorum_notify fatal error. "
				" Can't update cast vote timer vote");
		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		net_instance->schedule_disconnect = 1;
	}

	if (send_node_list) {
		if (qdevice_net_send_quorum_node_list(net_instance,
		    (quorate ? TLV_QUORATE_QUORATE : TLV_QUORATE_INQUORATE),
		    node_list_entries, node_list) != 0) {
			/*
			 * Fatal error -> schedule disconnect
			 */
			net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			net_instance->schedule_disconnect = 1;
		}
	}

	return (0);
}

int
qdevice_model_net_votequorum_node_list_notify(struct qdevice_instance *instance,
    votequorum_ring_id_t votequorum_ring_id, uint32_t node_list_entries, uint32_t node_list[])
{
	struct qdevice_net_instance *net_instance;
	struct tlv_ring_id tlv_rid;
	enum tlv_vote vote;
	int send_node_list;

	net_instance = instance->model_data;

	qdevice_net_votequorum_ring_id_to_tlv(&tlv_rid, &votequorum_ring_id);

	if (net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		/*
		 * Nodelist changed, but connection to qnetd not initiated yet.
		 */
		send_node_list = 0;

		if (net_instance->cast_vote_timer_vote == TLV_VOTE_ACK) {
			vote = TLV_VOTE_NACK;
		} else {
			vote = TLV_VOTE_NO_CHANGE;
		}
	} else {
		send_node_list = 1;
		vote = TLV_VOTE_NO_CHANGE;
	}

	if (qdevice_net_algorithm_votequorum_node_list_notify(net_instance, &tlv_rid,
	    node_list_entries, node_list, &send_node_list, &vote) != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_VOTEQUORUM_NODE_LIST_NOTIFY_ERR;
		net_instance->schedule_disconnect = 1;
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm decided to %s list and result vote is %s",
		    (send_node_list ? "send" : "not send"), tlv_vote_to_str(vote));
	}

	if (send_node_list) {
		if (qdevice_net_send_membership_node_list(net_instance, &tlv_rid,
		    node_list_entries, node_list) != 0) {
			/*
			 * Fatal error -> schedule disconnect
			 */
			net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			net_instance->schedule_disconnect = 1;
		}
	}

	if (qdevice_net_cast_vote_timer_update(net_instance, vote) != 0) {
		qdevice_log(LOG_CRIT, "qdevice_model_net_votequorum_node_list_notify fatal error "
		    "Can't update cast vote timer");
		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		net_instance->schedule_disconnect = 1;
	}

	return (0);
}

int
qdevice_model_net_votequorum_expected_votes_notify(struct qdevice_instance *instance,
    uint32_t expected_votes)
{
	struct qdevice_net_instance *net_instance;

	net_instance = instance->model_data;

	return (0);
}

static struct qdevice_model qdevice_model_net = {
	.name					= "net",
	.init					= qdevice_model_net_init,
	.destroy				= qdevice_model_net_destroy,
	.run					= qdevice_model_net_run,
	.get_config_node_list_failed		= qdevice_model_net_get_config_node_list_failed,
	.config_node_list_changed		= qdevice_model_net_config_node_list_changed,
	.votequorum_quorum_notify		= qdevice_model_net_votequorum_quorum_notify,
	.votequorum_node_list_notify		= qdevice_model_net_votequorum_node_list_notify,
	.votequorum_expected_votes_notify	= qdevice_model_net_votequorum_expected_votes_notify,
};

int
qdevice_model_net_register(void)
{
	return (qdevice_model_register(QDEVICE_MODEL_TYPE_NET, &qdevice_model_net));
}
