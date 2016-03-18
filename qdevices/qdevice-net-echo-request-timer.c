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

#include "qdevice-net-algorithm.h"
#include "qdevice-net-echo-request-timer.h"
#include "qdevice-net-send.h"
#include "qdevice-log.h"

static int
qdevice_net_echo_request_timer_callback(void *data1, void *data2)
{
	struct qdevice_net_instance *instance;

	instance = (struct qdevice_net_instance *)data1;

	if (instance->echo_reply_received_msg_seq_num !=
	    instance->echo_request_expected_msg_seq_num) {
		qdevice_log(LOG_ERR, "Server didn't send echo reply message on time");

		if (qdevice_net_algorithm_echo_reply_not_received(instance) != 0) {
			qdevice_log(LOG_DEBUG, "Algorithm decided to disconnect");

			instance->schedule_disconnect = 1;
			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_ALGO_ECHO_REPLY_NOT_RECEIVED_ERR;

			instance->echo_request_timer = NULL;
			return (0);
		} else {
			qdevice_log(LOG_DEBUG, "Algorithm decided to continue send heartbeat");

			return (-1);
		}
	}

	if (qdevice_net_send_echo_request(instance) == -1) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;

		instance->schedule_disconnect = 1;
		instance->echo_request_timer = NULL;
		return (0);
	}

	/*
	 * Schedule this function callback again
	 */
	return (-1);
}

int
qdevice_net_echo_request_timer_schedule(struct qdevice_net_instance *instance)
{
	instance->echo_request_expected_msg_seq_num = 0;
	instance->echo_reply_received_msg_seq_num = 0;

	if (instance->echo_request_timer != NULL) {
		timer_list_delete(&instance->main_timer_list, instance->echo_request_timer);
		instance->echo_request_timer = NULL;
	}

	qdevice_log(LOG_DEBUG, "Scheduling send of heartbeat every %"PRIu32"ms", instance->heartbeat_interval);
	instance->echo_request_timer = timer_list_add(&instance->main_timer_list,
	    instance->heartbeat_interval, qdevice_net_echo_request_timer_callback,
	    (void *)instance, NULL);

	if (instance->echo_request_timer == NULL) {
		qdevice_log(LOG_ERR, "Can't schedule regular sending of heartbeat.");

		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_HB_TIMER;

		return (-1);
	}

	return (0);
}
