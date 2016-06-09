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

#include <sys/types.h>

#include "msgio.h"
#include "msg.h"
#include "nss-sock.h"
#include "qnetd-log.h"
#include "qnetd-client-net.h"
#include "qnetd-client-send.h"
#include "qnetd-client-msg-received.h"

#define CLIENT_ADDR_STR_LEN_COLON_PORT	(1 + 5 + 1)
#define CLIENT_ADDR_STR_LEN		(INET6_ADDRSTRLEN + CLIENT_ADDR_STR_LEN_COLON_PORT)

static int
qnetd_client_net_write_finished(struct qnetd_instance *instance, struct qnetd_client *client)
{

	/*
	 * Callback is currently unused
	 */

	return (0);
}

int
qnetd_client_net_write(struct qnetd_instance *instance, struct qnetd_client *client)
{
	int res;
	struct send_buffer_list_entry *send_buffer;

	send_buffer = send_buffer_list_get_active(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log_nss(LOG_CRIT, "send_buffer_list_get_active returned NULL");

		return (-1);
	}

	res = msgio_write(client->socket, &send_buffer->buffer,
	    &send_buffer->msg_already_sent_bytes);

	if (res == 1) {
		send_buffer_list_delete(&client->send_buffer_list, send_buffer);

		if (qnetd_client_net_write_finished(instance, client) == -1) {
			return (-1);
		}
	}

	if (res == -1) {
		qnetd_log_nss(LOG_CRIT, "PR_Send returned 0");

		return (-1);
	}

	if (res == -2) {
		qnetd_log_nss(LOG_ERR, "Unhandled error when sending message to client");

		return (-1);
	}

	return (0);
}


/*
 * -1 means end of connection (EOF) or some other unhandled error. 0 = success
 */
int
qnetd_client_net_read(struct qnetd_instance *instance, struct qnetd_client *client)
{
	int res;
	int ret_val;
	int orig_skipping_msg;

	orig_skipping_msg = client->skipping_msg;

	res = msgio_read(client->socket, &client->receive_buffer,
	    &client->msg_already_received_bytes, &client->skipping_msg);

	if (!orig_skipping_msg && client->skipping_msg) {
		qnetd_log(LOG_DEBUG, "msgio_read set skipping_msg");
	}

	ret_val = 0;

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		qnetd_log(LOG_DEBUG, "Client closed connection");
		ret_val = -1;
		break;
	case -2:
		qnetd_log_nss(LOG_ERR, "Unhandled error when reading from client. "
		    "Disconnecting client");
		ret_val = -1;
		break;
	case -3:
		qnetd_log(LOG_ERR, "Can't store message header from client. Disconnecting client");
		ret_val = -1;
		break;
	case -4:
		qnetd_log(LOG_ERR, "Can't store message from client. Skipping message");
		client->skipping_msg_reason = TLV_REPLY_ERROR_CODE_ERROR_DECODING_MSG;
		break;
	case -5:
		qnetd_log(LOG_WARNING, "Client sent unsupported msg type %u. Skipping message",
			    msg_get_type(&client->receive_buffer));
		client->skipping_msg_reason = TLV_REPLY_ERROR_CODE_UNSUPPORTED_MESSAGE;
		break;
	case -6:
		qnetd_log(LOG_WARNING,
		    "Client wants to send too long message %u bytes. Skipping message",
		    msg_get_len(&client->receive_buffer));
		client->skipping_msg_reason = TLV_REPLY_ERROR_CODE_MESSAGE_TOO_LONG;
		break;
	case 1:
		/*
		 * Full message received / skipped
		 */
		if (!client->skipping_msg) {
			if (qnetd_client_msg_received(instance, client) == -1) {
				ret_val = -1;
			}
		} else {
			if (qnetd_client_send_err(client, 0, 0, client->skipping_msg_reason) != 0) {
				ret_val = -1;
			}
		}

		client->skipping_msg = 0;
		client->skipping_msg_reason = TLV_REPLY_ERROR_CODE_NO_ERROR;
		client->msg_already_received_bytes = 0;
		dynar_clean(&client->receive_buffer);
		break;
	default:
		qnetd_log(LOG_ERR, "Unhandled msgio_read error %d\n", res);
		exit(1);
		break;
	}

	return (ret_val);
}

int
qnetd_client_net_accept(struct qnetd_instance *instance)
{
	PRNetAddr client_addr;
	PRFileDesc *client_socket;
	struct qnetd_client *client;
	char *client_addr_str;
	int res_err;

	client_addr_str = NULL;

	res_err = -1;

	if ((client_socket = PR_Accept(instance->server.socket, &client_addr,
	    PR_INTERVAL_NO_TIMEOUT)) == NULL) {
		qnetd_log_nss(LOG_ERR, "Can't accept connection");
		return (-1);
	}

	if (nss_sock_set_non_blocking(client_socket) != 0) {
		qnetd_log_nss(LOG_ERR, "Can't set client socket to non blocking mode");
		goto exit_close;
	}

	if (instance->max_clients != 0 &&
	    qnetd_client_list_no_clients(&instance->clients) >= instance->max_clients) {
		qnetd_log(LOG_ERR, "Maximum clients reached. Not accepting connection");
		goto exit_close;
	}

	client_addr_str = malloc(CLIENT_ADDR_STR_LEN);
	if (client_addr_str == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc client addr str memory. Not accepting connection");
		goto exit_close;
	}

	if (PR_NetAddrToString(&client_addr, client_addr_str, CLIENT_ADDR_STR_LEN) != PR_SUCCESS) {
		qnetd_log_nss(LOG_ERR, "Can't convert client address to string. Not accepting connection");
		goto exit_close;
	}

	if (snprintf(client_addr_str + strlen(client_addr_str),
	    CLIENT_ADDR_STR_LEN_COLON_PORT, ":%"PRIu16,
	    ntohs(client_addr.ipv6.port)) >= CLIENT_ADDR_STR_LEN_COLON_PORT) {
		qnetd_log(LOG_ERR, "Can't store port to client addr str. Not accepting connection");
		goto exit_close;
	}

	client = qnetd_client_list_add(&instance->clients, client_socket, &client_addr,
	    client_addr_str,
	    instance->advanced_settings->max_client_receive_size,
	    instance->advanced_settings->max_client_send_buffers,
	    instance->advanced_settings->max_client_send_size, &instance->main_timer_list);
	if (client == NULL) {
		qnetd_log(LOG_ERR, "Can't add client to list");
		res_err = -2;
		goto exit_close;
	}

	return (0);

exit_close:
	free(client_addr_str);
	PR_Close(client_socket);

	return (res_err);
}
