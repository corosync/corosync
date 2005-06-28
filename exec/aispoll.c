/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
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
#include <errno.h>
#include <pthread.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "aispoll.h"
#include "../include/list.h"
#include "hdb.h"
#include "../include/ais_types.h"
#include "tlist.h"

typedef int (*dispatch_fn_t) (poll_handle poll_handle, int fd, int revents, void *data, unsigned int *prio);

struct poll_entry {
	unsigned int prio;
	struct pollfd ufd;
	dispatch_fn_t dispatch_fn;
	void *data;
};

struct poll_instance {
	struct poll_entry *poll_entries;
	struct pollfd *ufds;
	int poll_entry_count;
	struct timerlist timerlist;
};

/*
 * All instances in one database
 */
static struct saHandleDatabase poll_instance_database = {
        .handleCount				= 0,
        .handles					= 0,
		.handleInstanceDestructor	= 0
};

poll_handle poll_create (void)
{
	poll_handle handle;
	struct poll_instance *poll_instance;
	SaErrorT error;

	error = saHandleCreate (&poll_instance_database,
		sizeof (struct poll_instance), &handle);
	if (error != SA_OK) {
		goto error_exit;
	}
	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_destroy;
	}
	
	poll_instance->poll_entries = 0;
	poll_instance->ufds = 0;
	poll_instance->poll_entry_count = 0;
	timerlist_init (&poll_instance->timerlist);

	return (handle);

error_destroy:
	saHandleDestroy (&poll_instance_database, handle);
	
error_exit:
	return (-1);
}

int poll_destroy (poll_handle handle)
{
	struct poll_instance *poll_instance;
	SaErrorT error;

	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	if (poll_instance->poll_entries) {
		free (poll_instance->poll_entries);
	}
	if (poll_instance->ufds) {
		free (poll_instance->ufds);
	}
	timerlist_free (&poll_instance->timerlist);

	saHandleDestroy (&poll_instance_database, handle);

	saHandleInstancePut (&poll_instance_database, handle);

	return (0);

error_exit:
	return (-1);
}

int poll_dispatch_add (
	poll_handle handle,
	int fd,
	int events,
	void *data,
	int (*dispatch_fn) (poll_handle poll_handle, int fd, int revents, void *data, unsigned int *prio),
	unsigned int prio)
{
	struct poll_instance *poll_instance;
	struct poll_entry *poll_entries;
	struct pollfd *ufds;
	int found = 0;
	int install_pos;
	SaErrorT error;

	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	for (found = 0, install_pos = 0; install_pos < poll_instance->poll_entry_count; install_pos++) {
		if (poll_instance->poll_entries[install_pos].ufd.fd == -1) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		/*
		 * Grow pollfd list
		 */
		poll_entries = (struct poll_entry *)realloc (poll_instance->poll_entries,
			(poll_instance->poll_entry_count + 1) *
			sizeof (struct poll_entry));
		if (poll_entries == 0) {
			errno = ENOMEM;
			goto error_exit;
		}
		poll_instance->poll_entries = poll_entries;
	
		ufds = (struct pollfd *)realloc (poll_instance->ufds,
			(poll_instance->poll_entry_count + 1) *
			sizeof (struct pollfd));
		if (ufds == 0) {
			errno = ENOMEM;
			goto error_exit;
		}
		poll_instance->ufds = ufds;

		poll_instance->poll_entry_count += 1;
		install_pos = poll_instance->poll_entry_count - 1;
	}
	
	/*
	 * Install new dispatch handler
	 */
	poll_instance->poll_entries[install_pos].prio = prio;
	poll_instance->poll_entries[install_pos].ufd.fd = fd;
	poll_instance->poll_entries[install_pos].ufd.events = events;
	poll_instance->poll_entries[install_pos].ufd.revents = 0;
	poll_instance->poll_entries[install_pos].dispatch_fn = dispatch_fn;
	poll_instance->poll_entries[install_pos].data = data;

	saHandleInstancePut (&poll_instance_database, handle);

	return (0);

error_exit:
	return (-1);
}

int poll_dispatch_modify (
	poll_handle handle,
	int fd,
	int events,
	int (*dispatch_fn) (poll_handle poll_handle, int fd, int revents, void *data, unsigned int *prio),
	unsigned int prio)
{
	struct poll_instance *poll_instance;
	int i;
	SaErrorT error;

	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			poll_instance->poll_entries[i].ufd.events = events;
			poll_instance->poll_entries[i].dispatch_fn = dispatch_fn;
			poll_instance->poll_entries[i].prio = prio;
			return (0);
		}
	}

	errno = EBADF;

	saHandleInstancePut (&poll_instance_database, handle);

error_exit:
	return (-1);
}

int poll_dispatch_delete (
	poll_handle handle,
	int fd)
{
	struct poll_instance *poll_instance;
	int i;
	SaErrorT error;
	int found = 0;

	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	/*
	 * Find dispatch fd to delete
	 */
	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			found = 1;
			break;
		}
	}

	if (found) {
		poll_instance->poll_entries[i].ufd.fd = -1;
		poll_instance->poll_entries[i].ufd.revents = 0;
	}

	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->ufds[i].fd == fd) {
			found = 1;
			break;
		}
	}

	if (found) {
		poll_instance->ufds[i].fd = -1;
		poll_instance->ufds[i].revents = 0;
	}

	saHandleInstancePut (&poll_instance_database, handle);
	return (0);

error_exit:
	errno = EBADF;
	return (-1);
}

int poll_timer_add (
	poll_handle handle,
	int msec_in_future, void *data,
	void (*timer_fn) (void *data),
	poll_timer_handle *timer_handle_out)
{
	struct poll_instance *poll_instance;
	int res = -1;
	SaErrorT error;

	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	timerlist_add_future (&poll_instance->timerlist,
		timer_fn, data, msec_in_future, timer_handle_out);

	if (timer_handle_out != 0) {
		res = 0;
	}

	saHandleInstancePut (&poll_instance_database, handle);
error_exit:
	return (res);
}

int poll_timer_delete (
	poll_handle handle,
	poll_timer_handle timer_handle)
{
	struct poll_instance *poll_instance;
	SaErrorT error;

	if (timer_handle == 0) {
		return (0);
	}
	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	timerlist_del (&poll_instance->timerlist, (void *)timer_handle);

	saHandleInstancePut (&poll_instance_database, handle);

	return (0);

error_exit:
	return (-1);
}

int poll_timer_delete_data (
	poll_handle handle,
	poll_timer_handle timer_handle) {
	struct poll_instance *poll_instance;
	SaErrorT error;

	if (timer_handle == 0) {
		return (0);
	}
	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	timerlist_del_data (&poll_instance->timerlist, (void *)timer_handle);

	saHandleInstancePut (&poll_instance_database, handle);

	return (0);

error_exit:
        return (-1);

}


int poll_entry_compare (const void *a, const void *b) {
	struct poll_entry *poll_entry_a = (struct poll_entry *)a;
	struct poll_entry *poll_entry_b = (struct poll_entry *)b;

	return (poll_entry_a->prio < poll_entry_b->prio);
}

int poll_run (
	poll_handle handle)
{
	struct poll_instance *poll_instance;
	int i;
	int timeout = -1;
	int res;
	SaErrorT error;
	int poll_entry_count;

	error = saHandleInstanceGet (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	for (;;) {
		/*
		 * Sort the poll entries list highest priority to lowest priority
		 * Then build ufds structure for use with poll system call
		 */
		qsort (poll_instance->poll_entries, poll_instance->poll_entry_count,
			sizeof (struct poll_entry), poll_entry_compare);
		for (i = 0; i < poll_instance->poll_entry_count; i++) {
			memcpy (&poll_instance->ufds[i],
				&poll_instance->poll_entries[i].ufd,
				sizeof (struct pollfd));
		}
		timeout = timerlist_timeout_msec (&poll_instance->timerlist);

retry_poll:
		res = poll (poll_instance->ufds,
			poll_instance->poll_entry_count, timeout);
		if (errno == EINTR && res == -1) {
			goto retry_poll;
		} else
		if (res == -1) {
			goto error_exit;
		}

		poll_entry_count = poll_instance->poll_entry_count;
		for (i = 0; i < poll_entry_count; i++) {
			if (poll_instance->ufds[i].fd != -1 &&
				poll_instance->ufds[i].revents) {

				res = poll_instance->poll_entries[i].dispatch_fn (handle,
					poll_instance->ufds[i].fd, 
					poll_instance->ufds[i].revents,
					poll_instance->poll_entries[i].data,
					&poll_instance->poll_entries[i].prio);

				/*
				 * Remove dispatch functions that return -1
				 */
				if (res == -1) {
					poll_instance->poll_entries[i].ufd.fd = -1; /* empty entry */
				}
			}
		}
		timerlist_expire (&poll_instance->timerlist);
	} /* for (;;) */

	saHandleInstancePut (&poll_instance_database, handle);
error_exit:
	return (-1);
}

int poll_stop (
	poll_handle handle);
