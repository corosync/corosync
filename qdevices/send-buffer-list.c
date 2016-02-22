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
#include <stdlib.h>
#include <assert.h>

#include "send-buffer-list.h"

void
send_buffer_list_init(struct send_buffer_list *sblist, size_t max_list_entries,
    size_t max_buffer_size)
{

	memset(sblist, 0, sizeof(*sblist));

	sblist->max_list_entries = max_list_entries;
	sblist->allocated_list_entries = 0;
	sblist->max_buffer_size = max_buffer_size;
	TAILQ_INIT(&sblist->list);
	TAILQ_INIT(&sblist->free_list);
}

struct send_buffer_list_entry *
send_buffer_list_get_new(struct send_buffer_list *sblist)
{
	struct send_buffer_list_entry *entry;

	if (!TAILQ_EMPTY(&sblist->free_list)) {
		/*
		 * Use free list entry
		 */
		entry = TAILQ_FIRST(&sblist->free_list);
		TAILQ_REMOVE(&sblist->free_list, entry, entries);

		dynar_clean(&entry->buffer);
		dynar_set_max_size(&entry->buffer, sblist->max_buffer_size);
	} else {
		if (sblist->allocated_list_entries + 1 > sblist->max_list_entries) {
			return (NULL);
		}

		sblist->allocated_list_entries++;

		/*
		 * Alloc new entry
		 */
		entry = malloc(sizeof(*entry));
		if (entry == NULL) {
			return (NULL);
		}

		dynar_init(&entry->buffer, sblist->max_buffer_size);
	}

	entry->msg_already_sent_bytes = 0;

	return (entry);
}

void
send_buffer_list_put(struct send_buffer_list *sblist, struct send_buffer_list_entry *sblist_entry)
{

	TAILQ_INSERT_TAIL(&sblist->list, sblist_entry, entries);
}

void
send_buffer_list_discard_new(struct send_buffer_list *sblist, struct send_buffer_list_entry *sblist_entry)
{

	TAILQ_INSERT_HEAD(&sblist->free_list, sblist_entry, entries);
}

struct send_buffer_list_entry *
send_buffer_list_get_active(const struct send_buffer_list *sblist)
{
	struct send_buffer_list_entry *entry;

	entry = TAILQ_FIRST(&sblist->list);

	return (entry);
}

void
send_buffer_list_delete(struct send_buffer_list *sblist,
    struct send_buffer_list_entry *sblist_entry)
{

	/*
	 * Move item to free list
	 */
	TAILQ_REMOVE(&sblist->list, sblist_entry, entries);
	TAILQ_INSERT_HEAD(&sblist->free_list, sblist_entry, entries);
}

int
send_buffer_list_empty(const struct send_buffer_list *sblist)
{

	return (TAILQ_EMPTY(&sblist->list));
}

void
send_buffer_list_free(struct send_buffer_list *sblist)
{
	struct send_buffer_list_entry *entry;
	struct send_buffer_list_entry *entry_next;

	entry = TAILQ_FIRST(&sblist->list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		dynar_destroy(&entry->buffer);
		free(entry);

		entry = entry_next;
	}

	entry = TAILQ_FIRST(&sblist->free_list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		dynar_destroy(&entry->buffer);
		free(entry);

		entry = entry_next;
	}

	sblist->allocated_list_entries = 0;
	TAILQ_INIT(&sblist->list);
	TAILQ_INIT(&sblist->free_list);
}

void
send_buffer_list_set_max_buffer_size(struct send_buffer_list *sblist, size_t max_buffer_size)
{

	sblist->max_buffer_size = max_buffer_size;
}

void
send_buffer_list_set_max_list_entries(struct send_buffer_list *sblist, size_t max_list_entries)
{

	sblist->max_list_entries = max_list_entries;
}
