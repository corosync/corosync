/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006, 2009 Red Hat, Inc.
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

/*
 * Add work to a work group and have threads process the work
 * Provide blocking for all work to complete
 */

#include <config.h>

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <corosync/cs_queue.h>

#include "wthread.h"

struct thread_data {
	void *thread_state;
	void *data;
};

struct worker_thread_t {
	struct worker_thread_group *worker_thread_group;
	pthread_mutex_t new_work_mutex;
	pthread_cond_t new_work_cond;
	pthread_cond_t cond;
	pthread_mutex_t done_work_mutex;
	pthread_cond_t done_work_cond;
	pthread_t thread_id;
	struct cs_queue queue;
	void *thread_state;
	struct thread_data thread_data;
};

static void *start_worker_thread (void *thread_data_in) {
	struct thread_data *thread_data = (struct thread_data *)thread_data_in;
	struct worker_thread_t *worker_thread =
		(struct worker_thread_t *)thread_data->data;
	void *data_for_worker_fn;

	for (;;) {
		pthread_mutex_lock (&worker_thread->new_work_mutex);
		if (cs_queue_is_empty (&worker_thread->queue) == 1) {
		pthread_cond_wait (&worker_thread->new_work_cond,
			&worker_thread->new_work_mutex);
		}

		/*
		 * We unlock then relock the new_work_mutex to allow the
		 * worker function to execute and also allow new work to be
		 * added to the work queue
	  	 */
		data_for_worker_fn = cs_queue_item_get (&worker_thread->queue);
		pthread_mutex_unlock (&worker_thread->new_work_mutex);
		worker_thread->worker_thread_group->worker_fn (worker_thread->thread_state, data_for_worker_fn);
		pthread_mutex_lock (&worker_thread->new_work_mutex);
		cs_queue_item_remove (&worker_thread->queue);
		pthread_mutex_unlock (&worker_thread->new_work_mutex);
		pthread_mutex_lock (&worker_thread->done_work_mutex);
		if (cs_queue_is_empty (&worker_thread->queue) == 1) {
			pthread_cond_signal (&worker_thread->done_work_cond);
		}
		pthread_mutex_unlock (&worker_thread->done_work_mutex);
	}
	return (NULL);
}

int worker_thread_group_init (
	struct worker_thread_group *worker_thread_group,
	int threads,
	int items_max,
	int item_size,
	int thread_state_size,
	void (*thread_state_constructor)(void *),
	void (*worker_fn)(void *thread_state, void *work_item))
{
	int i;

	worker_thread_group->threadcount = threads;
	worker_thread_group->last_scheduled = 0;
	worker_thread_group->worker_fn = worker_fn;
	worker_thread_group->threads = malloc (sizeof (struct worker_thread_t) *
		threads);
	if (worker_thread_group->threads == 0) {
		return (-1);
	}

	for (i = 0; i < threads; i++) {
		if (thread_state_size) {
			worker_thread_group->threads[i].thread_state = malloc (thread_state_size);
		} else {
			worker_thread_group->threads[i].thread_state = NULL;
		}
		if (thread_state_constructor) {
			thread_state_constructor (worker_thread_group->threads[i].thread_state);
		}
		worker_thread_group->threads[i].worker_thread_group = worker_thread_group;
		pthread_mutex_init (&worker_thread_group->threads[i].new_work_mutex, NULL);
		pthread_cond_init (&worker_thread_group->threads[i].new_work_cond, NULL);
		pthread_mutex_init (&worker_thread_group->threads[i].done_work_mutex, NULL);
		pthread_cond_init (&worker_thread_group->threads[i].done_work_cond, NULL);
		cs_queue_init (&worker_thread_group->threads[i].queue, items_max,
			item_size);

		worker_thread_group->threads[i].thread_data.thread_state =
			worker_thread_group->threads[i].thread_state;
		worker_thread_group->threads[i].thread_data.data = &worker_thread_group->threads[i];
		pthread_create (&worker_thread_group->threads[i].thread_id,
			NULL, start_worker_thread, &worker_thread_group->threads[i].thread_data);
	}
	return (0);
}

int worker_thread_group_work_add (
	struct worker_thread_group *worker_thread_group,
	void *item)
{
	int schedule;

	schedule = (worker_thread_group->last_scheduled + 1) % (worker_thread_group->threadcount);
	worker_thread_group->last_scheduled = schedule;

	pthread_mutex_lock (&worker_thread_group->threads[schedule].new_work_mutex);
	if (cs_queue_is_full (&worker_thread_group->threads[schedule].queue)) {
		pthread_mutex_unlock (&worker_thread_group->threads[schedule].new_work_mutex);
		return (-1);
	}
	cs_queue_item_add (&worker_thread_group->threads[schedule].queue, item);
	pthread_cond_signal (&worker_thread_group->threads[schedule].new_work_cond);
	pthread_mutex_unlock (&worker_thread_group->threads[schedule].new_work_mutex);
	return (0);
}

void worker_thread_group_wait (
	struct worker_thread_group *worker_thread_group)
{
	int i;

	for (i = 0; i < worker_thread_group->threadcount; i++) {
		pthread_mutex_lock (&worker_thread_group->threads[i].done_work_mutex);
		if (cs_queue_is_empty (&worker_thread_group->threads[i].queue) == 0) {
			pthread_cond_wait (&worker_thread_group->threads[i].done_work_cond,
				&worker_thread_group->threads[i].done_work_mutex);
		}
		pthread_mutex_unlock (&worker_thread_group->threads[i].done_work_mutex);
	}
}

void worker_thread_group_exit (
	struct worker_thread_group *worker_thread_group)
{
	int i;

	for (i = 0; i < worker_thread_group->threadcount; i++) {
		pthread_cancel (worker_thread_group->threads[i].thread_id);

		/* Wait for worker thread to exit gracefully before destroying
		 * mutexes and processing items in the queue etc.
		 */
		pthread_join (worker_thread_group->threads[i].thread_id, NULL);
		pthread_mutex_destroy (&worker_thread_group->threads[i].new_work_mutex);
		pthread_cond_destroy (&worker_thread_group->threads[i].new_work_cond);
		pthread_mutex_destroy (&worker_thread_group->threads[i].done_work_mutex);
		pthread_cond_destroy (&worker_thread_group->threads[i].done_work_cond);
	}
}
void worker_thread_group_atsegv (
	struct worker_thread_group *worker_thread_group)
{
	void *data_for_worker_fn;
	struct worker_thread_t *worker_thread;
	unsigned int i;

	for (i = 0; i < worker_thread_group->threadcount; i++) {
		worker_thread = &worker_thread_group->threads[i];
		while (cs_queue_is_empty (&worker_thread->queue) == 0) {
			data_for_worker_fn = cs_queue_item_get (&worker_thread->queue);
			worker_thread->worker_thread_group->worker_fn (worker_thread->thread_state, data_for_worker_fn);
			cs_queue_item_remove (&worker_thread->queue);
		}
	}
}
