/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :

 * Copyright (c) 2004-2005 MontaVista Software, Inc.
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
#include <errno.h>

#include "../include/saAis.h"
#include "../include/evs.h"
#include "../include/ipc_evs.h"
#include "util.h"

struct evs_inst {
	int response_fd;
	int dispatch_fd;
	int finalize;
	evs_callbacks_t callbacks;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

struct res_overlay {
	struct res_header header;
	char data[512000];
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
}


/**
 * @defgroup evs_openais The extended virtual synchrony passthrough API
 * @ingroup openais
 *
 * @{
 */
/**
 * test
 * @param handle The handle of evs initialize
 * @param callbacks The callbacks for evs_initialize
 * @returns EVS_OK
 */
evs_error_t evs_initialize (
	evs_handle_t *handle,
	evs_callbacks_t *callbacks)
{
	SaAisErrorT error;
	struct evs_inst *evs_inst;

	error = saHandleCreate (&evs_handle_t_db, sizeof (struct evs_inst), handle);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&evs_handle_t_db, *handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = saServiceConnectTwo (&evs_inst->response_fd,
		&evs_inst->dispatch_fd,
		EVS_SERVICE);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	memcpy (&evs_inst->callbacks, callbacks, sizeof (evs_callbacks_t));

	pthread_mutex_init (&evs_inst->response_mutex, NULL);

	pthread_mutex_init (&evs_inst->dispatch_mutex, NULL);

	saHandleInstancePut (&evs_handle_t_db, *handle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&evs_handle_t_db, *handle);
error_destroy:
	saHandleDestroy (&evs_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

evs_error_t evs_finalize (
	evs_handle_t handle)
{
	struct evs_inst *evs_inst;
	SaAisErrorT error;

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}
//	  TODO is the locking right here
	pthread_mutex_lock (&evs_inst->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (evs_inst->finalize) {
		pthread_mutex_unlock (&evs_inst->response_mutex);
		saHandleInstancePut (&evs_handle_t_db, handle);
		return (EVS_ERR_BAD_HANDLE);
	}

	evs_inst->finalize = 1;

	pthread_mutex_unlock (&evs_inst->response_mutex);

	saHandleDestroy (&evs_handle_t_db, handle);
    /*
     * Disconnect from the server
     */
    if (evs_inst->response_fd != -1) {
        shutdown(evs_inst->response_fd, 0);
        close(evs_inst->response_fd);
    }
    if (evs_inst->dispatch_fd != -1) {
        shutdown(evs_inst->dispatch_fd, 0);
        close(evs_inst->dispatch_fd);
    }
	saHandleInstancePut (&evs_handle_t_db, handle);


	return (EVS_OK);
}

evs_error_t evs_fd_get (
	evs_handle_t handle,
	int *fd)
{
	SaAisErrorT error;
	struct evs_inst *evs_inst;

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	*fd = evs_inst->dispatch_fd; 

	saHandleInstancePut (&evs_handle_t_db, handle);

	return (SA_AIS_OK);
}

evs_error_t evs_dispatch (
	evs_handle_t handle,
	evs_dispatch_t dispatch_types)
{
	struct pollfd ufds;
	int timeout = -1;
	SaAisErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct evs_inst *evs_inst;
	struct res_evs_confchg_callback *res_evs_confchg_callback;
	struct res_evs_deliver_callback *res_evs_deliver_callback;
	evs_callbacks_t callbacks;
	struct res_overlay dispatch_data;
	int ignore_dispatch = 0;

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
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
		ufds.fd = evs_inst->dispatch_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto error_nounlock;
		}

		pthread_mutex_lock (&evs_inst->dispatch_mutex);

		/*
		 * Regather poll data in case ufds has changed since taking lock
		 */
		error = saPollRetry (&ufds, 1, 0);
		if (error != SA_AIS_OK) {
			goto error_nounlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (evs_inst->finalize == 1) {
			error = EVS_OK;
			pthread_mutex_unlock (&evs_inst->dispatch_mutex);
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatch_types == EVS_DISPATCH_ALL) {
			pthread_mutex_unlock (&evs_inst->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else 
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&evs_inst->dispatch_mutex);
			continue; /* next poll */
		}

		if (ufds.revents & POLLIN) {
			/*
			 * Queue empty, read response from socket
			 */
			error = saRecvRetry (evs_inst->dispatch_fd, &dispatch_data.header,
				sizeof (struct res_header));
			if (error != SA_AIS_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (struct res_header)) {
				error = saRecvRetry (evs_inst->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (struct res_header));

				if (error != SA_AIS_OK) {
					goto error_unlock;
				}
			}
		} else {
			pthread_mutex_unlock (&evs_inst->dispatch_mutex);
			continue;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that evsFinalize has been called.
		*/
		memcpy (&callbacks, &evs_inst->callbacks, sizeof (evs_callbacks_t));

		pthread_mutex_unlock (&evs_inst->dispatch_mutex);
		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data.header.id) {
		case MESSAGE_RES_EVS_DELIVER_CALLBACK:
			res_evs_deliver_callback = (struct res_evs_deliver_callback *)&dispatch_data;
			callbacks.evs_deliver_fn (
				&res_evs_deliver_callback->evs_address,
				&res_evs_deliver_callback->msg,
				res_evs_deliver_callback->msglen);
			break;

		case MESSAGE_RES_EVS_CONFCHG_CALLBACK:
			res_evs_confchg_callback = (struct res_evs_confchg_callback *)&dispatch_data;
			callbacks.evs_confchg_fn (
				res_evs_confchg_callback->member_list,
				res_evs_confchg_callback->member_list_entries,
				res_evs_confchg_callback->left_list,
				res_evs_confchg_callback->left_list_entries,
				res_evs_confchg_callback->joined_list,
				res_evs_confchg_callback->joined_list_entries);
			break;

		default:
			error = SA_AIS_ERR_LIBRARY;
			goto error_nounlock;
			break;
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
	saHandleInstancePut (&evs_handle_t_db, handle);
error_nounlock:
	return (error);
}

evs_error_t evs_join (
    evs_handle_t handle,
    struct evs_group *groups,
	int group_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[2];
	struct req_lib_evs_join req_lib_evs_join;
	struct res_lib_evs_join res_lib_evs_join;

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
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
	
	pthread_mutex_lock (&evs_inst->response_mutex);

	error = saSendMsgReceiveReply (evs_inst->response_fd, iov, 2,
		&res_lib_evs_join, sizeof (struct res_lib_evs_join));

	pthread_mutex_unlock (&evs_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_join.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_leave (
    evs_handle_t handle,
    struct evs_group *groups,
	int group_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov[2];
	struct req_lib_evs_leave req_lib_evs_leave;
	struct res_lib_evs_leave res_lib_evs_leave;

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
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
	
	pthread_mutex_lock (&evs_inst->response_mutex);

	error = saSendMsgReceiveReply (evs_inst->response_fd, iov, 2,
		&res_lib_evs_leave, sizeof (struct res_lib_evs_leave));

	pthread_mutex_unlock (&evs_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_leave.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_mcast_joined (
	evs_handle_t handle,
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

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
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
	
	pthread_mutex_lock (&evs_inst->response_mutex);

	error = saSendMsgReceiveReply (evs_inst->response_fd, iov, iov_len + 1,
		&res_lib_evs_mcast_joined, sizeof (struct res_lib_evs_mcast_joined));

	pthread_mutex_unlock (&evs_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_mcast_joined.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_mcast_groups (
	evs_handle_t handle,
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

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
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
	
	pthread_mutex_lock (&evs_inst->response_mutex);

	error = saSendMsgReceiveReply (evs_inst->response_fd, iov, iov_len + 2,
		&res_lib_evs_mcast_groups, sizeof (struct res_lib_evs_mcast_groups));

	pthread_mutex_unlock (&evs_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_mcast_groups.header.error;

error_exit:
	saHandleInstancePut (&evs_handle_t_db, handle);

	return (error);
}

evs_error_t evs_membership_get (
	evs_handle_t handle,
	struct evs_address *local_addr,
	struct evs_address *member_list,
	int *member_list_entries)
{
	evs_error_t error;
	struct evs_inst *evs_inst;
	struct iovec iov;
	struct req_lib_evs_membership_get req_lib_evs_membership_get;
	struct res_lib_evs_membership_get res_lib_evs_membership_get;

	error = saHandleInstanceGet (&evs_handle_t_db, handle, (void *)&evs_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_evs_membership_get.header.size = sizeof (struct req_lib_evs_membership_get);
	req_lib_evs_membership_get.header.id = MESSAGE_REQ_EVS_MEMBERSHIP_GET;

	iov.iov_base = &req_lib_evs_membership_get;
	iov.iov_len = sizeof (struct req_lib_evs_membership_get);

	pthread_mutex_lock (&evs_inst->response_mutex);

	error = saSendMsgReceiveReply (evs_inst->response_fd, &iov, 1,
		&res_lib_evs_membership_get, sizeof (struct res_lib_evs_membership_get));

	pthread_mutex_unlock (&evs_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_evs_membership_get.header.error;

	/*
	 * Copy results to caller
	 */
	if (local_addr) {
		memcpy (local_addr, &res_lib_evs_membership_get.local_addr, sizeof (struct in_addr));
 	}
	*member_list_entries = *member_list_entries < res_lib_evs_membership_get.member_list_entries ?
		*member_list_entries : res_lib_evs_membership_get.member_list_entries;
	if (member_list) {
		memcpy (member_list, &res_lib_evs_membership_get.member_list, 
			*member_list_entries * sizeof (struct in_addr));
	}

error_exit:
	saHandleInstancePut (&evs_handle_t_db, handle);

	return (error);
}

/** @} */
