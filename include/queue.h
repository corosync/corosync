/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#ifndef QUEUE_H_DEFINED
#define QUEUE_H_DEFINED

#include <string.h>
#include <pthread.h>
#include "assert.h"

#ifdef OPENAIS_SOLARIS
/* struct queue is already defined in sys/stream.h on Solaris */
#define	queue _queue
#endif
struct queue {
	int head;
	int tail;
	int used;
	int usedhw;
	int size;
	void *items;
	int size_per_item;
	int iterator;
	pthread_mutex_t mutex;
};

static inline int queue_init (struct queue *queue, int queue_items, int size_per_item) {
	queue->head = 0;
	queue->tail = queue_items - 1;
	queue->used = 0;
	queue->usedhw = 0;
	queue->size = queue_items;
	queue->size_per_item = size_per_item;

	queue->items = malloc (queue_items * size_per_item);
	if (queue->items == 0) {
		return (-ENOMEM);
	}
	memset (queue->items, 0, queue_items * size_per_item);
	pthread_mutex_init (&queue->mutex, NULL);
	return (0);
}

static inline int queue_reinit (struct queue *queue)
{
	pthread_mutex_lock (&queue->mutex);
	queue->head = 0;
	queue->tail = queue->size - 1;
	queue->used = 0;
	queue->usedhw = 0;

	memset (queue->items, 0, queue->size * queue->size_per_item);
	pthread_mutex_unlock (&queue->mutex);
	return (0);
}

static inline void queue_free (struct queue *queue) {
	pthread_mutex_destroy (&queue->mutex);
	free (queue->items);
}

static inline int queue_is_full (struct queue *queue) {
	int full;

	pthread_mutex_lock (&queue->mutex);
	full = queue->size - 1 == queue->used;
	pthread_mutex_unlock (&queue->mutex);
	return (full);
}

static inline int queue_is_empty (struct queue *queue) {
	int empty;

	pthread_mutex_lock (&queue->mutex);
	empty = (queue->used == 0);
	pthread_mutex_unlock (&queue->mutex);
	return (empty);
}

static inline void queue_item_add (struct queue *queue, void *item)
{
	char *queue_item;
	int queue_position;

	pthread_mutex_lock (&queue->mutex);
	queue_position = queue->head;
	queue_item = queue->items;
	queue_item += queue_position * queue->size_per_item;
	memcpy (queue_item, item, queue->size_per_item);

	assert (queue->tail != queue->head);

	queue->head = (queue->head + 1) % queue->size;
	queue->used++;
	if (queue->used > queue->usedhw) {
		queue->usedhw = queue->used;
	}
	pthread_mutex_unlock (&queue->mutex);
}

static inline void *queue_item_get (struct queue *queue)
{
	char *queue_item;
	int queue_position;

	pthread_mutex_lock (&queue->mutex);
	queue_position = (queue->tail + 1) % queue->size;
	queue_item = queue->items;
	queue_item += queue_position * queue->size_per_item;
	pthread_mutex_unlock (&queue->mutex);
	return ((void *)queue_item);
}

static inline void queue_item_remove (struct queue *queue) {
	pthread_mutex_lock (&queue->mutex);
	queue->tail = (queue->tail + 1) % queue->size;
	
	assert (queue->tail != queue->head);

	queue->used--;
	assert (queue->used >= 0);
	pthread_mutex_unlock (&queue->mutex);
}

static inline void queue_items_remove (struct queue *queue, int rel_count)
{
	pthread_mutex_lock (&queue->mutex);
	queue->tail = (queue->tail + rel_count) % queue->size;
	
	assert (queue->tail != queue->head);

	queue->used -= rel_count;
	pthread_mutex_unlock (&queue->mutex);
}


static inline void queue_item_iterator_init (struct queue *queue)
{
	pthread_mutex_lock (&queue->mutex);
	queue->iterator = (queue->tail + 1) % queue->size;
	pthread_mutex_unlock (&queue->mutex);
}

static inline void *queue_item_iterator_get (struct queue *queue)
{
	char *queue_item;
	int queue_position;

	pthread_mutex_lock (&queue->mutex);
	queue_position = (queue->iterator) % queue->size;
	if (queue->iterator == queue->head) {
		pthread_mutex_unlock (&queue->mutex);
		return (0);
	}
	queue_item = queue->items;
	queue_item += queue_position * queue->size_per_item;
	pthread_mutex_unlock (&queue->mutex);
	return ((void *)queue_item);
}

static inline int queue_item_iterator_next (struct queue *queue)
{
	int next_res;

	pthread_mutex_lock (&queue->mutex);
	queue->iterator = (queue->iterator + 1) % queue->size;

	next_res = queue->iterator == queue->head;
	pthread_mutex_unlock (&queue->mutex);
	return (next_res);
}

static inline void queue_avail (struct queue *queue, int *avail)
{
	pthread_mutex_lock (&queue->mutex);
	*avail = queue->size - queue->used - 2;
	assert (*avail >= 0);
	pthread_mutex_unlock (&queue->mutex);
}

static inline int queue_used (struct queue *queue) {
	int used;

	pthread_mutex_lock (&queue->mutex);
	used = queue->used;
	pthread_mutex_unlock (&queue->mutex);

	return (used);
}

static inline int queue_usedhw (struct queue *queue) {
	int usedhw;

	pthread_mutex_lock (&queue->mutex);
	usedhw = queue->usedhw;
	pthread_mutex_unlock (&queue->mutex);

	return (usedhw);
}

#endif /* QUEUE_H_DEFINED */
