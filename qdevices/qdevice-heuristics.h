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

#ifndef _QDEVICE_HEURISTICS_H_
#define _QDEVICE_HEURISTICS_H_

#include "dynar.h"
#include "qdevice-advanced-settings.h"
#include "send-buffer-list.h"
#include "qdevice-heuristics-instance.h"
#include "qdevice-heuristics-log.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void		qdevice_heuristics_init(struct qdevice_heuristics_instance *instance,
    struct qdevice_advanced_settings *advanced_settings);

extern void		qdevice_heuristics_destroy(struct qdevice_heuristics_instance *instance);

extern int		qdevice_heuristics_exec(struct qdevice_heuristics_instance *instance,
    int sync_in_progress);

extern int		qdevice_heuristics_waiting_for_result(
    const struct qdevice_heuristics_instance *instance);

extern int		qdevice_heuristics_change_exec_list(
    struct qdevice_heuristics_instance *instance,
    const struct qdevice_heuristics_exec_list *new_exec_list, int sync_in_progress);

extern int		qdevice_heuristics_wait_for_initial_exec_result(
    struct qdevice_heuristics_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_HEURISTICS_H_ */
