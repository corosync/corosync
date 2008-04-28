/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
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
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include "../include/saAis.h"
#include "../include/cfg.h"
#include "../include/mar_gen.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_cfg.h"
#include "../include/list.h"
#include "totem.h"
#include "totempg.h"
#include "flow.h"
#include "tlist.h"
#include "ipc.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "objdb.h"
#include "service.h"
#include "totempg.h"
#include "mempool.h"
#include "util.h"
#include "logsys.h"
#include "main.h"

LOGSYS_DECLARE_SUBSYS ("CFG", LOG_INFO);

enum cfg_message_req_types {
        MESSAGE_REQ_EXEC_CFG_RINGREENABLE = 0
};

static void cfg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static int cfg_exec_init_fn (struct objdb_iface_ver0 *objdb);

static int cfg_lib_init_fn (void *conn);

static int cfg_lib_exit_fn (void *conn);

static void message_handler_req_exec_cfg_ringreenable (
        void *message,
        unsigned int nodeid);

static void message_handler_req_lib_cfg_ringstatusget (
	void *conn,
	void *msg);

static void message_handler_req_lib_cfg_ringreenable (
	void *conn,
	void *msg);

static void message_handler_req_lib_cfg_statetrack (
	void *conn,
	void *msg);

static void message_handler_req_lib_cfg_statetrackstop (
	void *conn,
	void *msg);

static void message_handler_req_lib_cfg_administrativestateset (
	void *conn,
	void *msg);

static void message_handler_req_lib_cfg_administrativestateget (
	void *conn,
	void *msg);

static void message_handler_req_lib_cfg_serviceload (
	void *conn,
	void *msg);

static void message_handler_req_lib_cfg_serviceunload (
	void *conn,
	void *msg);

/*
 * Service Handler Definition
 */
static struct openais_lib_handler cfg_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_cfg_ringstatusget,
		.response_size		= sizeof (struct res_lib_cfg_ringstatusget),
		.response_id		= MESSAGE_RES_CFG_RINGSTATUSGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_cfg_ringreenable,
		.response_size		= sizeof (struct res_lib_cfg_ringreenable),
		.response_id		= MESSAGE_RES_CFG_RINGREENABLE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_cfg_statetrack,
		.response_size		= sizeof (struct res_lib_cfg_statetrack),
		.response_id		= MESSAGE_RES_CFG_STATETRACKSTART,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_cfg_statetrackstop,
		.response_size		= sizeof (struct res_lib_cfg_statetrackstop),
		.response_id		= MESSAGE_RES_CFG_STATETRACKSTOP,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_cfg_administrativestateset,
		.response_size		= sizeof (struct res_lib_cfg_administrativestateset),
		.response_id		= MESSAGE_RES_CFG_ADMINISTRATIVESTATESET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_cfg_administrativestateget,
		.response_size		= sizeof (struct res_lib_cfg_administrativestateget),
		.response_id		= MESSAGE_RES_CFG_ADMINISTRATIVESTATEGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_cfg_serviceload,
		.response_size		= sizeof (struct res_lib_cfg_serviceload),
		.response_id		= MESSAGE_RES_CFG_SERVICELOAD,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_cfg_serviceunload,
		.response_size		= sizeof (struct res_lib_cfg_serviceunload),
		.response_id		= MESSAGE_RES_CFG_SERVICEUNLOAD,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct openais_exec_handler cfg_exec_service[] =
{
	{
		message_handler_req_exec_cfg_ringreenable
	}
};

/*
 * Exports the interface for the service
 */
struct openais_service_handler cfg_service_handler = {
	.name					= "openais configuration service",
	.id					= CFG_SERVICE,
	.private_data_size			= 0,
	.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED, 
	.lib_init_fn				= cfg_lib_init_fn,
	.lib_exit_fn				= cfg_lib_exit_fn,
	.lib_service				= cfg_lib_service,
	.lib_service_count			= sizeof (cfg_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn				= cfg_exec_init_fn,
	.exec_service				= cfg_exec_service,
	.exec_service_count			= 0, /* sizeof (cfg_aisexec_handler_fns) / sizeof (openais_exec_handler), */
	.confchg_fn				= cfg_confchg_fn,
};

static struct objdb_iface_ver0 *my_objdb;

/*
 * Dynamic Loader definition
 */
static struct openais_service_handler *cfg_get_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 cfg_service_handler_iface = {
	.openais_get_service_handler_ver0	= cfg_get_handler_ver0
};

static struct lcr_iface openais_cfg_ver0[1] = {
	{
		.name				= "openais_cfg",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL
	}
};

static struct lcr_comp cfg_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= openais_cfg_ver0
};

static struct openais_service_handler *cfg_get_handler_ver0 (void)
{
	return (&cfg_service_handler);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_cfg_ver0[0], &cfg_service_handler_iface);

	lcr_component_register (&cfg_comp_ver0);
}

struct req_exec_cfg_ringreenable {
	mar_req_header_t header __attribute__((aligned(8)));
        mar_message_source_t source __attribute__((aligned(8)));
};

/* IMPL */

static int cfg_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	my_objdb = objdb;
	return (0);
}

static void cfg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
}

int cfg_lib_exit_fn (void *conn)
{
	return (0);
}

static int cfg_lib_init_fn (void *conn)
{
	
	ENTER("");
	LEAVE("");

        return (0);
}

/*
 * Executive message handlers
 */
static void message_handler_req_exec_cfg_ringreenable (
        void *message,
        unsigned int nodeid)
{
	struct req_exec_cfg_ringreenable *req_exec_cfg_ringreenable =
		(struct req_exec_cfg_ringreenable *)message;
	struct res_lib_cfg_ringreenable res_lib_cfg_ringreenable;

	ENTER("");
	totempg_ring_reenable ();
        if (message_source_is_local(&req_exec_cfg_ringreenable->source)) {
		res_lib_cfg_ringreenable.header.id = MESSAGE_RES_CFG_RINGREENABLE;
		res_lib_cfg_ringreenable.header.size = sizeof (struct res_lib_cfg_ringreenable);
		res_lib_cfg_ringreenable.header.error = SA_AIS_OK;
		openais_conn_send_response (
			req_exec_cfg_ringreenable->source.conn,
			&res_lib_cfg_ringreenable,
			sizeof (struct res_lib_cfg_ringreenable));
	}
	LEAVE("");
}


/*
 * Library Interface Implementation
 */
static void message_handler_req_lib_cfg_ringstatusget (
	void *conn,
	void *msg)
{
	struct res_lib_cfg_ringstatusget res_lib_cfg_ringstatusget;
	struct totem_ip_address interfaces[INTERFACE_MAX];
	unsigned int iface_count;
	char **status;
	char *totem_ip_string;
	unsigned int i;

	ENTER("");

	res_lib_cfg_ringstatusget.header.id = MESSAGE_RES_CFG_RINGSTATUSGET;
	res_lib_cfg_ringstatusget.header.size = sizeof (struct res_lib_cfg_ringstatusget);
	res_lib_cfg_ringstatusget.header.error = SA_AIS_OK;

	totempg_ifaces_get (
		totempg_my_nodeid_get(),
		interfaces,
		&status,
		&iface_count);

	res_lib_cfg_ringstatusget.interface_count = iface_count;

	for (i = 0; i < iface_count; i++) {
		totem_ip_string = (char *)totemip_print (&interfaces[i]);
		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_status[i],
			status[i]);
		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_name[i],
			totem_ip_string);
	}
	openais_conn_send_response (
		conn,
		&res_lib_cfg_ringstatusget,
		sizeof (struct res_lib_cfg_ringstatusget));

	LEAVE("");
}

static void message_handler_req_lib_cfg_ringreenable (
	void *conn,
	void *msg)
{
	struct req_exec_cfg_ringreenable req_exec_cfg_ringreenable;
	struct iovec iovec;

	ENTER("");
	req_exec_cfg_ringreenable.header.size =
		sizeof (struct req_exec_cfg_ringreenable);
	req_exec_cfg_ringreenable.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_RINGREENABLE);
	message_source_set (&req_exec_cfg_ringreenable.source, conn);

	iovec.iov_base = (char *)&req_exec_cfg_ringreenable;
	iovec.iov_len = sizeof (struct req_exec_cfg_ringreenable);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_SAFE) == 0);

	LEAVE("");
}

static void message_handler_req_lib_cfg_statetrack (
	void *conn,
	void *msg)
{
//	struct req_lib_cfg_statetrack *req_lib_cfg_statetrack = (struct req_lib_cfg_statetrack *)message;

	ENTER("");
	LEAVE("");
}

static void message_handler_req_lib_cfg_statetrackstop (
	void *conn,
	void *msg)
{
//	struct req_lib_cfg_statetrackstop *req_lib_cfg_statetrackstop = (struct req_lib_cfg_statetrackstop *)message;

	ENTER("");
	LEAVE("");
}

static void message_handler_req_lib_cfg_administrativestateset (
	void *conn,
	void *msg)
{
//	struct req_lib_cfg_administrativestateset *req_lib_cfg_administrativestateset = (struct req_lib_cfg_administrativestateset *)message;
	ENTER("");
	LEAVE("");
}
static void message_handler_req_lib_cfg_administrativestateget (
	void *conn,
	void *msg)
{
//	struct req_lib_cfg_administrativestateget *req_lib_cfg_administrativestateget = (struct req_lib_cfg_administrativestateget *)message;
	ENTER("");
	LEAVE("");
}

static void message_handler_req_lib_cfg_serviceload (
	void *conn,
	void *msg)
{
	struct req_lib_cfg_serviceload *req_lib_cfg_serviceload =
		(struct req_lib_cfg_serviceload *)msg;
	struct res_lib_cfg_serviceload res_lib_cfg_serviceload;

	ENTER("");
	openais_service_link_and_init (
		my_objdb,
		(char *)req_lib_cfg_serviceload->service_name,
		req_lib_cfg_serviceload->service_ver);

	res_lib_cfg_serviceload.header.id = MESSAGE_RES_CFG_SERVICEUNLOAD;
	res_lib_cfg_serviceload.header.size = sizeof (struct res_lib_cfg_serviceload);
	res_lib_cfg_serviceload.header.error = SA_AIS_OK;
	openais_conn_send_response (
		conn,
		&res_lib_cfg_serviceload,
		sizeof (struct res_lib_cfg_serviceload));
	LEAVE("");
}

static void message_handler_req_lib_cfg_serviceunload (
	void *conn,
	void *msg)
{
	struct req_lib_cfg_serviceunload *req_lib_cfg_serviceunload =
		(struct req_lib_cfg_serviceunload *)msg;
	struct res_lib_cfg_serviceunload res_lib_cfg_serviceunload;

	ENTER("");
	openais_service_unlink_and_exit (
		my_objdb,
		(char *)req_lib_cfg_serviceunload->service_name,
		req_lib_cfg_serviceunload->service_ver);
	res_lib_cfg_serviceunload.header.id = MESSAGE_RES_CFG_SERVICEUNLOAD;
	res_lib_cfg_serviceunload.header.size = sizeof (struct res_lib_cfg_serviceunload);
	res_lib_cfg_serviceunload.header.error = SA_AIS_OK;
	openais_conn_send_response (
		conn,
		&res_lib_cfg_serviceunload,
		sizeof (struct res_lib_cfg_serviceunload));
	LEAVE("");
}
