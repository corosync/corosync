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

#include "aispoll.h"
#include "../include/list.h"
#include "tlist.h"

typedef int (*dispatch_fn_t) (poll_handle poll_handle, int fd, int revents, void *data);

struct poll_instance {
	struct pollfd *ufds;
	int nfds;
	dispatch_fn_t *dispatch_fns;
	void **data;
	struct timerlist timerlist;
	pthread_mutex_t mutex;
};
#define POLLINSTANCE_MUTEX_OFFSET offset_of(struct poll_instance, mutex)

struct handle {
        int valid;
        void *instance;
        unsigned int generation;
};

struct handle_database {
        unsigned int handle_count;
        struct handle *handles;
        unsigned int generation;
        pthread_mutex_t mutex;
};

#define offset_of(type,member) (int)(&(((type *)0)->member))

#define HANDLECONVERT_NOLOCKING         0x80000000
#define HANDLECONVERT_DONTUNLOCKDB      0x40000000

int handle_create (
		struct handle_database *handle_database,
		void **instance_out,
		int instance_size,
		int *handle_out)
{
		int handle;
		void *new_handles;
		int found = 0;
		void *instance;

		pthread_mutex_lock (&handle_database->mutex);

		for (handle = 0; handle < handle_database->handle_count; handle++) {
			if (handle_database->handles[handle].valid == 0) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			handle_database->handle_count += 1;
			new_handles = (struct handle *)realloc (handle_database->handles,
			sizeof (struct handle) * handle_database->handle_count);
			if (new_handles == 0) {
				pthread_mutex_unlock (&handle_database->mutex);
				errno = ENOMEM;
				return (-1);
			}
			handle_database->handles = new_handles;
		}
		instance = (void *)malloc (instance_size);
		if (instance == 0) {
			errno = ENOMEM;
			return (-1);
		}
		memset (instance, 0, instance_size);

		handle_database->handles[handle].valid = 1;
		handle_database->handles[handle].instance = instance;
		handle_database->handles[handle].generation = handle_database->generation++;

		*handle_out = handle;
		*instance_out = instance;

		pthread_mutex_unlock (&handle_database->mutex);
		return (0);
}

int handle_convert (
	struct handle_database *handle_database,
	unsigned int handle,
	void **instance,
	int offset_to_mutex,
	unsigned int *generation_out)
{
	int unlock_db;
	int locking;

	unlock_db = (0 == (offset_to_mutex & HANDLECONVERT_DONTUNLOCKDB));
	locking = (0 == (offset_to_mutex & HANDLECONVERT_NOLOCKING));
	offset_to_mutex &= 0x00fffff; /* remove 8 bits of flags */

	if (locking) {
		pthread_mutex_lock (&handle_database->mutex);
	}

/* Add this later
	res = saHandleVerify (handle_database, handle);
	if (res == -1) {
		if (locking) {
			pthread_mutex_unlock (&handle_database->mutex);
		}
		errno = ENOENT;
		return (-1);
	}
*/

	*instance = handle_database->handles[handle].instance;
	if (generation_out) {
		*generation_out = handle_database->handles[handle].generation;
	}

	/*
	 * This function exits holding the mutex in the instance instance
	 * pointed to by offset_to_mutex (if NOLOCKING isn't set)
	 */
	if (locking) {
		pthread_mutex_lock ((pthread_mutex_t *)(*instance + offset_to_mutex));
		if (unlock_db) {
			pthread_mutex_unlock (&handle_database->mutex);
		}
	}

	return (0);
}


/*
 * All instances in one database
 */
static struct handle_database poll_instance_database = {
        .handle_count	= 0,
        .handles		= 0,
        .generation		= 0,
        .mutex			= PTHREAD_MUTEX_INITIALIZER
};

poll_handle poll_create (void)
{
	poll_handle poll_handle;
	struct poll_instance *poll_instance;
	int res;

	res = handle_create (&poll_instance_database, (void *)&poll_instance,
		sizeof (struct poll_instance), &poll_handle);
	if (res == -1) {
		goto error_exit;
	}
	poll_instance->ufds = 0;
	poll_instance->nfds = 0;
	poll_instance->dispatch_fns = 0;
	poll_instance->data = 0;
	timerlist_init (&poll_instance->timerlist);

	return (poll_handle);

error_exit:
	return (-1);
}

int poll_destroy (poll_handle poll_handle)
{
	struct poll_instance *poll_instance;
	int res;

	res = handle_convert (&poll_instance_database, poll_handle,
		(void *)&poll_instance, POLLINSTANCE_MUTEX_OFFSET, 0);
	if (res == -1) {
		goto error_exit;
	}

	if (poll_instance->ufds) {
		free (poll_instance->ufds);
	}
	if (poll_instance->dispatch_fns) {
		free (poll_instance->dispatch_fns);
	}
	if (poll_instance->data) {
		free (poll_instance->data);
	}
	timerlist_free (&poll_instance->timerlist);
// TODO destroy poll

	return (0);

error_exit:
	return (-1);
}

int poll_dispatch_add (
	poll_handle handle,
	int fd,
	int events,
	void *data,
	int (*dispatch_fn) (poll_handle poll_handle, int fd, int revents, void *data))
{
	struct poll_instance *poll_instance;
	struct pollfd *ufds;
	dispatch_fn_t *dispatch_fns;
	void **data_list;
	int res;
	int found = 0;
	int install_pos;

	res = handle_convert (&poll_instance_database, handle,
		(void *)&poll_instance, POLLINSTANCE_MUTEX_OFFSET, 0);

	if (res == -1) {
		goto error_exit;
	}

	for (found = 0, install_pos = 0; install_pos < poll_instance->nfds; install_pos++) {
		if (poll_instance->ufds[install_pos].fd == -1) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		/*
		 * Grow pollfd list
		 */
		ufds = (struct pollfd *)realloc (poll_instance->ufds,
			(poll_instance->nfds + 1) * sizeof (struct pollfd));
		if (ufds == 0) {
			errno = ENOMEM;
			goto error_exit;
		}
		poll_instance->ufds = ufds;
	
		/*
		 * Grow dispatch functions list
		 */
			dispatch_fns = (dispatch_fn_t *)realloc (poll_instance->dispatch_fns,
			(poll_instance->nfds + 1) * sizeof (dispatch_fn_t));
		if (dispatch_fns == 0) {
			errno = ENOMEM;
			goto error_exit;
		}
		poll_instance->dispatch_fns = dispatch_fns;
	
		/*
		 * Grow data list
		 */
		data_list = (void **)realloc (poll_instance->data,
			(poll_instance->nfds + 1) * sizeof (void *));
		if (data_list == 0) {
			errno = ENOMEM;
			goto error_exit;
		}
		poll_instance->data = data_list;
	
		poll_instance->nfds += 1;
		install_pos = poll_instance->nfds - 1;
	}
	
	/*
	 * Install new dispatch handler
	 */
	poll_instance->ufds[install_pos].fd = fd;
	poll_instance->ufds[install_pos].events = events;
	poll_instance->ufds[install_pos].revents = 0;
	poll_instance->dispatch_fns[install_pos] = dispatch_fn;
	poll_instance->data[install_pos] = data;

	return (0);

error_exit:
	return (-1);
}

int poll_dispatch_modify (
	poll_handle handle,
	int fd,
	int events,
	int (*dispatch_fn) (poll_handle poll_handle, int fd, int revents, void *data))
{
	struct poll_instance *poll_instance;
	int i;
	int res;

	res = handle_convert (&poll_instance_database, handle,
		(void *)&poll_instance, POLLINSTANCE_MUTEX_OFFSET, 0);
	if (res == -1) {
		return (-1);
	}

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < poll_instance->nfds; i++) {
		if (poll_instance->ufds[i].fd == fd) {
			poll_instance->ufds[i].events = events;
			poll_instance->dispatch_fns[i] = dispatch_fn;
			return (0);
		}
	}

	errno = EBADF;
	return (-1);
}

int poll_dispatch_delete (
	poll_handle handle,
	int fd)
{
	struct poll_instance *poll_instance;
	int i;
	int res;
	int found = 0;

	res = handle_convert (&poll_instance_database, handle,
		(void *)&poll_instance, POLLINSTANCE_MUTEX_OFFSET, 0);
	if (res == -1) {
		goto error_exit;
	}

	/*
	 * Find dispatch fd to delete
	 */
	for (i = 0; i < poll_instance->nfds; i++) {
		if (poll_instance->ufds[i].fd == fd) {
			found = 1;
			break;
		}
	}

	if (found) {
		poll_instance->ufds[i].fd = -1;
		return (0);
	}

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
	poll_timer_handle timer_handle;
	int res;

	res = handle_convert (&poll_instance_database, handle,
		(void *)&poll_instance, POLLINSTANCE_MUTEX_OFFSET, 0);
	if (res == -1) {
		return (-1);
	}

	timer_handle = (poll_timer_handle)timerlist_add_future (&poll_instance->timerlist,
		timer_fn, data, msec_in_future);

	if (timer_handle != 0) {
		*timer_handle_out = timer_handle;
		return (0);
	}
	return (-1);
}

int poll_timer_delete (
	poll_handle handle,
	poll_timer_handle timer_handle)
{
	struct poll_instance *poll_instance;
	int res;

	if (timer_handle == 0) {
		return (0);
	}
	res = handle_convert (&poll_instance_database, handle,
		(void *)&poll_instance, POLLINSTANCE_MUTEX_OFFSET, 0);
	if (res == -1) {
		return (-1);
	}

	timerlist_del (&poll_instance->timerlist, (void *)timer_handle);
	return (0);
}

int poll_run (
	poll_handle handle)
{
	struct poll_instance *poll_instance;
	int i;
	int timeout = -1;
	int res;

	res = handle_convert (&poll_instance_database, handle,
		(void *)&poll_instance, POLLINSTANCE_MUTEX_OFFSET, 0);
	if (res == -1) {
		goto error_exit;
	}

	for (;;) {
		timeout = timerlist_timeout_msec (&poll_instance->timerlist);

retry_poll:
		res = poll (poll_instance->ufds, poll_instance->nfds, timeout);
		if (errno == EINTR && res == -1) {
			goto retry_poll;
		} else
		if (res == -1) {
			goto error_exit;
		}


		for (i = 0; i < poll_instance->nfds; i++) {
			if (poll_instance->ufds[i].fd != -1 &&
				poll_instance->ufds[i].revents) {

				res = poll_instance->dispatch_fns[i] (handle, poll_instance->ufds[i].fd, 
					poll_instance->ufds[i].revents, poll_instance->data[i]);

				/*
				 * Remove dispatch functions that return -1
				 */
				if (res == -1) {
					poll_instance->ufds[i].fd = -1; /* empty entry */
				}
			}
		}
		timerlist_expire (&poll_instance->timerlist);
	} /* for (;;) */

error_exit:
	return (-1);
}

int poll_stop (
	poll_handle handle);
