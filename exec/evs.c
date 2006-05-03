/*
 * Copyright (c) 2004-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/saAis.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_evs.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "aispoll.h"
#include "totempg.h"
#include "totemip.h"
#include "main.h"
#include "mempool.h"
#include "service.h"
#include "print.h"

enum evs_exec_message_req_types {
	MESSAGE_REQ_EXEC_EVS_MCAST = 0
};

/*
 * Service Interfaces required by service_message_handler struct
 */
static int evs_exec_init_fn(struct objdb_iface_ver0 *objdb);
static void evs_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static void message_handler_req_exec_mcast (void *message, struct totem_ip_address *source_addr);

static void req_exec_mcast_endian_convert (void *msg);

static void message_handler_req_evs_join (void *conn, void *msg);
static void message_handler_req_evs_leave (void *conn, void *msg);
static void message_handler_req_evs_mcast_joined (void *conn, void *msg);
static void message_handler_req_evs_mcast_groups (void *conn, void *msg);
static void message_handler_req_evs_membership_get (void *conn, void *msg);

static int evs_lib_init_fn (void *conn);
static int evs_lib_exit_fn (void *conn);

struct evs_pd {
	struct evs_group *groups;
	int group_entries;
	struct list_head list;
	void *conn;
};
	
static struct openais_lib_handler evs_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_evs_join,
		.response_size				= sizeof (struct res_lib_evs_join),
		.response_id				= MESSAGE_RES_EVS_JOIN,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_evs_leave,
		.response_size				= sizeof (struct res_lib_evs_leave),
		.response_id				= MESSAGE_RES_EVS_LEAVE,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_evs_mcast_joined,
		.response_size				= sizeof (struct res_lib_evs_mcast_joined),
		.response_id				= MESSAGE_RES_EVS_MCAST_JOINED,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_evs_mcast_groups,
		.response_size				= sizeof (struct res_lib_evs_mcast_groups),
		.response_id				= MESSAGE_RES_EVS_MCAST_GROUPS,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_evs_membership_get,
		.response_size				= sizeof (struct res_lib_evs_membership_get),
		.response_id				= MESSAGE_RES_EVS_MEMBERSHIP_GET,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct openais_exec_handler evs_exec_service[] =
{
	{
		.exec_handler_fn 	= message_handler_req_exec_mcast,
		.exec_endian_convert_fn	= req_exec_mcast_endian_convert
	}
};

struct openais_service_handler evs_service_handler = {
	.name			= (unsigned char*)"openais extended virtual synchrony service",
	.id			= EVS_SERVICE,
	.private_data_size	= sizeof (struct evs_pd),
	.lib_init_fn		= evs_lib_init_fn,
	.lib_exit_fn		= evs_lib_exit_fn,
	.lib_service		= evs_lib_service,
	.lib_service_count	= sizeof (evs_lib_service) / sizeof (struct openais_lib_handler),
	.exec_service		= evs_exec_service,
	.exec_service_count	= sizeof (evs_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn		= evs_confchg_fn,
	.exec_init_fn		= evs_exec_init_fn,
	.exec_dump_fn		= NULL
};

static DECLARE_LIST_INIT (confchg_notify);

/*
 * Dynamic loading descriptor
 */

static struct openais_service_handler *evs_get_service_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 evs_service_handler_iface = {
	.openais_get_service_handler_ver0	= evs_get_service_handler_ver0
};

static struct lcr_iface openais_evs_ver0[1] = {
	{
		.name			= "openais_evs",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL,
	}
};

static struct lcr_comp evs_comp_ver0 = {
	.iface_count	= 1,
	.ifaces		= openais_evs_ver0
};

static struct openais_service_handler *evs_get_service_handler_ver0 (void)
{
	return (&evs_service_handler);
}

__attribute__ ((constructor)) static void evs_comp_register (void) {
	lcr_interfaces_set (&openais_evs_ver0[0], &evs_service_handler_iface);

	lcr_component_register (&evs_comp_ver0);
}

static int evs_exec_init_fn(struct objdb_iface_ver0 *objdb)
{
	(void) objdb;

	log_init ("EVS");

	return 0;
}

struct res_evs_confchg_callback res_evs_confchg_callback;

static void evs_confchg_fn (
	enum totem_configuration_type configuration_type,
    struct totem_ip_address *member_list, int member_list_entries,
    struct totem_ip_address *left_list, int left_list_entries,
    struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{

	int i;
	struct list_head *list;
	struct evs_pd *evs_pd;

	/*
	 * Build configuration change message
	 */
	res_evs_confchg_callback.header.size = sizeof (struct res_evs_confchg_callback);
	res_evs_confchg_callback.header.id = MESSAGE_RES_EVS_CONFCHG_CALLBACK;
	res_evs_confchg_callback.header.error = SA_AIS_OK;

	for (i = 0; i < member_list_entries; i++) {
		totemip_copy((struct totem_ip_address *)&res_evs_confchg_callback.member_list[i],
			&member_list[i]);
	}
	res_evs_confchg_callback.member_list_entries = member_list_entries;
	for (i = 0; i < left_list_entries; i++) {
		totemip_copy((struct totem_ip_address *)&res_evs_confchg_callback.left_list[i],
			&left_list[i]);
	}
	res_evs_confchg_callback.left_list_entries = left_list_entries;
	for (i = 0; i < joined_list_entries; i++) {
		totemip_copy((struct totem_ip_address *)&res_evs_confchg_callback.joined_list[i],
			&joined_list[i]);
	}
	res_evs_confchg_callback.joined_list_entries = joined_list_entries;

	/*
	 * Send configuration change message to every EVS library user
	 */
	for (list = confchg_notify.next; list != &confchg_notify; list = list->next) {
		evs_pd = list_entry (list, struct evs_pd, list);
		openais_conn_send_response (evs_pd->conn,
			&res_evs_confchg_callback,
			sizeof (res_evs_confchg_callback));
	}
}

static int evs_lib_init_fn (void *conn)
{
	struct evs_pd *evs_pd = (struct evs_pd *)openais_conn_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "Got request to initalize evs service.\n");

	evs_pd->groups = NULL;
	evs_pd->group_entries = 0;
	evs_pd->conn = conn;
	list_init (&evs_pd->list);
	list_add (&evs_pd->list, &confchg_notify);

	openais_conn_send_response (conn, &res_evs_confchg_callback,
		sizeof (res_evs_confchg_callback));

	return (0);
}

static int evs_lib_exit_fn (void *conn)
{
    struct evs_pd *evs_pd = (struct evs_pd *)openais_conn_private_data_get (conn);

	list_del (&evs_pd->list);
	return (0);
}

static void message_handler_req_evs_join (void *conn, void *msg)
{
	evs_error_t error = EVS_OK;
	struct req_lib_evs_join *req_lib_evs_join = (struct req_lib_evs_join *)msg;
	struct res_lib_evs_join res_lib_evs_join;
	void *addr;
	struct evs_pd *evs_pd = (struct evs_pd *)openais_conn_private_data_get (conn);

	if (req_lib_evs_join->group_entries > 50) {
		error = EVS_ERR_TOO_MANY_GROUPS;
		goto exit_error;
	}

	addr = realloc (evs_pd->groups, sizeof (struct evs_group) * 
		(evs_pd->group_entries + req_lib_evs_join->group_entries));
	if (addr == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto exit_error;
	}
	evs_pd->groups = addr;

	memcpy (&evs_pd->groups[evs_pd->group_entries],
		req_lib_evs_join->groups,
		sizeof (struct evs_group) * req_lib_evs_join->group_entries);

	evs_pd->group_entries += req_lib_evs_join->group_entries;

exit_error:
	res_lib_evs_join.header.size = sizeof (struct res_lib_evs_join);
	res_lib_evs_join.header.id = MESSAGE_RES_EVS_JOIN;
	res_lib_evs_join.header.error = error;

	openais_conn_send_response (conn, &res_lib_evs_join,
		sizeof (struct res_lib_evs_join));
}

static void message_handler_req_evs_leave (void *conn, void *msg)
{
	struct req_lib_evs_leave *req_lib_evs_leave = (struct req_lib_evs_leave *)msg;
	struct res_lib_evs_leave res_lib_evs_leave;
	evs_error_t error = EVS_OK;
	int error_index;
	int i, j;
	int found;
	struct evs_pd *evs_pd = (struct evs_pd *)openais_conn_private_data_get (conn);

	for (i = 0; i < req_lib_evs_leave->group_entries; i++) {
		found = 0;
		for (j = 0; j < evs_pd->group_entries;) {

			if (memcmp (&req_lib_evs_leave->groups[i],
				&evs_pd->groups[j], sizeof (struct evs_group)) == 0) {

				/*
				 * Delete entry
				 */
				memmove (&evs_pd->groups[j], &evs_pd->groups[j + 1],
					(evs_pd->group_entries - j - 1) * sizeof (struct evs_group));

				evs_pd->group_entries -= 1;

				found = 1;
				break;
			} else {
				j++;
			}
		}
		if (found == 0) {
			error = EVS_ERR_NOT_EXIST;
			error_index = i;
			break;
		}
	}

	res_lib_evs_leave.header.size = sizeof (struct res_lib_evs_leave);
	res_lib_evs_leave.header.id = MESSAGE_RES_EVS_LEAVE;
	res_lib_evs_leave.header.error = error;

	openais_conn_send_response (conn, &res_lib_evs_leave,
		sizeof (struct res_lib_evs_leave));
}

static void message_handler_req_evs_mcast_joined (void *conn, void *msg)
{
	evs_error_t error = EVS_ERR_TRY_AGAIN;
	struct req_lib_evs_mcast_joined *req_lib_evs_mcast_joined = (struct req_lib_evs_mcast_joined *)msg;
	struct res_lib_evs_mcast_joined res_lib_evs_mcast_joined;
	struct iovec req_exec_evs_mcast_iovec[3];
	struct req_exec_evs_mcast req_exec_evs_mcast;
	int send_ok = 0;
	int res;
	struct evs_pd *evs_pd = (struct evs_pd *)openais_conn_private_data_get (conn);

	req_exec_evs_mcast.header.size = sizeof (struct req_exec_evs_mcast) +
		evs_pd->group_entries * sizeof (struct evs_group) +
		req_lib_evs_mcast_joined->msg_len;

	req_exec_evs_mcast.header.id =
		SERVICE_ID_MAKE (EVS_SERVICE, MESSAGE_REQ_EXEC_EVS_MCAST);
	req_exec_evs_mcast.msg_len = req_lib_evs_mcast_joined->msg_len;
	req_exec_evs_mcast.group_entries = evs_pd->group_entries;

	req_exec_evs_mcast_iovec[0].iov_base = &req_exec_evs_mcast;
	req_exec_evs_mcast_iovec[0].iov_len = sizeof (req_exec_evs_mcast);
	req_exec_evs_mcast_iovec[1].iov_base = evs_pd->groups;
	req_exec_evs_mcast_iovec[1].iov_len = evs_pd->group_entries * sizeof (struct evs_group);
	req_exec_evs_mcast_iovec[2].iov_base = &req_lib_evs_mcast_joined->msg;
	req_exec_evs_mcast_iovec[2].iov_len = req_lib_evs_mcast_joined->msg_len;
// TODO this doesn't seem to work for some reason	
	send_ok = totempg_groups_send_ok_joined (openais_group_handle, req_exec_evs_mcast_iovec, 3);

	res = totempg_groups_mcast_joined (openais_group_handle, req_exec_evs_mcast_iovec, 3, TOTEMPG_AGREED);
		// TODO
	if (res == 0) {
		error = EVS_OK;
	}

	res_lib_evs_mcast_joined.header.size = sizeof (struct res_lib_evs_mcast_joined);
	res_lib_evs_mcast_joined.header.id = MESSAGE_RES_EVS_MCAST_JOINED;
	res_lib_evs_mcast_joined.header.error = error;

	openais_conn_send_response (conn, &res_lib_evs_mcast_joined,
		sizeof (struct res_lib_evs_mcast_joined));
}

static void message_handler_req_evs_mcast_groups (void *conn, void *msg)
{
	evs_error_t error = EVS_ERR_TRY_AGAIN;
	struct req_lib_evs_mcast_groups *req_lib_evs_mcast_groups = (struct req_lib_evs_mcast_groups *)msg;
	struct res_lib_evs_mcast_groups res_lib_evs_mcast_groups;
	struct iovec req_exec_evs_mcast_iovec[3];
	struct req_exec_evs_mcast req_exec_evs_mcast;
	char *msg_addr;
	int send_ok = 0;
	int res;

	req_exec_evs_mcast.header.size = sizeof (struct req_exec_evs_mcast) +
		sizeof (struct evs_group) * req_lib_evs_mcast_groups->group_entries +
		req_lib_evs_mcast_groups->msg_len;

	req_exec_evs_mcast.header.id =
		SERVICE_ID_MAKE (EVS_SERVICE, MESSAGE_REQ_EXEC_EVS_MCAST);
	req_exec_evs_mcast.msg_len = req_lib_evs_mcast_groups->msg_len;
	req_exec_evs_mcast.group_entries = req_lib_evs_mcast_groups->group_entries;

	msg_addr = (char *)req_lib_evs_mcast_groups +
		sizeof (struct req_lib_evs_mcast_groups) + 
		(sizeof (struct evs_group) * req_lib_evs_mcast_groups->group_entries);

	req_exec_evs_mcast_iovec[0].iov_base = &req_exec_evs_mcast;
	req_exec_evs_mcast_iovec[0].iov_len = sizeof (req_exec_evs_mcast);
	req_exec_evs_mcast_iovec[1].iov_base = &req_lib_evs_mcast_groups->groups;
	req_exec_evs_mcast_iovec[1].iov_len = sizeof (struct evs_group) * req_lib_evs_mcast_groups->group_entries;
	req_exec_evs_mcast_iovec[2].iov_base = msg_addr;
	req_exec_evs_mcast_iovec[2].iov_len = req_lib_evs_mcast_groups->msg_len;
	
// TODO this is wacky
	send_ok = totempg_groups_send_ok_joined (openais_group_handle, req_exec_evs_mcast_iovec, 3);
	res = totempg_groups_mcast_joined (openais_group_handle, req_exec_evs_mcast_iovec, 3, TOTEMPG_AGREED);
	if (res == 0) {
		error = EVS_OK;
	}

	res_lib_evs_mcast_groups.header.size = sizeof (struct res_lib_evs_mcast_groups);
	res_lib_evs_mcast_groups.header.id = MESSAGE_RES_EVS_MCAST_GROUPS;
	res_lib_evs_mcast_groups.header.error = error;

	openais_conn_send_response (conn, &res_lib_evs_mcast_groups,
		sizeof (struct res_lib_evs_mcast_groups));
}

static void message_handler_req_evs_membership_get (void *conn, void *msg)
{
	struct res_lib_evs_membership_get res_lib_evs_membership_get;

	res_lib_evs_membership_get.header.size = sizeof (struct res_lib_evs_membership_get);
	res_lib_evs_membership_get.header.id = MESSAGE_RES_EVS_MEMBERSHIP_GET;
	res_lib_evs_membership_get.header.error = EVS_OK;
	totemip_copy((struct totem_ip_address *)&res_lib_evs_membership_get.local_addr,
		this_ip);
	memcpy (&res_lib_evs_membership_get.member_list,
		&res_evs_confchg_callback.member_list,
		sizeof (res_lib_evs_membership_get.member_list));

	res_lib_evs_membership_get.member_list_entries =
		res_evs_confchg_callback.member_list_entries;

	openais_conn_send_response (conn, &res_lib_evs_membership_get,
		sizeof (struct res_lib_evs_membership_get));
}

static void req_exec_mcast_endian_convert (void *msg)
{
}

static void message_handler_req_exec_mcast (void *msg, struct totem_ip_address *source_addr)
{
	struct req_exec_evs_mcast *req_exec_evs_mcast = (struct req_exec_evs_mcast *)msg;
	struct res_evs_deliver_callback res_evs_deliver_callback;
	char *msg_addr;
	struct list_head *list;
	int found = 0;
	int i, j;
	struct evs_pd *evs_pd;

	res_evs_deliver_callback.header.size = sizeof (struct res_evs_deliver_callback) +
		req_exec_evs_mcast->msg_len;
	res_evs_deliver_callback.header.id = MESSAGE_RES_EVS_DELIVER_CALLBACK;
	res_evs_deliver_callback.header.error = SA_AIS_OK;
	res_evs_deliver_callback.msglen = req_exec_evs_mcast->msg_len;

	msg_addr = (char *)req_exec_evs_mcast + sizeof (struct req_exec_evs_mcast) + 
		(sizeof (struct evs_group) * req_exec_evs_mcast->group_entries);

	for (list = confchg_notify.next; list != &confchg_notify; list = list->next) {
		found = 0;
		evs_pd = list_entry (list, struct evs_pd, list);

		for (i = 0; i < evs_pd->group_entries; i++) {
			for (j = 0; j < req_exec_evs_mcast->group_entries; j++) {

				if (memcmp (&evs_pd->groups[i], &req_exec_evs_mcast->groups[j],
					sizeof (struct evs_group)) == 0) {

					found = 1;
					break;
				}
			}
			if (found) {
				break;
			}
		}

		if (found) {
			totemip_copy((struct totem_ip_address *)&res_evs_deliver_callback.evs_address,
				source_addr);
			openais_conn_send_response (evs_pd->conn, &res_evs_deliver_callback,
				sizeof (struct res_evs_deliver_callback));
			openais_conn_send_response (evs_pd->conn, msg_addr,
				req_exec_evs_mcast->msg_len);
		}
	}
}

