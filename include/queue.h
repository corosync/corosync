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
#include "assert.h"

struct queue {
	int head;
	int tail;
	int used;
	int usedhw;
	int size;
	void *items;
	int size_per_item;
	int iterator;
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
	return (0);
}

static inline int queue_reinit (struct queue *queue)
{
	queue->head = 0;
	queue->tail = queue->size - 1;
	queue->used = 0;
	queue->usedhw = 0;

	memset (queue->items, 0, queue->size * queue->size_per_item);
	return (0);
}

static inline void queue_free (struct queue *queue) {
	free (queue->items);
}

static inline int queue_is_full (struct queue *queue) {
	return (queue->size - 1 == queue->used);
}

static inline int queue_is_empty (struct queue *queue) {
	return (queue->used == 0);
}

static inline void queue_item_add (struct queue *queue, void *item)
{
	char *queue_item;
	int queue_position;

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
}

static inline void *queue_item_get (struct queue *queue)
{
	char *queue_item;
	int queue_position;

	queue_position = (queue->tail + 1) % queue->size;
	queue_item = queue->items;
	queue_item += queue_position * queue->size_per_item;
	return ((void *)queue_item);
}

static inline void queue_item_remove (struct queue *queue) {
	queue->tail = (queue->tail + 1) % queue->size;
	
	assert (queue->tail != queue->head);

	queue->used--;
	assert (queue->used >= 0);
}

static inline void queue_items_remove (struct queue *queue, int rel_count)
{
	queue->tail = (queue->tail + rel_count) % queue->size;
	
	assert (queue->tail != queue->head);

	queue->used -= rel_count;
}


static inline void queue_item_iterator_init (struct queue *queue)
{
	queue->iterator = (queue->tail + 1) % queue->size;
}

static inline void *queue_item_iterator_get (struct queue *queue)
{
	char *queue_item;
	int queue_position;

	queue_position = (queue->iterator) % queue->size;
	if (queue->iterator == queue->head) {
		return (0);
	}
	queue_item = queue->items;
	queue_item += queue_position * queue->size_per_item;
	return ((void *)queue_item);
}

static inline int queue_item_iterator_next (struct queue *queue)
{
	queue->iterator = (queue->iterator + 1) % queue->size;

	return (queue->iterator == queue->head);
}

static inline void queue_avail (struct queue *queue, int *avail)
{
	*avail = queue->size - queue->used - 2;
	assert (*avail >= 0);
}

#endif /* QUEUE_H_DEFINED */
