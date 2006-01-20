/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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

#ifndef HDB_H_DEFINED
#define HDB_H_DEFINED

#include <stdlib.h>
#include <string.h>
#include <assert.h>

enum HDB_HANDLE_STATE {
	HDB_HANDLE_STATE_EMPTY,
	HDB_HANDLE_STATE_PENDINGREMOVAL,
	HDB_HANDLE_STATE_ACTIVE
};

struct hdb_handle {
	int state;
	void *instance;
	int ref_count;
};

struct hdb_handle_database {
	unsigned int handle_count;
	struct hdb_handle *handles;
	unsigned int iterator;
};

static inline int hdb_handle_create (
	struct hdb_handle_database *handle_database,
	int instance_size,
	unsigned int *handle_id_out)
{
	int handle;
	void *new_handles;
	int found = 0;
	void *instance;

	for (handle = 0; handle < handle_database->handle_count; handle++) {
		if (handle_database->handles[handle].state == HDB_HANDLE_STATE_EMPTY) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		handle_database->handle_count += 1;
		new_handles = (struct hdb_handle *)realloc (handle_database->handles,
			sizeof (struct hdb_handle) * handle_database->handle_count);
		if (new_handles == 0) {
			return (-1);
		}
		handle_database->handles = new_handles;
	}

	instance = (void *)malloc (instance_size);
	if (instance == 0) {
		return (-1);
	}
	memset (instance, 0, instance_size);

	handle_database->handles[handle].state = HDB_HANDLE_STATE_ACTIVE;

	handle_database->handles[handle].instance = instance;

	handle_database->handles[handle].ref_count = 1;

	*handle_id_out = handle;

	return (0);
}

static inline int hdb_handle_get (
	struct hdb_handle_database *handle_database,
	unsigned int handle,
	void **instance)
{
	if (handle >= handle_database->handle_count) {
		return (-1);
	}

	if (handle_database->handles[handle].state != HDB_HANDLE_STATE_ACTIVE) {
		return (-1);
	}

	*instance = handle_database->handles[handle].instance;

	handle_database->handles[handle].ref_count += 1;
	return (0);
}

static inline void hdb_handle_put (
	struct hdb_handle_database *handle_database,
	unsigned int handle)
{
	handle_database->handles[handle].ref_count -= 1;
	assert (handle_database->handles[handle].ref_count >= 0);

	if (handle_database->handles[handle].ref_count == 0) {
		free (handle_database->handles[handle].instance);
		memset (&handle_database->handles[handle], 0, sizeof (struct hdb_handle));
	}
}

static inline void hdb_handle_destroy (
	struct hdb_handle_database *handle_database,
	unsigned int handle)
{
	handle_database->handles[handle].state = HDB_HANDLE_STATE_PENDINGREMOVAL;
	hdb_handle_put (handle_database, handle);
}

static inline void hdb_iterator_reset (
	struct hdb_handle_database *handle_database)
{
	handle_database->iterator = 0;
}

static inline int hdb_iterator_next (
	struct hdb_handle_database *handle_database,
	void **instance,
	unsigned int *handle)
{
	int res = -1;

	while (handle_database->iterator < handle_database->handle_count) {
		*handle = handle_database->iterator;
		res = hdb_handle_get (
			handle_database,
			handle_database->iterator,
			instance);
		

		handle_database->iterator += 1;
		if (res == 0) {
			break;
		}
	}
	return (res);
}

#endif /* HDB_H_DEFINED */
