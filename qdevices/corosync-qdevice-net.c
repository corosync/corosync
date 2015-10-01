/*
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#include <stdio.h>
#include <nss.h>
#include <secerr.h>
#include <sslerr.h>
#include <pk11func.h>
#include <certt.h>
#include <ssl.h>
#include <prio.h>
#include <prnetdb.h>
#include <prerror.h>
#include <prinit.h>
#include <getopt.h>
#include <err.h>
#include <keyhi.h>
#include <poll.h>

/*
 * Needed for creating nspr handle from unix fd
 */
#include <private/pprio.h>

#include <cmap.h>
#include <votequorum.h>

#include "qnetd-defines.h"
#include "dynar.h"
#include "nss-sock.h"
#include "tlv.h"
#include "msg.h"
#include "msgio.h"
#include "qnetd-log.h"
#include "timer-list.h"
#include "send-buffer-list.h"

#define NSS_DB_DIR	COROSYSCONFDIR "/qdevice-net/nssdb"

/*
 * It's usually not a good idea to change following defines
 */
#define QDEVICE_NET_INITIAL_MSG_RECEIVE_SIZE	(1 << 15)
#define QDEVICE_NET_INITIAL_MSG_SEND_SIZE	(1 << 15)
#define QDEVICE_NET_MIN_MSG_SEND_SIZE		QDEVICE_NET_INITIAL_MSG_SEND_SIZE
#define QDEVICE_NET_MAX_MSG_RECEIVE_SIZE	(1 << 24)

#define QNETD_NSS_SERVER_CN		"Qnetd Server"
#define QDEVICE_NET_NSS_CLIENT_CERT_NICKNAME	"Cluster Cert"
#define QDEVICE_NET_VOTEQUORUM_DEVICE_NAME	"QdeviceNet"

#define QDEVICE_NET_MAX_SEND_BUFFERS		10

#define QDEVICE_NET_DEFAULT_ALGORITHM		TLV_DECISION_ALGORITHM_TYPE_TEST

#define qdevice_net_log			qnetd_log
#define qdevice_net_log_nss		qnetd_log_nss
#define qdevice_net_log_init		qnetd_log_init
#define qdevice_net_log_close		qnetd_log_close
#define qdevice_net_log_set_debug	qnetd_log_set_debug

#define QDEVICE_NET_LOG_TARGET_STDERR		QNETD_LOG_TARGET_STDERR
#define QDEVICE_NET_LOG_TARGET_SYSLOG		QNETD_LOG_TARGET_SYSLOG

#define MAX_CS_TRY_AGAIN	10

enum qdevice_net_state {
	QDEVICE_NET_STATE_WAITING_PREINIT_REPLY,
	QDEVICE_NET_STATE_WAITING_STARTTLS_BEING_SENT,
	QDEVICE_NET_STATE_WAITING_INIT_REPLY,
	QDEVICE_NET_STATE_WAITING_SET_OPTION_REPLY,
	QDEVICE_NET_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS,
};

struct qdevice_net_instance {
	PRFileDesc *socket;
	size_t initial_send_size;
	size_t initial_receive_size;
	size_t max_receive_size;
	size_t min_send_size;
	struct dynar receive_buffer;
	struct send_buffer_list send_buffer_list;
	int skipping_msg;
	size_t msg_already_received_bytes;
	enum qdevice_net_state state;
	uint32_t last_msg_seq_num;
	uint32_t echo_request_expected_msg_seq_num;
	uint32_t echo_reply_received_msg_seq_num;
	enum tlv_tls_supported tls_supported;
	int using_tls;
	uint32_t node_id;
	uint32_t heartbeat_interval;		/* Heartbeat interval during normal operation */
	uint32_t sync_heartbeat_interval;	/* Heartbeat interval during corosync sync */
	const char *host_addr;
	uint16_t host_port;
	const char *cluster_name;
	enum tlv_decision_algorithm_type decision_algorithm;
	struct timer_list main_timer_list;
	struct timer_list_entry *echo_request_timer;
	int schedule_disconnect;
	cmap_handle_t cmap_handle;
	votequorum_handle_t votequorum_handle;
	PRFileDesc *votequorum_poll_fd;
};

static votequorum_ring_id_t global_last_received_ring_id;

static void
err_nss(void) {
	errx(1, "nss error %d: %s", PR_GetError(), PR_ErrorToString(PR_GetError(),
	    PR_LANGUAGE_I_DEFAULT));
}

static SECStatus
qdevice_net_nss_bad_cert_hook(void *arg, PRFileDesc *fd) {
	if (PR_GetError() == SEC_ERROR_EXPIRED_CERTIFICATE ||
	    PR_GetError() == SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE ||
	    PR_GetError() == SEC_ERROR_CRL_EXPIRED ||
	    PR_GetError() == SEC_ERROR_KRL_EXPIRED ||
	    PR_GetError() == SSL_ERROR_EXPIRED_CERT_ALERT) {
		qdevice_net_log(LOG_WARNING, "Server certificate is expired.");

		return (SECSuccess);
        }

	qdevice_net_log_nss(LOG_ERR, "Server certificate verification failure.");

	return (SECFailure);
}

static SECStatus
qdevice_net_nss_get_client_auth_data(void *arg, PRFileDesc *sock, struct CERTDistNamesStr *caNames,
    struct CERTCertificateStr **pRetCert, struct SECKEYPrivateKeyStr **pRetKey)
{
	qdevice_net_log(LOG_DEBUG, "Sending client auth data.");

	return (NSS_GetClientAuthData(arg, sock, caNames, pRetCert, pRetKey));
}

static int
qdevice_net_schedule_echo_request_send(struct qdevice_net_instance *instance)
{
	struct send_buffer_list_entry *send_buffer;

	if (instance->echo_reply_received_msg_seq_num !=
	    instance->echo_request_expected_msg_seq_num) {
		qdevice_net_log(LOG_ERR, "Server didn't send echo reply message on time. "
		    "Disconnecting from server.");
		return (-1);
	}

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_CRIT, "Can't allocate send list buffer for reply msg.");

		return (-1);
	}

	instance->echo_request_expected_msg_seq_num++;

	if (msg_create_echo_request(&send_buffer->buffer, 1,
	    instance->echo_request_expected_msg_seq_num) == -1) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for echo request msg");

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}

static void
qdevice_net_log_msg_decode_error(int ret)
{

	switch (ret) {
	case -1:
		qdevice_net_log(LOG_WARNING, "Received message with option with invalid length");
		break;
	case -2:
		qdevice_net_log(LOG_CRIT, "Can't allocate memory");
		break;
	case -3:
		qdevice_net_log(LOG_WARNING, "Received inconsistent msg (tlv len > msg size)");
		break;
	case -4:
		qdevice_net_log(LOG_ERR, "Received message with option with invalid value");
		break;
	default:
		qdevice_net_log(LOG_ERR, "Unknown error occured when decoding message");
		break;
	}
}

/*
 * -1 - Incompatible tls combination
 *  0 - Don't use TLS
 *  1 - Use TLS
 */
static int
qdevice_net_check_tls_compatibility(enum tlv_tls_supported server_tls,
    enum tlv_tls_supported client_tls)
{
	int res;

	res = -1;

	switch (server_tls) {
	case TLV_TLS_UNSUPPORTED:
		switch (client_tls) {
		case TLV_TLS_UNSUPPORTED: res = 0; break;
		case TLV_TLS_SUPPORTED: res = 0; break;
		case TLV_TLS_REQUIRED: res = -1; break;
		}
		break;
	case TLV_TLS_SUPPORTED:
		switch (client_tls) {
		case TLV_TLS_UNSUPPORTED: res = 0; break;
		case TLV_TLS_SUPPORTED: res = 1; break;
		case TLV_TLS_REQUIRED: res = 1; break;
		}
		break;
	case TLV_TLS_REQUIRED:
		switch (client_tls) {
		case TLV_TLS_UNSUPPORTED: res = -1; break;
		case TLV_TLS_SUPPORTED: res = 1; break;
		case TLV_TLS_REQUIRED: res = 1; break;
		}
		break;
	}

	return (res);
}

static int
qdevice_net_msg_received_preinit(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected preinit message. Disconnecting from server");

	return (-1);
}

static int
qdevice_net_msg_check_seq_number(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (!msg->seq_number_set || msg->seq_number != instance->last_msg_seq_num) {
		qdevice_net_log(LOG_ERR, "Received message doesn't contain seq_number or "
		    "it's not expected one.");

		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_check_echo_reply_seq_number(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (!msg->seq_number_set) {
		qdevice_net_log(LOG_ERR, "Received echo reply message doesn't contain seq_number.");

		return (-1);
	}

	if (msg->seq_number != instance->echo_request_expected_msg_seq_num) {
		qdevice_net_log(LOG_ERR, "Server doesn't replied in expected time. "
		    "Closing connection");

		return (-1);
	}

	return (0);
}

static int
qdevice_net_send_init(struct qdevice_net_instance *instance)
{
	enum msg_type *supported_msgs;
	size_t no_supported_msgs;
	enum tlv_opt_type *supported_opts;
	size_t no_supported_opts;
	struct send_buffer_list_entry *send_buffer;

	tlv_get_supported_options(&supported_opts, &no_supported_opts);
	msg_get_supported_messages(&supported_msgs, &no_supported_msgs);
	instance->last_msg_seq_num++;

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for init msg");

		return (-1);
	}

	if (msg_create_init(&send_buffer->buffer, 1, instance->last_msg_seq_num,
	    instance->decision_algorithm,
	    supported_msgs, no_supported_msgs, supported_opts, no_supported_opts,
	    instance->node_id) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for init msg");

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	instance->state = QDEVICE_NET_STATE_WAITING_INIT_REPLY;

	return (0);
}


static int
qdevice_net_msg_received_preinit_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	int res;
	struct send_buffer_list_entry *send_buffer;

	if (instance->state != QDEVICE_NET_STATE_WAITING_PREINIT_REPLY) {
		qdevice_net_log(LOG_ERR, "Received unexpected preinit reply message. "
		    "Disconnecting from server");

		return (-1);
	}

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		return (-1);
	}

	/*
	 * Check TLS support
	 */
	if (!msg->tls_supported_set || !msg->tls_client_cert_required_set) {
		qdevice_net_log(LOG_ERR, "Required tls_supported or tls_client_cert_required "
		    "option is unset");

		return (-1);
	}

	res = qdevice_net_check_tls_compatibility(msg->tls_supported, instance->tls_supported);
	if (res == -1) {
		qdevice_net_log(LOG_ERR, "Incompatible tls configuration (server %u client %u)",
		    msg->tls_supported, instance->tls_supported);

		return (-1);
	} else if (res == 1) {
		/*
		 * Start TLS
		 */
		send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
		if (send_buffer == NULL) {
			qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for "
			    "starttls msg");

			return (-1);
		}

		instance->last_msg_seq_num++;
		if (msg_create_starttls(&send_buffer->buffer, 1,
		    instance->last_msg_seq_num) == 0) {
			qdevice_net_log(LOG_ERR, "Can't allocate send buffer for starttls msg");

			return (-1);
		}

		send_buffer_list_put(&instance->send_buffer_list, send_buffer);

		instance->state = QDEVICE_NET_STATE_WAITING_STARTTLS_BEING_SENT;
	} else if (res == 0) {
		if (qdevice_net_send_init(instance) != 0) {
			return (-1);
		}
	}

	return (0);
}

static int
qdevice_net_msg_received_init_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	size_t zi;
	int res;
	struct send_buffer_list_entry *send_buffer;

	if (instance->state != QDEVICE_NET_STATE_WAITING_INIT_REPLY) {
		qdevice_net_log(LOG_ERR, "Received unexpected init reply message. "
		    "Disconnecting from server");

		return (-1);
	}

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		return (-1);
	}

	if (!msg->reply_error_code_set) {
		qdevice_net_log(LOG_ERR, "Received init reply message without error code."
		    "Disconnecting from server");

		return (-1);
	}

	if (msg->reply_error_code != TLV_REPLY_ERROR_CODE_NO_ERROR) {
		qdevice_net_log(LOG_ERR, "Received init reply message with error code %"PRIu16". "
		    "Disconnecting from server", msg->reply_error_code);

		return (-1);
	}

	if (!msg->server_maximum_request_size_set || !msg->server_maximum_reply_size_set) {
		qdevice_net_log(LOG_ERR, "Required maximum_request_size or maximum_reply_size "
		    "option is unset");

		return (-1);
	}

	if (msg->supported_messages == NULL || msg->supported_options == NULL) {
		qdevice_net_log(LOG_ERR, "Required supported messages or supported options "
		    "option is unset");

		return (-1);
	}

	if (msg->supported_decision_algorithms == NULL) {
		qdevice_net_log(LOG_ERR, "Required supported decision algorithms option is unset");

		return (-1);
	}

	if (msg->server_maximum_request_size < instance->min_send_size) {
		qdevice_net_log(LOG_ERR,
		    "Server accepts maximum %zu bytes message but this client minimum "
		    "is %zu bytes.", msg->server_maximum_request_size, instance->min_send_size);

		return (-1);
	}

	if (msg->server_maximum_reply_size > instance->max_receive_size) {
		qdevice_net_log(LOG_ERR,
		    "Server may send message up to %zu bytes message but this client maximum "
		    "is %zu bytes.", msg->server_maximum_reply_size, instance->max_receive_size);

		return (-1);
	}

	/*
	 * Change buffer sizes
	 */
	dynar_set_max_size(&instance->receive_buffer, msg->server_maximum_reply_size);
	send_buffer_list_set_max_buffer_size(&instance->send_buffer_list,
	    msg->server_maximum_request_size);


	/*
	 * Check if server supports decision algorithm we need
	 */
	res = 0;

	for (zi = 0; zi < msg->no_supported_decision_algorithms && !res; zi++) {
		if (msg->supported_decision_algorithms[zi] == instance->decision_algorithm) {
			res = 1;
		}
	}

	if (!res) {
		qdevice_net_log(LOG_ERR, "Server doesn't support required decision algorithm");

		return (-1);
	}

	/*
	 * Send set options message
	 */
	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for set option msg");

		return (-1);
	}

	instance->last_msg_seq_num++;

	if (msg_create_set_option(&send_buffer->buffer, 1, instance->last_msg_seq_num,
	    1, instance->heartbeat_interval) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for set option msg");

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	instance->state = QDEVICE_NET_STATE_WAITING_SET_OPTION_REPLY;

	return (0);
}

static int
qdevice_net_msg_received_starttls(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected starttls message. Disconnecting from server");

	return (-1);
}

static int
qdevice_net_msg_received_server_error(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (!msg->reply_error_code_set) {
		qdevice_net_log(LOG_ERR, "Received server error without error code set. "
		    "Disconnecting from server");
	} else {
		qdevice_net_log(LOG_ERR, "Received server error %"PRIu16". "
		    "Disconnecting from server", msg->reply_error_code);
	}

	return (-1);
}

static int
qdevice_net_msg_received_set_option(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected set option message. "
	    "Disconnecting from server");

	return (-1);
}

static int
qdevice_net_timer_send_heartbeat(void *data1, void *data2)
{
	struct qdevice_net_instance *instance;

	instance = (struct qdevice_net_instance *)data1;

	if (qdevice_net_schedule_echo_request_send(instance) == -1) {
		instance->schedule_disconnect = 1;
		return (0);
	}

	/*
	 * Schedule this function callback again
	 */
	return (-1);
}

static uint32_t
qdevice_net_autogenerate_node_id(const char *addr, int clear_node_high_byte)
{
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	int ret, i;

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	/*
	 * Hardcoded AF_INET because autogenerated nodeid is valid only for ipv4
	 */
	ahints.ai_family   = AF_INET;

	ret = getaddrinfo(addr, NULL, &ahints, &ainfo);
	if (ret != 0)
		return (0);

	if (ainfo->ai_family != AF_INET) {

		freeaddrinfo(ainfo);

		return (0);
	}

        memcpy(&i, &((struct sockaddr_in *)ainfo->ai_addr)->sin_addr, sizeof(struct in_addr));
	freeaddrinfo(ainfo);

	ret = htonl(i);

	if (clear_node_high_byte) {
		ret &= 0x7FFFFFFF;
	}

	return (ret);
}

static int
qdevice_net_get_nodelist(cmap_handle_t cmap_handle, struct node_list *list)
{
	cs_error_t cs_err;
	cmap_iter_handle_t iter_handle;
	char key_name[CMAP_KEYNAME_MAXLEN + 1];
	char tmp_key[CMAP_KEYNAME_MAXLEN + 1];
	int res;
	int ret_value;
	unsigned int node_pos;
	uint32_t node_id;
	uint32_t data_center_id;
	char *tmp_str;
	char *addr0_str;
	int clear_node_high_byte;

	ret_value = 0;

	node_list_init(list);

	cs_err = cmap_iter_init(cmap_handle, "nodelist.node.", &iter_handle);
	if (cs_err != CS_OK) {
		return (-1);
	}

	while ((cs_err = cmap_iter_next(cmap_handle, iter_handle, key_name, NULL, NULL)) == CS_OK) {
		res = sscanf(key_name, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", node_pos);
		cs_err = cmap_get_uint32(cmap_handle, tmp_key, &node_id);

		if (cs_err == CS_ERR_NOT_EXIST) {
			/*
			 * Nodeid doesn't exists -> autogenerate node id
			 */
			clear_node_high_byte = 0;

			if (cmap_get_string(cmap_handle, "totem.clear_node_high_bit", &tmp_str) == CS_OK) {
				if (strcmp (tmp_str, "yes") == 0) {
					clear_node_high_byte = 1;
				}

				free(tmp_str);
			}

			if (cmap_get_string(cmap_handle, key_name, &addr0_str) != CS_OK) {
				return (-1);
			}

			node_id = qdevice_net_autogenerate_node_id(addr0_str, clear_node_high_byte);

			free(addr0_str);
		} else if (cs_err != CS_OK) {
			ret_value = -1;

			goto iter_finalize;
		}

		snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, "nodelist.node.%u.datacenterid", node_pos);
		if (cmap_get_uint32(cmap_handle, tmp_key, &data_center_id) != CS_OK) {
			data_center_id = 0;
		}

		if (node_list_add(list, node_id, data_center_id, TLV_NODE_STATE_NOT_SET) == NULL) {
			ret_value = -1;

			goto iter_finalize;
		}
	}

iter_finalize:
	cmap_iter_finalize(cmap_handle, iter_handle);

	if (ret_value != 0) {
		node_list_free(list);
	}

	return (ret_value);
}

static
int qdevice_net_get_cmap_config_version(cmap_handle_t cmap_handle, uint64_t *config_version)
{
	int res;

	if (cmap_get_uint64(cmap_handle, "totem.config_version", config_version) == CS_OK) {
		res = 1;
	} else {
		*config_version = 0;
		res = 0;
	}

	return (res);
}

static int
qdevice_net_send_config_node_list(struct qdevice_net_instance *instance, int initial)
{
	struct node_list nlist;
	struct send_buffer_list_entry *send_buffer;
	uint64_t config_version;
	int send_config_version;

	/*
	 * Send initial node list
	 */
	if (qdevice_net_get_nodelist(instance->cmap_handle, &nlist) != 0) {
		qdevice_net_log(LOG_ERR, "Can't get initial configuration node list.");

		return (-1);
	}

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for config "
		    "node list msg");

		node_list_free(&nlist);

		return (-1);
	}

	send_config_version = qdevice_net_get_cmap_config_version(instance->cmap_handle,
	    &config_version);

	instance->last_msg_seq_num++;

	if (msg_create_node_list(&send_buffer->buffer, instance->last_msg_seq_num,
	    (initial ? TLV_NODE_LIST_TYPE_INITIAL_CONFIG : TLV_NODE_LIST_TYPE_CHANGED_CONFIG),
	    0, NULL, send_config_version, config_version, 0, TLV_QUORATE_INQUORATE, &nlist) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for config list msg");

		node_list_free(&nlist);

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}

static int
qdevice_net_register_votequorum_callbacks(struct qdevice_net_instance *instance)
{
	cs_error_t res;

	if ((res = votequorum_trackstart(instance->votequorum_handle, 0, CS_TRACK_CHANGES)) != CS_OK) {
		qdevice_net_log(LOG_ERR, "Can't start tracking votequorum changes. Error %s",
		    cs_strerror(res));

		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_set_option_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		return (-1);
	}

	if (!msg->decision_algorithm_set || !msg->heartbeat_interval_set) {
		qdevice_net_log(LOG_ERR, "Received set option reply message without "
		    "required options. Disconnecting from server");
	}

	if (msg->decision_algorithm != instance->decision_algorithm ||
	    msg->heartbeat_interval != instance->heartbeat_interval) {
		qdevice_net_log(LOG_ERR, "Server doesn't accept sent decision algorithm or "
		    "heartbeat interval.");

		return (-1);
	}

	/*
	 * Server accepted heartbeat interval -> schedule regular sending of echo request
	 */
	if (instance->heartbeat_interval > 0) {
		instance->echo_request_timer = timer_list_add(&instance->main_timer_list,
		    instance->heartbeat_interval, qdevice_net_timer_send_heartbeat,
		    (void *)instance, NULL);

		if (instance->echo_request_timer == NULL) {
			qdevice_net_log(LOG_ERR, "Can't schedule regular sending of heartbeat.");

			return (-1);
		}
	}

	/*
	 * Now we can finally really send node list and initialize qdevice
	 */
	if (qdevice_net_send_config_node_list(instance, 1) != 0) {
		return (-1);
	}

	if (qdevice_net_register_votequorum_callbacks(instance) != 0) {
		return (-1);
	}

	instance->state = QDEVICE_NET_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS;

	return (0);
}

static int
qdevice_net_msg_received_echo_request(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected echo request message. "
	    "Disconnecting from server");

	return (-1);
}

static int
qdevice_net_msg_received_echo_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (qdevice_net_msg_check_echo_reply_seq_number(instance, msg) != 0) {
		return (-1);
	}

	instance->echo_reply_received_msg_seq_num = msg->seq_number;

	return (0);
}

static int
qdevice_net_msg_received_node_list(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected echo request message. "
	    "Disconnecting from server");

	return (-1);
}

static int
qdevice_net_msg_received_node_list_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_INFO, "Received node list reply %u", msg->vote);

	return (0);
}

static int
qdevice_net_msg_received(struct qdevice_net_instance *instance)
{
	struct msg_decoded msg;
	int res;
	int ret_val;

	msg_decoded_init(&msg);

	res = msg_decode(&instance->receive_buffer, &msg);
	if (res != 0) {
		/*
		 * Error occurred. Disconnect.
		 */
		qdevice_net_log_msg_decode_error(res);
		qdevice_net_log(LOG_ERR, "Disconnecting from server");

		return (-1);
	}

	ret_val = 0;

	switch (msg.type) {
	case MSG_TYPE_PREINIT:
		ret_val = qdevice_net_msg_received_preinit(instance, &msg);
		break;
	case MSG_TYPE_PREINIT_REPLY:
		ret_val = qdevice_net_msg_received_preinit_reply(instance, &msg);
		break;
	case MSG_TYPE_STARTTLS:
		ret_val = qdevice_net_msg_received_starttls(instance, &msg);
		break;
	case MSG_TYPE_SERVER_ERROR:
		ret_val = qdevice_net_msg_received_server_error(instance, &msg);
		break;
	case MSG_TYPE_INIT_REPLY:
		ret_val = qdevice_net_msg_received_init_reply(instance, &msg);
		break;
	case MSG_TYPE_SET_OPTION:
		ret_val = qdevice_net_msg_received_set_option(instance, &msg);
		break;
	case MSG_TYPE_SET_OPTION_REPLY:
		ret_val = qdevice_net_msg_received_set_option_reply(instance, &msg);
		break;
	case MSG_TYPE_ECHO_REQUEST:
		ret_val = qdevice_net_msg_received_echo_request(instance, &msg);
		break;
	case MSG_TYPE_ECHO_REPLY:
		ret_val = qdevice_net_msg_received_echo_reply(instance, &msg);
		break;
	case MSG_TYPE_NODE_LIST:
		ret_val = qdevice_net_msg_received_node_list(instance, &msg);
		break;
	case MSG_TYPE_NODE_LIST_REPLY:
		ret_val = qdevice_net_msg_received_node_list_reply(instance, &msg);
		break;
	default:
		qdevice_net_log(LOG_ERR, "Received unsupported message %u. "
		    "Disconnecting from server", msg.type);
		ret_val = -1;
		break;
	}

	msg_decoded_destroy(&msg);

	return (ret_val);
}

/*
 * -1 means end of connection (EOF) or some other unhandled error. 0 = success
 */
static int
qdevice_net_socket_read(struct qdevice_net_instance *instance)
{
	int res;
	int ret_val;
	int orig_skipping_msg;

	orig_skipping_msg = instance->skipping_msg;

	res = msgio_read(instance->socket, &instance->receive_buffer,
	    &instance->msg_already_received_bytes, &instance->skipping_msg);

	if (!orig_skipping_msg && instance->skipping_msg) {
		qdevice_net_log(LOG_DEBUG, "msgio_read set skipping_msg");
	}

	ret_val = 0;

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		qdevice_net_log(LOG_DEBUG, "Server closed connection");
		ret_val = -1;
		break;
	case -2:
		qdevice_net_log_nss(LOG_ERR, "Unhandled error when reading from server. "
		    "Disconnecting from server");
		ret_val = -1;
		break;
	case -3:
		qdevice_net_log(LOG_ERR, "Can't store message header from server. "
		    "Disconnecting from server");
		ret_val = -1;
		break;
	case -4:
		qdevice_net_log(LOG_ERR, "Can't store message from server. "
		    "Disconnecting from server");
		ret_val = -1;
		break;
	case -5:
		qdevice_net_log(LOG_WARNING, "Server sent unsupported msg type %u. "
		    "Disconnecting from server", msg_get_type(&instance->receive_buffer));
		ret_val = -1;
		break;
	case -6:
		qdevice_net_log(LOG_WARNING,
		    "Server wants to send too long message %u bytes. Disconnecting from server",
		    msg_get_len(&instance->receive_buffer));
		ret_val = -1;
		break;
	case 1:
		/*
		 * Full message received / skipped
		 */
		if (!instance->skipping_msg) {
			if (qdevice_net_msg_received(instance) == -1) {
				ret_val = -1;
			}
		} else {
			errx(1, "net_socket_read in skipping msg state");
		}

		instance->skipping_msg = 0;
		instance->msg_already_received_bytes = 0;
		dynar_clean(&instance->receive_buffer);
		break;
	default:
		errx(1, "qdevice_net_socket_read unhandled error %d", res);
		break;
	}

	return (ret_val);
}

static int
qdevice_net_socket_write_finished(struct qdevice_net_instance *instance)
{
	PRFileDesc *new_pr_fd;

	if (instance->state == QDEVICE_NET_STATE_WAITING_STARTTLS_BEING_SENT) {
		/*
		 * StartTLS sent to server. Begin with TLS handshake
		 */
		if ((new_pr_fd = nss_sock_start_ssl_as_client(instance->socket, QNETD_NSS_SERVER_CN,
		    qdevice_net_nss_bad_cert_hook,
		    qdevice_net_nss_get_client_auth_data,
		    (void *)QDEVICE_NET_NSS_CLIENT_CERT_NICKNAME, 0, NULL)) == NULL) {
			qdevice_net_log_nss(LOG_ERR, "Can't start TLS");

			return (-1);
		}

		/*
		 * And send init msg
		 */
		if (qdevice_net_send_init(instance) != 0) {
			return (-1);
		}

		instance->socket = new_pr_fd;
	}

	return (0);
}

static int
qdevice_net_socket_write(struct qdevice_net_instance *instance)
{
	int res;
	struct send_buffer_list_entry *send_buffer;
	enum msg_type sent_msg_type;

	send_buffer = send_buffer_list_get_active(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_CRIT, "send_buffer_list_get_active returned NULL");

		return (-1);
	}

	res = msgio_write(instance->socket, &send_buffer->buffer,
	    &send_buffer->msg_already_sent_bytes);

	if (res == 1) {
		sent_msg_type = msg_get_type(&send_buffer->buffer);

		send_buffer_list_delete(&instance->send_buffer_list, send_buffer);

		if (sent_msg_type != MSG_TYPE_ECHO_REQUEST) {
			if (qdevice_net_socket_write_finished(instance) == -1) {
				return (-1);
			}
		}
	}

	if (res == -1) {
		qdevice_net_log_nss(LOG_CRIT, "PR_Send returned 0");

		return (-1);
	}

	if (res == -2) {
		qdevice_net_log_nss(LOG_ERR, "Unhandled error when sending message to server");

		return (-1);
	}

	return (0);
}


#define QDEVICE_NET_POLL_NO_FDS		2
#define QDEVICE_NET_POLL_SOCKET		0
#define QDEVICE_NET_POLL_VOTEQUORUM	1

static int
qdevice_net_poll(struct qdevice_net_instance *instance)
{
	PRPollDesc pfds[QDEVICE_NET_POLL_NO_FDS];
	PRInt32 poll_res;
	int i;

	pfds[QDEVICE_NET_POLL_SOCKET].fd = instance->socket;
	pfds[QDEVICE_NET_POLL_SOCKET].in_flags = PR_POLL_READ;
	if (!send_buffer_list_empty(&instance->send_buffer_list)) {
		pfds[QDEVICE_NET_POLL_SOCKET].in_flags |= PR_POLL_WRITE;
	}
	pfds[QDEVICE_NET_POLL_VOTEQUORUM].fd = instance->votequorum_poll_fd;
	pfds[QDEVICE_NET_POLL_VOTEQUORUM].in_flags = PR_POLL_READ;

	instance->schedule_disconnect = 0;

	if ((poll_res = PR_Poll(pfds, QDEVICE_NET_POLL_NO_FDS,
	    timer_list_time_to_expire(&instance->main_timer_list))) > 0) {
		for (i = 0; i < QDEVICE_NET_POLL_NO_FDS; i++) {
			if (pfds[i].out_flags & PR_POLL_READ) {
				switch (i) {
				case QDEVICE_NET_POLL_SOCKET:
					if (qdevice_net_socket_read(instance) == -1) {
						instance->schedule_disconnect = 1;
					}

					break;
				case QDEVICE_NET_POLL_VOTEQUORUM:
					if (votequorum_dispatch(instance->votequorum_handle,
					    CS_DISPATCH_ALL) != CS_OK) {
						errx(1, "Can't dispatch votequorum messages");
					}
					break;
				default:
					errx(1, "Unhandled read poll descriptor %u", i);
					break;
				}
			}

			if (!instance->schedule_disconnect && pfds[i].out_flags & PR_POLL_WRITE) {
				switch (i) {
				case QDEVICE_NET_POLL_SOCKET:
					if (qdevice_net_socket_write(instance) == -1) {
						instance->schedule_disconnect = 1;
					}

					break;
				default:
					errx(1, "Unhandled write poll descriptor %u", i);
					break;
				}
			}

			if (!instance->schedule_disconnect &&
			    pfds[i].out_flags &
			    (PR_POLL_ERR|PR_POLL_NVAL|PR_POLL_HUP|PR_POLL_EXCEPT)) {
				switch (i) {
				case QDEVICE_NET_POLL_SOCKET:
					qdevice_net_log(LOG_CRIT, "POLL_ERR (%u) on main socket",
					    pfds[i].out_flags);

					return (-1);

					break;
				default:
					errx(1, "Unhandled poll err on descriptor %u", i);
					break;
				}
			}
		}
	}

	if (!instance->schedule_disconnect) {
		timer_list_expire(&instance->main_timer_list);
	}

	if (instance->schedule_disconnect) {
		/*
		 * Schedule disconnect can be set by this function, by some timer_list callback
		 * or cmap/votequorum callbacks
		 */
		return (-1);
	}

	return (0);
}

static int
qdevice_net_instance_init(struct qdevice_net_instance *instance, size_t initial_receive_size,
    size_t initial_send_size, size_t min_send_size, size_t max_receive_size,
    enum tlv_tls_supported tls_supported, uint32_t node_id,
    enum tlv_decision_algorithm_type decision_algorithm, uint32_t heartbeat_interval,
    const char *host_addr, uint16_t host_port, const char *cluster_name)
{

	memset(instance, 0, sizeof(*instance));

	instance->initial_receive_size = initial_receive_size;
	instance->initial_send_size = initial_send_size;
	instance->min_send_size = min_send_size;
	instance->max_receive_size = max_receive_size;
	instance->node_id = node_id;
	instance->decision_algorithm = decision_algorithm;
	instance->heartbeat_interval = heartbeat_interval;
	instance->host_addr = host_addr;
	instance->host_port = host_port;
	instance->cluster_name = cluster_name;
	dynar_init(&instance->receive_buffer, initial_receive_size);
	send_buffer_list_init(&instance->send_buffer_list, QDEVICE_NET_MAX_SEND_BUFFERS,
	    initial_send_size);
	timer_list_init(&instance->main_timer_list);

	instance->tls_supported = tls_supported;

	return (0);
}

static int
qdevice_net_instance_destroy(struct qdevice_net_instance *instance)
{

	timer_list_free(&instance->main_timer_list);
	dynar_destroy(&instance->receive_buffer);
	send_buffer_list_free(&instance->send_buffer_list);

	/*
	 * Close cmap and votequorum connections
	 */
	if (votequorum_qdevice_unregister(instance->votequorum_handle,
	    QDEVICE_NET_VOTEQUORUM_DEVICE_NAME) != CS_OK) {
		qdevice_net_log_nss(LOG_WARNING, "Unable to unregister votequorum device");
	}
	votequorum_finalize(instance->votequorum_handle);
	cmap_finalize(instance->cmap_handle);

	return (0);
}

static void
qdevice_net_init_cmap(cmap_handle_t *handle)
{
	cs_error_t res;
	int no_retries;

	no_retries = 0;

	while ((res = cmap_initialize(handle)) == CS_ERR_TRY_AGAIN &&
	    no_retries++ < MAX_CS_TRY_AGAIN) {
		poll(NULL, 0, 1000);
	}

        if (res != CS_OK) {
		errx(1, "Failed to initialize the cmap API. Error %s", cs_strerror(res));
	}
}

/*
 * Check string to value on, off, yes, no, 0, 1. Return 1 if value is on, yes or 1, 0 if
 * value is off, no or 0 and -1 otherwise.
 */
static int
qdevice_net_parse_bool_str(const char *str)
{

	if (strcasecmp(str, "yes") == 0 ||
	    strcasecmp(str, "on") == 0 ||
	    strcasecmp(str, "1") == 0) {
		return (1);
	} else if (strcasecmp(str, "no") == 0 ||
	    strcasecmp(str, "off") == 0 ||
	    strcasecmp(str, "0") == 0) {
		return (0);
	}

	return (-1);
}

static void
qdevice_net_instance_init_from_cmap(struct qdevice_net_instance *instance,
    cmap_handle_t cmap_handle)
{
	uint32_t node_id;
	enum tlv_tls_supported tls_supported;
	int i;
	char *str;
	enum tlv_decision_algorithm_type decision_algorithm;
	uint32_t heartbeat_interval;
	uint32_t sync_heartbeat_interval;
	char *host_addr;
	int host_port;
	char *ep;
	char *cluster_name;

	/*
	 * Check if provider is net
	 */
	if (cmap_get_string(cmap_handle, "quorum.device.model", &str) != CS_OK) {
		errx(1, "Can't read quorum.device.model cmap key.");
	}

	if (strcmp(str, "net") != 0) {
		free(str);
		errx(1, "Configured device model is not net. "
		    "This qdevice provider is only for net.");
	}
	free(str);

	/*
	 * Get nodeid
	 */
	if (cmap_get_uint32(cmap_handle, "runtime.votequorum.this_node_id", &node_id) != CS_OK) {
		errx(1, "Unable to retrive this node nodeid.");
	}

	/*
	 * Check tls
	 */
	if (cmap_get_string(cmap_handle, "quorum.device.net.tls", &str) == CS_OK) {
		if ((i = qdevice_net_parse_bool_str(str)) == -1) {
			free(str);
			errx(1, "quorum.device.net.tls value is not valid.");
		}

		if (i == 1) {
			tls_supported = TLV_TLS_SUPPORTED;
		} else {
			tls_supported = TLV_TLS_UNSUPPORTED;
		}

		free(str);
	}

	/*
	 * Host
	 */
	if (cmap_get_string(cmap_handle, "quorum.device.net.host", &str) != CS_OK) {
		free(str);
		errx(1, "Qdevice net daemon address is not defined (quorum.device.net.host)");
	}
	host_addr = str;

	if (cmap_get_string(cmap_handle, "quorum.device.net.port", &str) == CS_OK) {
		host_port = strtol(str, &ep, 10);


		if (host_port <= 0 || host_port > ((uint16_t)~0) || *ep != '\0') {
			errx(1, "quorum.device.net.port must be in range 0-65535");
		}

		free(str);
	} else {
		host_port = QNETD_DEFAULT_HOST_PORT;
	}

	/*
	 * Cluster name
	 */
	if (cmap_get_string(cmap_handle, "totem.cluster_name", &str) != CS_OK) {
		errx(1, "Cluster name (totem.cluster_name) has to be defined.");
	}
	cluster_name = str;

	/*
	 * Configure timeouts
	 */
	if (cmap_get_uint32(cmap_handle, "quorum.device.timeout", &heartbeat_interval) != CS_OK) {
		heartbeat_interval = VOTEQUORUM_QDEVICE_DEFAULT_TIMEOUT;
	}
	heartbeat_interval = heartbeat_interval * 0.8;

	if (cmap_get_uint32(cmap_handle, "quorum.device.sync_timeout",
	    &sync_heartbeat_interval) != CS_OK) {
		sync_heartbeat_interval = VOTEQUORUM_QDEVICE_DEFAULT_SYNC_TIMEOUT;
	}
	sync_heartbeat_interval = sync_heartbeat_interval * 0.8;


	/*
	 * Choose decision algorithm
	 */
	if (cmap_get_string(cmap_handle, "quorum.device.net.algorithm", &str) != CS_OK) {
		decision_algorithm = QDEVICE_NET_DEFAULT_ALGORITHM;
	} else {
		if (strcmp(str, "test") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_TEST;
		} else if (strcmp(str, "ffsplit") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_FFSPLIT;
		} else {
			errx(1, "Unknown decision algorithm %s", str);
		}

		free(str);
	}

	/*
	 * Really initialize instance
	 */
	if (qdevice_net_instance_init(instance,
	    QDEVICE_NET_INITIAL_MSG_RECEIVE_SIZE, QDEVICE_NET_INITIAL_MSG_SEND_SIZE,
	    QDEVICE_NET_MIN_MSG_SEND_SIZE, QDEVICE_NET_MAX_MSG_RECEIVE_SIZE,
	    tls_supported, node_id, decision_algorithm,
	    heartbeat_interval,
	    host_addr, host_port, cluster_name) == -1) {
		errx(1, "Can't initialize qdevice-net");
	}

	instance->cmap_handle = cmap_handle;
}

static enum tlv_node_state
qdevice_net_convert_votequorum_to_tlv_node_state(uint32_t votequorum_node_state)
{
	enum tlv_node_state res;

	switch (votequorum_node_state) {
	case VOTEQUORUM_NODESTATE_MEMBER: res = TLV_NODE_STATE_MEMBER; break;
	case VOTEQUORUM_NODESTATE_DEAD: res = TLV_NODE_STATE_DEAD; break;
	case VOTEQUORUM_NODESTATE_LEAVING: res = TLV_NODE_STATE_LEAVING; break;
	default:
		errx(1, "qdevice_net_convert_votequorum_to_tlv_node_state: Unhandled votequorum "
		    "node state %"PRIu32, votequorum_node_state);
		break;
	}

	return (res);
}

static int
qdevice_net_send_membership_node_list(struct qdevice_net_instance *instance,
    enum tlv_quorate quorate, const struct tlv_ring_id *ring_id,
    uint32_t node_list_entries, votequorum_node_t node_list[])
{
	struct node_list nlist;
	struct send_buffer_list_entry *send_buffer;
	uint64_t config_version;
	int send_config_version;
	uint32_t i;

	node_list_init(&nlist);

	for (i = 0; i < node_list_entries; i++) {
		if (node_list[i].nodeid == 0) {
			continue;
		}

		if (node_list_add(&nlist, node_list[i].nodeid, 0,
		    qdevice_net_convert_votequorum_to_tlv_node_state(node_list[i].state)) == NULL) {
			qdevice_net_log(LOG_ERR, "Can't allocate membership node list.");

			node_list_free(&nlist);

			return (-1);
		}
	}

	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for config "
		    "node list msg");

		node_list_free(&nlist);

		return (-1);
	}

	instance->last_msg_seq_num++;

	send_config_version = qdevice_net_get_cmap_config_version(instance->cmap_handle,
	    &config_version);

	if (msg_create_node_list(&send_buffer->buffer, instance->last_msg_seq_num,
	    TLV_NODE_LIST_TYPE_MEMBERSHIP,
	    1, ring_id, send_config_version, config_version, 1, quorate, &nlist) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for config list msg");

		node_list_free(&nlist);

		return (-1);
	}

	send_buffer_list_put(&instance->send_buffer_list, send_buffer);

	return (0);
}

static void
qdevice_net_convert_votequorum_to_tlv_ring_id(struct tlv_ring_id *tlv_rid,
    const votequorum_ring_id_t *votequorum_rid)
{

	tlv_rid->node_id = votequorum_rid->nodeid;
	tlv_rid->seq = votequorum_rid->seq;
}

static void
qdevice_net_votequorum_notification(votequorum_handle_t votequorum_handle,
    uint64_t context, uint32_t quorate,
    votequorum_ring_id_t votequorum_ring_id,
    uint32_t node_list_entries, votequorum_node_t node_list[])
{
	struct qdevice_net_instance *instance;
	struct tlv_ring_id ring_id;

	if (votequorum_context_get(votequorum_handle, (void **)&instance) != CS_OK) {
		errx(1, "Fatal error. Can't get votequorum context");
	}

	qdevice_net_convert_votequorum_to_tlv_ring_id(&ring_id, &votequorum_ring_id);

	if (qdevice_net_send_membership_node_list(instance,
	    (quorate ? TLV_QUORATE_QUORATE : TLV_QUORATE_INQUORATE),
	    &ring_id, node_list_entries, node_list) != 0) {
		/*
		 * Fatal error -> schedule disconnect
		 */
		instance->schedule_disconnect = 1;
	}

	memcpy(&global_last_received_ring_id, &ring_id, sizeof(ring_id));
}


static void
qdevice_net_init_votequorum(struct qdevice_net_instance *instance)
{
	votequorum_callbacks_t votequorum_callbacks;
	votequorum_handle_t votequorum_handle;
	cs_error_t res;
	int no_retries;
	int fd;

	memset(&votequorum_callbacks, 0, sizeof(votequorum_callbacks));
	votequorum_callbacks.votequorum_notify_fn = qdevice_net_votequorum_notification;

	no_retries = 0;

	while ((res = votequorum_initialize(&votequorum_handle,
	    &votequorum_callbacks)) == CS_ERR_TRY_AGAIN && no_retries++ < MAX_CS_TRY_AGAIN) {
		poll(NULL, 0, 1000);
	}

        if (res != CS_OK) {
		errx(1, "Failed to initialize the votequorum API. Error %s", cs_strerror(res));
	}

	if ((res = votequorum_qdevice_register(votequorum_handle,
	    QDEVICE_NET_VOTEQUORUM_DEVICE_NAME)) != CS_OK) {
		errx(1, "Can't register votequorum device. Error %s", cs_strerror(res));
	}

	if ((res = votequorum_context_set(votequorum_handle, (void *)instance)) != CS_OK) {
		errx(1, "Can't set votequorum context. Error %s", cs_strerror(res));
	}

	instance->votequorum_handle = votequorum_handle;

	votequorum_fd_get(votequorum_handle, &fd);
	if ((instance->votequorum_poll_fd = PR_CreateSocketPollFd(fd)) == NULL) {
		err_nss();
	}
}


int
main(void)
{
	struct qdevice_net_instance instance;
	cmap_handle_t cmap_handle;
	struct send_buffer_list_entry *send_buffer;

	/*
	 * Init
	 */
	qdevice_net_init_cmap(&cmap_handle);
	qdevice_net_instance_init_from_cmap(&instance, cmap_handle);

	qdevice_net_log_init(QDEVICE_NET_LOG_TARGET_STDERR);
        qdevice_net_log_set_debug(1);

	if (nss_sock_init_nss((instance.tls_supported != TLV_TLS_UNSUPPORTED ?
	    (char *)NSS_DB_DIR : NULL)) != 0) {
		err_nss();
	}

	/*
	 * Try to connect to qnetd host
	 */
	instance.socket = nss_sock_create_client_socket(instance.host_addr, instance.host_port,
	    PR_AF_UNSPEC, 100);
	if (instance.socket == NULL) {
		err_nss();
	}

	if (nss_sock_set_nonblocking(instance.socket) != 0) {
		err_nss();
	}

	qdevice_net_init_votequorum(&instance);

	/*
	 * Create and schedule send of preinit message to qnetd
	 */
	send_buffer = send_buffer_list_get_new(&instance.send_buffer_list);
	if (send_buffer == NULL) {
		errx(1, "Can't allocate send buffer list");
	}

	instance.last_msg_seq_num = 1;
	if (msg_create_preinit(&send_buffer->buffer, instance.cluster_name, 1,
	    instance.last_msg_seq_num) == 0) {
		errx(1, "Can't allocate buffer");
	}

	send_buffer_list_put(&instance.send_buffer_list, send_buffer);

	instance.state = QDEVICE_NET_STATE_WAITING_PREINIT_REPLY;

	/*
	 * Main loop
	 */
	while (qdevice_net_poll(&instance) == 0) {
	}

	/*
	 * Cleanup
	 */
	if (PR_Close(instance.socket) != PR_SUCCESS) {
		err_nss();
	}

	qdevice_net_instance_destroy(&instance);

	SSL_ClearSessionCache();

	if (NSS_Shutdown() != SECSuccess) {
		err_nss();
	}

	PR_Cleanup();

	qdevice_net_log_close();

	return (0);
}
