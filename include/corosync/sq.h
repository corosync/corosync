/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
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
#ifndef SORTQUEUE_H_DEFINED
#define SORTQUEUE_H_DEFINED

#include <errno.h>
#include <string.h>

/**
 * @brief The sq struct
 */
struct sq {
	unsigned int head;
	unsigned int size;
	void *items;
	unsigned int *items_inuse;
	unsigned int *items_miss_count;
	unsigned int size_per_item;
	unsigned int head_seqid;
	unsigned int item_count;
	unsigned int pos_max;
};

/*
 * Compare a unsigned rollover-safe value to an unsigned rollover-safe value
 */

/**
 * ADJUST_ROLLOVER_POINT is the value used to determine when a window should be
 *	used to calculate a less-then or less-then-equal comparison.
 */
#define ADJUST_ROLLOVER_POINT 0x80000000

/**
 * ADJUST_ROLLOVER_VALUE is the value by which both values in a comparison are
 *	adjusted if either value in a comparison is greater then
 *	ADJUST_ROLLOVER_POINT.
 */
#define ADJUST_ROLLOVER_VALUE 0x10000

/**
 * @brief sq_lt_compare
 * @param a
 * @param b
 * @return
 */
static inline int sq_lt_compare (unsigned int a, unsigned int b) {
	if ((a > ADJUST_ROLLOVER_POINT) || (b > ADJUST_ROLLOVER_POINT)) {
		if ((a - ADJUST_ROLLOVER_VALUE) < (b - ADJUST_ROLLOVER_VALUE)) {
			return (1);
		}
	} else {
		if (a < b) {
			return (1);
		}
	}
	return (0);
}

/**
 * @brief sq_lte_compare
 * @param a
 * @param b
 * @return
 */
static inline int sq_lte_compare (unsigned int a, unsigned int b) {
	if ((a > ADJUST_ROLLOVER_POINT) || (b > ADJUST_ROLLOVER_POINT)) {
		if ((a - ADJUST_ROLLOVER_VALUE) <= (b - ADJUST_ROLLOVER_VALUE)) {
			return (1);
		}
	} else {
		if (a <= b) {
			return (1);
		}
	}
	return (0);
}

/**
 * @brief sq_init
 * @param sq
 * @param item_count
 * @param size_per_item
 * @param head_seqid
 * @return
 */
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

	sq->items = malloc (item_count * size_per_item);
	if (sq->items == NULL) {
		return (-ENOMEM);
	}
	memset (sq->items, 0, item_count * size_per_item);

	if ((sq->items_inuse = malloc (item_count * sizeof (unsigned int)))
	    == NULL) {
		return (-ENOMEM);
	}
	if ((sq->items_miss_count = malloc (item_count * sizeof (unsigned int)))
	    == NULL) {
		return (-ENOMEM);
	}
	memset (sq->items_inuse, 0, item_count * sizeof (unsigned int));
	memset (sq->items_miss_count, 0, item_count * sizeof (unsigned int));
	return (0);
}

/**
 * @brief sq_reinit
 * @param sq
 * @param head_seqid
 */
static inline void sq_reinit (struct sq *sq, unsigned int head_seqid)
{
	sq->head = 0;
	sq->head_seqid = head_seqid;
	sq->pos_max = 0;

	memset (sq->items, 0, sq->item_count * sq->size_per_item);
	memset (sq->items_inuse, 0, sq->item_count * sizeof (unsigned int));
	memset (sq->items_miss_count, 0, sq->item_count * sizeof (unsigned int));
}

/**
 * @brief sq_assert
 * @param sq
 * @param pos
 */
static inline void sq_assert (const struct sq *sq, unsigned int pos)
{
	unsigned int i;

//	printf ("Instrument[%d] Asserting from %d to %d\n",
//		pos, sq->pos_max, sq->size);
	for (i = sq->pos_max + 1; i < sq->size; i++) {
		assert (sq->items_inuse[i] == 0);
	}
}

/**
 * @brief sq_copy
 * @param sq_dest
 * @param sq_src
 */
static inline void sq_copy (struct sq *sq_dest, const struct sq *sq_src)
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
		sq_src->item_count * sizeof (unsigned int));
	memcpy (sq_dest->items_miss_count, sq_src->items_miss_count,
		sq_src->item_count * sizeof (unsigned int));
}

/**
 * @brief sq_free
 * @param sq
 */
static inline void sq_free (struct sq *sq) {
	free (sq->items);
	free (sq->items_inuse);
	free (sq->items_miss_count);
}

/**
 * @brief sq_item_add
 * @param sq
 * @param item
 * @param seqid
 * @return
 */
static inline void *sq_item_add (
	struct sq *sq,
	void *item,
	unsigned int seqid)
{
	char *sq_item;
	unsigned int sq_position;

	sq_position = (sq->head + seqid - sq->head_seqid) % sq->size;
	if (sq_position > sq->pos_max) {
		sq->pos_max = sq_position;
	}

	sq_item = sq->items;
	sq_item += sq_position * sq->size_per_item;
	assert(sq->items_inuse[sq_position] == 0);
	memcpy (sq_item, item, sq->size_per_item);
	if (seqid == 0) {
		sq->items_inuse[sq_position] = 1;
	} else {
		sq->items_inuse[sq_position] = seqid;
	}
	sq->items_miss_count[sq_position] = 0;

	return (sq_item);
}

/**
 * @brief sq_item_inuse
 * @param sq
 * @param seq_id
 * @return
 */
static inline unsigned int sq_item_inuse (
	const struct sq *sq,
	unsigned int seq_id) {

	unsigned int sq_position;

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
	return (sq->items_inuse[sq_position] != 0);
}

/**
 * @brief sq_item_miss_count
 * @param sq
 * @param seq_id
 * @return
 */
static inline unsigned int sq_item_miss_count (
	const struct sq *sq,
	unsigned int seq_id)
{
	unsigned int sq_position;

	sq_position = (sq->head - sq->head_seqid + seq_id) % sq->size;
	sq->items_miss_count[sq_position]++;
	return (sq->items_miss_count[sq_position]);
}

/**
 * @brief sq_size_get
 * @param sq
 * @return
 */
static inline unsigned int sq_size_get (
	const struct sq *sq)
{
	return sq->size;
}

/**
 * @brief sq_in_range
 * @param sq
 * @param seq_id
 * @return
 */
static inline unsigned int sq_in_range (
	const struct sq *sq,
	unsigned int seq_id)
{
	int res = 1;

	if (sq->head_seqid > ADJUST_ROLLOVER_POINT) {
		if (seq_id - ADJUST_ROLLOVER_VALUE <
			sq->head_seqid - ADJUST_ROLLOVER_VALUE) {

			res = 0;
		}
		if ((seq_id - ADJUST_ROLLOVER_VALUE) >=
			((sq->head_seqid - ADJUST_ROLLOVER_VALUE) + sq->size)) {

			res = 0;
		}
	} else {
		if (seq_id < sq->head_seqid) {
			res = 0;
		}
		if ((seq_id) >= ((sq->head_seqid) + sq->size)) {
			res = 0;
		}
	}
	return (res);

}

/**
 * @brief sq_item_get
 * @param sq
 * @param seq_id
 * @param sq_item_out
 * @return
 */
static inline unsigned int sq_item_get (
	const struct sq *sq,
	unsigned int seq_id,
	void **sq_item_out)
{
	char *sq_item;
	unsigned int sq_position;

	if (seq_id > ADJUST_ROLLOVER_POINT) {
		assert ((seq_id - ADJUST_ROLLOVER_POINT) <
			((sq->head_seqid - ADJUST_ROLLOVER_POINT) + sq->size));

		sq_position = ((sq->head - ADJUST_ROLLOVER_VALUE) -
			(sq->head_seqid - ADJUST_ROLLOVER_VALUE) + seq_id) % sq->size;
	} else {
		assert (seq_id < (sq->head_seqid + sq->size));
		sq_position = (sq->head - sq->head_seqid + seq_id) % sq->size;
	}
//printf ("seqid %x head %x head %x pos %x\n", seq_id, sq->head, sq->head_seqid, sq_position);
//	sq_position = (sq->head - sq->head_seqid + seq_id) % sq->size;
//printf ("sq_position = %x\n", sq_position);
//printf ("ITEMGET %d %d %d %d\n", sq_position, sq->head, sq->head_seqid, seq_id);
	if (sq->items_inuse[sq_position] == 0) {
		return (ENOENT);
	}
	sq_item = sq->items;
	sq_item += sq_position * sq->size_per_item;
	*sq_item_out = sq_item;
	return (0);
}

/**
 * @brief sq_items_release
 * @param sq
 * @param seqid
 */
static inline void sq_items_release (struct sq *sq, unsigned int seqid)
{
	unsigned int oldhead;

	oldhead = sq->head;

	sq->head = (sq->head + seqid - sq->head_seqid + 1) % sq->size;
	if ((oldhead + seqid - sq->head_seqid + 1) > sq->size) {
//		printf ("releasing %d for %d\n", oldhead, sq->size - oldhead);
//		printf ("releasing %d for %d\n", 0, sq->head);
		memset (&sq->items_inuse[oldhead], 0, (sq->size - oldhead) * sizeof (unsigned int));
		memset (sq->items_inuse, 0, sq->head * sizeof (unsigned int));
	} else {
//		printf ("releasing %d for %d\n", oldhead, seqid - sq->head_seqid + 1);
		memset (&sq->items_inuse[oldhead], 0,
			(seqid - sq->head_seqid + 1) * sizeof (unsigned int));
		memset (&sq->items_miss_count[oldhead], 0,
			(seqid - sq->head_seqid + 1) * sizeof (unsigned int));
	}
	sq->head_seqid = seqid + 1;
}

#endif /* SORTQUEUE_H_DEFINED */
