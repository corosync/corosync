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

#include "qdevice-net-ipc-cmd.h"
#include "qdevice-log.h"
#include "dynar-str.h"
#include "qdevice-net-algorithm.h"
#include "utils.h"

static int
qdevice_net_ipc_cmd_status_add_header(struct dynar *outbuf, int verbose)
{

	return ((dynar_str_catf(outbuf, "Qdevice-net information\n") != -1) &&
	    (dynar_str_catf(outbuf, "----------------------\n") != -1));
}

static int
qdevice_net_ipc_cmd_status_add_tie_breaker(struct qdevice_net_instance *instance,
    struct dynar *outbuf, int verbose)
{

	if (dynar_str_catf(outbuf, "Tie-breaker:\t\t") == -1) {
		return (0);
	}

	switch (instance->tie_breaker.mode) {
	case TLV_TIE_BREAKER_MODE_LOWEST:
		if (dynar_str_catf(outbuf, "Node with lowest node ID") == -1) {
			return (0);
		}
		break;
	case TLV_TIE_BREAKER_MODE_HIGHEST:
		if (dynar_str_catf(outbuf, "Node with highest node ID") == -1) {
			return (0);
		}
		break;
	case TLV_TIE_BREAKER_MODE_NODE_ID:
		if (dynar_str_catf(outbuf, "Node with node ID "UTILS_PRI_NODE_ID,
		    instance->tie_breaker.node_id) == -1) {
			return (0);
		}
		break;
	}

	return (dynar_str_catf(outbuf, "\n") != -1);
}

static int
qdevice_net_ipc_cmd_status_add_basic_info(struct qdevice_net_instance *instance,
    struct dynar *outbuf, int verbose)
{

	if (dynar_str_catf(outbuf, "Cluster name:\t\t%s\n", instance->cluster_name) == -1) {
		return (0);
	}

	if (dynar_str_catf(outbuf, "QNetd host:\t\t%s:%"PRIu16"\n",
	    instance->host_addr, instance->host_port) == -1) {
		return (0);
	}

	if (verbose && instance->force_ip_version != 0) {
		if (dynar_str_catf(outbuf, "Force IP version:\t%u\n",
		    instance->force_ip_version) == -1) {
			return (0);
		}
	}

	if (verbose) {
		if ((dynar_str_catf(outbuf, "Connect timeout:\t%"PRIu32"ms\n",
		    instance->connect_timeout) == -1) ||
		    (dynar_str_catf(outbuf, "HB interval:\t\t%"PRIu32"ms\n",
		    instance->heartbeat_interval) == -1) ||
		    (dynar_str_catf(outbuf, "VQ vote timer interval:\t%"PRIu32"ms\n",
		    instance->cast_vote_timer_interval) == -1)) {
			return (0);
		}

		if (dynar_str_catf(outbuf, "TLS:\t\t\t%s\n",
		    tlv_tls_supported_to_str(instance->tls_supported)) == -1) {
			return (0);
		}
	}

	if (dynar_str_catf(outbuf, "Algorithm:\t\t%s\n",
	    tlv_decision_algorithm_type_to_str(instance->decision_algorithm)) == -1) {
		return (0);
	}

	return (1);
}

static int
qdevice_net_ipc_cmd_status_add_poll_timer_status(struct qdevice_net_instance *instance,
    struct dynar *outbuf, int verbose)
{

	if (!verbose) {
		return (1);
	}

	if (dynar_str_catf(outbuf, "Poll timer running:\t%s",
	    (instance->cast_vote_timer != NULL ? "Yes" : "No")) == -1) {
		return (0);
	}

	if (instance->cast_vote_timer != NULL && instance->cast_vote_timer_vote == TLV_VOTE_ACK) {
		if (dynar_str_catf(outbuf, " (cast vote)") == -1) {
			return (0);
		}
	}

	return (dynar_str_catf(outbuf, "\n") != -1);
}

static int
qdevice_net_ipc_cmd_status_add_state(struct qdevice_net_instance *instance,
    struct dynar *outbuf, int verbose)
{
	const char *state;

	if (instance->schedule_disconnect) {
		state = "Disconnected";
	} else {
		if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
			state = "Connected";
		} else {
			if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT ||
			    !instance->non_blocking_client.destroyed) {
				state = "Connecting";
			} else {
				state = "Connect failed";
			}
		}
	}

	return (dynar_str_catf(outbuf, "State:\t\t\t%s\n", state) != -1);
}

static int
qdevice_net_ipc_cmd_status_add_heuristics(struct qdevice_net_instance *instance,
    struct dynar *outbuf, int verbose)
{
	enum qdevice_heuristics_mode active_heuristics_mode;
	int heuristics_enabled;

	active_heuristics_mode = instance->qdevice_instance_ptr->heuristics_instance.mode;
	heuristics_enabled = (active_heuristics_mode == QDEVICE_HEURISTICS_MODE_ENABLED ||
	    active_heuristics_mode == QDEVICE_HEURISTICS_MODE_SYNC);

	if (!heuristics_enabled) {
		return (1);
	}

	if (dynar_str_catf(outbuf, "Heuristics result:\t%s",
	    tlv_heuristics_to_str(instance->latest_heuristics_result)) == -1) {
		return (0);
	}

	if (verbose) {
		if (dynar_str_catf(outbuf, " (regular: %s, membership: %s, connect: %s)",
		    tlv_heuristics_to_str(instance->latest_regular_heuristics_result),
		    tlv_heuristics_to_str(instance->latest_vq_heuristics_result),
		    tlv_heuristics_to_str(instance->latest_connect_heuristics_result)) == -1) {
			return (0);
		}
	}

	return (dynar_str_catf(outbuf, "\n") != -1);
}

static int
qdevice_net_ipc_cmd_status_add_tls_state(struct qdevice_net_instance *instance,
    struct dynar *outbuf, int verbose)
{

	if (!verbose || instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		return (1);
	}

	if (dynar_str_catf(outbuf, "TLS active:\t\t%s", (instance->using_tls ? "Yes" : "No")) == -1) {
		return (0);
	}

	if (instance->using_tls && instance->tls_client_cert_sent) {
		if (dynar_str_catf(outbuf, " (client certificate sent)") == -1) {
			return (0);
		}
	}

	return (dynar_str_catf(outbuf, "\n") != -1);
}

static int
qdevice_net_ipc_cmd_status_add_times(struct qdevice_net_instance *instance,
    struct dynar *outbuf, int verbose)
{
	struct tm tm_res;

	if (!verbose || instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		return (1);
	}

	if (instance->connected_since_time != ((time_t) -1)) {
		localtime_r(&instance->connected_since_time, &tm_res);
		if (dynar_str_catf(outbuf, "Connected since:\t%04d-%02d-%02dT%02d:%02d:%02d\n",
		    tm_res.tm_year + 1900, tm_res.tm_mon + 1, tm_res.tm_mday,
		    tm_res.tm_hour, tm_res.tm_min, tm_res.tm_sec) == -1) {
			return (0);
		}
	}

	if (instance->last_echo_reply_received_time != ((time_t) -1)) {
		localtime_r(&instance->last_echo_reply_received_time, &tm_res);
		if (dynar_str_catf(outbuf, "Echo reply received:\t%04d-%02d-%02dT%02d:%02d:%02d\n",
		    tm_res.tm_year + 1900, tm_res.tm_mon + 1, tm_res.tm_mday,
		    tm_res.tm_hour, tm_res.tm_min, tm_res.tm_sec) == -1) {
			return (0);
		}
	}

	return (1);
}

int
qdevice_net_ipc_cmd_status(struct qdevice_net_instance *instance, struct dynar *outbuf, int verbose)
{

	if (qdevice_net_ipc_cmd_status_add_header(outbuf, verbose) &&
	    qdevice_net_ipc_cmd_status_add_basic_info(instance, outbuf, verbose) &&
	    qdevice_net_ipc_cmd_status_add_tie_breaker(instance, outbuf, verbose) &&
	    qdevice_net_ipc_cmd_status_add_poll_timer_status(instance, outbuf, verbose) &&
	    qdevice_net_ipc_cmd_status_add_state(instance, outbuf, verbose) &&
	    qdevice_net_ipc_cmd_status_add_heuristics(instance, outbuf, verbose) &&
	    qdevice_net_ipc_cmd_status_add_tls_state(instance, outbuf, verbose) &&
	    qdevice_net_ipc_cmd_status_add_times(instance, outbuf, verbose)) {
		return (1);
	}

	return (0);
}
