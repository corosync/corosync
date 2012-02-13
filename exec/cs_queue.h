/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2011 Red Hat, Inc.
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
#ifndef CS_QUEUE_H_DEFINED
#define CS_QUEUE_H_DEFINED

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include "assert.h"

struct cs_queue {
	int head;
	int tail;
	int used;
	int usedhw;
	int size;
	void *items;
	int size_per_item;
	int iterator;
	pthread_mutex_t mutex;
	int threaded_mode_enabled;
};

static inline int cs_queue_init (struct cs_queue *cs_queue, int cs_queue_items, int size_per_item, int threaded_mode_enabled) {
	cs_queue->head = 0;
	cs_queue->tail = cs_queue_items - 1;
	cs_queue->used = 0;
	cs_queue->usedhw = 0;
	cs_queue->size = cs_queue_items;
	cs_queue->size_per_item = size_per_item;
	cs_queue->threaded_mode_enabled = threaded_mode_enabled;

	cs_queue->items = malloc (cs_queue_items * size_per_item);
	if (cs_queue->items == 0) {
		return (-ENOMEM);
	}
	memset (cs_queue->items, 0, cs_queue_items * size_per_item);
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_init (&cs_queue->mutex, NULL);
	}
	return (0);
}

static inline int cs_queue_reinit (struct cs_queue *cs_queue)
{
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue->head = 0;
	cs_queue->tail = cs_queue->size - 1;
	cs_queue->used = 0;
	cs_queue->usedhw = 0;

	memset (cs_queue->items, 0, cs_queue->size * cs_queue->size_per_item);
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
	return (0);
}

static inline void cs_queue_free (struct cs_queue *cs_queue) {
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_destroy (&cs_queue->mutex);
	}
	free (cs_queue->items);
}

static inline int cs_queue_is_full (struct cs_queue *cs_queue) {
	int full;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	full = ((cs_queue->size - 1) == cs_queue->used);
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
	return (full);
}

static inline int cs_queue_is_empty (struct cs_queue *cs_queue) {
	int empty;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	empty = (cs_queue->used == 0);
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
	return (empty);
}

static inline void cs_queue_item_add (struct cs_queue *cs_queue, void *item)
{
	char *cs_queue_item;
	int cs_queue_position;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue_position = cs_queue->head;
	cs_queue_item = cs_queue->items;
	cs_queue_item += cs_queue_position * cs_queue->size_per_item;
	memcpy (cs_queue_item, item, cs_queue->size_per_item);

	assert (cs_queue->tail != cs_queue->head);

	cs_queue->head = (cs_queue->head + 1) % cs_queue->size;
	cs_queue->used++;
	if (cs_queue->used > cs_queue->usedhw) {
		cs_queue->usedhw = cs_queue->used;
	}
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
}

static inline void *cs_queue_item_get (struct cs_queue *cs_queue)
{
	char *cs_queue_item;
	int cs_queue_position;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue_position = (cs_queue->tail + 1) % cs_queue->size;
	cs_queue_item = cs_queue->items;
	cs_queue_item += cs_queue_position * cs_queue->size_per_item;
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
	return ((void *)cs_queue_item);
}

static inline void cs_queue_item_remove (struct cs_queue *cs_queue) {
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue->tail = (cs_queue->tail + 1) % cs_queue->size;

	assert (cs_queue->tail != cs_queue->head);

	cs_queue->used--;
	assert (cs_queue->used >= 0);
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
}

static inline void cs_queue_items_remove (struct cs_queue *cs_queue, int rel_count)
{
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue->tail = (cs_queue->tail + rel_count) % cs_queue->size;

	assert (cs_queue->tail != cs_queue->head);

	cs_queue->used -= rel_count;
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
}


static inline void cs_queue_item_iterator_init (struct cs_queue *cs_queue)
{
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue->iterator = (cs_queue->tail + 1) % cs_queue->size;
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
}

static inline void *cs_queue_item_iterator_get (struct cs_queue *cs_queue)
{
	char *cs_queue_item;
	int cs_queue_position;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue_position = (cs_queue->iterator) % cs_queue->size;
	if (cs_queue->iterator == cs_queue->head) {
		if (cs_queue->threaded_mode_enabled) {
			pthread_mutex_unlock (&cs_queue->mutex);
		}
		return (0);
	}
	cs_queue_item = cs_queue->items;
	cs_queue_item += cs_queue_position * cs_queue->size_per_item;
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
	return ((void *)cs_queue_item);
}

static inline int cs_queue_item_iterator_next (struct cs_queue *cs_queue)
{
	int next_res;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	cs_queue->iterator = (cs_queue->iterator + 1) % cs_queue->size;

	next_res = cs_queue->iterator == cs_queue->head;
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
	return (next_res);
}

static inline void cs_queue_avail (struct cs_queue *cs_queue, int *avail)
{
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	*avail = cs_queue->size - cs_queue->used - 2;
	assert (*avail >= 0);
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}
}

static inline int cs_queue_used (struct cs_queue *cs_queue) {
	int used;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}
	used = cs_queue->used;
	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}

	return (used);
}

static inline int cs_queue_usedhw (struct cs_queue *cs_queue) {
	int usedhw;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_lock (&cs_queue->mutex);
	}

	usedhw = cs_queue->usedhw;

	if (cs_queue->threaded_mode_enabled) {
		pthread_mutex_unlock (&cs_queue->mutex);
	}

	return (usedhw);
}

#endif /* CS_QUEUE_H_DEFINED */
