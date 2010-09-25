/*
 * Copyright (c) 2006 MontaVista Software, Inc.
 * Copyright (c) 2007-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

#ifndef OBJDB_H_DEFINED
#define OBJDB_H_DEFINED

#define OBJECT_PARENT_HANDLE 0xFFFFFFFF00000000ULL

#include <stdio.h>
#include <corosync/hdb.h>

typedef enum {
	OBJDB_VALUETYPE_INT16,
	OBJDB_VALUETYPE_UINT16,
	OBJDB_VALUETYPE_INT32,
	OBJDB_VALUETYPE_UINT32,
	OBJDB_VALUETYPE_INT64,
	OBJDB_VALUETYPE_UINT64,
	OBJDB_VALUETYPE_FLOAT,
	OBJDB_VALUETYPE_DOUBLE,
	OBJDB_VALUETYPE_STRING,
	OBJDB_VALUETYPE_ANY,
} objdb_value_types_t;

typedef enum {
	OBJECT_TRACK_DEPTH_ONE,
	OBJECT_TRACK_DEPTH_RECURSIVE
} object_track_depth_t;

typedef enum {
	OBJECT_KEY_CREATED,
	OBJECT_KEY_REPLACED,
	OBJECT_KEY_DELETED
} object_change_type_t;

typedef enum {
        OBJDB_RELOAD_NOTIFY_START,
        OBJDB_RELOAD_NOTIFY_END,
	OBJDB_RELOAD_NOTIFY_FAILED
} objdb_reload_notify_type_t;


typedef void (*object_key_change_notify_fn_t)(
	object_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t object_name_len,
	const void *key_name_pt, size_t key_len,
	const void *key_value_pt, size_t key_value_len,
	void *priv_data_pt);

typedef void (*object_create_notify_fn_t) (hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt);

typedef void (*object_destroy_notify_fn_t) (hdb_handle_t parent_object_handle,
	const void *name_pt,
	size_t name_len,
	void *priv_data_pt);

typedef void (*object_reload_notify_fn_t) (objdb_reload_notify_type_t, int flush,
	void *priv_data_pt);

struct object_valid {
	char *object_name;
	size_t object_len;
};

struct object_key_valid {
	char *key_name;
	size_t key_len;
	int (*validate_callback) (const void *key, size_t key_len,
				  const void *value, size_t value_len);
};

struct objdb_iface_ver0 {
	int (*objdb_init) (void);

	int (*object_create) (
		hdb_handle_t parent_object_handle,
		hdb_handle_t *object_handle,
		const void *object_name,
		size_t object_name_len);

	int (*object_priv_set) (
		hdb_handle_t object_handle,
		void *priv);

	int (*object_key_create) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		const void *value,
		size_t value_len);

	int (*object_destroy) (
		hdb_handle_t object_handle);

	int (*object_valid_set) (
		hdb_handle_t object_handle,
		struct object_valid *object_valid_list,
		size_t object_valid_list_entries);

	int (*object_key_valid_set) (
		hdb_handle_t object_handle,
		struct object_key_valid *object_key_valid_list,
		size_t object_key_valid_list_entries);

	int (*object_find_create) (
		hdb_handle_t parent_object_handle,
		const void *object_name,
		size_t object_name_len,
		hdb_handle_t *object_find_handle);

	int (*object_find_next) (
		hdb_handle_t object_find_handle,
		hdb_handle_t *object_handle);

	int (*object_find_destroy) (
		hdb_handle_t object_find_handle);

	int (*object_key_get) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		void **value,
		size_t *value_len);

	int (*object_priv_get) (
		hdb_handle_t jobject_handle,
		void **priv);

	int (*object_key_replace) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		const void *new_value,
		size_t new_value_len);

	int (*object_key_delete) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len);

	int (*object_iter_reset) (
		hdb_handle_t parent_object_handle);

	int (*object_iter) (
		hdb_handle_t parent_object_handle,
		void **object_name,
		size_t *name_len,
		hdb_handle_t *object_handle);

	int (*object_key_iter_reset) (
		hdb_handle_t object_handle);

	int (*object_key_iter) (
		hdb_handle_t parent_object_handle,
		void **key_name,
		size_t *key_len,
		void **value,
		size_t *value_len);

	int (*object_parent_get) (
		hdb_handle_t object_handle,
		hdb_handle_t *parent_handle);

	int (*object_name_get) (
		hdb_handle_t object_handle,
		char *object_name,
		size_t *object_name_len);

	int (*object_dump) (
		hdb_handle_t object_handle,
		FILE *file);

	int (*object_key_iter_from) (
		hdb_handle_t parent_object_handle,
		hdb_handle_t start_pos,
		void **key_name,
		size_t *key_len,
		void **value,
		size_t *value_len);

	int (*object_track_start) (
		hdb_handle_t object_handle,
		object_track_depth_t depth,
		object_key_change_notify_fn_t key_change_notify_fn,
		object_create_notify_fn_t object_create_notify_fn,
		object_destroy_notify_fn_t object_destroy_notify_fn,
		object_reload_notify_fn_t object_reload_notify_fn,
		void * priv_data_pt);

	void (*object_track_stop) (
		object_key_change_notify_fn_t key_change_notify_fn,
		object_create_notify_fn_t object_create_notify_fn,
		object_destroy_notify_fn_t object_destroy_notify_fn,
		object_reload_notify_fn_t object_reload_notify_fn,
		void * priv_data_pt);

	int (*object_write_config) (const char **error_string);

	int (*object_reload_config) (
		int flush,
		const char **error_string);

	int (*object_key_increment) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		unsigned int *value);

	int (*object_key_decrement) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		unsigned int *value);

	int (*object_key_create_typed) (
		hdb_handle_t object_handle,
		const char *key_name,
		const void *value,
		size_t value_len,
		objdb_value_types_t type);

	int (*object_key_get_typed) (
		hdb_handle_t object_handle,
		const char *key_name,
		void **value,
		size_t *value_len,
		objdb_value_types_t *type);

	int (*object_key_iter_typed) (
		hdb_handle_t parent_object_handle,
		char **key_name,
		void **value,
		size_t *value_len,
		objdb_value_types_t *type);
};

#endif /* OBJDB_H_DEFINED */
