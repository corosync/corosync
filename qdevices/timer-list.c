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

#include <string.h>

#include "timer-list.h"

void
timer_list_init(struct timer_list *tlist)
{

	memset(tlist, 0, sizeof(*tlist));

	TAILQ_INIT(&tlist->list);
	TAILQ_INIT(&tlist->free_list);
}

static PRIntervalTime
timer_list_entry_time_to_expire(const struct timer_list_entry *entry, PRIntervalTime current_time)
{
	PRIntervalTime diff, half_interval;

	diff = entry->expire_time - current_time;
	half_interval = ~0;
	half_interval /= 2;

	if (diff > half_interval) {
		return (0);
	}

	return (diff);
}

static int
timer_list_entry_cmp(const struct timer_list_entry *entry1,
    const struct timer_list_entry *entry2, PRIntervalTime current_time)
{
	PRIntervalTime diff1, diff2;
	int res;

	diff1 = timer_list_entry_time_to_expire(entry1, current_time);
	diff2 = timer_list_entry_time_to_expire(entry2, current_time);

	res = 0;

	if (diff1 < diff2) res = -1;
	if (diff1 > diff2) res = 1;

	return (res);
}

static void
timer_list_insert_into_list(struct timer_list *tlist, struct timer_list_entry *new_entry)
{
	struct timer_list_entry *entry;

	/*
	 * This can overflow and it's not a problem
	 */
	new_entry->expire_time = new_entry->epoch + PR_MillisecondsToInterval(new_entry->interval);

	entry = TAILQ_FIRST(&tlist->list);
	while (entry != NULL) {
		if (timer_list_entry_cmp(entry, new_entry, new_entry->epoch) > 0) {
			/*
			 * Insert new entry right before current entry
			 */
			TAILQ_INSERT_BEFORE(entry, new_entry, entries);

			break;
		}

		entry = TAILQ_NEXT(entry, entries);
	}

	if (entry == NULL) {
		TAILQ_INSERT_TAIL(&tlist->list, new_entry, entries);
	}
}

struct timer_list_entry *
timer_list_add(struct timer_list *tlist, PRUint32 interval, timer_list_cb_fn func, void *data1,
    void *data2)
{
	struct timer_list_entry *new_entry;

	if (interval < 1 || interval > TIMER_LIST_MAX_INTERVAL) {
		return (NULL);
	}

	if (!TAILQ_EMPTY(&tlist->free_list)) {
		/*
		 * Use free list entry
		 */
		new_entry = TAILQ_FIRST(&tlist->free_list);
		TAILQ_REMOVE(&tlist->free_list, new_entry, entries);
	} else {
		/*
		 * Alloc new entry
		 */
		new_entry = malloc(sizeof(*new_entry));
		if (new_entry == NULL) {
			return (NULL);
		}
	}

	memset(new_entry, 0, sizeof(*new_entry));
	new_entry->epoch = PR_IntervalNow();
	new_entry->interval = interval;
	new_entry->func = func;
	new_entry->user_data1 = data1;
	new_entry->user_data2 = data2;
	new_entry->is_active = 1;

	timer_list_insert_into_list(tlist, new_entry);

	return (new_entry);
}

void
timer_list_reschedule(struct timer_list *tlist, struct timer_list_entry *entry)
{

	if (entry->is_active) {
		entry->epoch = PR_IntervalNow();
		TAILQ_REMOVE(&tlist->list, entry, entries);
		timer_list_insert_into_list(tlist, entry);
	}
}

void
timer_list_expire(struct timer_list *tlist)
{
	PRIntervalTime now;
	struct timer_list_entry *entry;
	int res;

	now = PR_IntervalNow();

	while ((entry = TAILQ_FIRST(&tlist->list)) != NULL &&
	    timer_list_entry_time_to_expire(entry, now) == 0) {
		/*
		 * Expired
		 */
		res = entry->func(entry->user_data1, entry->user_data2);
		if (res == 0) {
			/*
			 * Move item to free list
			 */
			timer_list_delete(tlist, entry);
		} else if (entry->is_active) {
			/*
			 * Schedule again
			 */
			entry->epoch = now;
			TAILQ_REMOVE(&tlist->list, entry, entries);
			timer_list_insert_into_list(tlist, entry);
		}
	}
}

PRIntervalTime
timer_list_time_to_expire(struct timer_list *tlist)
{
	struct timer_list_entry *entry;

	entry = TAILQ_FIRST(&tlist->list);
	if (entry == NULL) {
		return (PR_INTERVAL_NO_TIMEOUT);
	}

	return (timer_list_entry_time_to_expire(entry, PR_IntervalNow()));
}

uint32_t
timer_list_time_to_expire_ms(struct timer_list *tlist)
{
	struct timer_list_entry *entry;
	uint32_t u32;

	entry = TAILQ_FIRST(&tlist->list);
	if (entry == NULL) {
		u32 = ~((uint32_t)0);
		return (u32);
	}

	return (PR_IntervalToMilliseconds(timer_list_entry_time_to_expire(entry, PR_IntervalNow())));
}

void
timer_list_delete(struct timer_list *tlist, struct timer_list_entry *entry)
{

	if (entry->is_active) {
		/*
		 * Move item to free list
		 */
		TAILQ_REMOVE(&tlist->list, entry, entries);
		TAILQ_INSERT_HEAD(&tlist->free_list, entry, entries);
		entry->is_active = 0;
	}
}

void
timer_list_free(struct timer_list *tlist)
{
	struct timer_list_entry *entry;
	struct timer_list_entry *entry_next;


	entry = TAILQ_FIRST(&tlist->list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		free(entry);

		entry = entry_next;
	}

	entry = TAILQ_FIRST(&tlist->free_list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		free(entry);

		entry = entry_next;
	}

	timer_list_init(tlist);
}
