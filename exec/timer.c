/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2008 Red Hat, Inc.
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
#include <pthread.h>
#include <assert.h>
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

#include "swab.h"
#include "../include/saAis.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_ifact.h"
#include "poll.h"
#include "totempg.h"
#include "totemsrp.h"
#include "mempool.h"
#include "mainconfig.h"
#include "totemconfig.h"
#include "main.h"
#include "service.h"
#include "sync.h"
#include "swab.h"
#include "objdb.h"
#include "config.h"
#include "tlist.h"
#define LOG_SERVICE LOG_SERVICE_IPC
#include "logsys.h"

#include "util.h"

#define SERVER_BACKLOG 5

static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t expiry_thread;

static pthread_attr_t thread_attr;

static struct timerlist timers_timerlist;

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

#if ! defined(TS_CLASS) && (defined(OPENAIS_BSD) || defined(OPENAIS_LINUX) || defined(OPENAIS_SOLARIS))
	struct sched_param sched_param;
	int res;

	sched_param.sched_priority = 2;
	res = pthread_setschedparam (expiry_thread, SCHED_RR, &sched_param);
#endif

	pthread_mutex_unlock (&timer_mutex);
	for (;;) {
retry_poll:
		timer_serialize_lock_fn ();
		timeout = timerlist_msec_duration_to_expire (&timers_timerlist);
		if (timeout != -1 && timeout > 0xFFFFFFFF) {
			timeout = 0xFFFFFFFE;
		}
		timer_serialize_unlock_fn ();
		fds = poll (NULL, 0, timeout);
		if (fds == -1) {
			goto retry_poll;
		}
		pthread_mutex_lock (&timer_mutex);
		timer_serialize_lock_fn ();

		timerlist_expire (&timers_timerlist);
		
		timer_serialize_unlock_fn ();
		pthread_mutex_unlock (&timer_mutex);
	}

	pthread_exit (0);
}

static void sigusr1_handler (int num) {
#ifdef OPENAIS_SOLARIS
	/* Rearm the signal facility */
        signal (num, sigusr1_handler);
#endif
}

int openais_timer_init (
        void (*serialize_lock_fn) (void),
        void (*serialize_unlock_fn) (void))
{
	int res;

	timer_serialize_lock_fn = serialize_lock_fn;
	timer_serialize_unlock_fn = serialize_unlock_fn;

	timerlist_init (&timers_timerlist);

	signal (SIGUSR1, sigusr1_handler);

	pthread_mutex_lock (&timer_mutex);
	pthread_attr_init (&thread_attr);
	pthread_attr_setstacksize (&thread_attr, 100000);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	res = pthread_create (&expiry_thread, &thread_attr,
		prioritized_timer_thread, NULL);

	return (res);
}

int openais_timer_add_absolute (
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

int openais_timer_add_duration (
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

void openais_timer_delete (
	timer_handle timer_handle)
{
	int unlock;

	if (timer_handle == 0) {
		return;
	}

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	timerlist_del (&timers_timerlist, timer_handle);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}
}

void openais_timer_lock (void)
{
	pthread_mutex_lock (&timer_mutex);
}

void openais_timer_unlock (void)
{
	pthread_mutex_unlock (&timer_mutex);
}
