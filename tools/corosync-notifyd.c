/*
 * Copyright (c) 2011-2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <poll.h>
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qbloop.h>
#include <qb/qbmap.h>
#include <qb/qblog.h>

#include <corosync/corotypes.h>
#include <corosync/cfg.h>
#include <corosync/quorum.h>
#include <corosync/cmap.h>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

/*
 * generic declarations
 */
enum {
	CS_NTF_LOG,
	CS_NTF_STDOUT,
	CS_NTF_SNMP,
	CS_NTF_DBUS,
	CS_NTF_FG,
	CS_NTF_NODNS,
	CS_NTF_MAX,
};
static int conf[CS_NTF_MAX];

static int exit_code = 0;

static int32_t _cs_is_quorate = 0;

typedef void (*node_membership_fn_t)(char *nodename, uint32_t nodeid, char *state, char* ip);
typedef void (*node_quorum_fn_t)(char *nodename, uint32_t nodeid, const char *state);
typedef void (*application_connection_fn_t)(char *nodename, uint32_t nodeid, char *app_name, const char *state);
typedef void (*link_faulty_fn_t)(char *nodename, uint32_t local_nodeid, uint32_t nodeid, uint32_t iface_no, const char *state);

struct notify_callbacks {
	node_membership_fn_t node_membership_fn;
	node_quorum_fn_t node_quorum_fn;
	application_connection_fn_t application_connection_fn;
	link_faulty_fn_t link_faulty_fn;
};

struct track_item {
	char key_name[CMAP_KEYNAME_MAXLEN + 1];
	cmap_track_handle_t track_handle;
};

#define MAX_NOTIFIERS 5
static int num_notifiers = 0;
static struct notify_callbacks notifiers[MAX_NOTIFIERS];
/*
 * Global variable with local nodeid
 */
static uint32_t g_local_nodeid = 0;
static char local_nodename[CS_MAX_NAME_LENGTH];
static qb_loop_t *main_loop;
static quorum_handle_t quorum_handle;
static qb_map_t *tracker_map;

static void _cs_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip);
static void _cs_node_quorum_event(const char *state);
static void _cs_application_connection_event(char *app_name, const char *state);
static void _cs_link_faulty_event(uint32_t nodeid, uint32_t iface_no, const char *state);

#ifdef HAVE_DBUS
#include <dbus/dbus.h>
/*
 * dbus
 */
#define DBUS_CS_NAME	"org.corosync"
#define DBUS_CS_IFACE	"org.corosync"
#define DBUS_CS_PATH	"/org/corosync"

static DBusConnection *db = NULL;
static char _err[512];
static int err_set = 0;
static void _cs_dbus_init(void);
#endif /* HAVE_DBUS */

#ifdef ENABLE_SNMP
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/snmpv3_api.h>
#include <net-snmp/agent/agent_trap.h>
#include <net-snmp/library/mib.h>
#include <net-snmp/library/snmp_api.h>
#include <net-snmp/library/snmp_client.h>
#include <net-snmp/library/snmp_debug.h>

enum snmp_node_status {
       SNMP_NODE_STATUS_UNKNOWN = 0,
       SNMP_NODE_STATUS_JOINED = 1,
       SNMP_NODE_STATUS_LEFT = 2
};

#define SNMP_OID_COROSYNC "1.3.6.1.4.1.35488"
#define SNMP_OID_OBJECT_ROOT		SNMP_OID_COROSYNC ".1"
#define SNMP_OID_OBJECT_NODE_NAME	SNMP_OID_OBJECT_ROOT ".1"
#define SNMP_OID_OBJECT_NODE_ID		SNMP_OID_OBJECT_ROOT ".2"
#define SNMP_OID_OBJECT_NODE_STATUS	SNMP_OID_OBJECT_ROOT ".3"
#define SNMP_OID_OBJECT_NODE_ADDR	SNMP_OID_OBJECT_ROOT ".4"
#define SNMP_OID_OBJECT_LOCAL_NODE_ID		SNMP_OID_OBJECT_ROOT ".5"

#define SNMP_OID_OBJECT_RINGSEQ		SNMP_OID_OBJECT_ROOT ".20"
#define SNMP_OID_OBJECT_QUORUM		SNMP_OID_OBJECT_ROOT ".21"

#define SNMP_OID_OBJECT_APP_NAME	SNMP_OID_OBJECT_ROOT ".40"
#define SNMP_OID_OBJECT_APP_STATUS	SNMP_OID_OBJECT_ROOT ".41"

#define SNMP_OID_OBJECT_LINK_IFACE_NO	SNMP_OID_OBJECT_ROOT ".60"
#define SNMP_OID_OBJECT_LINK_STATUS	SNMP_OID_OBJECT_ROOT ".61"

#define SNMP_OID_TRAPS_ROOT		SNMP_OID_COROSYNC ".0"
#define SNMP_OID_TRAPS_NODE		SNMP_OID_TRAPS_ROOT ".1"
#define SNMP_OID_TRAPS_QUORUM		SNMP_OID_TRAPS_ROOT ".2"
#define SNMP_OID_TRAPS_APP		SNMP_OID_TRAPS_ROOT ".3"
#define SNMP_OID_TRAPS_LINK		SNMP_OID_TRAPS_ROOT ".4"

#define CS_TIMESTAMP_STR_LEN 20
static const char *local_host = "localhost";
#endif /* ENABLE_SNMP */
static char snmp_manager_buf[CS_MAX_NAME_LENGTH];
static char *snmp_manager = NULL;
static char snmp_community_buf[CS_MAX_NAME_LENGTH];
static char *snmp_community = NULL;

#define CMAP_MAX_RETRIES 10

/*
 * cmap
 */
static cmap_handle_t cmap_handle;
static cmap_handle_t stats_handle;
static cmap_track_handle_t cmap_track_handle_runtime_members_key_changed;
static cmap_track_handle_t cmap_track_handle_stats_ipcs_key_changed;
static cmap_track_handle_t cmap_track_handle_stats_knet_key_changed;

static int32_t _cs_ip_to_hostname(char* ip, char* name_out)
{
	struct sockaddr_in sa;
	int rc;

	if (strchr(ip, ':') == NULL) {
		sa.sin_family = AF_INET;
	} else {
		sa.sin_family = AF_INET6;
	}

	rc = inet_pton(sa.sin_family, ip, &sa.sin_addr);
	if (rc == 0) {
		return -EINVAL;
	}

	rc = getnameinfo((struct sockaddr*)&sa, sizeof(sa),
			name_out, CS_MAX_NAME_LENGTH, NULL, 0, 0);
	if (rc != 0) {
		qb_log(LOG_ERR, "error looking up %s : %s", ip, gai_strerror(rc));
		return -EINVAL;
	}
	return 0;
}

static void _cs_cmap_members_key_changed (
	cmap_handle_t cmap_handle_c,
	cmap_track_handle_t cmap_track_handle,
	int32_t event,
	const char *key_name,
	struct cmap_notify_value new_value,
	struct cmap_notify_value old_value,
	void *user_data)
{
	char nodename[CS_MAX_NAME_LENGTH];
	char* open_bracket = NULL;
	char* close_bracket = NULL;
	int res;
	uint32_t nodeid;
	char *ip_str;
	char tmp_key[CMAP_KEYNAME_MAXLEN];
	cs_error_t err;
	int no_retries;

	if (event != CMAP_TRACK_ADD && event != CMAP_TRACK_MODIFY) {
		return ;
	}

	if (NULL == key_name) {
		qb_log(LOG_ERR, "key_name: nil");
		return ;
	}

	res = sscanf(key_name, "runtime.members.%u.%s", &nodeid, tmp_key);
	if (res != 2)
		return ;

	if (strcmp(tmp_key, "status") != 0) {
		return ;
	}

	res = snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, "runtime.members.%u.ip", nodeid);
	if (res <= 0 || res >= CMAP_KEYNAME_MAXLEN) {
		qb_log(LOG_ERR, "temp_key: failed, res: %d, nodeid: " CS_PRI_NODE_ID, res, nodeid);
		return ;
	}
	no_retries = 0;
	while ((err = cmap_get_string(cmap_handle, tmp_key, &ip_str)) == CS_ERR_TRY_AGAIN &&
			no_retries++ < CMAP_MAX_RETRIES) {
		sleep(1);
	}

	if (err != CS_OK) {
		return ;
	}
	/*
	 * We want the ip out of: "r(0) ip(192.168.100.92)"
	 */
	open_bracket = strrchr(ip_str, '(');
	if (NULL == open_bracket) {
		qb_log(LOG_ERR, "ip_str: %s", ip_str);
		free(ip_str);
		return ;
	}
	open_bracket++;
	close_bracket = strchr(open_bracket, ')');
	if (NULL == close_bracket) {
		qb_log(LOG_ERR, "open_bracket: %s", open_bracket);
		free(ip_str);
		return ;
	}
	*close_bracket = '\0';
	if(conf[CS_NTF_NODNS]) {
		strncpy(nodename, open_bracket, CS_MAX_NAME_LENGTH-1);
	} else {
		res = _cs_ip_to_hostname(open_bracket, nodename);
		if (res) {
			strncpy(nodename, open_bracket, CS_MAX_NAME_LENGTH-1);
		}
	}
	_cs_node_membership_event(nodename, nodeid, (char *)new_value.data, open_bracket);
	free(ip_str);
}

static void _cs_cmap_connections_key_changed (
	cmap_handle_t cmap_handle_c,
	cmap_track_handle_t cmap_track_handle,
	int32_t event,
	const char *key_name,
	struct cmap_notify_value new_value,
	struct cmap_notify_value old_value,
	void *user_data)
{
	char obj_name[CS_MAX_NAME_LENGTH];
	char conn_str[CMAP_KEYNAME_MAXLEN];
	char tmp_key[CMAP_KEYNAME_MAXLEN];
	int service, pid;
	int res;

	res = sscanf(key_name, "stats.ipcs.service%d.%d.%[^.].%s", &service,&pid, conn_str, tmp_key);
	if (res != 4) {
		return ;
	}

	if (strcmp(tmp_key, "procname") != 0) {
		return ;
	}

	if (snprintf(obj_name, CS_MAX_NAME_LENGTH, "%s.%d.%s", conn_str, pid,
	    (char*)new_value.data) >= CS_MAX_NAME_LENGTH) {
		/*
		 * This should never happen
		 */
		qb_log(LOG_ERR, "Can't snprintf obj_name");
		return ;
	}

	if (event == CMAP_TRACK_ADD) {
		_cs_application_connection_event(obj_name, "connected");
	}

	if (event == CMAP_TRACK_DELETE) {
		_cs_application_connection_event(obj_name, "disconnected");
	}
}

static void _cs_cmap_link_faulty_key_changed (
	cmap_handle_t cmap_handle_c,
	cmap_track_handle_t cmap_track_handle,
	int32_t event,
	const char *key_name,
	struct cmap_notify_value new_value,
	struct cmap_notify_value old_value,
	void *user_data)
{
	uint32_t iface_no;
	uint32_t nodeid;
	int res;
	int no_retries;
	uint8_t connected;
	cs_error_t err;

	res = sscanf(key_name, "stats.knet.node%u.link%u.connected", &nodeid, &iface_no);
	if (res != 2) {
		return ;
	}

	no_retries = 0;
	while ((err = cmap_get_uint8(stats_handle, key_name, &connected)) == CS_ERR_TRY_AGAIN &&
			no_retries++ < CMAP_MAX_RETRIES) {
		sleep(1);
	}

	if (err != CS_OK) {
		return ;
	}

	if (connected) {
		_cs_link_faulty_event(nodeid, iface_no, "operational");
	} else {
		_cs_link_faulty_event(nodeid, iface_no, "disconnected");
	}
}

static void _cs_cmap_link_added_removed (
	cmap_handle_t cmap_handle_c,
	cmap_track_handle_t track_handle,
	int32_t event,
	const char *key_name,
	struct cmap_notify_value new_value,
	struct cmap_notify_value old_value,
	void *user_data)
{
	struct track_item *track_item;
	cs_error_t err;

	/* Add/remove a tracker for a new/removed knet link */
	if (strstr(key_name, ".connected")) {
		if (event == CMAP_TRACK_ADD) {

			track_item = malloc(sizeof(struct track_item));
			if (!track_item) {
				qb_log(LOG_WARNING, "Can't alloc track_item for new/removed knet link");
				return;
			}
			err = cmap_track_add(stats_handle, key_name, CMAP_TRACK_MODIFY,
			    _cs_cmap_link_faulty_key_changed, NULL, &track_handle);
			if (err != CS_OK) {
				qb_log(LOG_WARNING, "Can't add tracker for new/removed knet link");

				free(track_item);
				return ;
			}

			assert(strlen(key_name) < sizeof(track_item->key_name));
			strcpy(track_item->key_name, key_name);

			track_item->track_handle = track_handle;
			qb_map_put(tracker_map, track_item->key_name, track_item);
		} else {
			track_item = qb_map_get(tracker_map, key_name);
			if (track_item) {
				cmap_track_delete(stats_handle, track_item->track_handle);
				qb_map_rm(tracker_map, track_item->key_name);
				free(track_item);
			}
		}
	}
}


static int
_cs_cmap_dispatch(int fd, int revents, void *data)
{
	cs_error_t err;

	err = cmap_dispatch(*(cmap_handle_t *)data, CS_DISPATCH_ONE);

	if (err != CS_OK && err != CS_ERR_TRY_AGAIN && err != CS_ERR_TIMEOUT &&
		err != CS_ERR_QUEUE_FULL) {
		qb_log(LOG_ERR, "Could not dispatch cmap events. Error %u", err);
		qb_loop_stop(main_loop);

		exit_code = 1;

		return -1;
	}

	return 0;
}

static void _cs_quorum_notification(quorum_handle_t handle,
	uint32_t quorate, uint64_t ring_seq,
	uint32_t view_list_entries, uint32_t *view_list)
{
	if (_cs_is_quorate == quorate) {
		return;
	}
	_cs_is_quorate = quorate;

	if (quorate) {
		_cs_node_quorum_event("quorate");
	} else {
		_cs_node_quorum_event("not quorate");
	}
}

static int
_cs_quorum_dispatch(int fd, int revents, void *data)
{
	cs_error_t err;

	err = quorum_dispatch(quorum_handle, CS_DISPATCH_ONE);
	if (err != CS_OK && err != CS_ERR_TRY_AGAIN && err != CS_ERR_TIMEOUT &&
		err != CS_ERR_QUEUE_FULL) {
		qb_log(LOG_ERR, "Could not dispatch quorum events. Error %u", err);
		qb_loop_stop(main_loop);

		exit_code = 1;

		return -1;
	}
	return 0;
}

static void
_cs_quorum_init(void)
{
	cs_error_t rc;
	uint32_t quorum_type;
	int fd;

	quorum_callbacks_t quorum_callbacks = {
		.quorum_notify_fn = _cs_quorum_notification,
	};

	rc = quorum_initialize (&quorum_handle, &quorum_callbacks,
			        &quorum_type);
	if (rc != CS_OK) {
		qb_log(LOG_ERR, "Could not connect to corosync(quorum)");
		return;
	}
	quorum_fd_get(quorum_handle, &fd);
	qb_loop_poll_add(main_loop, QB_LOOP_MED, fd, POLLIN|POLLNVAL, NULL,
		_cs_quorum_dispatch);
	rc = quorum_trackstart(quorum_handle, CS_TRACK_CHANGES);
	if (rc != CS_OK) {
		qb_log(LOG_ERR, "Could not start tracking");
		return;
	}
}

static void
_cs_quorum_finalize(void)
{
	quorum_finalize (quorum_handle);
}


#ifdef HAVE_DBUS
/*
 * dbus notifications
 */
static void
_cs_dbus_auto_flush(void)
{
	dbus_connection_ref(db);
	while (dbus_connection_get_dispatch_status(db) == DBUS_DISPATCH_DATA_REMAINS) {
		dbus_connection_dispatch(db);
	}

	while (dbus_connection_has_messages_to_send(db)) {
		dbus_connection_flush(db);
	}

	dbus_connection_unref(db);
}

static void
_cs_dbus_release(void)
{
	DBusError err;

	if (!db)
		return;

	dbus_error_init(&err);
	dbus_bus_release_name(db, DBUS_CS_NAME, &err);
	dbus_error_free(&err);
	dbus_connection_unref(db);
	db = NULL;
}

static void
_cs_dbus_node_quorum_event(char *nodename, uint32_t nodeid, const char *state)
{
	DBusMessage *msg = NULL;

	if (err_set) {
		qb_log(LOG_ERR, "%s", _err);
		err_set = 0;
	}

	if (!db) {
		goto out_free;
	}

	if (dbus_connection_get_is_connected(db) != TRUE) {
		err_set = 1;
		snprintf(_err, sizeof(_err), "DBus connection lost");
		_cs_dbus_release();
		goto out_unlock;
	}

	_cs_dbus_auto_flush();

	if (!(msg = dbus_message_new_signal(DBUS_CS_PATH,
					    DBUS_CS_IFACE,
					    "QuorumStateChange"))) {
		qb_log(LOG_ERR, "error creating dbus signal");
		goto out_unlock;
	}

	if (!dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &nodename,
			DBUS_TYPE_UINT32, &nodeid,
			DBUS_TYPE_STRING, &state,
			DBUS_TYPE_INVALID)) {
		qb_log(LOG_ERR, "error adding args to quorum signal");
		goto out_unlock;
	}

	dbus_connection_send(db, msg, NULL);

out_unlock:
	if (msg) {
		dbus_message_unref(msg);
	}
out_free:
	return;
}

static void
_cs_dbus_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip)
{
	DBusMessage *msg = NULL;

	if (err_set) {
		qb_log(LOG_ERR, "%s", _err);
		err_set = 0;
	}

	if (!db) {
		goto out_free;
	}

	if (dbus_connection_get_is_connected(db) != TRUE) {
		err_set = 1;
		snprintf(_err, sizeof(_err), "DBus connection lost");
		_cs_dbus_release();
		goto out_unlock;
	}

	_cs_dbus_auto_flush();

	if (!(msg = dbus_message_new_signal(DBUS_CS_PATH,
					    DBUS_CS_IFACE,
					    "NodeStateChange"))) {
		qb_log(LOG_ERR, "error creating NodeStateChange signal");
		goto out_unlock;
	}

	if (!dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &nodename,
			DBUS_TYPE_UINT32, &nodeid,
			DBUS_TYPE_STRING, &ip,
			DBUS_TYPE_STRING, &state,
			DBUS_TYPE_INVALID)) {
		qb_log(LOG_ERR, "error adding args to NodeStateChange signal");
		goto out_unlock;
	}

	dbus_connection_send(db, msg, NULL);

out_unlock:
	if (msg) {
		dbus_message_unref(msg);
	}
out_free:
	return;
}

static void
_cs_dbus_application_connection_event(char *nodename, uint32_t nodeid, char *app_name, const char *state)
{
	DBusMessage *msg = NULL;

	if (err_set) {
		qb_log(LOG_ERR, "%s", _err);
		err_set = 0;
	}

	if (!db) {
		goto out_free;
	}

	if (dbus_connection_get_is_connected(db) != TRUE) {
		err_set = 1;
		snprintf(_err, sizeof(_err), "DBus connection lost");
		_cs_dbus_release();
		goto out_unlock;
	}

	_cs_dbus_auto_flush();

	if (!(msg = dbus_message_new_signal(DBUS_CS_PATH,
				DBUS_CS_IFACE,
				"ConnectionStateChange"))) {
		qb_log(LOG_ERR, "error creating ConnectionStateChange signal");
		goto out_unlock;
	}

	if (!dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &nodename,
			DBUS_TYPE_UINT32, &nodeid,
			DBUS_TYPE_STRING, &app_name,
			DBUS_TYPE_STRING, &state,
			DBUS_TYPE_INVALID)) {
		qb_log(LOG_ERR, "error adding args to ConnectionStateChange signal");
		goto out_unlock;
	}

	dbus_connection_send(db, msg, NULL);

out_unlock:
	if (msg) {
		dbus_message_unref(msg);
	}
out_free:
	return;
}

static void
_cs_dbus_link_faulty_event(char *nodename, uint32_t local_nodeid, uint32_t nodeid, uint32_t iface_no, const char *state)
{
	DBusMessage *msg = NULL;

	if (err_set) {
		qb_log(LOG_ERR, "%s", _err);
		err_set = 0;
	}

	if (!db) {
		goto out_free;
	}

	if (dbus_connection_get_is_connected(db) != TRUE) {
		err_set = 1;
		snprintf(_err, sizeof(_err), "DBus connection lost");
		_cs_dbus_release();
		goto out_unlock;
	}

	_cs_dbus_auto_flush();

	if (!(msg = dbus_message_new_signal(DBUS_CS_PATH,
					    DBUS_CS_IFACE,
					    "QuorumStateChange"))) {
		qb_log(LOG_ERR, "error creating dbus signal");
		goto out_unlock;
	}

	if (!dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &nodename,
			DBUS_TYPE_UINT32, &local_nodeid,
			DBUS_TYPE_UINT32, &nodeid,
			DBUS_TYPE_UINT32, &iface_no,
			DBUS_TYPE_STRING, &state,
			DBUS_TYPE_INVALID)) {
		qb_log(LOG_ERR, "error adding args to link signal");
		goto out_unlock;
	}

	dbus_connection_send(db, msg, NULL);

out_unlock:
	if (msg) {
		dbus_message_unref(msg);
	}
out_free:
	return;
}

static void
_cs_dbus_init(void)
{
	DBusConnection *dbc = NULL;
	DBusError err;

	dbus_error_init(&err);

	dbc = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (!dbc) {
		snprintf(_err, sizeof(_err),
			 "dbus_bus_get: %s", err.message);
		err_set = 1;
		dbus_error_free(&err);
		return;
	}

	dbus_connection_set_exit_on_disconnect(dbc, FALSE);

	db = dbc;

	notifiers[num_notifiers].node_membership_fn =
		_cs_dbus_node_membership_event;
	notifiers[num_notifiers].node_quorum_fn =
		_cs_dbus_node_quorum_event;
	notifiers[num_notifiers].application_connection_fn =
		_cs_dbus_application_connection_event;
	notifiers[num_notifiers].link_faulty_fn =
		_cs_dbus_link_faulty_event;

	num_notifiers++;
}

#endif /* HAVE_DBUS */

#ifdef ENABLE_SNMP
static netsnmp_session *_cs_snmp_session_init (const char *target)
{
	static netsnmp_session *session = NULL;
#ifndef NETSNMPV54
	char default_port[128];
	snprintf (default_port, sizeof (default_port), "%s:162", target);
#endif
	if (session) {
		return (session);
	}

	if (target == NULL) {
		return NULL;
	}

	session = malloc (sizeof (netsnmp_session));
	snmp_sess_init (session);
	session->version = SNMP_VERSION_2c;
	session->callback = NULL;
	session->callback_magic = NULL;

	if (snmp_community) {
		session->community = (u_char *)snmp_community;
		session->community_len = strlen(snmp_community_buf);
	}

	session = snmp_add(session,
#ifdef NETSNMPV54
		netsnmp_transport_open_client ("snmptrap", target),
#else
		netsnmp_tdomain_transport (default_port, 0, "udp"),
#endif
		NULL, NULL);

	if (session == NULL) {
		qb_log(LOG_ERR, 0, "Could not create snmp transport");
	}
	return (session);
}

static void _cs_snmp_add_field (
	netsnmp_pdu *trap_pdu,
	u_char asn_type,
	const char *prefix,
	void *value,
	size_t value_size)
{
	oid _oid[MAX_OID_LEN];
	size_t _oid_len = MAX_OID_LEN;
	if (snmp_parse_oid(prefix, _oid, &_oid_len)) {
		snmp_pdu_add_variable (trap_pdu, _oid, _oid_len, asn_type, (u_char *) value, value_size);
	}
}

static netsnmp_pdu *_cs_snmp_trap_pdu_init (const char *trap_oid)
{
	static oid snmptrap_oid[]  = { 1,3,6,1,6,3,1,1,4,1,0 };
	static oid sysuptime_oid[] = { 1,3,6,1,2,1,1,3,0 };
	char csysuptime[CS_TIMESTAMP_STR_LEN];
	time_t now;
	struct tm *now_tm;
	netsnmp_pdu *trap_pdu;

	now = time (NULL);
	if (now == ((time_t)-1)) {
		qb_log(LOG_NOTICE, "Failed to get timestamp.");
		return (NULL);
	}

	/* format uptime */
	now_tm = localtime(&now);
	if (now_tm == NULL || strftime (csysuptime, sizeof(csysuptime), "%s", now_tm) == 0) {
		qb_log(LOG_NOTICE, "Failed to format timestamp.");
		return (NULL);
	}

	trap_pdu = snmp_pdu_create (SNMP_MSG_TRAP2);
	if (!trap_pdu) {
		qb_log(LOG_NOTICE, "Failed to create SNMP notification.");
		return (NULL);
	}

	/* send uptime */
	snmp_add_var (trap_pdu, sysuptime_oid, sizeof (sysuptime_oid) / sizeof (oid), 't', csysuptime);
	snmp_add_var (trap_pdu, snmptrap_oid, sizeof (snmptrap_oid) / sizeof (oid), 'o', trap_oid);

	return (trap_pdu);
}

static void
_cs_snmp_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip)
{
	int ret;
	netsnmp_pdu *trap_pdu;
	netsnmp_session *session = _cs_snmp_session_init (snmp_manager);

	if (session == NULL) {
		qb_log(LOG_NOTICE, "Failed to init SNMP session.");
		return ;
	}

	trap_pdu = _cs_snmp_trap_pdu_init(SNMP_OID_TRAPS_NODE);
	if (trap_pdu == NULL) {
		return ;
	}

	/* Add extries to the trap */
	_cs_snmp_add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_NAME, (void*)nodename, strlen (nodename));
	_cs_snmp_add_field (trap_pdu, ASN_UNSIGNED, SNMP_OID_OBJECT_NODE_ID, (void*)&nodeid, sizeof (nodeid));
	_cs_snmp_add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_ADDR, (void*)ip, strlen (ip));
	_cs_snmp_add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_STATUS, (void*)state, strlen (state));

	/* Send and cleanup */
	ret = snmp_send (session, trap_pdu);
	if (ret == 0) {
		/* error */
		qb_log(LOG_ERR, "Could not send SNMP trap");
		snmp_free_pdu (trap_pdu);
	}
}

static void
_cs_snmp_node_quorum_event(char *nodename, uint32_t nodeid,
			   const char *state)
{
	int ret;
	netsnmp_pdu *trap_pdu;
	netsnmp_session *session = _cs_snmp_session_init (snmp_manager);

	if (session == NULL) {
		qb_log(LOG_NOTICE, "Failed to init SNMP session.");
		return ;
	}

	trap_pdu = _cs_snmp_trap_pdu_init(SNMP_OID_TRAPS_QUORUM);
	if (trap_pdu == NULL) {
		return ;
	}

	/* Add extries to the trap */
	_cs_snmp_add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_NAME, (void*)nodename, strlen (nodename));
	_cs_snmp_add_field (trap_pdu, ASN_UNSIGNED, SNMP_OID_OBJECT_NODE_ID, (void*)&nodeid, sizeof (nodeid));
	_cs_snmp_add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_QUORUM, (void*)state, strlen (state));

	/* Send and cleanup */
	ret = snmp_send (session, trap_pdu);
	if (ret == 0) {
		/* error */
		qb_log(LOG_ERR, "Could not send SNMP trap");
		snmp_free_pdu (trap_pdu);
	}
}

static void
_cs_snmp_link_faulty_event(char *nodename, uint32_t local_nodeid, uint32_t nodeid,
		uint32_t iface_no, const char *state)
{
	int ret;
	netsnmp_pdu *trap_pdu;
	netsnmp_session *session = _cs_snmp_session_init (snmp_manager);

	if (session == NULL) {
		qb_log(LOG_NOTICE, "Failed to init SNMP session.");
		return ;
	}

	trap_pdu = _cs_snmp_trap_pdu_init(SNMP_OID_TRAPS_LINK);
	if (trap_pdu == NULL) {
		return ;
	}

	/* Add extries to the trap */
	_cs_snmp_add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_NAME, (void*)nodename, strlen (nodename));
	_cs_snmp_add_field (trap_pdu, ASN_UNSIGNED, SNMP_OID_OBJECT_LOCAL_NODE_ID, (void*)&local_nodeid, sizeof (local_nodeid));
	_cs_snmp_add_field (trap_pdu, ASN_UNSIGNED, SNMP_OID_OBJECT_NODE_ID, (void*)&nodeid, sizeof (nodeid));
	_cs_snmp_add_field (trap_pdu, ASN_INTEGER, SNMP_OID_OBJECT_LINK_IFACE_NO, (void*)&iface_no, sizeof (iface_no));
	_cs_snmp_add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_LINK_STATUS, (void*)state, strlen (state));

	/* Send and cleanup */
	ret = snmp_send (session, trap_pdu);
	if (ret == 0) {
		/* error */
		qb_log(LOG_ERR, "Could not send SNMP trap");
		snmp_free_pdu (trap_pdu);
	}
}

static void
_cs_snmp_init(void)
{
	if (snmp_manager == NULL) {
		snmp_manager = (char*)local_host;
	}

	notifiers[num_notifiers].node_membership_fn =
		_cs_snmp_node_membership_event;
	notifiers[num_notifiers].node_quorum_fn =
		_cs_snmp_node_quorum_event;
	notifiers[num_notifiers].application_connection_fn = NULL;
	notifiers[num_notifiers].link_faulty_fn =
		_cs_snmp_link_faulty_event;
	num_notifiers++;
}

#endif /* ENABLE_SNMP */

static void
_cs_syslog_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip)
{
	qb_log(LOG_NOTICE, "%s[" CS_PRI_NODE_ID "] ip:%s %s", nodename, nodeid, ip, state);
}

static void
_cs_syslog_node_quorum_event(char *nodename, uint32_t nodeid, const char *state)
{
	if (strcmp(state, "quorate") == 0) {
		qb_log(LOG_NOTICE, "%s[" CS_PRI_NODE_ID "] is now %s", nodename, nodeid, state);
	} else {
		qb_log(LOG_NOTICE, "%s[" CS_PRI_NODE_ID "] has lost quorum", nodename, nodeid);
	}
}

static void
_cs_syslog_application_connection_event(char *nodename, uint32_t nodeid, char* app_name, const char *state)
{
	if (strcmp(state, "connected") == 0) {
		qb_log(LOG_NOTICE, "%s[" CS_PRI_NODE_ID "] %s is now %s to corosync", nodename, nodeid, app_name, state);
	} else {
		qb_log(LOG_NOTICE, "%s[" CS_PRI_NODE_ID "] %s is now %s from corosync", nodename, nodeid, app_name, state);
	}
}

static void
_cs_syslog_link_faulty_event(char *nodename, uint32_t our_nodeid, uint32_t nodeid, uint32_t iface_no, const char *state)
{
	qb_log(LOG_NOTICE, "%s[" CS_PRI_NODE_ID "] link %u to node " CS_PRI_NODE_ID " is now %s", nodename, our_nodeid, iface_no, nodeid, state);
}

static void
_cs_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip)
{
	int i;

	for (i = 0; i < num_notifiers; i++) {
		if (notifiers[i].node_membership_fn) {
			notifiers[i].node_membership_fn(nodename, nodeid, state, ip);
		}
	}
}

static void
_cs_local_node_info_get(char **nodename, uint32_t *nodeid)
{
	cs_error_t rc;
	corosync_cfg_handle_t cfg_handle;

	if (g_local_nodeid == 0) {
		rc = corosync_cfg_initialize(&cfg_handle, NULL);
		if (rc != CS_OK) {
			syslog (LOG_ERR, "Failed to initialize the cfg API. Error %d\n", rc);
			exit (EXIT_FAILURE);
		}

		rc = corosync_cfg_local_get (cfg_handle, &g_local_nodeid);
		corosync_cfg_finalize(cfg_handle);
		if (rc != CS_OK) {
			g_local_nodeid = 0;
			strncpy(local_nodename, "localhost", sizeof (local_nodename));
			local_nodename[sizeof (local_nodename) - 1] = '\0';
		} else {
			gethostname(local_nodename, CS_MAX_NAME_LENGTH);
		}
	}
	*nodeid = g_local_nodeid;
	*nodename = local_nodename;
}

static void
_cs_node_quorum_event(const char *state)
{
	int i;
	char *nodename;
	uint32_t nodeid;

	_cs_local_node_info_get(&nodename, &nodeid);

	for (i = 0; i < num_notifiers; i++) {
		if (notifiers[i].node_quorum_fn) {
			notifiers[i].node_quorum_fn(nodename, nodeid, state);
		}
	}
}

static void
_cs_application_connection_event(char *app_name, const char *state)
{
	int i;
	char *nodename;
	uint32_t nodeid;

	_cs_local_node_info_get(&nodename, &nodeid);

	for (i = 0; i < num_notifiers; i++) {
		if (notifiers[i].application_connection_fn) {
			notifiers[i].application_connection_fn(nodename, nodeid, app_name, state);
		}
	}
}

static void
_cs_link_faulty_event(uint32_t nodeid, uint32_t iface_no, const char *state)
{
	int i;
	char *nodename;
	uint32_t our_nodeid;

	_cs_local_node_info_get(&nodename, &our_nodeid);

	for (i = 0; i < num_notifiers; i++) {
		if (notifiers[i].link_faulty_fn) {
			notifiers[i].link_faulty_fn(nodename, our_nodeid, nodeid, iface_no, state);
		}
	}
}

static int32_t
sig_exit_handler(int32_t num, void *data)
{
	qb_loop_stop(main_loop);
	return 0;
}

static void track_link_updown_events(void)
{
	cmap_iter_handle_t iter_handle;
	cmap_track_handle_t track_handle;

	char key_name[CMAP_KEYNAME_MAXLEN + 1];
	size_t value_len;
	cmap_value_types_t type;
	cs_error_t err;
	struct track_item *track_item;

	err = cmap_iter_init(stats_handle, "stats.knet.", &iter_handle);
	if (err != CS_OK) {
		fprintf (stderr, "Failed to initialize knet stats iterator. Error %s\n", cs_strerror(err));
		exit (EXIT_FAILURE);
	}

	while ((err = cmap_iter_next(stats_handle, iter_handle, key_name, &value_len, &type)) == CS_OK) {
		if (strstr(key_name, ".connected")) {

			track_item = malloc(sizeof(struct track_item));
			if (!track_item) {
				return;
			}

			if ((err = cmap_track_add(stats_handle, key_name, CMAP_TRACK_MODIFY, _cs_cmap_link_faulty_key_changed, NULL, &track_handle)) != CS_OK) {
				fprintf (stderr, "Failed to add tracker for %s. Error %s\n", key_name, cs_strerror(err));
				exit (EXIT_FAILURE);
			}
			strcpy(track_item->key_name, key_name);
			track_item->track_handle = track_handle;
			qb_map_put(tracker_map, track_item->key_name, track_item);
		}
	}
	cmap_iter_finalize(stats_handle, iter_handle);
}

static void
_cs_cmap_init(void)
{
	cs_error_t rc = CS_OK;
	int cmap_fd = 0;
	int stats_fd = 0;

	tracker_map = qb_trie_create();
	if (!tracker_map) {
		qb_log(LOG_ERR, "Failed to initialize the track map. Error %d", rc);
		exit (EXIT_FAILURE);
	}

	rc = cmap_initialize_map (&cmap_handle, CMAP_MAP_ICMAP);
	if (rc != CS_OK) {
		qb_log(LOG_ERR, "Failed to initialize the cmap API. Error %d", rc);
		exit (EXIT_FAILURE);
	}
	cmap_fd_get(cmap_handle, &cmap_fd);

	qb_loop_poll_add(main_loop, QB_LOOP_MED, cmap_fd, POLLIN|POLLNVAL, (void*)&cmap_handle,
		_cs_cmap_dispatch);


	rc = cmap_initialize_map (&stats_handle, CMAP_MAP_STATS);
	if (rc != CS_OK) {
		qb_log(LOG_ERR, "Failed to initialize the cmap stats API. Error %d", rc);
		exit (EXIT_FAILURE);
	}
	cmap_fd_get(stats_handle, &stats_fd);

	qb_loop_poll_add(main_loop, QB_LOOP_MED, stats_fd, POLLIN|POLLNVAL, (void*)&stats_handle,
		_cs_cmap_dispatch);


	rc = cmap_track_add(cmap_handle, "runtime.members.",
			CMAP_TRACK_ADD | CMAP_TRACK_MODIFY | CMAP_TRACK_PREFIX,
			_cs_cmap_members_key_changed,
			NULL,
			&cmap_track_handle_runtime_members_key_changed);
	if (rc != CS_OK) {
		qb_log(LOG_ERR,
			"Failed to track the members key. Error %d", rc);
		exit (EXIT_FAILURE);
	}

	rc = cmap_track_add(stats_handle, "stats.ipcs.",
			CMAP_TRACK_ADD | CMAP_TRACK_DELETE | CMAP_TRACK_PREFIX,
			_cs_cmap_connections_key_changed,
			NULL,
			&cmap_track_handle_stats_ipcs_key_changed);
	if (rc != CS_OK) {
		qb_log(LOG_ERR,
			"Failed to track the connections key. Error %d", rc);
		exit (EXIT_FAILURE);
	}

	rc = cmap_track_add(stats_handle, "stats.knet.",
			CMAP_TRACK_ADD | CMAP_TRACK_DELETE | CMAP_TRACK_PREFIX,
			_cs_cmap_link_added_removed,
			NULL,
			&cmap_track_handle_stats_knet_key_changed);
	if (rc != CS_OK) {
		qb_log(LOG_ERR,
			"Failed to track the knet link status key. Error %d", rc);
		exit (EXIT_FAILURE);
	}
	track_link_updown_events();
}

static void
_cs_cmap_finalize(void)
{
	struct qb_map_iter *map_iter;
	struct track_item *track_item;

	map_iter = qb_map_iter_create(tracker_map);
	while (qb_map_iter_next(map_iter, (void **)&track_item) != NULL) {
		cmap_track_delete(stats_handle, track_item->track_handle);
		free(track_item);
	}
	qb_map_iter_free(map_iter);

	cmap_track_delete(cmap_handle, cmap_track_handle_runtime_members_key_changed);
	cmap_track_delete(stats_handle, cmap_track_handle_stats_ipcs_key_changed);
	cmap_track_delete(stats_handle, cmap_track_handle_stats_knet_key_changed);
	cmap_finalize (cmap_handle);
	cmap_finalize (stats_handle);
}

static void
_cs_check_config(void)
{
	if (conf[CS_NTF_LOG] == QB_FALSE &&
		conf[CS_NTF_STDOUT] == QB_FALSE &&
		conf[CS_NTF_SNMP] == QB_FALSE &&
		conf[CS_NTF_DBUS] == QB_FALSE) {
		qb_log(LOG_ERR, "no event type enabled, see corosync-notifyd -h, exiting.");
		exit(EXIT_FAILURE);
	}

#ifndef ENABLE_SNMP
	if (conf[CS_NTF_SNMP]) {
		qb_log(LOG_ERR, "Not compiled with SNMP support enabled, exiting.");
		exit(EXIT_FAILURE);
	}
#endif
#ifndef HAVE_DBUS
	if (conf[CS_NTF_DBUS]) {
		qb_log(LOG_ERR, "Not compiled with DBus support enabled, exiting.");
		exit(EXIT_FAILURE);
	}
#endif

	if (conf[CS_NTF_STDOUT] && !conf[CS_NTF_FG]) {
		qb_log(LOG_ERR, "configured to print to stdout and run in the background, exiting");
		exit(EXIT_FAILURE);
	}
	if (conf[CS_NTF_SNMP] && conf[CS_NTF_DBUS]) {
		qb_log(LOG_ERR, "configured to send snmp traps and dbus signals - are you sure?.");
	}
}

static void
_cs_usage(void)
{
	fprintf(stderr,	"usage:\n"\
		"        -c     : SNMP Community name.\n"\
		"        -f     : Start application in foreground.\n"\
		"        -l     : Log all events.\n"\
		"        -o     : Print events to stdout (turns on -l).\n"\
		"        -s     : Send SNMP traps on all events.\n"\
		"        -m     : Set the SNMP Manager IP address (defaults to localhost).\n"\
		"        -n     : No reverse DNS lookup on cmap member change events.\n"\
		"        -d     : Send DBUS signals on all events.\n"\
		"        -h     : Print this help.\n\n");
}

int
main(int argc, char *argv[])
{
	int ch;

	conf[CS_NTF_FG] = QB_FALSE;
	conf[CS_NTF_LOG] = QB_FALSE;
	conf[CS_NTF_STDOUT] = QB_FALSE;
	conf[CS_NTF_SNMP] = QB_FALSE;
	conf[CS_NTF_DBUS] = QB_FALSE;

	while ((ch = getopt (argc, argv, "c:floshdnm:")) != EOF) {
		switch (ch) {
			case 'c':
				strncpy(snmp_community_buf, optarg, sizeof (snmp_community_buf));
				snmp_community_buf[sizeof (snmp_community_buf) - 1] = '\0';
				snmp_community = snmp_community_buf;
				break;
			case 'f':
				conf[CS_NTF_FG] = QB_TRUE;
				break;
			case 'l':
				conf[CS_NTF_LOG] = QB_TRUE;
				break;
			case 'm':
				conf[CS_NTF_SNMP] = QB_TRUE;
				strncpy(snmp_manager_buf, optarg, sizeof (snmp_manager_buf));
				snmp_manager_buf[sizeof (snmp_manager_buf) - 1] = '\0';
				snmp_manager = snmp_manager_buf;
				break;
			case 'n':
				conf[CS_NTF_NODNS] = QB_TRUE;
				break;
			case 'o':
				conf[CS_NTF_LOG] = QB_TRUE;
				conf[CS_NTF_STDOUT] = QB_TRUE;
				break;
			case 's':
				conf[CS_NTF_SNMP] = QB_TRUE;
				break;
			case 'd':
				conf[CS_NTF_DBUS] = QB_TRUE;
				break;
			case 'h':
			default:
				_cs_usage();
				return EXIT_FAILURE;
		}
	}

	qb_log_init("notifyd", LOG_DAEMON, LOG_INFO);

	if (conf[CS_NTF_STDOUT]) {
		qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
		qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, conf[CS_NTF_STDOUT]);
	}
	_cs_check_config();

	if (!conf[CS_NTF_FG]) {
		if (daemon(0, 0) < 0)
		{
			perror("daemon() failed");
			return EXIT_FAILURE;
		}
	}

	num_notifiers = 0;
	if (conf[CS_NTF_LOG]) {
		notifiers[num_notifiers].node_membership_fn =
			_cs_syslog_node_membership_event;
		notifiers[num_notifiers].node_quorum_fn =
			_cs_syslog_node_quorum_event;
		notifiers[num_notifiers].application_connection_fn =
			_cs_syslog_application_connection_event;
		notifiers[num_notifiers].link_faulty_fn =
			_cs_syslog_link_faulty_event;
		num_notifiers++;
	}

	main_loop = qb_loop_create();

	_cs_cmap_init();
	_cs_quorum_init();

#ifdef HAVE_DBUS
	if (conf[CS_NTF_DBUS]) {
		_cs_dbus_init();
	}
#endif /* HAVE_DBUS */

#ifdef ENABLE_SNMP
	if (conf[CS_NTF_SNMP]) {
		_cs_snmp_init();
	}
#endif /* ENABLE_SNMP */

	qb_loop_signal_add(main_loop,
			   QB_LOOP_HIGH,
			   SIGINT,
			   NULL,
			   sig_exit_handler,
			   NULL);
	qb_loop_signal_add(main_loop,
			   QB_LOOP_HIGH,
			   SIGQUIT,
			   NULL,
			   sig_exit_handler,
			   NULL);
	qb_loop_signal_add(main_loop,
			   QB_LOOP_HIGH,
			   SIGTERM,
			   NULL,
			   sig_exit_handler,
			   NULL);

#ifdef HAVE_LIBSYSTEMD
	sd_notify (0, "READY=1");
#endif

	qb_loop_run(main_loop);

#ifdef HAVE_DBUS
	if (conf[CS_NTF_DBUS]) {
		_cs_dbus_release();
	}
#endif /* HAVE_DBUS */

	_cs_quorum_finalize();
	_cs_cmap_finalize();

	return (exit_code);
}

