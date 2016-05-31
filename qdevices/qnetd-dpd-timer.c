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

#include "qnetd-dpd-timer.h"
#include "qnetd-log.h"

static int
qnetd_dpd_timer_cb(void *data1, void *data2)
{
	struct qnetd_instance *instance;
	struct qnetd_client *client;

	instance = (struct qnetd_instance *)data1;

	TAILQ_FOREACH(client, &instance->clients, entries) {
		if (!client->init_received) {
			continue;
		}

		client->dpd_time_since_last_check += instance->advanced_settings->dpd_interval;

		if (client->dpd_time_since_last_check > (client->heartbeat_interval * 2)) {
			if (!client->dpd_msg_received_since_last_check) {
				qnetd_log(LOG_WARNING, "Client %s doesn't sent any message during "
				    "%"PRIu32"ms. Disconnecting",
				    client->addr_str, client->dpd_time_since_last_check);

				client->schedule_disconnect = 1;
			} else {
				client->dpd_time_since_last_check = 0;
				client->dpd_msg_received_since_last_check = 0;
			}
		}
	}

	return (-1);
}

int
qnetd_dpd_timer_init(struct qnetd_instance *instance)
{

	if (!instance->advanced_settings->dpd_enabled) {
		return (0);
	}

	instance->dpd_timer = timer_list_add(&instance->main_timer_list,
	    instance->advanced_settings->dpd_interval,
	    qnetd_dpd_timer_cb, (void *)instance, NULL);
	if (instance->dpd_timer == NULL) {
		qnetd_log(LOG_ERR, "Can't initialize dpd timer");

		return (-1);
	}

	return (0);
}

void
qnetd_dpd_timer_destroy(struct qnetd_instance *instance)
{

	if (instance->dpd_timer != NULL) {
		timer_list_delete(&instance->main_timer_list, instance->dpd_timer);
		instance->dpd_timer = NULL;
	}
}
