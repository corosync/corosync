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
#ifndef SORTQUEUE_H_DEFINED
#define SORTQUEUE_H_DEFINED

#include "errno.h"

struct sq {
	int head;
	int size;
	void *items;
	unsigned char *items_inuse;
	int size_per_item;
	int head_seqid;
	int item_count;
	int pos_max;
};

static inline int sq_init (
	struct sq *sq,
	int item_count,
	int size_per_item,
	int head_seqid)
{
	sq->head = 0;
	sq->size = item_count;
	sq->size_per_item = size_per_item;
	sq->head_seqid = head_seqid;
	sq->item_count = item_count;
	sq->pos_max = 0;

	sq->items = (void *)malloc (item_count * size_per_item);
	if (sq->items == 0) {
		return (-ENOMEM);
	}
	memset (sq->items, 0, item_count * size_per_item);

	sq->items_inuse = (void *)malloc (item_count * sizeof (char));
	memset (sq->items_inuse, 0, item_count * sizeof (char));
	return (0);
}

static inline void sq_reinit (struct sq *sq, int head_seqid)
{
	sq->head = 0;
	sq->head_seqid = head_seqid;
	sq->pos_max = 0;

	memset (sq->items, 0, sq->item_count * sq->size_per_item);
	memset (sq->items_inuse, 0, sq->item_count * sizeof (char));
}

static inline void sq_assert (struct sq *sq, int pos)
{
	int i;

//	printf ("Instrument[%d] Asserting from %d to %d\n",
//		pos, sq->pos_max, sq->size);
	for (i = sq->pos_max + 1; i < sq->size; i++) {
		assert (sq->items_inuse[i] == 0);
	}
}
static inline void sq_copy (struct sq *sq_dest, struct sq *sq_src)
{
	sq_assert (sq_src, 20);
	sq_dest->head = sq_src->head;
	sq_dest->size = sq_src->item_count;
	sq_dest->size_per_item = sq_src->size_per_item;
	sq_dest->head_seqid = sq_src->head_seqid;
	sq_dest->item_count = sq_src->item_count;
	sq_dest->pos_max = sq_src->pos_max;
	memcpy (sq_dest->items, sq_src->items,
		sq_src->item_count * sq_src->size_per_item);
	memcpy (sq_dest->items_inuse, sq_src->items_inuse,
		sq_src->item_count * sizeof (char));
}

static inline void sq_free (struct sq *sq) {
	free (sq->items);
	free (sq->items_inuse);
}

static inline int sq_item_add (
	struct sq *sq,
	void *item,
	int seqid)
{
	char *sq_item;
	int sq_position;

	if (seqid - sq->head_seqid >= sq->size) {
		return E2BIG;
	}
	sq_position = (sq->head + seqid - sq->head_seqid) % sq->size;
	if (sq_position > sq->pos_max) {
		sq->pos_max = sq_position;
	}
	assert (sq_position >= 0);

//printf ("item add position %d seqid %d head seqid %d\n", sq_position, seqid, sq->head_seqid);
	sq_item = sq->items;
	sq_item += sq_position * sq->size_per_item;
	assert(sq->items_inuse[sq_position] == 0);
	memcpy (sq_item, item, sq->size_per_item);
	sq->items_inuse[sq_position] = 1;

	return (0);
}

static inline int sq_item_inuse (
	struct sq *sq,
	int seq_id) {

	int sq_position;

	/*
	 * We need to say that the seqid is in use if it shouldn't 
	 * be here in the first place.
	 * To keep old messages from being inserted.
	 */
#ifdef COMPILE_OUT
	if (seq_id < sq->head_seqid) {
		fprintf(stderr, "sq_item_inuse: seqid %d, head %d\n", 
						seq_id, sq->head_seqid);
		return 1;
	}
#endif
	sq_position = (sq->head - sq->head_seqid + seq_id) % sq->size;
//printf ("in use %d\n", sq_position);
	return (sq->items_inuse[sq_position]);
}

static inline int sq_size_get (
	struct sq *sq)
{
	return sq->size;
}

static inline int sq_item_get (
	struct sq *sq,
	int seq_id,
	void **sq_item_out)
{
	char *sq_item;
	int sq_position;

if (seq_id == -1) {
	return (ENOENT);
}
	assert (seq_id < (sq->head_seqid + sq->size));
	sq_position = (sq->head - sq->head_seqid + seq_id) % sq->size;
//printf ("ITEMGET %d %d %d %d\n", sq_position, sq->head, sq->head_seqid, seq_id);
assert (sq_position >= 0);
//printf ("itme get in use %d\n", sq_position);
	if (sq->items_inuse[sq_position] == 0) {
//printf ("not in use %d\n", sq_position);
		return (ENOENT);
	}
	sq_item = sq->items;
	sq_item += sq_position * sq->size_per_item;
	*sq_item_out = sq_item;
	return (0);
}

static inline void sq_items_release (struct sq *sq, int seqid)
{
	int oldhead;

	if (seqid < sq->head_seqid) {
		return;
	}

	oldhead = sq->head;

	sq->head = (sq->head + seqid - sq->head_seqid + 1) % sq->size;
	if ((oldhead + seqid - sq->head_seqid + 1) > sq->size) {
//		printf ("releasing %d for %d\n", oldhead, sq->size - oldhead);
//		printf ("releasing %d for %d\n", 0, sq->head);
		memset (&sq->items_inuse[oldhead], 0, sq->size - oldhead);
		memset (sq->items_inuse, 0, sq->head * sizeof (char));
	} else {
//		printf ("releasing %d for %d\n", oldhead, seqid - sq->head_seqid + 1);
		memset (&sq->items_inuse[oldhead], 0,
			(seqid - sq->head_seqid + 1) * sizeof (char));
	}
	sq->head_seqid = seqid + 1;
}

#endif /* SORTQUEUE_H_DEFINED */
