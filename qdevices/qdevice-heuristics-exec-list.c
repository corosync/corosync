/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
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

#include "qdevice-heuristics-exec-list.h"

void
qdevice_heuristics_exec_list_init(struct qdevice_heuristics_exec_list *list)
{

	TAILQ_INIT(list);
}

struct qdevice_heuristics_exec_list_entry *
qdevice_heuristics_exec_list_add(struct qdevice_heuristics_exec_list *list,
    char *name, char *command)
{
	struct qdevice_heuristics_exec_list_entry *entry;

	entry = (struct qdevice_heuristics_exec_list_entry *)malloc(sizeof(*entry));
	if (entry == NULL) {
		return (NULL);
	}

	memset(entry, 0, sizeof(*entry));

	entry->name = strdup(name);
	if (entry->name == NULL) {
		free(entry);

		return (NULL);
	}

	entry->command = strdup(command);
	if (entry->command == NULL) {
		free(entry->name);
		free(entry);

		return (NULL);
	}

	TAILQ_INSERT_TAIL(list, entry, entries);

	return (entry);
}

void
qdevice_heuristics_exec_list_free(struct qdevice_heuristics_exec_list *list)
{
	struct qdevice_heuristics_exec_list_entry *entry;
	struct qdevice_heuristics_exec_list_entry *entry_next;

	entry = TAILQ_FIRST(list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		free(entry->name);
		free(entry->command);
		free(entry);

		entry = entry_next;
	}

	TAILQ_INIT(list);
}

size_t
qdevice_heuristics_exec_list_size(const struct qdevice_heuristics_exec_list *list)
{
	struct qdevice_heuristics_exec_list_entry *entry;
	size_t res;

	res = 0;

	TAILQ_FOREACH(entry, list, entries) {
		res++;
	}

	return (res);
}

int
qdevice_heuristics_exec_list_clone(struct qdevice_heuristics_exec_list *dst_list,
    const struct qdevice_heuristics_exec_list *src_list)
{
	struct qdevice_heuristics_exec_list_entry *entry;

	qdevice_heuristics_exec_list_init(dst_list);

	TAILQ_FOREACH(entry, src_list, entries) {
		if (qdevice_heuristics_exec_list_add(dst_list, entry->name, entry->command) == NULL) {
			qdevice_heuristics_exec_list_free(dst_list);

			return (-1);
		}
	}

	return (0);
}

void
qdevice_heuristics_exec_list_del(struct qdevice_heuristics_exec_list *list,
    struct qdevice_heuristics_exec_list_entry *entry)
{

	TAILQ_REMOVE(list, entry, entries);

	free(entry->name);
	free(entry->command);
	free(entry);
}

int
qdevice_heuristics_exec_list_is_empty(const struct qdevice_heuristics_exec_list *list)
{

	return (TAILQ_EMPTY(list));
}

struct qdevice_heuristics_exec_list_entry *
qdevice_heuristics_exec_list_find_name(const struct qdevice_heuristics_exec_list *list,
    const char *name)
{
	struct qdevice_heuristics_exec_list_entry *entry;

	TAILQ_FOREACH(entry, list, entries) {
		if (strcmp(entry->name, name) == 0) {
			return (entry);
		}
	}

	return (NULL);
}

int
qdevice_heuristics_exec_list_eq(const struct qdevice_heuristics_exec_list *list1,
    const struct qdevice_heuristics_exec_list *list2)
{
	struct qdevice_heuristics_exec_list_entry *entry1;
	struct qdevice_heuristics_exec_list_entry *entry2;
	struct qdevice_heuristics_exec_list tmp_list;
	int res;

	res = 1;

	if (qdevice_heuristics_exec_list_clone(&tmp_list, list2) != 0) {
		return (-1);
	}

	TAILQ_FOREACH(entry1, list1, entries) {
		entry2 = qdevice_heuristics_exec_list_find_name(&tmp_list, entry1->name);
		if (entry2 == NULL) {
			res = 0;
			goto return_res;
		}

		if (strcmp(entry1->command, entry2->command) != 0) {
			res = 0;
			goto return_res;
		}

		qdevice_heuristics_exec_list_del(&tmp_list, entry2);
	}

	if (!qdevice_heuristics_exec_list_is_empty(&tmp_list)) {
		res = 0;
		goto return_res;
	}

return_res:
	qdevice_heuristics_exec_list_free(&tmp_list);

	return (res);
}
