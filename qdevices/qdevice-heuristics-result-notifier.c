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

#include <stdlib.h>
#include <string.h>

#include "qdevice-heuristics-result-notifier.h"

void
qdevice_heuristics_result_notifier_list_init(struct qdevice_heuristics_result_notifier_list *notifier_list)
{

        TAILQ_INIT(notifier_list);
}

struct qdevice_heuristics_result_notifier_item *
qdevice_heuristics_result_notifier_list_get(struct qdevice_heuristics_result_notifier_list *notifier_list,
    qdevice_heuristics_result_notifier_callback callback)
{
	struct qdevice_heuristics_result_notifier_item *item;

	TAILQ_FOREACH(item, notifier_list, entries) {
		if (item->callback == callback) {
			return (item);
		}
	}

	return (NULL);
}

struct qdevice_heuristics_result_notifier_item *
qdevice_heuristics_result_notifier_list_add(struct qdevice_heuristics_result_notifier_list *notifier_list,
    qdevice_heuristics_result_notifier_callback callback)
{
	struct qdevice_heuristics_result_notifier_item *item;

	item = qdevice_heuristics_result_notifier_list_get(notifier_list, callback);
	if (item != NULL) {
		return (item);
	}

	item = (struct qdevice_heuristics_result_notifier_item *)malloc(sizeof(*item));
	if (item == NULL) {
		return (NULL);
	}
	memset(item, 0, sizeof(*item));
	item->callback = callback;
	item->active = 0;

	TAILQ_INSERT_TAIL(notifier_list, item, entries);

	return (item);
}

int
qdevice_heuristics_result_notifier_list_set_active(struct qdevice_heuristics_result_notifier_list *notifier_list,
    qdevice_heuristics_result_notifier_callback callback, int active)
{
	struct qdevice_heuristics_result_notifier_item *item;

	item = qdevice_heuristics_result_notifier_list_get(notifier_list, callback);
	if (item == NULL) {
		return (-1);
	}

	item->active = active;

	return (0);
}

void
qdevice_heuristics_result_notifier_list_free(struct qdevice_heuristics_result_notifier_list *notifier_list)
{
	struct qdevice_heuristics_result_notifier_item *item;
	struct qdevice_heuristics_result_notifier_item *item_next;

	item = TAILQ_FIRST(notifier_list);
	while (item != NULL) {
		item_next = TAILQ_NEXT(item, entries);

		free(item);
		item = item_next;
	}
}

int
qdevice_heuristics_result_notifier_notify(struct qdevice_heuristics_result_notifier_list *notifier_list,
    void *heuristics_instance, uint32_t seq_number, enum qdevice_heuristics_exec_result exec_result)
{
	struct qdevice_heuristics_result_notifier_item *item;

	TAILQ_FOREACH(item, notifier_list, entries) {
		if (!item->active) {
			continue ;
		}

		if (item->callback(heuristics_instance, seq_number, exec_result) != 0) {
			return (-1);
		}
	}

	return (0);
}
