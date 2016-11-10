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

#include "qdevice-net-algo-test.h"
#include "qdevice-log.h"
#include "qdevice-net-send.h"
#include "qdevice-net-cast-vote-timer.h"

/*
 * Called after qdevice_net_instance is initialized. Connection to server is not yet
 * established. Used mainly for allocating instance->algorithm_data.
 *
 * Callback should return 0 on success or -1 on failure.
 */
int
qdevice_net_algo_test_init(struct qdevice_net_instance *instance)
{

	instance->algorithm_data = NULL;
	qdevice_log(LOG_INFO, "algo-test: Initialized");

	return (0);
}

/*
 * Called after qdevice connected to qnetd.
 * send_config_node_list, send_membership_node_list and send_quorum_node_list can be set to
 * nonzero (default) to make qdevice-net send given lists to qnetd
 * vote (default TLV_VOTE_WAIT_FOR_REPLY) can be set to update voting timer
 * heuristics is result of pre connect heuristics and can be updated
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_connected(struct qdevice_net_instance *instance, enum tlv_heuristics *heuristics,
    int *send_config_node_list, int *send_membership_node_list, int *send_quorum_node_list, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Connected");

	return (0);
}

/*
 * Called after config node list changed.
 *
 * Callback can override send_node_list and vote.
 * Depending on net_instance->state, they are set acordingly:
 * If net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS
 *   send_node_list = 0
 *   if cast_vote_timer_vote != TLV_VOTE_ACK
 *     vote = TLV_VOTE_NO_CHANGE
 *   if cast_vote_timer_vote = TLV_VOTE_ACK
 *     vote = TLV_VOTE_NACK.
 * Otherwise send_node_list = 1 and vote = TLV_VOTE_NO_CHANGE
 * If send_node_list is set to non zero, node list is sent to qnetd
 */
int
qdevice_net_algo_test_config_node_list_changed(struct qdevice_net_instance *instance,
    const struct node_list *nlist, int config_version_set, uint64_t config_version,
    int *send_node_list, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Config node list changed");

	return (0);
}

/*
 * Called after votequorum node list notify is dispatched but heuristics result is not
 * yet known (heuristics were just executed). Full list together with result of heuristics
 * are received in qdevice_net_algo_test_votequorum_node_list_heuristics_notify.
 * This applies also if heuristics are disabled.
 *
 * pause_cast_vote_timer is preset to 1. If after callback this value is still != 0,
 * cast vote timer is paused until heuristics finishes.
 *
 * vote is by default set to TLV_VOTE_NO_CHANGE with exception of situation when
 *   net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS and
 *   cast_vote_timer_vote = TLV_VOTE_ACK
 * then vote is set to TLV_VOTE_NACK. This is to allow qnetd disconnection and still vote
 * ACK as long as no change happens.
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_votequorum_node_list_notify(struct qdevice_net_instance *instance,
    const struct tlv_ring_id *ring_id, uint32_t node_list_entries, uint32_t node_list[],
    int *pause_cast_vote_timer, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Votequorum list notify");

	return (0);
}

/*
 * Called after votequorum node list notify is dispatched and heuristics result is known.
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 *
 * If net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS
 *   send_node_list = 0
 *   if cast_vote_timer_vote != TLV_VOTE_ACK
 *     vote = TLV_VOTE_NO_CHANGE
 *   if cast_vote_timer_vote = TLV_VOTE_ACK
 *     vote = TLV_VOTE_NACK.
 * Otherwise send_node_list = 1 and vote = TLV_VOTE_WAIT_FOR_REPLY
 * If send_node_list is set to non zero, node list is sent to qnetd
 * heuristics results can be used (and overwrite)
 */
int
qdevice_net_algo_test_votequorum_node_list_heuristics_notify(struct qdevice_net_instance *instance,
    const struct tlv_ring_id *ring_id, uint32_t node_list_entries, uint32_t node_list[],
    int *send_node_list, enum tlv_vote *vote, enum tlv_heuristics *heuristics)
{

	qdevice_log(LOG_INFO, "algo-test: Votequorum heuristics list notify");

	return (0);
}

/*
 * Called after votequorum quorum notify is dispatched.
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 *
 * Callback can override send_node_list and vote.
 * Depending on net_instance->state, they are set acordingly:
 * If net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS
 *   send_node_list = 0
 *   if cast_vote_timer_vote != TLV_VOTE_ACK
 *     vote = TLV_VOTE_NO_CHANGE
 *   if cast_vote_timer_vote = TLV_VOTE_ACK
 *     vote = TLV_VOTE_NACK.
 * Otherwise send_node_list = 1 and vote = TLV_VOTE_NO_CHANGE
 *
 * If send_node_list is set to non zero, node list is sent to qnetd
 */
int
qdevice_net_algo_test_votequorum_quorum_notify(struct qdevice_net_instance *instance,
    uint32_t quorate, uint32_t node_list_entries, votequorum_node_t node_list[], int *send_node_list,
    enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Votequorum quorum notify");

	return (0);
}
/*
 * Called after votequorum expected_votes notify is dispatched.
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 *
 * Vote is set to TLV_VOTE_NO_CHANGE
 */
int
qdevice_net_algo_test_votequorum_expected_votes_notify(struct qdevice_net_instance *instance,
    uint32_t expected_votes, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Votequorum expected votes notify");

	return (0);
}

/*
 * Called when config node list reply is received. Vote is set to value returned by server (and can
 * be overwriten by algorithm). ring_id is returned by server. ring_id_is_valid is set to 1 only if
 * received ring id matches last sent ring id. If it is 0, then it usually makes sense to not
 * update timer.
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_config_node_list_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, int initial, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Config node list reply");

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

/*
 * Called when membership node list reply (reply for votequorum votequorum_nodelist_notify_fn)
 * is received. Vote is set to value returned by server (and can be overwriten by algorithm).
 *
 * Also if server returned TLV_VOTE_ASK_LATER, it's good idea to create timer (call timer_list_add
 * with instance->main_timer_list parameter) and ask for reply (qdevice_net_send_ask_for_vote).
 * Another option may be to wait for vote_info message (if server algorithm is configured so).
 *
 * ring_id and ring_id_is_valid have same meaning as for
 * qdevice_net_algo_test_config_node_list_reply_received
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_membership_node_list_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Membership node list reply");

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

/*
 * Called when quorum node list reply (reply for votequorum votequorum_quorum_notify_fn)
 * is received. Vote is set to value returned by server (and can be overwriten by algorithm).
 *
 * ring_id and ring_id_is_valid have same meaning as for
 * qdevice_net_algo_test_config_node_list_reply_received
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_quorum_node_list_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Quorum node list reply");

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

/*
 * Called when reply for ask for vote message was received.
 * Vote is set to value returned by server (and can be overwriten by algorithm).
 *
 * ring_id and ring_id_is_valid have same meaning as for
 * qdevice_net_algo_test_config_node_list_reply_received
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_ask_for_vote_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Ask for vote reply received");

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

/*
 * Called when vote info message from server was received.
 * Vote is set to value sent by server (and can be overwriten by algorithm).
 *
 * ring_id and ring_id_is_valid have same meaning as for
 * qdevice_net_algo_test_config_node_list_reply_received
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_vote_info_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Vote info received");

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

/*
 * Called when echo reply message was received.
 * is_expected_seq_number is set to 1 if received seq_number was equal to last sent echo request.
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_echo_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, int is_expected_seq_number)
{

	qdevice_log(LOG_INFO, "algo-test: Echo reply received");

	return (is_expected_seq_number ? 0 : -1);
}

/*
 * Called when client is about to send echo request but echo reply to previous echo request
 * was not yet received.
 *
 * Callback should return 0 if processing should continue (echo request is not sent but timer is
 * scheduled again) otherwise -1 (-> disconnect client).
 */
int
qdevice_net_algo_test_echo_reply_not_received(struct qdevice_net_instance *instance)
{

	qdevice_log(LOG_INFO, "algo-test: Echo reply not received");

	return (-1);
}

/*
 * Called when regular heuristics finished and it's result differs from previous heuristics.
 *
 * heuristics contains result of heuristics
 * send_msg distinquish if message should be sent to qnetd. Default value is 0 if qnetd is not
 *   connected and 1 otherwise
 * vote can be set to change cast_vote_timer voting value (default is TLV_VOTE_NO_CHANGE)
 *
 * It's not possible to set send_msg to 1 and heuristics to TLV_HEURISTICS_UNDEFINED
 *
 * Callback should return 0 on success, -1 on failure (-> force exit)
 */
int
qdevice_net_algo_test_heuristics_change(struct qdevice_net_instance *instance,
    enum tlv_heuristics *heuristics, int *send_msg, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Heuristics change");

	return (0);
}

/*
 * Called when reply for heuristics change was received.
 * Vote is set to value returned by server (and can be overwriten by algorithm).
 *
 * ring_id and ring_id_is_valid have same meaning as for
 * qdevice_net_algo_test_config_node_list_reply_received
 *
 * heuristics is unmodified value sent to server (and set by qdevice_net_algo_test_heuristics_change)
 *
 * Callback should return 0 on success or -1 on failure (-> disconnect client).
 */
int
qdevice_net_algo_test_heuristics_change_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_heuristics heuristics, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: heuristics change reply received");

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

/*
 * Called when client disconnects from server.
 *
 * disconnect_reason contains one of QDEVICE_NET_DISCONNECT_REASON_
 * try_reconnect can be set to non zero value if reconnect to server should be tried
 * vote (default TLV_VOTE_NO_CHANGE) can be set to update voting timer
 *
 * Callback should return 0 on success, -1 on failure (-> force exit)
 */
int
qdevice_net_algo_test_disconnected(struct qdevice_net_instance *instance,
    enum qdevice_net_disconnect_reason disconnect_reason, int *try_reconnect, enum tlv_vote *vote)
{

	qdevice_log(LOG_INFO, "algo-test: Disconnected");

	return (0);
}

/*
 * Called when qdevice-net is going down.
 */
void
qdevice_net_algo_test_destroy(struct qdevice_net_instance *instance)
{

	qdevice_log(LOG_INFO, "algo-test: Destroy");
}

static struct qdevice_net_algorithm qdevice_net_algo_test = {
	.init					= qdevice_net_algo_test_init,
	.connected				= qdevice_net_algo_test_connected,
	.config_node_list_changed		= qdevice_net_algo_test_config_node_list_changed,
	.votequorum_node_list_notify		= qdevice_net_algo_test_votequorum_node_list_notify,
	.votequorum_node_list_heuristics_notify	= qdevice_net_algo_test_votequorum_node_list_heuristics_notify,
	.votequorum_quorum_notify		= qdevice_net_algo_test_votequorum_quorum_notify,
	.votequorum_expected_votes_notify	= qdevice_net_algo_test_votequorum_expected_votes_notify,
	.config_node_list_reply_received	= qdevice_net_algo_test_config_node_list_reply_received,
	.membership_node_list_reply_received	= qdevice_net_algo_test_membership_node_list_reply_received,
	.quorum_node_list_reply_received	= qdevice_net_algo_test_quorum_node_list_reply_received,
	.ask_for_vote_reply_received		= qdevice_net_algo_test_ask_for_vote_reply_received,
	.vote_info_received			= qdevice_net_algo_test_vote_info_received,
	.echo_reply_received			= qdevice_net_algo_test_echo_reply_received,
	.echo_reply_not_received		= qdevice_net_algo_test_echo_reply_not_received,
	.heuristics_change			= qdevice_net_algo_test_heuristics_change,
	.heuristics_change_reply_received	= qdevice_net_algo_test_heuristics_change_reply_received,
	.disconnected				= qdevice_net_algo_test_disconnected,
	.destroy				= qdevice_net_algo_test_destroy,
};

int
qdevice_net_algo_test_register(void)
{
	return (qdevice_net_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_TEST, &qdevice_net_algo_test));
}
