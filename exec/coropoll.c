/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
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

#include <config.h>

#include <errno.h>
#include <pthread.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <corosync/hdb.h>
#include <corosync/totem/coropoll.h>
#include <corosync/list.h>
#include "tlist.h"
#include "util.h"

typedef int (*dispatch_fn_t) (hdb_handle_t hdb_handle, int fd, int revents, void *data);

struct poll_entry {
	struct pollfd ufd;
	dispatch_fn_t dispatch_fn;
	void *data;
};

struct poll_instance {
	struct poll_entry *poll_entries;
	struct pollfd *ufds;
	int poll_entry_count;
	struct timerlist timerlist;
	int stop_requested;
	int pipefds[2];
	poll_low_fds_event_fn low_fds_event_fn;
	int32_t not_enough_fds;
};

DECLARE_HDB_DATABASE (poll_instance_database,NULL);

static int dummy_dispatch_fn (hdb_handle_t handle, int fd, int revents, void *data) {
	return (0);
}

hdb_handle_t poll_create (void)
{
	hdb_handle_t handle;
	struct poll_instance *poll_instance;
	unsigned int res;

	res = hdb_handle_create (&poll_instance_database,
		sizeof (struct poll_instance), &handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		goto error_destroy;
	}

	poll_instance->poll_entries = 0;
	poll_instance->ufds = 0;
	poll_instance->poll_entry_count = 0;
	poll_instance->stop_requested = 0;
	poll_instance->not_enough_fds = 0;
	timerlist_init (&poll_instance->timerlist);

	res = pipe (poll_instance->pipefds);
	if (res != 0) {
		goto error_destroy;
	}

	/*
	 * Allow changes in modify to propogate into new poll instance
	 */
	res = poll_dispatch_add (
		handle,
		poll_instance->pipefds[0],
		POLLIN,
		NULL,
		dummy_dispatch_fn);
	if (res != 0) {
		goto error_destroy;
	}
		
	return (handle);

error_destroy:
	hdb_handle_destroy (&poll_instance_database, handle);

error_exit:
	return (-1);
}

int poll_destroy (hdb_handle_t handle)
{
	struct poll_instance *poll_instance;
	int res = 0;

	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	free (poll_instance->poll_entries);
	free (poll_instance->ufds);

	hdb_handle_destroy (&poll_instance_database, handle);

	hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int poll_dispatch_add (
	hdb_handle_t handle,
	int fd,
	int events,
	void *data,
	int (*dispatch_fn) (
		hdb_handle_t hdb_handle_t,
		int fd,
		int revents,
		void *data))
{
	struct poll_instance *poll_instance;
	struct poll_entry *poll_entries;
	struct pollfd *ufds;
	int found = 0;
	int install_pos;
	int res = 0;

	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
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
		if (poll_entries == NULL) {
			res = -ENOMEM;
			goto error_put;
		}
		poll_instance->poll_entries = poll_entries;

		ufds = (struct pollfd *)realloc (poll_instance->ufds,
			(poll_instance->poll_entry_count + 1) *
			sizeof (struct pollfd));
		if (ufds == NULL) {
			res = -ENOMEM;
			goto error_put;
		}
		poll_instance->ufds = ufds;

		poll_instance->poll_entry_count += 1;
		install_pos = poll_instance->poll_entry_count - 1;
	}

	/*
	 * Install new dispatch handler
	 */
	poll_instance->poll_entries[install_pos].ufd.fd = fd;
	poll_instance->poll_entries[install_pos].ufd.events = events;
	poll_instance->poll_entries[install_pos].ufd.revents = 0;
	poll_instance->poll_entries[install_pos].dispatch_fn = dispatch_fn;
	poll_instance->poll_entries[install_pos].data = data;

error_put:
	hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int poll_dispatch_modify (
	hdb_handle_t handle,
	int fd,
	int events,
	int (*dispatch_fn) (
		hdb_handle_t hdb_handle_t,
		int fd,
		int revents,
		void *data))
{
	struct poll_instance *poll_instance;
	int i;
	int res = 0;

	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			int change_notify = 0;

			if (poll_instance->poll_entries[i].ufd.events != events) {
				change_notify = 1;
			}
			poll_instance->poll_entries[i].ufd.events = events;
			poll_instance->poll_entries[i].dispatch_fn = dispatch_fn;
			if (change_notify) {
				char buf = 1;
retry_write:
				if (write (poll_instance->pipefds[1], &buf, 1) < 0 && errno == EINTR )
					goto retry_write;
			}

			goto error_put;
		}
	}

	res = -EBADF;

error_put:
	hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int poll_dispatch_delete (
	hdb_handle_t handle,
	int fd)
{
	struct poll_instance *poll_instance;
	int i;
	int res = 0;

	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	/*
	 * Find dispatch fd to delete
	 */
	res = -EBADF;
	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			poll_instance->ufds[i].fd = -1;
			poll_instance->poll_entries[i].ufd.fd = -1;
			poll_instance->poll_entries[i].ufd.revents = 0;

			res = 0;
			break;
		}
	}


	hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int poll_timer_add (
	hdb_handle_t handle,
	int msec_duration, void *data,
	void (*timer_fn) (void *data),
	poll_timer_handle *timer_handle_out)
{
	struct poll_instance *poll_instance;
	int res = 0;

	if (timer_handle_out == NULL) {
		res = -ENOENT;
		goto error_exit;
	}

	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	timerlist_add_duration (&poll_instance->timerlist,
		timer_fn, data, ((unsigned long long)msec_duration) * 1000000ULL, timer_handle_out);

	hdb_handle_put (&poll_instance_database, handle);
error_exit:
	return (res);
}

int poll_timer_delete (
	hdb_handle_t handle,
	poll_timer_handle th)
{
	struct poll_instance *poll_instance;
	int res = 0;

	if (th == 0) {
		return (0);
	}
	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	timerlist_del (&poll_instance->timerlist, (void *)th);

	hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int poll_stop (
	hdb_handle_t handle)
{
	struct poll_instance *poll_instance;
	unsigned int res;

	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	poll_instance->stop_requested = 1;

	hdb_handle_put (&poll_instance_database, handle);
error_exit:
	return (res);
}

int poll_low_fds_event_set(
	hdb_handle_t handle,
	poll_low_fds_event_fn fn)
{
	struct poll_instance *poll_instance;

	if (hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance) != 0) {
		return -ENOENT;
	}

	poll_instance->low_fds_event_fn = fn;

	hdb_handle_put (&poll_instance_database, handle);
	return 0;
}

/* logs, std(in|out|err), pipe */
#define POLL_FDS_USED_MISC 50

static void poll_fds_usage_check(struct poll_instance *poll_instance)
{
	struct rlimit lim;
	static int32_t socks_limit = 0;
	int32_t send_event = 0;
	int32_t socks_used = 0;
	int32_t socks_avail = 0;
	int32_t i;

	if (socks_limit == 0) {
		if (getrlimit(RLIMIT_NOFILE, &lim) == -1) {
			perror("getrlimit() failed");
			return;
		}
		socks_limit = lim.rlim_cur;
		socks_limit -= POLL_FDS_USED_MISC;
		if (socks_limit < 0) {
			socks_limit = 0;
		}
	}

	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd != -1) {
			socks_used++;
		}
	}
	socks_avail = socks_limit - socks_used;
	if (socks_avail < 0) {
		socks_avail = 0;
	}
	send_event = 0;
	if (poll_instance->not_enough_fds) {
		if (socks_avail > 2) {
			poll_instance->not_enough_fds = 0;
			send_event = 1;
		}
	} else {
		if (socks_avail <= 1) {
			poll_instance->not_enough_fds = 1;
			send_event = 1;
		}
	}
	if (send_event) {
		poll_instance->low_fds_event_fn(poll_instance->not_enough_fds,
			socks_avail);
	}
}

int poll_run (
	hdb_handle_t handle)
{
	struct poll_instance *poll_instance;
	int i;
	unsigned long long expire_timeout_msec = -1;
	int res;
	int poll_entry_count;

	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	for (;;) {
rebuild_poll:
		for (i = 0; i < poll_instance->poll_entry_count; i++) {
			memcpy (&poll_instance->ufds[i],
				&poll_instance->poll_entries[i].ufd,
				sizeof (struct pollfd));
		}
		poll_fds_usage_check(poll_instance);
		expire_timeout_msec = timerlist_msec_duration_to_expire (&poll_instance->timerlist);

		if (expire_timeout_msec != -1 && expire_timeout_msec > 0xFFFFFFFF) {
			expire_timeout_msec = 0xFFFFFFFE;
		}

retry_poll:
		res = poll (poll_instance->ufds,
			poll_instance->poll_entry_count, expire_timeout_msec);
		if (poll_instance->stop_requested) {
			return (0);
		}
		if (errno == EINTR && res == -1) {
			goto retry_poll;
		} else
		if (res == -1) {
			goto error_exit;
		}

		if (poll_instance->ufds[0].revents) {
			char buf;
retry_read:
			if (read (poll_instance->ufds[0].fd, &buf, 1) < 0 && errno == EINTR)
				goto retry_read;
			goto rebuild_poll;
		}
		poll_entry_count = poll_instance->poll_entry_count;
		for (i = 0; i < poll_entry_count; i++) {
			if (poll_instance->ufds[i].fd != -1 &&
				poll_instance->ufds[i].revents) {

				res = poll_instance->poll_entries[i].dispatch_fn (handle,
					poll_instance->ufds[i].fd,
					poll_instance->ufds[i].revents,
					poll_instance->poll_entries[i].data);

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

	hdb_handle_put (&poll_instance_database, handle);
error_exit:
	return (-1);
}

#ifdef COMPILE_OUT
void poll_print_state (
	hdb_handle_t handle,
	int fd)
{
	struct poll_instance *poll_instance;
	int i;
	int res = 0;
	res = hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		exit (1);
	}

	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
		printf ("fd %d\n", poll_instance->poll_entries[i].ufd.fd);
		printf ("events %d\n", poll_instance->poll_entries[i].ufd.events);
		printf ("dispatch_fn %p\n", poll_instance->poll_entries[i].dispatch_fn);
		}
	}
}

#endif
