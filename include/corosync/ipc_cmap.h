/*
 * Copyright (c) 2011 Red Hat, Inc.
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

#ifndef IPC_CMAP_H_DEFINED
#define IPC_CMAP_H_DEFINED

#include <netinet/in.h>
#include <corosync/corotypes.h>
#include <corosync/mar_gen.h>

/**
 * @brief The req_cmap_types enum
 */
enum req_cmap_types {
	MESSAGE_REQ_CMAP_SET = 0,
	MESSAGE_REQ_CMAP_DELETE = 1,
	MESSAGE_REQ_CMAP_GET = 2,
	MESSAGE_REQ_CMAP_ADJUST_INT = 3,
	MESSAGE_REQ_CMAP_ITER_INIT = 4,
	MESSAGE_REQ_CMAP_ITER_NEXT = 5,
	MESSAGE_REQ_CMAP_ITER_FINALIZE = 6,
	MESSAGE_REQ_CMAP_TRACK_ADD = 7,
	MESSAGE_REQ_CMAP_TRACK_DELETE = 8,
};

/**
 * @brief The res_cmap_types enum
 */
enum res_cmap_types {
	MESSAGE_RES_CMAP_SET = 0,
	MESSAGE_RES_CMAP_DELETE = 1,
	MESSAGE_RES_CMAP_GET = 2,
	MESSAGE_RES_CMAP_ADJUST_INT = 3,
	MESSAGE_RES_CMAP_ITER_INIT = 4,
	MESSAGE_RES_CMAP_ITER_NEXT = 5,
	MESSAGE_RES_CMAP_ITER_FINALIZE = 6,
	MESSAGE_RES_CMAP_TRACK_ADD = 7,
	MESSAGE_RES_CMAP_TRACK_DELETE = 8,
	MESSAGE_RES_CMAP_NOTIFY_CALLBACK = 9,
};

/**
 * @brief The req_lib_cmap_set struct
 */
struct req_lib_cmap_set {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_size_t value_len __attribute__((aligned(8)));
	mar_uint8_t type __attribute__((aligned(8)));
	mar_uint8_t value[] __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_set struct
 */
struct res_lib_cmap_set {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_delete struct
 */
struct req_lib_cmap_delete {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_delete struct
 */
struct res_lib_cmap_delete {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_get struct
 */
struct req_lib_cmap_get {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_size_t value_len __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_get struct
 */
struct res_lib_cmap_get {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_size_t value_len __attribute__((aligned(8)));
	mar_uint8_t type __attribute__((aligned(8)));
	mar_uint8_t value[] __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_adjust_int struct
 */
struct req_lib_cmap_adjust_int {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_int32_t step __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_adjust_int struct
 */
struct res_lib_cmap_adjust_int {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_iter_init struct
 */
struct req_lib_cmap_iter_init {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_name_t prefix __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_iter_init struct
 */
struct res_lib_cmap_iter_init {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint64_t iter_handle __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_iter_next struct
 */
struct req_lib_cmap_iter_next {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_uint64_t iter_handle __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_iter_next struct
 */
struct res_lib_cmap_iter_next {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_size_t value_len __attribute__((aligned(8)));
	mar_uint8_t type __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_iter_finalize struct
 */
struct req_lib_cmap_iter_finalize {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_uint64_t iter_handle __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_iter_finalize struct
 */
struct res_lib_cmap_iter_finalize {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_track_add struct
 */
struct req_lib_cmap_track_add {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_int32_t track_type __attribute__((aligned(8)));
	mar_uint64_t track_inst_handle __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_track_add struct
 */
struct res_lib_cmap_track_add {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint64_t track_handle __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cmap_track_delete struct
 */
struct req_lib_cmap_track_delete {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	mar_uint64_t track_handle __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_track_delete struct
 */
struct res_lib_cmap_track_delete {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint64_t track_inst_handle __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cmap_notify_callback struct
 */
struct res_lib_cmap_notify_callback {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint64_t track_inst_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_int32_t event __attribute__((aligned(8)));
	mar_uint8_t new_value_type __attribute__((aligned(8)));
	mar_uint8_t old_value_type __attribute__((aligned(8)));
	mar_size_t new_value_len __attribute__((aligned(8)));
	mar_size_t old_value_len __attribute__((aligned(8)));
	/*
	 * After old_vale_len, there are two items with length of new_value_len
	 * and old_value_len, only first (as a pointer) is defined
	 *
	 * mar_uint8_t *new_value;
	 * mar_uint8_t *old_value;
	 */
	mar_uint8_t new_value[];
};

#endif /* IPC_CMAP_H_DEFINED */
