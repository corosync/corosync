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

#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dynar-str.h"
#include "qdevice-config.h"
#include "qdevice-heuristics-io.h"
#include "qdevice-heuristics-worker.h"
#include "qdevice-heuristics-worker-instance.h"
#include "qdevice-heuristics-worker-log.h"
#include "qdevice-heuristics-worker-cmd.h"

/*
 * Declarations
 */
static int		qdevice_heuristics_worker_kill_list_timer_callback(void *data1,
    void *data2);

static void		qdevice_heuristics_worker_process_list_notify(
    enum process_list_notify_reason reason, const struct process_list_entry *entry,
    void *user_data);

static void		qdevice_heuristics_worker_signal_handlers_register(void);


/*
 * Definitions
 */
static void
qdevice_heuristics_worker_process_list_notify(enum process_list_notify_reason reason,
    const struct process_list_entry *entry, void *user_data)
{
	struct qdevice_heuristics_worker_instance *instance;

	instance = (struct qdevice_heuristics_worker_instance *)user_data;

	switch (reason) {
	case PROCESS_LIST_NOTIFY_REASON_EXECUTED:
		qdevice_heuristics_worker_log_printf(instance, LOG_DEBUG,
		    "process %s executed", entry->name);
		break;
	case PROCESS_LIST_NOTIFY_REASON_FINISHED:
		if (!WIFEXITED(entry->exit_status) || WEXITSTATUS(entry->exit_status) != 0) {
			if (WIFEXITED(entry->exit_status)) {
				qdevice_heuristics_worker_log_printf(instance, LOG_WARNING,
				    "process %s finished with status %d", entry->name,
				    WEXITSTATUS(entry->exit_status));
			} else if (WIFSIGNALED(entry->exit_status)) {
				qdevice_heuristics_worker_log_printf(instance, LOG_WARNING,
				    "process %s killed by signal %d", entry->name,
				    WTERMSIG(entry->exit_status));
			} else {
				qdevice_heuristics_worker_log_printf(instance, LOG_WARNING,
				    "process %s finished with non zero status", entry->name);
			}
		} else {
			qdevice_heuristics_worker_log_printf(instance, LOG_DEBUG,
			    "process %s sucesfully finished", entry->name);
		}
		break;
	}
}

static void
qdevice_heuristics_worker_signal_handlers_register(void)
{
	struct sigaction act;

	act.sa_handler = SIG_DFL;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGCHLD, &act, NULL);

	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGPIPE, &act, NULL);

	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGINT, &act, NULL);
}

static int
qdevice_heuristics_worker_kill_list_timer_callback(void *data1, void *data2)
{
	struct qdevice_heuristics_worker_instance *instance;
	size_t kill_list_size;

	instance = (struct qdevice_heuristics_worker_instance *)data1;

	if (process_list_process_kill_list(&instance->main_process_list) != 0) {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_kill_list_timer_callback: process kill list failed. "
		    "Shutting down worker");

		instance->schedule_exit = 1;
		return (0);
	}

	kill_list_size = process_list_get_kill_list_items(&instance->main_process_list);

	if (kill_list_size > 0) {
		qdevice_heuristics_worker_log_printf(instance, LOG_DEBUG,
		    "Still waiting for %zu processes exit", kill_list_size);
	}

	/*
	 * Schedule this timer again
	 */
	return (-1);
}

int
qdevice_heuristics_worker_exec_timeout_timer_callback(void *data1, void *data2)
{
	struct qdevice_heuristics_worker_instance *instance;

	instance = (struct qdevice_heuristics_worker_instance *)data1;

	qdevice_heuristics_worker_log_printf(instance, LOG_WARNING,
	    "Not all heuristics execs finished on time");

	process_list_move_active_entries_to_kill_list(&instance->main_process_list);

	instance->exec_timeout_timer = NULL;

	if (qdevice_heuristics_worker_cmd_write_exec_result(instance, instance->last_exec_seq_number,
	    QDEVICE_HEURISTICS_EXEC_RESULT_FAIL) != 0) {
		instance->schedule_exit = 1;

		return (0);
	}

	return (0);
}

static int
qdevice_heuristics_worker_poll(struct qdevice_heuristics_worker_instance *instance)
{
	int poll_res;
	struct pollfd poll_input_fd;
	uint32_t timeout;
	int plist_summary;

	/*
	 * Poll command input
	 */
	poll_input_fd.fd = QDEVICE_HEURISTICS_WORKER_CMD_IN_FD;
	poll_input_fd.events = POLLIN;
	poll_input_fd.revents = 0;

	timeout = timer_list_time_to_expire_ms(&instance->main_timer_list);
	if (timeout > QDEVICE_MIN_HEURISTICS_TIMEOUT) {
		timeout = QDEVICE_MIN_HEURISTICS_TIMEOUT;
	}

	if ((poll_res = poll(&poll_input_fd, 1, timeout)) >= 0) {
		if (poll_input_fd.revents & POLLIN) {
			/*
			 * POLLIN
			 */
			if (qdevice_heuristics_worker_cmd_read_from_pipe(instance) != 0) {
				return (-1);
			}
		}

		if (poll_input_fd.revents & POLLOUT) {
			/*
			 * Pollout shouldn't happen (critical error)
			 */
			qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
			    "qdevice_heuristics_worker_poll: POLLOUT set. Shutting down worker");

			return (-1);
		}

		if (poll_input_fd.revents & (POLLERR|POLLHUP|POLLNVAL) &&
		    !(poll_input_fd.revents & (POLLIN|POLLOUT))) {
			/*
			 * Qdevice closed pipe
			 */

			return (-1);
		}
	}

	if (process_list_waitpid(&instance->main_process_list) != 0) {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_poll: Waitpid failed. Shutting down worker");

		return (-1);
	}

	if (instance->exec_timeout_timer != NULL) {
		plist_summary = process_list_get_summary_result_short(&instance->main_process_list);

		switch (plist_summary) {
		case -1:
			/*
			 * Processes not finished -> continue
			 */
			break;
		case 0:
			/*
			 * All processes finished sucesfully
			 */
			if (qdevice_heuristics_worker_cmd_write_exec_result(instance,
			    instance->last_exec_seq_number, QDEVICE_HEURISTICS_EXEC_RESULT_PASS) != 0) {
				return (-1);
			}

			process_list_move_active_entries_to_kill_list(&instance->main_process_list);

			timer_list_delete(&instance->main_timer_list, instance->exec_timeout_timer);
			instance->exec_timeout_timer = NULL;

			break;
		case 1:
			/*
			 * Some processes failed
			 */
			if (qdevice_heuristics_worker_cmd_write_exec_result(instance,
			    instance->last_exec_seq_number, QDEVICE_HEURISTICS_EXEC_RESULT_FAIL) != 0) {
				return (-1);
			}

			process_list_move_active_entries_to_kill_list(&instance->main_process_list);

			timer_list_delete(&instance->main_timer_list, instance->exec_timeout_timer);
			instance->exec_timeout_timer = NULL;
			break;
		default:
			qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
			    "qdevice_heuristics_worker_poll: Unhandled "
			    "process_list_get_summary_result. Shutting down worker");

			return (-1);
			break;
		}
	}

	timer_list_expire(&instance->main_timer_list);

	if (instance->schedule_exit) {
		return (-1);
	}

	return (0);
}

void
qdevice_heuristics_worker_start(size_t ipc_max_send_receive_size, int use_execvp,
    size_t max_processes, uint32_t kill_list_interval)
{
	struct qdevice_heuristics_worker_instance instance;

	memset(&instance, 0, sizeof(instance));

	instance.schedule_exit = 0;

	dynar_init(&instance.cmd_in_buffer, ipc_max_send_receive_size);
	dynar_init(&instance.cmd_out_buffer, ipc_max_send_receive_size);
	dynar_init(&instance.log_out_buffer, ipc_max_send_receive_size);

	process_list_init(&instance.main_process_list, max_processes, use_execvp,
	    qdevice_heuristics_worker_process_list_notify, (void *)&instance);

	timer_list_init(&instance.main_timer_list);
	instance.kill_list_timer = timer_list_add(&instance.main_timer_list,
	    kill_list_interval, qdevice_heuristics_worker_kill_list_timer_callback,
	    (void *)&instance, NULL);

	if (instance.kill_list_timer == NULL) {
		qdevice_heuristics_worker_log_printf(&instance, LOG_CRIT,
		    "Can't create kill list timer");
		return ;
	}

	instance.exec_timeout_timer = NULL;

	qdevice_heuristics_exec_list_init(&instance.exec_list);

	qdevice_heuristics_worker_signal_handlers_register();

	qdevice_heuristics_worker_log_printf(&instance, LOG_DEBUG, "Heuristic worker initialized");

	while (qdevice_heuristics_worker_poll(&instance) == 0) {
	}

	qdevice_heuristics_worker_log_printf(&instance, LOG_DEBUG, "Heuristic worker shutdown "
	    "requested");

	qdevice_heuristics_exec_list_free(&instance.exec_list);

	timer_list_free(&instance.main_timer_list);

	qdevice_heuristics_worker_log_printf(&instance, LOG_DEBUG,
	    "Waiting for all processes to exit");

	if (process_list_killall(&instance.main_process_list, kill_list_interval) != 0) {
		qdevice_heuristics_worker_log_printf(&instance, LOG_WARNING,
		    "Not all process exited");
	}

	process_list_free(&instance.main_process_list);

	dynar_destroy(&instance.cmd_in_buffer);
	dynar_destroy(&instance.cmd_out_buffer);
	dynar_destroy(&instance.log_out_buffer);
}
