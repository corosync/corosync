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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "node-list.h"

void
node_list_init(struct node_list *list)
{

	TAILQ_INIT(list);
}

struct node_list_entry *
node_list_add(struct node_list *list, uint32_t node_id, uint32_t data_center_id,
    enum tlv_node_state node_state)
{
	struct node_list_entry *node;

	node = (struct node_list_entry *)malloc(sizeof(*node));
	if (node == NULL) {
		return (NULL);
	}

	memset(node, 0, sizeof(*node));

	node->node_id = node_id;
	node->data_center_id = data_center_id;
	node->node_state = node_state;

	TAILQ_INSERT_TAIL(list, node, entries);

	return (node);
}

struct node_list_entry *
node_list_add_from_node_info(struct node_list *list, const struct tlv_node_info *node_info)
{

	return (node_list_add(list, node_info->node_id, node_info->data_center_id,
	    node_info->node_state));
}

int
node_list_clone(struct node_list *dst_list, const struct node_list *src_list)
{
	struct node_list_entry *node_entry;

	node_list_init(dst_list);

	TAILQ_FOREACH(node_entry, src_list, entries) {
		if (node_list_add(dst_list, node_entry->node_id, node_entry->data_center_id,
		    node_entry->node_state) == NULL) {
			node_list_free(dst_list);

			return (-1);
		}
	}

	return (0);
}

void
node_list_entry_to_tlv_node_info(const struct node_list_entry *node,
    struct tlv_node_info *node_info)
{

	node_info->node_id = node->node_id;
	node_info->data_center_id = node->data_center_id;
	node_info->node_state = node->node_state;
}

void
node_list_free(struct node_list *list)
{
	struct node_list_entry *node;
	struct node_list_entry *node_next;

	node = TAILQ_FIRST(list);

	while (node != NULL) {
		node_next = TAILQ_NEXT(node, entries);

		free(node);

		node = node_next;
	}

	TAILQ_INIT(list);
}

void
node_list_del(struct node_list *list, struct node_list_entry *node)
{

	TAILQ_REMOVE(list, node, entries);

	free(node);
}

int
node_list_is_empty(const struct node_list *list)
{

	return (TAILQ_EMPTY(list));
}

struct node_list_entry *
node_list_find_node_id(const struct node_list *list, uint32_t node_id)
{
	struct node_list_entry *node_entry;

	TAILQ_FOREACH(node_entry, list, entries) {
		if (node_entry->node_id == node_id) {
			return (node_entry);
		}
	}

	return (NULL);
}

int
node_list_eq(const struct node_list *list1, const struct node_list *list2)
{
	struct node_list_entry *node1_entry;
	struct node_list_entry *node2_entry;
	struct node_list tmp_list;
	int res;

	res = 1;

	if (node_list_clone(&tmp_list, list2) != 0) {
		return (-1);
	}

	TAILQ_FOREACH(node1_entry, list1, entries) {
		node2_entry = node_list_find_node_id(&tmp_list, node1_entry->node_id);
		if (node2_entry == NULL) {
			res = 0;
			goto return_res;
		}

		if (node1_entry->node_id != node2_entry->node_id ||
		    node1_entry->data_center_id != node2_entry->data_center_id ||
		    node1_entry->node_state != node2_entry->node_state) {
			res = 0;
			goto return_res;
		}

		node_list_del(&tmp_list, node2_entry);
	}

	if (!node_list_is_empty(&tmp_list)) {
		res = 0;
		goto return_res;
	}

return_res:
	node_list_free(&tmp_list);

	return (res);
}

size_t
node_list_size(const struct node_list *nlist)
{
	struct node_list_entry *node_entry;
	size_t res;

	res = 0;

	TAILQ_FOREACH(node_entry, nlist, entries) {
		res++;
	}

	return (res);
}
