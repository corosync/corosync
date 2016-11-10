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

#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qdevice-log.h"
#include "qdevice-heuristics.h"
#include "qdevice-heuristics-cmd.h"
#include "qdevice-heuristics-worker.h"
#include "qdevice-heuristics-io.h"
#include "qdevice-votequorum.h"
#include "utils.h"

#define QDEVICE_HEURISTICS_WAIT_FOR_INITIAL_EXEC_RESULT_MAX_PFDS	5

void
qdevice_heuristics_init(struct qdevice_heuristics_instance *instance,
    struct qdevice_advanced_settings *advanced_settings)
{
	int pipe_cmd_in[2], pipe_cmd_out[2], pipe_log_out[2];
	pid_t pid;

	if (pipe(pipe_cmd_in) != 0) {
		err(1, "Can't create command input pipe");
	}

	if (pipe(pipe_cmd_out) != 0) {
		err(1, "Can't create command output pipe");
	}

	if (pipe(pipe_log_out) != 0) {
		err(1, "Can't create logging output pipe");
	}

	pid = fork();
	if (pid == -1) {
		err(1, "Can't create child process");
	} else if (pid == 0) {
		/*
		 * Child
		 */
		(void)setsid();
		if (dup2(pipe_cmd_in[0], 0) == -1) {
			err(1, "Can't dup2 command input pipe");
		}
		close(pipe_cmd_in[1]);
		close(pipe_cmd_in[0]);
		if (utils_fd_set_non_blocking(0) == -1) {
			err(1, "Can't set non blocking flag on command input pipe");
		}

		if (dup2(pipe_cmd_out[1], 1) == -1) {
			err(1, "Can't dup2 command output pipe");
		}
		close(pipe_cmd_out[0]);
		close(pipe_cmd_out[1]);

		if (dup2(pipe_log_out[1], 2) == -1) {
			err(1, "Can't dup2 logging output pipe");
		}
		close(pipe_log_out[0]);
		close(pipe_log_out[1]);

		qdevice_heuristics_worker_start(advanced_settings->heuristics_ipc_max_send_receive_size,
		    advanced_settings->heuristics_use_execvp, advanced_settings->heuristics_max_processes,
		    advanced_settings->heuristics_kill_list_interval);

		qdevice_advanced_settings_destroy(advanced_settings);

		exit(0);
	} else {
		close(pipe_cmd_in[0]);
		close(pipe_cmd_out[1]);
		close(pipe_log_out[1]);

		qdevice_heuristics_instance_init(instance);

		instance->pipe_cmd_send = pipe_cmd_in[1];
		if (utils_fd_set_non_blocking(instance->pipe_cmd_send) == -1) {
			err(1, "Can't set non blocking flag on command input pipe");
		}
		instance->pipe_cmd_recv = pipe_cmd_out[0];
		if (utils_fd_set_non_blocking(instance->pipe_cmd_recv) == -1) {
			err(1, "Can't set non blocking flag on command output pipe");
		}
		instance->pipe_log_recv = pipe_log_out[0];
		if (utils_fd_set_non_blocking(instance->pipe_cmd_recv) == -1) {
			err(1, "Can't set non blocking flag on logging output pipe");
		}
		instance->worker_pid = pid;

		send_buffer_list_init(&instance->cmd_out_buffer_list,
		    advanced_settings->heuristics_ipc_max_send_buffers,
		    advanced_settings->heuristics_ipc_max_send_receive_size);
		dynar_init(&instance->log_in_buffer,
		    advanced_settings->heuristics_ipc_max_send_receive_size);
		dynar_init(&instance->cmd_in_buffer,
		    advanced_settings->heuristics_ipc_max_send_receive_size);
	}
}

void
qdevice_heuristics_destroy(struct qdevice_heuristics_instance *instance)
{
	int status;

	/*
	 * Close of pipe_cmd_send result is correct and almost instant exit of worker
	 */
	close(instance->pipe_cmd_send);

	qdevice_log(LOG_DEBUG, "Waiting for heuristics worker to finish");
	if (waitpid(instance->worker_pid, &status, 0) == -1) {
		qdevice_log_err(LOG_ERR, "Heuristics worker waitpid failed");
	} else {
		/*
		 * Log what left in worker log buffer. Errors can be ignored
		 */
		(void)qdevice_heuristics_log_read_from_pipe(instance);
	}

	close(instance->pipe_cmd_recv);
	close(instance->pipe_log_recv);

	dynar_destroy(&instance->log_in_buffer);
	dynar_destroy(&instance->cmd_in_buffer);
	send_buffer_list_free(&instance->cmd_out_buffer_list);

	qdevice_heuristics_instance_destroy(instance);
}

int
qdevice_heuristics_exec(struct qdevice_heuristics_instance *instance, int sync_in_progress)
{
	uint32_t timeout;

	instance->expected_reply_seq_number++;
	instance->waiting_for_result = 1;

	if (sync_in_progress) {
		timeout = instance->sync_timeout;
	} else {
		timeout = instance->timeout;
	}

	return (qdevice_heuristics_cmd_write_exec(instance, timeout,
	    instance->expected_reply_seq_number));
}

int
qdevice_heuristics_waiting_for_result(const struct qdevice_heuristics_instance *instance)
{

	return (instance->waiting_for_result);
}

int
qdevice_heuristics_change_exec_list(struct qdevice_heuristics_instance *instance,
    const struct qdevice_heuristics_exec_list *new_exec_list, int sync_in_progress)
{

	if (qdevice_heuristics_cmd_write_exec_list(instance, new_exec_list) != 0) {
		return (-1);
	}

	qdevice_heuristics_exec_list_free(&instance->exec_list);

	if (new_exec_list != NULL) {
		if (qdevice_heuristics_exec_list_clone(&instance->exec_list, new_exec_list) != 0) {
			qdevice_log(LOG_ERR, "Can't clone exec list");

			return (-1);
		}
	}

	if (qdevice_heuristics_waiting_for_result(instance)) {
		if (qdevice_heuristics_exec(instance, sync_in_progress) != 0) {
			qdevice_log(LOG_ERR, "Can't execute heuristics");

			return (-1);
		}
	}

	return (0);
}


int
qdevice_heuristics_wait_for_initial_exec_result(struct qdevice_heuristics_instance *instance)
{
	struct pollfd pfds[QDEVICE_HEURISTICS_WAIT_FOR_INITIAL_EXEC_RESULT_MAX_PFDS];
	int no_pfds;
	int poll_res;
	int timeout;
	int i;
	int case_processed;
	int res;

	while (!instance->qdevice_instance_ptr->vq_node_list_initial_heuristics_finished) {
		no_pfds = 0;

		assert(no_pfds < QDEVICE_HEURISTICS_WAIT_FOR_INITIAL_EXEC_RESULT_MAX_PFDS);
		pfds[no_pfds].fd = instance->pipe_log_recv;
		pfds[no_pfds].events = POLLIN;
		pfds[no_pfds].revents = 0;
		no_pfds++;

		assert(no_pfds < QDEVICE_HEURISTICS_WAIT_FOR_INITIAL_EXEC_RESULT_MAX_PFDS);
		pfds[no_pfds].fd = instance->pipe_cmd_recv;
		pfds[no_pfds].events = POLLIN;
		pfds[no_pfds].revents = 0;
		no_pfds++;

		assert(no_pfds < QDEVICE_HEURISTICS_WAIT_FOR_INITIAL_EXEC_RESULT_MAX_PFDS);
		pfds[no_pfds].fd = instance->qdevice_instance_ptr->votequorum_poll_fd;
		pfds[no_pfds].events = POLLIN;
		pfds[no_pfds].revents = 0;
		no_pfds++;

		if (!send_buffer_list_empty(&instance->cmd_out_buffer_list)) {
			assert(no_pfds < QDEVICE_HEURISTICS_WAIT_FOR_INITIAL_EXEC_RESULT_MAX_PFDS);
			pfds[no_pfds].fd = instance->pipe_cmd_send;
			pfds[no_pfds].events = POLLOUT;
			pfds[no_pfds].revents = 0;
			no_pfds++;
		}

		/*
		 * We know this is never larger than QDEVICE_DEFAULT_HEURISTICS_MAX_TIMEOUT * 2
		 */
		timeout = (int)instance->sync_timeout * 2;

		poll_res = poll(pfds, no_pfds, timeout);
		if (poll_res > 0) {
			for (i = 0; i < no_pfds; i++) {
				if (pfds[i].revents & POLLIN) {
					case_processed = 0;
					switch (i) {
					case 0:
						case_processed = 1;

						res = qdevice_heuristics_log_read_from_pipe(instance);
						if (res == -1) {
							return (-1);
						}
						break;
					case 1:
						case_processed = 1;
						res = qdevice_heuristics_cmd_read_from_pipe(instance);
						if (res == -1) {
							return (-1);
						}
						break;
					case 2:
						case_processed = 1;
						res = qdevice_votequorum_dispatch(instance->qdevice_instance_ptr);
						if (res == -1) {
							return (-1);
						}
					case 3:
						/*
						 * Read on heuristics cmd send fs shouldn't happen
						 */
						 break;
					}

					if (!case_processed) {
						qdevice_log(LOG_CRIT, "Unhandled read on poll descriptor %u", i);
						exit(1);
					}
				}

				if (pfds[i].revents & POLLOUT) {
					case_processed = 0;
					switch (i) {
					case 0:
					case 1:
					case 2:
						/*
						 * Write on heuristics log, cmd recv or vq shouldn't happen
						 */
						break;
					case 3:
						case_processed = 1;
						res = qdevice_heuristics_cmd_write(instance);
						if (res == -1) {
							return (-1);
						}
						break;
					}

					if (!case_processed) {
						qdevice_log(LOG_CRIT, "Unhandled write on poll descriptor %u", i);
						exit(1);
					}
				}

				if ((pfds[i].revents & (POLLERR|POLLHUP|POLLNVAL)) &&
				    !(pfds[i].revents & (POLLIN|POLLOUT))) {
					switch (i) {
					case 0:
					case 1:
					case 3:
						/*
						 *  Closed pipe doesn't mean return of POLLIN. To display
						 * better log message, we call read log as if POLLIN would
						 * be set.
						 */
						res = qdevice_heuristics_log_read_from_pipe(instance);
						if (res == -1) {
							return (-1);
						}

						qdevice_log(LOG_ERR, "POLLERR (%u) on heuristics pipe. Exiting");
						return (-1);
						break;
					case 2:
						qdevice_log(LOG_ERR, "POLLERR (%u) on corosync socket. Exiting");
						return (-1);
						break;
					}
				}
			}
		} else if (poll_res == 0) {
			qdevice_log(LOG_ERR, "Timeout waiting for initial heuristics exec result");
			return (-1);
		} else {
			qdevice_log_err(LOG_ERR, "Initial heuristics exec result poll failed");
			return (-1);
		}
	}

	return (0);
}
