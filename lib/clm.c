
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
	struct message_overlay message;
	pthread_mutex_t mutex;
};
#define CLMINSTANCE_MUTEX_OFFSET offset_of(struct clmInstance, mutex)

static struct saHandleDatabase clmHandleDatabase = {
	handleCount: 0,
	handles: 0,
	generation: 0,
	mutex: PTHREAD_MUTEX_INITIALIZER
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
		goto error_nofree;
	}

	error = saHandleCreate (&clmHandleDatabase, (void *)&clmInstance,
		sizeof (struct clmInstance), clmHandle);
	if (error != SA_OK) {
		goto error_nofree;
	}
	
	error = saServiceConnect (&clmInstance->fd, MESSAGE_REQ_CLM_INIT);
	if (error != SA_OK) {
		goto error_free;
	}

	memcpy (&clmInstance->callbacks, clmCallbacks, sizeof (SaClmCallbacksT));

	pthread_mutex_init (&clmInstance->mutex, NULL);

	return (SA_OK);

error_free:
	saHandleRemove (&clmHandleDatabase, *clmHandle);
error_nofree:
	return (error);
}

SaErrorT
saClmSelectionObjectGet (
	const SaClmHandleT *clmHandle,
	SaSelectionObjectT *selectionObject)
{
	struct clmInstance *clmInstance;
	SaErrorT error;

	error = saHandleConvert (&clmHandleDatabase, *clmHandle, (void *)&clmInstance, CLMINSTANCE_MUTEX_OFFSET, 0);
	if (error != SA_OK) {
		return (error);
	}

	*selectionObject = clmInstance->fd;

	pthread_mutex_unlock (&clmInstance->mutex);
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
	int handle_verified = 0;
	struct clmInstance *clmInstance;
	struct res_clm_trackcallback *res_clm_trackcallback;
	struct res_clm_nodegetcallback *res_clm_nodegetcallback;
	SaClmCallbacksT callbacks;
	unsigned int gen_first;
	unsigned int gen_second;
	struct message_overlay dispatch_data;

	/*
	 * Timeout instantly for SA_DISPATCH_ONE or SA_DISPATCH_ALL and
	 * wait indefinately for SA_DISPATCH_BLOCKING
	 */
	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		error = saHandleConvert (&clmHandleDatabase, *clmHandle, (void *)&clmInstance, CLMINSTANCE_MUTEX_OFFSET, &gen_first);
		if (error != SA_OK) {
			return (handle_verified ? SA_OK : error);
		}
		handle_verified = 1;

		poll_fd = clmInstance->fd;

		/*
		 * Unlock mutex for potentially long wait in select.  If fd
		 * is closed by clmFinalize in select, select will return
		 */

		pthread_mutex_unlock (&clmInstance->mutex);

		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_OK) {
			goto error_nounlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			break; /* exit do while cont is 1 loop */
		}
		if (dispatch_avail == 0) {
			continue; /* retry select */
		}
		/*
		 * Re-verify amfHandle
		 */
		error = saHandleConvert (&clmHandleDatabase, *clmHandle, (void *)&clmInstance, CLMINSTANCE_MUTEX_OFFSET, &gen_second);
		if (error != SA_OK) {
			return (handle_verified ? SA_OK : error);
		}

		/*
		 * Handle has been removed and then reallocated
		 */
		if (gen_first != gen_second) {
			return SA_OK;
		}

		/*
		 * Read header
		 */
		error = saRecvRetry (clmInstance->fd, &clmInstance->message.header, sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
		if (error != SA_OK) {
			goto error_unlock;
		}

		/*
		 * Read data payload
		 */
		if (clmInstance->message.header.size > sizeof (struct message_header)) {
			error = saRecvRetry (clmInstance->fd, &clmInstance->message.data,
				clmInstance->message.header.size - sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_unlock;
			}
		}
		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that amfFinalize has been called.
		*/
		memcpy (&callbacks, &clmInstance->callbacks, sizeof (SaClmCallbacksT));
		memcpy (&dispatch_data, &clmInstance->message, sizeof (struct message_overlay));


		pthread_mutex_unlock (&clmInstance->mutex);

		/*
		 * Dispatch incoming message
		 */
		switch (clmInstance->message.header.id) {

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

	return (error);

error_unlock:
	pthread_mutex_unlock (&clmInstance->mutex);
error_nounlock:
	return (error);
}

SaErrorT
saClmFinalize (
	SaClmHandleT *clmHandle)
{
	struct clmInstance *clmInstance;
	SaErrorT error;

	error = saHandleConvert (&clmHandleDatabase, *clmHandle, (void *)&clmInstance, CLMINSTANCE_MUTEX_OFFSET | HANDLECONVERT_DONTUNLOCKDB, 0);
	if (error != SA_OK) {
		return (error);
	}

	shutdown (clmInstance->fd, 0);
	close (clmInstance->fd);
	free (clmInstance);

	error = saHandleRemove (&clmHandleDatabase, *clmHandle);

	pthread_mutex_unlock (&clmInstance->mutex);

	saHandleUnlockDatabase (&clmHandleDatabase);

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

	error = saHandleConvert (&clmHandleDatabase, *clmHandle, (void *)&clmInstance, CLMINSTANCE_MUTEX_OFFSET, 0);
	if (error != SA_OK) {
		return (error);
	}

	error = saSendRetry (clmInstance->fd, &req_trackstart, sizeof (struct req_clm_trackstart), MSG_NOSIGNAL);

	pthread_mutex_unlock (&clmInstance->mutex);

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

	error = saHandleConvert (&clmHandleDatabase, *clmHandle, (void *)&clmInstance, CLMINSTANCE_MUTEX_OFFSET, 0);
	if (error != SA_OK) {
		return (error);
	}

	error = saSendRetry (clmInstance->fd, &req_trackstop, sizeof (struct req_clm_trackstop), MSG_NOSIGNAL);

	pthread_mutex_unlock (&clmInstance->mutex);

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

	error = saHandleConvert (&clmHandleDatabase, *clmHandle, (void *)&clmInstance, CLMINSTANCE_MUTEX_OFFSET, 0);
	if (error != SA_OK) {
		return (error);
	}

	error = saSendRetry (clmInstance->fd, &req_clm_nodeget, sizeof (struct req_clm_nodeget), MSG_NOSIGNAL);

	pthread_mutex_unlock (&clmInstance->mutex);

	return (error);
}
