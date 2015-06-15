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

#include "dynar.h"
#include "nss-sock.h"
#include "tlv.h"
#include "msg.h"
#include "msgio.h"
#include "qnetd-log.h"
#include "timer-list.h"

#define NSS_DB_DIR	"node/nssdb"

#define QNETD_HOST	"localhost"
#define QNETD_PORT	4433

#define QNETD_NSS_SERVER_CN		"Qnetd Server"
#define QDEVICE_NET_NSS_CLIENT_CERT_NICKNAME	"Cluster Cert"

#define QDEVICE_NET_CLUSTER_NAME		"Testcluster"

#define QDEVICE_NET_INITIAL_MSG_RECEIVE_SIZE	(1 << 15)
#define QDEVICE_NET_INITIAL_MSG_SEND_SIZE	(1 << 15)

#define QDEVICE_NET_MIN_MSG_SEND_SIZE		QDEVICE_NET_INITIAL_MSG_SEND_SIZE

#define QDEVICE_NET_MAX_MSG_RECEIVE_SIZE	(1 << 24)

#define QDEVICE_NET_TLS_SUPPORTED	TLV_TLS_SUPPORTED

#define QDEVICE_NET_NODE_ID		42

#define QDEVICE_NET_DECISION_ALGORITHM		TLV_DECISION_ALGORITHM_TYPE_TEST

#define QDEVICE_NET_HEARTBEAT_INTERVAL		10000

#define qdevice_net_log			qnetd_log
#define qdevice_net_log_nss		qnetd_log_nss
#define qdevice_net_log_init		qnetd_log_init
#define qdevice_net_log_close		qnetd_log_close
#define qdevice_net_log_set_debug	qnetd_log_set_debug

#define QDEVICE_NET_LOG_TARGET_STDERR		QNETD_LOG_TARGET_STDERR
#define QDEVICE_NET_LOG_TARGET_SYSLOG		QNETD_LOG_TARGET_SYSLOG

enum qdevice_net_state {
	QDEVICE_NET_STATE_WAITING_PREINIT_REPLY,
	QDEVICE_NET_STATE_WAITING_STARTTLS_BEING_SENT,
	QDEVICE_NET_STATE_WAITING_INIT_REPLY,
	QDEVICE_NET_STATE_WAITING_SET_OPTION_REPLY,
};

struct qdevice_net_instance {
	PRFileDesc *socket;
	size_t initial_send_size;
	size_t initial_receive_size;
	size_t max_receive_size;
	size_t min_send_size;
	struct dynar receive_buffer;
	struct dynar send_buffer;
	struct dynar echo_request_send_buffer;
	int sending_msg;
	int skipping_msg;
	int sending_echo_request_msg;
	size_t msg_already_received_bytes;
	size_t msg_already_sent_bytes;
	size_t echo_request_msg_already_sent_bytes;
	enum qdevice_net_state state;
	uint32_t expected_msg_seq_num;
	uint32_t echo_request_expected_msg_seq_num;
	uint32_t echo_reply_received_msg_seq_num;
	enum tlv_tls_supported tls_supported;
	int using_tls;
	uint32_t node_id;
	uint32_t heartbeat_interval;
	enum tlv_decision_algorithm_type decision_algorithm;
	struct timer_list main_timer_list;
	struct timer_list_entry *echo_request_timer;
	int schedule_disconnect;
};

static void
err_nss(void) {
	errx(1, "nss error %d: %s", PR_GetError(), PR_ErrorToString(PR_GetError(), PR_LANGUAGE_I_DEFAULT));
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
qdevice_net_schedule_send(struct qdevice_net_instance *instance)
{
	if (instance->sending_msg) {
		/*
		 * Msg is already scheduled for send
		 */
		return (-1);
	}

	instance->msg_already_sent_bytes = 0;
	instance->sending_msg = 1;

	return (0);
}

static int
qdevice_net_schedule_echo_request_send(struct qdevice_net_instance *instance)
{
	if (instance->sending_echo_request_msg) {
		qdevice_net_log(LOG_ERR, "Can't schedule send of echo request msg, because "
		    "previous message wasn't yet sent. Disconnecting from server.");
		return (-1);
	}

	if (instance->echo_reply_received_msg_seq_num != instance->echo_request_expected_msg_seq_num) {
		qdevice_net_log(LOG_ERR, "Server didn't send echo reply message on time. "
		    "Disconnecting from server.");
		return (-1);
	}

	instance->echo_request_expected_msg_seq_num++;

	if (msg_create_echo_request(&instance->echo_request_send_buffer, 1, instance->echo_request_expected_msg_seq_num) == -1) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for echo request msg");

		return (-1);
	}

	instance->echo_request_msg_already_sent_bytes = 0;
	instance->sending_echo_request_msg = 1;

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
qdevice_net_check_tls_compatibility(enum tlv_tls_supported server_tls, enum tlv_tls_supported client_tls)
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
qdevice_net_msg_received_preinit(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected preinit message. Disconnecting from server");

	return (-1);
}

static int
qdevice_net_msg_check_seq_number(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	if (!msg->seq_number_set || msg->seq_number != instance->expected_msg_seq_num) {
		qdevice_net_log(LOG_ERR, "Received message doesn't contain seq_number or it's not expected one.");

		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_check_echo_reply_seq_number(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	if (!msg->seq_number_set) {
		qdevice_net_log(LOG_ERR, "Received echo reply message doesn't contain seq_number.");

		return (-1);
	}

	if (msg->seq_number != instance->echo_request_expected_msg_seq_num) {
		qdevice_net_log(LOG_ERR, "Server doesn't replied in expected time. Closing connection");

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

	tlv_get_supported_options(&supported_opts, &no_supported_opts);
	msg_get_supported_messages(&supported_msgs, &no_supported_msgs);
	instance->expected_msg_seq_num++;

	if (msg_create_init(&instance->send_buffer, 1, instance->expected_msg_seq_num,
	    supported_msgs, no_supported_msgs, supported_opts, no_supported_opts,
	    instance->node_id) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for init msg");

		return (-1);
	}

	if (qdevice_net_schedule_send(instance) != 0) {
		qdevice_net_log(LOG_ERR, "Can't schedule send of init msg");

		return (-1);
	}

	instance->state = QDEVICE_NET_STATE_WAITING_INIT_REPLY;

	return (0);
}


static int
qdevice_net_msg_received_preinit_reply(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{
	int res;

	if (instance->state != QDEVICE_NET_STATE_WAITING_PREINIT_REPLY) {
		qdevice_net_log(LOG_ERR, "Received unexpected preinit reply message. Disconnecting from server");

		return (-1);
	}

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		return (-1);
	}

	/*
	 * Check TLS support
	 */
	if (!msg->tls_supported_set || !msg->tls_client_cert_required_set) {
		qdevice_net_log(LOG_ERR, "Required tls_supported or tls_client_cert_required option is unset");

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
		instance->expected_msg_seq_num++;
		if (msg_create_starttls(&instance->send_buffer, 1, instance->expected_msg_seq_num) == 0) {
			qdevice_net_log(LOG_ERR, "Can't allocate send buffer for starttls msg");

			return (-1);
		}

		if (qdevice_net_schedule_send(instance) != 0) {
			qdevice_net_log(LOG_ERR, "Can't schedule send of starttls msg");

			return (-1);
		}

		instance->state = QDEVICE_NET_STATE_WAITING_STARTTLS_BEING_SENT;
	} else if (res == 0) {
		if (qdevice_net_send_init(instance) != 0) {
			return (-1);
		}
	}

	return (0);
}

static int
qdevice_net_msg_received_init_reply(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{
	size_t zi;
	int res;

	if (instance->state != QDEVICE_NET_STATE_WAITING_INIT_REPLY) {
		qdevice_net_log(LOG_ERR, "Received unexpected init reply message. Disconnecting from server");

		return (-1);
	}

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		return (-1);
	}

	if (!msg->server_maximum_request_size_set || !msg->server_maximum_reply_size_set) {
		qdevice_net_log(LOG_ERR, "Required maximum_request_size or maximum_reply_size option is unset");

		return (-1);
	}

	if (msg->supported_messages == NULL || msg->supported_options == NULL) {
		qdevice_net_log(LOG_ERR, "Required supported messages or supported options option is unset");

		return (-1);
	}

	if (msg->supported_decision_algorithms == NULL) {
		qdevice_net_log(LOG_ERR, "Required supported decision algorithms option is unset");

		return (-1);
	}

	if (msg->server_maximum_request_size < instance->min_send_size) {
		qdevice_net_log(LOG_ERR,
		    "Server accepts maximum %zu bytes message but this client minimum is %zu bytes.",
		    msg->server_maximum_request_size, instance->min_send_size);

		return (-1);
	}

	if (msg->server_maximum_reply_size > instance->max_receive_size) {
		qdevice_net_log(LOG_ERR,
		    "Server may send message up to %zu bytes message but this client maximum is %zu bytes.",
		    msg->server_maximum_reply_size, instance->max_receive_size);

		return (-1);
	}

	/*
	 * Change buffer sizes
	 */
	dynar_set_max_size(&instance->receive_buffer, msg->server_maximum_reply_size);
	dynar_set_max_size(&instance->send_buffer, msg->server_maximum_request_size);
	dynar_set_max_size(&instance->echo_request_send_buffer, msg->server_maximum_request_size);


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
	instance->expected_msg_seq_num++;

	if (msg_create_set_option(&instance->send_buffer, 1, instance->expected_msg_seq_num,
	    1, instance->decision_algorithm, 1, instance->heartbeat_interval) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for set option msg");

		return (-1);
	}

	if (qdevice_net_schedule_send(instance) != 0) {
		qdevice_net_log(LOG_ERR, "Can't schedule send of set option msg");

		return (-1);
	}

	instance->state = QDEVICE_NET_STATE_WAITING_SET_OPTION_REPLY;

	return (0);
}

static int
qdevice_net_msg_received_stattls(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected starttls message. Disconnecting from server");

	return (-1);
}

static int
qdevice_net_msg_received_server_error(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	if (!msg->reply_error_code_set) {
		qdevice_net_log(LOG_ERR, "Received server error without error code set. Disconnecting from server");
	} else {
		qdevice_net_log(LOG_ERR, "Received server error %"PRIu16". Disconnecting from server",
		    msg->reply_error_code);
	}

	return (-1);
}

static int
qdevice_net_msg_received_set_option(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected set option message. Disconnecting from server");

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

static int
qdevice_net_msg_received_set_option_reply(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	if (qdevice_net_msg_check_seq_number(instance, msg) != 0) {
		return (-1);
	}

	if (!msg->decision_algorithm_set || !msg->heartbeat_interval_set) {
		qdevice_net_log(LOG_ERR, "Received set option reply message without required options. "
		    "Disconnecting from server");
	}

	if (msg->decision_algorithm != instance->decision_algorithm ||
	    msg->heartbeat_interval != instance->heartbeat_interval) {
		qdevice_net_log(LOG_ERR, "Server doesn't accept sent decision algorithm or heartbeat interval.");

		return (-1);
	}

	/*
	 * Server accepted heartbeat interval -> schedule regular sending of echo request
	 */
	if (instance->heartbeat_interval > 0) {
		instance->echo_request_timer = timer_list_add(&instance->main_timer_list, instance->heartbeat_interval,
		    qdevice_net_timer_send_heartbeat, (void *)instance, NULL);

		if (instance->echo_request_timer == NULL) {
			qdevice_net_log(LOG_ERR, "Can't schedule regular sending of heartbeat.");

			return (-1);
		}
	}

	return (0);
}

static int
qdevice_net_msg_received_echo_request(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	qdevice_net_log(LOG_ERR, "Received unexpected echo request message. Disconnecting from server");

	return (-1);
}

static int
qdevice_net_msg_received_echo_reply(struct qdevice_net_instance *instance, const struct msg_decoded *msg)
{

	if (qdevice_net_msg_check_echo_reply_seq_number(instance, msg) != 0) {
		return (-1);
	}

	instance->echo_reply_received_msg_seq_num = msg->seq_number;

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
		ret_val = qdevice_net_msg_received_stattls(instance, &msg);
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
	default:
		qdevice_net_log(LOG_ERR, "Received unsupported message %u. Disconnecting from server", msg.type);
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

	res = msgio_read(instance->socket, &instance->receive_buffer, &instance->msg_already_received_bytes,
	    &instance->skipping_msg);

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
		qdevice_net_log_nss(LOG_ERR, "Unhandled error when reading from server. Disconnecting from server");
		ret_val = -1;
		break;
	case -3:
		qdevice_net_log(LOG_ERR, "Can't store message header from server. Disconnecting from server");
		ret_val = -1;
		break;
	case -4:
		qdevice_net_log(LOG_ERR, "Can't store message from server. Disconnecting from server");
		ret_val = -1;
		break;
	case -5:
		qdevice_net_log(LOG_WARNING, "Server sent unsupported msg type %u. Disconnecting from server",
			    msg_get_type(&instance->receive_buffer));
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
		    qdevice_net_nss_get_client_auth_data, (void *)QDEVICE_NET_NSS_CLIENT_CERT_NICKNAME,
		    0, NULL)) == NULL) {
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
	int send_echo_request;

	/*
	 * Echo request has extra buffer and special processing. Messages other then echo request
	 * has higher priority, but if echo request send was not completed
	 * it's necesary to complete it.
	 */
	send_echo_request = !(instance->sending_msg && instance->echo_request_msg_already_sent_bytes == 0);

	if (!send_echo_request) {
		res = msgio_write(instance->socket, &instance->send_buffer, &instance->msg_already_sent_bytes);
	} else {
		res = msgio_write(instance->socket, &instance->echo_request_send_buffer,
		    &instance->echo_request_msg_already_sent_bytes);
	}

	if (res == 1) {
		if (!send_echo_request) {
			instance->sending_msg = 0;

			if (qdevice_net_socket_write_finished(instance) == -1) {
				return (-1);
			}
		} else {
			instance->sending_echo_request_msg = 0;
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


#define QDEVICE_NET_POLL_NO_FDS		1
#define QDEVICE_NET_POLL_SOCKET		0

static int
qdevice_net_poll(struct qdevice_net_instance *instance)
{
	PRPollDesc pfds[QDEVICE_NET_POLL_NO_FDS];
	PRInt32 poll_res;
	int i;

	pfds[QDEVICE_NET_POLL_SOCKET].fd = instance->socket;
	pfds[QDEVICE_NET_POLL_SOCKET].in_flags = PR_POLL_READ;
	if (instance->sending_msg || instance->sending_echo_request_msg) {
		pfds[QDEVICE_NET_POLL_SOCKET].in_flags |= PR_POLL_WRITE;
	}

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
			    pfds[i].out_flags & (PR_POLL_ERR|PR_POLL_NVAL|PR_POLL_HUP|PR_POLL_EXCEPT)) {
				switch (i) {
				case QDEVICE_NET_POLL_SOCKET:
					qdevice_net_log(LOG_CRIT, "POLL_ERR (%u) on main socket", pfds[i].out_flags);

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
		 * Schedule disconnect can be set by this function or by some timer_list callback
		 */
		return (-1);
	}

	return (0);
}

static int
qdevice_net_instance_init(struct qdevice_net_instance *instance, size_t initial_receive_size,
    size_t initial_send_size, size_t min_send_size, size_t max_receive_size, enum tlv_tls_supported tls_supported,
    uint32_t node_id, enum tlv_decision_algorithm_type decision_algorithm, uint32_t heartbeat_interval)
{

	memset(instance, 0, sizeof(*instance));

	instance->initial_receive_size = initial_receive_size;
	instance->initial_send_size = initial_send_size;
	instance->min_send_size = min_send_size;
	instance->max_receive_size = max_receive_size;
	instance->node_id = node_id;
	instance->decision_algorithm = decision_algorithm;
	instance->heartbeat_interval = heartbeat_interval;
	dynar_init(&instance->receive_buffer, initial_receive_size);
	dynar_init(&instance->send_buffer, initial_send_size);
	dynar_init(&instance->echo_request_send_buffer, initial_send_size);
	timer_list_init(&instance->main_timer_list);

	instance->tls_supported = tls_supported;

	return (0);
}

static int
qdevice_net_instance_destroy(struct qdevice_net_instance *instance)
{

	timer_list_free(&instance->main_timer_list);
	dynar_destroy(&instance->receive_buffer);
	dynar_destroy(&instance->send_buffer);
	dynar_destroy(&instance->echo_request_send_buffer);

	return (0);
}

int
main(void)
{
	struct qdevice_net_instance instance;

	/*
	 * Init
	 */
	qdevice_net_log_init(QDEVICE_NET_LOG_TARGET_STDERR);
        qdevice_net_log_set_debug(1);

	if (nss_sock_init_nss((char *)NSS_DB_DIR) != 0) {
		err_nss();
	}

	if (qdevice_net_instance_init(&instance,
	    QDEVICE_NET_INITIAL_MSG_RECEIVE_SIZE, QDEVICE_NET_INITIAL_MSG_SEND_SIZE,
	    QDEVICE_NET_MIN_MSG_SEND_SIZE, QDEVICE_NET_MAX_MSG_RECEIVE_SIZE,
	    QDEVICE_NET_TLS_SUPPORTED, QDEVICE_NET_NODE_ID, QDEVICE_NET_DECISION_ALGORITHM,
	    QDEVICE_NET_HEARTBEAT_INTERVAL) == -1) {
		errx(1, "Can't initialize qdevice-net");
	}

	/*
	 * Try to connect to qnetd host
	 */
	instance.socket = nss_sock_create_client_socket(QNETD_HOST, QNETD_PORT, PR_AF_UNSPEC, 100);
	if (instance.socket == NULL) {
		err_nss();
	}

	if (nss_sock_set_nonblocking(instance.socket) != 0) {
		err_nss();
	}

	/*
	 * Create and schedule send of preinit message to qnetd
	 */
	instance.expected_msg_seq_num = 1;
	if (msg_create_preinit(&instance.send_buffer, QDEVICE_NET_CLUSTER_NAME, 1, instance.expected_msg_seq_num) == 0) {
		errx(1, "Can't allocate buffer");
	}
	if (qdevice_net_schedule_send(&instance) != 0) {
		errx(1, "Can't schedule send of preinit msg");
	}

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
