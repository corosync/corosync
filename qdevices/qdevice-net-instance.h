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

#ifndef _QDEVICE_NET_INSTANCE_H_
#define _QDEVICE_NET_INSTANCE_H_

#include <sys/types.h>

#include <stdlib.h>
#include <stdint.h>

#include "nss-sock.h"

#include "qdevice-instance.h"

#include "dynar.h"
#include "node-list.h"
#include "pr-poll-array.h"
#include "qdevice-net-disconnect-reason.h"
#include "send-buffer-list.h"
#include "tlv.h"
#include "timer-list.h"

#ifdef __cplusplus
extern "C" {
#endif

enum qdevice_net_instance_state {
	QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT,
	QDEVICE_NET_INSTANCE_STATE_SENDING_PREINIT_REPLY,
	QDEVICE_NET_INSTANCE_STATE_WAITING_PREINIT_REPLY,
	QDEVICE_NET_INSTANCE_STATE_WAITING_STARTTLS_BEING_SENT,
	QDEVICE_NET_INSTANCE_STATE_WAITING_INIT_REPLY,
	QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS,
};

struct qdevice_net_instance {
	PRFileDesc *socket;
	struct dynar receive_buffer;
	struct send_buffer_list send_buffer_list;
	int skipping_msg;
	size_t msg_already_received_bytes;
	enum qdevice_net_instance_state state;
	uint32_t last_msg_seq_num;
	uint32_t echo_request_expected_msg_seq_num;
	uint32_t echo_reply_received_msg_seq_num;
	enum tlv_tls_supported tls_supported;
	int using_tls;
	int tls_client_cert_sent;
	uint32_t heartbeat_interval;		/* Adjusted heartbeat interval during normal operation */
	uint32_t sync_heartbeat_interval;	/* Adjusted heartbeat interval during corosync sync */
	uint32_t cast_vote_timer_interval;	/* Timer for cast vote */
	uint32_t connect_timeout;
	struct timer_list_entry *cast_vote_timer;
	enum tlv_vote cast_vote_timer_vote;
	int cast_vote_timer_paused;
	const char *host_addr;
	uint16_t host_port;
	const char *cluster_name;
	enum tlv_decision_algorithm_type decision_algorithm;
	struct timer_list main_timer_list;
	struct timer_list_entry *echo_request_timer;
	int schedule_disconnect;
	PRFileDesc *votequorum_poll_fd;
	PRFileDesc *cmap_poll_fd;
	PRFileDesc *ipc_socket_poll_fd;
	struct tlv_ring_id last_sent_ring_id;
	struct tlv_tie_breaker tie_breaker;
	void *algorithm_data;
	enum qdevice_net_disconnect_reason disconnect_reason;
	struct qdevice_instance *qdevice_instance_ptr;
	struct nss_sock_non_blocking_client non_blocking_client;
	struct timer_list_entry *connect_timer;
	int force_ip_version;
	struct pr_poll_array poll_array;
	time_t last_echo_reply_received_time;
	time_t connected_since_time;
	const struct qdevice_advanced_settings *advanced_settings;
	PRFileDesc *heuristics_pipe_cmd_send_poll_fd;
	PRFileDesc *heuristics_pipe_cmd_recv_poll_fd;
	PRFileDesc *heuristics_pipe_log_recv_poll_fd;
	struct timer_list_entry *regular_heuristics_timer;
	int server_supports_heuristics;
	enum tlv_heuristics latest_regular_heuristics_result;
	enum tlv_heuristics latest_connect_heuristics_result;
	enum tlv_heuristics latest_vq_heuristics_result;
	enum tlv_heuristics latest_heuristics_result;
};

extern int		qdevice_net_instance_init(struct qdevice_net_instance *instance,
    enum tlv_tls_supported tls_supported,
    enum tlv_decision_algorithm_type decision_algorithm, uint32_t heartbeat_interval,
    uint32_t sync_heartbeat_interval, uint32_t cast_vote_timer_interval,
    const char *host_addr, uint16_t host_port, const char *cluster_name,
    const struct tlv_tie_breaker *tie_breaker, uint32_t connect_timeout, int force_ip_version,
    int cmap_fd, int votequorum_fd, int local_socket_fd,
    const struct qdevice_advanced_settings *advanced_settings,
    int heuristics_pipe_cmd_send_fd, int heuristics_pipe_cmd_recv_fd,
    int heuristics_pipe_log_recv_fd);

extern void		qdevice_net_instance_clean(struct qdevice_net_instance *instance);

extern int		qdevice_net_instance_destroy(struct qdevice_net_instance *instance);

extern int		qdevice_net_instance_init_from_cmap(struct qdevice_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_NET_INSTANCE_H_ */
