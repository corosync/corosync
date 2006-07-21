/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
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

#include "../exec/totem.h"
#include "../include/saAis.h"
#include "../include/saClm.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_clm.h"
#include "../include/mar_gen.h"
#include "../include/mar_clm.h"

#include "util.h"

struct res_overlay {
	mar_res_header_t header;
	char data[512000];
};

struct clmInstance {
	int response_fd;
	int dispatch_fd;
	SaClmCallbacksT callbacks;
	int finalize;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

static void clmHandleInstanceDestructor (void *);

static struct saHandleDatabase clmHandleDatabase = {
	.handleCount			= 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
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


/**
 * @defgroup saClm SAF AIS Cluster Membership API
 * @ingroup saf
 *
 * @{
 */
/**
 * This function initializes the Cluster Membership Service for the invoking
 * process and registers the various callback functions.  This function must
 * be invoked prior to the invocation of any other Cluster Membership Service
 * functionality.  The handle clmHandle is returned as the reference to this
 * association between the process and the Cluster Membership Service.  The
 * process uses this handle in subsequent communication with the Cluster
 * Membership Service.
 *
 * @param clmHandle A pointer to the handle designating this particular
 *	initialization of the Cluster Membership Service that is to be
 *	returned by the Cluster Membership Service.
 * @param clmCallbacks If clmCallbacks is set to NULL, no callback is
 *	registered; otherise, it is a pointer to an SaClmCallbacksT structure,
 *	containing the callback functions of the process that the Cluster
 *	Membership Service may invoke.  Only non-NULL callback functions
 *	in this structure will be registered.
 * @param version The version requested from the application is passed into
 *	this parameter and the version supported is returned.
 *
 * @returns SA_AIS_OK if the function completed successfully.
 * @returns SA_AIS_ERR_LIBRARY if an unexpected problem occurred in
 *	the library.
 * @returns SA_AIS_ERR_TRY_AGAIN if the service cannot be provided at this
 *	time.
 * @returns SA_AIS_ERR_INVALID_PARAM if a parameter is not set correctly.
 * @returns SA_AIS_ERR_NO_MEMORY if the Cluster Membership Service is out
 *	of memory and cannot provide the service.
 * @returns SA_AIS_ERR_VERSION if the version parameter is not compatible with
 *	the version of the Cluster Membership Service implementation.
 */
SaAisErrorT
saClmInitialize (
	SaClmHandleT *clmHandle,
	const SaClmCallbacksT *clmCallbacks,
	SaVersionT *version)
{
	struct clmInstance *clmInstance;
	SaAisErrorT error = SA_AIS_OK;


	if (clmHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	if (version == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saVersionVerify (&clmVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleCreate (&clmHandleDatabase, sizeof (struct clmInstance),
		clmHandle);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&clmHandleDatabase, *clmHandle,
		(void *)&clmInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	clmInstance->response_fd = -1;

	clmInstance->dispatch_fd = -1;

	error = saServiceConnect (&clmInstance->response_fd,
		&clmInstance->dispatch_fd, CLM_SERVICE);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (clmCallbacks) {
		memcpy (&clmInstance->callbacks, clmCallbacks, sizeof (SaClmCallbacksT));
	} else {
		memset (&clmInstance->callbacks, 0, sizeof (SaClmCallbacksT));
	}

	pthread_mutex_init (&clmInstance->response_mutex, NULL);

	pthread_mutex_init (&clmInstance->dispatch_mutex, NULL);

	saHandleInstancePut (&clmHandleDatabase, *clmHandle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&clmHandleDatabase, *clmHandle);
error_destroy:
	saHandleDestroy (&clmHandleDatabase, *clmHandle);
error_no_destroy:
	return (error);
}

/**
 * This function returns the operating system handle, selectionObject,
 * assocated with the handle clmHandle.  The invoking process can use this
 * handle to detect pending callbacks, instead of repeatedly invoking
 * saClmDispatch() for this purpose.
 *
 * In a POSIX environment, the operating system handle is a file descriptor
 * that is used with the poll() or select() system calls to detect pending
 * callbacks.
 *
 * The selectionObject returned by saClmSelectionObjectGet() is valid until
 * saClmFinalize() is invoked on the same handle clmHandle.
 *
 * @param clmHandle The handle, obtained through the saClmInitialize function,
 *	designating this particular initialization of the Cluster Membership
 * @param selectionObject A pointer to the operating system handle that the
 *	invoking process can use to detect pending callbacks.
 *
 * @returns SA_AIS_OK if the function completed successfully.
 * @returns SA_AIS_ERR_BAD_HANDLE if the handle clmHandle is invalid, since it is
 *	corrupted, uninitialized, or has already been finalized.
 */
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
	if (error != SA_AIS_OK) {
		return (error);
	}

	*selectionObject = clmInstance->dispatch_fd;

	saHandleInstancePut (&clmHandleDatabase, clmHandle);
	return (SA_AIS_OK);
}

/**
 * This function invokes, in the context of the calling thread, pending callbacks for
 * the handle clmhandle in a way that is specified by the dispatchFlags parameter.
 *
 * @param clmHandle The handle, obtained through the saClmInitialize() function,
 *	designating the particular initialization of the Cluster Membership Service.
 * @param dispatchFlags Flags that specify the callback exection behavior of
 *	saClmDispatch, which have the values SA_DISPATCH_ONE, SA_DISPATCH_ALL, or
 *	SA_DISPATCH_BLOCKING.
 * @returns SA_AIS_OK if the function completed successfully.
 * @returns SA_AIS_ERR_TRY_AGAIN if the service cannot be provided at this time.  The
 *	process may retry later.
 * @returns SA_AIS_ERR_BAD_HANDLE if the handle clmHandle is invalid, since it is
 *	corrupted, uninitialized, or has already been finalized.
 * @returns SA_AIS_ERR_INVALID_PARAM if the dispatchFlags parameter is valid.
 */
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
	struct res_lib_clm_clustertrack *res_lib_clm_clustertrack;
	struct res_clm_nodegetcallback *res_clm_nodegetcallback;
	SaClmCallbacksT callbacks;
	struct res_overlay dispatch_data;
	SaClmClusterNotificationBufferT notificationBuffer;
	SaClmClusterNotificationT notification[PROCESSOR_COUNT_MAX];
	SaClmClusterNodeT clusterNode;
	int items_to_copy;
	unsigned int i;

	if (dispatchFlags != SA_DISPATCH_ONE &&
		dispatchFlags != SA_DISPATCH_ALL &&
		dispatchFlags != SA_DISPATCH_BLOCKING) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_AIS_OK) {
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
		if (error != SA_AIS_OK) {
			goto error_unlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (clmInstance->finalize == 1) {
			error = SA_AIS_OK;
			goto error_unlock;
		}

		if ((ufds.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
			error = SA_AIS_ERR_BAD_HANDLE;
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
				sizeof (mar_res_header_t));
			if (error != SA_AIS_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
				error = saRecvRetry (clmInstance->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (mar_res_header_t));
				if (error != SA_AIS_OK) {
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
		pthread_mutex_unlock (&clmInstance->dispatch_mutex);

		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data.header.id) {

		case MESSAGE_RES_CLM_TRACKCALLBACK:
			if (callbacks.saClmClusterTrackCallback == NULL) {
				continue;
			}
			res_lib_clm_clustertrack = (struct res_lib_clm_clustertrack *)&dispatch_data;
			error = SA_AIS_OK;

			notificationBuffer.notification = notification;

			notificationBuffer.viewNumber = res_lib_clm_clustertrack->view;
			notificationBuffer.notification = notification;
			notificationBuffer.numberOfItems =
				res_lib_clm_clustertrack->number_of_items;

			items_to_copy = notificationBuffer.numberOfItems
				< res_lib_clm_clustertrack->number_of_items ?
				notificationBuffer.numberOfItems :
				res_lib_clm_clustertrack->number_of_items;

			for (i = 0; i < items_to_copy; i++) {
				marshall_from_mar_clm_cluster_notification_t (
					&notificationBuffer.notification[i],
					&res_lib_clm_clustertrack->notification[i]);
			}

			callbacks.saClmClusterTrackCallback (
				(const SaClmClusterNotificationBufferT *)&notificationBuffer,
				res_lib_clm_clustertrack->number_of_items, error);

			break;

		case MESSAGE_RES_CLM_NODEGETCALLBACK:
			if (callbacks.saClmClusterNodeGetCallback == NULL) {
				continue;
			}
			res_clm_nodegetcallback = (struct res_clm_nodegetcallback *)&dispatch_data;
			marshall_from_mar_clm_cluster_node_t (
				&clusterNode,
				&res_clm_nodegetcallback->cluster_node);

			callbacks.saClmClusterNodeGetCallback (
				res_clm_nodegetcallback->invocation,
				&clusterNode,
				res_clm_nodegetcallback->header.error);
			break;

		default:
			error = SA_AIS_ERR_LIBRARY;
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


/**
 * The saClmFinalize function closes the assocation, represented by the clmHandle
 * parameter, between the invoking process and the Cluster Membership Service.  The
 * process must have invoked saClmInitialize before it invokes this function.  A
 * process must invoke this function once for each handle it acquired by invoking
 * saClmInitialize().
 *
 * If the saClmFinalize() function returns successfully, the saClmFinalize() function
 * releases all resources acquired when saClmInitialize(0 was called.  Moreover, it
 * stops any tracking associated with the particular handle.  Furthermore, it cancels
 * all pending callbacks related to the particular handle.  Note that because the
 * callback invocation is asynchronous, it is still possible that some callback calls
 * are processed after this call returns successfully.
 *
 * After saClmFinalize() is invoked, the selection object is no longer valid.
 *
 * @param clmHandle The handle, obtained through the saClmInitialize() function,
 *	designating this particular initialization of the Cluster Membership Service.
 *
 * @returns SA_AIS_OK if the function completed successfully.
 * @returns SA_AIS_ERR_BAD_HANDLE if the handle clmHandle is invalid, since it is
 *	corrupted, uninitialized, or has already been finalized.
 */
SaAisErrorT
saClmFinalize (
	SaClmHandleT clmHandle)
{
	struct clmInstance *clmInstance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

       pthread_mutex_lock (&clmInstance->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (clmInstance->finalize) {
		pthread_mutex_unlock (&clmInstance->response_mutex);
		saHandleInstancePut (&clmHandleDatabase, clmHandle);
		return (SA_AIS_ERR_BAD_HANDLE);
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
	struct req_lib_clm_clustertrack req_lib_clm_clustertrack;
	struct res_lib_clm_clustertrack res_lib_clm_clustertrack;
	struct clmInstance *clmInstance;
	SaAisErrorT error = SA_AIS_OK;
	int items_to_copy;
	unsigned int i;

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((trackFlags & SA_TRACK_CHANGES) && (trackFlags & SA_TRACK_CHANGES_ONLY)) {
		error = SA_AIS_ERR_BAD_FLAGS;
		goto error_nounlock;
	}

	if (trackFlags & ~(SA_TRACK_CURRENT | SA_TRACK_CHANGES | SA_TRACK_CHANGES_ONLY)) {
		error = SA_AIS_ERR_BAD_FLAGS;
		goto error_nounlock;
	}

	if ((notificationBuffer != NULL) &&
		(notificationBuffer->notification != NULL) &&
		(notificationBuffer->numberOfItems == 0)) {

		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_nounlock;
	}
		
	req_lib_clm_clustertrack.header.size = sizeof (struct req_lib_clm_clustertrack);
	req_lib_clm_clustertrack.header.id = MESSAGE_REQ_CLM_TRACKSTART;
	req_lib_clm_clustertrack.track_flags = trackFlags;
	req_lib_clm_clustertrack.return_in_callback = 0;
	if ((trackFlags & SA_TRACK_CURRENT) && (notificationBuffer == NULL)) {
		req_lib_clm_clustertrack.return_in_callback = 1;
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	if ((clmInstance->callbacks.saClmClusterTrackCallback == 0) &&
		((notificationBuffer == NULL) ||
		(trackFlags & (SA_TRACK_CHANGES | SA_TRACK_CHANGES_ONLY)))) {

		error = SA_AIS_ERR_INIT;
		goto error_exit;
	}

	error = saSendReceiveReply (clmInstance->response_fd,
		&req_lib_clm_clustertrack,
		sizeof (struct req_lib_clm_clustertrack),
		&res_lib_clm_clustertrack,
		sizeof (struct res_lib_clm_clustertrack));

	if ((trackFlags & SA_TRACK_CURRENT) && (notificationBuffer != NULL)) {
		if (notificationBuffer->notification == 0) {
			notificationBuffer->viewNumber = res_lib_clm_clustertrack.view;

			notificationBuffer->notification =
				malloc (res_lib_clm_clustertrack.number_of_items *
				sizeof (SaClmClusterNotificationT));

			notificationBuffer->numberOfItems =
				res_lib_clm_clustertrack.number_of_items;
		}

		items_to_copy = notificationBuffer->numberOfItems <
			res_lib_clm_clustertrack.number_of_items ?
			notificationBuffer->numberOfItems :
			res_lib_clm_clustertrack.number_of_items;

		for (i = 0; i < items_to_copy; i++) {
			marshall_from_mar_clm_cluster_notification_t (
				&notificationBuffer->notification[i],
				&res_lib_clm_clustertrack.notification[i]);
		}

		notificationBuffer->viewNumber = res_lib_clm_clustertrack.view;
		notificationBuffer->numberOfItems = items_to_copy;
	}

error_exit:
	pthread_mutex_unlock (&clmInstance->response_mutex);

error_nounlock:
	saHandleInstancePut (&clmHandleDatabase, clmHandle);

        return (error == SA_AIS_OK ? res_lib_clm_clustertrack.header.error : error);
}

SaAisErrorT
saClmClusterTrackStop (
	SaClmHandleT clmHandle)
{
	struct clmInstance *clmInstance;
	struct req_lib_clm_trackstop req_lib_clm_trackstop;
	struct res_lib_clm_trackstop res_lib_clm_trackstop;
	SaAisErrorT error = SA_AIS_OK;

	req_lib_clm_trackstop.header.size = sizeof (struct req_lib_clm_trackstop);
	req_lib_clm_trackstop.header.id = MESSAGE_REQ_CLM_TRACKSTOP;
	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	error = saSendReceiveReply (clmInstance->response_fd,
		&req_lib_clm_trackstop,
		sizeof (struct req_lib_clm_trackstop),
		&res_lib_clm_trackstop,
		sizeof (struct res_lib_clm_trackstop));

	pthread_mutex_unlock (&clmInstance->response_mutex);

	saHandleInstancePut (&clmHandleDatabase, clmHandle);

        return (error == SA_AIS_OK ? res_lib_clm_trackstop.header.error : error);
}

SaAisErrorT
saClmClusterNodeGet (
	SaClmHandleT clmHandle,
	SaClmNodeIdT nodeId,
	SaTimeT timeout,
	SaClmClusterNodeT *clusterNode)
{
	struct clmInstance *clmInstance;
	struct req_lib_clm_nodeget req_lib_clm_nodeget;
	struct res_clm_nodeget res_clm_nodeget;
	SaAisErrorT error = SA_AIS_OK;

	if (clusterNode == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (timeout == 0) {
		return (SA_AIS_ERR_TIMEOUT);
	}

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	/*
	 * Send request message
	 */
	req_lib_clm_nodeget.header.size = sizeof (struct req_lib_clm_nodeget);
	req_lib_clm_nodeget.header.id = MESSAGE_REQ_CLM_NODEGET;
	req_lib_clm_nodeget.node_id = nodeId;

	error = saSendReceiveReply (clmInstance->response_fd, &req_lib_clm_nodeget,
		sizeof (struct req_lib_clm_nodeget), &res_clm_nodeget, sizeof (res_clm_nodeget));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_clm_nodeget.header.error;

	marshall_from_mar_clm_cluster_node_t (clusterNode,
		&res_clm_nodeget.cluster_node);

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
	struct req_lib_clm_nodegetasync req_lib_clm_nodegetasync;
	struct res_clm_nodegetasync res_clm_nodegetasync;
	SaAisErrorT error = SA_AIS_OK;

	req_lib_clm_nodegetasync.header.size = sizeof (struct req_lib_clm_nodegetasync);
	req_lib_clm_nodegetasync.header.id = MESSAGE_REQ_CLM_NODEGETASYNC;
	req_lib_clm_nodegetasync.invocation = invocation;
	req_lib_clm_nodegetasync.node_id = nodeId;

	error = saHandleInstanceGet (&clmHandleDatabase, clmHandle,
		(void *)&clmInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&clmInstance->response_mutex);

	if (clmInstance->callbacks.saClmClusterNodeGetCallback == NULL) {
		error = SA_AIS_ERR_INIT;
		goto error_exit;
	}

	error = saSendReceiveReply (clmInstance->response_fd, &req_lib_clm_nodegetasync,
		sizeof (struct req_lib_clm_nodegetasync),
		&res_clm_nodegetasync, sizeof (struct res_clm_nodegetasync));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_clm_nodegetasync.header.error;

error_exit:
	pthread_mutex_unlock (&clmInstance->response_mutex);

	saHandleInstancePut (&clmHandleDatabase, clmHandle);

	return (error);
}

/** @} */
