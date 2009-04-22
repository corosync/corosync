/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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

#ifndef CONFIG_WTHREAD_H_DEFINED
#define CONFIG_WTHREAD_H_DEFINED

struct worker_thread_group {
	int threadcount;
	int last_scheduled;
	struct worker_thread_t *threads;
	void (*worker_fn) (void *thread_state, void *work_item);
};

extern int worker_thread_group_init (
	struct worker_thread_group *worker_thread_group,
	int threads,
	int items_max,
	int item_size,
	int thread_state_size,
	void (*thread_state_constructor)(void *),
	void (*worker_fn)(void *thread_state, void *work_item));

extern int worker_thread_group_work_add (
	struct worker_thread_group *worker_thread_group,
	void *item);

extern void worker_thread_group_wait (
	struct worker_thread_group *worker_thread_group);

extern void worker_thread_group_exit (
	struct worker_thread_group *worker_thread_group);

extern void worker_thread_group_atsegv (
	struct worker_thread_group *worker_thread_group);

#endif /* WTHREAD_H_DEFINED */
