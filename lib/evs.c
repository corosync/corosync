/*
 * Copyright (c) 2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
 * Provides an extended virtual synchrony API using the openais executive
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "util.h"

#include "../include/ais_msg.h"
#include "../include/ais_types.h"
#include "../include/evs.h"

struct evs_inst {
	int fd;
	int finalize;
	evs_callbacks_t callbacks;
	struct queue inq;
	char dispatch_buffer[512000];
	pthread_mutex_t mutex;
};

static void evs_instance_destructor (void *instance);

static struct saHandleDatabase evs_handle_t_db = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= evs_instance_destructor
};

/*
 * Clean up function for an evt instance (saEvtInitialize) handle
 */
static void evs_instance_destructor (void *instance)
{
    struct evs_inst *evs_inst = instance;
    void **msg;
    int empty;

    /*
     * Empty out the queue if there are any pending messages
     */
    while ((saQueueIsEmpty(&evs_inst->inq, &empty) == SA_OK) && !empty) {
        saQueueItemGet(&evs_inst->inq, (void *)&msg);
        saQueueItemRemove(&evs_inst->inq);
        free(*msg);
    }

    /*
     * clean up the queue itself
     */
    if (evs_inst->inq.items) {
            free(evs_inst->inq.items);
    }

    /*
     * Disconnect from the server
     */
    if (evs_inst->fd != -1) {
        shutdown(evs_inst->fd, 0);
        close(evs_inst->fd);
    }
}


evs_error_t evs_initialize (
	evs_handle_t *handle,
	evs_callbacks_t *callbacks)
{
	SaErrorT error;
	struct evs_inst *evs_inst;

	error = saHandleCreate (&evs_handle_t_db, sizeof (struct evs_inst), handle);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		goto error_destroy;
	}

	/*
	 * An inq is needed to store async messages while waiting for a
	 * sync response
	 */
	error = saQueueInit (&evs_inst->inq, 128, sizeof (void *));
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	error = saServiceConnect (&evs_inst->fd, MESSAGE_REQ_EVS_INIT);
	if (error != SA_OK) {
		goto error_put_destroy;
	}
	
	memcpy (&evs_inst->callbacks, callbacks, sizeof (evs_callbacks_t));

	pthread_mutex_init (&evs_inst->mutex, NULL);

	saHandleInstancePut (&evs_handle_t_db, *handle);

	return (SA_OK);

error_put_destroy:
	saHandleInstancePut (&evs_handle_t_db, *handle);
error_destroy:
	saHandleDestroy (&evs_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

evs_error_t evs_finalize (
	evs_handle_t *handle)
{
	struct evs_inst *evs_inst;
	SaErrorT error;

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		return (error);
	}
	/*
	 * Another thread has already started finalizing
	 */
	if (evs_inst->finalize) {
		pthread_mutex_unlock (&evs_inst->mutex);
		saHandleInstancePut (&evs_handle_t_db, *handle);
		return (EVS_ERR_BAD_HANDLE);
	}

	evs_inst->finalize = 1;

	saActivatePoll (evs_inst->fd);

	pthread_mutex_unlock (&evs_inst->mutex);

	saHandleInstancePut (&evs_handle_t_db, *handle);

	saHandleDestroy (&evs_handle_t_db, *handle);

	return (EVS_OK);
}

evs_error_t evs_fd_get (
	evs_handle_t *handle,
	int *fd)
{
	SaErrorT error;
	struct evs_inst *evs_inst;

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		return (error);
	}

	*fd = evs_inst->fd; 

	saHandleInstancePut (&evs_handle_t_db, *handle);

	return (SA_OK);
}

struct message_overlay {
	struct res_header header;
	char data[4096];
};

evs_error_t evs_dispatch (
	evs_handle_t *handle,
	evs_dispatch_t dispatch_types)
{
	struct pollfd ufds;
	int timeout = -1;
	SaErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	int poll_fd;
	struct evs_inst *evs_inst;
	struct res_evs_confchg_callback *res_evs_confchg_callback;
	struct res_evs_deliver_callback *res_evs_deliver_callback;
	evs_callbacks_t callbacks;
	struct message_overlay *dispatch_data;
	int empty;
	struct res_header **queue_msg;
	struct res_header *msg = NULL;
	int ignore_dispatch = 0;

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ONE or SA_DISPATCH_ALL and
	 * wait indefinately for SA_DISPATCH_BLOCKING
	 */
	if (dispatch_types == EVS_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		poll_fd = evs_inst->fd;

		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		pthread_mutex_lock (&evs_inst->mutex);
		saQueueIsEmpty (&evs_inst->inq, &empty);
		if (empty == 1) {
			pthread_mutex_unlock (&evs_inst->mutex);

			error = saPollRetry (&ufds, 1, timeout);
			if (error != SA_OK) {
				goto error_nounlock;
			}

			pthread_mutex_lock (&evs_inst->mutex);
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (evs_inst->finalize == 1) {
			error = SA_OK;
			pthread_mutex_unlock (&evs_inst->mutex);
			goto error_unlock;
		}

		dispatch_avail = (ufds.revents & POLLIN) | (empty == 0);
		if (dispatch_avail == 0 && dispatch_types == EVS_DISPATCH_ALL) {
			pthread_mutex_unlock (&evs_inst->mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&evs_inst->mutex);
			continue; /* next poll */
		}

		saQueueIsEmpty (&evs_inst->inq, &empty);
		if (empty == 0) {
			/*
			 * Queue is not empty, read data from queue
			 */
			saQueueItemGet (&evs_inst->inq, (void *)&queue_msg);
			msg = *queue_msg;
			dispatch_data = (struct message_overlay *)msg;
			res_evs_deliver_callback = (struct res_evs_deliver_callback *)msg;
			res_evs_confchg_callback = (struct res_evs_confchg_callback *)msg;

			saQueueItemRemove (&evs_inst->inq);
		} else {
			dispatch_data = (struct message_overlay *)evs_inst->dispatch_buffer;
			res_evs_deliver_callback = (struct res_evs_deliver_callback *)dispatch_data;
			res_evs_confchg_callback = (struct res_evs_confchg_callback *)dispatch_data;
			/*
			* Queue empty, read response from socket
			*/
			error = saRecvRetry (evs_inst->fd, &dispatch_data->header,
				sizeof (struct res_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_unlock;
			}
			if (dispatch_data->header.size > sizeof (struct res_header)) {
				error = saRecvRetry (evs_inst->fd, &dispatch_data->data,
					dispatch_data->header.size - sizeof (struct res_header),
					MSG_WAITALL | MSG_NOSIGNAL);
				if (error != SA_OK) {
					goto error_unlock;
				}
			}
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that evsFinalize has been called.
		*/
		memcpy (&callbacks, &evs_inst->callbacks, sizeof (evs_callbacks_t));

		pthread_mutex_unlock (&evs_inst->mutex);
		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data->header.id) {
		case MESSAGE_RES_LIB_ACTIVATEPOLL:
			ignore_dispatch = 1;
			break;

		case MESSAGE_RES_EVS_DELIVER_CALLBACK:
			callbacks.evs_deliver_fn (
				res_evs_deliver_callback->source_addr,
				&res_evs_deliver_callback->msg,
				res_evs_deliver_callback->msglen);
			break;

		case MESSAGE_RES_EVS_CONFCHG_CALLBACK:
			callbacks.evs_confchg_fn (
				res_evs_confchg_callback->member_list,
				res_evs_confchg_callback->member_list_entries,
				res_evs_confchg_callback->left_list,
				res_evs_confchg_callback->left_list_entries,
				res_evs_confchg_callback->joined_list,
				res_evs_confchg_callback->joined_list_entries);
			break;

		default:
			error = SA_ERR_LIBRARY;
			goto error_nounlock;
			break;
		}
		if (empty == 0) {
			free (msg);
		}

		/*
		 * Determine if more messages should be processed
		 * */
		switch (dispatch_types) {
		case EVS_DISPATCH_ONE:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			} else {
				cont = 0;
			}
			break;
		case EVS_DISPATCH_ALL:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			}
			break;
		case EVS_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

error_unlock:
	saHandleInstancePut (&evs_handle_t_db, *handle);
error_nounlock:
	return (error);
}

evs_error_t evs_join (
    evs_handle_t *handle,
    struct evs_group *groups,
	int group_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[2];
	struct req_lib_evs_join req_lib_evs_join;
	struct res_lib_evs_join res_lib_evs_join;

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		return (error);
	}
	req_lib_evs_join.header.size = sizeof (struct req_lib_evs_join) + 
		(group_entries * sizeof (struct evs_group));
	req_lib_evs_join.header.id = MESSAGE_REQ_EVS_JOIN;
	req_lib_evs_join.group_entries = group_entries;

	iov[0].iov_base = &req_lib_evs_join;
	iov[0].iov_len = sizeof (struct req_lib_evs_join);
	iov[1].iov_base = groups;
	iov[1].iov_len = (group_entries * sizeof (struct evs_group));
	
	error = saSendMsgRetry (evs_inst->fd, iov, 2);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (evs_inst->fd, &res_lib_evs_join,
		sizeof (struct res_lib_evs_join), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = res_lib_evs_join.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, *handle);

	return (error);
}

evs_error_t evs_leave (
    evs_handle_t *handle,
    struct evs_group *groups,
	int group_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[2];
	struct req_lib_evs_leave req_lib_evs_leave;
	struct res_lib_evs_leave res_lib_evs_leave;

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		return (error);
	}
	req_lib_evs_leave.header.size = sizeof (struct req_lib_evs_leave) + 
		(group_entries * sizeof (struct evs_group));
	req_lib_evs_leave.header.id = MESSAGE_REQ_EVS_LEAVE;
	req_lib_evs_leave.group_entries = group_entries;

	iov[0].iov_base = &req_lib_evs_leave;
	iov[0].iov_len = sizeof (struct req_lib_evs_leave);
	iov[1].iov_base = groups;
	iov[1].iov_len = (group_entries * sizeof (struct evs_group));
	
	error = saSendMsgRetry (evs_inst->fd, iov, 2);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (evs_inst->fd, &res_lib_evs_leave,
		sizeof (struct res_lib_evs_leave), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = res_lib_evs_leave.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, *handle);

	return (error);
}

evs_error_t evs_mcast_joined (
	evs_handle_t *handle,
	evs_guarantee_t guarantee,
	struct iovec *iovec,
	int iov_len)
{
	int i;
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[64];
	struct req_lib_evs_mcast_joined req_lib_evs_mcast_joined;
	struct res_lib_evs_mcast_joined res_lib_evs_mcast_joined;
	int msg_len = 0;

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		return (error);
	}

	for (i = 0; i < iov_len; i++ ) {
		msg_len += iovec[i].iov_len;
	}

	req_lib_evs_mcast_joined.header.size = sizeof (struct req_lib_evs_mcast_joined) +
		msg_len;

	req_lib_evs_mcast_joined.header.id = MESSAGE_REQ_EVS_MCAST_JOINED;
	req_lib_evs_mcast_joined.guarantee = guarantee;
	req_lib_evs_mcast_joined.msg_len = msg_len;

	iov[0].iov_base = &req_lib_evs_mcast_joined;
	iov[0].iov_len = sizeof (struct req_lib_evs_mcast_joined);
	memcpy (&iov[1], iovec, iov_len * sizeof (struct iovec));
	
	error = saSendMsgRetry (evs_inst->fd, iov, 1 + iov_len);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvQueue (evs_inst->fd, &res_lib_evs_mcast_joined, &evs_inst->inq,
		MESSAGE_RES_EVS_MCAST_JOINED);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = res_lib_evs_mcast_joined.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, *handle);

	return (error);
}

evs_error_t evs_mcast_groups (
	evs_handle_t *handle,
	evs_guarantee_t guarantee,
	struct evs_group *groups,
	int group_entries,
	struct iovec *iovec,
	int iov_len)
{
	int i;
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[64];
	struct req_lib_evs_mcast_groups req_lib_evs_mcast_groups;
	struct res_lib_evs_mcast_groups res_lib_evs_mcast_groups;
	int msg_len = 0;

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_OK) {
		return (error);
	}
	for (i = 0; i < iov_len; i++) {
		msg_len += iovec[i].iov_len;
	}
	req_lib_evs_mcast_groups.header.size = sizeof (struct req_lib_evs_mcast_groups) + 
		(group_entries * sizeof (struct evs_group)) + msg_len;
	req_lib_evs_mcast_groups.header.id = MESSAGE_REQ_EVS_MCAST_GROUPS;
	req_lib_evs_mcast_groups.guarantee = guarantee;
	req_lib_evs_mcast_groups.msg_len = msg_len;
	req_lib_evs_mcast_groups.group_entries = group_entries;

	iov[0].iov_base = &req_lib_evs_mcast_groups;
	iov[0].iov_len = sizeof (struct req_lib_evs_mcast_groups);
	iov[1].iov_base = groups;
	iov[1].iov_len = (group_entries * sizeof (struct evs_group));
	memcpy (&iov[2], iovec, iov_len * sizeof (struct iovec));
	
	error = saSendMsgRetry (evs_inst->fd, iov, 2 + iov_len);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvQueue (evs_inst->fd, &res_lib_evs_mcast_groups, &evs_inst->inq,
		MESSAGE_RES_EVS_MCAST_GROUPS);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = res_lib_evs_mcast_groups.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, *handle);

	return (error);
}
