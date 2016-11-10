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

#ifndef _QDEVICE_INSTANCE_H_
#define _QDEVICE_INSTANCE_H_

#include <sys/types.h>

#include <stdlib.h>
#include <stdint.h>

#include <cmap.h>
#include <votequorum.h>

#include "qdevice-advanced-settings.h"
#include "qdevice-heuristics.h"
#include "qdevice-model-type.h"
#include "node-list.h"
#include "unix-socket-ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qdevice_instance {
	cmap_handle_t cmap_handle;
	int cmap_poll_fd;
	int cmap_reload_in_progress;
	cmap_track_handle_t cmap_reload_track_handle;
	cmap_track_handle_t cmap_nodelist_track_handle;
	cmap_track_handle_t cmap_logging_track_handle;
	cmap_track_handle_t cmap_heuristics_track_handle;

	votequorum_handle_t votequorum_handle;
	int votequorum_poll_fd;

	struct unix_socket_ipc local_ipc;

	enum qdevice_model_type model_type;

	uint32_t node_id;
	uint32_t heartbeat_interval;		/* Heartbeat interval during normal operation */
	uint32_t sync_heartbeat_interval;	/* Heartbeat interval during corosync sync */

	struct node_list config_node_list;
	int config_node_list_version_set;
	uint64_t config_node_list_version;

	/*
	 * Copy of votequorum_quorum_notify_fn callback paramters.
	 * Set after model callback is called.
	 */
	uint32_t vq_quorum_quorate;
	uint32_t vq_quorum_node_list_entries;
	votequorum_node_t *vq_quorum_node_list;

	/*
	 * Copy of current votequorum_nodelist_notify_fn callback parameters.
	 * Set after model callback qdevice_votequorum_node_list_notify_callback is called.
	 */
	uint8_t vq_node_list_initial_ring_id_set;
	votequorum_ring_id_t vq_node_list_ring_id;
	uint32_t vq_node_list_entries;
	uint32_t *vq_node_list;
	uint8_t vq_node_list_initial_heuristics_finished;
	enum qdevice_heuristics_exec_result vq_node_list_heuristics_result;

	/*
	 * Copy of current votequorum_nodelist_notify_fn callback ring id
	 * It's set before any callback is called and used for qdevice_votequorum_poll
	 */
	votequorum_ring_id_t vq_poll_ring_id;

	/*
	 * Copy of votequorum_expectedvotes_notify_fn callback parameters.
	 * Set after model callback is called.
	 */
	uint32_t vq_expected_votes;

	time_t vq_last_poll;
	int vq_last_poll_cast_vote;

	void *model_data;

	const struct qdevice_advanced_settings *advanced_settings;

	int sync_in_progress;

	struct qdevice_heuristics_instance heuristics_instance;
};

extern int	qdevice_instance_init(struct qdevice_instance *instance,
    const struct qdevice_advanced_settings *advanced_settings);

extern int	qdevice_instance_destroy(struct qdevice_instance *instance);

extern int	qdevice_instance_configure_from_cmap(struct qdevice_instance *instance);

extern int	qdevice_instance_configure_from_cmap_heuristics(struct qdevice_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_INSTANCE_H_ */
