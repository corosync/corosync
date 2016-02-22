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

#ifndef _SEND_BUFFER_LIST_H_
#define _SEND_BUFFER_LIST_H_

#include <sys/queue.h>

#include "dynar.h"

#ifdef __cplusplus
extern "C" {
#endif

struct send_buffer_list_entry {
	struct dynar buffer;
	size_t msg_already_sent_bytes;

	TAILQ_ENTRY(send_buffer_list_entry) entries;
};

struct send_buffer_list {
	size_t max_list_entries;
	size_t allocated_list_entries;

	size_t max_buffer_size;

	TAILQ_HEAD(, send_buffer_list_entry) list;
	TAILQ_HEAD(, send_buffer_list_entry) free_list;
};

extern void				 send_buffer_list_init(struct send_buffer_list *sblist,
    size_t max_list_entries, size_t max_buffer_size);

extern struct send_buffer_list_entry	*send_buffer_list_get_new(struct send_buffer_list *sblist);

extern void				 send_buffer_list_put(struct send_buffer_list *sblist,
    struct send_buffer_list_entry *sblist_entry);

extern void				 send_buffer_list_discard_new(
    struct send_buffer_list *sblist, struct send_buffer_list_entry *sblist_entry);

extern struct send_buffer_list_entry	*send_buffer_list_get_active(
    const struct send_buffer_list *sblist);

extern void				 send_buffer_list_delete(struct send_buffer_list *sblist,
    struct send_buffer_list_entry *sblist_entry);

extern int				 send_buffer_list_empty(
    const struct send_buffer_list *sblist);

extern void				 send_buffer_list_free(struct send_buffer_list *sblist);

extern void				 send_buffer_list_set_max_buffer_size(
    struct send_buffer_list *sblist, size_t max_buffer_size);

extern void				 send_buffer_list_set_max_list_entries(
    struct send_buffer_list *sblist, size_t max_list_entries);

#ifdef __cplusplus
}
#endif

#endif /* _SEND_BUFFER_LIST_H_ */
