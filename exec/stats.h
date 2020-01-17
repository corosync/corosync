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

cs_error_t stats_map_init(const struct corosync_api_v1 *api);

cs_error_t stats_map_get(const char *key_name,
		   void *value,
		   size_t *value_len,
		   icmap_value_types_t *type);

cs_error_t stats_map_set(const char *key_name,
		   const void *value,
		   size_t value_len,
		   icmap_value_types_t type);

cs_error_t stats_map_adjust_int(const char *key_name, int32_t step);

cs_error_t stats_map_delete(const char *key_name);

int stats_map_is_key_ro(const char *key_name);

icmap_iter_t stats_map_iter_init(const char *prefix);
const char *stats_map_iter_next(icmap_iter_t iter, size_t *value_len, icmap_value_types_t *type);
void stats_map_iter_finalize(icmap_iter_t iter);

cs_error_t stats_map_track_add(const char *key_name,
			 int32_t track_type,
			 icmap_notify_fn_t notify_fn,
			 void *user_data,
			 icmap_track_t *icmap_track);

cs_error_t stats_map_track_delete(icmap_track_t icmap_track);
void *stats_map_track_get_user_data(icmap_track_t icmap_track);

void stats_trigger_trackers(void);


void stats_ipcs_add_connection(int service_id, uint32_t pid, void *ptr);
void stats_ipcs_del_connection(int service_id, uint32_t pid, void *ptr);
cs_error_t cs_ipcs_get_conn_stats(int service_id, uint32_t pid, void *conn_ptr, struct ipcs_conn_stats *ipcs_stats);

void stats_add_schedmiss_event(uint64_t, float delay);
