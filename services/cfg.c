/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
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
#include <limits.h>
#include <errno.h>
#include <string.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/cfg.h>
#include <corosync/list.h>
#include <corosync/mar_gen.h>
#include <corosync/totem/totemip.h>
#include <corosync/totem/totem.h>
#include <corosync/ipc_cfg.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/logsys.h>
#include <corosync/engine/coroapi.h>
#include <corosync/corodefs.h>

LOGSYS_DECLARE_SUBSYS ("CFG");

enum cfg_message_req_types {
        MESSAGE_REQ_EXEC_CFG_RINGREENABLE = 0,
	MESSAGE_REQ_EXEC_CFG_KILLNODE = 1,
	MESSAGE_REQ_EXEC_CFG_SHUTDOWN = 2,
	MESSAGE_REQ_EXEC_CFG_CRYPTO_SET = 3
};

#define DEFAULT_SHUTDOWN_TIMEOUT 5

static struct list_head trackers_list;

/*
 * Variables controlling a requested shutdown
 */
static corosync_timer_handle_t shutdown_timer;
static struct cfg_info *shutdown_con;
static uint32_t shutdown_flags;
static int shutdown_yes;
static int shutdown_no;
static int shutdown_expected;

struct cfg_info
{
	struct list_head list;
	void *conn;
	void *tracker_conn;
	enum {SHUTDOWN_REPLY_UNKNOWN, SHUTDOWN_REPLY_YES, SHUTDOWN_REPLY_NO} shutdown_reply;
};

static void cfg_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

static int cfg_exec_init_fn (struct corosync_api_v1 *corosync_api_v1);

static struct corosync_api_v1 *api;

static int cfg_lib_init_fn (void *conn);

static int cfg_lib_exit_fn (void *conn);

static void message_handler_req_exec_cfg_ringreenable (
        const void *message,
        unsigned int nodeid);

static void message_handler_req_exec_cfg_killnode (
        const void *message,
        unsigned int nodeid);

static void message_handler_req_exec_cfg_shutdown (
        const void *message,
        unsigned int nodeid);

static void message_handler_req_exec_cfg_crypto_set (
        const void *message,
        unsigned int nodeid);

static void exec_cfg_killnode_endian_convert (void *msg);

static void message_handler_req_lib_cfg_ringstatusget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_ringreenable (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_statetrack (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_statetrackstop (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_administrativestateset (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_administrativestateget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_serviceload (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_serviceunload (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_killnode (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_tryshutdown (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_replytoshutdown (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_get_node_addrs (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_local_get (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_crypto_set (
	void *conn,
	const void *msg);

/*
 * Service Handler Definition
 */
static struct corosync_lib_handler cfg_lib_engine[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_cfg_ringstatusget,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_cfg_ringreenable,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_cfg_statetrack,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_cfg_statetrackstop,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_cfg_administrativestateset,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_cfg_administrativestateget,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_cfg_serviceload,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_cfg_serviceunload,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_cfg_killnode,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_cfg_tryshutdown,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_cfg_replytoshutdown,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_cfg_get_node_addrs,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn		= message_handler_req_lib_cfg_local_get,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn		= message_handler_req_lib_cfg_crypto_set,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_exec_handler cfg_exec_engine[] =
{
	{ /* 0 */
		.exec_handler_fn = message_handler_req_exec_cfg_ringreenable,
	},
	{ /* 1 */
		.exec_handler_fn = message_handler_req_exec_cfg_killnode,
		.exec_endian_convert_fn	= exec_cfg_killnode_endian_convert
	},
	{ /* 2 */
		.exec_handler_fn = message_handler_req_exec_cfg_shutdown,
	},
	{ /* 3 */
		.exec_handler_fn = message_handler_req_exec_cfg_crypto_set,
	}
};

/*
 * Exports the interface for the service
 */
struct corosync_service_engine cfg_service_engine = {
	.name					= "corosync configuration service",
	.id					= CFG_SERVICE,
	.priority				= 1,
	.private_data_size			= sizeof(struct cfg_info),
	.flow_control				= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
	.lib_init_fn				= cfg_lib_init_fn,
	.lib_exit_fn				= cfg_lib_exit_fn,
	.lib_engine				= cfg_lib_engine,
	.lib_engine_count			= sizeof (cfg_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn				= cfg_exec_init_fn,
	.exec_engine				= cfg_exec_engine,
	.exec_engine_count			= sizeof (cfg_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn				= cfg_confchg_fn,
	.sync_mode				= CS_SYNC_V1
};

/*
 * Dynamic Loader definition
 */
static struct corosync_service_engine *cfg_get_service_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 cfg_service_engine_iface = {
	.corosync_get_service_engine_ver0	= cfg_get_service_engine_ver0
};

static struct lcr_iface corosync_cfg_ver0[1] = {
	{
		.name				= "corosync_cfg",
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
	.ifaces					= corosync_cfg_ver0
};

static struct corosync_service_engine *cfg_get_service_engine_ver0 (void)
{
	return (&cfg_service_engine);
}

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
	lcr_interfaces_set (&corosync_cfg_ver0[0], &cfg_service_engine_iface);

	lcr_component_register (&cfg_comp_ver0);
}

struct req_exec_cfg_ringreenable {
	coroipc_request_header_t header __attribute__((aligned(8)));
        mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_cfg_killnode {
	coroipc_request_header_t header __attribute__((aligned(8)));
        mar_uint32_t nodeid __attribute__((aligned(8)));
	mar_name_t reason __attribute__((aligned(8)));
};

struct req_exec_cfg_crypto_set {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint32_t type __attribute__((aligned(8)));
};

struct req_exec_cfg_shutdown {
	coroipc_request_header_t header __attribute__((aligned(8)));
};

/* IMPL */

static int cfg_exec_init_fn (
	struct corosync_api_v1 *corosync_api_v1)
{
#ifdef COROSYNC_SOLARIS
	logsys_subsys_init();
#endif

	api = corosync_api_v1;

	list_init(&trackers_list);
	return (0);
}

static void cfg_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
}

/*
 * Tell other nodes we are shutting down
 */
static int send_shutdown(void)
{
	struct req_exec_cfg_shutdown req_exec_cfg_shutdown;
	struct iovec iovec;
	int result;

	ENTER();
	req_exec_cfg_shutdown.header.size =
		sizeof (struct req_exec_cfg_shutdown);
	req_exec_cfg_shutdown.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_SHUTDOWN);

	iovec.iov_base = (char *)&req_exec_cfg_shutdown;
	iovec.iov_len = sizeof (struct req_exec_cfg_shutdown);

	result = api->totem_mcast (&iovec, 1, TOTEM_SAFE);

	LEAVE();
	return (result);
}

static void send_test_shutdown(void *only_conn, void *exclude_conn, int status)
{
	struct res_lib_cfg_testshutdown res_lib_cfg_testshutdown;
	struct list_head *iter;

	ENTER();
	res_lib_cfg_testshutdown.header.size = sizeof(struct res_lib_cfg_testshutdown);
	res_lib_cfg_testshutdown.header.id = MESSAGE_RES_CFG_TESTSHUTDOWN;
	res_lib_cfg_testshutdown.header.error = status;
	res_lib_cfg_testshutdown.flags = shutdown_flags;

	if (only_conn) {
		TRACE1("sending testshutdown to only %p", only_conn);
		api->ipc_dispatch_send(only_conn, &res_lib_cfg_testshutdown,
				       sizeof(res_lib_cfg_testshutdown));
	} else {
		for (iter = trackers_list.next; iter != &trackers_list; iter = iter->next) {
			struct cfg_info *ci = list_entry(iter, struct cfg_info, list);

			if (ci->conn != exclude_conn) {
				TRACE1("sending testshutdown to %p", ci->tracker_conn);
				api->ipc_dispatch_send(ci->tracker_conn, &res_lib_cfg_testshutdown,
						       sizeof(res_lib_cfg_testshutdown));
			}
		}
	}
	LEAVE();
}

static void check_shutdown_status(void)
{
	int result;
	cs_error_t error = CS_OK;

	ENTER();

	/*
	 * Shutdown client might have gone away
	 */
	if (!shutdown_con) {
		LEAVE();
		return;
	}

	/*
	 * All replies safely gathered in ?
	 */
	if (shutdown_yes + shutdown_no >= shutdown_expected) {
		struct res_lib_cfg_tryshutdown res_lib_cfg_tryshutdown;

		api->timer_delete(shutdown_timer);

		if (shutdown_yes >= shutdown_expected ||
		    shutdown_flags == CFG_SHUTDOWN_FLAG_REGARDLESS) {
			TRACE1("shutdown confirmed");

			/*
			 * Tell other nodes we are going down
			 */
			result = send_shutdown();
			if (result == -1) {
				error = CS_ERR_TRY_AGAIN;
			}

			res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
			res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
			res_lib_cfg_tryshutdown.header.error = error;

			/*
			 * Tell originator that shutdown was confirmed
			 */
			api->ipc_response_send(shutdown_con->conn, &res_lib_cfg_tryshutdown,
						    sizeof(res_lib_cfg_tryshutdown));
			shutdown_con = NULL;


		}
		else {

			TRACE1("shutdown cancelled");
			res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
			res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
			res_lib_cfg_tryshutdown.header.error = CS_ERR_BUSY;

			/*
			 * Tell originator that shutdown was cancelled
			 */
			api->ipc_response_send(shutdown_con->conn, &res_lib_cfg_tryshutdown,
						    sizeof(res_lib_cfg_tryshutdown));
			shutdown_con = NULL;
		}

		log_printf(LOGSYS_LEVEL_DEBUG, "shutdown decision is: (yes count: %d, no count: %d) flags=%x\n", shutdown_yes, shutdown_no, shutdown_flags);
	}
	LEAVE();
}


/*
 * Not all nodes responded to the shutdown (in time)
 */
static void shutdown_timer_fn(void *arg)
{
	ENTER();

	/*
	 * Mark undecideds as "NO"
	 */
	shutdown_no = shutdown_expected;
	check_shutdown_status();

	send_test_shutdown(NULL, NULL, CS_ERR_TIMEOUT);
	LEAVE();
}

static void remove_ci_from_shutdown(struct cfg_info *ci)
{
	ENTER();

	/*
	 * If the controlling shutdown process has quit, then cancel the
	 * shutdown session
	 */
	if (ci == shutdown_con) {
		shutdown_con = NULL;
		api->timer_delete(shutdown_timer);
	}

	if (!list_empty(&ci->list)) {
		list_del(&ci->list);
		list_init(&ci->list);

		/*
		 * Remove our option
		 */
		if (shutdown_con) {
			if (ci->shutdown_reply == SHUTDOWN_REPLY_YES)
				shutdown_yes--;
			if (ci->shutdown_reply == SHUTDOWN_REPLY_NO)
				shutdown_no--;
		}

		/*
		 * If we are leaving, then that's an implicit YES to shutdown
		 */
		ci->shutdown_reply = SHUTDOWN_REPLY_YES;
		shutdown_yes++;

		check_shutdown_status();
	}
	LEAVE();
}


int cfg_lib_exit_fn (void *conn)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);

	ENTER();
	remove_ci_from_shutdown(ci);
	LEAVE();
	return (0);
}

static int cfg_lib_init_fn (void *conn)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);

	ENTER();
	list_init(&ci->list);
	LEAVE();

        return (0);
}

/*
 * Executive message handlers
 */
static void message_handler_req_exec_cfg_ringreenable (
        const void *message,
        unsigned int nodeid)
{
	const struct req_exec_cfg_ringreenable *req_exec_cfg_ringreenable
	  = message;
	struct res_lib_cfg_ringreenable res_lib_cfg_ringreenable;

	ENTER();
	api->totem_ring_reenable ();
        if (api->ipc_source_is_local(&req_exec_cfg_ringreenable->source)) {
		res_lib_cfg_ringreenable.header.id = MESSAGE_RES_CFG_RINGREENABLE;
		res_lib_cfg_ringreenable.header.size = sizeof (struct res_lib_cfg_ringreenable);
		res_lib_cfg_ringreenable.header.error = CS_OK;
		api->ipc_response_send (
			req_exec_cfg_ringreenable->source.conn,
			&res_lib_cfg_ringreenable,
			sizeof (struct res_lib_cfg_ringreenable));

		api->ipc_refcnt_dec(req_exec_cfg_ringreenable->source.conn);
	}
	LEAVE();
}

static void exec_cfg_killnode_endian_convert (void *msg)
{
	struct req_exec_cfg_killnode *req_exec_cfg_killnode =
		(struct req_exec_cfg_killnode *)msg;
	ENTER();

	swab_mar_name_t(&req_exec_cfg_killnode->reason);
	LEAVE();
}


static void message_handler_req_exec_cfg_killnode (
        const void *message,
        unsigned int nodeid)
{
	const struct req_exec_cfg_killnode *req_exec_cfg_killnode = message;
	cs_name_t reason;

	ENTER();
	log_printf(LOGSYS_LEVEL_DEBUG, "request to kill node %d(us=%d): %s\n",  req_exec_cfg_killnode->nodeid, api->totem_nodeid_get(), reason.value);
        if (req_exec_cfg_killnode->nodeid == api->totem_nodeid_get()) {
		marshall_from_mar_name_t(&reason, &req_exec_cfg_killnode->reason);
		log_printf(LOGSYS_LEVEL_NOTICE, "Killed by node %d: %s\n",
			   nodeid, reason.value);
		corosync_fatal_error(COROSYNC_FATAL_ERROR_EXIT);
	}
	LEAVE();
}

/*
 * Self shutdown
 */
static void message_handler_req_exec_cfg_shutdown (
        const void *message,
        unsigned int nodeid)
{
	ENTER();

	log_printf(LOGSYS_LEVEL_NOTICE, "Node %d was shut down by sysadmin\n", nodeid);
	if (nodeid == api->totem_nodeid_get()) {
		api->shutdown_request();
	}
	LEAVE();
}

static void message_handler_req_exec_cfg_crypto_set (
        const void *message,
        unsigned int nodeid)
{
	const struct req_exec_cfg_crypto_set *req_exec_cfg_crypto_set = message;
	ENTER();

	log_printf(LOGSYS_LEVEL_NOTICE, "Node %d requested set crypto to %d\n", nodeid, req_exec_cfg_crypto_set->type);

	api->totem_crypto_set(req_exec_cfg_crypto_set->type);
	LEAVE();
}


/*
 * Library Interface Implementation
 */
static void message_handler_req_lib_cfg_ringstatusget (
	void *conn,
	const void *msg)
{
	struct res_lib_cfg_ringstatusget res_lib_cfg_ringstatusget;
	struct totem_ip_address interfaces[INTERFACE_MAX];
	unsigned int iface_count;
	char **status;
	const char *totem_ip_string;
	unsigned int i;

	ENTER();

	res_lib_cfg_ringstatusget.header.id = MESSAGE_RES_CFG_RINGSTATUSGET;
	res_lib_cfg_ringstatusget.header.size = sizeof (struct res_lib_cfg_ringstatusget);
	res_lib_cfg_ringstatusget.header.error = CS_OK;

	api->totem_ifaces_get (
		api->totem_nodeid_get(),
		interfaces,
		&status,
		&iface_count);

	res_lib_cfg_ringstatusget.interface_count = iface_count;

	for (i = 0; i < iface_count; i++) {
		totem_ip_string
		  = (const char *)api->totem_ip_print (&interfaces[i]);
		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_status[i],
			status[i]);
		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_name[i],
			totem_ip_string);
	}
	api->ipc_response_send (
		conn,
		&res_lib_cfg_ringstatusget,
		sizeof (struct res_lib_cfg_ringstatusget));

	LEAVE();
}

static void message_handler_req_lib_cfg_ringreenable (
	void *conn,
	const void *msg)
{
	struct req_exec_cfg_ringreenable req_exec_cfg_ringreenable;
	struct res_lib_cfg_ringreenable res_lib_cfg_ringreenable;
	struct iovec iovec;
	int result;

	ENTER();
	req_exec_cfg_ringreenable.header.size =
		sizeof (struct req_exec_cfg_ringreenable);
	req_exec_cfg_ringreenable.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_RINGREENABLE);
	api->ipc_source_set (&req_exec_cfg_ringreenable.source, conn);
	api->ipc_refcnt_inc(conn);

	iovec.iov_base = (char *)&req_exec_cfg_ringreenable;
	iovec.iov_len = sizeof (struct req_exec_cfg_ringreenable);

	result = api->totem_mcast (&iovec, 1, TOTEM_SAFE);

	if (result == -1) {
		res_lib_cfg_ringreenable.header.id = MESSAGE_RES_CFG_RINGREENABLE;
		res_lib_cfg_ringreenable.header.size = sizeof (struct res_lib_cfg_ringreenable);
		res_lib_cfg_ringreenable.header.error = CS_ERR_TRY_AGAIN;
		api->ipc_response_send (
			conn,
			&res_lib_cfg_ringreenable,
			sizeof (struct res_lib_cfg_ringreenable));

		api->ipc_refcnt_dec(conn);
	}

	LEAVE();
}

static void message_handler_req_lib_cfg_statetrack (
	void *conn,
	const void *msg)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);
	struct res_lib_cfg_statetrack res_lib_cfg_statetrack;

	ENTER();

	/*
	 * We only do shutdown tracking at the moment
	 */
	if (list_empty(&ci->list)) {
		list_add(&ci->list, &trackers_list);
		ci->tracker_conn = conn;

		if (shutdown_con) {
			/*
			 * Shutdown already in progress, ask the newcomer's opinion
			 */
			ci->shutdown_reply = SHUTDOWN_REPLY_UNKNOWN;
			shutdown_expected++;
			send_test_shutdown(conn, NULL, CS_OK);
		}
	}

	res_lib_cfg_statetrack.header.size = sizeof(struct res_lib_cfg_statetrack);
	res_lib_cfg_statetrack.header.id = MESSAGE_RES_CFG_STATETRACKSTART;
	res_lib_cfg_statetrack.header.error = CS_OK;

	api->ipc_response_send(conn, &res_lib_cfg_statetrack,
				    sizeof(res_lib_cfg_statetrack));

	LEAVE();
}

static void message_handler_req_lib_cfg_statetrackstop (
	void *conn,
	const void *msg)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);
//	struct req_lib_cfg_statetrackstop *req_lib_cfg_statetrackstop = (struct req_lib_cfg_statetrackstop *)message;

	ENTER();
	remove_ci_from_shutdown(ci);
	LEAVE();
}

static void message_handler_req_lib_cfg_administrativestateset (
	void *conn,
	const void *msg)
{
//	struct req_lib_cfg_administrativestateset *req_lib_cfg_administrativestateset = (struct req_lib_cfg_administrativestateset *)message;

	ENTER();
	LEAVE();
}
static void message_handler_req_lib_cfg_administrativestateget (
	void *conn,
	const void *msg)
{
//	struct req_lib_cfg_administrativestateget *req_lib_cfg_administrativestateget = (struct req_lib_cfg_administrativestateget *)message;
	ENTER();
	LEAVE();
}

static void message_handler_req_lib_cfg_serviceload (
	void *conn,
	const void *msg)
{
	const struct req_lib_cfg_serviceload *req_lib_cfg_serviceload = msg;
	struct res_lib_cfg_serviceload res_lib_cfg_serviceload;

	ENTER();
	api->service_link_and_init (
		api,
		(const char *)req_lib_cfg_serviceload->service_name,
		req_lib_cfg_serviceload->service_ver);

	res_lib_cfg_serviceload.header.id = MESSAGE_RES_CFG_SERVICEUNLOAD;
	res_lib_cfg_serviceload.header.size = sizeof (struct res_lib_cfg_serviceload);
	res_lib_cfg_serviceload.header.error = CS_OK;
	api->ipc_response_send (
		conn,
		&res_lib_cfg_serviceload,
		sizeof (struct res_lib_cfg_serviceload));
	LEAVE();
}

static void message_handler_req_lib_cfg_serviceunload (
	void *conn,
	const void *msg)
{
	const struct req_lib_cfg_serviceunload *req_lib_cfg_serviceunload = msg;
	struct res_lib_cfg_serviceunload res_lib_cfg_serviceunload;

	ENTER();
	api->service_unlink_and_exit (
		api,
		(const char *)req_lib_cfg_serviceunload->service_name,
		req_lib_cfg_serviceunload->service_ver);
	res_lib_cfg_serviceunload.header.id = MESSAGE_RES_CFG_SERVICEUNLOAD;
	res_lib_cfg_serviceunload.header.size = sizeof (struct res_lib_cfg_serviceunload);
	res_lib_cfg_serviceunload.header.error = CS_OK;
	api->ipc_response_send (
		conn,
		&res_lib_cfg_serviceunload,
		sizeof (struct res_lib_cfg_serviceunload));
	LEAVE();
}


static void message_handler_req_lib_cfg_killnode (
	void *conn,
	const void *msg)
{
	const struct req_lib_cfg_killnode *req_lib_cfg_killnode = msg;
	struct res_lib_cfg_killnode res_lib_cfg_killnode;
	struct req_exec_cfg_killnode req_exec_cfg_killnode;
	struct iovec iovec;
	int result;
	cs_error_t error = CS_OK;

	ENTER();
	req_exec_cfg_killnode.header.size =
		sizeof (struct req_exec_cfg_killnode);
	req_exec_cfg_killnode.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_KILLNODE);
	req_exec_cfg_killnode.nodeid = req_lib_cfg_killnode->nodeid;
	marshall_to_mar_name_t(&req_exec_cfg_killnode.reason, &req_lib_cfg_killnode->reason);

	iovec.iov_base = (char *)&req_exec_cfg_killnode;
	iovec.iov_len = sizeof (struct req_exec_cfg_killnode);

	result = api->totem_mcast (&iovec, 1, TOTEM_SAFE);
	if (result == -1) {
		error = CS_ERR_TRY_AGAIN;
	}

	res_lib_cfg_killnode.header.size = sizeof(struct res_lib_cfg_killnode);
	res_lib_cfg_killnode.header.id = MESSAGE_RES_CFG_KILLNODE;
	res_lib_cfg_killnode.header.error = error;

	api->ipc_response_send(conn, &res_lib_cfg_killnode,
				    sizeof(res_lib_cfg_killnode));

	LEAVE();
}


static void message_handler_req_lib_cfg_tryshutdown (
	void *conn,
	const void *msg)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);
	const struct req_lib_cfg_tryshutdown *req_lib_cfg_tryshutdown = msg;
	struct list_head *iter;
	int result;
	cs_error_t error = CS_OK;

	ENTER();

	if (req_lib_cfg_tryshutdown->flags == CFG_SHUTDOWN_FLAG_IMMEDIATE) {
		struct res_lib_cfg_tryshutdown res_lib_cfg_tryshutdown;

		/*
		 * Tell other nodes
		 */
		result = send_shutdown();
		if (result == -1) {
			error = CS_ERR_TRY_AGAIN;
		}

		res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
		res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
		res_lib_cfg_tryshutdown.header.error = error;
		api->ipc_response_send(conn, &res_lib_cfg_tryshutdown,
					    sizeof(res_lib_cfg_tryshutdown));

		LEAVE();
		return;
	}

	/*
	 * Shutdown in progress, return an error
	 */
	if (shutdown_con) {
		struct res_lib_cfg_tryshutdown res_lib_cfg_tryshutdown;

		res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
		res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
		res_lib_cfg_tryshutdown.header.error = CS_ERR_EXIST;

		api->ipc_response_send(conn, &res_lib_cfg_tryshutdown,
					    sizeof(res_lib_cfg_tryshutdown));


		LEAVE();

		return;
	}

	ci->conn = conn;
	shutdown_con = (struct cfg_info *)api->ipc_private_data_get (conn);
	shutdown_flags = req_lib_cfg_tryshutdown->flags;
	shutdown_yes = 0;
	shutdown_no = 0;

	/*
	 * Count the number of listeners
	 */
	shutdown_expected = 0;

	for (iter = trackers_list.next; iter != &trackers_list; iter = iter->next) {
		struct cfg_info *testci = list_entry(iter, struct cfg_info, list);
		/*
		 * It is assumed that we will allow shutdown
		 */
		if (testci != ci) {
			testci->shutdown_reply = SHUTDOWN_REPLY_UNKNOWN;
			shutdown_expected++;
		}
	}

	/*
	 * If no-one is listening for events then we can just go down now
	 */
	if (shutdown_expected == 0) {
		struct res_lib_cfg_tryshutdown res_lib_cfg_tryshutdown;

		result = send_shutdown();
		if (result == -1) {
			error = CS_ERR_TRY_AGAIN;
			shutdown_con = NULL;
		}

		res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
		res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
		res_lib_cfg_tryshutdown.header.error = error;

		/*
		 * Tell originator that shutdown was confirmed
		 */
		api->ipc_response_send(conn, &res_lib_cfg_tryshutdown,
				       sizeof(res_lib_cfg_tryshutdown));

		LEAVE();
		return;
	}
	else {
		hdb_handle_t cfg_handle;
		hdb_handle_t find_handle;
		char *timeout_str;
		unsigned int shutdown_timeout = DEFAULT_SHUTDOWN_TIMEOUT;

		/*
		 * Look for a shutdown timeout in objdb
		 */
		api->object_find_create(OBJECT_PARENT_HANDLE, "cfg", strlen("cfg"), &find_handle);
		api->object_find_next(find_handle, &cfg_handle);
		api->object_find_destroy(find_handle);

		if (cfg_handle) {
			if ( !api->object_key_get(cfg_handle,
						  "shutdown_timeout",
						  strlen("shutdown_timeout"),
						  (void *)&timeout_str,
						  NULL)) {
				shutdown_timeout = atoi(timeout_str);
			}
		}

		/*
		 * Start the timer. If we don't get a full set of replies before this goes
		 * off we'll cancel the shutdown
		 */
		api->timer_add_duration((unsigned long long)shutdown_timeout*1000000000, NULL,
					shutdown_timer_fn, &shutdown_timer);

		/*
		 * Tell the users we would like to shut down
		 */
		send_test_shutdown(NULL, conn, CS_OK);
	}

	/*
	 * We don't sent a reply to the caller here.
	 * We send it when we know if we can shut down or not
	 */

	LEAVE();
}

static void message_handler_req_lib_cfg_replytoshutdown (
	void *conn,
	const void *msg)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);
	const struct req_lib_cfg_replytoshutdown *req_lib_cfg_replytoshutdown = msg;
	struct res_lib_cfg_replytoshutdown res_lib_cfg_replytoshutdown;
	int status = CS_OK;

	ENTER();
	if (!shutdown_con) {
		status = CS_ERR_ACCESS;
		goto exit_fn;
	}

	if (req_lib_cfg_replytoshutdown->response) {
		shutdown_yes++;
		ci->shutdown_reply = SHUTDOWN_REPLY_YES;
	}
	else {
		shutdown_no++;
		ci->shutdown_reply = SHUTDOWN_REPLY_NO;
	}
	check_shutdown_status();

exit_fn:
	res_lib_cfg_replytoshutdown.header.error = status;
	res_lib_cfg_replytoshutdown.header.id = MESSAGE_RES_CFG_REPLYTOSHUTDOWN;
	res_lib_cfg_replytoshutdown.header.size = sizeof(res_lib_cfg_replytoshutdown);

	api->ipc_response_send(conn, &res_lib_cfg_replytoshutdown,
			       sizeof(res_lib_cfg_replytoshutdown));

	LEAVE();
}

static void message_handler_req_lib_cfg_get_node_addrs (void *conn,
							const void *msg)
{
	struct totem_ip_address node_ifs[INTERFACE_MAX];
	char buf[PIPE_BUF];
	char **status;
	unsigned int num_interfaces = 0;
	int ret = CS_OK;
	int i;
	const struct req_lib_cfg_get_node_addrs *req_lib_cfg_get_node_addrs = msg;
	struct res_lib_cfg_get_node_addrs *res_lib_cfg_get_node_addrs = (struct res_lib_cfg_get_node_addrs *)buf;
	unsigned int nodeid = req_lib_cfg_get_node_addrs->nodeid;
	char *addr_buf;

	if (nodeid == 0)
		nodeid = api->totem_nodeid_get();

	api->totem_ifaces_get(nodeid, node_ifs, &status, &num_interfaces);

	res_lib_cfg_get_node_addrs->header.size = sizeof(struct res_lib_cfg_get_node_addrs) + (num_interfaces * TOTEMIP_ADDRLEN);
	res_lib_cfg_get_node_addrs->header.id = MESSAGE_RES_CFG_GET_NODE_ADDRS;
	res_lib_cfg_get_node_addrs->header.error = ret;
	res_lib_cfg_get_node_addrs->num_addrs = num_interfaces;
	if (num_interfaces) {
		res_lib_cfg_get_node_addrs->family = node_ifs[0].family;
		for (i = 0, addr_buf = (char *)res_lib_cfg_get_node_addrs->addrs;
		    i < num_interfaces; i++, addr_buf += TOTEMIP_ADDRLEN) {
			memcpy(addr_buf, node_ifs[i].addr, TOTEMIP_ADDRLEN);
		}
	}
	else {
		res_lib_cfg_get_node_addrs->header.error = CS_ERR_NOT_EXIST;
	}
	api->ipc_response_send(conn, res_lib_cfg_get_node_addrs, res_lib_cfg_get_node_addrs->header.size);
}

static void message_handler_req_lib_cfg_local_get (void *conn, const void *msg)
{
	struct res_lib_cfg_local_get res_lib_cfg_local_get;

	res_lib_cfg_local_get.header.size = sizeof(res_lib_cfg_local_get);
	res_lib_cfg_local_get.header.id = MESSAGE_RES_CFG_LOCAL_GET;
	res_lib_cfg_local_get.header.error = CS_OK;
	res_lib_cfg_local_get.local_nodeid = api->totem_nodeid_get ();

	api->ipc_response_send(conn, &res_lib_cfg_local_get,
		sizeof(res_lib_cfg_local_get));
}


static void message_handler_req_lib_cfg_crypto_set (
	void *conn,
	const void *msg)
{
	const struct req_lib_cfg_crypto_set *req_lib_cfg_crypto_set = msg;
	struct res_lib_cfg_crypto_set res_lib_cfg_crypto_set;
	struct req_exec_cfg_crypto_set req_exec_cfg_crypto_set;
	struct iovec iovec;
	cs_error_t error = CS_ERR_INVALID_PARAM;
	int result;

	req_exec_cfg_crypto_set.header.size =
		sizeof (struct req_exec_cfg_crypto_set);
	req_exec_cfg_crypto_set.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_CRYPTO_SET);

	/*
	 * Set it locally first so we can tell if it is allowed
	 */
	if (api->totem_crypto_set(req_lib_cfg_crypto_set->type) == 0) {

		req_exec_cfg_crypto_set.type = req_lib_cfg_crypto_set->type;

		iovec.iov_base = (char *)&req_exec_cfg_crypto_set;
		iovec.iov_len = sizeof (struct req_exec_cfg_crypto_set);
		result = api->totem_mcast (&iovec, 1, TOTEM_SAFE);
		if (result == -1) {
			error = CS_ERR_TRY_AGAIN;
		} else {
			error = CS_OK;
		}
	}

	res_lib_cfg_crypto_set.header.size = sizeof(res_lib_cfg_crypto_set);
	res_lib_cfg_crypto_set.header.id = MESSAGE_RES_CFG_CRYPTO_SET;
	res_lib_cfg_crypto_set.header.error = error;

	api->ipc_response_send(conn, &res_lib_cfg_crypto_set,
		sizeof(res_lib_cfg_crypto_set));
}
