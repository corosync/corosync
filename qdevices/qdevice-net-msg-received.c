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

#include "qdevice-log.h"
#include "qdevice-net-algorithm.h"
#include "qdevice-net-cast-vote-timer.h"
#include "qdevice-net-heuristics.h"
#include "qdevice-net-msg-received.h"
#include "qdevice-net-send.h"
#include "qdevice-net-votequorum.h"
#include "qdevice-net-echo-request-timer.h"
#include "msg.h"
#include "utils.h"

/*
 * -1 - Incompatible tls combination
 *  0 - Don't use TLS
 *  1 - Use TLS
 */
static int
qdevice_net_msg_received_check_tls_compatibility(enum tlv_tls_supported server_tls,
    enum tlv_tls_supported client_tls)
{
	int res;

	res = -1;

	switch (server_tls) {
	case TLV_TLS_UNSUPPORTED:
		switch (client_tls) {
		case TLV_TLS_UNSUPPORTED: res = 0; break;
		case TLV_TLS_SUPPORTED: res = 0; break;
		case TLV_TLS_REQUIRED: res = -1; break;
		}
		break;
	case TLV_TLS_SUPPORTED:
		switch (client_tls) {
		case TLV_TLS_UNSUPPORTED: res = 0; break;
		case TLV_TLS_SUPPORTED: res = 1; break;
		case TLV_TLS_REQUIRED: res = 1; break;
		}
		break;
	case TLV_TLS_REQUIRED:
		switch (client_tls) {
		case TLV_TLS_UNSUPPORTED: res = -1; break;
		case TLV_TLS_SUPPORTED: res = 1; break;
		case TLV_TLS_REQUIRED: res = 1; break;
		}
		break;
	}

	return (res);
}

static void
qdevice_net_msg_received_log_msg_decode_error(int ret)
{

	switch (ret) {
	case -1:
		qdevice_log(LOG_WARNING, "Received message with option with invalid length");
		break;
	case -2:
		qdevice_log(LOG_CRIT, "Can't allocate memory");
		break;
	case -3:
		qdevice_log(LOG_WARNING, "Received inconsistent msg (tlv len > msg size)");
		break;
	case -4:
		qdevice_log(LOG_ERR, "Received message with option with invalid value");
		break;
	default:
		qdevice_log(LOG_ERR, "Unknown error occured when decoding message");
		break;
	}
}

static int
qdevice_net_msg_received_unexpected_msg(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg, const char *msg_str)
{

	qdevice_log(LOG_ERR, "Received unexpected %s message. Disconnecting from server",
	    msg_str);

	instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;

	return (-1);
}

static int
qdevice_net_msg_received_init(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "init"));
}

static int
qdevice_net_msg_received_preinit(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "preinit"));
}

static int
qdevice_net_msg_check_seq_number(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (!msg->seq_number_set || msg->seq_number != instance->last_msg_seq_num) {
		qdevice_log(LOG_ERR, "Received message doesn't contain seq_number or "
		    "it's not expected one.");

		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_preinit_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	int res;
	struct send_buffer_list_entry *send_buffer;

	qdevice_log(LOG_DEBUG, "Received preinit reply msg");

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_PREINIT_REPLY) {
		qdevice_log(LOG_ERR, "Received unexpected preinit reply message. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;

		return (-1);
	}

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	/*
	 * Check TLS support
	 */
	if (!msg->tls_supported_set || !msg->tls_client_cert_required_set) {
		qdevice_log(LOG_ERR, "Required tls_supported or tls_client_cert_required "
		    "option is unset");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	res = qdevice_net_msg_received_check_tls_compatibility(msg->tls_supported, instance->tls_supported);
	if (res == -1) {
		qdevice_log(LOG_ERR, "Incompatible tls configuration (server %u client %u)",
		    msg->tls_supported, instance->tls_supported);

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_INCOMPATIBLE_TLS;

		return (-1);
	} else if (res == 1) {
		/*
		 * Start TLS
		 */
		send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
		if (send_buffer == NULL) {
			qdevice_log(LOG_ERR, "Can't allocate send list buffer for "
			    "starttls msg");

			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;

			return (-1);
		}

		instance->last_msg_seq_num++;
		if (msg_create_starttls(&send_buffer->buffer, 1,
		    instance->last_msg_seq_num) == 0) {
			qdevice_log(LOG_ERR, "Can't allocate send buffer for starttls msg");

			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;

			send_buffer_list_discard_new(&instance->send_buffer_list, send_buffer);
			return (-1);
		}

		send_buffer_list_put(&instance->send_buffer_list, send_buffer);

		instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_STARTTLS_BEING_SENT;
	} else if (res == 0) {
		if (qdevice_net_send_init(instance) != 0) {
			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;

			return (-1);
		}
	}

	return (0);
}

static int
qdevice_net_msg_received_init_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	size_t zi;
	int res;
	enum qdevice_heuristics_mode active_heuristics_mode;

	qdevice_log(LOG_DEBUG, "Received init reply msg");

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_INIT_REPLY) {
		qdevice_log(LOG_ERR, "Received unexpected init reply message. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;

		return (-1);
	}

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	if (!msg->reply_error_code_set) {
		qdevice_log(LOG_ERR, "Received init reply message without error code."
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	if (msg->reply_error_code != TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qdevice_log(LOG_ERR, "Received init reply message with error code %"PRIu16". "
		    "Disconnecting from server", msg->reply_error_code);

		if (msg->reply_error_code == TLV_REPLY_ERROR_CODE_DUPLICATE_NODE_ID) {
			qdevice_log(LOG_ERR, "Duplicate node id may be result of server not yet "
			    "accepted this node disconnect. Retry again.");
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_SENT_DUPLICATE_NODE_ID_ERROR;
		} else if (msg->reply_error_code == TLV_REPLY_ERROR_CODE_TIE_BREAKER_DIFFERS_FROM_OTHER_NODES) {
			qdevice_log(LOG_ERR, "Configured tie-breaker differs in cluster. This may be "
			    "result of server not yet accepted this node disconnect. Retry again.");
			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_SERVER_SENT_TIE_BREAKER_DIFFERS_FROM_OTHER_NODES_ERROR;
		} else if (msg->reply_error_code == TLV_REPLY_ERROR_CODE_ALGORITHM_DIFFERS_FROM_OTHER_NODES) {
			qdevice_log(LOG_ERR, "Configured algorithm differs in cluster. This may be "
			    "result of server not yet accepted this node disconnect. Retry again.");
			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_SERVER_SENT_ALGORITHM_DIFFERS_FROM_OTHER_NODES_ERROR;
		} else {
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_SENT_ERROR;
		}

		return (-1);
	}

	if (!msg->server_maximum_request_size_set || !msg->server_maximum_reply_size_set) {
		qdevice_log(LOG_ERR, "Required maximum_request_size or maximum_reply_size "
		    "option is unset");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	if (msg->supported_messages == NULL || msg->supported_options == NULL) {
		qdevice_log(LOG_ERR, "Required supported messages or supported options "
		    "option is unset");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	if (msg->supported_decision_algorithms == NULL) {
		qdevice_log(LOG_ERR, "Required supported decision algorithms option is unset");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	if (msg->server_maximum_request_size < instance->advanced_settings->net_min_msg_send_size) {
		qdevice_log(LOG_ERR,
		    "Server accepts maximum %zu bytes message but this client minimum "
		    "is %zu bytes.", msg->server_maximum_request_size,
		    instance->advanced_settings->net_min_msg_send_size);

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_INCOMPATIBLE_MSG_SIZE;
		return (-1);
	}

	if (msg->server_maximum_reply_size > instance->advanced_settings->net_max_msg_receive_size) {
		qdevice_log(LOG_ERR,
		    "Server may send message up to %zu bytes message but this client maximum "
		    "is %zu bytes.", msg->server_maximum_reply_size,
		    instance->advanced_settings->net_max_msg_receive_size);

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_INCOMPATIBLE_MSG_SIZE;
		return (-1);
	}

	/*
	 * Change buffer sizes
	 */
	dynar_set_max_size(&instance->receive_buffer, msg->server_maximum_reply_size);
	send_buffer_list_set_max_buffer_size(&instance->send_buffer_list,
	    msg->server_maximum_request_size);


	/*
	 * Check if server supports decision algorithm we need
	 */
	res = 0;

	for (zi = 0; zi < msg->no_supported_decision_algorithms && !res; zi++) {
		if (msg->supported_decision_algorithms[zi] == instance->decision_algorithm) {
			res = 1;
		}
	}

	if (!res) {
		qdevice_log(LOG_ERR, "Server doesn't support required decision algorithm");

		instance->disconnect_reason =
		    QDEVICE_NET_DISCONNECT_REASON_SERVER_DOESNT_SUPPORT_REQUIRED_ALGORITHM;

		return (-1);
	}

	/*
	 * Check if server supports heuristics
	 */
	res = 0;
	for (zi = 0; zi < msg->no_supported_options; zi++) {
		if (msg->supported_options[zi] == TLV_OPT_HEURISTICS) {
			res = 1;
		}
	}

	instance->server_supports_heuristics = res;

	if (!res) {
		active_heuristics_mode = instance->qdevice_instance_ptr->heuristics_instance.mode;

		if (active_heuristics_mode == QDEVICE_HEURISTICS_MODE_ENABLED ||
		    active_heuristics_mode == QDEVICE_HEURISTICS_MODE_SYNC) {
			qdevice_log(LOG_ERR, "Heuristics are enabled but not supported by server");

			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_SERVER_DOESNT_SUPPORT_REQUIRED_OPT;

			return (-1);
		}
	}

	/*
	 * Finally fully connected so it's possible to remove connection timer
	 */
	if (instance->connect_timer != NULL) {
		timer_list_delete(&instance->main_timer_list, instance->connect_timer);
		instance->connect_timer = NULL;
	}

	/*
	 * Server accepted heartbeat interval -> schedule regular sending of echo request
	 */
	if (qdevice_net_echo_request_timer_schedule(instance) != 0) {
		return (-1);
	}

	/*
	 * Run heuristics (even when it is disabled, undefined result is ok, rest of sending
	 * is handled by qdevice_net_connect_heuristics_exec_result_callback
	 */
	if (qdevice_net_heuristics_exec_after_connect(instance) != 0) {
		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_starttls(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "starttls"));
}

static int
qdevice_net_msg_received_server_error(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (!msg->reply_error_code_set) {
		qdevice_log(LOG_ERR, "Received server error without error code set. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;
	} else {
		qdevice_log(LOG_ERR, "Received server error %"PRIu16". "
		    "Disconnecting from server", msg->reply_error_code);

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_SENT_ERROR;
	}

	return (-1);
}

static int
qdevice_net_msg_received_set_option(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "set option"));
}

static int
qdevice_net_msg_received_set_option_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_log(LOG_ERR, "Received unexpected set option reply message. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;

		return (-1);
	}

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;

		return (-1);
	}

	if (qdevice_net_echo_request_timer_schedule(instance) != 0) {
		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_echo_request(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "echo request"));
}

static int
qdevice_net_msg_received_echo_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (!msg->seq_number_set) {
		qdevice_log(LOG_ERR, "Received echo reply message doesn't contain seq_number.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;
		return (-1);
	}

	if (msg->seq_number != instance->echo_request_expected_msg_seq_num) {
		qdevice_log(LOG_WARNING, "Received echo reply message seq_number is not expected one.");
	}

	if (qdevice_net_algorithm_echo_reply_received(instance, msg->seq_number,
	    msg->seq_number == instance->echo_request_expected_msg_seq_num) != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_ECHO_REPLY_RECEIVED_ERR;
		return (-1);
	}

	instance->echo_reply_received_msg_seq_num = msg->seq_number;
	instance->last_echo_reply_received_time = time(NULL);

	return (0);
}

static int
qdevice_net_msg_received_node_list(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "node list"));
}

static int
qdevice_net_msg_received_node_list_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	const char *str;
	enum tlv_vote result_vote;
	int res;
	int case_processed;
	int ring_id_is_valid;

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_log(LOG_ERR, "Received unexpected node list reply message. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;
		return (-1);
	}

	if (!msg->vote_set || !msg->seq_number_set || !msg->node_list_type_set) {
		qdevice_log(LOG_ERR, "Received node list reply message without "
		    "required options. Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;
		return (-1);
	}

	if (!msg->ring_id_set) {
		qdevice_log(LOG_ERR, "Received node list reply message "
		    "without ring id set. Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;
		return (-1);
	}

	str = NULL;

	switch (msg->node_list_type) {
	case TLV_NODE_LIST_TYPE_INITIAL_CONFIG: str = "initial config"; break;
	case TLV_NODE_LIST_TYPE_CHANGED_CONFIG: str = "changed config"; break;
	case TLV_NODE_LIST_TYPE_MEMBERSHIP: str ="membership"; break;
	case TLV_NODE_LIST_TYPE_QUORUM: str ="quorum"; break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when new node list type
	 * is added
	 */
	}

	if (str == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_net_msg_received_node_list_reply fatal error. "
		    "Unhandled node_list_type (debug output)");
		exit(1);
	}

	qdevice_log(LOG_DEBUG, "Received %s node list reply", str);
	qdevice_log(LOG_DEBUG, "  seq = "UTILS_PRI_MSG_SEQ, msg->seq_number);
	qdevice_log(LOG_DEBUG, "  vote = %s", tlv_vote_to_str(msg->vote));
	qdevice_log(LOG_DEBUG, "  ring id = ("UTILS_PRI_RING_ID")",
		    msg->ring_id.node_id, msg->ring_id.seq);

	/*
	 * Call algorithm
	 */
	result_vote = msg->vote;

	if (!tlv_ring_id_eq(&msg->ring_id, &instance->last_sent_ring_id)) {
		ring_id_is_valid = 0;
		qdevice_log(LOG_DEBUG, "Received node list reply with old ring id.");
	} else {
		ring_id_is_valid = 1;
	}

	case_processed = 0;
	switch (msg->node_list_type) {
	case TLV_NODE_LIST_TYPE_INITIAL_CONFIG:
	case TLV_NODE_LIST_TYPE_CHANGED_CONFIG:
		case_processed = 1;
		res = qdevice_net_algorithm_config_node_list_reply_received(instance,
		    msg->seq_number, (msg->node_list_type == TLV_NODE_LIST_TYPE_INITIAL_CONFIG),
		    &msg->ring_id, ring_id_is_valid, &result_vote);
		break;
	case TLV_NODE_LIST_TYPE_MEMBERSHIP:
		case_processed = 1;
		res = qdevice_net_algorithm_membership_node_list_reply_received(instance,
		    msg->seq_number, &msg->ring_id, ring_id_is_valid, &result_vote);
		break;
	case TLV_NODE_LIST_TYPE_QUORUM:
		case_processed = 1;
		res = qdevice_net_algorithm_quorum_node_list_reply_received(instance,
		    msg->seq_number, &msg->ring_id, ring_id_is_valid, &result_vote);
		break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when new node list type
	 * is added
	 */
	}

	if (!case_processed) {
		qdevice_log(LOG_CRIT, "qdevice_net_msg_received_node_list_reply fatal error. "
		    "Unhandled node_list_type (algorithm call)");
		exit(1);
	}

	if (res != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_NODE_LIST_REPLY_ERR;
		return (-1);
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	if (qdevice_net_cast_vote_timer_update(instance, result_vote) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_ask_for_vote(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "ask for vote"));
}

static int
qdevice_net_msg_received_ask_for_vote_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	enum tlv_vote result_vote;
	int ring_id_is_valid;

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_log(LOG_ERR, "Received unexpected ask for vote reply message. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;
		return (-1);
	}

	if (!msg->vote_set || !msg->seq_number_set || !msg->ring_id_set) {
		qdevice_log(LOG_ERR, "Received ask for vote reply message without "
		    "required options. Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;
		return (-1);
	}

	qdevice_log(LOG_DEBUG, "Received ask for vote reply");
	qdevice_log(LOG_DEBUG, "  seq = "UTILS_PRI_MSG_SEQ, msg->seq_number);
	qdevice_log(LOG_DEBUG, "  vote = %s", tlv_vote_to_str(msg->vote));
	qdevice_log(LOG_DEBUG, "  ring id = ("UTILS_PRI_RING_ID")",
		    msg->ring_id.node_id, msg->ring_id.seq);

	result_vote = msg->vote;

	if (!tlv_ring_id_eq(&msg->ring_id, &instance->last_sent_ring_id)) {
		ring_id_is_valid = 0;
		qdevice_log(LOG_DEBUG, "Received ask for vote reply with old ring id.");
	} else {
		ring_id_is_valid = 1;
	}

	if (qdevice_net_algorithm_ask_for_vote_reply_received(instance, msg->seq_number,
	    &msg->ring_id, ring_id_is_valid, &result_vote) != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_ASK_FOR_VOTE_REPLY_ERR;
		return (-1);
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	if (qdevice_net_cast_vote_timer_update(instance, result_vote) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_vote_info(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	struct send_buffer_list_entry *send_buffer;
	enum tlv_vote result_vote;
	int ring_id_is_valid;

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_log(LOG_ERR, "Received unexpected vote info message. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;
		return (-1);
	}

	if (!msg->vote_set || !msg->seq_number_set || !msg->ring_id_set) {
		qdevice_log(LOG_ERR, "Received node list reply message without "
		    "required options. Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;
		return (-1);
	}

	qdevice_log(LOG_DEBUG, "Received vote info");
	qdevice_log(LOG_DEBUG, "  seq = "UTILS_PRI_MSG_SEQ, msg->seq_number);
	qdevice_log(LOG_DEBUG, "  vote = %s", tlv_vote_to_str(msg->vote));
	qdevice_log(LOG_DEBUG, "  ring id = ("UTILS_PRI_RING_ID")",
		    msg->ring_id.node_id, msg->ring_id.seq);

	result_vote = msg->vote;

	if (!tlv_ring_id_eq(&msg->ring_id, &instance->last_sent_ring_id)) {
		ring_id_is_valid = 0;
		qdevice_log(LOG_DEBUG, "Received vote info with old ring id.");
	} else {
		ring_id_is_valid = 1;
	}

	if (qdevice_net_algorithm_vote_info_received(instance, msg->seq_number,
	    &msg->ring_id, ring_id_is_valid, &result_vote) != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_VOTE_INFO_ERR;
		return (-1);
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	if (qdevice_net_cast_vote_timer_update(instance, result_vote) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		return (-1);
	}

	/*
	 * Create reply message
	 */
	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_log(LOG_ERR, "Can't allocate send list buffer for "
		    "vote info reply msg");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
		return (-1);
	}

	if (msg_create_vote_info_reply(&send_buffer->buffer, msg->seq_number) == 0) {
		qdevice_log(LOG_ERR, "Can't allocate send buffer for "
		    "vote info reply list msg");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
		send_buffer_list_discard_new(&instance->send_buffer_list, send_buffer);
		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}

static int
qdevice_net_msg_received_vote_info_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "vote info reply"));
}

static int
qdevice_net_msg_received_heuristics_change(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "heuristics change"));
}

static int
qdevice_net_msg_received_heuristics_change_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	enum tlv_vote result_vote;
	int ring_id_is_valid;

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_log(LOG_ERR, "Received unexpected heuristics change reply message. "
		    "Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;
		return (-1);
	}

	if (!msg->vote_set || !msg->seq_number_set || !msg->ring_id_set ||
	    msg->heuristics == TLV_HEURISTICS_UNDEFINED) {
		qdevice_log(LOG_ERR, "Received heuristics change reply message without "
		    "required options. Disconnecting from server");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_REQUIRED_OPTION_MISSING;
		return (-1);
	}

	qdevice_log(LOG_DEBUG, "Received heuristics change reply");
	qdevice_log(LOG_DEBUG, "  seq = "UTILS_PRI_MSG_SEQ, msg->seq_number);
	qdevice_log(LOG_DEBUG, "  vote = %s", tlv_vote_to_str(msg->vote));
	qdevice_log(LOG_DEBUG, "  ring id = ("UTILS_PRI_RING_ID")",
		    msg->ring_id.node_id, msg->ring_id.seq);
	qdevice_log(LOG_DEBUG, "  heuristics = %s", tlv_heuristics_to_str(msg->heuristics));

	result_vote = msg->vote;

	if (!tlv_ring_id_eq(&msg->ring_id, &instance->last_sent_ring_id)) {
		ring_id_is_valid = 0;
		qdevice_log(LOG_DEBUG, "Received heuristics change reply with old ring id.");
	} else {
		ring_id_is_valid = 1;
	}

	if (qdevice_net_algorithm_heuristics_change_reply_received(instance, msg->seq_number,
	    &msg->ring_id, ring_id_is_valid, msg->heuristics, &result_vote) != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_HEURISTICS_CHANGE_REPLY_ERR;
		return (-1);
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	if (qdevice_net_cast_vote_timer_update(instance, result_vote) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		return (-1);
	}

	return (0);
}

int
qdevice_net_msg_received(struct qdevice_net_instance *instance)
{
	struct msg_decoded msg;
	int res;
	int ret_val;
	int msg_processed;

	msg_decoded_init(&msg);

	res = msg_decode(&instance->receive_buffer, &msg);
	if (res != 0) {
		/*
		 * Error occurred. Disconnect.
		 */
		qdevice_net_msg_received_log_msg_decode_error(res);
		qdevice_log(LOG_ERR, "Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_MSG_DECODE_ERROR;

		return (-1);
	}

	ret_val = 0;

	msg_processed = 0;

	switch (msg.type) {
	case MSG_TYPE_INIT:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_init(instance, &msg);
		break;
	case MSG_TYPE_PREINIT:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_preinit(instance, &msg);
		break;
	case MSG_TYPE_PREINIT_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_preinit_reply(instance, &msg);
		break;
	case MSG_TYPE_STARTTLS:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_starttls(instance, &msg);
		break;
	case MSG_TYPE_SERVER_ERROR:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_server_error(instance, &msg);
		break;
	case MSG_TYPE_INIT_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_init_reply(instance, &msg);
		break;
	case MSG_TYPE_SET_OPTION:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_set_option(instance, &msg);
		break;
	case MSG_TYPE_SET_OPTION_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_set_option_reply(instance, &msg);
		break;
	case MSG_TYPE_ECHO_REQUEST:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_echo_request(instance, &msg);
		break;
	case MSG_TYPE_ECHO_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_echo_reply(instance, &msg);
		break;
	case MSG_TYPE_NODE_LIST:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_node_list(instance, &msg);
		break;
	case MSG_TYPE_NODE_LIST_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_node_list_reply(instance, &msg);
		break;
	case MSG_TYPE_ASK_FOR_VOTE:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_ask_for_vote(instance, &msg);
		break;
	case MSG_TYPE_ASK_FOR_VOTE_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_ask_for_vote_reply(instance, &msg);
		break;
	case MSG_TYPE_VOTE_INFO:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_vote_info(instance, &msg);
		break;
	case MSG_TYPE_VOTE_INFO_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_vote_info_reply(instance, &msg);
		break;
	case MSG_TYPE_HEURISTICS_CHANGE:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_heuristics_change(instance, &msg);
		break;
	case MSG_TYPE_HEURISTICS_CHANGE_REPLY:
		msg_processed = 1;
		ret_val = qdevice_net_msg_received_heuristics_change_reply(instance, &msg);
	/*
	 * Default is not defined intentionally. Compiler shows warning when msg type is added
	 */
	}

	if (!msg_processed) {
		qdevice_log(LOG_ERR, "Received unsupported message %u. "
		    "Disconnecting from server", msg.type);
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNEXPECTED_MSG;

		ret_val = -1;
	}

	msg_decoded_destroy(&msg);

	return (ret_val);
}
