
/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "../include/ais_types.h"
#include "../include/ais_clm.h"
#include "../include/ais_msg.h"
#include "util.h"

struct message_overlay {
	struct message_header header;
	char data[4096];
};

struct clmInstance {
	int fd;
	SaClmCallbacksT callbacks;
	int finalize;
	pthread_mutex_t mutex;
};

static void clmHandleInstanceDestructor (void *);

static struct saHandleDatabase clmHandleDatabase = {
	handleCount: 0,
	handles: 0,
	mutex: PTHREAD_MUTEX_INITIALIZER,
	handleInstanceDestructor: clmHandleInstanceDestructor
};

/*
 * Versions supported
 */
static SaVersionT clmVersionsSupported[] = {
	{ 'A', 1, 1 },
	{ 'a', 1, 1 }
};

static struct saVersionDatabase clmVersionDatabase = {
	sizeof (clmVersionsSupported) / sizeof (SaVersionT),
	clmVersionsSupported
};

void clmHandleInstanceDestructor (void *instance)
{
	struct clmInstance *clmInstance = (struct clmInstance *)instance;

	if (clmInstance->fd != -1) {
		shutdown (clmInstance->fd, 0);
		close (clmInstance->fd);
	}
}


SaErrorT
saClmInitialize (
	SaClmHandleT *clmHandle,
	const SaClmCallbacksT *clmCallbacks,
	const SaVersionT *version)
{
	struct clmInstance *clmInstance;
	SaErrorT error = SA_OK;

	error = saVersionVerify (&clmVersionDatabase, version);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleCreate (&clmHandleDatabase, sizeof (struct clmInstance),
		clmHandle);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle,
		(void *)&clmInstance);
	if (error != SA_OK) {
		goto error_destroy;
	}

	clmInstance->fd = -1;
	
	error = saServiceConnect (&clmInstance->fd, MESSAGE_REQ_CLM_INIT);
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	memcpy (&clmInstance->callbacks, clmCallbacks, sizeof (SaClmCallbacksT));

	pthread_mutex_init (&clmInstance->mutex, NULL);

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);

	return (SA_OK);

error_put_destroy:
	saHandleInstancePut (&clmHandleDatabase, *clmHandle);
error_destroy:
	saHandleDestroy (&clmHandleDatabase, *clmHandle);
error_no_destroy:
	return (error);
}

SaErrorT
saClmSelectionObjectGet (
	const SaClmHandleT *clmHandle,
	SaSelectionObjectT *selectionObject)
{
	struct clmInstance *clmInstance;
	SaErrorT error;

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle, (void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	*selectionObject = clmInstance->fd;

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);
	return (SA_OK);
}

SaErrorT
saClmDispatch (
	const SaClmHandleT *clmHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int timeout = -1;
	SaErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	int poll_fd;
	struct clmInstance *clmInstance;
	struct res_clm_trackcallback *res_clm_trackcallback;
	struct res_clm_nodegetcallback *res_clm_nodegetcallback;
	SaClmCallbacksT callbacks;
	struct message_overlay dispatch_data;

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle, (void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ONE or SA_DISPATCH_ALL and
	 * wait indefinately for SA_DISPATCH_BLOCKING
	 */
	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		poll_fd = clmInstance->fd;

		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_OK) {
			goto error_nounlock;
		}

		pthread_mutex_lock (&clmInstance->mutex);

		/*
		 * Handle has been finalized in another thread
		 */
		if (clmInstance->finalize == 1) {
			error = SA_OK;
			pthread_mutex_unlock (&clmInstance->mutex);
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock (&clmInstance->mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&clmInstance->mutex);
			continue; /* next poll */
		}

		/*
		 * Read header
		 */
		error = saRecvRetry (clmInstance->fd, &dispatch_data.header,
			sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
		if (error != SA_OK) {
			goto error_unlock;
		}

		/*
		 * Read data payload
		 */
		if (dispatch_data.header.size > sizeof (struct message_header)) {
			error = saRecvRetry (clmInstance->fd, &dispatch_data.data,
				dispatch_data.header.size - sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_unlock;
			}
		}
		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that clmFinalize has been called.
		*/
		memcpy (&callbacks, &clmInstance->callbacks, sizeof (SaClmCallbacksT));

		pthread_mutex_unlock (&clmInstance->mutex);
		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data.header.id) {

		case MESSAGE_RES_CLM_TRACKCALLBACK:
			res_clm_trackcallback = (struct res_clm_trackcallback *)&dispatch_data;

			memcpy (res_clm_trackcallback->notificationBufferAddress,
				&res_clm_trackcallback->notificationBuffer,
				res_clm_trackcallback->numberOfItems * sizeof (SaClmClusterNotificationT));

			callbacks.saClmClusterTrackCallback (
				res_clm_trackcallback->notificationBufferAddress,
				res_clm_trackcallback->numberOfItems, res_clm_trackcallback->numberOfMembers,
				res_clm_trackcallback->viewNumber, SA_OK);
			break;

		case MESSAGE_RES_CLM_NODEGETCALLBACK:
			res_clm_nodegetcallback = (struct res_clm_nodegetcallback *)&dispatch_data;

			memcpy (res_clm_nodegetcallback->clusterNodeAddress,
				&res_clm_nodegetcallback->clusterNode, sizeof (SaClmClusterNodeT));

			callbacks.saClmClusterNodeGetCallback (
				res_clm_nodegetcallback->invocation,
				&res_clm_nodegetcallback->clusterNode, SA_OK);
			break;

		default:
			error = SA_ERR_LIBRARY;
			goto error_nounlock;
			break;
		}

		/*
		 * Determine if more messages should be processed
		 * */
		switch (dispatchFlags) {
		case SA_DISPATCH_ONE:
			cont = 0;
			break;
		case SA_DISPATCH_ALL:
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

error_unlock:
	saHandleInstancePut (&clmHandleDatabase, *clmHandle);
error_nounlock:
	return (error);
}

SaErrorT
saClmFinalize (
	SaClmHandleT *clmHandle)
{
	struct clmInstance *clmInstance;
	SaErrorT error;

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle, (void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

       pthread_mutex_lock (&clmInstance->mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (clmInstance->finalize) {
		pthread_mutex_unlock (&clmInstance->mutex);
		saHandleInstancePut (&clmHandleDatabase, *clmHandle);
		return (SA_ERR_BAD_HANDLE);
	}

	clmInstance->finalize = 1;

	saActivatePoll (clmInstance->fd);

	pthread_mutex_unlock (&clmInstance->mutex);

	saHandleDestroy (&clmHandleDatabase, *clmHandle);

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);

	return (error);
}

SaErrorT
saClmClusterTrackStart (
	const SaClmHandleT *clmHandle,
	SaUint8T trackFlags,
	SaClmClusterNotificationT *notificationBuffer,
	SaUint32T numberOfItems)
{
	struct req_clm_trackstart req_trackstart;
	struct clmInstance *clmInstance;
	SaErrorT error = SA_OK;

	req_trackstart.header.magic = MESSAGE_MAGIC;
	req_trackstart.header.size = sizeof (struct req_clm_trackstart);
	req_trackstart.header.id = MESSAGE_REQ_CLM_TRACKSTART;
	req_trackstart.trackFlags = trackFlags;
	req_trackstart.notificationBufferAddress = notificationBuffer;
	req_trackstart.numberOfItems = numberOfItems;

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle, (void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->mutex);

	error = saSendRetry (clmInstance->fd, &req_trackstart, sizeof (struct req_clm_trackstart), MSG_NOSIGNAL);

	pthread_mutex_unlock (&clmInstance->mutex);

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);

	return (error);
}

SaErrorT
saClmClusterTrackStop (
	const SaClmHandleT *clmHandle)
{
	struct clmInstance *clmInstance;
	struct req_clm_trackstop req_trackstop;
	SaErrorT error = SA_OK;

	req_trackstop.header.magic = MESSAGE_MAGIC;
	req_trackstop.header.size = sizeof (struct req_clm_trackstop);
	req_trackstop.header.id = MESSAGE_REQ_CLM_TRACKSTOP;

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle, (void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->mutex);

	error = saSendRetry (clmInstance->fd, &req_trackstop, sizeof (struct req_clm_trackstop), MSG_NOSIGNAL);

	pthread_mutex_unlock (&clmInstance->mutex);

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);

	return (error);
}

SaErrorT
saClmClusterNodeGet (
	SaClmNodeIdT nodeId,
	SaTimeT timeout,
	SaClmClusterNodeT *clusterNode)
{
	int fd;
	struct req_clm_nodeget req_clm_nodeget;
	struct res_clm_nodeget res_clm_nodeget;
	struct message_overlay message;
	SaErrorT error = SA_OK;
	struct timeval select_timeout;
	fd_set read_fds;

	select_timeout.tv_usec = 0;
	select_timeout.tv_sec = 5;

	error = saServiceConnect (&fd, MESSAGE_REQ_CLM_INIT);
	if (error != SA_OK) {
		goto error_noclose;
	}

	/*
	 * Send request message
	 */
	req_clm_nodeget.header.magic = MESSAGE_MAGIC;
	req_clm_nodeget.header.size = sizeof (struct req_clm_nodeget);
	req_clm_nodeget.header.id = MESSAGE_REQ_CLM_NODEGET;
	req_clm_nodeget.nodeId = nodeId;
	error = saSendRetry (fd, &req_clm_nodeget, sizeof (struct req_clm_nodeget), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_close;
	}

	FD_ZERO (&read_fds);
	FD_SET (fd, &read_fds);
	/*
	 * Wait for timeout interval
	 */
	error = saSelectRetry (fd + 1, &read_fds, 0, 0, &select_timeout);
	if (error != SA_OK) {
		goto error_close;
	}

	/*
	 * Was there a timeout in receiving the information?
	 */
	if (FD_ISSET (fd, &read_fds) == 0) {
		error = SA_ERR_TIMEOUT;
		goto error_close;
	}

	error = saRecvRetry (fd, &message.header, sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_close;
	}

	error = saRecvRetry (fd, &message.data, message.header.size - sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_close;
	}

	memcpy (clusterNode, &res_clm_nodeget.clusterNode, sizeof (SaClmClusterNodeT));

error_close:
	close (fd);
error_noclose:
	return (error);
}

SaErrorT
saClmClusterNodeGetAsync (
	const SaClmHandleT *clmHandle,
	SaInvocationT invocation,
	SaClmNodeIdT nodeId,
	SaClmClusterNodeT *clusterNode)
{
	struct clmInstance *clmInstance;
	struct req_clm_nodeget req_clm_nodeget;
	SaErrorT error = SA_OK;

	req_clm_nodeget.header.magic = MESSAGE_MAGIC;
	req_clm_nodeget.header.size = sizeof (struct req_clm_nodeget);
	req_clm_nodeget.header.id = MESSAGE_REQ_CLM_NODEGET;
	memcpy (&req_clm_nodeget.invocation, &invocation, sizeof (SaInvocationT));
	memcpy (&req_clm_nodeget.nodeId, &nodeId, sizeof (SaClmNodeIdT));
	req_clm_nodeget.clusterNodeAddress = clusterNode;

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle, (void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->mutex);

	error = saSendRetry (clmInstance->fd, &req_clm_nodeget,
		sizeof (struct req_clm_nodeget), MSG_NOSIGNAL);

	pthread_mutex_unlock (&clmInstance->mutex);

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);

	return (error);
}
