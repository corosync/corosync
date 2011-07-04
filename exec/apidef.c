/*
 * Copyright (c) 2008, 2009 Red Hat, Inc.
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

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/lcr/lcr_ifact.h>
#include <corosync/totem/totempg.h>
#include <corosync/totem/totemip.h>
#include <corosync/totem/totem.h>
#include <corosync/engine/logsys.h>
#include <corosync/coroipcs.h>
#include "util.h"
#include "timer.h"
#include "sync.h"
#include "quorum.h"
#include "schedwrk.h"
#include "main.h"
#include "apidef.h"
#include <corosync/engine/coroapi.h>
#include "service.h"

LOGSYS_DECLARE_SUBSYS ("APIDEF");

/*
 * Remove compile warnings about type name changes in corosync_tpg_group
 */
typedef int (*typedef_tpg_join) (
	hdb_handle_t,
	const struct corosync_tpg_group *,
	size_t);

typedef int (*typedef_tpg_leave) (hdb_handle_t,
	const struct corosync_tpg_group *,
	size_t);

typedef int (*typedef_tpg_groups_mcast_groups) (
	hdb_handle_t, int,
	const struct corosync_tpg_group *,
	size_t groups_cnt,
	const struct iovec *,
	unsigned int);

typedef int (*typedef_tpg_groups_send_ok) (
	hdb_handle_t,
	const struct corosync_tpg_group *,
	size_t groups_cnt,
	struct iovec *,
	int);

static inline void _corosync_public_exit_error (cs_fatal_error_t err,
						const char *file,
						unsigned int line)
  __attribute__((noreturn));
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
	.timer_expire_time_get = corosync_timer_expire_time_get,
	.ipc_source_set = message_source_set,
	.ipc_source_is_local = message_source_is_local,
	.ipc_private_data_get = coroipcs_private_data_get,
	.ipc_response_iov_send = coroipcs_response_iov_send,
	.ipc_response_send = coroipcs_response_send,
	.ipc_dispatch_send = coroipcs_dispatch_send,
	.ipc_dispatch_iov_send = coroipcs_dispatch_iov_send,
	.ipc_refcnt_inc =  coroipcs_refcount_inc,
	.ipc_refcnt_dec = coroipcs_refcount_dec,
	.totem_nodeid_get = totempg_my_nodeid_get,
	.totem_family_get = totempg_my_family_get,
	.totem_ring_reenable = totempg_ring_reenable,
	.totem_mcast = main_mcast,
	.totem_ifaces_get = totempg_ifaces_get,
	.totem_ifaces_print = totempg_ifaces_print,
	.totem_ip_print = totemip_print,
	.totem_crypto_set = totempg_crypto_set,
	.totem_callback_token_create = totempg_callback_token_create,
	.totem_get_stats = totempg_get_stats,
	.tpg_init = totempg_groups_initialize,
	.tpg_exit = NULL, /* missing from totempg api */
	.tpg_join = (typedef_tpg_join)totempg_groups_join,
	.tpg_leave = (typedef_tpg_leave)totempg_groups_leave,
	.tpg_joined_mcast = totempg_groups_mcast_joined,
	.tpg_joined_reserve = totempg_groups_joined_reserve,
	.tpg_joined_release = totempg_groups_joined_release,
	.tpg_groups_mcast = (typedef_tpg_groups_mcast_groups)totempg_groups_mcast_groups,
	.tpg_groups_reserve = NULL,
	.tpg_groups_release = NULL,
	.schedwrk_create = schedwrk_create,
	.schedwrk_create_nolock = schedwrk_create_nolock,
	.schedwrk_destroy = schedwrk_destroy,
	.sync_request = NULL, //sync_request,
	.quorum_is_quorate = corosync_quorum_is_quorate,
	.quorum_register_callback = corosync_quorum_register_callback,
	.quorum_unregister_callback = corosync_quorum_unregister_callback,
	.quorum_initialize = corosync_quorum_initialize,
	.service_link_and_init = corosync_service_link_and_init,
	.service_unlink_and_exit = corosync_service_unlink_and_exit,
	.plugin_interface_reference = lcr_ifact_reference,
	.plugin_interface_release = lcr_ifact_release,
	.error_memory_failure = _corosync_out_of_memory_error,
	.fatal_error = _corosync_public_exit_error,
	.shutdown_request = corosync_shutdown_request,
	.state_dump = corosync_state_dump,
	.poll_handle_get = corosync_poll_handle_get,
	.poll_dispatch_add = poll_dispatch_add,
	.poll_dispatch_delete = poll_dispatch_delete
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
	apidef_corosync_api_v1.object_key_create_typed = objdb->object_key_create_typed;
	apidef_corosync_api_v1.object_key_get_typed = objdb->object_key_get_typed;
	apidef_corosync_api_v1.object_key_iter_typed = objdb->object_key_iter_typed;
}

struct corosync_api_v1 *apidef_get (void)
{
	return (&apidef_corosync_api_v1);
}
