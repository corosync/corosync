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

#include "qnet-config.h"
#include "qdevice-log.h"
#include "qdevice-net-cast-vote-timer.h"
#include "qdevice-votequorum.h"

static int
qdevice_net_cast_vote_timer_callback(void *data1, void *data2)
{
	struct qdevice_net_instance *instance;
	int cast_vote;
	int case_processed;

	instance = (struct qdevice_net_instance *)data1;

	if (instance->cast_vote_timer_paused) {
		return (-1);
	}

	case_processed = 0;

	switch (instance->cast_vote_timer_vote) {
	case TLV_VOTE_ACK:
		case_processed = 1;
		cast_vote = 1;
		break;
	case TLV_VOTE_NACK:
		case_processed = 1;
		cast_vote = 0;
		break;
	case TLV_VOTE_ASK_LATER:
	case TLV_VOTE_WAIT_FOR_REPLY:
	case TLV_VOTE_NO_CHANGE:
	case TLV_VOTE_UNDEFINED:
		/*
		 * Shouldn't happen
		 */
		break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when
	 * new tlv_vote is added.
	 */
	}

	if (!case_processed) {
		qdevice_log(LOG_CRIT, "qdevice_net_timer_cast_vote: Unhandled cast_vote_timer_vote %u\n",
		    instance->cast_vote_timer_vote);
		exit(1);
	}

	if (qdevice_votequorum_poll(instance->qdevice_instance_ptr, cast_vote) != 0) {
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SCHEDULE_VOTING_TIMER;
		instance->schedule_disconnect = 1;
		instance->cast_vote_timer = NULL;
		return (0);
	}

	/*
	 * Schedule this function callback again
	 */
	return (-1);
}

int
qdevice_net_cast_vote_timer_update(struct qdevice_net_instance *instance, enum tlv_vote vote)
{
	int timer_needs_running;
	int case_processed;

	case_processed = 0;

	switch (vote) {
	case TLV_VOTE_UNDEFINED:
		break;
	case TLV_VOTE_ACK:
	case TLV_VOTE_NACK:
		case_processed = 1;
		timer_needs_running = 1;
		break;
	case TLV_VOTE_WAIT_FOR_REPLY:
	case TLV_VOTE_ASK_LATER:
		case_processed = 1;
		timer_needs_running = 0;
		break;
	case TLV_VOTE_NO_CHANGE:
		case_processed = 1;
		return (0);

		break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when
	 * new tlv_vote is added.
	 */
	}

	if (!case_processed) {
		qdevice_log(LOG_CRIT, "qdevice_net_cast_vote_timer_update_vote: Unhandled vote parameter %u\n",
		    vote);
		exit(1);
	}

	instance->cast_vote_timer_vote = vote;

	if (timer_needs_running) {
		if (instance->cast_vote_timer == NULL) {
			instance->cast_vote_timer = timer_list_add(&instance->main_timer_list,
			    instance->cast_vote_timer_interval,
			    qdevice_net_cast_vote_timer_callback, (void *)instance, NULL);

			if (instance->cast_vote_timer == NULL) {
				qdevice_log(LOG_ERR, "Can't schedule sending of "
				    "votequorum poll");

				return (-1);
			} else {
				qdevice_log(LOG_DEBUG, "Cast vote timer is now scheduled every "
				    "%"PRIu32"ms voting %s.", instance->cast_vote_timer_interval,
				    tlv_vote_to_str(instance->cast_vote_timer_vote));
			}
		} else {
			qdevice_log(LOG_DEBUG, "Cast vote timer remains scheduled every "
			    "%"PRIu32"ms voting %s.", instance->cast_vote_timer_interval,
			    tlv_vote_to_str(instance->cast_vote_timer_vote));
		}

		if (qdevice_net_cast_vote_timer_callback((void *)instance, NULL) != -1) {
			return (-1);
		}
	} else {
		if (instance->cast_vote_timer != NULL) {
			timer_list_delete(&instance->main_timer_list, instance->cast_vote_timer);
			instance->cast_vote_timer = NULL;
			qdevice_log(LOG_DEBUG, "Cast vote timer is now stopped.");
		} else {
			qdevice_log(LOG_DEBUG, "Cast vote timer remains stopped.");
		}
	}

	return (0);
}

void
qdevice_net_cast_vote_timer_set_paused(struct qdevice_net_instance *instance, int paused)
{

	if (paused != instance->cast_vote_timer_paused) {
		instance->cast_vote_timer_paused = paused;

		if (instance->cast_vote_timer_paused) {
			qdevice_log(LOG_DEBUG, "Cast vote timer is now paused.");
		} else {
			qdevice_log(LOG_DEBUG, "Cast vote timer is no longer paused.");
		}
	}
}
