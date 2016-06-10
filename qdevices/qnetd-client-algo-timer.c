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

#include "qnetd-log.h"
#include "qnetd-client-algo-timer.h"
#include "qnetd-client-send.h"
#include "qnetd-algorithm.h"
#include "timer-list.h"

static int
qnetd_client_algo_timer_callback(void *data1, void *data2)
{
	struct qnetd_client *client;
	enum tlv_vote result_vote;
	int send_vote;
	int reschedule_timer;
	enum tlv_reply_error_code reply_error_code;

	client = (struct qnetd_client *)data1;

	result_vote = TLV_VOTE_WAIT_FOR_REPLY;
	send_vote = 0;
	reschedule_timer = 0;

	reply_error_code = qnetd_algorithm_timer_callback(client, &reschedule_timer,
	    &send_vote, &result_vote);

	if (reply_error_code != TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qnetd_log(LOG_ERR, "Algorithm for client %s returned error code. "
		    "Sending error reply.", client->addr_str);

		if (qnetd_client_send_err(client, 0, 0, reply_error_code) != 0) {
			client->schedule_disconnect = 1;
			return (0);
		}

		return (0);
	} else {
		qnetd_log(LOG_DEBUG, "Algorithm for client %s decided to %s timer and %s vote "
		    "with value %s", client->addr_str,
		    (reschedule_timer ? "reschedule" : "not reschedule"),
		    (send_vote ? "send" : "not send"),
		    tlv_vote_to_str(result_vote));
	}

	if (send_vote) {
		client->algo_timer_vote_info_msq_seq_number++;

		if (qnetd_client_send_vote_info(client,
		    client->algo_timer_vote_info_msq_seq_number, &client->last_ring_id,
		    result_vote) != 0) {
			client->schedule_disconnect = 1;
			return (0);
		}
	}

	if (reschedule_timer) {
		/*
		 * Timer list makes sure to schedule callback again
		 */
		return (-1);
	}

	client->algo_timer = NULL;
	return (0);
}

int
qnetd_client_algo_timer_is_scheduled(struct qnetd_client *client)
{

	return (client->algo_timer != NULL);
}

int
qnetd_client_algo_timer_schedule_timeout(struct qnetd_client *client, uint32_t timeout)
{

	if (qnetd_client_algo_timer_is_scheduled(client)) {
		if (qnetd_client_algo_timer_abort(client) != 0) {
			qnetd_log(LOG_ERR, "Can't abort algo timer");

			return (-1);
		}
	}

	client->algo_timer = timer_list_add(client->main_timer_list, timeout,
	    qnetd_client_algo_timer_callback, (void *)client, NULL);
	if (client->algo_timer == NULL) {
		qnetd_log(LOG_ERR, "Can't schedule algo timer");

		return (-1);
	}

	return (0);
}

int
qnetd_client_algo_timer_schedule(struct qnetd_client *client)
{

	return (qnetd_client_algo_timer_schedule_timeout(client, client->heartbeat_interval / 4));
}

int
qnetd_client_algo_timer_abort(struct qnetd_client *client)
{

	if (qnetd_client_algo_timer_is_scheduled(client)) {
		timer_list_delete(client->main_timer_list, client->algo_timer);
		client->algo_timer = NULL;
	}

	return (0);
}
