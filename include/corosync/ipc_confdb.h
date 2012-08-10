/*
 * Copyright (c) 2008-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
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
#ifndef IPC_CONFDB_H_DEFINED
#define IPC_CONFDB_H_DEFINED

#include <netinet/in.h>
#include <corosync/corotypes.h>
#include <corosync/mar_gen.h>

enum req_confdb_types {
	MESSAGE_REQ_CONFDB_OBJECT_CREATE = 0,
	MESSAGE_REQ_CONFDB_OBJECT_DESTROY = 1,
	MESSAGE_REQ_CONFDB_OBJECT_FIND = 2,
	MESSAGE_REQ_CONFDB_KEY_CREATE = 3,
	MESSAGE_REQ_CONFDB_KEY_GET = 4,
	MESSAGE_REQ_CONFDB_KEY_REPLACE = 5,
	MESSAGE_REQ_CONFDB_KEY_DELETE = 6,
	MESSAGE_REQ_CONFDB_OBJECT_ITER = 7,
	MESSAGE_REQ_CONFDB_OBJECT_PARENT_GET = 8,
	MESSAGE_REQ_CONFDB_KEY_ITER = 9,
	MESSAGE_REQ_CONFDB_TRACK_START = 10,
	MESSAGE_REQ_CONFDB_TRACK_STOP = 11,
	MESSAGE_REQ_CONFDB_WRITE = 12,
	MESSAGE_REQ_CONFDB_RELOAD = 13,
	MESSAGE_REQ_CONFDB_OBJECT_FIND_DESTROY = 14,
	MESSAGE_REQ_CONFDB_KEY_INCREMENT = 15,
	MESSAGE_REQ_CONFDB_KEY_DECREMENT = 16,
	MESSAGE_REQ_CONFDB_KEY_CREATE_TYPED = 17,
	MESSAGE_REQ_CONFDB_KEY_GET_TYPED = 18,
	MESSAGE_REQ_CONFDB_KEY_ITER_TYPED = 19,
	MESSAGE_REQ_CONFDB_OBJECT_NAME_GET = 20,
	MESSAGE_REQ_CONFDB_KEY_ITER_TYPED2 = 21,
	MESSAGE_REQ_CONFDB_KEY_REPLACE2 = 22,
	MESSAGE_REQ_CONFDB_KEY_GET_TYPED2 = 23,
	MESSAGE_REQ_CONFDB_KEY_CREATE_TYPED2 = 24,
};

enum res_confdb_types {
	MESSAGE_RES_CONFDB_OBJECT_CREATE = 0,
	MESSAGE_RES_CONFDB_OBJECT_DESTROY = 1,
	MESSAGE_RES_CONFDB_OBJECT_FIND = 2,
	MESSAGE_RES_CONFDB_KEY_CREATE = 3,
	MESSAGE_RES_CONFDB_KEY_GET = 4,
	MESSAGE_RES_CONFDB_KEY_REPLACE = 5,
	MESSAGE_RES_CONFDB_KEY_DELETE = 6,
	MESSAGE_RES_CONFDB_OBJECT_ITER = 7,
	MESSAGE_RES_CONFDB_OBJECT_PARENT_GET = 8,
	MESSAGE_RES_CONFDB_KEY_ITER = 9,
	MESSAGE_RES_CONFDB_TRACK_START = 10,
	MESSAGE_RES_CONFDB_TRACK_STOP = 11,
	MESSAGE_RES_CONFDB_KEY_CHANGE_CALLBACK = 12,
	MESSAGE_RES_CONFDB_OBJECT_CREATE_CALLBACK = 13,
	MESSAGE_RES_CONFDB_OBJECT_DESTROY_CALLBACK = 14,
	MESSAGE_RES_CONFDB_WRITE = 15,
	MESSAGE_RES_CONFDB_RELOAD = 16,
	MESSAGE_RES_CONFDB_OBJECT_FIND_DESTROY = 17,
	MESSAGE_RES_CONFDB_KEY_INCREMENT = 18,
	MESSAGE_RES_CONFDB_KEY_DECREMENT = 19,
	MESSAGE_RES_CONFDB_KEY_GET_TYPED = 20,
	MESSAGE_RES_CONFDB_KEY_ITER_TYPED = 21,
	MESSAGE_RES_CONFDB_RELOAD_CALLBACK = 22,
	MESSAGE_RES_CONFDB_OBJECT_NAME_GET = 23,
	MESSAGE_RES_CONFDB_KEY_ITER_TYPED2 = 24,
	MESSAGE_RES_CONFDB_KEY_GET_TYPED2 = 25,
	MESSAGE_RES_CONFDB_KEY_CHANGE_CALLBACK2 = 26,
};


struct req_lib_confdb_object_create {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
};

struct res_lib_confdb_object_create {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_object_destroy {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_object_parent_get {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
};

struct res_lib_confdb_object_parent_get {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_object_name_get {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
};

struct res_lib_confdb_object_name_get {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
};

struct req_lib_confdb_key_create {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};

struct req_lib_confdb_key_create_typed {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
	mar_int32_t type __attribute__((aligned(8)));
};

struct req_lib_confdb_key_create_typed2 {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_int32_t type __attribute__((aligned(8)));
	mar_int32_t value_length __attribute__((aligned(8)));
	mar_uint8_t value __attribute__((aligned(8))); /* First byte of value */
};

struct req_lib_confdb_key_delete {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};

struct req_lib_confdb_key_replace {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t old_value __attribute__((aligned(8)));
	mar_name_t new_value __attribute__((aligned(8)));
};

struct req_lib_confdb_object_find {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
	mar_uint64_t find_handle __attribute__((aligned(8)));
};

struct res_lib_confdb_object_find {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_uint64_t find_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_object_iter {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_uint64_t find_handle __attribute__((aligned(8)));
};

struct res_lib_confdb_object_iter {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_uint64_t find_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_key_iter {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_uint64_t next_entry __attribute__((aligned(8)));
};

struct res_lib_confdb_key_iter {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};
struct res_lib_confdb_key_iter_typed {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
	mar_int32_t type __attribute__((aligned(8)));
};

struct req_lib_confdb_key_get {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
};

struct req_lib_confdb_object_find_destroy {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t find_handle __attribute__((aligned(8)));
};

struct res_lib_confdb_key_get {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};
struct res_lib_confdb_key_get_typed {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
	mar_int32_t type __attribute__((aligned(8)));
};

struct res_lib_confdb_key_incdec {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t value __attribute__((aligned(8)));
};

struct res_lib_confdb_write {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t error __attribute__((aligned(8)));
};

struct req_lib_confdb_reload {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_int32_t flush __attribute__((aligned(8)));
};

struct res_lib_confdb_reload {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t error __attribute__((aligned(8)));
};

struct res_lib_confdb_key_change_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t change_type __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t key_value __attribute__((aligned(8)));
};

struct res_lib_confdb_key_change_callback2 {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t change_type __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_uint32_t key_value_length __attribute__((aligned(8)));
	mar_uint8_t key_value __attribute__((aligned(8)));   /* First byte of new value */
};

struct res_lib_confdb_object_create_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t name __attribute__((aligned(8)));
};

struct res_lib_confdb_object_destroy_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t parent_object_handle __attribute__((aligned(8)));
	mar_name_t name __attribute__((aligned(8)));
};

struct res_lib_confdb_reload_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t type __attribute__((aligned(8)));
};

struct req_lib_confdb_object_track_start {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_uint32_t flags __attribute__((aligned(8)));
};

struct res_lib_confdb_key_get_typed2 {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_int32_t type __attribute__((aligned(8)));
	mar_uint32_t value_length __attribute__((aligned(8)));
	mar_uint8_t value __attribute__((aligned(8)));
	// Actual value follows this
};

struct res_lib_confdb_key_iter_typed2 {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_int32_t type __attribute__((aligned(8)));
	mar_int32_t value_length __attribute__((aligned(8)));
	mar_uint8_t value __attribute__((aligned(8))); /* First byte of value */
};

struct req_lib_confdb_key_replace2 {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_int32_t new_value_length __attribute__((aligned(8)));
	mar_uint8_t new_value __attribute__((aligned(8)));  /* First byte of new value */
	/* Oddly objdb doesn't use the old value, so we don't bother sending it */
};

#endif /* IPC_CONFDB_H_DEFINED */
