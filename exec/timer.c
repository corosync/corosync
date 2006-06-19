/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
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
#include "print.h"

#include "util.h"

#define SERVER_BACKLOG 5

static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t thread;

static pthread_attr_t thread_attr;

static struct timerlist timers_timerlist;

static void (*timer_serialize_lock_fn) (void);

static void (*timer_serialize_unlock_fn) (void);

static void *prioritized_timer_thread (void *data);

/*
 * This thread runs at the highest priority to run system wide timers
 */
static void *prioritized_timer_thread (void *data)
{
	int fds;
	struct sched_param sched_param;
	int res;
	unsigned int timeout;

	sched_param.sched_priority = 2;
	res = pthread_setschedparam (thread, SCHED_RR, &sched_param);

	pthread_mutex_unlock (&timer_mutex);
	for (;;) {
retry_poll:
		timeout = timerlist_timeout_msec (&timers_timerlist);
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
	return (0);
}

int openais_timer_init (
        void (*serialize_lock_fn) (void),
        void (*serialize_unlock_fn) (void))
{
	int res;

	timer_serialize_lock_fn = serialize_lock_fn;
	timer_serialize_unlock_fn = serialize_unlock_fn;

	timerlist_init (&timers_timerlist);

	pthread_mutex_lock (&timer_mutex);
        pthread_attr_init (&thread_attr);
        pthread_attr_setstacksize (&thread_attr, 8192);
        pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
        res = pthread_create (&thread, &thread_attr, prioritized_timer_thread,
		NULL);

	return (res);
}

int openais_timer_add (
	unsigned int msec_in_future,
	void *data,
	void (*timer_fn) (void *data),
	timer_handle *handle)
{
	int res;

	pthread_mutex_lock (&timer_mutex);

	res = timerlist_add_future (
		&timers_timerlist,
		timer_fn,
		data,
		msec_in_future,
		handle);

	pthread_mutex_unlock (&timer_mutex);

	pthread_kill (thread, SIGUSR1);


	return (res);
}

void openais_timer_delete (
	timer_handle timer_handle)
{
	if (timer_handle == 0) {
		return;
	}

	pthread_mutex_lock (&timer_mutex);

	timerlist_del (&timers_timerlist, timer_handle);

	pthread_mutex_unlock (&timer_mutex);
}

void openais_timer_delete_data (
	timer_handle timer_handle)
{
	if (timer_handle == 0) {
		return;
	}
	pthread_mutex_lock (&timer_mutex);

	timerlist_del_data (&timers_timerlist, timer_handle);

	pthread_mutex_unlock (&timer_mutex);
}

void openais_timer_lock (void)
{
	pthread_mutex_lock (&timer_mutex);
}

void openais_timer_unlock (void)
{
	pthread_mutex_unlock (&timer_mutex);
}
