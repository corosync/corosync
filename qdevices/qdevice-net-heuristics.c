/*
 * Copyright (c) 2017 Red Hat, Inc.
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
#include "qdevice-net-send.h"
#include "qdevice-net-votequorum.h"

enum tlv_heuristics
qdevice_net_heuristics_exec_result_to_tlv(enum qdevice_heuristics_exec_result exec_result)
{
	enum tlv_heuristics res;

	switch (exec_result) {
	case QDEVICE_HEURISTICS_EXEC_RESULT_DISABLED: res = TLV_HEURISTICS_UNDEFINED; break;
	case QDEVICE_HEURISTICS_EXEC_RESULT_PASS: res = TLV_HEURISTICS_PASS; break;
	case QDEVICE_HEURISTICS_EXEC_RESULT_FAIL: res = TLV_HEURISTICS_FAIL; break;
	default:
		qdevice_log(LOG_ERR, "qdevice_net_heuristics_exec_result_to_tlv: Unhandled "
		    "heuristics exec result %s",
		    qdevice_heuristics_exec_result_to_str(exec_result));
		exit(1);
		break;
	}

	return (res);
}

static int
qdevice_net_regular_heuristics_exec_result_callback(void *heuristics_instance_ptr,
    uint32_t seq_number, enum qdevice_heuristics_exec_result exec_result)
{
	struct qdevice_heuristics_instance *heuristics_instance;
	struct qdevice_instance *instance;
	struct qdevice_net_instance *net_instance;
	int send_msg;
	enum tlv_vote vote;
	enum tlv_heuristics heuristics;

	heuristics_instance = (struct qdevice_heuristics_instance *)heuristics_instance_ptr;
	instance = heuristics_instance->qdevice_instance_ptr;
	net_instance = instance->model_data;

	if (qdevice_heuristics_result_notifier_list_set_active(&heuristics_instance->exec_result_notifier_list,
	    qdevice_net_regular_heuristics_exec_result_callback, 0) != 0) {
		qdevice_log(LOG_ERR, "Can't deactivate net regular heuristics exec callback notifier");

		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ACTIVATE_HEURISTICS_RESULT_NOTIFIER;
		net_instance->schedule_disconnect = 1;

		return (0);
	}

	heuristics = qdevice_net_heuristics_exec_result_to_tlv(exec_result);

	if (exec_result == QDEVICE_HEURISTICS_EXEC_RESULT_DISABLED) {
		/*
		 * Can happen when user disables heuristics during runtime
		 */
		return (0);
	}

	if (net_instance->latest_heuristics_result != heuristics) {
		qdevice_log(LOG_ERR, "Heuristics result changed from %s to %s",
		    tlv_heuristics_to_str(net_instance->latest_heuristics_result),
		    tlv_heuristics_to_str(heuristics));

		if (net_instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
			/*
			 * Not connected to qnetd
			 */
			send_msg = 0;
		} else {
			send_msg = 1;
		}

		vote = TLV_VOTE_NO_CHANGE;

		if (qdevice_net_algorithm_heuristics_change(net_instance, &heuristics, &send_msg,
		    &vote) == -1) {
			qdevice_log(LOG_ERR, "Algorithm returned error. Disconnecting.");

			net_instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_ALGO_HEURISTICS_CHANGE_ERR;
			net_instance->schedule_disconnect = 1;

			return (0);
		} else {
			qdevice_log(LOG_DEBUG, "Algorithm decided to %s message with heuristics result "
			    "%s and result vote is %s", (send_msg ? "send" : "not send"),
			    tlv_heuristics_to_str(heuristics), tlv_vote_to_str(vote));
		}

		if (send_msg) {
			if (heuristics == TLV_HEURISTICS_UNDEFINED) {
				qdevice_log(LOG_ERR, "Inconsistent algorithm result. "
				    "It's not possible to send message with undefined heuristics. "
				    "Disconnecting.");

				net_instance->disconnect_reason =
				    QDEVICE_NET_DISCONNECT_REASON_ALGO_HEURISTICS_CHANGE_ERR;
				net_instance->schedule_disconnect = 1;

				return (0);
			}

			if (!net_instance->server_supports_heuristics) {
				qdevice_log(LOG_ERR, "Server doesn't support heuristics. "
				    "Disconnecting.");

				net_instance->disconnect_reason =
				    QDEVICE_NET_DISCONNECT_REASON_SERVER_DOESNT_SUPPORT_REQUIRED_OPT;
				net_instance->schedule_disconnect = 1;

				return (0);
			}

			if (qdevice_net_send_heuristics_change(net_instance, heuristics) != 0) {
				net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
				net_instance->schedule_disconnect = 1;

				return (0);
			}
		}

		if (qdevice_net_cast_vote_timer_update(net_instance, vote) != 0) {
			qdevice_log(LOG_CRIT, "qdevice_net_heuristics_exec_result_callback "
			    "Can't update cast vote timer");

			net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
			net_instance->schedule_disconnect = 1;

			return (0);
		}
	}

	net_instance->latest_regular_heuristics_result = heuristics;
	net_instance->latest_heuristics_result = heuristics;

	if (qdevice_net_heuristics_schedule_timer(net_instance) != 0) {
		return (0);
	}

	return (0);
}

static int
qdevice_net_connect_heuristics_exec_result_callback(void *heuristics_instance_ptr,
    uint32_t seq_number, enum qdevice_heuristics_exec_result exec_result)
{
	struct qdevice_heuristics_instance *heuristics_instance;
	struct qdevice_instance *instance;
	struct qdevice_net_instance *net_instance;
	enum tlv_vote vote;
	enum tlv_heuristics heuristics;
	int send_config_node_list;
	int send_membership_node_list;
	int send_quorum_node_list;
	struct tlv_ring_id tlv_rid;
	enum tlv_quorate quorate;

	heuristics_instance = (struct qdevice_heuristics_instance *)heuristics_instance_ptr;
	instance = heuristics_instance->qdevice_instance_ptr;
	net_instance = instance->model_data;


	if (qdevice_heuristics_result_notifier_list_set_active(&heuristics_instance->exec_result_notifier_list,
	    qdevice_net_connect_heuristics_exec_result_callback, 0) != 0) {
		qdevice_log(LOG_ERR, "Can't deactivate net connect heuristics exec callback notifier");

		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ACTIVATE_HEURISTICS_RESULT_NOTIFIER;
		net_instance->schedule_disconnect = 1;

		return (0);
	}

	heuristics = qdevice_net_heuristics_exec_result_to_tlv(exec_result);

	send_config_node_list = 1;
	send_membership_node_list = 1;
	send_quorum_node_list = 1;
	vote = TLV_VOTE_WAIT_FOR_REPLY;

	if (qdevice_net_algorithm_connected(net_instance, &heuristics, &send_config_node_list,
	    &send_membership_node_list, &send_quorum_node_list, &vote) != 0) {
		qdevice_log(LOG_DEBUG, "Algorithm returned error. Disconnecting.");
		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_ALGO_CONNECTED_ERR;
		return (0);
	} else {
		qdevice_log(LOG_DEBUG, "Algorithm decided to %s config node list, %s membership "
		    "node list, %s quorum node list, heuristics is %s and result vote is %s",
		    (send_config_node_list ? "send" : "not send"),
		    (send_membership_node_list ? "send" : "not send"),
		    (send_quorum_node_list ? "send" : "not send"),
		    tlv_heuristics_to_str(heuristics),
		    tlv_vote_to_str(vote));
	}

	/*
	 * Now we can finally really send node list, votequorum node list and update timer
	 */
	if (send_config_node_list) {
		if (qdevice_net_send_config_node_list(net_instance,
		    &instance->config_node_list,
		    instance->config_node_list_version_set,
		    instance->config_node_list_version, 1) != 0) {
			net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			return (0);
		}
	}

	if (send_membership_node_list) {
		qdevice_net_votequorum_ring_id_to_tlv(&tlv_rid,
		    &instance->vq_node_list_ring_id);

		if (qdevice_net_send_membership_node_list(net_instance, &tlv_rid,
		    instance->vq_node_list_entries,
		    instance->vq_node_list,
		    heuristics) != 0) {
			net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			return (0);
		}
	}

	if (send_quorum_node_list) {
		quorate = (instance->vq_quorum_quorate ?
		    TLV_QUORATE_QUORATE : TLV_QUORATE_INQUORATE);

		if (qdevice_net_send_quorum_node_list(net_instance,
		    quorate,
		    instance->vq_quorum_node_list_entries,
		    instance->vq_quorum_node_list) != 0) {
			net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			return (0);
		}
	}

	if (qdevice_net_cast_vote_timer_update(net_instance, vote) != 0) {
		qdevice_log(LOG_CRIT, "qdevice_net_msg_received_set_option_reply fatal error. "
		    " Can't update cast vote timer vote");
		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
	}

	net_instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS;
	net_instance->connected_since_time = time(NULL);

	net_instance->latest_connect_heuristics_result = heuristics;
	net_instance->latest_heuristics_result = heuristics;

	return (0);
}

static int
qdevice_net_heuristics_timer_callback(void *data1, void *data2)
{
	struct qdevice_net_instance *net_instance;
	struct qdevice_heuristics_instance *heuristics_instance;

	net_instance = (struct qdevice_net_instance *)data1;
	heuristics_instance = &net_instance->qdevice_instance_ptr->heuristics_instance;

	if (qdevice_heuristics_waiting_for_result(heuristics_instance)) {
		qdevice_log(LOG_DEBUG, "Not executing regular heuristics because other heuristics is already running.");

		return (1);
	}

	net_instance->regular_heuristics_timer = NULL;

	qdevice_log(LOG_DEBUG, "Executing regular heuristics.");

	if (qdevice_heuristics_result_notifier_list_set_active(&heuristics_instance->exec_result_notifier_list,
	    qdevice_net_regular_heuristics_exec_result_callback, 1) != 0) {
		qdevice_log(LOG_ERR, "Can't activate net regular heuristics exec callback notifier");

		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ACTIVATE_HEURISTICS_RESULT_NOTIFIER;
		net_instance->schedule_disconnect = 1;

		return (0);
	}

	if (qdevice_heuristics_exec(heuristics_instance,
	    net_instance->qdevice_instance_ptr->sync_in_progress) != 0) {
		qdevice_log(LOG_ERR, "Can't execute regular heuristics.");

		net_instance->schedule_disconnect = 1;
		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_START_HEURISTICS;

		return (0);
	}

	/*
	 * Do not schedule this callback again. It's going to be scheduled in the
	 * qdevice_net_heuristics_exec_result_callback
	 */
	return (0);
}

int
qdevice_net_heuristics_stop_timer(struct qdevice_net_instance *net_instance)
{
	struct qdevice_instance *instance;
	struct qdevice_heuristics_instance *heuristics_instance;

	instance = net_instance->qdevice_instance_ptr;
	heuristics_instance = &instance->heuristics_instance;

	if (net_instance->regular_heuristics_timer != NULL) {
		qdevice_log(LOG_DEBUG, "Regular heuristics timer stopped");

		timer_list_delete(&net_instance->main_timer_list, net_instance->regular_heuristics_timer);
		net_instance->regular_heuristics_timer = NULL;

		if (qdevice_heuristics_result_notifier_list_set_active(&heuristics_instance->exec_result_notifier_list,
		    qdevice_net_regular_heuristics_exec_result_callback, 0) != 0) {
			qdevice_log(LOG_ERR, "Can't deactivate net regular heuristics exec callback notifier");

			net_instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_CANT_ACTIVATE_HEURISTICS_RESULT_NOTIFIER;
			net_instance->schedule_disconnect = 1;
			return (-1);
		}
	}

	return (0);
}

int
qdevice_net_heuristics_schedule_timer(struct qdevice_net_instance *net_instance)
{
	uint32_t interval;
	struct qdevice_instance *instance;
	struct qdevice_heuristics_instance *heuristics_instance;

	instance = net_instance->qdevice_instance_ptr;
	heuristics_instance = &instance->heuristics_instance;

        if (heuristics_instance->mode != QDEVICE_HEURISTICS_MODE_ENABLED) {
		qdevice_log(LOG_DEBUG, "Not scheduling heuristics timer because mode is not enabled");

		if (qdevice_net_heuristics_stop_timer(net_instance) != 0) {
			return (-1);
		}

		return (0);
        }

	if (net_instance->regular_heuristics_timer != NULL) {
		qdevice_log(LOG_DEBUG, "Not scheduling heuristics timer because it is already scheduled");

		return (0);
	}

	interval = heuristics_instance->interval;

	qdevice_log(LOG_DEBUG, "Scheduling next regular heuristics in %"PRIu32"ms", interval);

	net_instance->regular_heuristics_timer = timer_list_add(&net_instance->main_timer_list,
		interval,
		qdevice_net_heuristics_timer_callback,
	        (void *)net_instance, NULL);

	if (net_instance->regular_heuristics_timer == NULL) {
		qdevice_log(LOG_ERR, "Can't schedule regular heuristics.");

		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_HEURISTICS_TIMER;
		net_instance->schedule_disconnect = 1;
		return (-1);
	}

	return (0);
}

int
qdevice_net_heuristics_init(struct qdevice_net_instance *net_instance)
{

	if (qdevice_heuristics_result_notifier_list_add(
	    &net_instance->qdevice_instance_ptr->heuristics_instance.exec_result_notifier_list,
	    qdevice_net_regular_heuristics_exec_result_callback) == NULL) {
		qdevice_log(LOG_ERR, "Can't add net regular heuristics exec callback into notifier");

		return (-1);
	}

	if (qdevice_heuristics_result_notifier_list_add(
	    &net_instance->qdevice_instance_ptr->heuristics_instance.exec_result_notifier_list,
	    qdevice_net_connect_heuristics_exec_result_callback) == NULL) {
		qdevice_log(LOG_ERR, "Can't add net connect heuristics exec callback into notifier");

		return (-1);
	}

	return (0);
}

int
qdevice_net_heuristics_exec_after_connect(struct qdevice_net_instance *net_instance)
{
	struct qdevice_instance *instance;
	struct qdevice_heuristics_instance *heuristics_instance;

	instance = net_instance->qdevice_instance_ptr;
	heuristics_instance = &instance->heuristics_instance;

	qdevice_log(LOG_DEBUG, "Executing after-connect heuristics.");

	if (qdevice_heuristics_result_notifier_list_set_active(&heuristics_instance->exec_result_notifier_list,
	    qdevice_net_connect_heuristics_exec_result_callback, 1) != 0) {
		qdevice_log(LOG_ERR, "Can't activate net connect heuristics exec callback notifier");

		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ACTIVATE_HEURISTICS_RESULT_NOTIFIER;
		net_instance->schedule_disconnect = 1;

		return (-1);
	}

	if (qdevice_heuristics_exec(heuristics_instance,
	    instance->sync_in_progress) != 0) {
		qdevice_log(LOG_ERR, "Can't execute connect heuristics.");

		net_instance->schedule_disconnect = 1;
		net_instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_START_HEURISTICS;

		return (-1);
	}

	return (0);
}
