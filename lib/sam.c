/*
 * Copyright (c) 2009-2010 Red Hat, Inc.
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

/*
 * Provides a SAM API
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>

#include <corosync/sam.h>

#include "util.h"

#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>

enum sam_internal_status_t {
	SAM_INTERNAL_STATUS_NOT_INITIALIZED = 0,
	SAM_INTERNAL_STATUS_INITIALIZED,
	SAM_INTERNAL_STATUS_REGISTERED,
	SAM_INTERNAL_STATUS_STARTED,
	SAM_INTERNAL_STATUS_FINALIZED
};

enum sam_command_t {
	SAM_COMMAND_START,
	SAM_COMMAND_STOP,
	SAM_COMMAND_HB
};

enum sam_parent_action_t {
	SAM_PARENT_ACTION_ERROR,
	SAM_PARENT_ACTION_RECOVERY,
	SAM_PARENT_ACTION_QUIT,
	SAM_PARENT_ACTION_CONTINUE
};

static struct {
	int time_interval;
	sam_recovery_policy_t recovery_policy;
	enum sam_internal_status_t internal_status;
	unsigned int instance_id;
	int parent_fd;
	int term_send;
	int warn_signal;

	sam_hc_callback_t hc_callback;
	pthread_t cb_thread;
	int cb_rpipe_fd, cb_wpipe_fd;
	int cb_registered;
} sam_internal_data;

cs_error_t sam_initialize (
	int time_interval,
	sam_recovery_policy_t recovery_policy)
{
	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_NOT_INITIALIZED) {
		return (CS_ERR_BAD_HANDLE);
	}

	if (recovery_policy != SAM_RECOVERY_POLICY_QUIT && recovery_policy != SAM_RECOVERY_POLICY_RESTART) {
		return (CS_ERR_INVALID_PARAM);
	}

	sam_internal_data.recovery_policy = recovery_policy;

	sam_internal_data.time_interval = time_interval;

	sam_internal_data.internal_status = SAM_INTERNAL_STATUS_INITIALIZED;

	sam_internal_data.warn_signal = SIGTERM;

	return (CS_OK);
}

/*
 * Wrapper on top of write(2) function. It handles EAGAIN and EINTR states and sends whole buffer if possible.
 */
static size_t sam_safe_write (
	int d,
	const void *buf,
	size_t nbyte)
{
	ssize_t bytes_write;
	ssize_t tmp_bytes_write;

	bytes_write = 0;

	do {
		tmp_bytes_write = write (d, (const char *)buf + bytes_write, nbyte - bytes_write);

		if (tmp_bytes_write == -1) {
			if (!(errno == EAGAIN || errno == EINTR))
				return -1;
		} else {
			bytes_write += tmp_bytes_write;
		}
	} while (bytes_write != nbyte);

	return bytes_write;
}

cs_error_t sam_start (void)
{
	char command;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED) {
		return (CS_ERR_BAD_HANDLE);
	}

	command = SAM_COMMAND_START;

	if (sam_safe_write (sam_internal_data.parent_fd, &command, 1) == -1)
		return (CS_ERR_LIBRARY);

	if (sam_internal_data.hc_callback)
		if (sam_safe_write (sam_internal_data.cb_wpipe_fd, &command, 1) == -1)
			return (CS_ERR_LIBRARY);

	sam_internal_data.internal_status = SAM_INTERNAL_STATUS_STARTED;

	return (CS_OK);
}

cs_error_t sam_stop (void)
{
	char command;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {
		return (CS_ERR_BAD_HANDLE);
	}

	command = SAM_COMMAND_STOP;

	if (sam_safe_write (sam_internal_data.parent_fd, &command, 1) == -1)
		return (CS_ERR_LIBRARY);

	if (sam_internal_data.hc_callback)
		if (sam_safe_write (sam_internal_data.cb_wpipe_fd, &command, 1) == -1)
			return (CS_ERR_LIBRARY);

	sam_internal_data.internal_status = SAM_INTERNAL_STATUS_REGISTERED;

	return (CS_OK);
}

cs_error_t sam_hc_send (void)
{
	char command;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {
		return (CS_ERR_BAD_HANDLE);
	}

	command = SAM_COMMAND_HB;

	if (sam_safe_write (sam_internal_data.parent_fd, &command, 1) == -1)
		return (CS_ERR_LIBRARY);

	return (CS_OK);
}

cs_error_t sam_finalize (void)
{
	cs_error_t error;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {
		return (CS_ERR_BAD_HANDLE);
	}

	if (sam_internal_data.internal_status == SAM_INTERNAL_STATUS_STARTED) {
		error = sam_stop ();
		if (error != CS_OK)
			goto exit_error;
	}

	sam_internal_data.internal_status = SAM_INTERNAL_STATUS_FINALIZED;

exit_error:
	return (CS_OK);
}


cs_error_t sam_warn_signal_set (int warn_signal)
{
	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {
		return (CS_ERR_BAD_HANDLE);
	}

	sam_internal_data.warn_signal = warn_signal;

	return (CS_OK);
}

static enum sam_parent_action_t sam_parent_handler (int pipe_fd, pid_t child_pid)
{
	int poll_error;
	int action;
	int status;
	ssize_t bytes_read;
	char command;
	int time_interval;
	struct pollfd pfds;

	status = 0;

	action = SAM_PARENT_ACTION_CONTINUE;

	while (action == SAM_PARENT_ACTION_CONTINUE) {
		pfds.fd = pipe_fd;
		pfds.events = POLLIN;
		pfds.revents = 0;

		if (status == 1 && sam_internal_data.time_interval != 0) {
			time_interval = sam_internal_data.time_interval;
		} else {
			time_interval = -1;
		}

		poll_error = poll (&pfds, 1, time_interval);

		if (poll_error == -1) {
			/*
			 *  Error in poll
			 *  If it is EINTR, continue, otherwise QUIT
			 */
			if (errno != EINTR) {
				action = SAM_PARENT_ACTION_ERROR;
			}
		}

		if (poll_error == 0) {
			/*
			 *  Time limit expires
			 */
			if (status == 0) {
				action = SAM_PARENT_ACTION_QUIT;
			} else {
				/*
				 *  Kill child process
				 */
				if (!sam_internal_data.term_send) {
					/*
					 * We didn't send warn_signal yet.
					 */
					kill (child_pid, sam_internal_data.warn_signal);

					sam_internal_data.term_send = 1;
				} else {
					/*
					 * We sent child warning. Now, we will not be so nice
					 */
					kill (child_pid, SIGKILL);
					action = SAM_PARENT_ACTION_RECOVERY;
				}
			}
		}

		if (poll_error > 0) {
			/*
			 *  We have EOF or command in pipe
			 */
			bytes_read = read (pipe_fd, &command, 1);

			if (bytes_read == 0) {
				/*
				 *  Handle EOF -> Take recovery action or quit if sam_start wasn't called
				 */
				if (status == 0)
					action = SAM_PARENT_ACTION_QUIT;
				else
					action = SAM_PARENT_ACTION_RECOVERY;

				continue;
			}

			if (bytes_read == -1) {
				/*
				 * Something really bad happened in read side
				 */
				if (errno == EAGAIN || errno == EINTR) {
					continue;
				}

				action = SAM_PARENT_ACTION_ERROR;
				goto action_exit;
			}

			/*
			 * We have read command -> take status
			 */
			switch (status) {
			case 0:
				/*
				 *  Not started yet
				 */
				if (command == SAM_COMMAND_START)
					status = 1;
			break;

			case 1:
				/*
				 *  Started
				 */
				if (command == SAM_COMMAND_STOP)
					status = 0;
			break;

			}
		} /* select_error > 0 */
	} /* action == SAM_PARENT_ACTION_CONTINUE */

action_exit:
	return action;
}

cs_error_t sam_register (
	unsigned int *instance_id)
{
	cs_error_t error;
	pid_t pid;
	int pipe_error;
	int pipe_fd[2];
	enum sam_parent_action_t action;
	int child_status;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED) {
		return (CS_ERR_BAD_HANDLE);
	}

	error = CS_OK;

	while (1) {
		pipe_error = pipe (pipe_fd);

		if (pipe_error != 0) {
			/*
			 *  Pipe creation error
			 */
			error = CS_ERR_LIBRARY;
			goto error_exit;
		}

		sam_internal_data.instance_id++;

		sam_internal_data.term_send = 0;

		pid = fork ();

		if (pid == -1) {
			/*
			 *  Fork error
			 */
			sam_internal_data.instance_id--;

			error = CS_ERR_LIBRARY;
			goto error_exit;
		}

		if (pid == 0) {
			/*
			 *  Child process
			 */
			close (pipe_fd[0]);

			sam_internal_data.parent_fd = pipe_fd[1];
			if (instance_id)
				*instance_id = sam_internal_data.instance_id;

			sam_internal_data.internal_status = SAM_INTERNAL_STATUS_REGISTERED;

			goto error_exit;
		} else {
			/*
			 *  Parent process
			 */
			close (pipe_fd[1]);

			action = sam_parent_handler (pipe_fd[0], pid);

			close (pipe_fd[0]);

			if (action == SAM_PARENT_ACTION_ERROR) {
				error = CS_ERR_LIBRARY;
				goto error_exit;
			}

			/*
			 * We really don't like zombies
			 */
			while (waitpid (pid, &child_status, 0) == -1 && errno == EINTR)
				;

			if (action == SAM_PARENT_ACTION_RECOVERY) {
				if (sam_internal_data.recovery_policy == SAM_RECOVERY_POLICY_QUIT)
					action = SAM_PARENT_ACTION_QUIT;
			}

			if (action == SAM_PARENT_ACTION_QUIT) {
				exit (WEXITSTATUS (child_status));
			}

		}
	}

error_exit:
	return (error);
}

static void *hc_callback_thread (void *unused_param)
{
	int poll_error;
	int status;
	ssize_t bytes_readed;
	char command;
	int time_interval, tmp_time_interval;
	int counter;
	struct pollfd pfds;

	status = 0;
	counter = 0;

	time_interval = sam_internal_data.time_interval >> 2;

	while (1) {
		pfds.fd = sam_internal_data.cb_rpipe_fd;
		pfds.events = POLLIN;
		pfds.revents = 0;

		if (status == 1) {
			tmp_time_interval = time_interval;
		} else {
			tmp_time_interval = -1;
		}

		poll_error = poll (&pfds, 1, tmp_time_interval);

		if (poll_error == 0) {
			if (sam_hc_send () == CS_OK) {
				counter++;
			}

			if (counter >= 4) {
				if (sam_internal_data.hc_callback () != 0) {
					status = 3;
				}

				counter = 0;
			}
		}

		if (poll_error > 0) {
			bytes_readed = read (sam_internal_data.cb_rpipe_fd, &command, 1);

			if (bytes_readed > 0) {
				if (status == 0 && command == SAM_COMMAND_START)
					status = 1;

				if (status == 1 && command == SAM_COMMAND_STOP)
					status = 0;

			}
		}
	}

	/*
	 * This makes compiler happy, it's same as return (NULL);
	 */
	return (unused_param);
}

cs_error_t sam_hc_callback_register (sam_hc_callback_t cb)
{
	cs_error_t error = CS_OK;
	pthread_attr_t thread_attr;
	int pipe_error;
	int pipe_fd[2];

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED) {
		return (CS_ERR_BAD_HANDLE);
	}

	if (sam_internal_data.time_interval == 0) {
		return (CS_ERR_INVALID_PARAM);
	}

	if (sam_internal_data.cb_registered) {
		sam_internal_data.hc_callback = cb;

		return (CS_OK);
	}

	/*
	 * We know, this is first registration
	 */

	if (cb == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	pipe_error = pipe (pipe_fd);

	if (pipe_error != 0) {
		/*
		 *  Pipe creation error
		 */
		error = CS_ERR_LIBRARY;
		goto error_exit;
	}

	sam_internal_data.cb_rpipe_fd = pipe_fd[0];
	sam_internal_data.cb_wpipe_fd = pipe_fd[1];

	/*
	 * Create thread attributes
	 */
	error = pthread_attr_init (&thread_attr);
	if (error != 0) {
		error = CS_ERR_LIBRARY;
		goto error_close_fd_exit;
	}


	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize (&thread_attr, 32768);

	/*
	 * Create thread
	 */
	error = pthread_create (&sam_internal_data.cb_thread, &thread_attr, hc_callback_thread, NULL);

	if (error != 0) {
		error = CS_ERR_LIBRARY;
		goto error_attr_destroy_exit;
	}

	/*
	 * Cleanup
	 */
	pthread_attr_destroy(&thread_attr);

	sam_internal_data.cb_registered = 1;
	sam_internal_data.hc_callback = cb;

	return (CS_OK);

error_attr_destroy_exit:
	pthread_attr_destroy(&thread_attr);
error_close_fd_exit:
	sam_internal_data.cb_rpipe_fd = sam_internal_data.cb_wpipe_fd = 0;
	close (pipe_fd[0]);
	close (pipe_fd[1]);
error_exit:
	return (error);
}
