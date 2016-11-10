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

#include "qdevice-ipc-cmd.h"
#include "qdevice-log.h"
#include "qdevice-model.h"
#include "dynar-str.h"
#include "utils.h"

static int
qdevice_ipc_cmd_status_add_header(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{

	return ((dynar_str_catf(outbuf, "Qdevice information\n") != -1) &&
	    (dynar_str_catf(outbuf, "-------------------\n") != -1));
}

static int
qdevice_ipc_cmd_status_add_model(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{

	return (dynar_str_catf(outbuf, "Model:\t\t\t%s\n",
		qdevice_model_type_to_str(instance->model_type)) != -1);
}

static int
qdevice_ipc_cmd_status_add_nodeid(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{

	return (dynar_str_catf(outbuf, "Node ID:\t\t"UTILS_PRI_NODE_ID"\n",
	    instance->node_id) != -1);
}

static int
qdevice_ipc_cmd_status_add_intervals(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{

	if (!verbose) {
		return (1);
	}

	return ((dynar_str_catf(outbuf, "HB interval:\t\t%"PRIu32"ms\n",
	    instance->heartbeat_interval) != -1) &&
	    (dynar_str_catf(outbuf, "Sync HB interval:\t%"PRIu32"ms\n",
	    instance->sync_heartbeat_interval) != -1));
}

static int
qdevice_ipc_cmd_status_add_config_node_list(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{
	struct node_list_entry *node_info;
	size_t zi;

	if (instance->config_node_list_version_set) {
		if (dynar_str_catf(outbuf, "Configuration version:\t"UTILS_PRI_CONFIG_VERSION"\n",
		    instance->config_node_list_version) == -1) {
			return (0);
		}
	}

	if (dynar_str_catf(outbuf, "Configured node list:\n") == -1) {
		return (0);
	}

	zi = 0;

	TAILQ_FOREACH(node_info, &instance->config_node_list, entries) {
		if ((dynar_str_catf(outbuf, "    %zu\tNode ID = "UTILS_PRI_NODE_ID, zi,
		    node_info->node_id) == -1) ||
		    (node_info->data_center_id != 0 && dynar_str_catf(outbuf, ", Data center ID = "
		    UTILS_PRI_DATACENTER_ID, node_info->data_center_id) == -1) ||
		    (dynar_str_catf(outbuf, "\n") == -1)) {
			return (0);
		}

		zi++;
	}

	return (1);
}

static int
qdevice_ipc_cmd_status_add_membership_node_list(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{
	uint32_t u32;

	if (verbose && dynar_str_catf(outbuf, "Ring ID:\t\t"UTILS_PRI_RING_ID"\n",
	    instance->vq_node_list_ring_id.nodeid, instance->vq_node_list_ring_id.seq) == -1) {
		return (0);
	}

	if (dynar_str_catf(outbuf, "Membership node list:\t") == -1) {
		return (0);
	}

	for (u32 = 0; u32 < instance->vq_node_list_entries; u32++) {
		if (u32 != 0) {
			if (dynar_str_catf(outbuf, ", ") == -1) {
				return (0);
			}
		}

		if (dynar_str_catf(outbuf, UTILS_PRI_NODE_ID, instance->vq_node_list[u32]) == -1) {
			return (0);
		}
	}

	if (dynar_str_catf(outbuf, "\n") == -1) {
		return (0);
	}

	return (1);
}

static const char *
qdevice_ipc_cmd_vq_nodestate_to_str(uint32_t state)
{

	switch (state) {
	case VOTEQUORUM_NODESTATE_MEMBER: return ("member"); break;
	case VOTEQUORUM_NODESTATE_DEAD: return ("dead"); break;
	case VOTEQUORUM_NODESTATE_LEAVING: return ("leaving"); break;
	default:
		qdevice_log(LOG_ERR, "qdevice_ipc_cmd_vq_nodestate_to_str: Unhandled votequorum "
		    "node state %"PRIu32, state);
		exit(1);
		break;
	}

	return ("Unhandled votequorum node state");
}

static int
qdevice_ipc_cmd_status_add_quorum_node_list(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{
	uint32_t u32;
	votequorum_node_t *node;

	if (!verbose) {
		return (1);
	}

	if (dynar_str_catf(outbuf, "Quorate:\t\t%s\n",
	    (instance->vq_quorum_quorate ? "Yes" : "No")) == -1) {
		return (0);
	}

	if (dynar_str_catf(outbuf, "Quorum node list:\n") == -1) {
		return (0);
	}

	for (u32 = 0; u32 < instance->vq_quorum_node_list_entries; u32++) {
		node = &instance->vq_quorum_node_list[u32];

		if (node->nodeid == 0) {
			continue;
		}

		if (dynar_str_catf(outbuf, "    %"PRIu32"\tNode ID = "UTILS_PRI_NODE_ID
		    ", State = %s\n", u32, node->nodeid,
		    qdevice_ipc_cmd_vq_nodestate_to_str(node->state)) == -1) {
			return (0);
		}
	}

	return (1);
}

static int
qdevice_ipc_cmd_status_add_expected_votes(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{

	if (!verbose) {
		return (1);
	}

	return (dynar_str_catf(outbuf, "Expected votes:\t\t"UTILS_PRI_EXPECTED_VOTES"\n",
	    instance->vq_expected_votes) != -1);
}

static int
qdevice_ipc_cmd_status_add_last_poll(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{
	struct tm tm_res;

	if (!verbose) {
		return (1);
	}

	if (instance->vq_last_poll == ((time_t) -1)) {
		return (dynar_str_catf(outbuf, "Last poll call:\t\tNever\n") != -1);
	}

	localtime_r(&instance->vq_last_poll, &tm_res);

	if (dynar_str_catf(outbuf, "Last poll call:\t\t%04d-%02d-%02dT%02d:%02d:%02d%s\n",
	    tm_res.tm_year + 1900, tm_res.tm_mon + 1, tm_res.tm_mday,
	    tm_res.tm_hour, tm_res.tm_min, tm_res.tm_sec,
	    (instance->vq_last_poll_cast_vote ? " (cast vote)" : "")) == -1) {
		return (0);
	}

	return (1);
}

static int
qdevice_ipc_cmd_status_add_heuristics(struct qdevice_instance *instance, struct dynar *outbuf,
    int verbose)
{

	if (!verbose) {
		return (1);
	}

	return (dynar_str_catf(outbuf, "Heuristics:\t\t%s\n",
	    qdevice_heuristics_mode_to_str(instance->heuristics_instance.mode)) != 0);
}

int
qdevice_ipc_cmd_status(struct qdevice_instance *instance, struct dynar *outbuf, int verbose)
{

	if (qdevice_ipc_cmd_status_add_header(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_model(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_nodeid(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_intervals(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_config_node_list(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_heuristics(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_membership_node_list(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_quorum_node_list(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_expected_votes(instance, outbuf, verbose) &&
	    qdevice_ipc_cmd_status_add_last_poll(instance, outbuf, verbose) &&
	    dynar_str_catf(outbuf, "\n") != -1 &&
	    qdevice_model_ipc_cmd_status(instance, outbuf, verbose) != -1) {
		return (0);
	}

	return (-1);
}
