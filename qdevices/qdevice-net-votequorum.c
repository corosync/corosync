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

#include <err.h>
#include <poll.h>

/*
 * Needed for creating nspr handle from unix fd
 */
#include <private/pprio.h>

#include "qnet-config.h"
#include "qdevice-net-votequorum.h"
#include "qdevice-net-send.h"
#include "qdevice-net-log.h"
#include "qdevice-net-cast-vote-timer.h"
#include "qdevice-net-algorithm.h"
#include "nss-sock.h"

enum tlv_node_state
qdevice_net_votequorum_node_state_to_tlv(uint32_t votequorum_node_state)
{
	enum tlv_node_state res;

	switch (votequorum_node_state) {
	case VOTEQUORUM_NODESTATE_MEMBER: res = TLV_NODE_STATE_MEMBER; break;
	case VOTEQUORUM_NODESTATE_DEAD: res = TLV_NODE_STATE_DEAD; break;
	case VOTEQUORUM_NODESTATE_LEAVING: res = TLV_NODE_STATE_LEAVING; break;
	default:
		errx(1, "qdevice_net_convert_votequorum_to_tlv_node_state: Unhandled votequorum "
		    "node state %"PRIu32, votequorum_node_state);
		break;
	}

	return (res);
}

void
qdevice_net_votequorum_ring_id_to_tlv(struct tlv_ring_id *tlv_rid,
    const votequorum_ring_id_t *votequorum_rid)
{

	tlv_rid->node_id = votequorum_rid->nodeid;
	tlv_rid->seq = votequorum_rid->seq;
}

static void
qdevice_net_votequorum_quorum_notify_callback(votequorum_handle_t votequorum_handle,
    uint64_t context, uint32_t quorate,
    uint32_t node_list_entries, votequorum_node_t node_list[])
{
	struct qdevice_net_instance *instance;
	uint32_t u32;
	int algo_send_list;

	if (votequorum_context_get(votequorum_handle, (void **)&instance) != CS_OK) {
		errx(1, "Fatal error. Can't get votequorum context");
	}

	qdevice_net_log(LOG_DEBUG, "Votequorum quorum notify callback:");
	qdevice_net_log(LOG_DEBUG, "  Quorate = %u", quorate);

	qdevice_net_log(LOG_DEBUG, "  Node list (size = %"PRIu32"):", node_list_entries);
	for (u32 = 0; u32 < node_list_entries; u32++) {
		qdevice_net_log(LOG_DEBUG, "    %"PRIu32" nodeid = %"PRIx32", state = %"PRIu32,
		    u32, node_list[u32].nodeid, node_list[u32].state);
	}

	algo_send_list = 1;
	if (qdevice_net_algorithm_votequorum_quorum_notify(instance, quorate,
	    node_list_entries, node_list, &algo_send_list) != 0) {
		qdevice_net_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_VOTEQUORUM_QUORUM_NOTIFY_ERR;
		instance->schedule_disconnect = 1;
	} else {
		qdevice_net_log(LOG_DEBUG, "Algorithm decided to %s list",
		    (algo_send_list ? "send" : "not send"));
	}

	if (algo_send_list) {
		if (qdevice_net_send_quorum_node_list(instance,
		    (quorate ? TLV_QUORATE_QUORATE : TLV_QUORATE_INQUORATE),
		    node_list_entries, node_list) != 0) {
			/*
			 * Fatal error -> schedule disconnect
			 */
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			instance->schedule_disconnect = 1;
		}
	}
}

static void
qdevice_net_votequorum_node_list_notify_callback(votequorum_handle_t votequorum_handle,
    uint64_t context, votequorum_ring_id_t votequorum_ring_id,
    uint32_t node_list_entries, uint32_t node_list[])
{
	struct qdevice_net_instance *instance;
	struct tlv_ring_id tlv_rid;
	uint32_t u32;
	enum tlv_vote result_vote;

	if (votequorum_context_get(votequorum_handle, (void **)&instance) != CS_OK) {
		qdevice_net_log(LOG_ERR, "Fatal error. Can't get votequorum context");
		exit(1);
	}

	qdevice_net_log(LOG_DEBUG, "Votequorum nodelist notify callback:");
	qdevice_net_log(LOG_DEBUG, "  Ring_id = (%"PRIx32".%"PRIx64")",
	    votequorum_ring_id.nodeid, votequorum_ring_id.seq);

	qdevice_net_log(LOG_DEBUG, "  Node list (size = %"PRIu32"):", node_list_entries);
	for (u32 = 0; u32 < node_list_entries; u32++) {
		qdevice_net_log(LOG_DEBUG, "    %"PRIu32" nodeid = %"PRIx32,
		    u32, node_list[u32]);
	}

	qdevice_net_votequorum_ring_id_to_tlv(&tlv_rid, &votequorum_ring_id);

	result_vote = TLV_VOTE_WAIT_FOR_REPLY;
	if (qdevice_net_algorithm_votequorum_node_list_notify(instance, &tlv_rid, node_list_entries,
	    node_list, &result_vote) != 0) {
		qdevice_net_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_VOTEQUORUM_NODE_LIST_NOTIFY_ERR;
		instance->schedule_disconnect = 1;
	} else {
		qdevice_net_log(LOG_DEBUG, "Algorithm result vote is %s", tlv_vote_to_str(result_vote));
	}

	if (result_vote == TLV_VOTE_WAIT_FOR_REPLY || result_vote == TLV_VOTE_ASK_LATER) {
		if (qdevice_net_send_membership_node_list(instance, &tlv_rid, node_list_entries, node_list) != 0) {
			/*
			 * Fatal error -> schedule disconnect
			 */
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			instance->schedule_disconnect = 1;
		}
	}

	memcpy(&instance->last_received_votequorum_ring_id, &votequorum_ring_id,
	    sizeof(votequorum_ring_id));

	if (result_vote != TLV_VOTE_NO_CHANGE && result_vote != TLV_VOTE_ASK_LATER) {
		if (qdevice_net_cast_vote_timer_update(instance, result_vote) != 0) {
			qdevice_net_log(LOG_CRIT, "qdevice_net_votequorum_node_list_notify_callback fatal error "
			    "Can't update cast vote timer");
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
			instance->schedule_disconnect = 1;
		}
	}
}

void
qdevice_net_votequorum_init(struct qdevice_net_instance *instance)
{
	votequorum_callbacks_t votequorum_callbacks;
	votequorum_handle_t votequorum_handle;
	cs_error_t res;
	int no_retries;
	int fd;

	memset(&votequorum_callbacks, 0, sizeof(votequorum_callbacks));

	votequorum_callbacks.votequorum_quorum_notify_fn =
	    qdevice_net_votequorum_quorum_notify_callback;

	votequorum_callbacks.votequorum_nodelist_notify_fn =
	    qdevice_net_votequorum_node_list_notify_callback;

	no_retries = 0;

	while ((res = votequorum_initialize(&votequorum_handle,
	    &votequorum_callbacks)) == CS_ERR_TRY_AGAIN &&
	    no_retries++ < QDEVICE_NET_MAX_CS_TRY_AGAIN) {
		poll(NULL, 0, 1000);
	}

        if (res != CS_OK) {
		errx(1, "Failed to initialize the votequorum API. Error %s", cs_strerror(res));
	}

	if ((res = votequorum_qdevice_register(votequorum_handle,
	    QDEVICE_NET_VOTEQUORUM_DEVICE_NAME)) != CS_OK) {
		errx(1, "Can't register votequorum device. Error %s", cs_strerror(res));
	}

	if ((res = votequorum_context_set(votequorum_handle, (void *)instance)) != CS_OK) {
		errx(1, "Can't set votequorum context. Error %s", cs_strerror(res));
	}

	instance->votequorum_handle = votequorum_handle;

	votequorum_fd_get(votequorum_handle, &fd);
	if ((instance->votequorum_poll_fd = PR_CreateSocketPollFd(fd)) == NULL) {
		qdevice_net_log_nss(LOG_CRIT, "Can't create NSPR votequorum poll fd");
		exit(1);
	}
}

void
qdevice_net_votequorum_destroy(struct qdevice_net_instance *instance)
{
	cs_error_t res;

	res = votequorum_qdevice_unregister(instance->votequorum_handle,
		QDEVICE_NET_VOTEQUORUM_DEVICE_NAME);

        if (res != CS_OK) {
                qdevice_net_log(LOG_WARNING, "Unable to unregister votequorum device. Error %s", cs_strerror(res));
	}

	res = votequorum_finalize(instance->votequorum_handle);
	if (res != CS_OK) {
		qdevice_net_log(LOG_WARNING, "Unable to finalize votequorum. Error %s", cs_strerror(res));
	}

	if (PR_DestroySocketPollFd(instance->votequorum_poll_fd) != PR_SUCCESS) {
		qdevice_net_log_nss(LOG_WARNING, "Unable to close votequorum connection fd");
	}
}
