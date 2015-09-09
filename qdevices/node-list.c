/*
 * Copyright (c) 2015 Red Hat, Inc.
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
node_list_is_empty(struct node_list *list)
{

	return (TAILQ_EMPTY(list));
}
