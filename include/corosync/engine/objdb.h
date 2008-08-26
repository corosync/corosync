/*
 * Copyright (c) 2006 MontaVista Software, Inc.
 * Copyright (c) 2007-2008 Red Hat, Inc.
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

#define OBJECT_PARENT_HANDLE 0

#include <stdio.h>

typedef enum {
	OBJECT_TRACK_DEPTH_ONE,
	OBJECT_TRACK_DEPTH_RECURSIVE
} object_track_depth_t;

typedef enum {
	OBJECT_KEY_CREATED,
	OBJECT_KEY_REPLACED,
	OBJECT_KEY_DELETED
} object_change_type_t;

typedef void (*object_key_change_notify_fn_t)(object_change_type_t change_type,
											  unsigned int parent_object_handle,
											  unsigned int object_handle,
											  void *object_name_pt, int object_name_len,
											  void *key_name_pt, int key_len,
											  void *key_value_pt, int key_value_len,
											  void *priv_data_pt);

typedef void (*object_create_notify_fn_t) (unsigned int parent_object_handle,
										   unsigned int object_handle,
										   void *name_pt, int name_len,
										   void *priv_data_pt);

typedef void (*object_destroy_notify_fn_t) (unsigned int parent_object_handle,
											void *name_pt, int name_len,
											void *priv_data_pt);

struct object_valid {
	char *object_name;
	int object_len;
};

struct object_key_valid {
	char *key_name;
	int key_len;
	int (*validate_callback) (void *key, int key_len, void *value, int value_len);
};

struct objdb_iface_ver0 {
	int (*objdb_init) (void);

	int (*object_create) (
		unsigned int parent_object_handle,
		unsigned int *object_handle,
		void *object_name,
		unsigned int object_name_len);

	int (*object_priv_set) (
		unsigned int object_handle,
		void *priv);

	int (*object_key_create) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void *value,
		int value_len);

	int (*object_destroy) (
		unsigned int object_handle);

	int (*object_valid_set) (
		unsigned int object_handle,
		struct object_valid *object_valid_list,
		unsigned int object_valid_list_entries);

	int (*object_key_valid_set) (
		unsigned int object_handle,
		struct object_key_valid *object_key_valid_list,
		unsigned int object_key_valid_list_entries);

	int (*object_find_create) (
		unsigned int parent_object_handle,
		void *object_name,
		int object_name_len,
		unsigned int *object_find_handle);

	int (*object_find_next) (
		unsigned int object_find_handle,
		unsigned int *object_handle);

	int (*object_find_destroy) (
		unsigned int object_find_handle);

	int (*object_key_get) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void **value,
		int *value_len);

	int (*object_priv_get) (
		unsigned int jobject_handle,
		void **priv);

	int (*object_key_replace) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void *old_value,
		int old_value_len,
		void *new_value,
		int new_value_len);

	int (*object_key_delete) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void *value,
		int value_len);

	int (*object_iter_reset) (
		unsigned int parent_object_handle);

	int (*object_iter) (
		unsigned int parent_object_handle,
		void **object_name,
		int *name_len,
		unsigned int *object_handle);

	int (*object_key_iter_reset) (
		unsigned int object_handle);

	int (*object_key_iter) (
		unsigned int parent_object_handle,
		void **key_name,
		int *key_len,
		void **value,
		int *value_len);

	int (*object_parent_get) (
		unsigned int object_handle,
		unsigned int *parent_handle);

	int (*object_name_get) (
		unsigned int object_handle,
		char *object_name,
		int *object_name_len);

	int (*object_dump) (
		unsigned int object_handle,
		FILE *file);

	int (*object_key_iter_from) (
		unsigned int parent_object_handle,
		unsigned int start_pos,
		void **key_name,
		int *key_len,
		void **value,
		int *value_len);

	int (*object_track_start) (
		unsigned int object_handle,
		object_track_depth_t depth,
		object_key_change_notify_fn_t key_change_notify_fn,
		object_create_notify_fn_t object_create_notify_fn,
		object_destroy_notify_fn_t object_destroy_notify_fn,
		void * priv_data_pt);

	void (*object_track_stop) (
		object_key_change_notify_fn_t key_change_notify_fn,
		object_create_notify_fn_t object_create_notify_fn,
		object_destroy_notify_fn_t object_destroy_notify_fn,
		void * priv_data_pt);

	int (*object_write_config) (char **error_string);

	int (*object_reload_config) (
		int flush,
		char **error_string);
};

#endif /* OBJDB_H_DEFINED */
