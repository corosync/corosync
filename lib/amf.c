
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
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "../include/ais_types.h"
#include "../include/ais_amf.h"
#include "../include/ais_msg.h"
#include "util.h"

struct res_overlay {
	struct res_header header;
	char data[4096];
};

/*
 * Data structure for instance data
 */
struct amfInstance {
	int fd;
	SaAmfCallbacksT callbacks;
	struct queue inq;
	SaNameT compName;
	int compRegistered;
	int finalize;
	pthread_mutex_t mutex;
};

static void amfHandleInstanceDestructor (void *);

/*
 * All instances in one database
 */
static struct saHandleDatabase amfHandleDatabase = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= amfHandleInstanceDestructor
};

/*
 * Versions supported
 */
static SaVersionT amfVersionsSupported[] = {
	{ 'A', 1, 1 },
	{ 'a', 1, 1 }
};

static struct saVersionDatabase amfVersionDatabase = {
	sizeof (amfVersionsSupported) / sizeof (SaVersionT),
	amfVersionsSupported
};
	
/*
 * Implementation
 */

void amfHandleInstanceDestructor (void *instance)
{
	struct amfInstance *amfInstance = (struct amfInstance *)instance;

	if (amfInstance->fd != -1) {
		shutdown (amfInstance->fd, 0);
		close (amfInstance->fd);
	}

	if (amfInstance->inq.items) {
		free (amfInstance->inq.items);
	}
}

SaErrorT
saAmfInitialize (
	SaAmfHandleT *amfHandle,
	const SaAmfCallbacksT *amfCallbacks,
	const SaVersionT *version)
{
	struct amfInstance *amfInstance;
	SaErrorT error = SA_OK;

	error = saVersionVerify (&amfVersionDatabase, version);
	if (error != SA_OK) {
		goto error_no_destroy;
	}
	
	error = saHandleCreate (&amfHandleDatabase, sizeof (struct amfInstance), amfHandle);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		goto error_destroy;
	}

	amfInstance->fd = -1;
	
	/*
	 * An inq is needed to store async messages while waiting for a 
	 * sync response
	 */
	error = saQueueInit (&amfInstance->inq, 512, sizeof (void *));
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	error = saServiceConnect (&amfInstance->fd, MESSAGE_REQ_AMF_INIT);
	if (error != SA_OK) {
		goto error_put_destroy;
// this is wrong
	}

	memcpy (&amfInstance->callbacks, amfCallbacks, sizeof (SaAmfCallbacksT));

	pthread_mutex_init (&amfInstance->mutex, NULL);

	saHandleInstancePut (&amfHandleDatabase, *amfHandle);

	return (SA_OK);

error_put_destroy:
	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
error_destroy:
	saHandleDestroy (&amfHandleDatabase, *amfHandle);
error_no_destroy:
	return (error);
}

SaErrorT
saAmfSelectionObjectGet (
	const SaAmfHandleT *amfHandle,
	SaSelectionObjectT *selectionObject)
{
	struct amfInstance *amfInstance;
	SaErrorT error;

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	*selectionObject = amfInstance->fd;

	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
	return (SA_OK);
}

SaErrorT
saAmfDispatch (
	const SaAmfHandleT *amfHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int timeout = -1;
	SaErrorT error;
	int dispatch_avail;
	struct amfInstance *amfInstance;
	SaAmfCallbacksT callbacks;
	struct res_lib_amf_healthcheckcallback *res_lib_amf_healthcheckcallback;
	struct res_lib_amf_readinessstatesetcallback *res_lib_amf_readinessstatesetcallback;
	struct res_lib_amf_csisetcallback *res_lib_amf_csisetcallback;
	struct res_lib_amf_csiremovecallback *res_lib_amf_csiremovecallback;
	struct res_lib_amf_protectiongrouptrackcallback *res_lib_amf_protectiongrouptrackcallback;
	struct req_header **queue_msg;
	struct req_header *msg;
	int empty;
	int ignore_dispatch = 0;
	int cont = 1; /* always continue do loop except when set to 0 */
	int poll_fd;
	struct res_overlay dispatch_data;

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle,
		(void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ALL
	 */
	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		poll_fd = amfInstance->fd;

		/*
		 * Read data directly from socket
		 */
		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_OK) {
			goto error_nounlock;
		}

		pthread_mutex_lock (&amfInstance->mutex);

		/*
		 * Handle has been finalized in another thread
		 */
		if (amfInstance->finalize == 1) {
			error = SA_OK;
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock (&amfInstance->mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&amfInstance->mutex);
			continue; /* next poll */
		}

		saQueueIsEmpty(&amfInstance->inq, &empty);
		if (empty == 0) {
			/*
			 * Queue is not empty, read data from queue
			 */
			saQueueItemGet (&amfInstance->inq, (void *)&queue_msg);
			msg = *queue_msg;
			memcpy (&dispatch_data, msg, msg->size);
			saQueueItemRemove (&amfInstance->inq);
			free (msg);
		} else {
			/*
			 * Queue empty, read response from socket
			 */
			error = saRecvRetry (amfInstance->fd, &dispatch_data.header,
				sizeof (struct res_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (struct res_header)) {
				error = saRecvRetry (amfInstance->fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (struct res_header),
					MSG_WAITALL | MSG_NOSIGNAL);
				if (error != SA_OK) {
					goto error_unlock;
				}
			}
		}
		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that amfFinalize has been called in another thread.
		 */
		memcpy (&callbacks, &amfInstance->callbacks, sizeof (SaAmfCallbacksT));
		pthread_mutex_unlock (&amfInstance->mutex);


		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data.header.id) {
		case MESSAGE_RES_LIB_ACTIVATEPOLL:
			/*
			 * This is a do nothing message which the node executive sends
			 * to activate the file amfHandle in poll when the library has
			 * queued a message into amfHandle->inq
			 * The dispatch is ignored for the following two cases:
			 * 1) setting of timeout to zero for the DISPATCH_ALL case
			 * 2) expiration of the do loop for the DISPATCH_ONE case
			 */
			ignore_dispatch = 1;
			break;

		case MESSAGE_RES_AMF_HEALTHCHECKCALLBACK:
			res_lib_amf_healthcheckcallback = (struct res_lib_amf_healthcheckcallback *)&dispatch_data;

			callbacks.saAmfHealthcheckCallback (
				res_lib_amf_healthcheckcallback->invocation,
				&res_lib_amf_healthcheckcallback->compName,
				res_lib_amf_healthcheckcallback->checkType);
			break;

		case MESSAGE_RES_AMF_READINESSSTATESETCALLBACK:
			res_lib_amf_readinessstatesetcallback = (struct res_lib_amf_readinessstatesetcallback *)&dispatch_data;
			callbacks.saAmfReadinessStateSetCallback (
				res_lib_amf_readinessstatesetcallback->invocation,
				&res_lib_amf_readinessstatesetcallback->compName,
				res_lib_amf_readinessstatesetcallback->readinessState);
			break;

		case MESSAGE_RES_AMF_CSISETCALLBACK:
			res_lib_amf_csisetcallback = (struct res_lib_amf_csisetcallback *)&dispatch_data;
			callbacks.saAmfCSISetCallback (
				res_lib_amf_csisetcallback->invocation,
				&res_lib_amf_csisetcallback->compName,
				&res_lib_amf_csisetcallback->csiName,
				res_lib_amf_csisetcallback->csiFlags,
				&res_lib_amf_csisetcallback->haState,
				&res_lib_amf_csisetcallback->activeCompName,
				res_lib_amf_csisetcallback->transitionDescriptor);
			break;

		case MESSAGE_RES_AMF_CSIREMOVECALLBACK:
			res_lib_amf_csiremovecallback = (struct res_lib_amf_csiremovecallback *)&dispatch_data;
			callbacks.saAmfCSIRemoveCallback (
				res_lib_amf_csiremovecallback->invocation,
				&res_lib_amf_csiremovecallback->compName,
				&res_lib_amf_csiremovecallback->csiName,
				&res_lib_amf_csiremovecallback->csiFlags);
			break;

		case MESSAGE_RES_AMF_PROTECTIONGROUPTRACKCALLBACK:
			res_lib_amf_protectiongrouptrackcallback = (struct res_lib_amf_protectiongrouptrackcallback *)&dispatch_data;
			memcpy (res_lib_amf_protectiongrouptrackcallback->notificationBufferAddress,
				res_lib_amf_protectiongrouptrackcallback->notificationBuffer,
				res_lib_amf_protectiongrouptrackcallback->numberOfItems * sizeof (SaAmfProtectionGroupNotificationT));
			callbacks.saAmfProtectionGroupTrackCallback(
				&res_lib_amf_protectiongrouptrackcallback->csiName,
				res_lib_amf_protectiongrouptrackcallback->notificationBufferAddress,
				res_lib_amf_protectiongrouptrackcallback->numberOfItems,
				res_lib_amf_protectiongrouptrackcallback->numberOfMembers,
				res_lib_amf_protectiongrouptrackcallback->error);
			break;

		default:
			error = SA_ERR_LIBRARY;	
			goto error_nounlock;
			break;
		}

		/*
		 * Determine if more messages should be processed
		 */
		switch (dispatchFlags) {
		case SA_DISPATCH_ONE:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			} else {
				cont = 0;
			}
			break;
		case SA_DISPATCH_ALL:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			}
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

error_unlock:
	pthread_mutex_unlock (&amfInstance->mutex);
error_nounlock:
	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
	return (error);
}

SaErrorT
saAmfFinalize (
	const SaAmfHandleT *amfHandle)
{
	struct amfInstance *amfInstance;
	SaErrorT error;

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&amfInstance->mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (amfInstance->finalize) {
		pthread_mutex_unlock (&amfInstance->mutex);
		saHandleInstancePut (&amfHandleDatabase, *amfHandle);
		return (SA_ERR_BAD_HANDLE);
	}

	amfInstance->finalize = 1;

	saActivatePoll (amfInstance->fd);

	pthread_mutex_unlock (&amfInstance->mutex);

	saHandleDestroy (&amfHandleDatabase, *amfHandle);

	saHandleInstancePut (&amfHandleDatabase, *amfHandle);

	return (error);
}

SaErrorT
saAmfComponentRegister (
	const SaAmfHandleT *amfHandle,
	const SaNameT *compName,
	const SaNameT *proxyCompName)
{
	struct amfInstance *amfInstance;
	SaErrorT error;
	struct req_lib_amf_componentregister req_lib_amf_componentregister;
	struct res_lib_amf_componentregister res_lib_amf_componentregister;

	req_lib_amf_componentregister.header.size = sizeof (struct req_lib_amf_componentregister);
	req_lib_amf_componentregister.header.id = MESSAGE_REQ_AMF_COMPONENTREGISTER;
	memcpy (&req_lib_amf_componentregister.compName, compName, sizeof (SaNameT));
	if (proxyCompName) {
		memcpy (&req_lib_amf_componentregister.proxyCompName, proxyCompName, sizeof (SaNameT));
	} else {
		memset (&req_lib_amf_componentregister.proxyCompName, 0, sizeof (SaNameT));
	}

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&amfInstance->mutex);

	error = saSendRetry (amfInstance->fd, &req_lib_amf_componentregister, sizeof (struct req_lib_amf_componentregister), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_unlock;
	}

	/*
	 * Search for COMPONENTREGISTER responses and queue any
	 * messages that dont match in this amfHandle's inq.
	 * This must be done to avoid dropping async messages
	 * during this sync message retrieval
	 */
	error = saRecvQueue (amfInstance->fd, &res_lib_amf_componentregister,
		&amfInstance->inq, MESSAGE_RES_AMF_COMPONENTREGISTER);
	if (error != SA_OK) {
		goto error_unlock;
	}

	if (res_lib_amf_componentregister.header.error == SA_OK) {
		amfInstance->compRegistered = 1;
		memcpy (&amfInstance->compName, compName, sizeof (SaNameT));
	}

	error = res_lib_amf_componentregister.header.error;

error_unlock:
	pthread_mutex_unlock (&amfInstance->mutex);
	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
	return (error);
}

SaErrorT
saAmfComponentUnregister (
	const SaAmfHandleT *amfHandle,
	const SaNameT *compName,
	const SaNameT *proxyCompName)
{
	struct req_lib_amf_componentunregister req_lib_amf_componentunregister;
	struct res_lib_amf_componentunregister res_lib_amf_componentunregister;
	struct amfInstance *amfInstance;
	SaErrorT error;

	req_lib_amf_componentunregister.header.size = sizeof (struct req_lib_amf_componentunregister);
	req_lib_amf_componentunregister.header.id = MESSAGE_REQ_AMF_COMPONENTUNREGISTER;
	memcpy (&req_lib_amf_componentunregister.compName, compName, sizeof (SaNameT));
	if (proxyCompName) {
		memcpy (&req_lib_amf_componentunregister.proxyCompName, proxyCompName, sizeof (SaNameT));
	} else {
		memset (&req_lib_amf_componentunregister.proxyCompName, 0, sizeof (SaNameT));
	}	

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&amfInstance->mutex);

	error = saSendRetry (amfInstance->fd, &req_lib_amf_componentunregister,
		sizeof (struct req_lib_amf_componentunregister), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_unlock;
	}

	/*
	 * Search for COMPONENTUNREGISTER responses and queue any
	 * messages that dont match in this amfHandle's inq.
	 * This must be done to avoid dropping async messages
	 * during this sync message retrieval
	 */
	error = saRecvQueue (amfInstance->fd, &res_lib_amf_componentunregister,
		&amfInstance->inq, MESSAGE_RES_AMF_COMPONENTUNREGISTER);
	if (error != SA_OK) {
		goto error_unlock;
	}

	if (res_lib_amf_componentunregister.header.error == SA_OK) {
		amfInstance->compRegistered = 0;
	}
	error = res_lib_amf_componentunregister.header.error;

error_unlock:
	pthread_mutex_unlock (&amfInstance->mutex);
	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
	return (error);
}

SaErrorT
saAmfCompNameGet (
	const SaAmfHandleT *amfHandle,
	SaNameT *compName)
{
	struct amfInstance *amfInstance;
	SaErrorT error;

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&amfInstance->mutex);

	if (amfInstance->compRegistered == 0) {
		pthread_mutex_unlock (&amfInstance->mutex);
		return (SA_ERR_NOT_EXIST);
	}

	memcpy (compName, &amfInstance->compName, sizeof (SaNameT));

	pthread_mutex_unlock (&amfInstance->mutex);

	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
	return (SA_OK);
}

SaErrorT
saAmfReadinessStateGet (
	const SaNameT *compName,
	SaAmfReadinessStateT *readinessState)
{
	int fd;
	SaErrorT error;
	struct req_amf_readinessstateget req_amf_readinessstateget;
	struct res_lib_amf_readinessstateget res_lib_amf_readinessstateget;

	error = saServiceConnect (&fd, MESSAGE_REQ_AMF_INIT);
	if (error != SA_OK) {
		goto exit_noclose;
	}
	req_amf_readinessstateget.header.id = MESSAGE_RES_AMF_READINESSSTATEGET;
	req_amf_readinessstateget.header.size = sizeof (struct req_amf_readinessstateget);
	memcpy (&req_amf_readinessstateget.compName, compName, sizeof (SaNameT));

	error = saSendRetry (fd, &req_amf_readinessstateget,
		sizeof (struct req_amf_readinessstateget), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto exit_close;
	}

	error = saRecvRetry (fd, &res_lib_amf_readinessstateget,
		sizeof (struct res_lib_amf_readinessstateget), MSG_WAITALL | MSG_NOSIGNAL);
	if (error == SA_OK) {
		memcpy (readinessState, &res_lib_amf_readinessstateget.readinessState, 
			sizeof (SaAmfReadinessStateT));
		error = res_lib_amf_readinessstateget.header.error;
	}
		
exit_close:
	close (fd);
exit_noclose:
	return (error);
}

SaErrorT
saAmfStoppingComplete (
	SaInvocationT invocation,
	SaErrorT error)
{
	struct req_amf_stoppingcomplete req_amf_stoppingcomplete;
	int fd;
	SaErrorT errorResult;

	errorResult = saServiceConnect (&fd, MESSAGE_REQ_AMF_INIT);
	if (errorResult != SA_OK) {
		goto exit_noclose;
	}
	req_amf_stoppingcomplete.header.id = MESSAGE_REQ_AMF_STOPPINGCOMPLETE;
	req_amf_stoppingcomplete.header.size = sizeof (struct req_amf_stoppingcomplete);
	req_amf_stoppingcomplete.invocation = invocation;
	req_amf_stoppingcomplete.error = error;
	errorResult = saSendRetry (fd, &req_amf_stoppingcomplete,
		sizeof (struct req_amf_stoppingcomplete), MSG_NOSIGNAL);

	close (fd);

exit_noclose:
	return (errorResult);
}

SaErrorT
saAmfHAStateGet (
	const SaNameT *compName,
	const SaNameT *csiName,
	SaAmfHAStateT *haState) {

	struct req_amf_hastateget req_amf_hastateget;
	struct res_lib_amf_hastateget res_lib_amf_hastateget;
	int fd;
	SaErrorT error;

	error = saServiceConnect (&fd, MESSAGE_REQ_AMF_INIT);
	if (error != SA_OK) {
		goto exit_noclose;
	}
	req_amf_hastateget.header.id = MESSAGE_REQ_AMF_HASTATEGET;
	req_amf_hastateget.header.size = sizeof (struct req_amf_hastateget);
	memcpy (&req_amf_hastateget.compName, compName, sizeof (SaNameT));
	memcpy (&req_amf_hastateget.csiName, csiName, sizeof (SaNameT));

	error = saSendRetry (fd, &req_amf_hastateget,
			sizeof (struct req_amf_hastateget), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto exit_close;
	}

	error = saRecvRetry (fd, &res_lib_amf_hastateget,
		sizeof (struct res_lib_amf_hastateget), MSG_WAITALL | MSG_NOSIGNAL);
	if (error == SA_OK) {
		memcpy (haState, &res_lib_amf_hastateget.haState, sizeof (SaAmfHAStateT));
		error = res_lib_amf_hastateget.header.error;
	}
	
exit_close:
	close (fd);
exit_noclose:
	return (error);
}

SaErrorT
saAmfProtectionGroupTrackStart (
	const SaAmfHandleT *amfHandle,
	const SaNameT *csiName,
	SaUint8T trackFlags,
	const SaAmfProtectionGroupNotificationT *notificationBuffer,
	SaUint32T numberOfItems) {

	struct amfInstance *amfInstance;
	struct req_amf_protectiongrouptrackstart req_amf_protectiongrouptrackstart;
	struct res_lib_amf_protectiongrouptrackstart res_lib_amf_protectiongrouptrackstart;
	SaErrorT error;

	req_amf_protectiongrouptrackstart.header.size = sizeof (struct req_amf_protectiongrouptrackstart);
	req_amf_protectiongrouptrackstart.header.id = MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTART;
	memcpy (&req_amf_protectiongrouptrackstart.csiName, csiName, sizeof (SaNameT));
	req_amf_protectiongrouptrackstart.trackFlags = trackFlags;
	req_amf_protectiongrouptrackstart.notificationBufferAddress = (SaAmfProtectionGroupNotificationT *)notificationBuffer;
	req_amf_protectiongrouptrackstart.numberOfItems = numberOfItems;

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&amfInstance->mutex);

	error = saSendRetry (amfInstance->fd, &req_amf_protectiongrouptrackstart,
		sizeof (struct req_amf_protectiongrouptrackstart), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_unlock;
	}

	error = saRecvQueue (amfInstance->fd, &res_lib_amf_protectiongrouptrackstart,
		&amfInstance->inq, MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTART);

	error = res_lib_amf_protectiongrouptrackstart.header.error;

error_unlock:
	pthread_mutex_unlock (&amfInstance->mutex);
	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
	return (error);
}

SaErrorT
saAmfProtectionGroupTrackStop (
	const SaAmfHandleT *amfHandle,
	const SaNameT *csiName) {

	struct amfInstance *amfInstance;
	struct req_amf_protectiongrouptrackstop req_amf_protectiongrouptrackstop;
	struct res_lib_amf_protectiongrouptrackstop res_lib_amf_protectiongrouptrackstop;
	SaErrorT error;

	req_amf_protectiongrouptrackstop.header.size = sizeof (struct req_amf_protectiongrouptrackstop);
	req_amf_protectiongrouptrackstop.header.id = MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTOP;
	memcpy (&req_amf_protectiongrouptrackstop.csiName, csiName, sizeof (SaNameT));

	error = saHandleInstanceGet (&amfHandleDatabase, *amfHandle, (void *)&amfInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&amfInstance->mutex);

	error = saSendRetry (amfInstance->fd, &req_amf_protectiongrouptrackstop,
		sizeof (struct req_amf_protectiongrouptrackstop), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_unlock;
	}

	error = saRecvQueue (amfInstance->fd, &res_lib_amf_protectiongrouptrackstop,
		&amfInstance->inq, MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTOP);

	error = res_lib_amf_protectiongrouptrackstop.header.error;

error_unlock:
	pthread_mutex_unlock (&amfInstance->mutex);
	saHandleInstancePut (&amfHandleDatabase, *amfHandle);
	return (error);
}

SaErrorT
saAmfErrorReport (
	const SaNameT *reportingComponent,
	const SaNameT *erroneousComponent,
	SaTimeT errorDetectionTime,
	const SaAmfErrorDescriptorT *errorDescriptor,
	const SaAmfAdditionalDataT *additionalData) {

	struct req_lib_amf_errorreport req_lib_amf_errorreport;
	struct res_lib_amf_errorreport res_lib_amf_errorreport;
	int fd;
	SaErrorT error;

	error = saServiceConnect (&fd, MESSAGE_REQ_AMF_INIT);
	if (error != SA_OK) {
		goto exit_noclose;
	}
	req_lib_amf_errorreport.header.id = MESSAGE_REQ_AMF_ERRORREPORT;
	req_lib_amf_errorreport.header.size = sizeof (struct req_lib_amf_errorreport);
	memcpy (&req_lib_amf_errorreport.reportingComponent, reportingComponent, sizeof (SaNameT));
	memcpy (&req_lib_amf_errorreport.erroneousComponent, erroneousComponent, sizeof (SaNameT));
	req_lib_amf_errorreport.errorDetectionTime = errorDetectionTime;
	memcpy (&req_lib_amf_errorreport.errorDescriptor,
		errorDescriptor, sizeof (SaAmfErrorDescriptorT));
	/* TODO this is wrong, and needs some thinking
	memcpy (&req_lib_amf_errorreport.additionalData,
		additionalData, sizeof (SaAmfAdditionalDataT));
	*/

	error = saSendRetry (fd, &req_lib_amf_errorreport,
		sizeof (struct req_lib_amf_errorreport), MSG_NOSIGNAL);

	/*
	 * Get response from executive and respond to user application
	 */
	error = saRecvRetry (fd, &res_lib_amf_errorreport,
		sizeof (struct res_lib_amf_errorreport), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto exit_close;
	}

	error = res_lib_amf_errorreport.header.error;

exit_close:
	close (fd);
exit_noclose:
	return (error);
}

SaErrorT
saAmfErrorCancelAll (
	const SaNameT *compName) {

	struct req_lib_amf_errorcancelall req_lib_amf_errorcancelall;
	struct res_lib_amf_errorcancelall res_lib_amf_errorcancelall;
	int fd;
	SaErrorT error;

	error = saServiceConnect (&fd, MESSAGE_REQ_AMF_INIT);
	if (error != SA_OK) {
		goto exit_noclose;
	}
	req_lib_amf_errorcancelall.header.id = MESSAGE_REQ_AMF_ERRORCANCELALL;
	req_lib_amf_errorcancelall.header.size = sizeof (struct req_lib_amf_errorcancelall);
	memcpy (&req_lib_amf_errorcancelall.compName, compName, sizeof (SaNameT));

	error = saSendRetry (fd, &req_lib_amf_errorcancelall,
		sizeof (struct req_lib_amf_errorcancelall), MSG_NOSIGNAL);

	/*
	 * Get response from executive and respond to user application
	 */
	error = saRecvRetry (fd, &res_lib_amf_errorcancelall,
		sizeof (struct res_lib_amf_errorcancelall), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto exit_close;
	}

	error = res_lib_amf_errorcancelall.header.error;

exit_close:
	close (fd);
exit_noclose:
	return (error);
}

SaErrorT
saAmfComponentCapabilityModelGet (
	const SaNameT *compName,
	SaAmfComponentCapabilityModelT *componentCapabilityModel)
{

	int fd;
	SaErrorT error;
	struct req_amf_componentcapabilitymodelget req_amf_componentcapabilitymodelget;
	struct res_lib_amf_componentcapabilitymodelget res_lib_amf_componentcapabilitymodelget;

	error = saServiceConnect (&fd, MESSAGE_REQ_AMF_INIT);
	if (error != SA_OK) {
		goto exit_noclose;
	}
	req_amf_componentcapabilitymodelget.header.id = MESSAGE_REQ_AMF_COMPONENTCAPABILITYMODELGET;
	req_amf_componentcapabilitymodelget.header.size = sizeof (struct req_amf_componentcapabilitymodelget);
	memcpy (&req_amf_componentcapabilitymodelget.compName, compName, sizeof (SaNameT));

	error = saSendRetry (fd, &req_amf_componentcapabilitymodelget,
		sizeof (struct req_amf_componentcapabilitymodelget), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto exit_close;
	}

	error = saRecvRetry (fd, &res_lib_amf_componentcapabilitymodelget,
		sizeof (struct res_lib_amf_componentcapabilitymodelget), MSG_WAITALL | MSG_NOSIGNAL);
	if (error == SA_OK) {
		memcpy (componentCapabilityModel,
			&res_lib_amf_componentcapabilitymodelget.componentCapabilityModel, 
			sizeof (SaAmfComponentCapabilityModelT));
		error = res_lib_amf_componentcapabilitymodelget.header.error;
	}
		
exit_close:
	close (fd);
exit_noclose:
	return (error);
}

SaErrorT
saAmfPendingOperationGet (
	const SaNameT *compName,
	SaAmfPendingOperationFlagsT *pendingOperationFlags) {

	*pendingOperationFlags = 0;	
	return (SA_OK);
}

SaErrorT
saAmfResponse (
	SaInvocationT invocation,
	SaErrorT error)
{
	struct req_amf_response req_amf_response;
	int fd;
	SaErrorT errorResult;

	errorResult = saServiceConnect (&fd, MESSAGE_REQ_AMF_INIT);
	if (errorResult != SA_OK) {
		goto exit_noclose;
	}
	req_amf_response.header.id = MESSAGE_REQ_AMF_RESPONSE;
	req_amf_response.header.size = sizeof (struct req_amf_response);
	req_amf_response.invocation = invocation;
	req_amf_response.error = error;
	errorResult = saSendRetry (fd, &req_amf_response,
		sizeof (struct req_amf_response), MSG_NOSIGNAL);

	close (fd);

exit_noclose:
	return (errorResult);
}
