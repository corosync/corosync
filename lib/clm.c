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
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "../include/ais_types.h"
#include "../include/saClm.h"
#include "../include/ais_msg.h"
#include "../include/ipc_clm.h"

#include "util.h"

struct res_overlay {
	struct res_header header;
	char data[512000];
};

struct clmInstance {
	int response_fd;
	int dispatch_fd;
	SaClmCallbacksT callbacks;
	int finalize;
	SaClmClusterNotificationBufferT notificationBuffer;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

static void clmHandleInstanceDestructor (void *);

static struct saHandleDatabase clmHandleDatabase = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= clmHandleInstanceDestructor
};

/*
 * Versions supported
 */
static SaVersionT clmVersionsSupported[] = {
	{ 'B', 1, 1 }
};

static struct saVersionDatabase clmVersionDatabase = {
	sizeof (clmVersionsSupported) / sizeof (SaVersionT),
	clmVersionsSupported
};

void clmHandleInstanceDestructor (void *instance)
{
}


SaAisErrorT
saClmInitialize (
	SaClmHandleT *clmHandle,
	const SaClmCallbacksT *clmCallbacks,
	SaVersionT *version)
{
	struct clmInstance *clmInstance;
	SaAisErrorT error = SA_OK;


	if (clmHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	if (version == NULL) {
		return (SA_AIS_ERR_VERSION);
	}

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

	clmInstance->response_fd = -1;

	clmInstance->dispatch_fd = -1;

	error = saServiceConnectTwo (&clmInstance->response_fd,
		&clmInstance->dispatch_fd, CLM_SERVICE);
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	if (clmCallbacks) {
		memcpy (&clmInstance->callbacks, clmCallbacks, sizeof (SaClmCallbacksT));
	} else {
		memset (&clmInstance->callbacks, 0, sizeof (SaClmCallbacksT));
	}

	pthread_mutex_init (&clmInstance->response_mutex, NULL);

	pthread_mutex_init (&clmInstance->dispatch_mutex, NULL);

	clmInstance->notificationBuffer.notification = 0;

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);

	return (SA_OK);

error_put_destroy:
	saHandleInstancePut (&clmHandleDatabase, *clmHandle);
error_destroy:
	saHandleDestroy (&clmHandleDatabase, *clmHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
saClmSelectionObjectGet (
	SaClmHandleT clmHandle,
	SaSelectionObjectT *selectionObject)
{
	struct clmInstance *clmInstance;
	SaAisErrorT error;

	if (selectionObject == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	*selectionObject = clmInstance->dispatch_fd;

	saHandleInstancePut (&clmHandleDatabase, clmHandle);
	return (SA_OK);
}

SaAisErrorT
saClmDispatch (
	SaClmHandleT clmHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int timeout = -1;
	SaAisErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct clmInstance *clmInstance;
	struct res_clm_trackcallback *res_clm_trackcallback;
	struct res_clm_nodegetcallback *res_clm_nodegetcallback;
	SaClmCallbacksT callbacks;
	struct res_overlay dispatch_data;
	SaClmClusterNotificationBufferT notificationBuffer;
	int copy_items;

	if (dispatchFlags != SA_DISPATCH_ONE &&
		dispatchFlags != SA_DISPATCH_ALL &&
		dispatchFlags != SA_DISPATCH_BLOCKING) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
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
		ufds.fd = clmInstance->dispatch_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		pthread_mutex_lock (&clmInstance->dispatch_mutex);

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_OK) {
			goto error_unlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (clmInstance->finalize == 1) {
			error = SA_OK;
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock (&clmInstance->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&clmInstance->dispatch_mutex);
			continue; /* next poll */
		}

		if (ufds.revents & POLLIN) {
			error = saRecvRetry (clmInstance->dispatch_fd, &dispatch_data.header,
				sizeof (struct res_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (struct res_header)) {
				error = saRecvRetry (clmInstance->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (struct res_header),
					MSG_WAITALL | MSG_NOSIGNAL);
				if (error != SA_OK) {
					goto error_unlock;
				}
			}
		} else {
			pthread_mutex_unlock (&clmInstance->dispatch_mutex);
			continue;
		}
			
		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that clmFinalize has been called in another thread.
		 */
		memcpy (&callbacks, &clmInstance->callbacks, sizeof (SaClmCallbacksT));
		memcpy (&notificationBuffer, &clmInstance->notificationBuffer,
			sizeof (SaClmClusterNotificationBufferT));

		pthread_mutex_unlock (&clmInstance->dispatch_mutex);

		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data.header.id) {

		case MESSAGE_RES_CLM_TRACKCALLBACK:
			if (callbacks.saClmClusterTrackCallback == NULL) {
				continue;
			}
			res_clm_trackcallback = (struct res_clm_trackcallback *)&dispatch_data;
			error = SA_AIS_OK;

			/*
			 * If buffer is not specified, allocate one
			 */
			if (notificationBuffer.notification == 0) {
				notificationBuffer.notification = malloc (
					res_clm_trackcallback->numberOfItems *
					sizeof (SaClmClusterNotificationT));
				if (notificationBuffer.notification) {
					notificationBuffer.numberOfItems =
						res_clm_trackcallback->numberOfItems;
				} else {
					error = SA_AIS_ERR_NO_MEMORY;
				}
			}

			copy_items = res_clm_trackcallback->numberOfItems;
			if (copy_items > notificationBuffer.numberOfItems) {
				copy_items = notificationBuffer.numberOfItems;
				error = SA_AIS_ERR_NO_SPACE;
			}

			memcpy (notificationBuffer.notification, 
				&res_clm_trackcallback->notification,
				copy_items *
					sizeof (SaClmClusterNotificationT));

			callbacks.saClmClusterTrackCallback (
				(const SaClmClusterNotificationBufferT *)&notificationBuffer,
				res_clm_trackcallback->numberOfMembers, error);
			break;

		case MESSAGE_RES_CLM_NODEGETCALLBACK:
			if (callbacks.saClmClusterNodeGetCallback == NULL) {
				continue;
			}
			res_clm_nodegetcallback = (struct res_clm_nodegetcallback *)&dispatch_data;

			callbacks.saClmClusterNodeGetCallback (
				res_clm_nodegetcallback->invocation,
				&res_clm_nodegetcallback->clusterNode,
				res_clm_nodegetcallback->header.error);
			break;

		default:
			error = SA_ERR_LIBRARY;
			goto error_put;
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

	goto error_put;

error_unlock:
	pthread_mutex_unlock (&clmInstance->dispatch_mutex);

error_put:
	saHandleInstancePut (&clmHandleDatabase, clmHandle);
	return (error);
}

SaAisErrorT
saClmFinalize (
	SaClmHandleT clmHandle)
{
	struct clmInstance *clmInstance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

       pthread_mutex_lock (&clmInstance->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (clmInstance->finalize) {
		pthread_mutex_unlock (&clmInstance->response_mutex);
		saHandleInstancePut (&clmHandleDatabase, clmHandle);
		return (SA_ERR_BAD_HANDLE);
	}

	clmInstance->finalize = 1;

	pthread_mutex_unlock (&clmInstance->response_mutex);

	saHandleDestroy (&clmHandleDatabase, clmHandle);

	if (clmInstance->response_fd != -1) {
		shutdown (clmInstance->response_fd, 0);
		close (clmInstance->response_fd);
	}
	if (clmInstance->dispatch_fd != -1) {
		shutdown (clmInstance->dispatch_fd, 0);
		close (clmInstance->dispatch_fd);
	}

	saHandleInstancePut (&clmHandleDatabase, clmHandle);

	return (error);
}

SaAisErrorT
saClmClusterTrack (
	SaClmHandleT clmHandle,
	SaUint8T trackFlags,
	SaClmClusterNotificationBufferT *notificationBuffer)
{
	struct req_clm_clustertrack req_clustertrack;
	struct clmInstance *clmInstance;
	SaAisErrorT error = SA_OK;

	/*
	 * Parameter checking
	 */
	if (notificationBuffer == 0) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (notificationBuffer->notification &&
		notificationBuffer->numberOfItems == 0) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}
	if ((trackFlags & SA_TRACK_CHANGES) && (trackFlags & SA_TRACK_CHANGES_ONLY)) {
		return (SA_AIS_ERR_BAD_FLAGS);
	}
		
	/*
	 * Request service
	 */
	req_clustertrack.header.size = sizeof (struct req_clm_clustertrack);
	req_clustertrack.header.id = MESSAGE_REQ_CLM_TRACKSTART;
	req_clustertrack.trackFlags = trackFlags;

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	if (clmInstance->callbacks.saClmClusterTrackCallback == 0) {
		error = SA_AIS_ERR_INIT;
		goto error_exit;
	}

	error = saSendRetry (clmInstance->response_fd, &req_clustertrack,
		sizeof (struct req_clm_clustertrack), MSG_NOSIGNAL);

	memcpy (&clmInstance->notificationBuffer, notificationBuffer,
		sizeof (SaClmClusterNotificationBufferT));

// TODO get response packet with saRecvRetry, but need to implement that 
// in executive service
error_exit:
	pthread_mutex_unlock (&clmInstance->response_mutex);

	saHandleInstancePut (&clmHandleDatabase, clmHandle);

	return (error);
}

SaAisErrorT
saClmClusterTrackStop (
	SaClmHandleT clmHandle)
{
	struct clmInstance *clmInstance;
	struct req_clm_trackstop req_trackstop;
	SaAisErrorT error = SA_OK;

	req_trackstop.header.size = sizeof (struct req_clm_trackstop);
	req_trackstop.header.id = MESSAGE_REQ_CLM_TRACKSTOP;

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	error = saSendRetry (clmInstance->response_fd, &req_trackstop,
		sizeof (struct req_clm_trackstop), MSG_NOSIGNAL);

	clmInstance->notificationBuffer.notification = 0;

	pthread_mutex_unlock (&clmInstance->response_mutex);
	// TODO what about getting response from executive?  The
	// executive should send a response

	saHandleInstancePut (&clmHandleDatabase, clmHandle);

	return (error);
}

SaAisErrorT
saClmClusterNodeGet (
	SaClmHandleT clmHandle,
	SaClmNodeIdT nodeId,
	SaTimeT timeout,
	SaClmClusterNodeT *clusterNode)
{
	struct clmInstance *clmInstance;
	struct req_clm_nodeget req_clm_nodeget;
	struct res_clm_nodeget res_clm_nodeget;
	SaAisErrorT error = SA_OK;

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	/*
	 * Send request message
	 */
	req_clm_nodeget.header.size = sizeof (struct req_clm_nodeget);
	req_clm_nodeget.header.id = MESSAGE_REQ_CLM_NODEGET;
	req_clm_nodeget.nodeId = nodeId;

	error = saSendReceiveReply (clmInstance->response_fd, &req_clm_nodeget,
		sizeof (struct req_clm_nodeget), &res_clm_nodeget, sizeof (res_clm_nodeget));
	if (error != SA_OK) {
		goto error_exit;
	}

	error = res_clm_nodeget.header.error;

	memcpy (clusterNode, &res_clm_nodeget.clusterNode,
		sizeof (SaClmClusterNodeT));

error_exit:
	pthread_mutex_unlock (&clmInstance->response_mutex);

	saHandleInstancePut (&clmHandleDatabase, clmHandle);

	return (error);
}

SaAisErrorT
saClmClusterNodeGetAsync (
	SaClmHandleT clmHandle,
	SaInvocationT invocation,
	SaClmNodeIdT nodeId)
{
	struct clmInstance *clmInstance;
	struct req_clm_nodegetasync req_clm_nodegetasync;
	struct res_clm_nodegetasync res_clm_nodegetasync;
	SaAisErrorT error = SA_OK;

	req_clm_nodegetasync.header.size = sizeof (struct req_clm_nodegetasync);
	req_clm_nodegetasync.header.id = MESSAGE_REQ_CLM_NODEGETASYNC;
	memcpy (&req_clm_nodegetasync.invocation, &invocation,
		sizeof (SaInvocationT));
	memcpy (&req_clm_nodegetasync.nodeId, &nodeId, sizeof (SaClmNodeIdT));

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	error = saSendReceiveReply (clmInstance->response_fd, &req_clm_nodegetasync,
		sizeof (struct req_clm_nodegetasync),
		&res_clm_nodegetasync, sizeof (struct res_clm_nodegetasync));
	if (error != SA_OK) {
		goto error_exit;
	}

	error = res_clm_nodegetasync.header.error;

error_exit:
	pthread_mutex_unlock (&clmInstance->response_mutex);

	saHandleInstancePut (&clmHandleDatabase, clmHandle);

	return (error);
}
