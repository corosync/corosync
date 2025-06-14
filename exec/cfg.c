/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2018 Red Hat, Inc.
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
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <corosync/corotypes.h>
#include <qb/qbipc_common.h>
#include <corosync/cfg.h>
#include <qb/qblist.h>
#include <qb/qbutil.h>
#include <corosync/mar_gen.h>
#include <corosync/totem/totemip.h>
#include <corosync/totem/totem.h>
#include <corosync/ipc_cfg.h>
#include <corosync/logsys.h>
#include <corosync/coroapi.h>
#include <corosync/icmap.h>
#include <corosync/corodefs.h>

#include "totemconfig.h"
#include "totemknet.h"
#include "service.h"
#include "main.h"

LOGSYS_DECLARE_SUBSYS ("CFG");

enum cfg_message_req_types {
        MESSAGE_REQ_EXEC_CFG_RINGREENABLE = 0,
	MESSAGE_REQ_EXEC_CFG_KILLNODE = 1,
	MESSAGE_REQ_EXEC_CFG_SHUTDOWN = 2,
	MESSAGE_REQ_EXEC_CFG_RELOAD_CONFIG = 3,
	MESSAGE_REQ_EXEC_CFG_CRYPTO_RECONFIG = 4
};

/* in milliseconds */
#define DEFAULT_SHUTDOWN_TIMEOUT 5000

static struct qb_list_head trackers_list;

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
	struct qb_list_head list;
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

static char *cfg_exec_init_fn (struct corosync_api_v1 *corosync_api_v1);

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

static void message_handler_req_exec_cfg_reload_config (
        const void *message,
        unsigned int nodeid);

static void message_handler_req_exec_cfg_reconfig_crypto (
        const void *message,
        unsigned int nodeid);

static void exec_cfg_killnode_endian_convert (void *msg);

static void message_handler_req_lib_cfg_ringstatusget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_nodestatusget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_ringreenable (
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

static void message_handler_req_lib_cfg_trackstart (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_trackstop (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_get_node_addrs (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_local_get (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_reload_config (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_reopen_log_files (
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
		.lib_handler_fn		= message_handler_req_lib_cfg_killnode,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_cfg_tryshutdown,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_cfg_replytoshutdown,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_cfg_get_node_addrs,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_cfg_local_get,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_cfg_reload_config,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_cfg_reopen_log_files,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_cfg_nodestatusget,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_cfg_trackstart,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_cfg_trackstop,
		.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED
	},

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
		.exec_handler_fn = message_handler_req_exec_cfg_reload_config,
	},
	{ /* 4 */
		.exec_handler_fn = message_handler_req_exec_cfg_reconfig_crypto,
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
	.confchg_fn				= cfg_confchg_fn
};

struct corosync_service_engine *cfg_get_service_engine_ver0 (void)
{
	return (&cfg_service_engine);
}

struct req_exec_cfg_ringreenable {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
        mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_cfg_reload_config {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_cfg_crypto_reconfig {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_uint32_t phase __attribute__((aligned(8)));
};

struct req_exec_cfg_killnode {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
        mar_uint32_t nodeid __attribute__((aligned(8)));
	mar_name_t reason __attribute__((aligned(8)));
};

struct req_exec_cfg_shutdown {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
};

/* IMPL */

static char *cfg_exec_init_fn (
	struct corosync_api_v1 *corosync_api_v1)
{
	api = corosync_api_v1;

	qb_list_init(&trackers_list);
	return (NULL);
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

	ENTER();
	req_exec_cfg_shutdown.header.size =
		sizeof (struct req_exec_cfg_shutdown);
	req_exec_cfg_shutdown.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_SHUTDOWN);

	iovec.iov_base = (char *)&req_exec_cfg_shutdown;
	iovec.iov_len = sizeof (struct req_exec_cfg_shutdown);

	assert (api->totem_mcast (&iovec, 1, TOTEM_SAFE) == 0);

	LEAVE();
	return 0;
}

static void send_test_shutdown(void *only_conn, void *exclude_conn, int status)
{
	struct res_lib_cfg_testshutdown res_lib_cfg_testshutdown;
	struct qb_list_head *iter;

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
		qb_list_for_each(iter, &trackers_list) {
			struct cfg_info *ci = qb_list_entry(iter, struct cfg_info, list);

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

			res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
			res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
			res_lib_cfg_tryshutdown.header.error = CS_OK;

			/*
			 * Tell originator that shutdown was confirmed
			 */
			api->ipc_response_send(shutdown_con->conn, &res_lib_cfg_tryshutdown,
						    sizeof(res_lib_cfg_tryshutdown));
			shutdown_con = NULL;

			/*
			 * Tell other nodes we are going down
			 */
			send_shutdown();

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

		log_printf(LOGSYS_LEVEL_DEBUG, "shutdown decision is: (yes count: %d, no count: %d) flags=%x",
		       shutdown_yes, shutdown_no, shutdown_flags);
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

	if (!qb_list_empty(&ci->list)) {
		qb_list_del(&ci->list);
		qb_list_init(&ci->list);

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
	qb_list_init(&ci->list);
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
	ENTER();

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
	log_printf(LOGSYS_LEVEL_DEBUG, "request to kill node " CS_PRI_NODE_ID " (us=" CS_PRI_NODE_ID ")",
		req_exec_cfg_killnode->nodeid, api->totem_nodeid_get());
        if (req_exec_cfg_killnode->nodeid == api->totem_nodeid_get()) {
		marshall_from_mar_name_t(&reason, &req_exec_cfg_killnode->reason);
		log_printf(LOGSYS_LEVEL_NOTICE, "Killed by node " CS_PRI_NODE_ID " : %s",
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

	log_printf(LOGSYS_LEVEL_NOTICE, "Node " CS_PRI_NODE_ID " was shut down by sysadmin", nodeid);
	if (nodeid == api->totem_nodeid_get()) {
		api->shutdown_request();
	}
	LEAVE();
}

/* strcmp replacement that can handle NULLs */
static int nullcheck_strcmp(const char* left, const char *right)
{
	if (!left && right)
		return -1;
	if (left && !right)
		return 1;

	if (!left && !right)
		return 0;

	return strcmp(left, right);
}

/*
 * If a key has changed value in the new file, then warn the user and remove it from the temp_map
 */
static void delete_and_notify_if_changed(icmap_map_t temp_map, const char *key_name)
{
	if (!(icmap_key_value_eq(temp_map, key_name, icmap_get_global_map(), key_name))) {
		if (icmap_delete_r(temp_map, key_name) == CS_OK) {
			log_printf(LOGSYS_LEVEL_NOTICE, "Modified entry '%s' in corosync.conf cannot be changed at run-time", key_name);
		}
	}
}
/*
 * Remove any keys from the new config file that in the new corosync.conf but that
 * cannot be changed at run time. A log message will be issued for each
 * entry that the user wants to change but they cannot.
 *
 * Add more here as needed.
 */
static void remove_ro_entries(icmap_map_t temp_map)
{
#ifndef HAVE_KNET_CRYPTO_RECONF
	delete_and_notify_if_changed(temp_map, "totem.secauth");
	delete_and_notify_if_changed(temp_map, "totem.crypto_hash");
	delete_and_notify_if_changed(temp_map, "totem.crypto_cipher");
	delete_and_notify_if_changed(temp_map, "totem.keyfile");
	delete_and_notify_if_changed(temp_map, "totem.key");
#endif
	delete_and_notify_if_changed(temp_map, "totem.version");
	delete_and_notify_if_changed(temp_map, "totem.threads");
	delete_and_notify_if_changed(temp_map, "totem.ip_version");
	delete_and_notify_if_changed(temp_map, "totem.netmtu");
	delete_and_notify_if_changed(temp_map, "totem.interface.bindnetaddr");
	delete_and_notify_if_changed(temp_map, "totem.interface.mcastaddr");
	delete_and_notify_if_changed(temp_map, "totem.interface.broadcast");
	delete_and_notify_if_changed(temp_map, "totem.interface.mcastport");
	delete_and_notify_if_changed(temp_map, "totem.interface.ttl");
	delete_and_notify_if_changed(temp_map, "totem.transport");
	delete_and_notify_if_changed(temp_map, "totem.cluster_name");
	delete_and_notify_if_changed(temp_map, "quorum.provider");
	delete_and_notify_if_changed(temp_map, "system.move_to_root_cgroup");
	delete_and_notify_if_changed(temp_map, "system.allow_knet_handle_fallback");
	delete_and_notify_if_changed(temp_map, "system.sched_rr");
	delete_and_notify_if_changed(temp_map, "system.priority");
	delete_and_notify_if_changed(temp_map, "system.qb_ipc_type");
	delete_and_notify_if_changed(temp_map, "system.state_dir");
}

/*
 * Remove entries that exist in the global map, but not in the temp_map, this will
 * cause delete notifications to be sent to any listeners.
 *
 * NOTE: This routine depends entirely on the keys returned by the iterators
 * being in alpha-sorted order.
 */
static void remove_deleted_entries(icmap_map_t temp_map, const char *prefix)
{
	icmap_iter_t old_iter;
	icmap_iter_t new_iter;
	const char *old_key, *new_key;
	int ret;

	old_iter = icmap_iter_init(prefix);
	new_iter = icmap_iter_init_r(temp_map, prefix);

	old_key = icmap_iter_next(old_iter, NULL, NULL);
	new_key = icmap_iter_next(new_iter, NULL, NULL);

	while (old_key || new_key) {
		ret = nullcheck_strcmp(old_key, new_key);
		if ((ret < 0 && old_key) || !new_key) {
			/*
			 * new_key is greater, a line (or more) has been deleted
			 * Continue until old is >= new
			 */
			do {
				/* Remove it from icmap & send notifications */
				icmap_delete(old_key);

				old_key = icmap_iter_next(old_iter, NULL, NULL);
				ret = nullcheck_strcmp(old_key, new_key);
			} while (ret < 0 && old_key);
		}
		else if ((ret > 0 && new_key) || !old_key) {
			/*
			 * old_key is greater, a line (or more) has been added
			 * Continue until new is >= old
			 *
			 * we don't need to do anything special with this like tell
			 * icmap. That will happen when we copy the values over
			 */
			do {
				new_key = icmap_iter_next(new_iter, NULL, NULL);
				ret = nullcheck_strcmp(old_key, new_key);
			} while (ret > 0 && new_key);
		}
		if (ret == 0) {
			new_key = icmap_iter_next(new_iter, NULL, NULL);
			old_key = icmap_iter_next(old_iter, NULL, NULL);
		}
	}
	icmap_iter_finalize(new_iter);
	icmap_iter_finalize(old_iter);
}

/*
 * Reload configuration file
 */
static void message_handler_req_exec_cfg_reload_config (
        const void *message,
        unsigned int nodeid)
{
	const struct req_exec_cfg_reload_config *req_exec_cfg_reload_config = message;
	struct res_lib_cfg_reload_config res_lib_cfg_reload_config;
	struct totem_config new_config;
	icmap_map_t temp_map;
	const char *error_string;
	int res = CS_OK;

	ENTER();

	log_printf(LOGSYS_LEVEL_NOTICE, "Config reload requested by node " CS_PRI_NODE_ID, nodeid);

	// Clear this out in case it all goes well
	icmap_delete("config.reload_error_message");

	icmap_set_uint8("config.totemconfig_reload_in_progress", 1);

	/* Make sure there is no rubbish in this that might be checked, even on error */
	memset(&new_config, 0, sizeof(new_config));
	/*
	 * Set up a new hashtable as a staging area.
	 */
	if ((res = icmap_init_r(&temp_map)) != CS_OK) {
		log_printf(LOGSYS_LEVEL_ERROR, "Unable to create temporary icmap. config file reload cancelled\n");
		goto reload_fini_nomap;
	}

	/*
	 * Load new config into the temporary map
	 */
	res = coroparse_configparse(temp_map, &error_string);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Unable to reload config file: %s", error_string);
		res = CS_ERR_INVALID_PARAM;
		goto reload_fini_nofree;
	}

	/* Signal start of the reload process */
	icmap_set_uint8("config.reload_in_progress", 1);

	/* Detect deleted entries and remove them from the main icmap hashtable */
	remove_deleted_entries(temp_map, "logging.");
	remove_deleted_entries(temp_map, "totem.");
	remove_deleted_entries(temp_map, "nodelist.");
	remove_deleted_entries(temp_map, "quorum.");
	remove_deleted_entries(temp_map, "uidgid.config.");
	remove_deleted_entries(temp_map, "nozzle.");

	/* Remove entries that cannot be changed */
	remove_ro_entries(temp_map);

	/* Take a copy of the current setup so we can check what has changed */
	memset(&new_config, 0, sizeof(new_config));
	new_config.orig_interfaces = malloc (sizeof (struct totem_interface) * INTERFACE_MAX);
	assert(new_config.orig_interfaces != NULL);

	totempg_get_config(&new_config);
	new_config.crypto_changed = 0;

	new_config.interfaces = malloc (sizeof (struct totem_interface) * INTERFACE_MAX);
	assert(new_config.interfaces != NULL);
	memset(new_config.interfaces, 0, sizeof (struct totem_interface) * INTERFACE_MAX);

	/* For UDP[U] the configuration on link0 is static (apart from the nodelist) and only read at
	   startup. So preserve it here */
	if ( (new_config.transport_number == TOTEM_TRANSPORT_UDP) ||
	     (new_config.transport_number == TOTEM_TRANSPORT_UDPU)) {
		memcpy(&new_config.interfaces[0], &new_config.orig_interfaces[0],
		       sizeof(struct totem_interface));
	}

	/* Calculate new node and interface definitions */
	if (totemconfig_configure_new_params(&new_config, temp_map, &error_string) == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Cannot configure new interface definitions: %s\n", error_string);
		res = CS_ERR_INVALID_PARAM;
		goto reload_fini;
	}

	/* Read from temp_map into new_config */
	totem_volatile_config_read(&new_config, temp_map, NULL);

	/* Get updated crypto parameters. Will set a flag in new_config if things have changed */
	if (totem_reread_crypto_config(&new_config, temp_map, &error_string) == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Crypto configuration is not valid: %s\n", error_string);
		res = CS_ERR_INVALID_PARAM;
		goto reload_fini;
	}

	/* Validate dynamic parameters */
	if (totem_volatile_config_validate(&new_config, temp_map, &error_string) == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Configuration is not valid: %s\n", error_string);
		res = CS_ERR_INVALID_PARAM;
		goto reload_fini;
	}

	/* Save this here so we can get at it for the later phases of crypto change */
	if (new_config.crypto_changed) {
#ifndef HAVE_KNET_CRYPTO_RECONF
		new_config.crypto_changed = 0;
		log_printf (LOGSYS_LEVEL_ERROR, "Crypto reconfiguration is not supported by the linked version of knet\n");
		res = CS_ERR_INVALID_PARAM;
		goto reload_fini;
#endif
	}

	/*
	 * Copy new keys into live config.
	 */
	if ( (res = icmap_copy_map(icmap_get_global_map(), temp_map)) != CS_OK) {
		log_printf (LOGSYS_LEVEL_ERROR, "Error making new config live. cmap database may be inconsistent\n");
		/* Return res from icmap */
		goto reload_fini;
	}

	/* Copy into live system */
	totempg_put_config(&new_config);
	totemconfig_commit_new_params(&new_config, temp_map);

reload_fini:
	/* All done - let clients know */
	icmap_set_int32("config.reload_status", res);
	icmap_set_uint8("config.totemconfig_reload_in_progress", 0);
	icmap_set_uint8("config.reload_in_progress", 0);

	/* Finished with the temporary storage */
	free(new_config.interfaces);
	free(new_config.orig_interfaces);

reload_fini_nofree:
	icmap_fini_r(temp_map);

reload_fini_nomap:

	/* If crypto was changed, now it's loaded on all nodes we can enable it.
	 * Each node sends its own PHASE message so we're not relying on the leader
	 * node to survive the transition
	 */
	if (new_config.crypto_changed) {
		struct req_exec_cfg_crypto_reconfig req_exec_cfg_crypto_reconfig;
		struct iovec iovec;

		req_exec_cfg_crypto_reconfig.header.size =
			sizeof (struct req_exec_cfg_crypto_reconfig);
		req_exec_cfg_crypto_reconfig.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
									  MESSAGE_REQ_EXEC_CFG_CRYPTO_RECONFIG);
		req_exec_cfg_crypto_reconfig.phase = CRYPTO_RECONFIG_PHASE_ACTIVATE;

		iovec.iov_base = (char *)&req_exec_cfg_crypto_reconfig;
		iovec.iov_len = sizeof (struct req_exec_cfg_crypto_reconfig);

		assert (api->totem_mcast (&iovec, 1, TOTEM_SAFE) == 0);
	}

	/* All done, return result to the caller if it was on this system */
	if (nodeid == api->totem_nodeid_get()) {
		res_lib_cfg_reload_config.header.size = sizeof(res_lib_cfg_reload_config);
		res_lib_cfg_reload_config.header.id = MESSAGE_RES_CFG_RELOAD_CONFIG;
		res_lib_cfg_reload_config.header.error = res;
		api->ipc_response_send(req_exec_cfg_reload_config->source.conn,
				       &res_lib_cfg_reload_config,
				       sizeof(res_lib_cfg_reload_config));
		api->ipc_refcnt_dec(req_exec_cfg_reload_config->source.conn);;
	}

	LEAVE();
}

/* Handle the phases of crypto reload
 * The first time we are called is after the new crypto config has been loaded
 * but not activated.
 *
 * 1 - activate the new crypto configuration
 * 2 - clear out the old configuration
 */
static void message_handler_req_exec_cfg_reconfig_crypto (
        const void *message,
        unsigned int nodeid)
{
	const struct req_exec_cfg_crypto_reconfig *req_exec_cfg_crypto_reconfig = message;

	/* Got our own reconfig message */
	if (nodeid == api->totem_nodeid_get()) {
		log_printf (LOGSYS_LEVEL_DEBUG, "Crypto reconfiguration phase %d", req_exec_cfg_crypto_reconfig->phase);

		/* Do the deed */
		totempg_crypto_reconfigure_phase(req_exec_cfg_crypto_reconfig->phase);

		/* Move to the next phase if not finished */
		if (req_exec_cfg_crypto_reconfig->phase < CRYPTO_RECONFIG_PHASE_CLEANUP) {
			struct req_exec_cfg_crypto_reconfig req_exec_cfg_crypto_reconfig2;
			struct iovec iovec;

			req_exec_cfg_crypto_reconfig2.header.size =
				sizeof (struct req_exec_cfg_crypto_reconfig);
			req_exec_cfg_crypto_reconfig2.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
										   MESSAGE_REQ_EXEC_CFG_CRYPTO_RECONFIG);
			req_exec_cfg_crypto_reconfig2.phase = CRYPTO_RECONFIG_PHASE_CLEANUP;

			iovec.iov_base = (char *)&req_exec_cfg_crypto_reconfig2;
			iovec.iov_len = sizeof (struct req_exec_cfg_crypto_reconfig);

			assert (api->totem_mcast (&iovec, 1, TOTEM_SAFE) == 0);
		}
	}
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
	char ifname[CFG_INTERFACE_NAME_MAX_LEN];
	unsigned int iface_ids[INTERFACE_MAX];
	unsigned int i;
	cs_error_t res = CS_OK;

	ENTER();

	res_lib_cfg_ringstatusget.header.id = MESSAGE_RES_CFG_RINGSTATUSGET;
	res_lib_cfg_ringstatusget.header.size = sizeof (struct res_lib_cfg_ringstatusget);

	api->totem_ifaces_get (
		api->totem_nodeid_get(),
		iface_ids,
		interfaces,
		INTERFACE_MAX,
		&status,
		&iface_count);

	assert(iface_count <= CFG_MAX_INTERFACES);

	res_lib_cfg_ringstatusget.interface_count = iface_count;

	for (i = 0; i < iface_count; i++) {
		totem_ip_string
		  = (const char *)api->totem_ip_print (&interfaces[i]);

		if (!totem_ip_string) {
			totem_ip_string="";
		}

		/* Allow for i/f number at the start */
		if (strlen(totem_ip_string) >= CFG_INTERFACE_NAME_MAX_LEN-3) {
			log_printf(LOGSYS_LEVEL_ERROR, "String representation of interface %u is too long", i);
			res = CS_ERR_NAME_TOO_LONG;
			goto send_response;
		}
		snprintf(ifname, sizeof(ifname), "%d %s", iface_ids[i], totem_ip_string);

		if (strlen(status[i]) >= CFG_INTERFACE_STATUS_MAX_LEN) {
			log_printf(LOGSYS_LEVEL_ERROR, "Status string for interface %u is too long", i);
			res = CS_ERR_NAME_TOO_LONG;
			goto send_response;
		}

		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_status[i],
			status[i]);
		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_name[i],
			ifname);
	}

send_response:
	res_lib_cfg_ringstatusget.header.error = res;
	api->ipc_response_send (
		conn,
		&res_lib_cfg_ringstatusget,
		sizeof (struct res_lib_cfg_ringstatusget));

	LEAVE();
}


static void message_handler_req_lib_cfg_nodestatusget (
	void *conn,
	const void *msg)
{
	struct res_lib_cfg_nodestatusget_version res_lib_cfg_nodestatusget_version;
	struct res_lib_cfg_nodestatusget_v1 res_lib_cfg_nodestatusget_v1;
	void *res_lib_cfg_nodestatusget_ptr = NULL;
	size_t res_lib_cfg_nodestatusget_size;
	struct req_lib_cfg_nodestatusget *req_lib_cfg_nodestatusget = (struct req_lib_cfg_nodestatusget *)msg;
	struct totem_node_status node_status;
	int i;

	ENTER();

	memset(&node_status, 0, sizeof(node_status));
	if (totempg_nodestatus_get(req_lib_cfg_nodestatusget->nodeid, &node_status) != 0) {
		res_lib_cfg_nodestatusget_ptr = &res_lib_cfg_nodestatusget_version;
		res_lib_cfg_nodestatusget_size = sizeof(res_lib_cfg_nodestatusget_version);

		res_lib_cfg_nodestatusget_version.header.error = CS_ERR_FAILED_OPERATION;
		res_lib_cfg_nodestatusget_version.header.id = MESSAGE_RES_CFG_NODESTATUSGET;
		res_lib_cfg_nodestatusget_version.header.size = res_lib_cfg_nodestatusget_size;

		goto ipc_response_send;
	}

	/* Currently only one structure version supported */
	switch (req_lib_cfg_nodestatusget->version) {
	case CFG_NODE_STATUS_V1:
		res_lib_cfg_nodestatusget_ptr = &res_lib_cfg_nodestatusget_v1;
		res_lib_cfg_nodestatusget_size = sizeof(res_lib_cfg_nodestatusget_v1);

		res_lib_cfg_nodestatusget_v1.header.error = CS_OK;
		res_lib_cfg_nodestatusget_v1.header.id = MESSAGE_RES_CFG_NODESTATUSGET;
		res_lib_cfg_nodestatusget_v1.header.size = res_lib_cfg_nodestatusget_size;

		res_lib_cfg_nodestatusget_v1.node_status.version = CFG_NODE_STATUS_V1;
		res_lib_cfg_nodestatusget_v1.node_status.nodeid = req_lib_cfg_nodestatusget->nodeid;
		res_lib_cfg_nodestatusget_v1.node_status.reachable = node_status.reachable;
		res_lib_cfg_nodestatusget_v1.node_status.remote = node_status.remote;
		res_lib_cfg_nodestatusget_v1.node_status.external = node_status.external;
		res_lib_cfg_nodestatusget_v1.node_status.onwire_min = node_status.onwire_min;
		res_lib_cfg_nodestatusget_v1.node_status.onwire_max = node_status.onwire_max;
		res_lib_cfg_nodestatusget_v1.node_status.onwire_ver = node_status.onwire_ver;

		for (i=0; i < KNET_MAX_LINK; i++) {
			res_lib_cfg_nodestatusget_v1.node_status.link_status[i].enabled = node_status.link_status[i].enabled;
			res_lib_cfg_nodestatusget_v1.node_status.link_status[i].connected = node_status.link_status[i].connected;
			res_lib_cfg_nodestatusget_v1.node_status.link_status[i].dynconnected = node_status.link_status[i].dynconnected;
			res_lib_cfg_nodestatusget_v1.node_status.link_status[i].mtu = node_status.link_status[i].mtu;
			memcpy(res_lib_cfg_nodestatusget_v1.node_status.link_status[i].src_ipaddr,
			       node_status.link_status[i].src_ipaddr, CFG_MAX_HOST_LEN);
			memcpy(res_lib_cfg_nodestatusget_v1.node_status.link_status[i].dst_ipaddr,
			       node_status.link_status[i].dst_ipaddr, CFG_MAX_HOST_LEN);
		}
		break;
	default:
		/*
		 * Unsupported version requested
		 */
		res_lib_cfg_nodestatusget_ptr = &res_lib_cfg_nodestatusget_version;
		res_lib_cfg_nodestatusget_size = sizeof(res_lib_cfg_nodestatusget_version);

		res_lib_cfg_nodestatusget_version.header.error = CS_ERR_NOT_SUPPORTED;
		res_lib_cfg_nodestatusget_version.header.id = MESSAGE_RES_CFG_NODESTATUSGET;
		res_lib_cfg_nodestatusget_version.header.size = res_lib_cfg_nodestatusget_size;
		break;
	}

ipc_response_send:
	api->ipc_response_send (
		conn,
		res_lib_cfg_nodestatusget_ptr,
		res_lib_cfg_nodestatusget_size);

	LEAVE();
}

static void message_handler_req_lib_cfg_trackstart (
	void *conn,
	const void *msg)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);
	struct res_lib_cfg_trackstart res_lib_cfg_trackstart;

	ENTER();

	/*
	 * We only do shutdown tracking at the moment
	 */
	if (qb_list_empty(&ci->list)) {
		qb_list_add(&ci->list, &trackers_list);
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

	res_lib_cfg_trackstart.header.size = sizeof(struct res_lib_cfg_trackstart);
	res_lib_cfg_trackstart.header.id = MESSAGE_RES_CFG_STATETRACKSTART;
	res_lib_cfg_trackstart.header.error = CS_OK;

	api->ipc_response_send(conn, &res_lib_cfg_trackstart,
				    sizeof(res_lib_cfg_trackstart));

	LEAVE();
}

static void message_handler_req_lib_cfg_trackstop (
	void *conn,
	const void *msg)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);
	struct res_lib_cfg_trackstop res_lib_cfg_trackstop;

	ENTER();
	remove_ci_from_shutdown(ci);

	res_lib_cfg_trackstop.header.size = sizeof(struct res_lib_cfg_trackstop);
	res_lib_cfg_trackstop.header.id = MESSAGE_RES_CFG_STATETRACKSTOP;
	res_lib_cfg_trackstop.header.error = CS_OK;

	api->ipc_response_send(conn, &res_lib_cfg_trackstop,
				    sizeof(res_lib_cfg_trackstop));
	LEAVE();
}

static void message_handler_req_lib_cfg_ringreenable (
	void *conn,
	const void *msg)
{
	struct res_lib_cfg_ringreenable res_lib_cfg_ringreenable;
	ENTER();

	res_lib_cfg_ringreenable.header.id = MESSAGE_RES_CFG_RINGREENABLE;
	res_lib_cfg_ringreenable.header.size = sizeof (struct res_lib_cfg_ringreenable);
	res_lib_cfg_ringreenable.header.error = CS_ERR_NOT_SUPPORTED;
	api->ipc_response_send (
		conn, &res_lib_cfg_ringreenable,
		sizeof (struct res_lib_cfg_ringreenable));

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
	char key_name[ICMAP_KEYNAME_MAXLEN];
	char tmp_key[ICMAP_KEYNAME_MAXLEN + 1];
	icmap_map_t map;
	icmap_iter_t iter;
	const char *iter_key;
	uint32_t nodeid;
	char *status_str = NULL;
	int match_nodeid_flag = 0;
	cs_error_t error = CS_OK;

	ENTER();

	map = icmap_get_global_map();
	iter = icmap_iter_init_r(map, "runtime.members.");
	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		if (sscanf(iter_key, "runtime.members.%u.%s", &nodeid, key_name) != 2) {
			continue;
		}
		if (strcmp(key_name, "status") != 0) {
			continue;
		}
		if (nodeid != req_lib_cfg_killnode->nodeid) {
			continue;
		}
		match_nodeid_flag = 1;
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "runtime.members.%u.status", nodeid);
		if (icmap_get_string_r(map, tmp_key, &status_str) != CS_OK) {
			error = CS_ERR_LIBRARY;
			goto send_response;
		}
		if (strcmp(status_str, "joined") != 0) {
			error = CS_ERR_NOT_EXIST;
			goto send_response;
		}
		break;
	}

	if (!match_nodeid_flag) {
		error = CS_ERR_NOT_EXIST;
		goto send_response;
	}

	req_exec_cfg_killnode.header.size =
		sizeof (struct req_exec_cfg_killnode);
	req_exec_cfg_killnode.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_KILLNODE);
	req_exec_cfg_killnode.nodeid = req_lib_cfg_killnode->nodeid;
	marshall_to_mar_name_t(&req_exec_cfg_killnode.reason, &req_lib_cfg_killnode->reason);

	iovec.iov_base = (char *)&req_exec_cfg_killnode;
	iovec.iov_len = sizeof (struct req_exec_cfg_killnode);

	(void)api->totem_mcast (&iovec, 1, TOTEM_SAFE);

send_response:
	res_lib_cfg_killnode.header.size = sizeof(struct res_lib_cfg_killnode);
	res_lib_cfg_killnode.header.id = MESSAGE_RES_CFG_KILLNODE;
	res_lib_cfg_killnode.header.error = error;

	api->ipc_response_send(conn, &res_lib_cfg_killnode,
				    sizeof(res_lib_cfg_killnode));

	free(status_str);
	icmap_iter_finalize(iter);
	LEAVE();
}


static void message_handler_req_lib_cfg_tryshutdown (
	void *conn,
	const void *msg)
{
	struct cfg_info *ci = (struct cfg_info *)api->ipc_private_data_get (conn);
	const struct req_lib_cfg_tryshutdown *req_lib_cfg_tryshutdown = msg;
	struct qb_list_head *iter;

	ENTER();

	if (req_lib_cfg_tryshutdown->flags == CFG_SHUTDOWN_FLAG_IMMEDIATE) {
		struct res_lib_cfg_tryshutdown res_lib_cfg_tryshutdown;

		/*
		 * Tell other nodes
		 */
		send_shutdown();

		res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
		res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
		res_lib_cfg_tryshutdown.header.error = CS_OK;
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

	qb_list_for_each(iter, &trackers_list) {
		struct cfg_info *testci = qb_list_entry(iter, struct cfg_info, list);
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

		res_lib_cfg_tryshutdown.header.size = sizeof(struct res_lib_cfg_tryshutdown);
		res_lib_cfg_tryshutdown.header.id = MESSAGE_RES_CFG_TRYSHUTDOWN;
		res_lib_cfg_tryshutdown.header.error = CS_OK;

		/*
		 * Tell originator that shutdown was confirmed
		 */
		api->ipc_response_send(conn, &res_lib_cfg_tryshutdown,
				       sizeof(res_lib_cfg_tryshutdown));

		send_shutdown();
		LEAVE();
		return;
	}
	else {
		unsigned int shutdown_timeout = DEFAULT_SHUTDOWN_TIMEOUT;

		/*
		 * Look for a shutdown timeout in configuration map
		 */
		icmap_get_uint32("cfg.shutdown_timeout", &shutdown_timeout);

		/*
		 * Start the timer. If we don't get a full set of replies before this goes
		 * off we'll cancel the shutdown
		 */
		api->timer_add_duration((unsigned long long)shutdown_timeout*QB_TIME_NS_IN_MSEC, NULL,
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
	unsigned int iface_ids[INTERFACE_MAX];
	char buf[PIPE_BUF];
	char **status;
	unsigned int num_interfaces = 0;
	struct sockaddr_storage *ss;
	int ret = CS_OK;
	int i;
	int live_addrs = 0;
	const struct req_lib_cfg_get_node_addrs *req_lib_cfg_get_node_addrs = msg;
	struct res_lib_cfg_get_node_addrs *res_lib_cfg_get_node_addrs = (struct res_lib_cfg_get_node_addrs *)buf;
	unsigned int nodeid = req_lib_cfg_get_node_addrs->nodeid;
	char *addr_buf;

	if (nodeid == 0)
		nodeid = api->totem_nodeid_get();

	if (api->totem_ifaces_get(nodeid, iface_ids, node_ifs, INTERFACE_MAX, &status, &num_interfaces)) {
		ret = CS_ERR_EXIST;
		num_interfaces = 0;
	}

	res_lib_cfg_get_node_addrs->header.size = sizeof(struct res_lib_cfg_get_node_addrs) + (num_interfaces * TOTEMIP_ADDRLEN);
	res_lib_cfg_get_node_addrs->header.id = MESSAGE_RES_CFG_GET_NODE_ADDRS;
	res_lib_cfg_get_node_addrs->header.error = ret;
	if (num_interfaces) {
		res_lib_cfg_get_node_addrs->family = node_ifs[0].family;
		for (i = 0, addr_buf = (char *)res_lib_cfg_get_node_addrs->addrs;
		    i < num_interfaces; i++) {
			ss = (struct sockaddr_storage *)&node_ifs[i].addr;
			if (ss->ss_family) {
				memcpy(addr_buf, node_ifs[i].addr, TOTEMIP_ADDRLEN);
				live_addrs++;
				addr_buf += TOTEMIP_ADDRLEN;
			}
		}
		res_lib_cfg_get_node_addrs->num_addrs = live_addrs;
	} else {
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

static void message_handler_req_lib_cfg_reload_config (void *conn, const void *msg)
{
	struct req_exec_cfg_reload_config req_exec_cfg_reload_config;
	struct iovec iovec;

	ENTER();

	req_exec_cfg_reload_config.header.size =
		sizeof (struct req_exec_cfg_reload_config);
	req_exec_cfg_reload_config.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_RELOAD_CONFIG);
	api->ipc_source_set (&req_exec_cfg_reload_config.source, conn);
	api->ipc_refcnt_inc(conn);

	iovec.iov_base = (char *)&req_exec_cfg_reload_config;
	iovec.iov_len = sizeof (struct req_exec_cfg_reload_config);

	assert (api->totem_mcast (&iovec, 1, TOTEM_SAFE) == 0);

	LEAVE();
}

static void message_handler_req_lib_cfg_reopen_log_files (void *conn, const void *msg)
{
	struct res_lib_cfg_reopen_log_files res_lib_cfg_reopen_log_files;
	cs_error_t res;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Reopening logging files\n");

	res = logsys_reopen_log_files();

	res_lib_cfg_reopen_log_files.header.size = sizeof(res_lib_cfg_reopen_log_files);
	res_lib_cfg_reopen_log_files.header.id = MESSAGE_RES_CFG_REOPEN_LOG_FILES;
	res_lib_cfg_reopen_log_files.header.error = res;
	api->ipc_response_send(conn,
			       &res_lib_cfg_reopen_log_files,
			       sizeof(res_lib_cfg_reopen_log_files));

	LEAVE();
}
