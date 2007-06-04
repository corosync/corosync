/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
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

#ifndef TLIST_H_DEFINED
#define TLIST_H_DEFINED

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/param.h>

#include "../include/list.h"

typedef void * timer_handle;

struct timerlist {
	struct list_head timer_head;
	struct list_head *timer_iter;
};

struct timerlist_timer {
	struct list_head list;
	unsigned long long nano_from_epoch;
	void (*timer_fn)(void *data);
	void *data;
	timer_handle handle_addr;
};

static inline void timerlist_init (struct timerlist *timerlist)
{
	list_init (&timerlist->timer_head);
}

static inline unsigned long long timerlist_nano_from_epoch (void)
{
	unsigned long long nano_from_epoch;
	struct timeval time_from_epoch;
	gettimeofday (&time_from_epoch, 0);

	nano_from_epoch = ((time_from_epoch.tv_sec * 1000000000ULL) + (time_from_epoch.tv_usec * 1000ULL));
	return (nano_from_epoch);
}

static inline void timerlist_add (struct timerlist *timerlist, struct timerlist_timer *timer)
{
	struct list_head *timer_list = 0;
	struct timerlist_timer *timer_from_list;
	int found;

	for (found = 0, timer_list = timerlist->timer_head.next;
		timer_list != &timerlist->timer_head;
		timer_list = timer_list->next) {

		timer_from_list = list_entry (timer_list,
			struct timerlist_timer, list);

		if (timer_from_list->nano_from_epoch > timer->nano_from_epoch) {
			list_add (&timer->list, timer_list->prev);
			found = 1;
			break; /* for timer iteration */
		}
	}
	if (found == 0) {
		list_add (&timer->list, timerlist->timer_head.prev);
	}
}

static inline int timerlist_add_absolute (struct timerlist *timerlist,
	void (*timer_fn) (void *data),
	void *data,
	unsigned long long nano_from_epoch,
	timer_handle *handle)
{
	struct timerlist_timer *timer;

	timer = (struct timerlist_timer *)malloc (sizeof (struct timerlist_timer));
	if (timer == 0) {
		errno = ENOMEM;
		return (-1);
	}
	
	timer->nano_from_epoch = nano_from_epoch;
	timer->data = data;
	timer->timer_fn = timer_fn;
	timer->handle_addr = handle;
	timerlist_add (timerlist, timer);

	*handle = timer;
	return (0);
}

static inline int timerlist_add_duration (struct timerlist *timerlist,
	void (*timer_fn) (void *data),
	void *data,
	unsigned long long nano_duration,
	timer_handle *handle)
{
	struct timerlist_timer *timer;

	timer = (struct timerlist_timer *)malloc (sizeof (struct timerlist_timer));
	if (timer == 0) {
		errno = ENOMEM;
		return (-1);
	}
	
	timer->nano_from_epoch = timerlist_nano_from_epoch() + nano_duration;
	timer->data = data;
	timer->timer_fn = timer_fn;
	timer->handle_addr = handle;
	timerlist_add (timerlist, timer);

	*handle = timer;
	return (0);
}

static inline void timerlist_del (struct timerlist *timerlist, timer_handle timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)timer_handle;

	memset (timer->handle_addr, 0, sizeof (struct timerlist_timer *));
	/*
	 * If the next timer after the currently expiring timer because
	 * timerlist_del is called from a timer handler, get to the next
	 * timer
	 */
	if (timerlist->timer_iter == &timer->list) {
		timerlist->timer_iter = timerlist->timer_iter->next;
	}
	list_del (&timer->list);
	list_init (&timer->list);
	free (timer);
}

static inline void timerlist_pre_dispatch (struct timerlist *timerlist, timer_handle timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)timer_handle;

	memset (timer->handle_addr, 0, sizeof (struct timerlist_timer *));
	list_del (&timer->list);
	list_init (&timer->list);
}

static inline void timerlist_post_dispatch (struct timerlist *timerlist, timer_handle timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)timer_handle;

	free (timer);
}

/*
 * returns the number of msec until the next timer will expire for use with poll
 */
static inline unsigned long long timerlist_msec_duration_to_expire (struct timerlist *timerlist)
{
	struct timerlist_timer *timer_from_list;
	volatile unsigned long long nano_from_epoch;
	volatile unsigned long long msec_duration_to_expire;

	/*
	 * empty list, no expire
	 */
	if (timerlist->timer_head.next == &timerlist->timer_head) {
		return (-1);
	}
	
	timer_from_list = list_entry (timerlist->timer_head.next,
		struct timerlist_timer, list);

	nano_from_epoch = timerlist_nano_from_epoch();

	/*
	 * timer at head of list is expired, zero msecs required
	 */
	if (timer_from_list->nano_from_epoch < nano_from_epoch) {
		return (0);
	}

	
	msec_duration_to_expire = ((timer_from_list->nano_from_epoch - nano_from_epoch) / 1000000ULL) +
		(1000 / HZ);
	return (msec_duration_to_expire);
}

/*
 * Expires any timers that should be expired
 */
static inline void timerlist_expire (struct timerlist *timerlist)
{
	struct timerlist_timer *timer_from_list;
	unsigned long long nano_from_epoch;

	nano_from_epoch = timerlist_nano_from_epoch();

	for (timerlist->timer_iter = timerlist->timer_head.next;
		timerlist->timer_iter != &timerlist->timer_head;) {

		timer_from_list = list_entry (timerlist->timer_iter,
			struct timerlist_timer, list);

		if (timer_from_list->nano_from_epoch < nano_from_epoch) {
			timerlist->timer_iter = timerlist->timer_iter->next;

			timerlist_pre_dispatch (timerlist, timer_from_list);

			timer_from_list->timer_fn (timer_from_list->data);

			timerlist_post_dispatch (timerlist, timer_from_list);
		} else {
			break; /* for timer iteration */
		}
	}
	timerlist->timer_iter = 0;
}
#endif /* TLIST_H_DEFINED */
