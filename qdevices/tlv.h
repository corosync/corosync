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

#ifndef _TLV_H_
#define _TLV_H_

#include <sys/types.h>
#include <inttypes.h>

#include "dynar.h"

#ifdef __cplusplus
extern "C" {
#endif

enum tlv_opt_type {
	TLV_OPT_MSG_SEQ_NUMBER = 0,
	TLV_OPT_CLUSTER_NAME = 1,
	TLV_OPT_TLS_SUPPORTED = 2,
	TLV_OPT_TLS_CLIENT_CERT_REQUIRED = 3,
	TLV_OPT_SUPPORTED_MESSAGES = 4,
	TLV_OPT_SUPPORTED_OPTIONS = 5,
	TLV_OPT_REPLY_ERROR_CODE = 6,
	TLV_OPT_SERVER_MAXIMUM_REQUEST_SIZE = 7,
	TLV_OPT_SERVER_MAXIMUM_REPLY_SIZE = 8,
	TLV_OPT_NODE_ID = 9,
	TLV_OPT_SUPPORTED_DECISION_ALGORITHMS = 10,
	TLV_OPT_DECISION_ALGORITHM = 11,
	TLV_OPT_HEARTBEAT_INTERVAL = 12,
	TLV_OPT_RING_ID = 13,
	TLV_OPT_CONFIG_VERSION = 14,
	TLV_OPT_DATA_CENTER_ID = 15,
	TLV_OPT_NODE_STATE = 16,
	TLV_OPT_NODE_INFO = 17,
	TLV_OPT_NODE_LIST_TYPE = 18,
	TLV_OPT_VOTE = 19,
	TLV_OPT_QUORATE = 20,
	TLV_OPT_TIE_BREAKER = 21,
	TLV_OPT_HEURISTICS = 22,
};

enum tlv_tls_supported {
	TLV_TLS_UNSUPPORTED = 0,
	TLV_TLS_SUPPORTED = 1,
	TLV_TLS_REQUIRED = 2,
};

enum tlv_reply_error_code {
	TLV_REPLY_ERROR_CODE_NO_ERROR = 0,
	TLV_REPLY_ERROR_CODE_UNSUPPORTED_NEEDED_MESSAGE = 1,
	TLV_REPLY_ERROR_CODE_UNSUPPORTED_NEEDED_OPTION = 2,
	TLV_REPLY_ERROR_CODE_TLS_REQUIRED = 3,
	TLV_REPLY_ERROR_CODE_UNSUPPORTED_MESSAGE = 4,
	TLV_REPLY_ERROR_CODE_MESSAGE_TOO_LONG = 5,
	TLV_REPLY_ERROR_CODE_PREINIT_REQUIRED = 6,
	TLV_REPLY_ERROR_CODE_DOESNT_CONTAIN_REQUIRED_OPTION = 7,
	TLV_REPLY_ERROR_CODE_UNEXPECTED_MESSAGE = 8,
	TLV_REPLY_ERROR_CODE_ERROR_DECODING_MSG = 9,
	TLV_REPLY_ERROR_CODE_INTERNAL_ERROR = 10,
	TLV_REPLY_ERROR_CODE_INIT_REQUIRED = 11,
	TLV_REPLY_ERROR_CODE_UNSUPPORTED_DECISION_ALGORITHM = 12,
	TLV_REPLY_ERROR_CODE_INVALID_HEARTBEAT_INTERVAL = 13,
	TLV_REPLY_ERROR_CODE_UNSUPPORTED_DECISION_ALGORITHM_MESSAGE = 14,
	TLV_REPLY_ERROR_CODE_TIE_BREAKER_DIFFERS_FROM_OTHER_NODES = 15,
	TLV_REPLY_ERROR_CODE_ALGORITHM_DIFFERS_FROM_OTHER_NODES = 16,
	TLV_REPLY_ERROR_CODE_DUPLICATE_NODE_ID = 17,
	TLV_REPLY_ERROR_CODE_INVALID_CONFIG_NODE_LIST = 18,
	TLV_REPLY_ERROR_CODE_INVALID_MEMBERSHIP_NODE_LIST = 19,
};

enum tlv_decision_algorithm_type {
	TLV_DECISION_ALGORITHM_TYPE_TEST = 0,
	TLV_DECISION_ALGORITHM_TYPE_FFSPLIT = 1,
	TLV_DECISION_ALGORITHM_TYPE_2NODELMS = 2,
	TLV_DECISION_ALGORITHM_TYPE_LMS = 3,
};

struct tlv_ring_id {
	uint32_t node_id;
	uint64_t seq;
};

enum tlv_node_state {
	TLV_NODE_STATE_NOT_SET = 0,
	TLV_NODE_STATE_MEMBER = 1,
	TLV_NODE_STATE_DEAD = 2,
	TLV_NODE_STATE_LEAVING = 3,
};

enum tlv_node_list_type {
	TLV_NODE_LIST_TYPE_INITIAL_CONFIG = 0,
	TLV_NODE_LIST_TYPE_CHANGED_CONFIG = 1,
	TLV_NODE_LIST_TYPE_MEMBERSHIP = 2,
	TLV_NODE_LIST_TYPE_QUORUM = 3,
};

enum tlv_vote {
	TLV_VOTE_UNDEFINED = 0,
	TLV_VOTE_ACK = 1,
	TLV_VOTE_NACK = 2,
	TLV_VOTE_ASK_LATER = 3,
	TLV_VOTE_WAIT_FOR_REPLY = 4,
	TLV_VOTE_NO_CHANGE = 5,
};

enum tlv_quorate {
	TLV_QUORATE_INQUORATE = 0,
	TLV_QUORATE_QUORATE = 1,
};

enum tlv_tie_breaker_mode {
	TLV_TIE_BREAKER_MODE_LOWEST = 1,
	TLV_TIE_BREAKER_MODE_HIGHEST = 2,
	TLV_TIE_BREAKER_MODE_NODE_ID = 3,
};

struct tlv_tie_breaker {
	enum tlv_tie_breaker_mode mode;
	uint32_t node_id;
};

struct tlv_node_info {
	uint32_t node_id;
	uint32_t data_center_id;		/* 0 - data center id was not set */
	enum tlv_node_state node_state;		/* TLV_NODE_STATE_NOT_SET - state was not set */
};

enum tlv_heuristics {
	TLV_HEURISTICS_UNDEFINED = 0,
	TLV_HEURISTICS_PASS = 1,
	TLV_HEURISTICS_FAIL = 2,
};

struct tlv_iterator {
	const char *msg;
	size_t msg_len;
	size_t current_pos;
	size_t msg_header_len;
	int iter_next_called;
};

extern int			 tlv_add(struct dynar *msg, enum tlv_opt_type opt_type,
    uint16_t opt_len, const void *value);

extern int			 tlv_add_u32(struct dynar *msg, enum tlv_opt_type opt_type,
    uint32_t u32);

extern int			 tlv_add_u8(struct dynar *msg, enum tlv_opt_type opt_type,
    uint8_t u8);

extern int			 tlv_add_u16(struct dynar *msg, enum tlv_opt_type opt_type,
    uint16_t u16);

extern int			 tlv_add_u64(struct dynar *msg, enum tlv_opt_type opt_type,
    uint64_t u64);

extern int			 tlv_add_string(struct dynar *msg, enum tlv_opt_type opt_type,
    const char *str);

extern int			 tlv_add_u16_array(struct dynar *msg, enum tlv_opt_type opt_type,
    const uint16_t *array, size_t array_size);

extern int			 tlv_add_supported_options(struct dynar *msg,
    const enum tlv_opt_type *supported_options, size_t no_supported_options);

extern int			 tlv_add_msg_seq_number(struct dynar *msg,
    uint32_t msg_seq_number);

extern int			 tlv_add_cluster_name(struct dynar *msg, const char *cluster_name);

extern int			 tlv_add_tls_supported(struct dynar *msg,
    enum tlv_tls_supported tls_supported);

extern int			 tlv_add_tls_client_cert_required(struct dynar *msg,
    int tls_client_cert_required);

extern int			 tlv_add_reply_error_code(struct dynar *msg,
    enum tlv_reply_error_code error_code);

extern int			 tlv_add_node_id(struct dynar *msg, uint32_t node_id);

extern int			 tlv_add_server_maximum_request_size(struct dynar *msg,
    size_t server_maximum_request_size);

extern int			 tlv_add_server_maximum_reply_size(struct dynar *msg,
    size_t server_maximum_reply_size);

extern int			 tlv_add_supported_decision_algorithms(struct dynar *msg,
    const enum tlv_decision_algorithm_type *supported_algorithms, size_t no_supported_algorithms);

extern int			 tlv_add_decision_algorithm(struct dynar *msg,
    enum tlv_decision_algorithm_type decision_algorithm);

extern int			 tlv_add_heartbeat_interval(struct dynar *msg,
    uint32_t heartbeat_interval);

extern int			 tlv_add_ring_id(struct dynar *msg,
    const struct tlv_ring_id *ring_id);

extern int			 tlv_add_tie_breaker(struct dynar *msg,
    const struct tlv_tie_breaker *tie_breaker);

extern int			 tlv_add_config_version(struct dynar *msg,
    uint64_t config_version);

extern int			 tlv_add_data_center_id(struct dynar *msg,
    uint32_t data_center_id);

extern int			 tlv_add_node_state(struct dynar *msg,
    enum tlv_node_state node_state);

extern int			 tlv_add_node_info(struct dynar *msg,
    const struct tlv_node_info *node_info);

extern int			 tlv_add_node_list_type(struct dynar *msg,
    enum tlv_node_list_type node_list_type);

extern int			 tlv_add_vote(struct dynar *msg, enum tlv_vote vote);

extern int			 tlv_add_quorate(struct dynar *msg, enum tlv_quorate quorate);

extern int			 tlv_add_heuristics(struct dynar *msg,
    enum tlv_heuristics heuristics);

extern void			 tlv_iter_init_str(const char *msg, size_t msg_len,
    size_t msg_header_len, struct tlv_iterator *tlv_iter);

extern void			 tlv_iter_init(const struct dynar *msg, size_t msg_header_len,
    struct tlv_iterator *tlv_iter);

extern enum tlv_opt_type	 tlv_iter_get_type(const struct tlv_iterator *tlv_iter);

extern uint16_t			 tlv_iter_get_len(const struct tlv_iterator *tlv_iter);

extern const char		*tlv_iter_get_data(const struct tlv_iterator *tlv_iter);

extern int			 tlv_iter_next(struct tlv_iterator *tlv_iter);

extern int			 tlv_iter_decode_u8(struct tlv_iterator *tlv_iter, uint8_t *res);

extern int			 tlv_iter_decode_tls_supported(struct tlv_iterator *tlv_iter,
    enum tlv_tls_supported *tls_supported);

extern int			 tlv_iter_decode_u32(struct tlv_iterator *tlv_iter,
    uint32_t *res);

extern int			 tlv_iter_decode_str(struct tlv_iterator *tlv_iter, char **str,
    size_t *str_len);

extern int			 tlv_iter_decode_client_cert_required(
    struct tlv_iterator *tlv_iter, uint8_t *client_cert_required);

extern int			 tlv_iter_decode_u16_array(struct tlv_iterator *tlv_iter,
    uint16_t **u16a, size_t *no_items);

extern int			 tlv_iter_decode_supported_options(struct tlv_iterator *tlv_iter,
    enum tlv_opt_type **supported_options, size_t *no_supported_options);

extern int			 tlv_iter_decode_supported_decision_algorithms(
    struct tlv_iterator *tlv_iter,
    enum tlv_decision_algorithm_type **supported_decision_algorithms,
    size_t *no_supported_decision_algorithms);

extern int			 tlv_iter_decode_u16(struct tlv_iterator *tlv_iter,
    uint16_t *u16);

extern int			 tlv_iter_decode_u64(struct tlv_iterator *tlv_iter,
    uint64_t *u64);

extern int			 tlv_iter_decode_reply_error_code(struct tlv_iterator *tlv_iter,
    enum tlv_reply_error_code *reply_error_code);

extern int			 tlv_iter_decode_decision_algorithm(struct tlv_iterator *tlv_iter,
    enum tlv_decision_algorithm_type *decision_algorithm);

extern int			 tlv_iter_decode_ring_id(struct tlv_iterator *tlv_iter,
    struct tlv_ring_id *ring_id);

extern int			 tlv_iter_decode_tie_breaker(struct tlv_iterator *tlv_iter,
    struct tlv_tie_breaker *tie_breaker);

extern int			 tlv_iter_decode_node_state(struct tlv_iterator *tlv_iter,
    enum tlv_node_state *node_state);

extern int			 tlv_iter_decode_node_info(struct tlv_iterator *tlv_iter,
    struct tlv_node_info *node_info);

extern int			 tlv_iter_decode_node_list_type(struct tlv_iterator *tlv_iter,
    enum tlv_node_list_type *node_list_type);

extern int			 tlv_iter_decode_vote(struct tlv_iterator *tlv_iter,
    enum tlv_vote *vote);

extern int			 tlv_iter_decode_quorate(struct tlv_iterator *tlv_iter,
    enum tlv_quorate *quorate);

extern int			 tlv_iter_decode_heuristics(struct tlv_iterator *tlv_iter,
    enum tlv_heuristics *heuristics);

extern void			 tlv_get_supported_options(enum tlv_opt_type **supported_options,
    size_t *no_supported_options);

extern int			 tlv_ring_id_eq(const struct tlv_ring_id *rid1,
    const struct tlv_ring_id *rid2);

extern int			 tlv_tie_breaker_eq(const struct tlv_tie_breaker *tb1,
    const struct tlv_tie_breaker *tb2);

extern const char		*tlv_vote_to_str(enum tlv_vote vote);

extern const char		*tlv_node_state_to_str(enum tlv_node_state state);

extern const char		*tlv_tls_supported_to_str(enum tlv_tls_supported tls_supported);

extern const char		*tlv_decision_algorithm_type_to_str(
    enum tlv_decision_algorithm_type algorithm);

extern const char		*tlv_heuristics_to_str(enum tlv_heuristics heuristics);

/*
 * Compare h1 and h2. Return -1 if h1 < h2, 0 if h1 == h2 and 1 if h1 > h2
 */
extern int			 tlv_heuristics_cmp(enum tlv_heuristics h1, enum tlv_heuristics h2);

#ifdef __cplusplus
}
#endif

#endif /* _TLV_H_ */
