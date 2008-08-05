/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfi@redhat.com)
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
#ifndef OPENAIS_CONFDB_H_DEFINED
#define OPENAIS_CONFDB_H_DEFINED

/**
 * @addtogroup confdb_openais
 *
 * @{
 */
typedef uint64_t confdb_handle_t;

#define OBJECT_PARENT_HANDLE 0

typedef enum {
	CONFDB_DISPATCH_ONE,
	CONFDB_DISPATCH_ALL,
	CONFDB_DISPATCH_BLOCKING
} confdb_dispatch_t;

typedef enum {
	CONFDB_OK = 1,
	CONFDB_ERR_LIBRARY = 2,
	CONFDB_ERR_TIMEOUT = 5,
	CONFDB_ERR_TRY_AGAIN = 6,
	CONFDB_ERR_INVALID_PARAM = 7,
	CONFDB_ERR_NO_MEMORY = 8,
	CONFDB_ERR_BAD_HANDLE = 9,
	CONFDB_ERR_ACCESS = 11,
	CONFDB_ERR_NOT_EXIST = 12,
	CONFDB_ERR_EXIST = 14,
	CONFDB_ERR_CONTEXT_NOT_FOUND = 17,
	CONFDB_ERR_NOT_SUPPORTED = 20,
	CONFDB_ERR_SECURITY = 29,
} confdb_error_t;


typedef void (*confdb_change_notify_fn_t) (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	unsigned int object_handle,
	void *object_name,
	int  object_name_len,
	void *key_name,
	int key_name_len,
	void *key_value,
	int key_value_len);

typedef struct {
	confdb_change_notify_fn_t confdb_change_notify_fn;
} confdb_callbacks_t;

/** @} */

/*
 * Create a new confdb connection
 */
confdb_error_t confdb_initialize (
	confdb_handle_t *handle,
	confdb_callbacks_t *callbacks);

/*
 * Close the confdb handle
 */
confdb_error_t confdb_finalize (
	confdb_handle_t handle);


/*
 * Write back the configuration
 */
confdb_error_t confdb_write (
	confdb_handle_t handle,
	char *error_text);

/*
 * Get a file descriptor on which to poll.  confdb_handle_t is NOT a
 * file descriptor and may not be used directly.
 */
confdb_error_t confdb_fd_get (
	confdb_handle_t handle,
	int *fd);

/*
 * Dispatch configuration changes
 */
confdb_error_t confdb_dispatch (
	confdb_handle_t handle,
	confdb_dispatch_t dispatch_types);


/*
 * Change notification
 */
confdb_error_t confdb_track_changes (
	confdb_handle_t handle,
	unsigned int object_handle,
	unsigned int flags);

confdb_error_t confdb_stop_track_changes (
	confdb_handle_t handle);

/*
 * Manipulate objects
 */
confdb_error_t confdb_object_create (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *object_name,
	int object_name_len,
	unsigned int *object_handle);

confdb_error_t confdb_object_destroy (
	confdb_handle_t handle,
	unsigned int object_handle);

confdb_error_t confdb_object_parent_get (
	confdb_handle_t handle,
	unsigned int object_handle,
	unsigned int *parent_object_handle);

/*
 * Manipulate keys
 */
confdb_error_t confdb_key_create (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *value,
	int value_len);

confdb_error_t confdb_key_delete (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *value,
	int value_len);

/*
 * Key queries
 */
confdb_error_t confdb_key_get (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *value,
	int *value_len);

confdb_error_t confdb_key_replace (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *old_value,
	int old_value_len,
	void *new_value,
	int new_value_len);

/*
 * Object queries
 * "find" loops through all objects of a given name and is also
 * a quick way of finding a specific object,
 * "iter" returns ech object in sequence.
 */
confdb_error_t confdb_object_find_start (
	confdb_handle_t handle,
	unsigned int parent_object_handle);

confdb_error_t confdb_object_find (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *object_name,
	int object_name_len,
	unsigned int *object_handle);

confdb_error_t confdb_object_iter_start (
	confdb_handle_t handle,
	unsigned int parent_object_handle);

confdb_error_t confdb_object_iter (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	unsigned int *object_handle,
	void *object_name,
	int *object_name_len);

/*
 * Key iterator
 */
confdb_error_t confdb_key_iter_start (
	confdb_handle_t handle,
	unsigned int object_handle);

confdb_error_t confdb_key_iter (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int *key_name_len,
	void *value,
	int *value_len);


#endif /* OPENAIS_CONFDB_H_DEFINED */
