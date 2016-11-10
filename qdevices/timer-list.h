/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#ifndef _TIMER_LIST_H_
#define _TIMER_LIST_H_

#include <sys/queue.h>

#include <nspr.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PR Interval is 32-bit integer which overflows. Maximum useable interval is around
 * 6 hours (less). So define max interval as 5 hours
 */
#define TIMER_LIST_MAX_INTERVAL			18000000

typedef int (*timer_list_cb_fn)(void *data1, void *data2);

struct timer_list_entry {
	/* Time when timer was planned */
	PRIntervalTime epoch;
	/* Number of miliseconds to expire */
	PRUint32 interval;
	/* Time when timer expires (epoch + interval) */
	PRIntervalTime expire_time;
	timer_list_cb_fn func;
	void *user_data1;
	void *user_data2;
	int is_active;
	TAILQ_ENTRY(timer_list_entry) entries;
};

struct timer_list {
	TAILQ_HEAD(, timer_list_entry) list;
	TAILQ_HEAD(, timer_list_entry) free_list;
};

extern void				 timer_list_init(struct timer_list *tlist);

extern struct timer_list_entry		*timer_list_add(struct timer_list *tlist,
    PRUint32 interval, timer_list_cb_fn func, void *data1, void *data2);

extern void				 timer_list_reschedule(struct timer_list *tlist,
    struct timer_list_entry *entry);

extern void				 timer_list_delete(struct timer_list *tlist,
    struct timer_list_entry *entry);

extern void				 timer_list_expire(struct timer_list *tlist);

extern PRIntervalTime			 timer_list_time_to_expire(struct timer_list *tlist);

extern uint32_t				 timer_list_time_to_expire_ms(struct timer_list *tlist);

extern void				 timer_list_free(struct timer_list *tlist);

#ifdef __cplusplus
}
#endif

#endif /* _TIMER_LIST_H_ */
