/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
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
#include <arpa/inet.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "msg.h"

#define MSG_TYPE_LENGTH		2
#define MSG_LENGTH_LENGTH	4

#define MSG_STATIC_SUPPORTED_MESSAGES_SIZE	18

enum msg_type msg_static_supported_messages[MSG_STATIC_SUPPORTED_MESSAGES_SIZE] = {
    MSG_TYPE_PREINIT,
    MSG_TYPE_PREINIT_REPLY,
    MSG_TYPE_STARTTLS,
    MSG_TYPE_INIT,
    MSG_TYPE_INIT_REPLY,
    MSG_TYPE_SERVER_ERROR,
    MSG_TYPE_SET_OPTION,
    MSG_TYPE_SET_OPTION_REPLY,
    MSG_TYPE_ECHO_REQUEST,
    MSG_TYPE_ECHO_REPLY,
    MSG_TYPE_NODE_LIST,
    MSG_TYPE_NODE_LIST_REPLY,
    MSG_TYPE_ASK_FOR_VOTE,
    MSG_TYPE_ASK_FOR_VOTE_REPLY,
    MSG_TYPE_VOTE_INFO,
    MSG_TYPE_VOTE_INFO_REPLY,
    MSG_TYPE_HEURISTICS_CHANGE,
    MSG_TYPE_HEURISTICS_CHANGE_REPLY,
};

size_t
msg_get_header_length(void)
{
	return (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH);
}

static void
msg_add_type(struct dynar *msg, enum msg_type type)
{
	uint16_t ntype;

	ntype = htons((uint16_t)type);
	dynar_cat(msg, &ntype, sizeof(ntype));
}

enum msg_type
msg_get_type(const struct dynar *msg)
{
	uint16_t ntype;
	uint16_t type;

	memcpy(&ntype, dynar_data(msg), sizeof(ntype));
	type = ntohs(ntype);

	return (type);
}

/*
 * We don't know size of message before call of this function, so zero is
 * added. Real value is set afterwards by msg_set_len.
 */
static void
msg_add_len(struct dynar *msg)
{
	uint32_t len;

	len = 0;
	dynar_cat(msg, &len, sizeof(len));
}

static void
msg_set_len(struct dynar *msg, uint32_t len)
{
	uint32_t nlen;

	nlen = htonl(len);
	memcpy(dynar_data(msg) + MSG_TYPE_LENGTH, &nlen, sizeof(nlen));
}

/*
 * Used only for echo reply msg. All other messages should use msg_add_type.
 */
static void
msg_set_type(struct dynar *msg, enum msg_type type)
{
	uint16_t ntype;

	ntype = htons((uint16_t)type);
	memcpy(dynar_data(msg), &ntype, sizeof(ntype));
}

uint32_t
msg_get_len(const struct dynar *msg)
{
	uint32_t nlen;
	uint32_t len;

	memcpy(&nlen, dynar_data(msg) + MSG_TYPE_LENGTH, sizeof(nlen));
	len = ntohl(nlen);

	return (len);
}


size_t
msg_create_preinit(struct dynar *msg, const char *cluster_name, int add_msg_seq_number,
    uint32_t msg_seq_number)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_PREINIT);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	if (tlv_add_cluster_name(msg, cluster_name) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_preinit_reply(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number,
    enum tlv_tls_supported tls_supported, int tls_client_cert_required)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_PREINIT_REPLY);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	if (tlv_add_tls_supported(msg, tls_supported) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_tls_client_cert_required(msg, tls_client_cert_required) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_starttls(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_STARTTLS);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_server_error(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number,
    enum tlv_reply_error_code reply_error_code)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_SERVER_ERROR);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	if (tlv_add_reply_error_code(msg, reply_error_code) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

static uint16_t *
msg_convert_msg_type_array_to_u16_array(const enum msg_type *msg_type_array, size_t array_size)
{
	uint16_t *u16a;
	size_t i;

	u16a = malloc(sizeof(*u16a) * array_size);
	if (u16a == NULL) {
		return (NULL);
	}

	for (i = 0; i < array_size; i++) {
		u16a[i] = (uint16_t)msg_type_array[i];
	}

	return (u16a);
}

size_t
msg_create_init(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number,
    enum tlv_decision_algorithm_type decision_algorithm,
    const enum msg_type *supported_msgs, size_t no_supported_msgs,
    const enum tlv_opt_type *supported_opts, size_t no_supported_opts, uint32_t node_id,
    uint32_t heartbeat_interval, const struct tlv_tie_breaker *tie_breaker,
    const struct tlv_ring_id *ring_id)
{
	uint16_t *u16a;
	int res;

	u16a = NULL;

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_INIT);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	if (supported_msgs != NULL && no_supported_msgs > 0) {
		u16a = msg_convert_msg_type_array_to_u16_array(supported_msgs, no_supported_msgs);

		if (u16a == NULL) {
			goto small_buf_err;
		}

		res = tlv_add_u16_array(msg, TLV_OPT_SUPPORTED_MESSAGES, u16a, no_supported_msgs);

		free(u16a);

		if (res == -1) {
			goto small_buf_err;
		}
	}

	if (supported_opts != NULL && no_supported_opts > 0) {
		if (tlv_add_supported_options(msg, supported_opts, no_supported_opts) == -1) {
			goto small_buf_err;
		}
	}

	if (tlv_add_node_id(msg, node_id) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_decision_algorithm(msg, decision_algorithm) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_heartbeat_interval(msg, heartbeat_interval) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_tie_breaker(msg, tie_breaker) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_ring_id(msg, ring_id) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_init_reply(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number,
    enum tlv_reply_error_code reply_error_code,
    const enum msg_type *supported_msgs, size_t no_supported_msgs,
    const enum tlv_opt_type *supported_opts, size_t no_supported_opts,
    size_t server_maximum_request_size, size_t server_maximum_reply_size,
    const enum tlv_decision_algorithm_type *supported_decision_algorithms,
    size_t no_supported_decision_algorithms)
{
	uint16_t *u16a;
	int res;

	u16a = NULL;

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_INIT_REPLY);
	msg_add_len(msg);

	if (tlv_add_reply_error_code(msg, reply_error_code) == -1) {
		goto small_buf_err;
	}

	if (supported_msgs != NULL && no_supported_msgs > 0) {
		u16a = msg_convert_msg_type_array_to_u16_array(supported_msgs, no_supported_msgs);

		if (u16a == NULL) {
			goto small_buf_err;
		}

		res = tlv_add_u16_array(msg, TLV_OPT_SUPPORTED_MESSAGES, u16a, no_supported_msgs);

		free(u16a);

		if (res == -1) {
			goto small_buf_err;
		}
	}

	if (supported_opts != NULL && no_supported_opts > 0) {
		if (tlv_add_supported_options(msg, supported_opts, no_supported_opts) == -1) {
			goto small_buf_err;
		}
	}

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	if (tlv_add_server_maximum_request_size(msg, server_maximum_request_size) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_server_maximum_reply_size(msg, server_maximum_reply_size) == -1) {
		goto small_buf_err;
	}

	if (supported_decision_algorithms != NULL && no_supported_decision_algorithms > 0) {
		if (tlv_add_supported_decision_algorithms(msg, supported_decision_algorithms,
		    no_supported_decision_algorithms) == -1) {
			goto small_buf_err;
		}
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_set_option(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number,
    int add_heartbeat_interval, uint32_t heartbeat_interval)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_SET_OPTION);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	if (add_heartbeat_interval) {
		if (tlv_add_heartbeat_interval(msg, heartbeat_interval) == -1) {
			goto small_buf_err;
		}
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_set_option_reply(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number,
    uint32_t heartbeat_interval)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_SET_OPTION_REPLY);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	if (tlv_add_heartbeat_interval(msg, heartbeat_interval) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_echo_request(struct dynar *msg, int add_msg_seq_number, uint32_t msg_seq_number)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_ECHO_REQUEST);
	msg_add_len(msg);

	if (add_msg_seq_number) {
		if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
			goto small_buf_err;
		}
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_echo_reply(struct dynar *msg, const struct dynar *echo_request_msg)
{

	dynar_clean(msg);

	if (dynar_cat(msg, dynar_data(echo_request_msg), dynar_size(echo_request_msg)) == -1) {
		goto small_buf_err;
	}

	msg_set_type(msg, MSG_TYPE_ECHO_REPLY);

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_node_list(struct dynar *msg,
    uint32_t msg_seq_number, enum tlv_node_list_type node_list_type,
    int add_ring_id, const struct tlv_ring_id *ring_id,
    int add_config_version, uint64_t config_version,
    int add_quorate, enum tlv_quorate quorate,
    int add_heuristics, enum tlv_heuristics heuristics,
    const struct node_list *nodes)
{
	struct node_list_entry *node_info;
	struct tlv_node_info tlv_ni;

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_NODE_LIST);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_node_list_type(msg, node_list_type) == -1) {
		goto small_buf_err;
	}

	if (add_ring_id) {
		if (tlv_add_ring_id(msg, ring_id) == -1) {
			goto small_buf_err;
		}
	}

	if (add_config_version) {
		if (tlv_add_config_version(msg, config_version) == -1) {
			goto small_buf_err;
		}
	}

	if (add_quorate) {
		if (tlv_add_quorate(msg, quorate) == -1) {
			goto small_buf_err;
		}
	}

	TAILQ_FOREACH(node_info, nodes, entries) {
		node_list_entry_to_tlv_node_info(node_info, &tlv_ni);

		if (tlv_add_node_info(msg, &tlv_ni) == -1) {
			goto small_buf_err;
		}
	}

	if (add_heuristics && heuristics != TLV_HEURISTICS_UNDEFINED) {
		if (tlv_add_heuristics(msg, heuristics) == -1) {
			goto small_buf_err;
		}
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_node_list_reply(struct dynar *msg, uint32_t msg_seq_number,
    enum tlv_node_list_type node_list_type, const struct tlv_ring_id *ring_id,
    enum tlv_vote vote)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_NODE_LIST_REPLY);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_node_list_type(msg, node_list_type) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_ring_id(msg, ring_id) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_vote(msg, vote) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_ask_for_vote(struct dynar *msg, uint32_t msg_seq_number)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_ASK_FOR_VOTE);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_ask_for_vote_reply(struct dynar *msg, uint32_t msg_seq_number,
    const struct tlv_ring_id *ring_id, enum tlv_vote vote)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_ASK_FOR_VOTE_REPLY);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_vote(msg, vote) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_ring_id(msg, ring_id) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_vote_info(struct dynar *msg, uint32_t msg_seq_number, const struct tlv_ring_id *ring_id,
    enum tlv_vote vote)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_VOTE_INFO);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_vote(msg, vote) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_ring_id(msg, ring_id) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_vote_info_reply(struct dynar *msg, uint32_t msg_seq_number)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_VOTE_INFO_REPLY);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_heuristics_change(struct dynar *msg, uint32_t msg_seq_number,
    enum tlv_heuristics heuristics)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_HEURISTICS_CHANGE);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_heuristics(msg, heuristics) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

size_t
msg_create_heuristics_change_reply(struct dynar *msg, uint32_t msg_seq_number,
    const struct tlv_ring_id *ring_id, enum tlv_heuristics heuristics, enum tlv_vote vote)
{

	dynar_clean(msg);

	msg_add_type(msg, MSG_TYPE_HEURISTICS_CHANGE_REPLY);
	msg_add_len(msg);

	if (tlv_add_msg_seq_number(msg, msg_seq_number) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_vote(msg, vote) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_ring_id(msg, ring_id) == -1) {
		goto small_buf_err;
	}

	if (tlv_add_heuristics(msg, heuristics) == -1) {
		goto small_buf_err;
	}

	msg_set_len(msg, dynar_size(msg) - (MSG_TYPE_LENGTH + MSG_LENGTH_LENGTH));

	return (dynar_size(msg));

small_buf_err:
	return (0);
}

int
msg_is_valid_msg_type(const struct dynar *msg)
{
	enum msg_type type;
	size_t i;

	type = msg_get_type(msg);

	for (i = 0; i < MSG_STATIC_SUPPORTED_MESSAGES_SIZE; i++) {
		if (msg_static_supported_messages[i] == type) {
			return (1);
		}
	}

	return (0);
}

void
msg_decoded_init(struct msg_decoded *decoded_msg)
{

	memset(decoded_msg, 0, sizeof(*decoded_msg));

	node_list_init(&decoded_msg->nodes);
}

void
msg_decoded_destroy(struct msg_decoded *decoded_msg)
{

	free(decoded_msg->cluster_name);
	free(decoded_msg->supported_messages);
	free(decoded_msg->supported_options);
	free(decoded_msg->supported_decision_algorithms);
	node_list_free(&decoded_msg->nodes);

	msg_decoded_init(decoded_msg);
}

/*
 *  0 - No error
 * -1 - option with invalid length
 * -2 - Unable to allocate memory
 * -3 - Inconsistent msg (tlv len > msg size)
 * -4 - invalid option content
 */
int
msg_decode(const struct dynar *msg, struct msg_decoded *decoded_msg)
{
	struct tlv_iterator tlv_iter;
	uint16_t *u16a;
	uint32_t u32;
	uint64_t u64;
	struct tlv_ring_id ring_id;
	struct tlv_node_info node_info;
	struct tlv_tie_breaker tie_breaker;
	size_t zi;
	enum tlv_opt_type opt_type;
	int iter_res;
	int res;

	msg_decoded_destroy(decoded_msg);

	decoded_msg->type = msg_get_type(msg);

	tlv_iter_init(msg, msg_get_header_length(), &tlv_iter);

	while ((iter_res = tlv_iter_next(&tlv_iter)) > 0) {
		opt_type = tlv_iter_get_type(&tlv_iter);

		switch (opt_type) {
		case TLV_OPT_MSG_SEQ_NUMBER:
			if ((res = tlv_iter_decode_u32(&tlv_iter, &u32)) != 0) {
				return (res);
			}

			decoded_msg->seq_number_set = 1;
			decoded_msg->seq_number = u32;
			break;
		case TLV_OPT_CLUSTER_NAME:
			if ((res = tlv_iter_decode_str(&tlv_iter, &decoded_msg->cluster_name,
			    &decoded_msg->cluster_name_len)) != 0) {
				return (-2);
			}
			break;
		case TLV_OPT_TLS_SUPPORTED:
			if ((res = tlv_iter_decode_tls_supported(&tlv_iter,
			    &decoded_msg->tls_supported)) != 0) {
				return (res);
			}

			decoded_msg->tls_supported_set = 1;
			break;
		case TLV_OPT_TLS_CLIENT_CERT_REQUIRED:
			if ((res = tlv_iter_decode_client_cert_required(&tlv_iter,
			    &decoded_msg->tls_client_cert_required)) != 0) {
				return (res);
			}

			decoded_msg->tls_client_cert_required_set = 1;
			break;
		case TLV_OPT_SUPPORTED_MESSAGES:
			free(decoded_msg->supported_messages);

			if ((res = tlv_iter_decode_u16_array(&tlv_iter, &u16a,
			    &decoded_msg->no_supported_messages)) != 0) {
				return (res);
			}

			decoded_msg->supported_messages =
			    malloc(sizeof(enum msg_type) * decoded_msg->no_supported_messages);

			if (decoded_msg->supported_messages == NULL) {
				free(u16a);
				return (-2);
			}

			for (zi = 0; zi < decoded_msg->no_supported_messages; zi++) {
				decoded_msg->supported_messages[zi] = (enum msg_type)u16a[zi];
			}

			free(u16a);
			break;
		case TLV_OPT_SUPPORTED_OPTIONS:
			free(decoded_msg->supported_options);

			if ((res = tlv_iter_decode_supported_options(&tlv_iter,
			    &decoded_msg->supported_options,
			    &decoded_msg->no_supported_options)) != 0) {
				return (res);
			}
			break;
		case TLV_OPT_REPLY_ERROR_CODE:
			if ((res = tlv_iter_decode_reply_error_code(&tlv_iter,
			    &decoded_msg->reply_error_code)) != 0) {
				return (res);
			}

			decoded_msg->reply_error_code_set = 1;
			break;
		case TLV_OPT_SERVER_MAXIMUM_REQUEST_SIZE:
			if ((res = tlv_iter_decode_u32(&tlv_iter, &u32)) != 0) {
				return (res);
			}

			decoded_msg->server_maximum_request_size_set = 1;
			decoded_msg->server_maximum_request_size = u32;
			break;
		case TLV_OPT_SERVER_MAXIMUM_REPLY_SIZE:
			if ((res = tlv_iter_decode_u32(&tlv_iter, &u32)) != 0) {
				return (res);
			}

			decoded_msg->server_maximum_reply_size_set = 1;
			decoded_msg->server_maximum_reply_size = u32;
			break;
		case TLV_OPT_NODE_ID:
			if ((res = tlv_iter_decode_u32(&tlv_iter, &u32)) != 0) {
				return (res);
			}

			decoded_msg->node_id_set = 1;
			decoded_msg->node_id = u32;
			break;
		case TLV_OPT_SUPPORTED_DECISION_ALGORITHMS:
			free(decoded_msg->supported_decision_algorithms);

			if ((res = tlv_iter_decode_supported_decision_algorithms(&tlv_iter,
			    &decoded_msg->supported_decision_algorithms,
			    &decoded_msg->no_supported_decision_algorithms)) != 0) {
				return (res);
			}
			break;
		case TLV_OPT_DECISION_ALGORITHM:
			if ((res = tlv_iter_decode_decision_algorithm(&tlv_iter,
			    &decoded_msg->decision_algorithm)) != 0) {
				return (res);
			}

			decoded_msg->decision_algorithm_set = 1;
			break;
		case TLV_OPT_HEARTBEAT_INTERVAL:
			if ((res = tlv_iter_decode_u32(&tlv_iter, &u32)) != 0) {
				return (res);
			}

			decoded_msg->heartbeat_interval_set = 1;
			decoded_msg->heartbeat_interval = u32;
			break;
		case TLV_OPT_RING_ID:
			if ((res = tlv_iter_decode_ring_id(&tlv_iter, &ring_id)) != 0) {
				return (res);
			}

			decoded_msg->ring_id_set = 1;
			memcpy(&decoded_msg->ring_id, &ring_id, sizeof(ring_id));
			break;
		case TLV_OPT_CONFIG_VERSION:
			if ((res = tlv_iter_decode_u64(&tlv_iter, &u64)) != 0) {
				return (res);
			}

			decoded_msg->config_version_set = 1;
			decoded_msg->config_version = u64;
			break;
		case TLV_OPT_DATA_CENTER_ID:
			if ((res = tlv_iter_decode_u32(&tlv_iter, &u32)) != 0) {
				return (res);
			}

			decoded_msg->data_center_id = u32;
			break;
		case TLV_OPT_NODE_STATE:
			if ((res = tlv_iter_decode_node_state(&tlv_iter,
			    &decoded_msg->node_state)) != 0) {
				return (res);
			}
			break;
		case TLV_OPT_NODE_INFO:
			if ((res = tlv_iter_decode_node_info(&tlv_iter, &node_info)) != 0) {
				return (res);
			}

			if (node_list_add_from_node_info(&decoded_msg->nodes, &node_info) == NULL) {
				return (-2);
			}
			break;
		case TLV_OPT_NODE_LIST_TYPE:
			if ((res = tlv_iter_decode_node_list_type(&tlv_iter,
			    &decoded_msg->node_list_type)) != 0) {
				return (res);
			}

			decoded_msg->node_list_type_set = 1;
			break;
		case TLV_OPT_VOTE:
			if ((res = tlv_iter_decode_vote(&tlv_iter, &decoded_msg->vote)) != 0) {
				return (res);
			}

			decoded_msg->vote_set = 1;
			break;
		case TLV_OPT_QUORATE:
			if ((res = tlv_iter_decode_quorate(&tlv_iter,
			    &decoded_msg->quorate)) != 0) {
				return (res);
			}

			decoded_msg->quorate_set = 1;
			break;
		case TLV_OPT_TIE_BREAKER:
			if ((res = tlv_iter_decode_tie_breaker(&tlv_iter, &tie_breaker)) != 0) {
				return (res);
			}

			decoded_msg->tie_breaker_set = 1;
			memcpy(&decoded_msg->tie_breaker, &tie_breaker, sizeof(tie_breaker));
			break;
		case TLV_OPT_HEURISTICS:
			if ((res = tlv_iter_decode_heuristics(&tlv_iter,
			    &decoded_msg->heuristics)) != 0) {
				return (res);
			}
			break;
		/*
		 * Default is not defined intentionally. Compiler shows warning when
		 * new tlv option is added. Also protocol ignores unknown options so
		 * no extra work is needed.
		 */
		}
	}

	if (iter_res != 0) {
		return (-3);
	}

	return (0);
}

void
msg_get_supported_messages(enum msg_type **supported_messages, size_t *no_supported_messages)
{

	*supported_messages = msg_static_supported_messages;
	*no_supported_messages = MSG_STATIC_SUPPORTED_MESSAGES_SIZE;
}

const char *
msg_type_to_str(enum msg_type type)
{

	switch (type) {
	case MSG_TYPE_PREINIT: return ("Preinit"); break;
	case MSG_TYPE_PREINIT_REPLY: return ("Preinit reply"); break;
	case MSG_TYPE_STARTTLS: return ("StartTLS"); break;
	case MSG_TYPE_INIT: return ("Init"); break;
	case MSG_TYPE_INIT_REPLY: return ("Init reply"); break;
	case MSG_TYPE_SERVER_ERROR: return ("Server error"); break;
	case MSG_TYPE_SET_OPTION: return ("Set option"); break;
	case MSG_TYPE_SET_OPTION_REPLY: return ("Set option reply"); break;
	case MSG_TYPE_ECHO_REQUEST: return ("Echo request"); break;
	case MSG_TYPE_ECHO_REPLY: return ("Echo reply"); break;
	case MSG_TYPE_NODE_LIST: return ("Node list"); break;
	case MSG_TYPE_NODE_LIST_REPLY: return ("Node list reply"); break;
	case MSG_TYPE_ASK_FOR_VOTE: return ("Ask for vote"); break;
	case MSG_TYPE_ASK_FOR_VOTE_REPLY: return ("Ask for vote reply"); break;
	case MSG_TYPE_VOTE_INFO: return ("Vote info"); break;
	case MSG_TYPE_VOTE_INFO_REPLY: return ("Vote info reply"); break;
	case MSG_TYPE_HEURISTICS_CHANGE: return ("Heuristics change"); break;
	case MSG_TYPE_HEURISTICS_CHANGE_REPLY: return ("Heuristics change reply"); break;
	}

	return ("Unknown message type");
}
