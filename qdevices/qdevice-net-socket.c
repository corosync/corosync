/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#include "msg.h"
#include "msgio.h"
#include "qnet-config.h"
#include "qdevice-log.h"
#include "qdevice-net-msg-received.h"
#include "qdevice-net-nss.h"
#include "qdevice-net-send.h"
#include "qdevice-net-socket.h"

/*
 * -1 means end of connection (EOF) or some other unhandled error. 0 = success
 */
int
qdevice_net_socket_read(struct qdevice_net_instance *instance)
{
	int res;
	int ret_val;
	int orig_skipping_msg;

	orig_skipping_msg = instance->skipping_msg;

	res = msgio_read(instance->socket, &instance->receive_buffer,
	    &instance->msg_already_received_bytes, &instance->skipping_msg);

	if (!orig_skipping_msg && instance->skipping_msg) {
		qdevice_log(LOG_DEBUG, "msgio_read set skipping_msg");
	}

	ret_val = 0;

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		qdevice_log(LOG_DEBUG, "Server closed connection");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_CLOSED_CONNECTION;
		ret_val = -1;
		break;
	case -2:
		qdevice_log(LOG_ERR, "Unhandled error when reading from server. "
		    "Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
		ret_val = -1;
		break;
	case -3:
		qdevice_log(LOG_ERR, "Can't store message header from server. "
		    "Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
		ret_val = -1;
		break;
	case -4:
		qdevice_log(LOG_ERR, "Can't store message from server. "
		    "Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
		ret_val = -1;
		break;
	case -5:
		qdevice_log(LOG_WARNING, "Server sent unsupported msg type %u. "
		    "Disconnecting from server", msg_get_type(&instance->receive_buffer));
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNSUPPORTED_MSG;
		ret_val = -1;
		break;
	case -6:
		qdevice_log(LOG_WARNING,
		    "Server wants to send too long message %u bytes. Disconnecting from server",
		    msg_get_len(&instance->receive_buffer));
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
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
			qdevice_log(LOG_CRIT, "net_socket_read in skipping msg state");
			exit(1);
		}

		instance->skipping_msg = 0;
		instance->msg_already_received_bytes = 0;
		dynar_clean(&instance->receive_buffer);
		break;
	default:
		qdevice_log(LOG_CRIT, "qdevice_net_socket_read unhandled error %d", res);
		exit(1);
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
		if ((new_pr_fd = nss_sock_start_ssl_as_client(instance->socket,
		    instance->advanced_settings->net_nss_qnetd_cn,
		    qdevice_net_nss_bad_cert_hook,
		    qdevice_net_nss_get_client_auth_data,
		    instance, 0, NULL)) == NULL) {
			qdevice_log_nss(LOG_ERR, "Can't start TLS");
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_START_TLS;
			return (-1);
		}

		/*
		 * And send init msg
		 */
		if (qdevice_net_send_init(instance) != 0) {
			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;

			return (-1);
		}

		instance->socket = new_pr_fd;
		instance->using_tls = 1;
	}

	return (0);
}

int
qdevice_net_socket_write(struct qdevice_net_instance *instance)
{
	int res;
	struct send_buffer_list_entry *send_buffer;
	enum msg_type sent_msg_type;

	send_buffer = send_buffer_list_get_active(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		qdevice_log(LOG_CRIT, "send_buffer_list_get_active returned NULL");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SEND_MESSAGE;

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
		qdevice_log_nss(LOG_CRIT, "PR_Send returned 0");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_CLOSED_CONNECTION;
		return (-1);
	}

	if (res == -2) {
		qdevice_log_nss(LOG_ERR, "Unhandled error when sending message to server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SEND_MESSAGE;

		return (-1);
	}

	return (0);
}
