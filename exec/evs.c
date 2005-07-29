/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
 * Copyright (c) 2004 MontaVista Software, Inc.
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
#include <sys/sysinfo.h>
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

#include "../include/ais_types.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_evs.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "aispoll.h"
#include "totempg.h"
#include "main.h"
#include "mempool.h"
#include "handlers.h"

#define LOG_SERVICE LOG_SERVICE_EVS
#include "print.h"

static DECLARE_LIST_INIT (confchg_notify);

/*
 * Service Interfaces required by service_message_handler struct
 */
static int evs_executive_initialize (struct openais_config *);

static int evs_confchg_fn (
	enum totem_configuration_type configuration_type,
    struct in_addr *member_list, int member_list_entries,
    struct in_addr *left_list, int left_list_entries,
    struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static int evs_init_two_fn (struct conn_info *conn_info);

static int message_handler_req_exec_mcast (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_evs_join (struct conn_info *conn_info, void *message);
static int message_handler_req_evs_leave (struct conn_info *conn_info, void *message);
static int message_handler_req_evs_mcast_joined (struct conn_info *conn_info, void *message);
static int message_handler_req_evs_mcast_groups (struct conn_info *conn_info, void *message);
static int message_handler_req_evs_membership_get (struct conn_info *conn_info, void *message);

static int evs_exit_fn (struct conn_info *conn_info);

struct libais_handler evs_libais_handlers[] =
{
	{ /* 0 */
		.libais_handler_fn			= message_handler_req_evs_join,
		.response_size				= sizeof (struct res_lib_evs_join),
		.response_id				= MESSAGE_RES_EVS_JOIN,
		.flow_control				= FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.libais_handler_fn			= message_handler_req_evs_leave,
		.response_size				= sizeof (struct res_lib_evs_leave),
		.response_id				= MESSAGE_RES_EVS_LEAVE,
		.flow_control				= FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.libais_handler_fn			= message_handler_req_evs_mcast_joined,
		.response_size				= sizeof (struct res_lib_evs_mcast_joined),
		.response_id				= MESSAGE_RES_EVS_MCAST_JOINED,
		.flow_control				= FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.libais_handler_fn			= message_handler_req_evs_mcast_groups,
		.response_size				= sizeof (struct res_lib_evs_mcast_groups),
		.response_id				= MESSAGE_RES_EVS_MCAST_GROUPS,
		.flow_control				= FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.libais_handler_fn			= message_handler_req_evs_membership_get,
		.response_size				= sizeof (struct res_lib_evs_membership_get),
		.response_id				= MESSAGE_RES_EVS_MEMBERSHIP_GET,
		.flow_control				= FLOW_CONTROL_NOT_REQUIRED
	}
};

static int (*evs_aisexec_handler_fns[]) (void *, struct in_addr source_addr, int endian_conversion_required) = {
	message_handler_req_exec_mcast
};
	
struct service_handler evs_service_handler = {
	.libais_handlers			= evs_libais_handlers,
	.libais_handlers_count		= sizeof (evs_libais_handlers) / sizeof (struct libais_handler),
	.aisexec_handler_fns		= evs_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (evs_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn					= evs_confchg_fn,
	.libais_init_two_fn			= evs_init_two_fn,
	.libais_exit_fn				= evs_exit_fn,
	.exec_init_fn				= evs_executive_initialize,
	.exec_dump_fn				= 0
};

static int evs_executive_initialize (struct openais_config *openais_config)
{
	return (0);
}

static int evs_exit_fn (struct conn_info *conn_info)
{

	list_del (&conn_info->conn_list);
	return (0);
}

struct res_evs_confchg_callback res_evs_confchg_callback;

static int evs_confchg_fn (
	enum totem_configuration_type configuration_type,
    struct in_addr *member_list, int member_list_entries,
    struct in_addr *left_list, int left_list_entries,
    struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{

	int i;
	struct list_head *list;
	struct conn_info *conn_info;

	/*
	 * Build configuration change message
	 */
	res_evs_confchg_callback.header.size = sizeof (struct res_evs_confchg_callback);
	res_evs_confchg_callback.header.id = MESSAGE_RES_EVS_CONFCHG_CALLBACK;
	res_evs_confchg_callback.header.error = SA_OK;

	for (i = 0; i < member_list_entries; i++) {
		res_evs_confchg_callback.member_list[i].s_addr = member_list[i].s_addr;
	}
	res_evs_confchg_callback.member_list_entries = member_list_entries;
	for (i = 0; i < left_list_entries; i++) {
		res_evs_confchg_callback.left_list[i].s_addr = left_list[i].s_addr;
	}
	res_evs_confchg_callback.left_list_entries = left_list_entries;
	for (i = 0; i < joined_list_entries; i++) {
		res_evs_confchg_callback.joined_list[i].s_addr = joined_list[i].s_addr;
	}
	res_evs_confchg_callback.joined_list_entries = joined_list_entries;

	/*
	 * Send configuration change message to every EVS library user
	 */
	for (list = confchg_notify.next; list != &confchg_notify; list = list->next) {
		conn_info = list_entry (list, struct conn_info, conn_list);
		libais_send_response (conn_info, &res_evs_confchg_callback,
			sizeof (res_evs_confchg_callback));
	}

	return (0);
}

static int evs_init_two_fn (struct conn_info *conn_info)
{
	log_printf (LOG_LEVEL_DEBUG, "Got request to initalize evs service.\n");
	list_init (&conn_info->conn_list);
	list_add (&conn_info->conn_list, &confchg_notify);
	libais_send_response (conn_info, &res_evs_confchg_callback,
		sizeof (res_evs_confchg_callback));

	return (0);
}

static int message_handler_req_evs_join (struct conn_info *conn_info, void *message)
{
	evs_error_t error = EVS_OK;
	struct req_lib_evs_join *req_lib_evs_join = (struct req_lib_evs_join *)message;
	struct res_lib_evs_join res_lib_evs_join;
	void *addr;

	if (req_lib_evs_join->group_entries > 50) {
		error = EVS_ERR_TOO_MANY_GROUPS;
		goto exit_error;
	}

#ifdef DEBUG
{ int i;
	for (i = 0; i < req_lib_evs_join->group_entries; i++) {
		printf ("Joining group %s\n", req_lib_evs_join->groups[i].key);
	}
}
#endif
	addr = realloc (conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups,
		sizeof (struct evs_group) * 
		(conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries +
		req_lib_evs_join->group_entries));
	if (addr == 0) {
		error = SA_ERR_NO_MEMORY;
		goto exit_error;
	}
	conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups = addr;

	memcpy (&conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups[conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries],
		req_lib_evs_join->groups,
		sizeof (struct evs_group) * req_lib_evs_join->group_entries);

	conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries += req_lib_evs_join->group_entries;

exit_error:
	res_lib_evs_join.header.size = sizeof (struct res_lib_evs_join);
	res_lib_evs_join.header.id = MESSAGE_RES_EVS_JOIN;
	res_lib_evs_join.header.error = error;

	libais_send_response (conn_info, &res_lib_evs_join,
		sizeof (struct res_lib_evs_join));

	return (0);
}

static int message_handler_req_evs_leave (struct conn_info *conn_info, void *message)
{
	struct req_lib_evs_leave *req_lib_evs_leave = (struct req_lib_evs_leave *)message;
	struct res_lib_evs_leave res_lib_evs_leave;
	evs_error_t error = EVS_OK;
	int error_index;
	int i, j;
	int found;

	for (i = 0; i < req_lib_evs_leave->group_entries; i++) {
		found = 0;
		for (j = 0; j < conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries;) {
			if (memcmp (&req_lib_evs_leave->groups[i],
				&conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups[j],
				sizeof (struct evs_group)) == 0) {

				/*
				 * Delete entry
				 */
				memmove (&conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups[j],
					&conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups[j + 1],
					(conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries - j - 1) * 
					sizeof (struct evs_group));

				conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries -= 1;

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

#ifdef DEBUG
	for (i = 0; i < conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries; i++) {
		printf ("Groups Left %s\n", 
					&conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups[i].key);
	}
#endif
	res_lib_evs_leave.header.size = sizeof (struct res_lib_evs_leave);
	res_lib_evs_leave.header.id = MESSAGE_RES_EVS_LEAVE;
	res_lib_evs_leave.header.error = error;

	libais_send_response (conn_info, &res_lib_evs_leave,
		sizeof (struct res_lib_evs_leave));

	return (0);
}

static int message_handler_req_evs_mcast_joined (struct conn_info *conn_info, void *message)
{
	evs_error_t error = EVS_ERR_TRY_AGAIN;
	struct req_lib_evs_mcast_joined *req_lib_evs_mcast_joined = (struct req_lib_evs_mcast_joined *)message;
	struct res_lib_evs_mcast_joined res_lib_evs_mcast_joined;
	struct iovec req_exec_evs_mcast_iovec[3];
	struct req_exec_evs_mcast req_exec_evs_mcast;
	int send_ok = 0;
	int res;

	req_exec_evs_mcast.header.size = sizeof (struct req_exec_evs_mcast) +
		conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries *
		sizeof (struct evs_group) +
		req_lib_evs_mcast_joined->msg_len;

	req_exec_evs_mcast.header.id = MESSAGE_REQ_EXEC_EVS_MCAST;
	req_exec_evs_mcast.msg_len = req_lib_evs_mcast_joined->msg_len;
	req_exec_evs_mcast.group_entries = conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries;

	req_exec_evs_mcast_iovec[0].iov_base = &req_exec_evs_mcast;
	req_exec_evs_mcast_iovec[0].iov_len = sizeof (req_exec_evs_mcast);
	req_exec_evs_mcast_iovec[1].iov_base = conn_info->conn_info_partner->ais_ci.u.libevs_ci.groups;
	req_exec_evs_mcast_iovec[1].iov_len = conn_info->conn_info_partner->ais_ci.u.libevs_ci.group_entries * sizeof (struct evs_group);
	req_exec_evs_mcast_iovec[2].iov_base = &req_lib_evs_mcast_joined->msg;
	req_exec_evs_mcast_iovec[2].iov_len = req_lib_evs_mcast_joined->msg_len;
// TODO this doesn't seem to work for some reason	
	send_ok = totempg_send_ok (req_lib_evs_mcast_joined->msg_len); 

	res = totempg_mcast (req_exec_evs_mcast_iovec, 3, TOTEMPG_AGREED);
		// TODO
	if (res == 0) {
		error = EVS_OK;
	} else {
		printf ("res is wrong\n");
	}
error = EVS_OK;

	res_lib_evs_mcast_joined.header.size = sizeof (struct res_lib_evs_mcast_joined);
	res_lib_evs_mcast_joined.header.id = MESSAGE_RES_EVS_MCAST_JOINED;
	res_lib_evs_mcast_joined.header.error = error;

	libais_send_response (conn_info, &res_lib_evs_mcast_joined,
		sizeof (struct res_lib_evs_mcast_joined));

	return (0);
}

static int message_handler_req_evs_mcast_groups (struct conn_info *conn_info, void *message)
{
	evs_error_t error = EVS_ERR_TRY_AGAIN;
	struct req_lib_evs_mcast_groups *req_lib_evs_mcast_groups = (struct req_lib_evs_mcast_groups *)message;
	struct res_lib_evs_mcast_groups res_lib_evs_mcast_groups;
	struct iovec req_exec_evs_mcast_iovec[3];
	struct req_exec_evs_mcast req_exec_evs_mcast;
	char *msg_addr;
	int send_ok = 0;
	int res;

	req_exec_evs_mcast.header.size = sizeof (struct req_exec_evs_mcast) +
		sizeof (struct evs_group) * req_lib_evs_mcast_groups->group_entries +
		req_lib_evs_mcast_groups->msg_len;

	req_exec_evs_mcast.header.id = MESSAGE_REQ_EXEC_EVS_MCAST;
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
	send_ok = totempg_send_ok (req_lib_evs_mcast_groups->msg_len);
	res = totempg_mcast (req_exec_evs_mcast_iovec, 3, TOTEMPG_AGREED);
	if (res == 0) {
		error = EVS_OK;
	}

	res_lib_evs_mcast_groups.header.size = sizeof (struct res_lib_evs_mcast_groups);
	res_lib_evs_mcast_groups.header.id = MESSAGE_RES_EVS_MCAST_GROUPS;
	res_lib_evs_mcast_groups.header.error = error;

	libais_send_response (conn_info, &res_lib_evs_mcast_groups,
		sizeof (struct res_lib_evs_mcast_groups));

	return (0);
}
static int message_handler_req_evs_membership_get (struct conn_info *conn_info, void *message)
{
	struct res_lib_evs_membership_get res_lib_evs_membership_get;

	res_lib_evs_membership_get.header.size = sizeof (struct res_lib_evs_membership_get);
	res_lib_evs_membership_get.header.id = MESSAGE_RES_EVS_MEMBERSHIP_GET;
	res_lib_evs_membership_get.header.error = EVS_OK;
	res_lib_evs_membership_get.local_addr.s_addr = this_ip->sin_addr.s_addr;
	memcpy (&res_lib_evs_membership_get.member_list,
		&res_evs_confchg_callback.member_list,
		sizeof (res_lib_evs_membership_get.member_list));

	res_lib_evs_membership_get.member_list_entries =
		res_evs_confchg_callback.member_list_entries;

	libais_send_response (conn_info, &res_lib_evs_membership_get,
		sizeof (struct res_lib_evs_membership_get));

	return (0);
}

static int message_handler_req_exec_mcast (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_evs_mcast *req_exec_evs_mcast = (struct req_exec_evs_mcast *)message;
	struct res_evs_deliver_callback res_evs_deliver_callback;
	char *msg_addr;
	struct conn_info *conn_info;
	struct list_head *list;
	int found = 0;
	int i, j;

	res_evs_deliver_callback.header.size = sizeof (struct res_evs_deliver_callback) +
		req_exec_evs_mcast->msg_len;
	res_evs_deliver_callback.header.id = MESSAGE_RES_EVS_DELIVER_CALLBACK;
	res_evs_deliver_callback.header.error = SA_OK;
	res_evs_deliver_callback.msglen = req_exec_evs_mcast->msg_len;

	msg_addr = (char *)req_exec_evs_mcast + sizeof (struct req_exec_evs_mcast) + 
		(sizeof (struct evs_group) * req_exec_evs_mcast->group_entries);

	for (list = confchg_notify.next; list != &confchg_notify; list = list->next) {
		found = 0;
		conn_info = list_entry (list, struct conn_info, conn_list);

		for (i = 0; i < conn_info->ais_ci.u.libevs_ci.group_entries; i++) {
			for (j = 0; j < req_exec_evs_mcast->group_entries; j++) {
				if (memcmp (&conn_info->ais_ci.u.libevs_ci.groups[i],
					&req_exec_evs_mcast->groups[j],
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
			res_evs_deliver_callback.source_addr.s_addr = source_addr.s_addr;
			libais_send_response (conn_info, &res_evs_deliver_callback,
				sizeof (struct res_evs_deliver_callback));
			libais_send_response (conn_info, msg_addr,
				req_exec_evs_mcast->msg_len);
		}
	}
//TODO printf ("Got evs message %s\n", msg_addr);

	return (0);
}
