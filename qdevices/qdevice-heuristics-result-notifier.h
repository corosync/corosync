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

#ifndef _QDEVICE_HEURISTICS_RESULT_NOTIFIER_H_
#define _QDEVICE_HEURISTICS_RESULT_NOTIFIER_H_

#include <sys/queue.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>

#include "qdevice-heuristics-exec-result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*qdevice_heuristics_result_notifier_callback)(void *heuristics_instance, uint32_t seq_number,
    enum qdevice_heuristics_exec_result exec_result);

struct qdevice_heuristics_result_notifier_item {
	qdevice_heuristics_result_notifier_callback callback;
	int active;
	TAILQ_ENTRY(qdevice_heuristics_result_notifier_item) entries;
};

TAILQ_HEAD(qdevice_heuristics_result_notifier_list, qdevice_heuristics_result_notifier_item);

extern void						 qdevice_heuristics_result_notifier_list_init(
    struct qdevice_heuristics_result_notifier_list *notifier_list);

extern struct qdevice_heuristics_result_notifier_item	*qdevice_heuristics_result_notifier_list_add(
    struct qdevice_heuristics_result_notifier_list *notifier_list,
    qdevice_heuristics_result_notifier_callback callback);

extern struct qdevice_heuristics_result_notifier_item	*qdevice_heuristics_result_notifier_list_get(
    struct qdevice_heuristics_result_notifier_list *notifier_list,
    qdevice_heuristics_result_notifier_callback callback);

extern int						 qdevice_heuristics_result_notifier_list_set_active(
    struct qdevice_heuristics_result_notifier_list *notifier_list,
    qdevice_heuristics_result_notifier_callback callback, int active);

extern void						 qdevice_heuristics_result_notifier_list_free(
    struct qdevice_heuristics_result_notifier_list *notifier_list);

extern int						 qdevice_heuristics_result_notifier_notify(
    struct qdevice_heuristics_result_notifier_list *notifier_list,
    void *heuristics_instance, uint32_t seq_number,
    enum qdevice_heuristics_exec_result exec_result);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_HEURISTICS_RESULT_NOTIFIER_H_ */
