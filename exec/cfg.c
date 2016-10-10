/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2013 Red Hat, Inc.
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
#include <assert.h>

#include <corosync/corotypes.h>
#include <qb/qbipc_common.h>
#include <corosync/cfg.h>
#include <corosync/list.h>
#include <corosync/mar_gen.h>
#include <corosync/totem/totemip.h>
#include <corosync/totem/totem.h>
#include <corosync/ipc_cfg.h>
#include <corosync/logsys.h>
#include <corosync/coroapi.h>
#include <corosync/icmap.h>
#include <corosync/corodefs.h>

#include "service.h"
#include "main.h"

LOGSYS_DECLARE_SUBSYS ("CFG");

enum cfg_message_req_types {
        MESSAGE_REQ_EXEC_CFG_RINGREENABLE = 0,
	MESSAGE_REQ_EXEC_CFG_KILLNODE = 1,
	MESSAGE_REQ_EXEC_CFG_SHUTDOWN = 2,
	MESSAGE_REQ_EXEC_CFG_RELOAD_CONFIG = 3
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

static void exec_cfg_killnode_endian_convert (void *msg);

static void message_handler_req_lib_cfg_ringstatusget (
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

static void message_handler_req_lib_cfg_get_node_addrs (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_local_get (
	void *conn,
	const void *msg);

static void message_handler_req_lib_cfg_reload_config (
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
		.exec_handler_fn = message_handler_req_exec_cfg_reload_config,
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

	list_init(&trackers_list);
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
	log_printf(LOGSYS_LEVEL_DEBUG, "request to kill node %d(us=%d)",
		req_exec_cfg_killnode->nodeid, api->totem_nodeid_get());
        if (req_exec_cfg_killnode->nodeid == api->totem_nodeid_get()) {
		marshall_from_mar_name_t(&reason, &req_exec_cfg_killnode->reason);
		log_printf(LOGSYS_LEVEL_NOTICE, "Killed by node %d: %s",
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

	log_printf(LOGSYS_LEVEL_NOTICE, "Node %d was shut down by sysadmin", nodeid);
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
	delete_and_notify_if_changed(temp_map, "totem.secauth");
	delete_and_notify_if_changed(temp_map, "totem.crypto_hash");
	delete_and_notify_if_changed(temp_map, "totem.crypto_cipher");
	delete_and_notify_if_changed(temp_map, "totem.version");
	delete_and_notify_if_changed(temp_map, "totem.threads");
	delete_and_notify_if_changed(temp_map, "totem.ip_version");
	delete_and_notify_if_changed(temp_map, "totem.rrp_mode");
	delete_and_notify_if_changed(temp_map, "totem.netmtu");
	delete_and_notify_if_changed(temp_map, "totem.interface.ringnumber");
	delete_and_notify_if_changed(temp_map, "totem.interface.bindnetaddr");
	delete_and_notify_if_changed(temp_map, "totem.interface.mcastaddr");
	delete_and_notify_if_changed(temp_map, "totem.interface.broadcast");
	delete_and_notify_if_changed(temp_map, "totem.interface.mcastport");
	delete_and_notify_if_changed(temp_map, "totem.interface.ttl");
	delete_and_notify_if_changed(temp_map, "totem.vsftype");
	delete_and_notify_if_changed(temp_map, "totem.transport");
	delete_and_notify_if_changed(temp_map, "totem.cluster_name");
	delete_and_notify_if_changed(temp_map, "quorum.provider");
	delete_and_notify_if_changed(temp_map, "qb.ipc_type");
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
	icmap_map_t temp_map;
	const char *error_string;
	int res = CS_OK;

	ENTER();

	log_printf(LOGSYS_LEVEL_NOTICE, "Config reload requested by node %d", nodeid);

	/*
	 * Set up a new hashtable as a staging area.
	 */
	if ((res = icmap_init_r(&temp_map)) != CS_OK) {
		log_printf(LOGSYS_LEVEL_ERROR, "Unable to create temporary icmap. config file reload cancelled\n");
		goto reload_fini;
	}

	/*
	 * Load new config into the temporary map
	 */
	res = coroparse_configparse(temp_map, &error_string);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Unable to reload config file: %s", error_string);
		res = CS_ERR_LIBRARY;
		goto reload_return;
	}

	/* Tell interested listeners that we have started a reload */
	icmap_set_uint8("config.reload_in_progress", 1);

	/* Detect deleted entries and remove them from the main icmap hashtable */
	remove_deleted_entries(temp_map, "logging.");
	remove_deleted_entries(temp_map, "totem.");
	remove_deleted_entries(temp_map, "nodelist.");
	remove_deleted_entries(temp_map, "quorum.");
	remove_deleted_entries(temp_map, "uidgid.config.");

	/* Remove entries that cannot be changed */
	remove_ro_entries(temp_map);

	/*
	 * Copy new keys into live config.
	 * If this fails we will have a partially loaded config because some keys (above) might
	 * have been reset to defaults - I'm not sure what to do here, we might have to quit.
	 */
	if ( (res = icmap_copy_map(icmap_get_global_map(), temp_map)) != CS_OK) {
		log_printf (LOGSYS_LEVEL_ERROR, "Error making new config live. cmap database may be inconsistent\n");
	}

	/* All done - let clients know */
	icmap_set_uint8("config.reload_in_progress", 0);

reload_fini:
	/* Finished with the temporary storage */
	icmap_fini_r(temp_map);

reload_return:
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
	cs_error_t res = CS_OK;

	ENTER();

	res_lib_cfg_ringstatusget.header.id = MESSAGE_RES_CFG_RINGSTATUSGET;
	res_lib_cfg_ringstatusget.header.size = sizeof (struct res_lib_cfg_ringstatusget);

	api->totem_ifaces_get (
		api->totem_nodeid_get(),
		interfaces,
		INTERFACE_MAX,
		&status,
		&iface_count);

	assert(iface_count <= CFG_MAX_INTERFACES);

	res_lib_cfg_ringstatusget.interface_count = iface_count;

	for (i = 0; i < iface_count; i++) {
		totem_ip_string
		  = (const char *)api->totem_ip_print (&interfaces[i]);

		if (strlen(totem_ip_string) >= CFG_INTERFACE_NAME_MAX_LEN) {
			log_printf(LOGSYS_LEVEL_ERROR, "String representation of interface %u is too long", i);
			res = CS_ERR_NAME_TOO_LONG;
			goto send_response;
		}

		if (strlen(status[i]) >= CFG_INTERFACE_STATUS_MAX_LEN) {
			log_printf(LOGSYS_LEVEL_ERROR, "Status string for interface %u is too long", i);
			res = CS_ERR_NAME_TOO_LONG;
			goto send_response;
		}

		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_status[i],
			status[i]);
		strcpy ((char *)&res_lib_cfg_ringstatusget.interface_name[i],
			totem_ip_string);
	}

send_response:
	res_lib_cfg_ringstatusget.header.error = res;
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
	struct iovec iovec;

	ENTER();
	req_exec_cfg_ringreenable.header.size =
		sizeof (struct req_exec_cfg_ringreenable);
	req_exec_cfg_ringreenable.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_RINGREENABLE);
	api->ipc_source_set (&req_exec_cfg_ringreenable.source, conn);
	api->ipc_refcnt_inc(conn);

	iovec.iov_base = (char *)&req_exec_cfg_ringreenable;
	iovec.iov_len = sizeof (struct req_exec_cfg_ringreenable);

	assert (api->totem_mcast (&iovec, 1, TOTEM_SAFE) == 0);

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

	ENTER();
	req_exec_cfg_killnode.header.size =
		sizeof (struct req_exec_cfg_killnode);
	req_exec_cfg_killnode.header.id = SERVICE_ID_MAKE (CFG_SERVICE,
		MESSAGE_REQ_EXEC_CFG_KILLNODE);
	req_exec_cfg_killnode.nodeid = req_lib_cfg_killnode->nodeid;
	marshall_to_mar_name_t(&req_exec_cfg_killnode.reason, &req_lib_cfg_killnode->reason);

	iovec.iov_base = (char *)&req_exec_cfg_killnode;
	iovec.iov_len = sizeof (struct req_exec_cfg_killnode);

	(void)api->totem_mcast (&iovec, 1, TOTEM_SAFE);

	res_lib_cfg_killnode.header.size = sizeof(struct res_lib_cfg_killnode);
	res_lib_cfg_killnode.header.id = MESSAGE_RES_CFG_KILLNODE;
	res_lib_cfg_killnode.header.error = CS_OK;

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

	api->totem_ifaces_get(nodeid, node_ifs, INTERFACE_MAX, &status, &num_interfaces);

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
