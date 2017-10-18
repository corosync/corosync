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

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/*
 * 64-bit variant of ntoh is not exactly standard...
 */
#if defined(__linux__)
#include <endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/endian.h>
#elif defined(__OpenBSD__)
#define be64toh(x) betoh64(x)
#endif

#include "tlv.h"

#define TLV_TYPE_LENGTH		2
#define TLV_LENGTH_LENGTH	2

#define TLV_STATIC_SUPPORTED_OPTIONS_SIZE	23

enum tlv_opt_type tlv_static_supported_options[TLV_STATIC_SUPPORTED_OPTIONS_SIZE] = {
    TLV_OPT_MSG_SEQ_NUMBER,
    TLV_OPT_CLUSTER_NAME,
    TLV_OPT_TLS_SUPPORTED,
    TLV_OPT_TLS_CLIENT_CERT_REQUIRED,
    TLV_OPT_SUPPORTED_MESSAGES,
    TLV_OPT_SUPPORTED_OPTIONS,
    TLV_OPT_REPLY_ERROR_CODE,
    TLV_OPT_SERVER_MAXIMUM_REQUEST_SIZE,
    TLV_OPT_SERVER_MAXIMUM_REPLY_SIZE,
    TLV_OPT_NODE_ID,
    TLV_OPT_SUPPORTED_DECISION_ALGORITHMS,
    TLV_OPT_DECISION_ALGORITHM,
    TLV_OPT_HEARTBEAT_INTERVAL,
    TLV_OPT_RING_ID,
    TLV_OPT_CONFIG_VERSION,
    TLV_OPT_DATA_CENTER_ID,
    TLV_OPT_NODE_STATE,
    TLV_OPT_NODE_INFO,
    TLV_OPT_NODE_LIST_TYPE,
    TLV_OPT_VOTE,
    TLV_OPT_QUORATE,
    TLV_OPT_TIE_BREAKER,
    TLV_OPT_HEURISTICS,
};

int
tlv_add(struct dynar *msg, enum tlv_opt_type opt_type, uint16_t opt_len, const void *value)
{
	uint16_t nlen;
	uint16_t nopt_type;

	if (dynar_size(msg) + sizeof(nopt_type) + sizeof(nlen) + opt_len > dynar_max_size(msg)) {
		return (-1);
	}

	nopt_type = htons((uint16_t)opt_type);
	nlen = htons(opt_len);

	dynar_cat(msg, &nopt_type, sizeof(nopt_type));
	dynar_cat(msg, &nlen, sizeof(nlen));
	dynar_cat(msg, value, opt_len);

	return (0);
}

int
tlv_add_u32(struct dynar *msg, enum tlv_opt_type opt_type, uint32_t u32)
{
	uint32_t nu32;

	nu32 = htonl(u32);

	return (tlv_add(msg, opt_type, sizeof(nu32), &nu32));
}

int
tlv_add_u8(struct dynar *msg, enum tlv_opt_type opt_type, uint8_t u8)
{

	return (tlv_add(msg, opt_type, sizeof(u8), &u8));
}

int
tlv_add_u16(struct dynar *msg, enum tlv_opt_type opt_type, uint16_t u16)
{
	uint16_t nu16;

	nu16 = htons(u16);

	return (tlv_add(msg, opt_type, sizeof(nu16), &nu16));
}

int
tlv_add_u64(struct dynar *msg, enum tlv_opt_type opt_type, uint64_t u64)
{
	uint64_t nu64;

	nu64 = htobe64(u64);

	return (tlv_add(msg, opt_type, sizeof(nu64), &nu64));
}

int
tlv_add_string(struct dynar *msg, enum tlv_opt_type opt_type, const char *str)
{

	return (tlv_add(msg, opt_type, strlen(str), str));
}

int
tlv_add_msg_seq_number(struct dynar *msg, uint32_t msg_seq_number)
{

	return (tlv_add_u32(msg, TLV_OPT_MSG_SEQ_NUMBER, msg_seq_number));
}

int
tlv_add_cluster_name(struct dynar *msg, const char *cluster_name)
{

	return (tlv_add_string(msg, TLV_OPT_CLUSTER_NAME, cluster_name));
}

int
tlv_add_tls_supported(struct dynar *msg, enum tlv_tls_supported tls_supported)
{

	return (tlv_add_u8(msg, TLV_OPT_TLS_SUPPORTED, tls_supported));
}

int
tlv_add_tls_client_cert_required(struct dynar *msg, int tls_client_cert_required)
{

	return (tlv_add_u8(msg, TLV_OPT_TLS_CLIENT_CERT_REQUIRED, tls_client_cert_required));
}

int
tlv_add_u16_array(struct dynar *msg, enum tlv_opt_type opt_type, const uint16_t *array,
    size_t array_size)
{
	size_t i;
	uint16_t *nu16a;
	uint16_t opt_len;
	int res;

	nu16a = malloc(sizeof(uint16_t) * array_size);
	if (nu16a == NULL) {
		return (-1);
	}

	for (i = 0; i < array_size; i++) {
		nu16a[i] = htons(array[i]);
	}

	opt_len = sizeof(uint16_t) * array_size;

	res = tlv_add(msg, opt_type, opt_len, nu16a);

	free(nu16a);

	return (res);
}

int
tlv_add_supported_options(struct dynar *msg, const enum tlv_opt_type *supported_options,
    size_t no_supported_options)
{
	uint16_t *u16a;
	size_t i;
	int res;

	u16a = malloc(sizeof(*u16a) * no_supported_options);
	if (u16a == NULL) {
		return (-1);
	}

	for (i = 0; i < no_supported_options; i++) {
		u16a[i] = (uint16_t)supported_options[i];
	}

	res = (tlv_add_u16_array(msg, TLV_OPT_SUPPORTED_OPTIONS, u16a, no_supported_options));

	free(u16a);

	return (res);
}

int
tlv_add_supported_decision_algorithms(struct dynar *msg,
    const enum tlv_decision_algorithm_type *supported_algorithms, size_t no_supported_algorithms)
{
	uint16_t *u16a;
	size_t i;
	int res;

	u16a = malloc(sizeof(*u16a) * no_supported_algorithms);
	if (u16a == NULL) {
		return (-1);
	}

	for (i = 0; i < no_supported_algorithms; i++) {
		u16a[i] = (uint16_t)supported_algorithms[i];
	}

	res = (tlv_add_u16_array(msg, TLV_OPT_SUPPORTED_DECISION_ALGORITHMS, u16a,
	    no_supported_algorithms));

	free(u16a);

	return (res);
}

int
tlv_add_reply_error_code(struct dynar *msg, enum tlv_reply_error_code error_code)
{

	return (tlv_add_u16(msg, TLV_OPT_REPLY_ERROR_CODE, (uint16_t)error_code));
}

int
tlv_add_server_maximum_request_size(struct dynar *msg, size_t server_maximum_request_size)
{

	return (tlv_add_u32(msg, TLV_OPT_SERVER_MAXIMUM_REQUEST_SIZE, server_maximum_request_size));
}

int
tlv_add_server_maximum_reply_size(struct dynar *msg, size_t server_maximum_reply_size)
{

	return (tlv_add_u32(msg, TLV_OPT_SERVER_MAXIMUM_REPLY_SIZE, server_maximum_reply_size));
}

int
tlv_add_node_id(struct dynar *msg, uint32_t node_id)
{

	return (tlv_add_u32(msg, TLV_OPT_NODE_ID, node_id));
}

int
tlv_add_decision_algorithm(struct dynar *msg, enum tlv_decision_algorithm_type decision_algorithm)
{

	return (tlv_add_u16(msg, TLV_OPT_DECISION_ALGORITHM, (uint16_t)decision_algorithm));
}

int
tlv_add_heartbeat_interval(struct dynar *msg, uint32_t heartbeat_interval)
{

	return (tlv_add_u32(msg, TLV_OPT_HEARTBEAT_INTERVAL, heartbeat_interval));
}

int
tlv_add_ring_id(struct dynar *msg, const struct tlv_ring_id *ring_id)
{
	uint64_t nu64;
	uint32_t nu32;
	char tmp_buf[12];

	nu32 = htonl(ring_id->node_id);
	nu64 = htobe64(ring_id->seq);

	memcpy(tmp_buf, &nu32, sizeof(nu32));
	memcpy(tmp_buf + sizeof(nu32), &nu64, sizeof(nu64));

	return (tlv_add(msg, TLV_OPT_RING_ID, sizeof(tmp_buf), tmp_buf));
}

int
tlv_add_tie_breaker(struct dynar *msg, const struct tlv_tie_breaker *tie_breaker)
{
	uint32_t nu32;
	uint8_t u8;
	char tmp_buf[5];

	u8 = tie_breaker->mode;
	nu32 = (tie_breaker->mode == TLV_TIE_BREAKER_MODE_NODE_ID ?
	    htonl(tie_breaker->node_id) : 0);

	memcpy(tmp_buf, &u8, sizeof(u8));
	memcpy(tmp_buf + sizeof(u8), &nu32, sizeof(nu32));

	return (tlv_add(msg, TLV_OPT_TIE_BREAKER, sizeof(tmp_buf), tmp_buf));
}

int
tlv_add_config_version(struct dynar *msg, uint64_t config_version)
{

	return (tlv_add_u64(msg, TLV_OPT_CONFIG_VERSION, config_version));
}

int
tlv_add_data_center_id(struct dynar *msg, uint32_t data_center_id)
{

	return (tlv_add_u32(msg, TLV_OPT_DATA_CENTER_ID, data_center_id));
}

int
tlv_add_node_state(struct dynar *msg, enum tlv_node_state node_state)
{

	return (tlv_add_u8(msg, TLV_OPT_NODE_STATE, node_state));
}

int
tlv_add_node_info(struct dynar *msg, const struct tlv_node_info *node_info)
{
	struct dynar opt_value;
	int res;

	res = 0;
	/*
	 * Create sub message,
	 */
	dynar_init(&opt_value, 1024);
	if ((res = tlv_add_node_id(&opt_value, node_info->node_id)) != 0) {
		goto exit_dynar_destroy;
	}

	if (node_info->data_center_id != 0) {
		if ((res = tlv_add_data_center_id(&opt_value, node_info->data_center_id)) != 0) {
			goto exit_dynar_destroy;
		}
	}

	if (node_info->node_state != TLV_NODE_STATE_NOT_SET) {
		if ((res = tlv_add_node_state(&opt_value, node_info->node_state)) != 0) {
			goto exit_dynar_destroy;
		}
	}

	res = tlv_add(msg, TLV_OPT_NODE_INFO, dynar_size(&opt_value), dynar_data(&opt_value));
	if (res != 0) {
		goto exit_dynar_destroy;
	}


exit_dynar_destroy:
	dynar_destroy(&opt_value);

	return (res);
}

int
tlv_add_node_list_type(struct dynar *msg, enum tlv_node_list_type node_list_type)
{

	return (tlv_add_u8(msg, TLV_OPT_NODE_LIST_TYPE, node_list_type));
}

int
tlv_add_vote(struct dynar *msg, enum tlv_vote vote)
{

	return (tlv_add_u8(msg, TLV_OPT_VOTE, vote));
}

int
tlv_add_quorate(struct dynar *msg, enum tlv_quorate quorate)
{

	return (tlv_add_u8(msg, TLV_OPT_QUORATE, quorate));
}

int
tlv_add_heuristics(struct dynar *msg, enum tlv_heuristics heuristics)
{

	if (heuristics == TLV_HEURISTICS_UNDEFINED) {
		return (-1);
	}

	return (tlv_add_u8(msg, TLV_OPT_HEURISTICS, heuristics));
}

void
tlv_iter_init_str(const char *msg, size_t msg_len, size_t msg_header_len,
    struct tlv_iterator *tlv_iter)
{

	tlv_iter->msg = msg;
	tlv_iter->msg_len = msg_len;
	tlv_iter->current_pos = 0;
	tlv_iter->msg_header_len = msg_header_len;
	tlv_iter->iter_next_called = 0;
}

void
tlv_iter_init(const struct dynar *msg, size_t msg_header_len, struct tlv_iterator *tlv_iter)
{

	tlv_iter_init_str(dynar_data(msg), dynar_size(msg), msg_header_len, tlv_iter);
}

enum tlv_opt_type
tlv_iter_get_type(const struct tlv_iterator *tlv_iter)
{
	uint16_t ntype;
	uint16_t type;

	memcpy(&ntype, tlv_iter->msg + tlv_iter->current_pos, sizeof(ntype));
	type = ntohs(ntype);

	return (type);
}

uint16_t
tlv_iter_get_len(const struct tlv_iterator *tlv_iter)
{
	uint16_t nlen;
	uint16_t len;

	memcpy(&nlen, tlv_iter->msg + tlv_iter->current_pos + TLV_TYPE_LENGTH, sizeof(nlen));
	len = ntohs(nlen);

	return (len);
}

const char *
tlv_iter_get_data(const struct tlv_iterator *tlv_iter)
{

	return (tlv_iter->msg + tlv_iter->current_pos + TLV_TYPE_LENGTH + TLV_LENGTH_LENGTH);
}

int
tlv_iter_next(struct tlv_iterator *tlv_iter)
{
	uint16_t len;

	if (tlv_iter->iter_next_called == 0) {
		tlv_iter->iter_next_called = 1;
		tlv_iter->current_pos = tlv_iter->msg_header_len;

		goto check_tlv_validity;
	}

	len = tlv_iter_get_len(tlv_iter);

	if (tlv_iter->current_pos + TLV_TYPE_LENGTH + TLV_LENGTH_LENGTH + len >=
	    tlv_iter->msg_len) {
		return (0);
	}

	tlv_iter->current_pos += TLV_TYPE_LENGTH + TLV_LENGTH_LENGTH + len;

check_tlv_validity:
	/*
	 * Check if tlv is valid = is not larger than whole message
	 */
	len = tlv_iter_get_len(tlv_iter);

	if (tlv_iter->current_pos + TLV_TYPE_LENGTH + TLV_LENGTH_LENGTH + len > tlv_iter->msg_len) {
		return (-1);
	}

	return (1);
}

int
tlv_iter_decode_u32(struct tlv_iterator *tlv_iter, uint32_t *res)
{
	const char *opt_data;
	uint16_t opt_len;
	uint32_t nu32;

	opt_len = tlv_iter_get_len(tlv_iter);
	opt_data = tlv_iter_get_data(tlv_iter);

	if (opt_len != sizeof(nu32)) {
		return (-1);
	}

	memcpy(&nu32, opt_data, sizeof(nu32));
	*res = ntohl(nu32);

	return (0);
}

int
tlv_iter_decode_u8(struct tlv_iterator *tlv_iter, uint8_t *res)
{
	const char *opt_data;
	uint16_t opt_len;

	opt_len = tlv_iter_get_len(tlv_iter);
	opt_data = tlv_iter_get_data(tlv_iter);

	if (opt_len != sizeof(*res)) {
		return (-1);
	}

	memcpy(res, opt_data, sizeof(*res));

	return (0);
}

int
tlv_iter_decode_client_cert_required(struct tlv_iterator *tlv_iter, uint8_t *client_cert_required)
{

	return (tlv_iter_decode_u8(tlv_iter, client_cert_required));
}

int
tlv_iter_decode_str(struct tlv_iterator *tlv_iter, char **str, size_t *str_len)
{
	const char *opt_data;
	uint16_t opt_len;
	char *tmp_str;

	opt_len = tlv_iter_get_len(tlv_iter);
	opt_data = tlv_iter_get_data(tlv_iter);

	tmp_str = malloc(opt_len + 1);
	if (tmp_str == NULL) {
		return (-1);
	}

	memcpy(tmp_str, opt_data, opt_len);
	tmp_str[opt_len] = '\0';

	*str = tmp_str;
	*str_len = opt_len;

	return (0);
}

int
tlv_iter_decode_u16_array(struct tlv_iterator *tlv_iter, uint16_t **u16a, size_t *no_items)
{
	uint16_t opt_len;
	uint16_t *u16a_res;
	size_t i;

	opt_len = tlv_iter_get_len(tlv_iter);

	if (opt_len % sizeof(uint16_t) != 0) {
		return (-1);
	}

	*no_items = opt_len / sizeof(uint16_t);

	u16a_res = malloc(sizeof(uint16_t) * *no_items);
	if (u16a_res == NULL) {
		return (-2);
	}

	memcpy(u16a_res, tlv_iter_get_data(tlv_iter), opt_len);

	for (i = 0; i < *no_items; i++) {
		u16a_res[i] = ntohs(u16a_res[i]);
	}

	*u16a = u16a_res;

	return (0);
}

int
tlv_iter_decode_supported_options(struct tlv_iterator *tlv_iter,
    enum tlv_opt_type **supported_options, size_t *no_supported_options)
{
	uint16_t *u16a;
	enum tlv_opt_type *tlv_opt_array;
	size_t i;
	int res;

	res = tlv_iter_decode_u16_array(tlv_iter, &u16a, no_supported_options);
	if (res != 0) {
		return (res);
	}

	tlv_opt_array = malloc(sizeof(enum tlv_opt_type) * *no_supported_options);
	if (tlv_opt_array == NULL) {
		free(u16a);
		return (-2);
	}

	for (i = 0; i < *no_supported_options; i++) {
		tlv_opt_array[i] = (enum tlv_opt_type)u16a[i];
	}

	free(u16a);

	*supported_options = tlv_opt_array;

	return (0);
}

int
tlv_iter_decode_supported_decision_algorithms(struct tlv_iterator *tlv_iter,
    enum tlv_decision_algorithm_type **supported_decision_algorithms,
    size_t *no_supported_decision_algorithms)
{
	uint16_t *u16a;
	enum tlv_decision_algorithm_type *tlv_decision_algorithm_type_array;
	size_t i;
	int res;

	res = tlv_iter_decode_u16_array(tlv_iter, &u16a, no_supported_decision_algorithms);
	if (res != 0) {
		return (res);
	}

	tlv_decision_algorithm_type_array = malloc(
	    sizeof(enum tlv_decision_algorithm_type) * *no_supported_decision_algorithms);

	if (tlv_decision_algorithm_type_array == NULL) {
		free(u16a);
		return (-2);
	}

	for (i = 0; i < *no_supported_decision_algorithms; i++) {
		tlv_decision_algorithm_type_array[i] = (enum tlv_decision_algorithm_type)u16a[i];
	}

	free(u16a);

	*supported_decision_algorithms = tlv_decision_algorithm_type_array;

	return (0);
}

int
tlv_iter_decode_u16(struct tlv_iterator *tlv_iter, uint16_t *u16)
{
	const char *opt_data;
	uint16_t opt_len;
	uint16_t nu16;

	opt_len = tlv_iter_get_len(tlv_iter);
	opt_data = tlv_iter_get_data(tlv_iter);

	if (opt_len != sizeof(nu16)) {
		return (-1);
	}

	memcpy(&nu16, opt_data, sizeof(nu16));
	*u16 = ntohs(nu16);

	return (0);
}

int
tlv_iter_decode_u64(struct tlv_iterator *tlv_iter, uint64_t *u64)
{
	const char *opt_data;
	uint64_t opt_len;
	uint64_t nu64;

	opt_len = tlv_iter_get_len(tlv_iter);
	opt_data = tlv_iter_get_data(tlv_iter);

	if (opt_len != sizeof(nu64)) {
		return (-1);
	}

	memcpy(&nu64, opt_data, sizeof(nu64));
	*u64 = be64toh(nu64);

	return (0);
}

int
tlv_iter_decode_reply_error_code(struct tlv_iterator *tlv_iter,
    enum tlv_reply_error_code *reply_error_code)
{

	return (tlv_iter_decode_u16(tlv_iter, (uint16_t *)reply_error_code));
}

int
tlv_iter_decode_tls_supported(struct tlv_iterator *tlv_iter, enum tlv_tls_supported *tls_supported)
{
	uint8_t u8;
	enum tlv_tls_supported tmp_tls_supported;

	if (tlv_iter_decode_u8(tlv_iter, &u8) != 0) {
		return (-1);
	}

	tmp_tls_supported = u8;

	if (tmp_tls_supported != TLV_TLS_UNSUPPORTED &&
	    tmp_tls_supported != TLV_TLS_SUPPORTED &&
	    tmp_tls_supported != TLV_TLS_REQUIRED) {
		return (-4);
	}

	*tls_supported = tmp_tls_supported;

	return (0);
}

int
tlv_iter_decode_decision_algorithm(struct tlv_iterator *tlv_iter,
    enum tlv_decision_algorithm_type *decision_algorithm)
{
	uint16_t u16;

	if (tlv_iter_decode_u16(tlv_iter, &u16) != 0) {
		return (-1);
	}

	*decision_algorithm = (enum tlv_decision_algorithm_type)u16;

	return (0);
}

int
tlv_iter_decode_ring_id(struct tlv_iterator *tlv_iter, struct tlv_ring_id *ring_id)
{
	const char *opt_data;
	uint16_t opt_len;
	uint32_t nu32;
	uint64_t nu64;
	char tmp_buf[12];

	opt_len = tlv_iter_get_len(tlv_iter);
	opt_data = tlv_iter_get_data(tlv_iter);

	if (opt_len != sizeof(tmp_buf)) {
		return (-1);
	}

	memcpy(&nu32, opt_data, sizeof(nu32));
	memcpy(&nu64, opt_data + sizeof(nu32), sizeof(nu64));

	ring_id->node_id = ntohl(nu32);
	ring_id->seq = be64toh(nu64);

	return (0);
}

int
tlv_iter_decode_tie_breaker(struct tlv_iterator *tlv_iter, struct tlv_tie_breaker *tie_breaker)
{
	const char *opt_data;
	uint16_t opt_len;
	uint32_t nu32;
	uint8_t u8;
	enum tlv_tie_breaker_mode tie_breaker_mode;
	char tmp_buf[5];

	opt_len = tlv_iter_get_len(tlv_iter);
	opt_data = tlv_iter_get_data(tlv_iter);

	if (opt_len != sizeof(tmp_buf)) {
		return (-1);
	}

	memcpy(&u8, opt_data, sizeof(u8));
	tie_breaker_mode = u8;

	if (tie_breaker_mode != TLV_TIE_BREAKER_MODE_LOWEST &&
	    tie_breaker_mode != TLV_TIE_BREAKER_MODE_HIGHEST &&
	    tie_breaker_mode != TLV_TIE_BREAKER_MODE_NODE_ID) {
		return (-4);
	}

	memcpy(&nu32, opt_data + sizeof(u8), sizeof(nu32));

	tie_breaker->mode = tie_breaker_mode;
	tie_breaker->node_id = (tie_breaker->mode == TLV_TIE_BREAKER_MODE_NODE_ID ?
	    ntohl(nu32) : 0);

	return (0);
}

int
tlv_iter_decode_node_state(struct tlv_iterator *tlv_iter, enum tlv_node_state *node_state)
{
	uint8_t u8;
	enum tlv_node_state tmp_node_state;

	if (tlv_iter_decode_u8(tlv_iter, &u8) != 0) {
		return (-1);
	}

	tmp_node_state = u8;

	if (tmp_node_state != TLV_NODE_STATE_MEMBER &&
	    tmp_node_state != TLV_NODE_STATE_DEAD &&
	    tmp_node_state != TLV_NODE_STATE_LEAVING) {
		return (-4);
	}

	*node_state = tmp_node_state;

	return (0);
}

int
tlv_iter_decode_node_info(struct tlv_iterator *tlv_iter, struct tlv_node_info *node_info)
{
	struct tlv_iterator data_tlv_iter;
	int iter_res;
	int res;
	enum tlv_opt_type opt_type;
	struct tlv_node_info tmp_node_info;

	memset(&tmp_node_info, 0, sizeof(tmp_node_info));

	tlv_iter_init_str(tlv_iter_get_data(tlv_iter), tlv_iter_get_len(tlv_iter), 0,
	    &data_tlv_iter);

	while ((iter_res = tlv_iter_next(&data_tlv_iter)) > 0) {
		opt_type = tlv_iter_get_type(&data_tlv_iter);

		switch (opt_type) {
		case TLV_OPT_NODE_ID:
			if ((res = tlv_iter_decode_u32(&data_tlv_iter,
			    &tmp_node_info.node_id)) != 0) {
				return (res);
			}
			break;
		case TLV_OPT_DATA_CENTER_ID:
			if ((res = tlv_iter_decode_u32(&data_tlv_iter,
			    &tmp_node_info.data_center_id)) != 0) {
				return (res);
			}
			break;
		case TLV_OPT_NODE_STATE:
			if ((res = tlv_iter_decode_node_state(&data_tlv_iter,
			    &tmp_node_info.node_state)) != 0) {
				return (res);
			}
			break;
		default:
			/*
			 * Other options are not processed
			 */
			break;
		}
	}

	if (iter_res != 0) {
		return (-3);
	}

	if (tmp_node_info.node_id == 0) {
		return (-4);
	}

	memcpy(node_info, &tmp_node_info, sizeof(tmp_node_info));

	return (0);
}

int
tlv_iter_decode_node_list_type(struct tlv_iterator *tlv_iter,
    enum tlv_node_list_type *node_list_type)
{
	uint8_t u8;
	enum tlv_node_list_type tmp_node_list_type;

	if (tlv_iter_decode_u8(tlv_iter, &u8) != 0) {
		return (-1);
	}

	tmp_node_list_type = u8;

	if (tmp_node_list_type != TLV_NODE_LIST_TYPE_INITIAL_CONFIG &&
	    tmp_node_list_type != TLV_NODE_LIST_TYPE_CHANGED_CONFIG &&
	    tmp_node_list_type != TLV_NODE_LIST_TYPE_MEMBERSHIP &&
	    tmp_node_list_type != TLV_NODE_LIST_TYPE_QUORUM) {
		return (-4);
	}

	*node_list_type = tmp_node_list_type;

	return (0);
}

int
tlv_iter_decode_vote(struct tlv_iterator *tlv_iter, enum tlv_vote *vote)
{
	uint8_t u8;
	enum tlv_vote tmp_vote;

	if (tlv_iter_decode_u8(tlv_iter, &u8) != 0) {
		return (-1);
	}

	tmp_vote = u8;

	if (tmp_vote != TLV_VOTE_ACK &&
	    tmp_vote != TLV_VOTE_NACK &&
	    tmp_vote != TLV_VOTE_ASK_LATER &&
	    tmp_vote != TLV_VOTE_WAIT_FOR_REPLY &&
	    tmp_vote != TLV_VOTE_NO_CHANGE) {
		return (-4);
	}

	*vote = tmp_vote;

	return (0);
}

int
tlv_iter_decode_quorate(struct tlv_iterator *tlv_iter, enum tlv_quorate *quorate)
{
	uint8_t u8;
	enum tlv_quorate tmp_quorate;

	if (tlv_iter_decode_u8(tlv_iter, &u8) != 0) {
		return (-1);
	}

	tmp_quorate = u8;

	if (tmp_quorate != TLV_QUORATE_QUORATE &&
	    tmp_quorate != TLV_QUORATE_INQUORATE) {
		return (-4);
	}

	*quorate = tmp_quorate;

	return (0);
}

int
tlv_iter_decode_heuristics(struct tlv_iterator *tlv_iter, enum tlv_heuristics *heuristics)
{
	uint8_t u8;
	enum tlv_heuristics tmp_heuristics;

	if (tlv_iter_decode_u8(tlv_iter, &u8) != 0) {
		return (-1);
	}

	tmp_heuristics = u8;

	if (tmp_heuristics != TLV_HEURISTICS_PASS &&
	    tmp_heuristics != TLV_HEURISTICS_FAIL) {
		return (-4);
	}

	*heuristics = tmp_heuristics;

	return (0);
}

void
tlv_get_supported_options(enum tlv_opt_type **supported_options, size_t *no_supported_options)
{

	*supported_options = tlv_static_supported_options;
	*no_supported_options = TLV_STATIC_SUPPORTED_OPTIONS_SIZE;
}

int
tlv_ring_id_eq(const struct tlv_ring_id *rid1, const struct tlv_ring_id *rid2)
{

	return (rid1->node_id == rid2->node_id && rid1->seq == rid2->seq);
}

int
tlv_tie_breaker_eq(const struct tlv_tie_breaker *tb1, const struct tlv_tie_breaker *tb2)
{

	if (tb1->mode == tb2->mode && tb1->mode == TLV_TIE_BREAKER_MODE_NODE_ID) {
		return (tb1->node_id == tb2->node_id);
	}

	return (tb1->mode == tb2->mode);
}

const char *
tlv_vote_to_str(enum tlv_vote vote)
{

	switch (vote) {
	case TLV_VOTE_UNDEFINED: break;
	case TLV_VOTE_ACK: return ("ACK"); break;
	case TLV_VOTE_NACK: return ("NACK"); break;
	case TLV_VOTE_ASK_LATER: return ("Ask later"); break;
	case TLV_VOTE_WAIT_FOR_REPLY: return ("Wait for reply"); break;
	case TLV_VOTE_NO_CHANGE: return ("No change"); break;
	}

	return ("Unknown vote value");
}

const char *
tlv_node_state_to_str(enum tlv_node_state state)
{

	switch (state) {
	case TLV_NODE_STATE_NOT_SET: return ("not set"); break;
	case TLV_NODE_STATE_MEMBER: return ("member"); break;
	case TLV_NODE_STATE_DEAD: return ("dead"); break;
	case TLV_NODE_STATE_LEAVING: return ("leaving"); break;
	}

	return ("Unhandled node state");
}

const char *
tlv_tls_supported_to_str(enum tlv_tls_supported tls_supported)
{

	switch (tls_supported) {
	case TLV_TLS_UNSUPPORTED: return ("Unsupported"); break;
	case TLV_TLS_SUPPORTED: return ("Supported"); break;
	case TLV_TLS_REQUIRED: return ("Required"); break;
	}

	return ("Unhandled tls supported state");
}

const char *
tlv_decision_algorithm_type_to_str(enum tlv_decision_algorithm_type algorithm)
{

	switch (algorithm) {
	case TLV_DECISION_ALGORITHM_TYPE_TEST: return ("Test"); break;
	case TLV_DECISION_ALGORITHM_TYPE_FFSPLIT: return ("Fifty-Fifty split"); break;
	case TLV_DECISION_ALGORITHM_TYPE_2NODELMS: return ("2 Node LMS"); break;
	case TLV_DECISION_ALGORITHM_TYPE_LMS: return ("LMS"); break;
	}

	return ("Unknown algorithm");
}

const char *
tlv_heuristics_to_str(enum tlv_heuristics heuristics)
{

	switch (heuristics) {
	case TLV_HEURISTICS_UNDEFINED: return ("Undefined"); break;
	case TLV_HEURISTICS_PASS: return ("Pass"); break;
	case TLV_HEURISTICS_FAIL: return ("Fail"); break;
	}

	return ("Unknown heuristics type");
}

int
tlv_heuristics_cmp(enum tlv_heuristics h1, enum tlv_heuristics h2)
{
	int res;

	res = -2;

	switch (h1) {
	case TLV_HEURISTICS_UNDEFINED:
		switch (h2) {
		case TLV_HEURISTICS_UNDEFINED: res = 0; break;
		case TLV_HEURISTICS_PASS: res = -1; break;
		case TLV_HEURISTICS_FAIL: res = 1; break;
		}
		break;
	case TLV_HEURISTICS_PASS:
		switch (h2) {
		case TLV_HEURISTICS_UNDEFINED: res = 1; break;
		case TLV_HEURISTICS_PASS: res = 0; break;
		case TLV_HEURISTICS_FAIL: res = 1; break;
		}
		break;
	case TLV_HEURISTICS_FAIL:
		switch (h2) {
		case TLV_HEURISTICS_UNDEFINED: res = -1; break;
		case TLV_HEURISTICS_PASS: res = -1; break;
		case TLV_HEURISTICS_FAIL: res = 0; break;
		}
		break;
	}

	assert(res == -1 || res == 0 || res == 1);

	return (res);
}
