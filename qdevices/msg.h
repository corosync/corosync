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

#ifndef _MSG_H_
#define _MSG_H_

#include <sys/types.h>
#include <inttypes.h>

#include "dynar.h"
#include "tlv.h"
#include "node-list.h"

#ifdef __cplusplus
extern "C" {
#endif

enum msg_type {
	MSG_TYPE_PREINIT = 0,
	MSG_TYPE_PREINIT_REPLY = 1,
	MSG_TYPE_STARTTLS = 2,
	MSG_TYPE_INIT = 3,
	MSG_TYPE_INIT_REPLY = 4,
	MSG_TYPE_SERVER_ERROR = 5,
	MSG_TYPE_SET_OPTION = 6,
	MSG_TYPE_SET_OPTION_REPLY = 7,
	MSG_TYPE_ECHO_REQUEST = 8,
	MSG_TYPE_ECHO_REPLY = 9,
	MSG_TYPE_NODE_LIST = 10,
	MSG_TYPE_NODE_LIST_REPLY = 11,
	MSG_TYPE_ASK_FOR_VOTE = 12,
	MSG_TYPE_ASK_FOR_VOTE_REPLY = 13,
	MSG_TYPE_VOTE_INFO = 14,
	MSG_TYPE_VOTE_INFO_REPLY = 15,
	MSG_TYPE_HEURISTICS_CHANGE = 16,
	MSG_TYPE_HEURISTICS_CHANGE_REPLY = 17,
};

struct msg_decoded {
	enum msg_type type;
	uint8_t seq_number_set;
	uint32_t seq_number;	/* Only valid if seq_number_set != 0 */
	size_t cluster_name_len;
	/* Valid only if != NULL. Trailing \0 is added but not counted in cluster_name_len */
	char *cluster_name;
	uint8_t tls_supported_set;
	enum tlv_tls_supported tls_supported;	/* Valid only if tls_supported_set != 0. */
	uint8_t tls_client_cert_required_set;
	uint8_t tls_client_cert_required;	/* Valid only if tls_client_cert_required_set != 0 */
	size_t no_supported_messages;
	enum msg_type *supported_messages;	/* Valid only if != NULL */
	size_t no_supported_options;
	enum tlv_opt_type *supported_options;	/* Valid only if != NULL */
	uint8_t reply_error_code_set;
	enum tlv_reply_error_code reply_error_code;	/* Valid only if reply_error_code_set != 0 */
	uint8_t server_maximum_request_size_set;
	/* Valid only if server_maximum_request_size_set != 0 */
	size_t server_maximum_request_size;
	uint8_t server_maximum_reply_size_set;
	size_t server_maximum_reply_size;	/* Valid only if server_maximum_reply_size_set != 0 */
	uint8_t node_id_set;
	uint32_t node_id;
	size_t no_supported_decision_algorithms;
	/* Valid only if != NULL */
	enum tlv_decision_algorithm_type *supported_decision_algorithms;
	uint8_t decision_algorithm_set;
	/* Valid only if decision_algorithm_set != 0 */
	enum tlv_decision_algorithm_type decision_algorithm;
	uint8_t heartbeat_interval_set;
	uint32_t heartbeat_interval;	/* Valid only if heartbeat_interval_set != 0 */
	uint8_t ring_id_set;
	struct tlv_ring_id ring_id;	/* Valid only if ring_id_set != 0 */
	uint8_t config_version_set;
	uint64_t config_version;	/* Valid only if config_version_set != 0 */
	uint32_t data_center_id;	/* Valid only if != 0 */
	enum tlv_node_state node_state;	/* Valid only if != TLV_NODE_STATE_NOT_SET */
	struct node_list nodes;		/* Valid only if node_list_is_empty(nodes) != 0 */
	int node_list_type_set;
	enum tlv_node_list_type node_list_type;	/* Valid only if node_list_type_set != 0 */
	int vote_set;
	enum tlv_vote vote;	/* Valid only if vote_set != 0 */
	int quorate_set;
	enum tlv_quorate quorate;	/* Valid only if quorate_set != 0 */
	int tie_breaker_set;
	struct tlv_tie_breaker tie_breaker;
	enum tlv_heuristics heuristics;	/* Always valid but can be TLV_HEURISTICS_UNDEFINED */
};

extern size_t		msg_create_preinit(struct dynar *msg, const char *cluster_name,
    int add_msg_seq_number, uint32_t msg_seq_number);

extern size_t		msg_create_preinit_reply(struct dynar *msg, int add_msg_seq_number,
    uint32_t msg_seq_number, enum tlv_tls_supported tls_supported, int tls_client_cert_required);

extern size_t		msg_create_starttls(struct dynar *msg, int add_msg_seq_number,
    uint32_t msg_seq_number);

extern size_t		msg_create_init(struct dynar *msg, int add_msg_seq_number,
    uint32_t msg_seq_number, enum tlv_decision_algorithm_type decision_algorithm,
    const enum msg_type *supported_msgs, size_t no_supported_msgs,
    const enum tlv_opt_type *supported_opts, size_t no_supported_opts, uint32_t node_id,
    uint32_t heartbeat_interval, const struct tlv_tie_breaker *tie_breaker,
    const struct tlv_ring_id *ring_id);

extern size_t		msg_create_server_error(struct dynar *msg, int add_msg_seq_number,
    uint32_t msg_seq_number, enum tlv_reply_error_code reply_error_code);

extern size_t		msg_create_init_reply(struct dynar *msg, int add_msg_seq_number,
    uint32_t msg_seq_number, enum tlv_reply_error_code reply_error_code,
    const enum msg_type *supported_msgs, size_t no_supported_msgs,
    const enum tlv_opt_type *supported_opts, size_t no_supported_opts,
    size_t server_maximum_request_size, size_t server_maximum_reply_size,
    const enum tlv_decision_algorithm_type *supported_decision_algorithms,
    size_t no_supported_decision_algorithms);

extern size_t		msg_create_set_option(struct dynar *msg,
    int add_msg_seq_number, uint32_t msg_seq_number,
    int add_heartbeat_interval, uint32_t heartbeat_interval);

extern size_t		msg_create_set_option_reply(struct dynar *msg,
    int add_msg_seq_number, uint32_t msg_seq_number, uint32_t heartbeat_interval);

extern size_t		msg_create_echo_request(struct dynar *msg, int add_msg_seq_number,
    uint32_t msg_seq_number);

extern size_t		msg_create_echo_reply(struct dynar *msg,
    const struct dynar *echo_request_msg);

extern size_t		msg_create_node_list(struct dynar *msg,
    uint32_t msg_seq_number, enum tlv_node_list_type node_list_type,
    int add_ring_id, const struct tlv_ring_id *ring_id,
    int add_config_version, uint64_t config_version,
    int add_quorate, enum tlv_quorate quorate,
    int add_heuristics, enum tlv_heuristics heuristics,
    const struct node_list *nodes);

extern size_t		msg_create_node_list_reply(struct dynar *msg, uint32_t msg_seq_number,
    enum tlv_node_list_type node_list_type, const struct tlv_ring_id *ring_id,
    enum tlv_vote vote);

extern size_t		msg_create_ask_for_vote(struct dynar *msg, uint32_t msg_seq_number);

extern size_t		msg_create_ask_for_vote_reply(struct dynar *msg, uint32_t msg_seq_number,
    const struct tlv_ring_id *ring_id, enum tlv_vote vote);

extern size_t		msg_create_vote_info(struct dynar *msg, uint32_t msg_seq_number,
    const struct tlv_ring_id *ring_id, enum tlv_vote vote);

extern size_t		msg_create_vote_info_reply(struct dynar *msg, uint32_t msg_seq_number);

extern size_t		msg_create_heuristics_change(struct dynar *msg, uint32_t msg_seq_number,
    enum tlv_heuristics heuristics);

extern size_t		msg_create_heuristics_change_reply(struct dynar *msg,
    uint32_t msg_seq_number, const struct tlv_ring_id *ring_id, enum tlv_heuristics heuristics,
    enum tlv_vote vote);

extern size_t		msg_get_header_length(void);

extern uint32_t		msg_get_len(const struct dynar *msg);

extern enum msg_type	msg_get_type(const struct dynar *msg);

extern int		msg_is_valid_msg_type(const struct dynar *msg);

extern void		msg_decoded_init(struct msg_decoded *decoded_msg);

extern void		msg_decoded_destroy(struct msg_decoded *decoded_msg);

extern int		msg_decode(const struct dynar *msg, struct msg_decoded *decoded_msg);

extern void		msg_get_supported_messages(enum msg_type **supported_messages,
    size_t *no_supported_messages);

extern const char *	msg_type_to_str(enum msg_type type);

#ifdef __cplusplus
}
#endif

#endif /* _MSG_H_ */
