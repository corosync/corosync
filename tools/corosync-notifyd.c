/*
 * Copyright (c) 2011 Red Hat
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
#include <syslog.h>

#include <corosync/corotypes.h>
#include <corosync/totem/coropoll.h>
#include <corosync/confdb.h>
#include <corosync/cfg.h>
#include <corosync/quorum.h>

/*
 * generic declarations
 */
enum {
	CS_NTF_LOG,
	CS_NTF_STDOUT,
	CS_NTF_SNMP,
	CS_NTF_DBUS,
	CS_NTF_FG,
	CS_NTF_MAX,
};
static int conf[CS_NTF_MAX];

static int32_t _cs_is_quorate = 0;

typedef void (*node_membership_fn_t)(char *nodename, uint32_t nodeid, char *state, char* ip);
typedef void (*node_quorum_fn_t)(char *nodename, uint32_t nodeid, const char *state);
typedef void (*application_connection_fn_t)(char *nodename, uint32_t nodeid, char *app_name, const char *state);

struct notify_callbacks {
	node_membership_fn_t node_membership_fn;
	node_quorum_fn_t node_quorum_fn;
	application_connection_fn_t application_connection_fn;
};

#define MAX_NOTIFIERS 5
static int num_notifiers = 0;
static struct notify_callbacks notifiers[MAX_NOTIFIERS];
static uint32_t local_nodeid = 0;
static char local_nodename[CS_MAX_NAME_LENGTH];
static hdb_handle_t poll_handle;
static quorum_handle_t quorum_handle;

static void _cs_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip);
static void _cs_node_quorum_event(const char *state);
static void _cs_application_connection_event(char *app_name, const char *state);

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

#define SNMP_OID_OBJECT_RINGSEQ		SNMP_OID_OBJECT_ROOT ".20"
#define SNMP_OID_OBJECT_QUORUM		SNMP_OID_OBJECT_ROOT ".21"

#define SNMP_OID_OBJECT_APP_NAME	SNMP_OID_OBJECT_ROOT ".40"
#define SNMP_OID_OBJECT_APP_STATUS	SNMP_OID_OBJECT_ROOT ".41"

#define SNMP_OID_TRAPS_ROOT		SNMP_OID_COROSYNC ".0"
#define SNMP_OID_TRAPS_NODE		SNMP_OID_TRAPS_ROOT ".1"
#define SNMP_OID_TRAPS_QUORUM		SNMP_OID_TRAPS_ROOT ".2"
#define SNMP_OID_TRAPS_APP		SNMP_OID_TRAPS_ROOT ".3"

#define CS_TIMESTAMP_STR_LEN 20
static const char *local_host = "localhost";
#endif /* ENABLE_SNMP */
static char snmp_manager_buf[CS_MAX_NAME_LENGTH];
static char *snmp_manager = NULL;


/*
 * confdb
 */
#define SEPERATOR_STR "."

static confdb_handle_t confdb_handle;

static void _cs_confdb_key_changed(confdb_handle_t handle,
	confdb_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name, size_t object_name_len,
	const void *key_name, size_t key_name_len,
	const void *key_value, size_t key_value_len);

static void _cs_confdb_object_created(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt, size_t name_len);

static void _cs_confdb_object_deleted(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *name_pt, size_t name_len);

static confdb_callbacks_t callbacks = {
	.confdb_key_change_notify_fn = _cs_confdb_key_changed,
	.confdb_object_create_change_notify_fn = _cs_confdb_object_created,
	.confdb_object_delete_change_notify_fn = _cs_confdb_object_deleted,
};

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
		syslog (LOG_ERR, "error looking up %s : %s\n", ip, gai_strerror(rc));
		return -EINVAL;
	}
	return 0;
}

static void
_cs_confdb_key_changed(confdb_handle_t handle,
	confdb_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t  object_name_len,
	const void *key_name_pt, size_t key_name_len,
	const void *key_value_pt, size_t key_value_len)
{
	char parent_name[CS_MAX_NAME_LENGTH];
	size_t len = 0;
	hdb_handle_t real_parent_object_handle;
	cs_error_t rc = CS_OK;
	char nodename[CS_MAX_NAME_LENGTH];
	char nodeid_str[CS_MAX_NAME_LENGTH];
	uint32_t nodeid;
	char status[CS_MAX_NAME_LENGTH];
	char ip[CS_MAX_NAME_LENGTH];
	size_t ip_len;
	confdb_value_types_t type;
	char* open_bracket = NULL;
	char* close_bracket = NULL;

	rc = confdb_object_parent_get (handle,
		parent_object_handle, &real_parent_object_handle);
	assert(rc == CS_OK);

	rc = confdb_object_name_get (handle,
		real_parent_object_handle,
		parent_name,
		&len);
	parent_name[len] = '\0';
	assert(rc == CS_OK);

	if (strcmp(parent_name, "members") == 0) {
		if (strncmp(key_name_pt, "status", strlen("status")) == 0) {

			memcpy(nodeid_str, object_name_pt, object_name_len);
			nodeid_str[object_name_len] = '\0';
			nodeid = atoi(nodeid_str);

			memcpy(status, key_value_pt, key_value_len);
			status[key_value_len] = '\0';

			rc = confdb_key_get_typed(handle, parent_object_handle,
				"ip", ip, &ip_len, &type);
			assert(rc == CS_OK);
			ip[ip_len-1] = '\0';

			/*
			 * We want the ip out of: "r(0) ip(192.168.100.92)"
			 */
			open_bracket = strrchr(ip, '(');
			open_bracket++;
			close_bracket = strrchr(open_bracket, ')');
			*close_bracket = '\0';
			_cs_ip_to_hostname(open_bracket, nodename);

			_cs_node_membership_event(nodename, nodeid, status, open_bracket);
		}
	}
}

static void
_cs_confdb_object_created(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt,
	size_t name_len)
{
	char parent_name[CS_MAX_NAME_LENGTH];
	size_t len = 0;
	char obj_name[CS_MAX_NAME_LENGTH];
	cs_error_t rc = CS_OK;

	memcpy(obj_name, name_pt, name_len);
	obj_name[name_len] = '\0';

	rc = confdb_object_name_get (handle,
		object_handle, parent_name, &len);
	parent_name[len] = '\0';
	if (rc != CS_OK) {
		return;
	}

	if (strcmp(parent_name, "connections") == 0) {
		_cs_application_connection_event(obj_name, "connected");
	}
}

static void
_cs_confdb_object_deleted(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *name_pt,
	size_t name_len)
{
	char obj_name[CS_MAX_NAME_LENGTH];
	char parent_name[CS_MAX_NAME_LENGTH];
	size_t len = 0;
	cs_error_t rc;

	memcpy(obj_name, name_pt, name_len);
	obj_name[name_len] = '\0';

	rc = confdb_object_name_get (handle,
		parent_object_handle, parent_name, &len);
	parent_name[len] = '\0';
	assert(rc == CS_OK);

	if (strcmp(parent_name, "connections") == 0) {
		_cs_application_connection_event(obj_name, "disconnected");
	}
}

static cs_error_t
_cs_confdb_find_object (confdb_handle_t handle,
	const char * name_pt,
	hdb_handle_t * out_handle)
{
	char * obj_name_pt;
	char * save_pt;
	hdb_handle_t obj_handle;
	confdb_handle_t parent_object_handle = OBJECT_PARENT_HANDLE;
	char tmp_name[CS_MAX_NAME_LENGTH];
	cs_error_t res = CS_OK;

	strncpy (tmp_name, name_pt, sizeof (tmp_name));
	tmp_name[sizeof (tmp_name) - 1] = '\0';
	obj_name_pt = strtok_r(tmp_name, SEPERATOR_STR, &save_pt);

	while (obj_name_pt != NULL) {
		res = confdb_object_find_start(handle, parent_object_handle);
		if (res != CS_OK) {
			syslog (LOG_ERR, "Could not start object_find %d\n", res);
			exit (EXIT_FAILURE);
		}

		res = confdb_object_find(handle, parent_object_handle,
				obj_name_pt, strlen (obj_name_pt), &obj_handle);
		if (res != CS_OK) {
			return res;
		}
		confdb_object_find_destroy(handle, parent_object_handle);

		parent_object_handle = obj_handle;
		obj_name_pt = strtok_r (NULL, SEPERATOR_STR, &save_pt);
	}

	*out_handle = parent_object_handle;
	return res;
}

static int
_cs_confdb_dispatch(hdb_handle_t handle,
	int fd,	int revents, void *data)
{
	confdb_dispatch(confdb_handle, CS_DISPATCH_ONE);
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
_cs_quorum_dispatch(hdb_handle_t handle,
	int fd,	int revents, void *data)
{
	quorum_dispatch(quorum_handle, CS_DISPATCH_ONE);
	return 0;
}

static void
_cs_quorum_init(void)
{
	cs_error_t rc;
	int fd;

	quorum_callbacks_t quorum_callbacks = {
		.quorum_notify_fn = _cs_quorum_notification,
	};

	rc = quorum_initialize (&quorum_handle, &quorum_callbacks);
	if (rc != CS_OK) {
		syslog(LOG_ERR, "Could not connect to corosync(quorum)");
		return;
	}
	quorum_fd_get(quorum_handle, &fd);
	poll_dispatch_add (poll_handle, fd, POLLIN|POLLNVAL, NULL,
		_cs_quorum_dispatch);
	quorum_trackstart(quorum_handle, CS_TRACK_CHANGES);
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
	int ret = -1;

	if (err_set) {
		syslog (LOG_ERR, "%s\n", _err);
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
		syslog (LOG_ERR, "%s(%d) error\n", __func__, __LINE__);
		goto out_unlock;
	}

	if (!dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &nodename,
			DBUS_TYPE_UINT32, &nodeid,
			DBUS_TYPE_STRING, &state,
			DBUS_TYPE_INVALID)) {
		syslog (LOG_ERR, "%s(%d) error\n", __func__, __LINE__);
		goto out_unlock;
	}

	dbus_connection_send(db, msg, NULL);
	ret = 0;

out_unlock:
	if (ret == -1) {
		syslog (LOG_ERR, "%s() error\n", __func__);
	}
	if (msg)
		dbus_message_unref(msg);
out_free:
	return;
}

static void
_cs_dbus_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip)
{
	DBusMessage *msg = NULL;
	int ret = -1;

	if (err_set) {
		syslog (LOG_ERR, "%s\n", _err);
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
		syslog (LOG_ERR, "%s(%d) error\n", __func__, __LINE__);
		goto out_unlock;
	}

	if (!dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &nodename,
			DBUS_TYPE_UINT32, &nodeid,
			DBUS_TYPE_STRING, &ip,
			DBUS_TYPE_STRING, &state,
			DBUS_TYPE_INVALID)) {
		syslog (LOG_ERR, "%s(%d) error\n", __func__, __LINE__);
		goto out_unlock;
	}

	dbus_connection_send(db, msg, NULL);
	ret = 0;

out_unlock:
	if (ret == -1) {
		syslog (LOG_ERR, "%s() error\n", __func__);
	}
	if (msg)
		dbus_message_unref(msg);
out_free:
	return;
}

static void
_cs_dbus_application_connection_event(char *nodename, uint32_t nodeid, char *app_name, const char *state)
{
	DBusMessage *msg = NULL;
	int ret = -1;

	if (err_set) {
		syslog (LOG_ERR, "%s\n", _err);
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
		syslog (LOG_ERR, "%s(%d) error\n", __func__, __LINE__);
		goto out_unlock;
	}

	if (!dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &nodename,
			DBUS_TYPE_UINT32, &nodeid,
			DBUS_TYPE_STRING, &app_name,
			DBUS_TYPE_STRING, &state,
			DBUS_TYPE_INVALID)) {
		syslog (LOG_ERR, "%s(%d) error\n", __func__, __LINE__);
		goto out_unlock;
	}

	dbus_connection_send(db, msg, NULL);
	ret = 0;

out_unlock:
	if (msg)
		dbus_message_unref(msg);
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
	num_notifiers++;
}

#endif /* HAVE_DBUS */

#ifdef ENABLE_SNMP
static netsnmp_session *snmp_init (const char *target)
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

	session = snmp_add(session,
#ifdef NETSNMPV54
		netsnmp_transport_open_client ("snmptrap", target),
#else
		netsnmp_tdomain_transport (default_port, 0, "udp"),
#endif
		NULL, NULL);

	if (session == NULL) {
		syslog(LOG_ERR, "Could not create snmp transport");
	}
	return (session);
}

static inline void add_field (
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

static void
_cs_snmp_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip)
{
	int ret;
	char csysuptime[CS_TIMESTAMP_STR_LEN];
	static oid snmptrap_oid[]  = { 1,3,6,1,6,3,1,1,4,1,0 };
	static oid sysuptime_oid[] = { 1,3,6,1,2,1,1,3,0 };
	time_t now = time (NULL);

	netsnmp_pdu *trap_pdu;
	netsnmp_session *session = snmp_init (snmp_manager);
	if (session == NULL) {
		syslog (LOG_NOTICE, "Failed to init SNMP session.\n");
		return ;
	}

	trap_pdu = snmp_pdu_create (SNMP_MSG_TRAP2);
	if (!trap_pdu) {
		syslog (LOG_NOTICE, "Failed to create SNMP notification.\n");
		return ;
	}

	/* send uptime */
	snprintf (csysuptime, CS_TIMESTAMP_STR_LEN, "%ld", now);
	snmp_add_var (trap_pdu, sysuptime_oid, sizeof (sysuptime_oid) / sizeof (oid), 't', csysuptime);
	snmp_add_var (trap_pdu, snmptrap_oid, sizeof (snmptrap_oid) / sizeof (oid), 'o', SNMP_OID_TRAPS_NODE);

	/* Add extries to the trap */
	add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_NAME, (void*)nodename, strlen (nodename));
	add_field (trap_pdu, ASN_INTEGER, SNMP_OID_OBJECT_NODE_ID, (void*)&nodeid, sizeof (nodeid));
	add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_ADDR, (void*)ip, strlen (ip));
	add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_STATUS, (void*)state, strlen (state));

	/* Send and cleanup */
	ret = snmp_send (session, trap_pdu);
	if (ret == 0) {
		/* error */
		syslog (LOG_ERR, "Could not send SNMP trap");
		snmp_free_pdu (trap_pdu);
	}
}

static void
_cs_snmp_node_quorum_event(char *nodename, uint32_t nodeid,
			   const char *state)
{
	int ret;
	char csysuptime[20];
	static oid snmptrap_oid[]  = { 1,3,6,1,6,3,1,1,4,1,0 };
	static oid sysuptime_oid[] = { 1,3,6,1,2,1,1,3,0 };
	time_t now = time (NULL);

	netsnmp_pdu *trap_pdu;
	netsnmp_session *session = snmp_init (snmp_manager);
	if (session == NULL) {
		syslog (LOG_NOTICE, "Failed to init SNMP session.\n");
		return ;
	}

	trap_pdu = snmp_pdu_create (SNMP_MSG_TRAP2);
	if (!trap_pdu) {
		syslog (LOG_NOTICE, "Failed to create SNMP notification.\n");
		return ;
	}

	/* send uptime */
	sprintf (csysuptime, "%ld", now);
	snmp_add_var (trap_pdu, sysuptime_oid, sizeof (sysuptime_oid) / sizeof (oid), 't', csysuptime);
	snmp_add_var (trap_pdu, snmptrap_oid, sizeof (snmptrap_oid) / sizeof (oid), 'o', SNMP_OID_TRAPS_NODE);

	/* Add extries to the trap */
	add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_NODE_NAME, (void*)nodename, strlen (nodename));
	add_field (trap_pdu, ASN_INTEGER, SNMP_OID_OBJECT_NODE_ID, (void*)&nodeid, sizeof (nodeid));
	add_field (trap_pdu, ASN_OCTET_STR, SNMP_OID_OBJECT_QUORUM, (void*)state, strlen (state));

	/* Send and cleanup */
	ret = snmp_send (session, trap_pdu);
	if (ret == 0) {
		/* error */
		syslog (LOG_ERR, "Could not send SNMP trap");
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
	num_notifiers++;
}

#endif /* ENABLE_SNMP */

static void
_cs_syslog_node_membership_event(char *nodename, uint32_t nodeid, char *state, char* ip)
{
	syslog (LOG_NOTICE, "%s[%d] ip:%s %s\n", nodename, nodeid, ip, state);
}

static void
_cs_syslog_node_quorum_event(char *nodename, uint32_t nodeid, const char *state)
{
	if (strcmp(state, "quorate") == 0) {
		syslog (LOG_NOTICE, "%s[%d] is now %s\n", nodename, nodeid, state);
	} else {
		syslog (LOG_NOTICE, "%s[%d] has lost quorum\n", nodename, nodeid);
	}
}

static void
_cs_syslog_application_connection_event(char *nodename, uint32_t nodeid, char* app_name, const char *state)
{
	if (strcmp(state, "connected") == 0) {
		syslog (LOG_ERR, "%s[%d] %s is now %s to corosync\n", nodename, nodeid, app_name, state);
	} else {
		syslog (LOG_ERR, "%s[%d] %s is now %s from corosync\n", nodename, nodeid, app_name, state);
	}
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

	if (local_nodeid == 0) {
		rc = corosync_cfg_initialize(&cfg_handle, NULL);
		if (rc != CS_OK) {
			syslog (LOG_ERR, "Failed to initialize the cfg API. Error %d\n", rc);
			exit (EXIT_FAILURE);
		}

		rc = corosync_cfg_local_get (cfg_handle, &local_nodeid);
		corosync_cfg_finalize(cfg_handle);
		if (rc != CS_OK) {
			local_nodeid = 0;
			strncpy(local_nodename, "localhost", sizeof (local_nodename));
			local_nodename[sizeof (local_nodename) - 1] = '\0';
		} else {
			gethostname(local_nodename, CS_MAX_NAME_LENGTH);
		}
	}
	*nodeid = local_nodeid;
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
sig_exit_handler (int num)
{
	poll_stop(poll_handle);
}

static void
_cs_confdb_init(void)
{
	hdb_handle_t obj_handle;
	cs_error_t rc;
	int conf_fd = 0;

	rc = confdb_initialize (&confdb_handle, &callbacks);
	if (rc != CS_OK) {
		syslog (LOG_ERR, "Failed to initialize the objdb API. Error %d\n", rc);
		exit (EXIT_FAILURE);
	}
	confdb_fd_get(confdb_handle, &conf_fd);

	poll_dispatch_add (poll_handle, conf_fd, POLLIN|POLLNVAL, NULL,
		_cs_confdb_dispatch);

	rc = _cs_confdb_find_object (confdb_handle, "runtime.connections.",
		&obj_handle);
	if (rc != CS_OK) {
		syslog (LOG_ERR,
			"Failed to find the connections object. Error %d\n", rc);
		exit (EXIT_FAILURE);
	}

	rc = confdb_track_changes (confdb_handle, obj_handle,
		CONFDB_TRACK_DEPTH_ONE);
	if (rc != CS_OK) {
		syslog (LOG_ERR,
			"Failed to track the connections object. Error %d\n", rc);
		exit (EXIT_FAILURE);
	}
	rc = _cs_confdb_find_object(confdb_handle,
		"runtime.totem.pg.mrp.srp.members.", &obj_handle);
	if (rc != CS_OK) {
		syslog (LOG_ERR, "Failed to find the object. Error %d\n", rc);
		exit (EXIT_FAILURE);
	}

	rc = confdb_track_changes(confdb_handle,
		obj_handle, CONFDB_TRACK_DEPTH_RECURSIVE);
	if (rc != CS_OK) {
		syslog (LOG_ERR,
			"Failed to track the object. Error %d\n", rc);
		exit (EXIT_FAILURE);
	}
}

static void
_cs_confdb_finalize(void)
{
	confdb_stop_track_changes (confdb_handle);
	confdb_finalize (confdb_handle);
}

static void
_cs_check_config(void)
{
	if (conf[CS_NTF_LOG] == 0 &&
		conf[CS_NTF_STDOUT] == 0 &&
		conf[CS_NTF_SNMP] == 0 &&
		conf[CS_NTF_DBUS] == 0) {
		syslog(LOG_ERR, "no event type enabled, see corosync-notifyd -h, exiting.");
		exit(EXIT_FAILURE);
	}

#ifndef ENABLE_SNMP
	if (conf[CS_NTF_SNMP]) {
		syslog(LOG_ERR, "Not compiled with SNMP support enabled, exiting.");
		exit(EXIT_FAILURE);
	}
#endif
#ifndef HAVE_DBUS
	if (conf[CS_NTF_DBUS]) {
		syslog(LOG_ERR, "Not compiled with DBus support enabled, exiting.");
		exit(EXIT_FAILURE);
	}
#endif

	if (conf[CS_NTF_STDOUT] && !conf[CS_NTF_FG]) {
		syslog(LOG_ERR, "configured to print to stdout and run in the background, exiting");
		exit(EXIT_FAILURE);
	}
	if (conf[CS_NTF_SNMP] && conf[CS_NTF_DBUS]) {
		syslog(LOG_ERR, "configured to send snmp traps and dbus signals - are you sure?.");
	}
}

static void
_cs_usage(void)
{
	fprintf(stderr,	"usage:\n"\
		"        -f     : Start application in foreground.\n"\
		"        -l     : Log all events.\n"\
		"        -o     : Print events to stdout (turns on -l).\n"\
		"        -s     : Send SNMP traps on all events.\n"\
		"        -m     : SNMP Manager IP address (defaults to localhost).\n"\
		"        -d     : Send DBUS signals on all events.\n"\
		"        -h     : Print this help\n\n");
}

int
main(int argc, char *argv[])
{
	int ch;

	conf[CS_NTF_FG] = 0;
	conf[CS_NTF_LOG] = 0;
	conf[CS_NTF_STDOUT] = 0;
	conf[CS_NTF_SNMP] = 0;
	conf[CS_NTF_DBUS] = 0;

	while ((ch = getopt (argc, argv, "floshdm:")) != EOF) {
		switch (ch) {
			case 'f':
				conf[CS_NTF_FG] = 1;
				break;
			case 'l':
				conf[CS_NTF_LOG] = 1;
				break;
			case 'm':
				conf[CS_NTF_SNMP] = 1;
				strncpy(snmp_manager_buf, optarg, sizeof (snmp_manager_buf));
				snmp_manager_buf[sizeof (snmp_manager_buf) - 1] = '\0';
				snmp_manager = snmp_manager_buf;
				break;
			case 'o':
				conf[CS_NTF_LOG] = 1;
				conf[CS_NTF_STDOUT] = 1;
				break;
			case 's':
				conf[CS_NTF_SNMP] = 1;
				break;
			case 'd':
				conf[CS_NTF_DBUS] = 1;
				break;
			case 'h':
			default:
				_cs_usage();
				return EXIT_FAILURE;
		}
	}

	if (conf[CS_NTF_STDOUT]) {
		openlog(NULL, LOG_PID|LOG_PERROR, LOG_DAEMON);
	} else {
		openlog(NULL, LOG_PID, LOG_DAEMON);
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
		num_notifiers++;
	}

	poll_handle = poll_create();

	_cs_confdb_init();
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

	(void)signal (SIGINT, sig_exit_handler);
	(void)signal (SIGQUIT, sig_exit_handler);
	(void)signal (SIGTERM, sig_exit_handler);

	poll_run(poll_handle);

#ifdef HAVE_DBUS
	if (conf[CS_NTF_DBUS]) {
		_cs_dbus_release();
	}
#endif /* HAVE_DBUS */

	_cs_quorum_finalize();
	_cs_confdb_finalize();

	return 0;
}

