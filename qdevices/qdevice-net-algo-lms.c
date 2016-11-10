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

#include "qdevice-net-algo-lms.h"
#include "qdevice-log.h"
#include "qdevice-net-send.h"
#include "qdevice-net-cast-vote-timer.h"

struct algo_lms_instance_data {
	uint32_t quorate;
	uint8_t have_wfa;
	enum tlv_vote vote;
};

int
qdevice_net_algo_lms_init(struct qdevice_net_instance *instance)
{
	struct algo_lms_instance_data *data;
	int res;

	data = malloc(sizeof(struct algo_lms_instance_data));
	if (!data) {
		return (-1);
	}
	instance->algorithm_data = data;

	data->quorate = 0;
	data->vote = TLV_VOTE_ASK_LATER;
	res = cmap_get_uint8(instance->qdevice_instance_ptr->cmap_handle, "quorum.wait_for_all", &data->have_wfa);
	if (res != CS_OK) {
		qdevice_log(LOG_DEBUG, "algo-lms: Can't get WFA res = %d", res);
		data->have_wfa = 0;
	}

	qdevice_log(LOG_DEBUG, "algo-lms: initialised. WFA = %d", data->have_wfa);
	return (0);
}


int
qdevice_net_algo_lms_connected(struct qdevice_net_instance *instance,
    enum tlv_heuristics *heuristics, int *send_config_node_list, int *send_membership_node_list,
    int *send_quorum_node_list, enum tlv_vote *vote)
{

	return (0);
}

int
qdevice_net_algo_lms_config_node_list_changed(struct qdevice_net_instance *instance,
    const struct node_list *nlist, int config_version_set, uint64_t config_version,
    int *send_node_list, enum tlv_vote *vote)
{

	return (0);
}

int
qdevice_net_algo_lms_votequorum_node_list_notify(struct qdevice_net_instance *instance,
    const struct tlv_ring_id *ring_id, uint32_t node_list_entries, uint32_t node_list[],
    int *pause_cast_vote_timer, enum tlv_vote *vote)
{

	return (0);
}

int
qdevice_net_algo_lms_votequorum_node_list_heuristics_notify(struct qdevice_net_instance *instance,
    const struct tlv_ring_id *ring_id, uint32_t node_list_entries, uint32_t node_list[],
    int *send_node_list, enum tlv_vote *vote, enum tlv_heuristics *heuristics)
{

	return (0);
}

int
qdevice_net_algo_lms_votequorum_quorum_notify(struct qdevice_net_instance *instance,
    uint32_t quorate, uint32_t node_list_entries, votequorum_node_t node_list[], int *send_node_list,
    enum tlv_vote *vote)
{
	struct algo_lms_instance_data *data = instance->algorithm_data;

	data->quorate = quorate;
	qdevice_log(LOG_DEBUG, "algo-lms: quorum_notify. quorate = %d", data->quorate);

	return (0);
}

int
qdevice_net_algo_lms_config_node_list_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, int initial, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

int
qdevice_net_algo_lms_membership_node_list_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

int
qdevice_net_algo_lms_quorum_node_list_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

int
qdevice_net_algo_lms_votequorum_expected_votes_notify(struct qdevice_net_instance *instance,
    uint32_t expected_votes, enum tlv_vote *vote)
{

	return (0);
}

int
qdevice_net_algo_lms_ask_for_vote_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

int
qdevice_net_algo_lms_vote_info_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_vote *vote)
{

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

int
qdevice_net_algo_lms_echo_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, int is_expected_seq_number)
{

	return (is_expected_seq_number ? 0 : -1);
}

int
qdevice_net_algo_lms_echo_reply_not_received(struct qdevice_net_instance *instance)
{
	struct algo_lms_instance_data *data = instance->algorithm_data;

	qdevice_log(LOG_DEBUG, "algo-lms: echo_not_recvd. quorate = %d, WFA = %d", data->quorate, data->have_wfa);

	/* qnetd server is disconnected, if we were already quorate AND WFA is enabled
	   then we can continue to provide our vote.
	   Otherwise ... no
	*/
	if (data->quorate && data->have_wfa) {
		return (0);
	}

	return (-1);
}

int
qdevice_net_algo_lms_heuristics_change(struct qdevice_net_instance *instance,
    enum tlv_heuristics *heuristics, int *send_msg, enum tlv_vote *vote)
{

	return (0);
}

int
qdevice_net_algo_lms_heuristics_change_reply_received(struct qdevice_net_instance *instance,
    uint32_t seq_number, const struct tlv_ring_id *ring_id, int ring_id_is_valid,
    enum tlv_heuristics heuristics, enum tlv_vote *vote)
{

	if (!ring_id_is_valid) {
		*vote = TLV_VOTE_NO_CHANGE;
	}

	return (0);
}

int
qdevice_net_algo_lms_disconnected(struct qdevice_net_instance *instance,
    enum qdevice_net_disconnect_reason disconnect_reason, int *try_reconnect, enum tlv_vote *vote)
{
	struct algo_lms_instance_data *data = instance->algorithm_data;

	qdevice_log(LOG_DEBUG, "algo-lms: disconnected. quorate = %d, WFA = %d", data->quorate, data->have_wfa);
	qdevice_log(LOG_DEBUG, "algo-lms: disconnected. reason = %d, WFA = %d", disconnect_reason, data->have_wfa);

	if (!data->quorate || !data->have_wfa) {
		*vote = TLV_VOTE_NACK;
	}
	*try_reconnect = 1;
	return (0);
}

void
qdevice_net_algo_lms_destroy(struct qdevice_net_instance *instance)
{
	free(instance->algorithm_data);
}

static struct qdevice_net_algorithm qdevice_net_algo_lms = {
	.init					= qdevice_net_algo_lms_init,
	.connected				= qdevice_net_algo_lms_connected,
	.config_node_list_changed		= qdevice_net_algo_lms_config_node_list_changed,
	.votequorum_node_list_notify		= qdevice_net_algo_lms_votequorum_node_list_notify,
	.votequorum_node_list_heuristics_notify	= qdevice_net_algo_lms_votequorum_node_list_heuristics_notify,
	.votequorum_quorum_notify		= qdevice_net_algo_lms_votequorum_quorum_notify,
	.votequorum_expected_votes_notify	= qdevice_net_algo_lms_votequorum_expected_votes_notify,
	.config_node_list_reply_received	= qdevice_net_algo_lms_config_node_list_reply_received,
	.membership_node_list_reply_received	= qdevice_net_algo_lms_membership_node_list_reply_received,
	.quorum_node_list_reply_received	= qdevice_net_algo_lms_quorum_node_list_reply_received,
	.ask_for_vote_reply_received		= qdevice_net_algo_lms_ask_for_vote_reply_received,
	.vote_info_received			= qdevice_net_algo_lms_vote_info_received,
	.echo_reply_received			= qdevice_net_algo_lms_echo_reply_received,
	.echo_reply_not_received		= qdevice_net_algo_lms_echo_reply_not_received,
	.heuristics_change			= qdevice_net_algo_lms_heuristics_change,
	.heuristics_change_reply_received	= qdevice_net_algo_lms_heuristics_change_reply_received,
	.disconnected				= qdevice_net_algo_lms_disconnected,
	.destroy				= qdevice_net_algo_lms_destroy,
};

int
qdevice_net_algo_lms_register(void)
{
	return (qdevice_net_algorithm_register(TLV_DECISION_ALGORITHM_TYPE_LMS, &qdevice_net_algo_lms));
}
