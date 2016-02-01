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

#include "qdevice-net-instance.h"

int
qdevice_net_instance_init(struct qdevice_net_instance *instance, size_t initial_receive_size,
    size_t initial_send_size, size_t min_send_size, size_t max_send_buffers,
    size_t max_receive_size,
    enum tlv_tls_supported tls_supported, uint32_t node_id,
    enum tlv_decision_algorithm_type decision_algorithm, uint32_t heartbeat_interval,
    uint32_t sync_heartbeat_interval, uint32_t cast_vote_timer_interval,
    const char *host_addr, uint16_t host_port, const char *cluster_name,
    const struct tlv_tie_breaker *tie_breaker, uint32_t connect_timeout)
{

	memset(instance, 0, sizeof(*instance));

	instance->initial_receive_size = initial_receive_size;
	instance->initial_send_size = initial_send_size;
	instance->min_send_size = min_send_size;
	instance->max_receive_size = max_receive_size;
	instance->node_id = node_id;
	instance->decision_algorithm = decision_algorithm;
	instance->heartbeat_interval = heartbeat_interval;
	instance->sync_heartbeat_interval = sync_heartbeat_interval;
	instance->cast_vote_timer_interval = cast_vote_timer_interval;
	instance->cast_vote_timer = NULL;
	instance->host_addr = host_addr;
	instance->host_port = host_port;
	instance->cluster_name = cluster_name;
	instance->connect_timeout = connect_timeout;
	instance->last_msg_seq_num = 1;
	instance->echo_request_expected_msg_seq_num = 1;
	instance->echo_reply_received_msg_seq_num = 1;
	memcpy(&instance->tie_breaker, tie_breaker, sizeof(*tie_breaker));

	dynar_init(&instance->receive_buffer, initial_receive_size);

	send_buffer_list_init(&instance->send_buffer_list, max_send_buffers,
	    initial_send_size);

	timer_list_init(&instance->main_timer_list);

	node_list_init(&instance->last_sent_config_node_list);

	instance->tls_supported = tls_supported;

	return (0);
}

void
qdevice_net_instance_clean(struct qdevice_net_instance *instance)
{

	dynar_clean(&instance->receive_buffer);

	send_buffer_list_free(&instance->send_buffer_list);

	instance->skipping_msg = 0;
	instance->msg_already_received_bytes = 0;
	instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_PREINIT_REPLY;
	instance->echo_request_expected_msg_seq_num = instance->echo_reply_received_msg_seq_num;
	instance->using_tls = 0;

	node_list_free(&instance->last_sent_config_node_list);

	timer_list_free(&instance->main_timer_list);
	instance->cast_vote_timer = NULL;
	instance->echo_request_timer = NULL;

	instance->schedule_disconnect = 0;
	instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNDEFINED;
	instance->cmap_reload_in_progress = 0;
}

int
qdevice_net_instance_destroy(struct qdevice_net_instance *instance)
{

	dynar_destroy(&instance->receive_buffer);

	send_buffer_list_free(&instance->send_buffer_list);

	node_list_free(&instance->last_sent_config_node_list);

	timer_list_free(&instance->main_timer_list);

	free((void *)instance->cluster_name);
	free((void *)instance->host_addr);

	return (0);
}
