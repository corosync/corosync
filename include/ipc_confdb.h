/*
 * Copyright (c) 2008 Red Hat, Inc.
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
#include "saAis.h"
#include "ipc_gen.h"

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
	MESSAGE_REQ_CONFDB_WRITE = 12
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
	MESSAGE_RES_CONFDB_CHANGE_CALLBACK = 12,
	MESSAGE_RES_CONFDB_WRITE = 13
};


struct req_lib_confdb_object_create {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t parent_object_handle __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
};

struct res_lib_confdb_object_create {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_object_destroy {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_object_parent_get {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
};

struct res_lib_confdb_object_parent_get {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_uint32_t parent_object_handle __attribute__((aligned(8)));
};


struct req_lib_confdb_key_create {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};

struct req_lib_confdb_key_delete {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};

struct req_lib_confdb_key_replace {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t old_value __attribute__((aligned(8)));
	mar_name_t new_value __attribute__((aligned(8)));
};

struct req_lib_confdb_object_find {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t parent_object_handle __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
	mar_uint32_t next_entry __attribute__((aligned(8)));
};

struct res_lib_confdb_object_find {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
	mar_uint32_t next_entry __attribute__((aligned(8)));
};

struct req_lib_confdb_object_iter {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t parent_object_handle __attribute__((aligned(8)));
	mar_uint32_t next_entry __attribute__((aligned(8)));
};

struct res_lib_confdb_object_iter {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
};

struct req_lib_confdb_key_iter {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t parent_object_handle __attribute__((aligned(8)));
	mar_uint32_t next_entry __attribute__((aligned(8)));
};

struct res_lib_confdb_key_iter {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};

struct req_lib_confdb_key_get {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t parent_object_handle __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
};

struct res_lib_confdb_key_get {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_name_t value __attribute__((aligned(8)));
};

struct res_lib_confdb_write {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_name_t error __attribute__((aligned(8)));
};

struct res_lib_confdb_change_callback {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_uint32_t parent_object_handle __attribute__((aligned(8)));
	mar_uint32_t object_handle __attribute__((aligned(8)));
	mar_name_t object_name __attribute__((aligned(8)));
	mar_name_t key_name __attribute__((aligned(8)));
	mar_name_t key_value __attribute__((aligned(8)));
};


#endif /* IPC_CONFDB_H_DEFINED */
