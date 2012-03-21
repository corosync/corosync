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

#include <config.h>

#include <pthread.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>

#include <corosync/swab.h>
#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/list.h>
#include <corosync/lcr/lcr_ifact.h>
#include <corosync/totem/coropoll.h>
#include <corosync/totem/totempg.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>
#define LOG_SERVICE LOG_SERVICE_IPC
#include <corosync/engine/logsys.h>

#include "poll.h"
#include "totemsrp.h"
#include "mainconfig.h"
#include "totemconfig.h"
#include "main.h"
#include "sync.h"
#include "tlist.h"
#include "util.h"
#include "timer.h"

#define SERVER_BACKLOG 5

static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t timer_mutex_cond = PTHREAD_COND_INITIALIZER;

static pthread_t expiry_thread;

static pthread_attr_t thread_attr;

static struct timerlist timers_timerlist;

static int sched_priority = 0;

static void (*timer_serialize_lock_fn) (void);

static void (*timer_serialize_unlock_fn) (void);

static void *prioritized_timer_thread (void *data);

extern void pthread_exit(void *) __attribute__((noreturn));

/*
 * This thread runs at the highest priority to run system wide timers
 */
static void *prioritized_timer_thread (void *data)
{
	int fds;
	unsigned long long timeout;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && defined(HAVE_SCHED_GET_PRIORITY_MAX)
	if (sched_priority != 0) {
		struct sched_param sched_param;

		sched_param.sched_priority = sched_priority;
		pthread_setschedparam (expiry_thread, SCHED_RR, &sched_param);
	}
#endif

	pthread_mutex_lock (&timer_mutex);
	pthread_cond_signal (&timer_mutex_cond);
	pthread_mutex_unlock (&timer_mutex);
	for (;;) {
		timer_serialize_lock_fn ();
		timeout = timerlist_msec_duration_to_expire (&timers_timerlist);
		if (timeout != -1 && timeout > 0xFFFFFFFF) {
			timeout = 0xFFFFFFFE;
		}
		timer_serialize_unlock_fn ();
		fds = poll (NULL, 0, timeout);
		if (fds < 0 && errno == EINTR) {
			continue;
		}
		if (fds < 0) {
			return NULL;
		}
		timer_serialize_lock_fn ();
		pthread_mutex_lock (&timer_mutex);

		timerlist_expire (&timers_timerlist);

		pthread_mutex_unlock (&timer_mutex);
		timer_serialize_unlock_fn ();
	}
}

static void sigusr1_handler (int num) {
#ifdef COROSYNC_SOLARIS
	/* Rearm the signal facility */
        signal (num, sigusr1_handler);
#endif
}

int corosync_timer_init (
        void (*serialize_lock_fn) (void),
        void (*serialize_unlock_fn) (void),
	int sched_priority_in)
{
	int res;

	timer_serialize_lock_fn = serialize_lock_fn;
	timer_serialize_unlock_fn = serialize_unlock_fn;
	sched_priority = sched_priority_in;

	timerlist_init (&timers_timerlist);

	signal (SIGUSR1, sigusr1_handler);

	pthread_mutex_lock (&timer_mutex);
	pthread_attr_init (&thread_attr);
	pthread_attr_setstacksize (&thread_attr, 100000);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	res = pthread_create (&expiry_thread, &thread_attr,
		prioritized_timer_thread, NULL);

	/*
	 * Wait for thread to really exec
	 */
	pthread_cond_wait (&timer_mutex_cond, &timer_mutex);
	pthread_mutex_unlock (&timer_mutex);

	return (res);
}

int corosync_timer_add_absolute (
	unsigned long long nanosec_from_epoch,
	void *data,
	void (*timer_fn) (void *data),
	timer_handle *handle)
{
	int res;
	int unlock;

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	res = timerlist_add_absolute (
		&timers_timerlist,
		timer_fn,
		data,
		nanosec_from_epoch,
		handle);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}

	pthread_kill (expiry_thread, SIGUSR1);

	return (res);
}

int corosync_timer_add_duration (
	unsigned long long nanosec_duration,
	void *data,
	void (*timer_fn) (void *data),
	timer_handle *handle)
{
	int res;
	int unlock;

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	res = timerlist_add_duration (
		&timers_timerlist,
		timer_fn,
		data,
		nanosec_duration,
		handle);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}

	pthread_kill (expiry_thread, SIGUSR1);

	return (res);
}

void corosync_timer_delete (
	timer_handle th)
{
	int unlock;

	if (th == 0) {
		return;
	}

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	timerlist_del (&timers_timerlist, th);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}
}

void corosync_timer_lock (void)
{
	pthread_mutex_lock (&timer_mutex);
}

void corosync_timer_unlock (void)
{
	pthread_mutex_unlock (&timer_mutex);
}

unsigned long long corosync_timer_time_get (void)
{
	return (timerlist_nano_from_epoch());
}

unsigned long long corosync_timer_expire_time_get (
	timer_handle th)
{
	int unlock;
	unsigned long long expire;

	if (th == 0) {
		return (0);
	}

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	expire = timerlist_expire_time (&timers_timerlist, th);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}

	return (expire);
}
