/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
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

#ifndef HDB_H_DEFINED
#define HDB_H_DEFINED

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

typedef uint64_t hdb_handle_t;

/*
 * Formatting for string printing on 32/64 bit systems
 */
#define HDB_D_FORMAT "%"PRIu64
#define HDB_X_FORMAT "%"PRIx64

enum HDB_HANDLE_STATE {
	HDB_HANDLE_STATE_EMPTY,
	HDB_HANDLE_STATE_PENDINGREMOVAL,
	HDB_HANDLE_STATE_ACTIVE
};

struct hdb_handle {
	int state;
	void *instance;
	int check;
	int ref_count;
};

struct hdb_handle_database {
	unsigned int handle_count;
	struct hdb_handle *handles;
	unsigned int iterator;
        void (*destructor) (void *);
	pthread_mutex_t lock;
	unsigned int first_run;
};

static inline void hdb_database_lock (pthread_mutex_t *mutex)
{
	pthread_mutex_lock (mutex);
}

static inline void hdb_database_unlock (pthread_mutex_t *mutex)
{
	pthread_mutex_unlock (mutex);
}
static inline void hdb_database_lock_init (pthread_mutex_t *mutex)
{
	pthread_mutex_init (mutex, NULL);
}

static inline void hdb_database_lock_destroy (pthread_mutex_t *mutex)
{
	pthread_mutex_destroy (mutex);
}

#define DECLARE_HDB_DATABASE(database_name,destructor_function)		\
static struct hdb_handle_database (database_name) = {			\
	.handle_count	= 0,						\
	.handles 	= NULL,						\
	.iterator	= 0,						\
	.destructor	= destructor_function,				\
	.first_run	= 1						\
};									\

static inline void hdb_create (
	struct hdb_handle_database *handle_database)
{
	memset (handle_database, 0, sizeof (struct hdb_handle_database));
	hdb_database_lock_init (&handle_database->lock);
}

static inline void hdb_destroy (
	struct hdb_handle_database *handle_database)
{
	free (handle_database->handles);
	hdb_database_lock_destroy (&handle_database->lock);
	memset (handle_database, 0, sizeof (struct hdb_handle_database));
}


static inline int hdb_handle_create (
	struct hdb_handle_database *handle_database,
	int instance_size,
	hdb_handle_t *handle_id_out)
{
	int handle;
	unsigned int check;
	void *new_handles;
	int found = 0;
	void *instance;
	int i;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		hdb_database_lock_init (&handle_database->lock);
	}
	hdb_database_lock (&handle_database->lock);

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
		if (new_handles == NULL) {
			hdb_database_unlock (&handle_database->lock);
			errno = ENOMEM;
			return (-1);
		}
		handle_database->handles = new_handles;
	}

	instance = (void *)malloc (instance_size);
	if (instance == 0) {
		hdb_database_unlock (&handle_database->lock);
		errno = ENOMEM;
		return (-1);
	}

	/*
	 * This code makes sure the random number isn't zero
	 * We use 0 to specify an invalid handle out of the 1^64 address space
	 * If we get 0 200 times in a row, the RNG may be broken
	 */
	for (i = 0; i < 200; i++) {
		check = random();

		if (check != 0 && check != 0xffffffff) {
			break;
		}
	}

	memset (instance, 0, instance_size);

	handle_database->handles[handle].state = HDB_HANDLE_STATE_ACTIVE;

	handle_database->handles[handle].instance = instance;

	handle_database->handles[handle].ref_count = 1;

	handle_database->handles[handle].check = check;

	*handle_id_out = (((unsigned long long)(check)) << 32) | handle;

	hdb_database_unlock (&handle_database->lock);

	return (0);
}

static inline int hdb_handle_get (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in,
	void **instance)
{
	unsigned int check = ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		hdb_database_lock_init (&handle_database->lock);
	}
	hdb_database_lock (&handle_database->lock);

	*instance = NULL;
	if (handle >= handle_database->handle_count) {
		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (handle_database->handles[handle].state != HDB_HANDLE_STATE_ACTIVE) {
		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
		check != handle_database->handles[handle].check) {

		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	*instance = handle_database->handles[handle].instance;

	handle_database->handles[handle].ref_count += 1;

	hdb_database_unlock (&handle_database->lock);
	return (0);
}

static inline int hdb_handle_get_always (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in,
	void **instance)
{
	unsigned int check = ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		hdb_database_lock_init (&handle_database->lock);
	}
	hdb_database_lock (&handle_database->lock);

	*instance = NULL;
	if (handle >= handle_database->handle_count) {
		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (handle_database->handles[handle].state == HDB_HANDLE_STATE_EMPTY) {
		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
		check != handle_database->handles[handle].check) {

		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	*instance = handle_database->handles[handle].instance;

	handle_database->handles[handle].ref_count += 1;

	hdb_database_unlock (&handle_database->lock);
	return (0);
}

static inline int hdb_handle_put (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in)
{
	unsigned int check = ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		hdb_database_lock_init (&handle_database->lock);
	}
	hdb_database_lock (&handle_database->lock);

	if (handle >= handle_database->handle_count) {
		hdb_database_unlock (&handle_database->lock);

		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
		check != handle_database->handles[handle].check) {

		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	handle_database->handles[handle].ref_count -= 1;
	assert (handle_database->handles[handle].ref_count >= 0);

	if (handle_database->handles[handle].ref_count == 0) {
		if (handle_database->destructor) {
			handle_database->destructor (handle_database->handles[handle].instance);
		}
		free (handle_database->handles[handle].instance);
		memset (&handle_database->handles[handle], 0, sizeof (struct hdb_handle));
	}
	hdb_database_unlock (&handle_database->lock);
	return (0);
}

static inline int hdb_handle_destroy (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in)
{
	unsigned int check = ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;
	int res;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		hdb_database_lock_init (&handle_database->lock);
	}
	hdb_database_lock (&handle_database->lock);

	if (handle >= handle_database->handle_count) {
		hdb_database_unlock (&handle_database->lock);

		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
		check != handle_database->handles[handle].check) {
		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	handle_database->handles[handle].state = HDB_HANDLE_STATE_PENDINGREMOVAL;
	hdb_database_unlock (&handle_database->lock);
	res = hdb_handle_put (handle_database, handle_in);
	return (res);
}

static inline int hdb_handle_refcount_get (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in)
{
	unsigned int check = ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	int refcount = 0;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		hdb_database_lock_init (&handle_database->lock);
	}
	hdb_database_lock (&handle_database->lock);

	if (handle >= handle_database->handle_count) {
		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
		check != handle_database->handles[handle].check) {
		hdb_database_unlock (&handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	refcount = handle_database->handles[handle].ref_count;

	hdb_database_unlock (&handle_database->lock);

	return (refcount);
}

static inline void hdb_iterator_reset (
	struct hdb_handle_database *handle_database)
{
	handle_database->iterator = 0;
}

static inline int hdb_iterator_next (
	struct hdb_handle_database *handle_database,
	void **instance,
	hdb_handle_t *handle)
{
	int res = -1;

	while (handle_database->iterator < handle_database->handle_count) {
		*handle = ((unsigned long long)(handle_database->handles[handle_database->iterator].check) << 32) | handle_database->iterator;
		res = hdb_handle_get (
			handle_database,
			*handle,
			instance);

		handle_database->iterator += 1;
		if (res == 0) {
			break;
		}
	}
	return (res);
}

static inline unsigned int hdb_base_convert (hdb_handle_t handle)
{
	return (handle & 0xffffffff);
}

static inline unsigned long long hdb_nocheck_convert (unsigned int handle)
{
	unsigned long long retvalue = 0xffffffffULL << 32 | handle;

	return (retvalue);
}

#endif /* HDB_H_DEFINED */
