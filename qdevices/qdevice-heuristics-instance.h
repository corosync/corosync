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

#ifndef _QDEVICE_HEURISTICS_INSTANCE_H_
#define _QDEVICE_HEURISTICS_INSTANCE_H_

#include "dynar.h"
#include "send-buffer-list.h"
#include "qdevice-heuristics-mode.h"
#include "qdevice-heuristics-exec-list.h"
#include "qdevice-heuristics-exec-result.h"
#include "qdevice-heuristics-result-notifier.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qdevice_heuristics_instance {
	int pipe_cmd_send;
	int pipe_cmd_recv;
	int pipe_log_recv;
	pid_t worker_pid;
	struct send_buffer_list cmd_out_buffer_list;
	struct dynar log_in_buffer;
	struct dynar cmd_in_buffer;

	uint32_t timeout;
	uint32_t sync_timeout;
	uint32_t interval;

	enum qdevice_heuristics_mode mode;

	int waiting_for_result;
	uint32_t expected_reply_seq_number;

	struct qdevice_heuristics_exec_list exec_list;

	struct qdevice_instance *qdevice_instance_ptr;

	struct qdevice_heuristics_result_notifier_list exec_result_notifier_list;
};

extern int	qdevice_heuristics_instance_init(struct qdevice_heuristics_instance *instance);

extern int	qdevice_heuristics_instance_destroy(struct qdevice_heuristics_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_HEURISTICS_INSTANCE_H_ */
