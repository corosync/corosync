/*
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Christine Caulfield (ccaulfie@redhat.com)
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
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
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

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <libknet.h>

#include <qb/qblist.h>
#include <qb/qbipc_common.h>

#include <corosync/corodefs.h>
#include <corosync/coroapi.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>
#include <corosync/totem/totemstats.h>

#include "util.h"
#include "stats.h"

LOGSYS_DECLARE_SUBSYS ("STATS");

/* Global struct for people who use stats_get_map() */
static struct knet_link_status link_status;

/* Convert iterator number to text and a stats pointer */
struct cs_stats_conv {
	const char *text;
	const size_t offset;
	const icmap_value_types_t type;
};

struct cs_stats_conv cs_pg_stats[] = {
	{ "msg_queue_avail",        offsetof(totempg_stats_t, msg_queue_avail),         ICMAP_VALUETYPE_UINT32},
	{ "msg_reserved",           offsetof(totempg_stats_t, msg_reserved),            ICMAP_VALUETYPE_UINT32},
};

struct cs_stats_conv cs_srp_stats[] = {
	{ "orf_token_tx",           offsetof(totemsrp_stats_t, orf_token_tx),           ICMAP_VALUETYPE_UINT64},
	{ "orf_token_rx",           offsetof(totemsrp_stats_t, orf_token_rx),           ICMAP_VALUETYPE_UINT64},
	{ "memb_merge_detect_tx",   offsetof(totemsrp_stats_t, memb_merge_detect_tx),   ICMAP_VALUETYPE_UINT64},
	{ "memb_merge_detect_rx",   offsetof(totemsrp_stats_t, memb_merge_detect_rx),   ICMAP_VALUETYPE_UINT64},
	{ "memb_join_tx",           offsetof(totemsrp_stats_t, memb_join_tx),           ICMAP_VALUETYPE_UINT64},
	{ "memb_join_rx",           offsetof(totemsrp_stats_t, memb_join_rx),           ICMAP_VALUETYPE_UINT64},
	{ "mcast_tx",               offsetof(totemsrp_stats_t, mcast_tx),               ICMAP_VALUETYPE_UINT64},
	{ "mcast_retx",             offsetof(totemsrp_stats_t, mcast_retx),             ICMAP_VALUETYPE_UINT64},
	{ "mcast_rx",               offsetof(totemsrp_stats_t, mcast_rx),               ICMAP_VALUETYPE_UINT64},
	{ "memb_commit_token_tx",   offsetof(totemsrp_stats_t, memb_commit_token_tx),   ICMAP_VALUETYPE_UINT64},
	{ "memb_commit_token_rx",   offsetof(totemsrp_stats_t, memb_commit_token_rx),   ICMAP_VALUETYPE_UINT64},
	{ "token_hold_cancel_tx",   offsetof(totemsrp_stats_t, token_hold_cancel_tx),   ICMAP_VALUETYPE_UINT64},
	{ "token_hold_cancel_rx",   offsetof(totemsrp_stats_t, token_hold_cancel_rx),   ICMAP_VALUETYPE_UINT64},
	{ "operational_entered",    offsetof(totemsrp_stats_t, operational_entered),    ICMAP_VALUETYPE_UINT64},
	{ "operational_token_lost", offsetof(totemsrp_stats_t, operational_token_lost), ICMAP_VALUETYPE_UINT64},
	{ "gather_entered",         offsetof(totemsrp_stats_t, gather_entered),         ICMAP_VALUETYPE_UINT64},
	{ "gather_token_lost",      offsetof(totemsrp_stats_t, gather_token_lost),      ICMAP_VALUETYPE_UINT64},
	{ "commit_entered",         offsetof(totemsrp_stats_t, commit_entered),         ICMAP_VALUETYPE_UINT64},
	{ "commit_token_lost",      offsetof(totemsrp_stats_t, commit_token_lost),      ICMAP_VALUETYPE_UINT64},
	{ "recovery_entered",       offsetof(totemsrp_stats_t, recovery_entered),       ICMAP_VALUETYPE_UINT64},
	{ "recovery_token_lost",    offsetof(totemsrp_stats_t, recovery_token_lost),    ICMAP_VALUETYPE_UINT64},
	{ "consensus_timeouts",     offsetof(totemsrp_stats_t, consensus_timeouts),     ICMAP_VALUETYPE_UINT64},
	{ "rx_msg_dropped",         offsetof(totemsrp_stats_t, rx_msg_dropped),         ICMAP_VALUETYPE_UINT64},
	{ "continuous_gather",      offsetof(totemsrp_stats_t, continuous_gather),      ICMAP_VALUETYPE_UINT32},
	{ "continuous_sendmsg_failures", offsetof(totemsrp_stats_t, continuous_sendmsg_failures), ICMAP_VALUETYPE_UINT32},
	{ "firewall_enabled_or_nic_failure", offsetof(totemsrp_stats_t, firewall_enabled_or_nic_failure), ICMAP_VALUETYPE_UINT8},
	{ "mtt_rx_token",           offsetof(totemsrp_stats_t, mtt_rx_token),           ICMAP_VALUETYPE_UINT32},
	{ "avg_token_workload",     offsetof(totemsrp_stats_t, avg_token_workload),     ICMAP_VALUETYPE_UINT32},
	{ "avg_backlog_calc",       offsetof(totemsrp_stats_t, avg_backlog_calc),       ICMAP_VALUETYPE_UINT32},
};

/* knet stats - this needs updating for each knet stats update */
struct cs_stats_conv cs_knet_link_stats[] = {
	{ "tx_data_packets",  offsetof(struct knet_link_stats, tx_data_packets),  ICMAP_VALUETYPE_UINT64},
	{ "rx_data_packets",  offsetof(struct knet_link_stats, rx_data_packets),  ICMAP_VALUETYPE_UINT64},
	{ "tx_data_bytes",    offsetof(struct knet_link_stats, tx_data_bytes),    ICMAP_VALUETYPE_UINT64},
	{ "rx_data_bytes",    offsetof(struct knet_link_stats, rx_data_bytes),    ICMAP_VALUETYPE_UINT64},
	{ "tx_ping_packets",  offsetof(struct knet_link_stats, tx_ping_packets),  ICMAP_VALUETYPE_UINT64},
	{ "rx_ping_packets",  offsetof(struct knet_link_stats, rx_ping_packets),  ICMAP_VALUETYPE_UINT64},
	{ "tx_ping_bytes",    offsetof(struct knet_link_stats, tx_ping_bytes),    ICMAP_VALUETYPE_UINT64},
	{ "rx_ping_bytes",    offsetof(struct knet_link_stats, rx_ping_bytes),    ICMAP_VALUETYPE_UINT64},
	{ "tx_pong_packets",  offsetof(struct knet_link_stats, tx_pong_packets),  ICMAP_VALUETYPE_UINT64},
	{ "rx_pong_packets",  offsetof(struct knet_link_stats, rx_pong_packets),  ICMAP_VALUETYPE_UINT64},
	{ "tx_pong_bytes",    offsetof(struct knet_link_stats, tx_pong_bytes),    ICMAP_VALUETYPE_UINT64},
	{ "rx_pong_bytes",    offsetof(struct knet_link_stats, rx_pong_bytes),    ICMAP_VALUETYPE_UINT64},
	{ "tx_pmtu_packets",  offsetof(struct knet_link_stats, tx_pmtu_packets),  ICMAP_VALUETYPE_UINT64},
	{ "rx_pmtu_packets",  offsetof(struct knet_link_stats, rx_pmtu_packets),  ICMAP_VALUETYPE_UINT64},
	{ "tx_pmtu_bytes",    offsetof(struct knet_link_stats, tx_pmtu_bytes),    ICMAP_VALUETYPE_UINT64},
	{ "rx_pmtu_bytes",    offsetof(struct knet_link_stats, rx_pmtu_bytes),    ICMAP_VALUETYPE_UINT64},

	{ "tx_total_packets", offsetof(struct knet_link_stats, tx_total_packets), ICMAP_VALUETYPE_UINT64},
	{ "rx_total_packets", offsetof(struct knet_link_stats, rx_total_packets), ICMAP_VALUETYPE_UINT64},
	{ "tx_total_bytes",   offsetof(struct knet_link_stats, tx_total_bytes),   ICMAP_VALUETYPE_UINT64},
	{ "rx_total_bytes",   offsetof(struct knet_link_stats, rx_total_bytes),   ICMAP_VALUETYPE_UINT64},
	{ "tx_total_errors",  offsetof(struct knet_link_stats, tx_total_errors),  ICMAP_VALUETYPE_UINT64},
	{ "rx_total_retries", offsetof(struct knet_link_stats, tx_total_retries), ICMAP_VALUETYPE_UINT64},

	{ "tx_pmtu_errors",   offsetof(struct knet_link_stats, tx_pmtu_errors),   ICMAP_VALUETYPE_UINT32},
	{ "tx_pmtu_retries",  offsetof(struct knet_link_stats, tx_pmtu_retries),  ICMAP_VALUETYPE_UINT32},
	{ "tx_ping_errors",   offsetof(struct knet_link_stats, tx_ping_errors),   ICMAP_VALUETYPE_UINT32},
	{ "tx_ping_retries",  offsetof(struct knet_link_stats, tx_ping_retries),  ICMAP_VALUETYPE_UINT32},
	{ "tx_pong_errors",   offsetof(struct knet_link_stats, tx_pong_errors),   ICMAP_VALUETYPE_UINT32},
	{ "tx_pong_retries",  offsetof(struct knet_link_stats, tx_pong_retries),  ICMAP_VALUETYPE_UINT32},
	{ "tx_data_errors",   offsetof(struct knet_link_stats, tx_data_errors),   ICMAP_VALUETYPE_UINT32},
	{ "tx_data_retries",  offsetof(struct knet_link_stats, tx_data_retries),  ICMAP_VALUETYPE_UINT32},

	{ "latency_min",      offsetof(struct knet_link_stats, latency_min),      ICMAP_VALUETYPE_UINT32},
	{ "latency_max",      offsetof(struct knet_link_stats, latency_max),      ICMAP_VALUETYPE_UINT32},
	{ "latency_ave",      offsetof(struct knet_link_stats, latency_ave),      ICMAP_VALUETYPE_UINT32},
	{ "latency_samples",  offsetof(struct knet_link_stats, latency_samples),  ICMAP_VALUETYPE_UINT32},

	{ "down_count",       offsetof(struct knet_link_stats, down_count),       ICMAP_VALUETYPE_UINT32},
	{ "up_count",         offsetof(struct knet_link_stats, up_count),         ICMAP_VALUETYPE_UINT32},
};

#define NUM_PG_STATS (sizeof(cs_pg_stats) / sizeof(struct cs_stats_conv))
#define NUM_SRP_STATS (sizeof(cs_srp_stats) / sizeof(struct cs_stats_conv))
#define NUM_KNET_STATS (sizeof(cs_knet_link_stats) / sizeof(struct cs_stats_conv))

#define STATS_TRACKER_TIMER_MS 1000

static const struct corosync_api_v1 *corosync_api;

/* One of these per iterator */
struct stats_iterator
{
	knet_node_id_t knet_node;
	uint8_t knet_link;
	uint32_t stats_index;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	enum {PG_STATS, SRP_STATS, KNET_STATS} stats_state;
};

/* One of these per tracker */
struct cs_stats_tracker
{
	char *key_name;
	void *user_data;
	icmap_notify_fn_t notify_fn;
	struct qb_list_head list;
};
QB_LIST_DECLARE (stats_tracker_list_head);

static void stats_map_set_value(struct cs_stats_conv *conv_array,
				int index,
				void *stat_array,
				void *value,
				size_t *value_len,
				icmap_value_types_t *type)
{
	if (type) {
		*type = conv_array[index].type;
	}
	if (value_len) {
		*value_len = icmap_get_valuetype_len(conv_array[index].type);
	}
	if (value) {
		memcpy(value, (char *)(stat_array) + conv_array[index].offset, *value_len);
	}
}

static int stats_map_find_and_set_value(const char *key_name,
					struct cs_stats_conv *conv_array,
					int array_size,
					void *stat_array,
					void *value,
					size_t *value_len,
					icmap_value_types_t *type)
{
	int i;

	for (i=0; i < array_size; i++) {
		if (strcmp(key_name, conv_array[i].text) == 0) {
			stats_map_set_value(conv_array, i, stat_array, value, value_len, type);
			return CS_OK;
		}
	}
	return CS_ERR_NOT_EXIST;
}


cs_error_t stats_map_init(const struct corosync_api_v1 *api)
{
	corosync_api = api;
	return CS_OK;
}

cs_error_t stats_map_get(const char *key_name,
			 void *value,
			 size_t *value_len,
			 icmap_value_types_t *type)
{
	int link, node;
	totempg_stats_t *stats;
	char param[ICMAP_KEYNAME_MAXLEN];
	int res;

	log_printf(LOGSYS_LEVEL_DEBUG, "stats_map_get: %s", key_name);

	if (strncmp(key_name, "stats.pg.", 9) == 0) {
		stats = corosync_api->totem_get_stats();

		return stats_map_find_and_set_value(key_name+9,
						    cs_pg_stats,
						    NUM_PG_STATS,
						    stats,
						    value,
						    value_len,
						    type);
	}

	if (strncmp(key_name, "stats.srp.", 10) == 0) {
		stats = corosync_api->totem_get_stats();

		return stats_map_find_and_set_value(key_name+10,
						    cs_srp_stats,
						    NUM_SRP_STATS,
						    stats->srp,
						    value,
						    value_len,
						    type);
	}


	if (strncmp(key_name, "stats.knet.", 11) == 0) {
		/* KNET stats */
		if (sscanf(key_name, "stats.knet.node%d.link%d.%s", &node, &link, param) != 3) {
			return CS_ERR_INVALID_PARAM;
		}

		if (node < 0 || node > KNET_MAX_HOST ||
		    link < 0 || link > KNET_MAX_LINK) {
			return CS_ERR_INVALID_PARAM;
		}
		log_printf(LOGSYS_LEVEL_DEBUG, "stats_map_get: node:%d, link:%d, stat %s", node, link, param);

		res = totemknet_link_get_status(node, link, &link_status);

		/* returns < 0 indicate invalid nodeid or link id
		   (used by the iterators) */
		if (res < 0) {
			return CS_ERR_INVALID_PARAM;
		}
		if (res != CS_OK) {
			return res;
		}

		return stats_map_find_and_set_value(param,
						    cs_knet_link_stats,
						    NUM_KNET_STATS,
						    &link_status.stats,
						    value,
						    value_len,
						    type);
	}

	return CS_ERR_NOT_EXIST;
}


cs_error_t stats_map_set(const char *key_name,
			 const void *value,
			 size_t value_len,
			 icmap_value_types_t type)
{
	return CS_ERR_NOT_SUPPORTED;
}

cs_error_t stats_map_adjust_int(const char *key_name, int32_t step)
{
	return CS_ERR_NOT_SUPPORTED;
}

cs_error_t stats_map_delete(const char *key_name)
{
	return CS_ERR_NOT_SUPPORTED;
}

int stats_map_is_key_ro(const char *key_name)
{
	/* It's all read-only */
	return 1;
}

icmap_iter_t stats_map_iter_init(const char *prefix)
{
	struct stats_iterator *iter;

	iter = malloc(sizeof(struct stats_iterator));
	if (!iter) {
		return NULL;
	}

	iter->knet_node = 1;
	iter->knet_link = 0xFF; /* wraps around to 0 the first time we do a get */
	iter->stats_index = 0;
	iter->stats_state = PG_STATS;

        return (icmap_iter_t)iter;
}


const char *stats_map_iter_next(icmap_iter_t iter, size_t *value_len, icmap_value_types_t *type)
{
	struct stats_iterator *siter = (struct stats_iterator *)iter;
	int ret;

	/* Non-knet stats first */
	if (siter->stats_state == PG_STATS) {

		if (siter->stats_index >= NUM_PG_STATS) {
			siter->stats_state = SRP_STATS;
			siter->stats_index = 0;
		}
		else {
			sprintf(siter->key_name, "stats.pg.%s", cs_pg_stats[siter->stats_index].text);

			stats_map_set_value(cs_pg_stats, siter->stats_index, NULL, NULL, value_len, type);
			goto return_iter;
		}
	}

	if (siter->stats_state == SRP_STATS) {

		if (siter->stats_index >= NUM_SRP_STATS) {
			siter->stats_state = KNET_STATS;
			siter->stats_index = NUM_KNET_STATS; /* Force re-read */
		}
		else {
			sprintf(siter->key_name, "stats.srp.%s", cs_srp_stats[siter->stats_index].text);

			stats_map_set_value(cs_srp_stats, siter->stats_index, NULL, NULL, value_len, type);
			goto return_iter;
		}
	}

	/* Now knet */
	if (siter->stats_index >= NUM_KNET_STATS) {

		siter->knet_link++;
	retry:
		log_printf(LOGSYS_LEVEL_DEBUG, "Getting stats for node %d link %d", siter->knet_node, siter->knet_link);
		ret = totemknet_link_get_status(siter->knet_node, siter->knet_link,
						&link_status);
		if (ret == -1) { /* no more links for this node */
			siter->knet_node++;

			/* Don't try and get stats for the local node id */
			if (siter->knet_node == corosync_api->totem_nodeid_get()) {
				siter->knet_node++;
			}
			siter->knet_link = 0;
			siter->stats_index = 0;
			goto retry;
		}
		if (ret != CS_OK) { /* includes no more nodes */
			return NULL;
		}
		siter->stats_index = 0;
	}
	sprintf(siter->key_name, "stats.knet.node%d.link%d.%s",
		siter->knet_node, siter->knet_link, cs_knet_link_stats[siter->stats_index].text);

	stats_map_set_value(cs_knet_link_stats, siter->stats_index, NULL, NULL, value_len, type);

return_iter:
	siter->stats_index++;
	return siter->key_name;
}

void stats_map_iter_finalize(icmap_iter_t iter)
{
        free(iter);
}

void stats_trigger_trackers()
{
	struct cs_stats_tracker *tracker;
	struct qb_list_head *iter;
	cs_error_t res;
	size_t value_len;
	icmap_value_types_t type;
	uint64_t value;
	struct icmap_notify_value new_val;
	struct icmap_notify_value old_val;

	qb_list_for_each(iter, &stats_tracker_list_head) {

		tracker = qb_list_entry(iter, struct cs_stats_tracker, list);
		res = stats_map_get(tracker->key_name,
				    &value, &value_len, &type);

		if (res == CS_OK) {
			old_val.type = new_val.type = type;
			old_val.len = new_val.len = value_len;
			old_val.data = new_val.data = &value;

			tracker->notify_fn(ICMAP_TRACK_MODIFY, tracker->key_name,
					   old_val, new_val, tracker->user_data);
		}
	}
}


cs_error_t stats_map_track_add(const char *key_name,
			       int32_t track_type,
			       icmap_notify_fn_t notify_fn,
			       void *user_data,
			       icmap_track_t *icmap_track)
{
	struct cs_stats_tracker *tracker;

	if (track_type & ICMAP_TRACK_PREFIX) {
		return CS_ERR_NOT_SUPPORTED;
	}

	tracker = malloc(sizeof(struct cs_stats_tracker));
	if (!tracker) {
		return CS_ERR_NO_MEMORY;
	}

	tracker->notify_fn = notify_fn;
	tracker->user_data = user_data;
	tracker->key_name = strdup(key_name);

	qb_list_add (&tracker->list, &stats_tracker_list_head);

	*icmap_track = (icmap_track_t)tracker;

	return CS_OK;
}

cs_error_t stats_map_track_delete(icmap_track_t icmap_track)
{
	struct cs_stats_tracker *tracker = (struct cs_stats_tracker *)icmap_track;

	qb_list_del(&tracker->list);
	free(tracker->key_name);
	free(tracker);

	return CS_OK;
}

void *stats_map_track_get_user_data(icmap_track_t icmap_track)
{
	struct cs_stats_tracker *tracker = (struct cs_stats_tracker *)icmap_track;

	return tracker->user_data;
}
