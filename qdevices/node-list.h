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

#ifndef _NODE_LIST_H_
#define _NODE_LIST_H_

#include <sys/types.h>

#include <sys/queue.h>
#include <inttypes.h>

#include "tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

struct node_list_entry {
	uint32_t node_id;
	uint32_t data_center_id;
	enum tlv_node_state node_state;
	TAILQ_ENTRY(node_list_entry) entries;
};

TAILQ_HEAD(node_list, node_list_entry);

extern void				 node_list_init(struct node_list *list);

extern struct node_list_entry		*node_list_add(struct node_list *list,
    uint32_t node_id, uint32_t data_center_id, enum tlv_node_state node_state);

extern struct node_list_entry		*node_list_add_from_node_info(
    struct node_list *list, const struct tlv_node_info *node_info);

extern int				 node_list_clone(struct node_list *dst_list,
    const struct node_list *src_list);

extern void				 node_list_free(struct node_list *list);

extern void				 node_list_del(struct node_list *list,
    struct node_list_entry *node);

extern int				 node_list_is_empty(const struct node_list *list);

extern void				 node_list_entry_to_tlv_node_info(
    const struct node_list_entry *node, struct tlv_node_info *node_info);

extern struct node_list_entry *		 node_list_find_node_id(const struct node_list *list,
    uint32_t node_id);

extern int				 node_list_eq(const struct node_list *list1,
    const struct node_list *list2);

extern size_t				 node_list_size(const struct node_list *nlist);

#ifdef __cplusplus
}
#endif

#endif /* _NODE_LIST_H_ */
