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

#include "swab.h"
#include "totem.h"
#include "util.h"
#include "logsys.h"
#include "timer.h"
#include "totempg.h"
#include "totemip.h"
#include "main.h"
#include "ipc.h"
#include "../include/coroapi.h"
#include "service.h"

LOGSYS_DECLARE_SUBSYS ("APIDEF", LOG_INFO);

static struct corosync_api_v1 apidef_corosync_api_v1 = {
	.timer_add_duration = openais_timer_add_duration,
	.timer_add_absolute = openais_timer_add_absolute,
	.timer_delete = openais_timer_delete,
	.timer_time_get = NULL,
	.ipc_source_set = message_source_set,
	.ipc_source_is_local = message_source_is_local,
	.ipc_private_data_get = openais_conn_private_data_get,
	.ipc_response_send = NULL,
	.ipc_dispatch_send = NULL,
	.ipc_conn_send_response = openais_conn_send_response,
	.ipc_conn_partner_get = openais_conn_partner_get,
	.ipc_refcnt_inc =  openais_ipc_flow_control_local_increment,
	.ipc_refcnt_dec = openais_ipc_flow_control_local_decrement,
	.ipc_fc_create = openais_ipc_flow_control_create,
	.ipc_fc_destroy = openais_ipc_flow_control_destroy,
	.totem_nodeid_get = totempg_my_nodeid_get,
	.totem_ring_reenable = totempg_ring_reenable,
	.totem_mcast = main_mcast,
	.totem_send_ok = main_send_ok,
	.totem_ifaces_get = totempg_ifaces_get,
	.totem_ifaces_print = totempg_ifaces_print,
	.totem_ip_print = totemip_print,
	.service_link_and_init = openais_service_link_and_init,
	.service_unlink_and_exit = openais_service_unlink_and_exit,
	.error_memory_failure = NULL
};

void apidef_init (struct objdb_iface_ver0 *objdb) {
	apidef_corosync_api_v1.object_create = objdb->object_create;
	apidef_corosync_api_v1.object_priv_set = objdb->object_priv_set;
	apidef_corosync_api_v1.object_key_create = objdb->object_key_create;
	apidef_corosync_api_v1.object_destroy = objdb->object_destroy;
	apidef_corosync_api_v1.object_valid_set = objdb->object_valid_set;
	apidef_corosync_api_v1.object_key_valid_set = objdb->object_key_valid_set;
	apidef_corosync_api_v1.object_find_reset = objdb->object_find_reset;
	apidef_corosync_api_v1.object_find = objdb->object_find;
	apidef_corosync_api_v1.object_key_get = objdb->object_key_get;
	apidef_corosync_api_v1.object_priv_get = objdb->object_priv_get;
	apidef_corosync_api_v1.object_key_replace = objdb->object_key_replace;
	apidef_corosync_api_v1.object_key_delete = objdb->object_key_delete;
	apidef_corosync_api_v1.object_iter_reset = objdb->object_iter_reset;
	apidef_corosync_api_v1.object_iter = objdb->object_iter;
	apidef_corosync_api_v1.object_key_iter = objdb->object_key_iter;
	apidef_corosync_api_v1.object_parent_get = objdb->object_parent_get;
	apidef_corosync_api_v1.object_dump = objdb->object_dump;
	apidef_corosync_api_v1.object_find_from = objdb->object_find_from;
	apidef_corosync_api_v1.object_iter_from = objdb->object_iter_from;
	apidef_corosync_api_v1.object_key_iter_from = objdb->object_key_iter_from;
	apidef_corosync_api_v1.object_write_config = objdb->object_write_config;
}

struct corosync_api_v1 *apidef_get (void)
{
	return (&apidef_corosync_api_v1);
}
