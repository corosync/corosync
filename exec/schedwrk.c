/*
 * Copyright (c) 2009-2010 Red Hat, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
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

#include <config.h>
#include <corosync/totem/totempg.h>
#include <corosync/hdb.h>
#include "schedwrk.h"

static void (*serialize_lock) (void);
static void (*serialize_unlock) (void);

DECLARE_HDB_DATABASE (schedwrk_instance_database,NULL);

struct schedwrk_instance {
	int (*schedwrk_fn) (const void *);
	const void *context;
	void *callback_handle;
	int lock;
};

static int schedwrk_do (enum totem_callback_token_type type, const void *context)
{
	hdb_handle_t handle = *((hdb_handle_t *)context);
	struct schedwrk_instance *instance;
	int res;

	res = hdb_handle_get (&schedwrk_instance_database,
		handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	if (instance->lock)
		serialize_lock ();

	res = instance->schedwrk_fn (instance->context);

	if (instance->lock)
		serialize_unlock ();

	if (res == 0) {
		hdb_handle_destroy (&schedwrk_instance_database, handle);
	}
        hdb_handle_put (&schedwrk_instance_database, handle);
	return (res);

error_exit:
	return (-1);
}

void schedwrk_init (
	void (*serialize_lock_fn) (void),
	void (*serialize_unlock_fn) (void))
{
	serialize_lock = serialize_lock_fn;
	serialize_unlock = serialize_unlock_fn;
}

static int schedwrk_internal_create (
	hdb_handle_t *handle,
	int (schedwrk_fn) (const void *),
	const void *context,
	int lock)
{
	struct schedwrk_instance *instance;
	int res;

	res = hdb_handle_create (&schedwrk_instance_database,
		sizeof (struct schedwrk_instance), handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&schedwrk_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}

	totempg_callback_token_create (
		&instance->callback_handle,
		TOTEM_CALLBACK_TOKEN_SENT,
		1,
		schedwrk_do,
		handle);

	instance->schedwrk_fn = schedwrk_fn;
	instance->context = context;
	instance->lock = lock;

        hdb_handle_put (&schedwrk_instance_database, *handle);

	return (0);

error_destroy:
	hdb_handle_destroy (&schedwrk_instance_database, *handle);

error_exit:
	return (-1);
}

/*
 * handle pointer is internally used by totempg_callback_token_create. To make schedwrk work,
 * handle must be pointer to ether heap or .text or static memory (not stack) which is not
 * changed by caller.
 */
int schedwrk_create (
	hdb_handle_t *handle,
	int (schedwrk_fn) (const void *),
	const void *context)
{
	return schedwrk_internal_create (handle, schedwrk_fn, context, 1);
}

int schedwrk_create_nolock (
	hdb_handle_t *handle,
	int (schedwrk_fn) (const void *),
	const void *context)
{
	return schedwrk_internal_create (handle, schedwrk_fn, context, 0);
}

void schedwrk_destroy (hdb_handle_t handle)
{
	hdb_handle_destroy (&schedwrk_instance_database, handle);
}
