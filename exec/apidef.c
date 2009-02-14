/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <corosync/swab.h>
#include <corosync/totem/totem.h>
#include "util.h"
#include <corosync/engine/logsys.h>
#include "timer.h"
#include <corosync/totem/totempg.h>
#include <corosync/totem/totemip.h>
#include "main.h"
#include "ipc.h"
#include "sync.h"
#include "quorum.h"
#include <corosync/engine/coroapi.h>
#include "service.h"
#include <corosync/lcr/lcr_ifact.h>

LOGSYS_DECLARE_SUBSYS ("APIDEF", LOG_INFO);

/*
 * Remove compile warnings about type name changes
 */
typedef int (*typedef_tpg_join) (cs_tpg_handle, struct corosync_tpg_group *, int);
typedef int (*typedef_tpg_leave) (cs_tpg_handle, struct corosync_tpg_group *, int);
typedef int (*typedef_tpg_groups_mcast) (cs_tpg_handle, int, struct corosync_tpg_group *, int groups_cnt, struct iovec *, int);
typedef int (*typedef_tpg_groups_send_ok) (cs_tpg_handle, struct corosync_tpg_group *, int groups_cnt, struct iovec *, int);

static inline void _corosync_public_exit_error (
	cs_fatal_error_t err, const char *file, unsigned int line)
{
	_corosync_exit_error (err, file, line);
}

static struct corosync_api_v1 apidef_corosync_api_v1 = {
	.timer_add_duration = corosync_timer_add_duration,
	.timer_add_absolute = corosync_timer_add_absolute,
	.timer_delete = corosync_timer_delete,
	.timer_time_get = corosync_timer_time_get,
	.ipc_source_set = message_source_set,
	.ipc_source_is_local = message_source_is_local,
	.ipc_private_data_get = cs_conn_private_data_get,
	.ipc_response_send = NULL,
	.ipc_response_no_fcc = cs_conn_send_response_no_fcc,
	.ipc_dispatch_send = NULL,
	.ipc_conn_send_response = cs_conn_send_response,
	.ipc_conn_partner_get = cs_conn_partner_get,
	.ipc_refcnt_inc =  cs_ipc_flow_control_local_increment,
	.ipc_refcnt_dec = cs_ipc_flow_control_local_decrement,
	.ipc_fc_create = cs_ipc_flow_control_create,
	.ipc_fc_destroy = cs_ipc_flow_control_destroy,
	.totem_nodeid_get = totempg_my_nodeid_get,
	.totem_family_get = totempg_my_family_get,
	.totem_ring_reenable = totempg_ring_reenable,
	.totem_mcast = main_mcast,
	.totem_send_ok = main_send_ok,
	.totem_ifaces_get = totempg_ifaces_get,
	.totem_ifaces_print = totempg_ifaces_print,
	.totem_ip_print = totemip_print,
	.totem_callback_token_create = totempg_callback_token_create,
	.tpg_init = totempg_groups_initialize,
	.tpg_exit = NULL, /* missing from totempg api */
	.tpg_join = (typedef_tpg_join)totempg_groups_join,
	.tpg_leave = (typedef_tpg_leave)totempg_groups_leave,
	.tpg_joined_mcast = totempg_groups_mcast_joined,
	.tpg_joined_send_ok = totempg_groups_send_ok_joined,
	.tpg_groups_mcast = (typedef_tpg_groups_mcast)totempg_groups_mcast_groups,
	.tpg_groups_send_ok = (typedef_tpg_groups_send_ok)totempg_groups_send_ok_groups,
	.sync_request = sync_request,
	.quorum_is_quorate = corosync_quorum_is_quorate,
	.quorum_register_callback = corosync_quorum_register_callback,
	.quorum_unregister_callback = corosync_quorum_unregister_callback,
	.quorum_initialize = corosync_quorum_initialize,
	.service_link_and_init = corosync_service_link_and_init,
	.service_unlink_and_exit = corosync_service_unlink_and_exit,
	.plugin_interface_reference = lcr_ifact_reference,
	.plugin_interface_release = lcr_ifact_release,
	.error_memory_failure = _corosync_out_of_memory_error,
	.fatal_error = _corosync_public_exit_error
};

void apidef_init (struct objdb_iface_ver0 *objdb) {
	apidef_corosync_api_v1.object_create = objdb->object_create;
	apidef_corosync_api_v1.object_priv_set = objdb->object_priv_set;
	apidef_corosync_api_v1.object_key_create = objdb->object_key_create;
	apidef_corosync_api_v1.object_destroy = objdb->object_destroy;
	apidef_corosync_api_v1.object_valid_set = objdb->object_valid_set;
	apidef_corosync_api_v1.object_key_valid_set = objdb->object_key_valid_set;
	apidef_corosync_api_v1.object_find_create = objdb->object_find_create;
	apidef_corosync_api_v1.object_find_next = objdb->object_find_next;
	apidef_corosync_api_v1.object_find_destroy = objdb->object_find_destroy;
	apidef_corosync_api_v1.object_key_get = objdb->object_key_get;
	apidef_corosync_api_v1.object_priv_get = objdb->object_priv_get;
	apidef_corosync_api_v1.object_key_replace = objdb->object_key_replace;
	apidef_corosync_api_v1.object_key_delete = objdb->object_key_delete;
	apidef_corosync_api_v1.object_iter_reset = objdb->object_iter_reset;
	apidef_corosync_api_v1.object_iter = objdb->object_iter;
	apidef_corosync_api_v1.object_key_iter = objdb->object_key_iter;
	apidef_corosync_api_v1.object_parent_get = objdb->object_parent_get;
	apidef_corosync_api_v1.object_name_get = objdb->object_name_get;
	apidef_corosync_api_v1.object_dump = objdb->object_dump;
	apidef_corosync_api_v1.object_key_iter_from = objdb->object_key_iter_from;
	apidef_corosync_api_v1.object_track_start = objdb->object_track_start;
	apidef_corosync_api_v1.object_track_stop = objdb->object_track_stop;
	apidef_corosync_api_v1.object_write_config = objdb->object_write_config;
	apidef_corosync_api_v1.object_reload_config = objdb->object_reload_config;
	apidef_corosync_api_v1.object_key_increment = objdb->object_key_increment;
	apidef_corosync_api_v1.object_key_decrement = objdb->object_key_decrement;
}

struct corosync_api_v1 *apidef_get (void)
{
	return (&apidef_corosync_api_v1);
}
