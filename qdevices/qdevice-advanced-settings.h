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

#ifndef _QDEVICE_ADVANCED_SETTINGS_H_
#define _QDEVICE_ADVANCED_SETTINGS_H_

#ifdef __cplusplus
extern "C" {
#endif

enum qdevice_advanced_settings_master_wins {
	QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_MODEL,
	QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_FORCE_ON,
	QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_FORCE_OFF,
};

struct qdevice_advanced_settings {
	char *lock_file;
	char *local_socket_file;
	int local_socket_backlog;
	int max_cs_try_again;
	char *votequorum_device_name;
	size_t ipc_max_clients;
	size_t ipc_max_send_size;
	size_t ipc_max_receive_size;
	enum qdevice_advanced_settings_master_wins master_wins;
	size_t heuristics_ipc_max_send_buffers;
	size_t heuristics_ipc_max_send_receive_size;
	uint32_t heuristics_min_timeout;
	uint32_t heuristics_max_timeout;
	uint32_t heuristics_min_interval;
	uint32_t heuristics_max_interval;
	size_t heuristics_max_execs;
	int heuristics_use_execvp;
	size_t heuristics_max_processes;
	uint32_t heuristics_kill_list_interval;

	/*
	 * Related to model NET
	 */
	char *net_nss_db_dir;
	size_t net_initial_msg_receive_size;
	size_t net_initial_msg_send_size;
	size_t net_min_msg_send_size;
	size_t net_max_msg_receive_size;
	size_t net_max_send_buffers;
	char *net_nss_qnetd_cn;
	char *net_nss_client_cert_nickname;
	uint32_t net_heartbeat_interval_min;
	uint32_t net_heartbeat_interval_max;
	uint32_t net_min_connect_timeout;
	uint32_t net_max_connect_timeout;
	uint8_t net_test_algorithm_enabled;
};

extern int		qdevice_advanced_settings_init(struct qdevice_advanced_settings *settings);

extern int		qdevice_advanced_settings_set(struct qdevice_advanced_settings *settings,
    const char *option, const char *value);

extern void		qdevice_advanced_settings_destroy(struct qdevice_advanced_settings *settings);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_ADVANCED_SETTINGS_H_ */
