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

#include <string.h>

#include "qnetd-client-send.h"
#include "qnetd-log.h"
#include "qnetd-log-debug.h"
#include "msg.h"

int
qnetd_client_send_err(struct qnetd_client *client, int add_msg_seq_number, uint32_t msg_seq_number,
    enum tlv_reply_error_code reply)
{
	struct send_buffer_list_entry *send_buffer;

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc server error msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_server_error(&send_buffer->buffer, add_msg_seq_number,
	    msg_seq_number, reply) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc server error msg. "
		    "Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);
		return (-1);
	};

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}

int
qnetd_client_send_vote_info(struct qnetd_client *client, uint32_t msg_seq_number,
    const struct tlv_ring_id *ring_id, enum tlv_vote vote)
{
	struct send_buffer_list_entry *send_buffer;

	/*
	 * Store result vote
	 */
	client->last_sent_vote = vote;
	if (vote == TLV_VOTE_ACK || vote == TLV_VOTE_NACK) {
		client->last_sent_ack_nack_vote = vote;
	}

	qnetd_log_debug_send_vote_info(client, msg_seq_number, vote);

	send_buffer = send_buffer_list_get_new(&client->send_buffer_list);
	if (send_buffer == NULL) {
		qnetd_log(LOG_ERR, "Can't alloc vote info msg from list. "
		    "Disconnecting client connection.");

		return (-1);
	}

	if (msg_create_vote_info(&send_buffer->buffer, msg_seq_number, ring_id, vote) == 0) {
		qnetd_log(LOG_ERR, "Can't alloc vote info msg. "
		    "Disconnecting client connection.");

		send_buffer_list_discard_new(&client->send_buffer_list, send_buffer);
		return (-1);
	};

	send_buffer_list_put(&client->send_buffer_list, send_buffer);

	return (0);
}
