/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../include/list.h"
#include "mempool.h"

int mempool_bytes = 0;

struct mempool_list {
	struct list_head free;
	short free_entries;
	short used_entries;
};

struct mempool_entry {
	struct list_head list;
	int mempool_entry;
	char mem[0];
};

struct mempool_list mempool_group[MEMPOOL_GROUP_SIZE];

#ifdef MEMPOOL_ON
int mempool_init (int pool_sizes[MEMPOOL_GROUP_SIZE])
{
	int i, j;
	struct mempool_entry *entry;
	void *mempool;
	char *p;
	int bytes_to_alloc;

	for (i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
		for (j = 0; j < pool_sizes[i]; j++) {
			bytes_to_alloc = sizeof (struct mempool_entry) + (1 << i) + 3;
			bytes_to_alloc &= 0xFFFFFFFC;
			mempool_bytes += bytes_to_alloc;
		}
	}
	mempool = malloc (mempool_bytes);
	if (mempool == 0) {
		return (ENOMEM);
	}
	memset (mempool, 0, mempool_bytes);

	for (p = (char *)mempool, i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
		list_init (&mempool_group[i].free);
		mempool_group[i].free_entries = pool_sizes[i];
		mempool_group[i].used_entries = 0;
		
		for (j = 0; j < pool_sizes[i]; j++) {
			entry = (struct mempool_entry *)p;

			entry->mempool_entry = i;
			list_add (&entry->list, &mempool_group[i].free);

			bytes_to_alloc = sizeof (struct mempool_entry) + (1 << i) + 3;
			bytes_to_alloc &= 0xFFFFFFFC;
			p += bytes_to_alloc;
		}
	}

	return (0);
}

void *mempool_malloc (size_t size)
{
	struct mempool_entry *mempool_entry;
	int i;
#ifdef DEBUG
	int first = 0;

	int stats_inuse[MEMPOOL_GROUP_SIZE];
	int stats_avail[MEMPOOL_GROUP_SIZE];
	int stats_memoryused[MEMPOOL_GROUP_SIZE];
#endif

	for (i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
#ifdef DEBUG
		if (((i << 1) >= size) && first == 0) {
			first = i;
		}
#endif

		if (((1 << i) >= size) &&
			mempool_group[i].free_entries) {
	
			mempool_group[i].used_entries += 1;
			mempool_group[i].free_entries -= 1;
			mempool_entry = list_entry (mempool_group[i].free.next,
				struct mempool_entry, list);
			list_del (mempool_group[i].free.next);
			return (&mempool_entry->mem);
		}
	}

#ifdef DEBUG
	mempool_getstats (stats_inuse, stats_avail, stats_memoryused);
	printf ("MEMORY POOLS first %d %d:\n", first, size);
	for (i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
	printf ("order %d size %d inuse %d avail %d memory used %d\n",
		i, 1<<i, stats_inuse[i], stats_avail[i], stats_memoryused[i]);
	}
#endif
	return (0);
}

void mempool_free (void *ptr) {
	struct mempool_entry *mempool_entry;

	mempool_entry = ((struct mempool_entry *)((unsigned long)(ptr) - (unsigned long)(&((struct mempool_entry *)0)->mem)));

	mempool_group[mempool_entry->mempool_entry].free_entries += 1;
	mempool_group[mempool_entry->mempool_entry].used_entries -= 1;
	list_add (&mempool_entry->list, &mempool_group[mempool_entry->mempool_entry].free);
}

void *mempool_realloc (void *ptr, size_t size) {
	struct mempool_entry *mempool_entry;
	void *new_ptr;

	mempool_entry = ((struct mempool_entry *)((unsigned long)(ptr) - (unsigned long)(&((struct mempool_entry *)0)->mem)));
	
	if (ptr == 0 || (1 << mempool_entry->mempool_entry) < size) {
		/*
		 * Must grow allocated block, copy memory, free old block
		 */
		new_ptr = (void *)mempool_malloc (size);
		if (new_ptr == 0) {
			return (0);
		}
		if (ptr) {
			memcpy (new_ptr, ptr, (1 << mempool_entry->mempool_entry));
			mempool_free (ptr);
		}
		ptr = new_ptr;
	}

	return (ptr);
}

char *mempool_strdup (const char *s)
{
	char *mem;

	mem = mempool_malloc (strlen (s));
	strcpy (mem, s);
	return (mem);
}

void mempool_getstats (
	int stats_inuse[MEMPOOL_GROUP_SIZE],
	int stats_avail[MEMPOOL_GROUP_SIZE],
	int stats_memoryused[MEMPOOL_GROUP_SIZE])
{
	int i;

	for (i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
		stats_inuse[i] = mempool_group[i].used_entries;
		stats_avail[i] = mempool_group[i].free_entries;
		stats_memoryused[i] = (mempool_group[i].used_entries + mempool_group[i].free_entries) * (sizeof (struct mempool_entry) + (1<<i));
	}
}
#else /* MEMPOOL_ON NOT SET */
int mempool_init (int pool_sizes[MEMPOOL_GROUP_SIZE]) {
	return (0);
}

void *mempool_malloc (size_t size) {
	return (malloc (size));
}
void mempool_free (void *ptr) {
	free (ptr);
}

void *mempool_realloc (void *ptr, size_t size) {
	return (realloc (ptr, size));
}

char *mempool_strdup (const char *s) {
	return (strdup (s));
}
void mempool_getstats (
	int stats_inuse[MEMPOOL_GROUP_SIZE],
	int stats_avail[MEMPOOL_GROUP_SIZE],
	int stats_memoryused[MEMPOOL_GROUP_SIZE]) {

	return;
}
#endif
