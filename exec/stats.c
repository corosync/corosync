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
#include <qb/qbipcs.h>
#include <qb/qbipc_common.h>

#include <corosync/corodefs.h>
#include <corosync/coroapi.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>
#include <corosync/totem/totemstats.h>

#include "util.h"
#include "ipcs_stats.h"
#include "stats.h"

LOGSYS_DECLARE_SUBSYS ("STATS");

static qb_map_t *stats_map;

/* Convert iterator number to text and a stats pointer */
struct cs_stats_conv {
	enum {STAT_PG, STAT_SRP, STAT_KNET, STAT_IPCSC, STAT_IPCSG} type;
	const char *name;
	const size_t offset;
	const icmap_value_types_t value_type;
};

struct cs_stats_conv cs_pg_stats[] = {
	{ STAT_PG, "msg_queue_avail",         offsetof(totempg_stats_t, msg_queue_avail),         ICMAP_VALUETYPE_UINT32},
	{ STAT_PG, "msg_reserved",            offsetof(totempg_stats_t, msg_reserved),            ICMAP_VALUETYPE_UINT32},
};
struct cs_stats_conv cs_srp_stats[] = {
	{ STAT_SRP, "orf_token_tx",           offsetof(totemsrp_stats_t, orf_token_tx),           ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "orf_token_rx",           offsetof(totemsrp_stats_t, orf_token_rx),           ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "memb_merge_detect_tx",   offsetof(totemsrp_stats_t, memb_merge_detect_tx),   ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "memb_merge_detect_rx",   offsetof(totemsrp_stats_t, memb_merge_detect_rx),   ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "memb_join_tx",           offsetof(totemsrp_stats_t, memb_join_tx),           ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "memb_join_rx",           offsetof(totemsrp_stats_t, memb_join_rx),           ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "mcast_tx",               offsetof(totemsrp_stats_t, mcast_tx),               ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "mcast_retx",             offsetof(totemsrp_stats_t, mcast_retx),             ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "mcast_rx",               offsetof(totemsrp_stats_t, mcast_rx),               ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "memb_commit_token_tx",   offsetof(totemsrp_stats_t, memb_commit_token_tx),   ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "memb_commit_token_rx",   offsetof(totemsrp_stats_t, memb_commit_token_rx),   ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "token_hold_cancel_tx",   offsetof(totemsrp_stats_t, token_hold_cancel_tx),   ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "token_hold_cancel_rx",   offsetof(totemsrp_stats_t, token_hold_cancel_rx),   ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "operational_entered",    offsetof(totemsrp_stats_t, operational_entered),    ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "operational_token_lost", offsetof(totemsrp_stats_t, operational_token_lost), ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "gather_entered",         offsetof(totemsrp_stats_t, gather_entered),         ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "gather_token_lost",      offsetof(totemsrp_stats_t, gather_token_lost),      ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "commit_entered",         offsetof(totemsrp_stats_t, commit_entered),         ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "commit_token_lost",      offsetof(totemsrp_stats_t, commit_token_lost),      ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "recovery_entered",       offsetof(totemsrp_stats_t, recovery_entered),       ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "recovery_token_lost",    offsetof(totemsrp_stats_t, recovery_token_lost),    ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "consensus_timeouts",     offsetof(totemsrp_stats_t, consensus_timeouts),     ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "rx_msg_dropped",         offsetof(totemsrp_stats_t, rx_msg_dropped),         ICMAP_VALUETYPE_UINT64},
	{ STAT_SRP, "continuous_gather",      offsetof(totemsrp_stats_t, continuous_gather),      ICMAP_VALUETYPE_UINT32},
	{ STAT_SRP, "continuous_sendmsg_failures", offsetof(totemsrp_stats_t, continuous_sendmsg_failures), ICMAP_VALUETYPE_UINT32},
	{ STAT_SRP, "firewall_enabled_or_nic_failure", offsetof(totemsrp_stats_t, firewall_enabled_or_nic_failure), ICMAP_VALUETYPE_UINT8},
	{ STAT_SRP, "mtt_rx_token",           offsetof(totemsrp_stats_t, mtt_rx_token),           ICMAP_VALUETYPE_UINT32},
	{ STAT_SRP, "avg_token_workload",     offsetof(totemsrp_stats_t, avg_token_workload),     ICMAP_VALUETYPE_UINT32},
	{ STAT_SRP, "avg_backlog_calc",       offsetof(totemsrp_stats_t, avg_backlog_calc),       ICMAP_VALUETYPE_UINT32},
};

struct cs_stats_conv cs_knet_stats[] = {
	{ STAT_KNET, "enabled",          offsetof(struct knet_link_status, enabled),                ICMAP_VALUETYPE_UINT8},
	{ STAT_KNET, "connected",        offsetof(struct knet_link_status, connected),              ICMAP_VALUETYPE_UINT8},
	{ STAT_KNET, "mtu",              offsetof(struct knet_link_status, mtu),                    ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_data_packets",  offsetof(struct knet_link_status, stats.tx_data_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_data_packets",  offsetof(struct knet_link_status, stats.rx_data_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_data_bytes",    offsetof(struct knet_link_status, stats.tx_data_bytes),    ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_data_bytes",    offsetof(struct knet_link_status, stats.rx_data_bytes),    ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_ping_packets",  offsetof(struct knet_link_status, stats.tx_ping_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_ping_packets",  offsetof(struct knet_link_status, stats.rx_ping_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_ping_bytes",    offsetof(struct knet_link_status, stats.tx_ping_bytes),    ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_ping_bytes",    offsetof(struct knet_link_status, stats.rx_ping_bytes),    ICMAP_VALUETYPE_UINT64},
 	{ STAT_KNET, "tx_pong_packets",  offsetof(struct knet_link_status, stats.tx_pong_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_pong_packets",  offsetof(struct knet_link_status, stats.rx_pong_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_pong_bytes",    offsetof(struct knet_link_status, stats.tx_pong_bytes),    ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_pong_bytes",    offsetof(struct knet_link_status, stats.rx_pong_bytes),    ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_pmtu_packets",  offsetof(struct knet_link_status, stats.tx_pmtu_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_pmtu_packets",  offsetof(struct knet_link_status, stats.rx_pmtu_packets),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_pmtu_bytes",    offsetof(struct knet_link_status, stats.tx_pmtu_bytes),    ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_pmtu_bytes",    offsetof(struct knet_link_status, stats.rx_pmtu_bytes),    ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_total_packets", offsetof(struct knet_link_status, stats.tx_total_packets), ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_total_packets", offsetof(struct knet_link_status, stats.rx_total_packets), ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_total_bytes",   offsetof(struct knet_link_status, stats.tx_total_bytes),   ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_total_bytes",   offsetof(struct knet_link_status, stats.rx_total_bytes),   ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_total_errors",  offsetof(struct knet_link_status, stats.tx_total_errors),  ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "rx_total_retries", offsetof(struct knet_link_status, stats.tx_total_retries), ICMAP_VALUETYPE_UINT64},
	{ STAT_KNET, "tx_pmtu_errors",   offsetof(struct knet_link_status, stats.tx_pmtu_errors),   ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_pmtu_retries",  offsetof(struct knet_link_status, stats.tx_pmtu_retries),  ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_ping_errors",   offsetof(struct knet_link_status, stats.tx_ping_errors),   ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_ping_retries",  offsetof(struct knet_link_status, stats.tx_ping_retries),  ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_pong_errors",   offsetof(struct knet_link_status, stats.tx_pong_errors),   ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_pong_retries",  offsetof(struct knet_link_status, stats.tx_pong_retries),  ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_data_errors",   offsetof(struct knet_link_status, stats.tx_data_errors),   ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "tx_data_retries",  offsetof(struct knet_link_status, stats.tx_data_retries),  ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "latency_min",      offsetof(struct knet_link_status, stats.latency_min),      ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "latency_max",      offsetof(struct knet_link_status, stats.latency_max),      ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "latency_ave",      offsetof(struct knet_link_status, stats.latency_ave),      ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "latency_samples",  offsetof(struct knet_link_status, stats.latency_samples),  ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "down_count",       offsetof(struct knet_link_status, stats.down_count),       ICMAP_VALUETYPE_UINT32},
	{ STAT_KNET, "up_count",         offsetof(struct knet_link_status, stats.up_count),         ICMAP_VALUETYPE_UINT32},
};
struct cs_stats_conv cs_ipcs_conn_stats[] = {
	{ STAT_IPCSC, "cnx.queueing",        offsetof(struct ipcs_conn_stats, cnx.queuing),          ICMAP_VALUETYPE_INT32},
	{ STAT_IPCSC, "cnx.queued",          offsetof(struct ipcs_conn_stats, cnx.queued),           ICMAP_VALUETYPE_UINT32},
	{ STAT_IPCSC, "cnx.invalid_request", offsetof(struct ipcs_conn_stats, cnx.invalid_request),  ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSC, "cnx.overload",        offsetof(struct ipcs_conn_stats, cnx.overload),         ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSC, "cnx.sent",            offsetof(struct ipcs_conn_stats, cnx.sent),             ICMAP_VALUETYPE_UINT32},
	{ STAT_IPCSC, "cnx.procname",        offsetof(struct ipcs_conn_stats, cnx.proc_name),        ICMAP_VALUETYPE_STRING},
	{ STAT_IPCSC, "conn.requests",       offsetof(struct ipcs_conn_stats, conn.requests),        ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSC, "conn.responses",      offsetof(struct ipcs_conn_stats, conn.responses),       ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSC, "conn.dispatched",     offsetof(struct ipcs_conn_stats, conn.events),          ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSC, "conn.send_retries",   offsetof(struct ipcs_conn_stats, conn.send_retries),    ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSC, "conn.recv_retries",   offsetof(struct ipcs_conn_stats, conn.recv_retries),    ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSC, "conn.flow_control",   offsetof(struct ipcs_conn_stats, conn.flow_control_state),    ICMAP_VALUETYPE_UINT32},
	{ STAT_IPCSC, "conn.flow_control_count",   offsetof(struct ipcs_conn_stats, conn.flow_control_count),    ICMAP_VALUETYPE_UINT64},
};
struct cs_stats_conv cs_ipcs_global_stats[] = {
	{ STAT_IPCSG, "global.active",        offsetof(struct ipcs_global_stats, active),           ICMAP_VALUETYPE_UINT64},
	{ STAT_IPCSG, "global.closed",        offsetof(struct ipcs_global_stats, closed),           ICMAP_VALUETYPE_UINT64},
};

#define NUM_PG_STATS (sizeof(cs_pg_stats) / sizeof(struct cs_stats_conv))
#define NUM_SRP_STATS (sizeof(cs_srp_stats) / sizeof(struct cs_stats_conv))
#define NUM_KNET_STATS (sizeof(cs_knet_stats) / sizeof(struct cs_stats_conv))
#define NUM_IPCSC_STATS (sizeof(cs_ipcs_conn_stats) / sizeof(struct cs_stats_conv))
#define NUM_IPCSG_STATS (sizeof(cs_ipcs_global_stats) / sizeof(struct cs_stats_conv))

/* What goes in the trie */
struct stats_item {
	char *key_name;
	struct cs_stats_conv * cs_conv;
};

/* One of these per tracker */
struct cs_stats_tracker
{
	char *key_name;
	void *user_data;
	int32_t events;
	icmap_notify_fn_t notify_fn;
	uint64_t old_value;
	struct qb_list_head list;
};
QB_LIST_DECLARE (stats_tracker_list_head);
static const struct corosync_api_v1 *api;

static void stats_map_set_value(struct cs_stats_conv *conv,
				void *stat_array,
				void *value,
				size_t *value_len,
				icmap_value_types_t *type)
{
	if (value_len) {
		*value_len = icmap_get_valuetype_len(conv->value_type);
	}
	if (type) {
		*type = conv->value_type;
		if ((*type == ICMAP_VALUETYPE_STRING) && value_len && stat_array) {
			*value_len = strlen((char *)(stat_array) + conv->offset)+1;
		}
	}
	if (value) {
		memcpy(value, (char *)(stat_array) + conv->offset, *value_len);
	}
}

static void stats_add_entry(const char *key, struct cs_stats_conv *cs_conv)
{
	struct stats_item *item = malloc(sizeof(struct stats_item));

	if (item) {
		item->cs_conv = cs_conv;
		item->key_name = strdup(key);
		qb_map_put(stats_map, item->key_name, item);
	}
}
static void stats_rm_entry(const char *key)
{
	struct stats_item *item = qb_map_get(stats_map, key);

	if (item) {
		qb_map_rm(stats_map, item->key_name);
		free(item->key_name);
		free(item);
	}
}

cs_error_t stats_map_init(const struct corosync_api_v1 *corosync_api)
{
	int i;
	char param[ICMAP_KEYNAME_MAXLEN];

	api = corosync_api;

	stats_map = qb_trie_create();
	if (!stats_map) {
		return CS_ERR_INIT;
	}

	/* Populate the static portions of the trie */
	for (i = 0; i<NUM_PG_STATS; i++) {
		sprintf(param, "stats.pg.%s", cs_pg_stats[i].name);
		stats_add_entry(param, &cs_pg_stats[i]);
	}
	for (i = 0; i<NUM_SRP_STATS; i++) {
		sprintf(param, "stats.srp.%s", cs_srp_stats[i].name);
		stats_add_entry(param, &cs_srp_stats[i]);
	}
	for (i = 0; i<NUM_IPCSG_STATS; i++) {
		sprintf(param, "stats.ipcs.%s", cs_ipcs_global_stats[i].name);
		stats_add_entry(param, &cs_ipcs_global_stats[i]);
	}

	/* KNET and IPCS stats are added when appropriate */
	return CS_OK;
}

cs_error_t stats_map_get(const char *key_name,
			 void *value,
			 size_t *value_len,
			 icmap_value_types_t *type)
{
	struct cs_stats_conv *statinfo;
	struct stats_item *item;
	totempg_stats_t *pg_stats;
	struct knet_link_status link_status;
	struct ipcs_conn_stats ipcs_conn_stats;
	struct ipcs_global_stats ipcs_global_stats;
	int res;
	int nodeid;
	int link_no;
	int service_id;
	uint32_t pid;
	void *conn_ptr;

	item = qb_map_get(stats_map, key_name);
	if (!item) {
		return CS_ERR_NOT_EXIST;
	}

	statinfo = item->cs_conv;
	switch (statinfo->type) {
		case STAT_PG:
			pg_stats = api->totem_get_stats();
			stats_map_set_value(statinfo, pg_stats, value, value_len, type);
			break;
		case STAT_SRP:
			pg_stats = api->totem_get_stats();
			stats_map_set_value(statinfo, pg_stats->srp, value, value_len, type);
			break;
		case STAT_KNET:
			if (sscanf(key_name, "stats.knet.node%d.link%d", &nodeid, &link_no) != 2) {
				return CS_ERR_NOT_EXIST;
			}

			/* Validate node & link IDs */
			if (nodeid <= 0 || nodeid > KNET_MAX_HOST ||
			    link_no < 0 || link_no > KNET_MAX_LINK) {
				return CS_ERR_NOT_EXIST;
			}

			/* Always get the latest stats */
			res = totemknet_link_get_status((knet_node_id_t)nodeid, (uint8_t)link_no, &link_status);
			if (res != CS_OK) {
				return CS_ERR_LIBRARY;
			}
			stats_map_set_value(statinfo, &link_status, value, value_len, type);
			break;
		case STAT_IPCSC:
			if (sscanf(key_name, "stats.ipcs.service%d.%d.%p", &service_id, &pid, &conn_ptr) != 3) {
				return CS_ERR_NOT_EXIST;
			}
			res = cs_ipcs_get_conn_stats(service_id, pid, conn_ptr, &ipcs_conn_stats);
			if (res != CS_OK) {
				return res;
			}
			stats_map_set_value(statinfo, &ipcs_conn_stats, value, value_len, type);
			break;
		case STAT_IPCSG:
			cs_ipcs_get_global_stats(&ipcs_global_stats);
			stats_map_set_value(statinfo, &ipcs_global_stats, value, value_len, type);
			break;
		default:
			return CS_ERR_LIBRARY;
	}
	return CS_OK;
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
	return (qb_map_pref_iter_create(stats_map, prefix));
}


const char *stats_map_iter_next(icmap_iter_t iter, size_t *value_len, icmap_value_types_t *type)
{
	const char *res;
	struct stats_item *item;

	res = qb_map_iter_next(iter, (void **)&item);
	if (res == NULL) {
		return (res);
	}
	stats_map_set_value(item->cs_conv, NULL, NULL, value_len, type);

	return res;
}

void stats_map_iter_finalize(icmap_iter_t iter)
{
	qb_map_iter_free(iter);
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
		if (tracker->events & ICMAP_TRACK_PREFIX) {
			continue;
		}

		res = stats_map_get(tracker->key_name,
				    &value, &value_len, &type);

		/* Check if it has changed */
		if ((res == CS_OK) && (memcmp(&value, &tracker->old_value, value_len) != 0)) {

			old_val.type = new_val.type = type;
			old_val.len = new_val.len = value_len;
			old_val.data = new_val.data = &value;

			tracker->notify_fn(ICMAP_TRACK_MODIFY, tracker->key_name,
					   old_val, new_val, tracker->user_data);

			memcpy(&tracker->old_value, &value, value_len);
		}
	}
}


/* Callback from libqb when a key is added/removed */
static void stats_map_notify_fn(uint32_t event, char *key, void *old_value, void *value, void *user_data)
{
	struct cs_stats_tracker *tracker = user_data;
	struct icmap_notify_value new_val;
	struct icmap_notify_value old_val;
	char new_value[64];

	if (value == NULL && old_value == NULL) {
		return ;
	}

	new_val.data = new_value;
	if (stats_map_get(key,
			  &new_value,
			  &new_val.len,
			  &new_val.type) != CS_OK) {
	}

	/* We don't know what the old value was
	   but as this only tracks ADD & DELETE I'm not worried
	   about it */
	memcpy(&old_val, &new_val, sizeof(new_val));

	tracker->notify_fn(icmap_qbtt_to_tt(event),
			   key,
			   new_val,
			   old_val,
			   tracker->user_data);

}

cs_error_t stats_map_track_add(const char *key_name,
			       int32_t track_type,
			       icmap_notify_fn_t notify_fn,
			       void *user_data,
			       icmap_track_t *icmap_track)
{
	struct cs_stats_tracker *tracker;
	size_t value_len;
	icmap_value_types_t type;
	cs_error_t err;

	/* We can track adding or deleting a key under a prefix */
	if ((track_type & ICMAP_TRACK_PREFIX) &&
	    (!(track_type & ICMAP_TRACK_DELETE) ||
	     !(track_type & ICMAP_TRACK_ADD))) {
		return CS_ERR_NOT_SUPPORTED;
	}

	tracker = malloc(sizeof(struct cs_stats_tracker));
	if (!tracker) {
		return CS_ERR_NO_MEMORY;
	}

	tracker->notify_fn = notify_fn;
	tracker->user_data = user_data;
	if (key_name) {
		tracker->key_name = strdup(key_name);
	}

	/* Get initial value */
	if (stats_map_get(tracker->key_name,
			  &tracker->old_value, &value_len, &type) == CS_OK) {
		tracker->old_value = 0ULL;
	}

	/* Add/delete trackers can use the qb_map tracking */
	if ((track_type & ICMAP_TRACK_ADD) ||
	    (track_type & ICMAP_TRACK_DELETE)) {
		err = qb_map_notify_add(stats_map, tracker->key_name,
					stats_map_notify_fn,
					icmap_tt_to_qbtt(track_type),
					tracker);
		if (err != 0) {
			log_printf(LOGSYS_LEVEL_ERROR, "creating stats tracker %s failed. %d\n", tracker->key_name, err);
			free(tracker->key_name);
			free(tracker);
			return (qb_to_cs_error(err));
		}
		tracker->events = track_type;
	}

	qb_list_add (&tracker->list, &stats_tracker_list_head);

	*icmap_track = (icmap_track_t)tracker;
	return CS_OK;
}

cs_error_t stats_map_track_delete(icmap_track_t icmap_track)
{
	struct cs_stats_tracker *tracker = (struct cs_stats_tracker *)icmap_track;
	int err;

	if ((tracker->events & ICMAP_TRACK_ADD) ||
	    (tracker->events & ICMAP_TRACK_DELETE)) {
		err = qb_map_notify_del_2(stats_map,
					  tracker->key_name, stats_map_notify_fn,
					  icmap_tt_to_qbtt(tracker->events), tracker);
		if (err) {
			log_printf(LOGSYS_LEVEL_ERROR, "deleting tracker %s failed. %d\n", tracker->key_name, err);
		}
	}

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

/* Called from totemknet to add/remove keys from our map */
void stats_knet_add_member(knet_node_id_t nodeid, uint8_t link)
{
	int i;
	char param[ICMAP_KEYNAME_MAXLEN];

	for (i = 0; i<NUM_KNET_STATS; i++) {
		sprintf(param, "stats.knet.node%d.link%d.%s", nodeid, link, cs_knet_stats[i].name);
		stats_add_entry(param, &cs_knet_stats[i]);
	}
}
void stats_knet_del_member(knet_node_id_t nodeid, uint8_t link)
{
	int i;
	char param[ICMAP_KEYNAME_MAXLEN];

	for (i = 0; i<NUM_KNET_STATS; i++) {
		sprintf(param, "stats.knet.node%d.link%d.%s", nodeid, link, cs_knet_stats[i].name);
		stats_rm_entry(param);
	}
}


/* Called from ipc_glue to add/remove keys from our map */
void stats_ipcs_add_connection(int service_id, uint32_t pid, void *ptr)
{
	int i;
	char param[ICMAP_KEYNAME_MAXLEN];

	for (i = 0; i<NUM_IPCSC_STATS; i++) {
		sprintf(param, "stats.ipcs.service%d.%d.%p.%s", service_id, pid, ptr, cs_ipcs_conn_stats[i].name);
		stats_add_entry(param, &cs_ipcs_conn_stats[i]);
	}
}
void stats_ipcs_del_connection(int service_id, uint32_t pid, void *ptr)
{
	int i;
	char param[ICMAP_KEYNAME_MAXLEN];

	for (i = 0; i<NUM_IPCSC_STATS; i++) {
		sprintf(param, "stats.ipcs.service%d.%d.%p.%s", service_id, pid, ptr, cs_ipcs_conn_stats[i].name);
		stats_rm_entry(param);
	}
}
