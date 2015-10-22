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

#include "qnet-config.h"
#include "dynar.h"
#include "nss-sock.h"
#include "tlv.h"
#include "msg.h"
#include "msgio.h"
#include "qdevice-net-log.h"
#include "timer-list.h"
#include "send-buffer-list.h"
#include "qdevice-net-instance.h"
#include "qdevice-net-send.h"
#include "qdevice-net-votequorum.h"
#include "qdevice-net-cast-vote-timer.h"

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
qdevice_net_msg_received_unexpected_msg(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg, const char *msg_str)
{

	qdevice_net_log(LOG_ERR, "Received unexpected %s message. Disconnecting from server",
	    msg_str);

	return (-1);
}

static int
qdevice_net_msg_received_preinit(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "preinit"));
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
qdevice_net_msg_received_preinit_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	int res;
	struct send_buffer_list_entry *send_buffer;

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_PREINIT_REPLY) {
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

		instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_STARTTLS_BEING_SENT;
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

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_INIT_REPLY) {
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

	instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_SET_OPTION_REPLY;

	return (0);
}

static int
qdevice_net_msg_received_starttls(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "starttls"));
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

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "set option"));
}

static int
qdevice_net_timer_send_heartbeat(void *data1, void *data2)
{
	struct qdevice_net_instance *instance;

	instance = (struct qdevice_net_instance *)data1;

	if (qdevice_net_send_echo_request(instance) == -1) {
		instance->schedule_disconnect = 1;
		return (0);
	}

	/*
	 * Schedule this function callback again
	 */
	return (-1);
}

static int
qdevice_net_register_votequorum_callbacks(struct qdevice_net_instance *instance)
{
	cs_error_t res;

	if ((res = votequorum_trackstart(instance->votequorum_handle, 0,
	    CS_TRACK_CHANGES)) != CS_OK) {
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

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_SET_OPTION_REPLY) {
		qdevice_net_log(LOG_ERR, "Received unexpected set option reply message. "
		    "Disconnecting from server");

		return (-1);
	}

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

	if (qdevice_net_cast_vote_timer_update(instance, TLV_VOTE_WAIT_FOR_REPLY) != 0) {
		errx(1, "qdevice_net_msg_received_set_option_reply fatal error. Can't update "
		    "cast vote timer vote");
	}

	instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS;

	return (0);
}

static int
qdevice_net_msg_received_echo_request(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "echo request"));
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

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "node list"));
}

static int
qdevice_net_msg_received_node_list_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_net_log(LOG_ERR, "Received unexpected node list reply message. "
		    "Disconnecting from server");

		return (-1);
	}

	if (!msg->vote_set || !msg->seq_number_set) {
		qdevice_net_log(LOG_ERR, "Received node list reply message without "
		    "required options. Disconnecting from server");
	}

	/*
	 * TODO API
	 */
	qdevice_net_log(LOG_INFO, "Received node list reply seq=%"PRIu32", vote=%u",
	    msg->seq_number, msg->vote);

	if (qdevice_net_cast_vote_timer_update(instance, msg->vote) != 0) {
		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_ask_for_vote(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "ask for vote"));
}

static int
qdevice_net_msg_received_ask_for_vote_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_net_log(LOG_ERR, "Received unexpected ask for vote reply message. "
		    "Disconnecting from server");

		return (-1);
	}

	if (!msg->vote_set || !msg->seq_number_set) {
		qdevice_net_log(LOG_ERR, "Received node list reply message without "
		    "required options. Disconnecting from server");
	}

	/*
	 * TODO API
	 */
	qdevice_net_log(LOG_INFO, "Received ask for vote reply seq=%"PRIu32", vote=%u",
	    msg->seq_number, msg->vote);

	if (qdevice_net_cast_vote_timer_update(instance, msg->vote) != 0) {
		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_vote_info(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{
	struct send_buffer_list_entry *send_buffer;

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_VOTEQUORUM_CMAP_EVENTS) {
		qdevice_net_log(LOG_ERR, "Received unexpected vote info message. "
		    "Disconnecting from server");

		return (-1);
	}

	if (!msg->vote_set || !msg->seq_number_set) {
		qdevice_net_log(LOG_ERR, "Received node list reply message without "
		    "required options. Disconnecting from server");
	}

	/*
	 * TODO API
	 */
	qdevice_net_log(LOG_INFO, "Received vote info seq=%"PRIu32", vote=%u",
	    msg->seq_number, msg->vote);

	if (qdevice_net_cast_vote_timer_update(instance, msg->vote) != 0) {
		return (-1);
	}

	/*
	 * Create reply message
	 */
	send_buffer = send_buffer_list_get_new(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_net_log(LOG_ERR, "Can't allocate send list buffer for "
		    "vote info reply msg");

		return (-1);
	}

	if (msg_create_vote_info_reply(&send_buffer->buffer, msg->seq_number) == 0) {
		qdevice_net_log(LOG_ERR, "Can't allocate send buffer for "
		    "vote info reply list msg");

		return (-1);
	}

	return (0);
}

static int
qdevice_net_msg_received_vote_info_reply(struct qdevice_net_instance *instance,
    const struct msg_decoded *msg)
{

	return (qdevice_net_msg_received_unexpected_msg(instance, msg, "vote info reply"));
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
	case MSG_TYPE_ASK_FOR_VOTE:
		ret_val = qdevice_net_msg_received_ask_for_vote(instance, &msg);
		break;
	case MSG_TYPE_ASK_FOR_VOTE_REPLY:
		ret_val = qdevice_net_msg_received_ask_for_vote_reply(instance, &msg);
		break;
	case MSG_TYPE_VOTE_INFO:
		ret_val = qdevice_net_msg_received_vote_info(instance, &msg);
		break;
	case MSG_TYPE_VOTE_INFO_REPLY:
		ret_val = qdevice_net_msg_received_vote_info_reply(instance, &msg);
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

	if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_STARTTLS_BEING_SENT) {
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
	uint32_t cast_vote_timer_interval;
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
	cast_vote_timer_interval = heartbeat_interval * 0.5;
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
		} else if (strcmp(str, "2nodelms") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_2NODELMS;
		} else if (strcmp(str, "lms") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_LMS;
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
	    QDEVICE_NET_MIN_MSG_SEND_SIZE, QDEVICE_NET_MAX_SEND_BUFFERS, QDEVICE_NET_MAX_MSG_RECEIVE_SIZE,
	    tls_supported, node_id, decision_algorithm,
	    heartbeat_interval, sync_heartbeat_interval, cast_vote_timer_interval,
	    host_addr, host_port, cluster_name) == -1) {
		errx(1, "Can't initialize qdevice-net");
	}

	instance->cmap_handle = cmap_handle;
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
	qdevice_net_cmap_init(&cmap_handle);
	qdevice_net_instance_init_from_cmap(&instance, cmap_handle);

	qdevice_net_log_init(QDEVICE_NET_LOG_TARGET_STDERR);
        qdevice_net_log_set_debug(1);

	if (nss_sock_init_nss((instance.tls_supported != TLV_TLS_UNSUPPORTED ?
	    (char *)QDEVICE_NET_NSS_DB_DIR : NULL)) != 0) {
		nss_sock_err(1);
	}

	/*
	 * Try to connect to qnetd host
	 */
	instance.socket = nss_sock_create_client_socket(instance.host_addr, instance.host_port,
	    PR_AF_UNSPEC, 100);
	if (instance.socket == NULL) {
		nss_sock_err(1);
	}

	if (nss_sock_set_nonblocking(instance.socket) != 0) {
		nss_sock_err(1);
	}

	qdevice_net_votequorum_init(&instance);

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

	instance.state = QDEVICE_NET_INSTANCE_STATE_WAITING_PREINIT_REPLY;

	/*
	 * Main loop
	 */
	while (qdevice_net_poll(&instance) == 0) {
	}

	/*
	 * Cleanup
	 */
	if (PR_Close(instance.socket) != PR_SUCCESS) {
		qdevice_net_log_nss(LOG_WARNING, "Unable to close connection");
	}

	/*
	 * Close cmap and votequorum connections
	 */
	if (votequorum_qdevice_unregister(instance.votequorum_handle,
	    QDEVICE_NET_VOTEQUORUM_DEVICE_NAME) != CS_OK) {
		qdevice_net_log_nss(LOG_WARNING, "Unable to unregister votequorum device");
	}

	votequorum_finalize(instance.votequorum_handle);
	cmap_finalize(instance.cmap_handle);

	qdevice_net_instance_destroy(&instance);

	SSL_ClearSessionCache();

	if (NSS_Shutdown() != SECSuccess) {
		nss_sock_err(1);
	}

	PR_Cleanup();

	qdevice_net_log_close();

	return (0);
}
