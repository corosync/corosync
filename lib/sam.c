/*
 * Copyright (c) 2009-2011 Red Hat, Inc.
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

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <poll.h>

#include <corosync/corotypes.h>
#include <qb/qbipcc.h>
#include <corosync/corodefs.h>
#include <corosync/cmap.h>
#include <corosync/hdb.h>
#include <corosync/quorum.h>

#include <corosync/sam.h>

#include "util.h"

#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>

#define SAM_CMAP_S_FAILED		"failed"
#define SAM_CMAP_S_REGISTERED		"stopped"
#define SAM_CMAP_S_STARTED		"running"
#define SAM_CMAP_S_Q_WAIT		"waiting for quorum"

#define SAM_RP_MASK_Q(pol)	(pol & (~SAM_RECOVERY_POLICY_QUORUM))
#define SAM_RP_MASK_C(pol)	(pol & (~SAM_RECOVERY_POLICY_CMAP))
#define SAM_RP_MASK(pol)	(pol & (~(SAM_RECOVERY_POLICY_QUORUM | SAM_RECOVERY_POLICY_CMAP)))

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
	SAM_COMMAND_HB,
	SAM_COMMAND_DATA_STORE,
	SAM_COMMAND_WARN_SIGNAL_SET,
	SAM_COMMAND_MARK_FAILED,
};

enum sam_reply_t {
	SAM_REPLY_OK,
	SAM_REPLY_ERROR,
};

enum sam_parent_action_t {
	SAM_PARENT_ACTION_ERROR,
	SAM_PARENT_ACTION_RECOVERY,
	SAM_PARENT_ACTION_QUIT,
	SAM_PARENT_ACTION_CONTINUE
};

enum sam_cmap_key_t {
	SAM_CMAP_KEY_RECOVERY,
	SAM_CMAP_KEY_HC_PERIOD,
	SAM_CMAP_KEY_LAST_HC,
	SAM_CMAP_KEY_STATE,
};

static struct {
	int time_interval;
	sam_recovery_policy_t recovery_policy;
	enum sam_internal_status_t internal_status;
	unsigned int instance_id;
	int child_fd_out;
	int child_fd_in;
	int term_send;
	int warn_signal;
	int am_i_child;

	sam_hc_callback_t hc_callback;
	pthread_t cb_thread;
	int cb_rpipe_fd, cb_wpipe_fd;
	int cb_registered;

	void *user_data;
	size_t user_data_size;
	size_t user_data_allocated;

	pthread_mutex_t lock;

	quorum_handle_t quorum_handle;
	uint32_t quorate;
	int quorum_fd;

	cmap_handle_t cmap_handle;
	char cmap_pid_path[CMAP_KEYNAME_MAXLEN];
} sam_internal_data;

extern const char *__progname;

static cs_error_t sam_cmap_update_key (enum sam_cmap_key_t key, const char *value)
{
	cs_error_t err;
	const char *svalue;
	uint64_t hc_period, last_hc;
	const char *ssvalue[] = { [SAM_RECOVERY_POLICY_QUIT] = "quit", [SAM_RECOVERY_POLICY_RESTART] = "restart" };
	char key_name[CMAP_KEYNAME_MAXLEN];

	switch (key) {
	case SAM_CMAP_KEY_RECOVERY:
		svalue = ssvalue[SAM_RP_MASK (sam_internal_data.recovery_policy)];

		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "%s%s", sam_internal_data.cmap_pid_path,
				"recovery");
		if ((err = cmap_set_string(sam_internal_data.cmap_handle, key_name, svalue)) != CS_OK) {
			goto exit_error;
		}
		break;
	case SAM_CMAP_KEY_HC_PERIOD:
		hc_period = sam_internal_data.time_interval;

		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "%s%s", sam_internal_data.cmap_pid_path,
				"poll_period");
		if ((err = cmap_set_uint64(sam_internal_data.cmap_handle, key_name, hc_period)) != CS_OK) {
			goto exit_error;
		}
		break;
	case SAM_CMAP_KEY_LAST_HC:
		last_hc = cs_timestamp_get();

		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "%s%s", sam_internal_data.cmap_pid_path,
				"last_updated");
		if ((err = cmap_set_uint64(sam_internal_data.cmap_handle, key_name, last_hc)) != CS_OK) {
			goto exit_error;
		}
		break;
	case SAM_CMAP_KEY_STATE:
		svalue = value;
		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "%s%s", sam_internal_data.cmap_pid_path,
				"state");
		if ((err = cmap_set_string(sam_internal_data.cmap_handle, key_name, svalue)) != CS_OK) {
			goto exit_error;
		}
		break;
	}

	return (CS_OK);

exit_error:
	return (err);
}

static cs_error_t sam_cmap_destroy_pid_path (void)
{
	cmap_iter_handle_t iter;
	cs_error_t err;
	char key_name[CMAP_KEYNAME_MAXLEN];

	err = cmap_iter_init(sam_internal_data.cmap_handle, sam_internal_data.cmap_pid_path, &iter);
	if (err != CS_OK) {
		goto error_exit;
	}

	while ((err = cmap_iter_next(sam_internal_data.cmap_handle, iter, key_name, NULL, NULL)) == CS_OK) {
		cmap_delete(sam_internal_data.cmap_handle, key_name);
	}

	err = cmap_iter_finalize(sam_internal_data.cmap_handle, iter);

error_exit:
	return (err);
}

static cs_error_t sam_cmap_register (void)
{
	cs_error_t err;
	cmap_handle_t cmap_handle;

	if ((err = cmap_initialize (&cmap_handle)) != CS_OK) {
		return (err);
	}

	snprintf(sam_internal_data.cmap_pid_path, CMAP_KEYNAME_MAXLEN, "resources.process.%d.", getpid());

	sam_internal_data.cmap_handle = cmap_handle;

	if ((err = sam_cmap_update_key (SAM_CMAP_KEY_RECOVERY, NULL)) != CS_OK) {
		goto destroy_finalize_error;
	}

	if ((err = sam_cmap_update_key (SAM_CMAP_KEY_HC_PERIOD, NULL)) != CS_OK) {
		goto destroy_finalize_error;
	}

	return (CS_OK);

destroy_finalize_error:
	sam_cmap_destroy_pid_path ();
	cmap_finalize (cmap_handle);
	return (err);
}

static void quorum_notification_fn (
        quorum_handle_t handle,
        uint32_t quorate,
        uint64_t ring_id,
        uint32_t view_list_entries,
        uint32_t *view_list)
{
	sam_internal_data.quorate = quorate;
}

cs_error_t sam_initialize (
	int time_interval,
	sam_recovery_policy_t recovery_policy)
{
	quorum_callbacks_t quorum_callbacks;
	uint32_t quorum_type;
	cs_error_t err;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_NOT_INITIALIZED) {
		return (CS_ERR_BAD_HANDLE);
	}

	if (SAM_RP_MASK (recovery_policy) != SAM_RECOVERY_POLICY_QUIT &&
	    SAM_RP_MASK (recovery_policy) != SAM_RECOVERY_POLICY_RESTART) {
		return (CS_ERR_INVALID_PARAM);
	}

	if (recovery_policy & SAM_RECOVERY_POLICY_QUORUM) {
		/*
		 * Initialize quorum
		 */
		quorum_callbacks.quorum_notify_fn = quorum_notification_fn;
		if ((err = quorum_initialize (&sam_internal_data.quorum_handle, &quorum_callbacks, &quorum_type)) != CS_OK) {
			goto exit_error;
		}

		if ((err = quorum_trackstart (sam_internal_data.quorum_handle, CS_TRACK_CHANGES)) != CS_OK) {
			goto exit_error_quorum;
		}

		if ((err = quorum_fd_get (sam_internal_data.quorum_handle, &sam_internal_data.quorum_fd)) != CS_OK) {
			goto exit_error_quorum;
		}

		/*
		 * Dispatch initial quorate state
		 */
		if ((err = quorum_dispatch (sam_internal_data.quorum_handle, CS_DISPATCH_ONE)) != CS_OK) {
			goto exit_error_quorum;
		}
	}
	sam_internal_data.recovery_policy = recovery_policy;

	sam_internal_data.time_interval = time_interval;

	sam_internal_data.internal_status = SAM_INTERNAL_STATUS_INITIALIZED;

	sam_internal_data.warn_signal = SIGTERM;

	sam_internal_data.am_i_child = 0;

	sam_internal_data.user_data = NULL;
	sam_internal_data.user_data_size = 0;
	sam_internal_data.user_data_allocated = 0;

	pthread_mutex_init (&sam_internal_data.lock, NULL);

	return (CS_OK);

exit_error_quorum:
	quorum_finalize (sam_internal_data.quorum_handle);
exit_error:
	return (err);
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
		tmp_bytes_write = write (d, (const char *)buf + bytes_write,
			(nbyte - bytes_write > SSIZE_MAX) ? SSIZE_MAX : nbyte - bytes_write);

		if (tmp_bytes_write == -1) {
			if (!(errno == EAGAIN || errno == EINTR))
				return -1;
		} else {
			bytes_write += tmp_bytes_write;
		}
	} while (bytes_write != nbyte);

	return (bytes_write);
}

/*
 * Wrapper on top of read(2) function. It handles EAGAIN and EINTR states and reads whole buffer if possible.
 */
static size_t sam_safe_read (
	int d,
	void *buf,
	size_t nbyte)
{
	ssize_t bytes_read;
	ssize_t tmp_bytes_read;

	bytes_read = 0;

	do {
		tmp_bytes_read = read (d, (char *)buf + bytes_read,
			(nbyte - bytes_read > SSIZE_MAX) ? SSIZE_MAX : nbyte - bytes_read);

		if (tmp_bytes_read == -1) {
			if (!(errno == EAGAIN || errno == EINTR))
				return -1;
		} else {
			bytes_read += tmp_bytes_read;
		}

	} while (bytes_read != nbyte && tmp_bytes_read != 0);

	return (bytes_read);
}

static cs_error_t sam_read_reply (
	int child_fd_in)
{
	char reply;
	cs_error_t err;

	if (sam_safe_read (sam_internal_data.child_fd_in, &reply, sizeof (reply)) != sizeof (reply)) {
		return (CS_ERR_LIBRARY);
	}

	switch (reply) {
	case SAM_REPLY_ERROR:
		/*
		 * Read error and return that
		 */
		if (sam_safe_read (sam_internal_data.child_fd_in, &err, sizeof (err)) != sizeof (err)) {
			return (CS_ERR_LIBRARY);
		}

		return (err);
		break;
	case SAM_REPLY_OK:
		/*
		 * Everything correct
		 */
		break;
	default:
		return (CS_ERR_LIBRARY);
		break;
	}

	return (CS_OK);
}

cs_error_t sam_data_getsize (size_t *size)
{
	if (size == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {

		return (CS_ERR_BAD_HANDLE);
	}

	pthread_mutex_lock (&sam_internal_data.lock);

	*size = sam_internal_data.user_data_size;

	pthread_mutex_unlock (&sam_internal_data.lock);

	return (CS_OK);
}

cs_error_t sam_data_restore (
	void *data,
	size_t size)
{
	cs_error_t err;

	err = CS_OK;

	if (data == NULL) {
		return (CS_ERR_INVALID_PARAM);
	}

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {

		return (CS_ERR_BAD_HANDLE);
	}

	pthread_mutex_lock (&sam_internal_data.lock);

	if (sam_internal_data.user_data_size == 0) {
		err = CS_OK;

		goto error_unlock;
	}

	if (size < sam_internal_data.user_data_size) {
		err = CS_ERR_INVALID_PARAM;

		goto error_unlock;
	}

	memcpy (data, sam_internal_data.user_data, sam_internal_data.user_data_size);

	pthread_mutex_unlock (&sam_internal_data.lock);

	return (CS_OK);

error_unlock:
	pthread_mutex_unlock (&sam_internal_data.lock);

	return (err);
}

cs_error_t sam_data_store (
	const void *data,
	size_t size)
{
	cs_error_t err;
	char command;
	char *new_data;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {

		return (CS_ERR_BAD_HANDLE);
	}


	if (data == NULL) {
		size = 0;
	}

	pthread_mutex_lock (&sam_internal_data.lock);

	if (sam_internal_data.am_i_child) {
		/*
		 * We are child so we must send data to parent
		 */
		command = SAM_COMMAND_DATA_STORE;
		if (sam_safe_write (sam_internal_data.child_fd_out, &command, sizeof (command)) != sizeof (command)) {
			err = CS_ERR_LIBRARY;

			goto error_unlock;
		}

		if (sam_safe_write (sam_internal_data.child_fd_out, &size, sizeof (size)) != sizeof (size)) {
			err = CS_ERR_LIBRARY;

			goto error_unlock;
		}

		if (data != NULL && sam_safe_write (sam_internal_data.child_fd_out, data, size) != size) {
			err = CS_ERR_LIBRARY;

			goto error_unlock;
		}

		/*
		 * And wait for reply
		 */
		if ((err = sam_read_reply (sam_internal_data.child_fd_in)) != CS_OK) {
			goto error_unlock;
		}
	}

	/*
	 * We are parent or we received OK reply from parent -> do required action
	 */
	if (data == NULL) {
		free (sam_internal_data.user_data);
		sam_internal_data.user_data = NULL;
		sam_internal_data.user_data_allocated = 0;
		sam_internal_data.user_data_size = 0;
	} else {
		if (sam_internal_data.user_data_allocated < size) {
			if ((new_data = realloc (sam_internal_data.user_data, size)) == NULL) {
				err = CS_ERR_NO_MEMORY;

				goto error_unlock;
			}

			sam_internal_data.user_data_allocated = size;
		} else {
			new_data = sam_internal_data.user_data;
		}
		sam_internal_data.user_data = new_data;
		sam_internal_data.user_data_size = size;

		memcpy (sam_internal_data.user_data, data, size);
	}

	pthread_mutex_unlock (&sam_internal_data.lock);

	return (CS_OK);

error_unlock:
	pthread_mutex_unlock (&sam_internal_data.lock);

	return (err);
}

cs_error_t sam_start (void)
{
	char command;
	cs_error_t err;
	sam_recovery_policy_t recpol;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED) {
		return (CS_ERR_BAD_HANDLE);
	}

	recpol = sam_internal_data.recovery_policy;

	if (recpol & SAM_RECOVERY_POLICY_QUORUM || recpol & SAM_RECOVERY_POLICY_CMAP) {
		pthread_mutex_lock (&sam_internal_data.lock);
	}

	command = SAM_COMMAND_START;

	if (sam_safe_write (sam_internal_data.child_fd_out, &command, sizeof (command)) != sizeof (command)) {
		if (recpol & SAM_RECOVERY_POLICY_QUORUM || recpol & SAM_RECOVERY_POLICY_CMAP) {
			pthread_mutex_unlock (&sam_internal_data.lock);
		}

		return (CS_ERR_LIBRARY);
	}

	if (recpol & SAM_RECOVERY_POLICY_QUORUM || recpol & SAM_RECOVERY_POLICY_CMAP) {
		/*
		 * Wait for parent reply
		 */
		if ((err = sam_read_reply (sam_internal_data.child_fd_in)) != CS_OK) {
			pthread_mutex_unlock (&sam_internal_data.lock);

			return (err);
		}

		pthread_mutex_unlock (&sam_internal_data.lock);
	}

	if (sam_internal_data.hc_callback)
		if (sam_safe_write (sam_internal_data.cb_wpipe_fd, &command, sizeof (command)) != sizeof (command))
			return (CS_ERR_LIBRARY);

	sam_internal_data.internal_status = SAM_INTERNAL_STATUS_STARTED;

	return (CS_OK);
}

cs_error_t sam_stop (void)
{
	char command;
	cs_error_t err;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {
		return (CS_ERR_BAD_HANDLE);
	}

	command = SAM_COMMAND_STOP;

	if (sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_CMAP) {
		pthread_mutex_lock (&sam_internal_data.lock);
	}

	if (sam_safe_write (sam_internal_data.child_fd_out, &command, sizeof (command)) != sizeof (command)) {
		if (sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_CMAP) {
			pthread_mutex_unlock (&sam_internal_data.lock);
		}

		return (CS_ERR_LIBRARY);
	}

	if (sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_CMAP) {
		/*
		 * Wait for parent reply
		 */
		if ((err = sam_read_reply (sam_internal_data.child_fd_in)) != CS_OK) {
			pthread_mutex_unlock (&sam_internal_data.lock);

			return (err);
		}

		pthread_mutex_unlock (&sam_internal_data.lock);
	}

	if (sam_internal_data.hc_callback)
		if (sam_safe_write (sam_internal_data.cb_wpipe_fd, &command, sizeof (command)) != sizeof (command))
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

	if (sam_safe_write (sam_internal_data.child_fd_out, &command, sizeof (command)) != sizeof (command))
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

	free (sam_internal_data.user_data);

exit_error:
	return (CS_OK);
}

cs_error_t sam_mark_failed (void)
{
	char command;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED &&
	    sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED) {
		return (CS_ERR_BAD_HANDLE);
	}

	if (!(sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_CMAP)) {
		return (CS_ERR_INVALID_PARAM);
	}

	command = SAM_COMMAND_MARK_FAILED;

	if (sam_safe_write (sam_internal_data.child_fd_out, &command, sizeof (command)) != sizeof (command))
		return (CS_ERR_LIBRARY);

	return (CS_OK);
}

cs_error_t sam_warn_signal_set (int warn_signal)
{
	char command;
	cs_error_t err;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_REGISTERED &&
		sam_internal_data.internal_status != SAM_INTERNAL_STATUS_STARTED) {
		return (CS_ERR_BAD_HANDLE);
	}

	pthread_mutex_lock (&sam_internal_data.lock);

	if (sam_internal_data.am_i_child) {
		/*
		 * We are child so we must send data to parent
		 */
		command = SAM_COMMAND_WARN_SIGNAL_SET;
		if (sam_safe_write (sam_internal_data.child_fd_out, &command, sizeof (command)) != sizeof (command)) {
			err = CS_ERR_LIBRARY;

			goto error_unlock;
		}

		if (sam_safe_write (sam_internal_data.child_fd_out, &warn_signal, sizeof (warn_signal)) !=
		   sizeof (warn_signal)) {
			err = CS_ERR_LIBRARY;

			goto error_unlock;
		}

		/*
		 * And wait for reply
		 */
		if ((err = sam_read_reply (sam_internal_data.child_fd_in)) != CS_OK) {
			goto error_unlock;
		}
	}

	/*
	 * We are parent or we received OK reply from parent -> do required action
	 */
	sam_internal_data.warn_signal = warn_signal;

	pthread_mutex_unlock (&sam_internal_data.lock);

	return (CS_OK);

error_unlock:
	pthread_mutex_unlock (&sam_internal_data.lock);

	return (err);
}

static cs_error_t sam_parent_reply_send (
	cs_error_t err,
	int parent_fd_in,
	int parent_fd_out)
{
	char reply;

	if (err == CS_OK) {
		reply = SAM_REPLY_OK;

		if (sam_safe_write (parent_fd_out, &reply, sizeof (reply)) != sizeof (reply)) {
			err = CS_ERR_LIBRARY;
			goto error_reply;
		}

		return (CS_OK);
	}

error_reply:
	reply = SAM_REPLY_ERROR;
	if (sam_safe_write (parent_fd_out, &reply, sizeof (reply)) != sizeof (reply)) {
		return (CS_ERR_LIBRARY);
	}
	if (sam_safe_write (parent_fd_out, &err, sizeof (err)) != sizeof (err)) {
		return (CS_ERR_LIBRARY);
	}

	return (err);
}


static cs_error_t sam_parent_warn_signal_set (
	int parent_fd_in,
	int parent_fd_out)
{
	int warn_signal;
	cs_error_t err;

	err = CS_OK;

	if (sam_safe_read (parent_fd_in, &warn_signal, sizeof (warn_signal)) != sizeof (warn_signal)) {
		err = CS_ERR_LIBRARY;
		goto error_reply;
	}

	err = sam_warn_signal_set (warn_signal);
	if (err != CS_OK) {
		goto error_reply;
	}


	return (sam_parent_reply_send (CS_OK, parent_fd_in, parent_fd_out));

error_reply:
	return (sam_parent_reply_send (err, parent_fd_in, parent_fd_out));
}

static cs_error_t sam_parent_wait_for_quorum (
	int parent_fd_in,
	int parent_fd_out)
{
	cs_error_t err;
	struct pollfd pfds[2];
	int poll_err;

	if (sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_CMAP) {
		if ((err = sam_cmap_update_key (SAM_CMAP_KEY_STATE, SAM_CMAP_S_Q_WAIT)) != CS_OK) {
			goto error_reply;
		}
	}

	/*
	 * Update current quorum
	 */
	if ((err = quorum_dispatch (sam_internal_data.quorum_handle, CS_DISPATCH_ALL)) != CS_OK) {
		goto error_reply;
	}

	/*
	 * Wait for quorum
	 */
	while (!sam_internal_data.quorate) {
		pfds[0].fd = parent_fd_in;
		pfds[0].events = 0;
		pfds[0].revents = 0;

		pfds[1].fd = sam_internal_data.quorum_fd;
		pfds[1].events = POLLIN;
		pfds[1].revents = 0;

		poll_err = poll (pfds, 2, -1);

		if (poll_err == -1) {
			/*
			 *  Error in poll
			 *  If it is EINTR, continue, otherwise QUIT
			 */
			if (errno != EINTR) {
				err = CS_ERR_LIBRARY;
				goto error_reply;
			}
		}

		if (pfds[0].revents != 0) {
			if (pfds[0].revents == POLLERR || pfds[0].revents == POLLHUP ||pfds[0].revents == POLLNVAL) {
				/*
				 * Child has exited
				 */
				return (CS_OK);
			}
		}

		if (pfds[1].revents != 0) {
			if ((err = quorum_dispatch (sam_internal_data.quorum_handle, CS_DISPATCH_ONE)) != CS_OK) {
				goto error_reply;
			}
		}
	}

	if (sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_CMAP) {
		if ((err = sam_cmap_update_key (SAM_CMAP_KEY_STATE, SAM_CMAP_S_STARTED)) != CS_OK) {
			goto error_reply;
		}
	}

	return (sam_parent_reply_send (CS_OK, parent_fd_in, parent_fd_out));

error_reply:
	if (sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_CMAP) {
		sam_cmap_update_key (SAM_CMAP_KEY_STATE, SAM_CMAP_S_REGISTERED);
	}

	return (sam_parent_reply_send (err, parent_fd_in, parent_fd_out));
}

static cs_error_t sam_parent_cmap_state_set (
	int parent_fd_in,
	int parent_fd_out,
	int state)
{
	cs_error_t err;
	const char *state_s;

	if (state == 1) {
		state_s = SAM_CMAP_S_STARTED;
	} else {
		state_s = SAM_CMAP_S_REGISTERED;
	}

	if ((err = sam_cmap_update_key (SAM_CMAP_KEY_STATE, state_s)) != CS_OK) {
		goto error_reply;
	}

	return (sam_parent_reply_send (CS_OK, parent_fd_in, parent_fd_out));

error_reply:
	return (sam_parent_reply_send (err, parent_fd_in, parent_fd_out));
}

static cs_error_t sam_parent_kill_child (
	int *action,
	pid_t child_pid)
{
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
		*action = SAM_PARENT_ACTION_RECOVERY;
	}

	return (CS_OK);
}

static cs_error_t sam_parent_mark_child_failed (
	int *action,
	pid_t child_pid)
{
	sam_recovery_policy_t recpol;

	recpol = sam_internal_data.recovery_policy;

	sam_internal_data.term_send = 1;
	sam_internal_data.recovery_policy = SAM_RECOVERY_POLICY_QUIT |
	    (SAM_RP_MASK_C (recpol) ? SAM_RECOVERY_POLICY_CMAP : 0) |
	    (SAM_RP_MASK_Q (recpol) ? SAM_RECOVERY_POLICY_QUORUM : 0);

	return (sam_parent_kill_child (action, child_pid));
}

static cs_error_t sam_parent_data_store (
	int parent_fd_in,
	int parent_fd_out)
{
	char *user_data;
	ssize_t size;
	cs_error_t err;

	err = CS_OK;
	user_data = NULL;

	if (sam_safe_read (parent_fd_in, &size, sizeof (size)) != sizeof (size)) {
		err = CS_ERR_LIBRARY;
		goto error_reply;
	}

	if (size > 0) {
		user_data = malloc (size);
		if (user_data == NULL) {
			err = CS_ERR_NO_MEMORY;
			goto error_reply;
		}

		if (sam_safe_read (parent_fd_in, user_data, size) != size) {
			err = CS_ERR_LIBRARY;
			goto free_error_reply;
		}
	}

	err = sam_data_store (user_data, size);
	if (err != CS_OK) {
		goto free_error_reply;
	}

	free (user_data);

	return (sam_parent_reply_send (CS_OK, parent_fd_in, parent_fd_out));

free_error_reply:
	free (user_data);
error_reply:
	return (sam_parent_reply_send (err, parent_fd_in, parent_fd_out));
}

static enum sam_parent_action_t sam_parent_handler (
	int parent_fd_in,
	int parent_fd_out,
	pid_t child_pid)
{
	int poll_error;
	int action;
	int status;
	ssize_t bytes_read;
	char command;
	int time_interval;
	struct pollfd pfds[2];
	nfds_t nfds;
	cs_error_t err;
	sam_recovery_policy_t recpol;

	status = 0;

	action = SAM_PARENT_ACTION_CONTINUE;
	recpol = sam_internal_data.recovery_policy;

	while (action == SAM_PARENT_ACTION_CONTINUE) {
		pfds[0].fd = parent_fd_in;
		pfds[0].events = POLLIN;
		pfds[0].revents = 0;
		nfds = 1;

		if (status == 1 && sam_internal_data.time_interval != 0) {
			time_interval = sam_internal_data.time_interval;
		} else {
			time_interval = -1;
		}

		if (recpol & SAM_RECOVERY_POLICY_QUORUM) {
			pfds[nfds].fd = sam_internal_data.quorum_fd;
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			nfds++;
		}

		poll_error = poll (pfds, nfds, time_interval);

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
				sam_parent_kill_child (&action, child_pid);
			}
		}

		if (poll_error > 0) {
			if (pfds[0].revents != 0) {
				/*
				 *  We have EOF or command in pipe
				 */
				bytes_read = sam_safe_read (parent_fd_in, &command, 1);

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
					action = SAM_PARENT_ACTION_ERROR;
					goto action_exit;
				}

				if (recpol & SAM_RECOVERY_POLICY_CMAP) {
					sam_cmap_update_key (SAM_CMAP_KEY_LAST_HC, NULL);
				}

				/*
				 * We have read command
				 */
				switch (command) {
				case SAM_COMMAND_START:
					if (status == 0) {
						/*
						 *  Not started yet
						 */
						if (recpol & SAM_RECOVERY_POLICY_QUORUM) {
							if (sam_parent_wait_for_quorum (parent_fd_in,
							    parent_fd_out) != CS_OK) {
								continue;
							}
						}

						if (recpol & SAM_RECOVERY_POLICY_CMAP) {
							if (sam_parent_cmap_state_set (parent_fd_in,
							    parent_fd_out, 1) != CS_OK) {
								continue;
							    }
						}

						status = 1;
					}
					break;
				case SAM_COMMAND_STOP:
					if (status == 1) {
						/*
						 *  Started
						 */
						if (recpol & SAM_RECOVERY_POLICY_CMAP) {
							if (sam_parent_cmap_state_set (parent_fd_in,
							    parent_fd_out, 0) != CS_OK) {
								continue;
							    }
						}

						status = 0;
					}
					break;
				case SAM_COMMAND_DATA_STORE:
					sam_parent_data_store (parent_fd_in, parent_fd_out);
					break;
				case SAM_COMMAND_WARN_SIGNAL_SET:
					sam_parent_warn_signal_set (parent_fd_in, parent_fd_out);
					break;
				case SAM_COMMAND_MARK_FAILED:
					status = 1;
					sam_parent_mark_child_failed (&action, child_pid);
					break;
				}
			} /* if (pfds[0].revents != 0) */

			if ((sam_internal_data.recovery_policy & SAM_RECOVERY_POLICY_QUORUM) &&
			    pfds[1].revents != 0) {
				/*
				 * Handle quorum change
				 */
				err = quorum_dispatch (sam_internal_data.quorum_handle, CS_DISPATCH_ALL);

				if (status == 1 &&
				    (!sam_internal_data.quorate || (err != CS_ERR_TRY_AGAIN && err != CS_OK))) {
					sam_parent_kill_child (&action, child_pid);
				}
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
	int pipe_fd_out[2], pipe_fd_in[2];
	enum sam_parent_action_t action, old_action;
	int child_status;
	sam_recovery_policy_t recpol;

	if (sam_internal_data.internal_status != SAM_INTERNAL_STATUS_INITIALIZED) {
		return (CS_ERR_BAD_HANDLE);
	}

	recpol = sam_internal_data.recovery_policy;

	if (recpol & SAM_RECOVERY_POLICY_CMAP) {
		/*
		 * Register to cmap
		 */
		if ((error = sam_cmap_register ()) != CS_OK) {
			goto error_exit;
		}
	}

	error = CS_OK;

	while (1) {
		if ((pipe_error = pipe (pipe_fd_out)) != 0) {
			error = CS_ERR_LIBRARY;
			goto error_exit;
		}

		if ((pipe_error = pipe (pipe_fd_in)) != 0) {
			close (pipe_fd_out[0]);
			close (pipe_fd_out[1]);

			error = CS_ERR_LIBRARY;
			goto error_exit;
		}

		if (recpol & SAM_RECOVERY_POLICY_CMAP) {
			if ((error = sam_cmap_update_key (SAM_CMAP_KEY_STATE, SAM_CMAP_S_REGISTERED)) != CS_OK) {
				goto error_exit;
			}
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
			close (pipe_fd_out[0]);
			close (pipe_fd_in[1]);

			sam_internal_data.child_fd_out = pipe_fd_out[1];
			sam_internal_data.child_fd_in = pipe_fd_in[0];

			if (instance_id)
				*instance_id = sam_internal_data.instance_id;

			sam_internal_data.am_i_child = 1;
			sam_internal_data.internal_status = SAM_INTERNAL_STATUS_REGISTERED;

			pthread_mutex_init (&sam_internal_data.lock, NULL);

			goto error_exit;
		} else {
			/*
			 *  Parent process
			 */
			close (pipe_fd_out[1]);
			close (pipe_fd_in[0]);

			action = sam_parent_handler (pipe_fd_out[0], pipe_fd_in[1], pid);

			close (pipe_fd_out[0]);
			close (pipe_fd_in[1]);

			if (action == SAM_PARENT_ACTION_ERROR) {
				error = CS_ERR_LIBRARY;
				goto error_exit;
			}

			/*
			 * We really don't like zombies
			 */
			while (waitpid (pid, &child_status, 0) == -1 && errno == EINTR)
				;

			old_action = action;

			if (action == SAM_PARENT_ACTION_RECOVERY) {
				if (SAM_RP_MASK (sam_internal_data.recovery_policy) == SAM_RECOVERY_POLICY_QUIT)
					action = SAM_PARENT_ACTION_QUIT;
			}


			if (action == SAM_PARENT_ACTION_QUIT) {
				if (recpol & SAM_RECOVERY_POLICY_QUORUM) {
					quorum_finalize (sam_internal_data.quorum_handle);
				}

				if (recpol & SAM_RECOVERY_POLICY_CMAP) {
					if (old_action == SAM_PARENT_ACTION_RECOVERY) {
						/*
						 * Mark as failed
						 */
						sam_cmap_update_key (SAM_CMAP_KEY_STATE, SAM_CMAP_S_FAILED);
					} else {
						sam_cmap_destroy_pid_path ();
					}
				}

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
			bytes_readed = sam_safe_read (sam_internal_data.cb_rpipe_fd, &command, 1);

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
