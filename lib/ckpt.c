/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "../include/list.h"
#include "../include/ais_types.h"
#include "../include/ais_ckpt.h"
#include "../include/ais_msg.h"
#include "util.h"

struct message_overlay {
	struct req_header header;
	char data[4096];
};

/*
 * Data structure for instance data
 */
struct ckptInstance {
	int fd;
	struct queue inq;
	SaCkptCallbacksT callbacks;
	int finalize;
	pthread_mutex_t mutex;
};

struct ckptCheckpointInstance {
	int fd;
	SaNameT checkpointName;
	SaUint32T maxSectionIdSize;
	pthread_mutex_t mutex;
};

struct ckptSectionIteratorInstance {
	int fd;
	struct list_head sectionIdListHead;
	SaUint32T maxSectionIdSize;
	pthread_mutex_t mutex;
};

void ckptHandleInstanceDestructor (void *instance);
void checkpointHandleInstanceDestructor (void *instance);
void ckptSectionIteratorHandleInstanceDestructor (void *instance);

/*
 * All CKPT instances in this database
 */
static struct saHandleDatabase ckptHandleDatabase = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= ckptHandleInstanceDestructor
};

/*
 *  All Checkpoint instances in this database
 */
static struct saHandleDatabase checkpointHandleDatabase = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= checkpointHandleInstanceDestructor
};

/*
 * All section iterators in this database
 */
static struct saHandleDatabase ckptSectionIteratorHandleDatabase = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= ckptSectionIteratorHandleInstanceDestructor
};

/*
 * Versions supported
 */
static SaVersionT ckptVersionsSupported[] = {
	{ 'A', 1, 1 },
	{ 'a', 1, 1 }
};

static struct saVersionDatabase ckptVersionDatabase = {
	sizeof (ckptVersionsSupported) / sizeof (SaVersionT),
	ckptVersionsSupported
};


/*
 * Implementation
 */
void ckptHandleInstanceDestructor (void *instance)
{
struct ckptInstance *ckptInstance = (struct ckptInstance *)instance;

	if (ckptInstance->fd != -1) {
		shutdown (ckptInstance->fd, 0);
		close (ckptInstance->fd);
	}
	if (ckptInstance->inq.items) {
		free (ckptInstance->inq.items);
	}
}

void checkpointHandleInstanceDestructor (void *instance)
{
	struct ckptCheckpointInstance *ckptCheckpointInstance = (struct ckptCheckpointInstance *)instance;

	if (ckptCheckpointInstance->fd != -1) {
		shutdown (ckptCheckpointInstance->fd, 0);

		close (ckptCheckpointInstance->fd);
	}
}

void ckptSectionIteratorHandleInstanceDestructor (void *instance)
{
	struct ckptSectionIteratorInstance *ckptSectionIteratorInstance = (struct ckptSectionIteratorInstance *)instance;

	if (ckptSectionIteratorInstance->fd != -1) {
		shutdown (ckptSectionIteratorInstance->fd, 0);

		close (ckptSectionIteratorInstance->fd);
	}
}

SaErrorT
saCkptInitialize (
	SaCkptHandleT *ckptHandle,
	const SaCkptCallbacksT *callbacks,
	const SaVersionT *version)
{
	struct ckptInstance *ckptInstance;
	SaErrorT error = SA_OK;

	error = saVersionVerify (&ckptVersionDatabase, version);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleCreate (&ckptHandleDatabase, sizeof (struct ckptInstance),
		ckptHandle);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&ckptHandleDatabase, *ckptHandle,
		(void *)&ckptInstance);
	if (error != SA_OK) {
		goto error_destroy;
	}

	ckptInstance->fd = -1;

	/*
	 * An inq is needed to store async messages while waiting for a 
	 * sync response
	 */
	error = saQueueInit (&ckptInstance->inq, 512, sizeof (void *));
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	error = saServiceConnect (&ckptInstance->fd, MESSAGE_REQ_CKPT_CHECKPOINT_INIT);
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	memcpy (&ckptInstance->callbacks, callbacks, sizeof (SaCkptCallbacksT));

	pthread_mutex_init (&ckptInstance->mutex, NULL);

	saHandleInstancePut (&ckptHandleDatabase, *ckptHandle);

	return (SA_OK);

error_put_destroy:
	saHandleInstancePut (&ckptHandleDatabase, *ckptHandle);
error_destroy:
	saHandleDestroy (&ckptHandleDatabase, *ckptHandle);
error_no_destroy:
	return (error);
}

SaErrorT
saCkptSelectionObjectGet (
	const SaCkptHandleT *ckptHandle,
	SaSelectionObjectT *selectionObject)
{
	struct ckptInstance *ckptInstance;
	SaErrorT error;

	error = saHandleInstanceGet (&ckptHandleDatabase, *ckptHandle, (void *)&ckptInstance);
	if (error != SA_OK) {
		return (error);
	}

	*selectionObject = ckptInstance->fd;

	saHandleInstancePut (&ckptHandleDatabase, *ckptHandle);

	return (SA_OK);
}

#ifdef COMPILE_OUT
SaErrorT
saCkptDispatch (
	const SaCkptHandleT *ckptHandle,
	SaDispatchFlagsT dispatchFlags)
{
	fd_set read_fds;
	SaErrorT error;
	int dispatch_avail;
	struct timeval *timeout = 0;
	struct ckptInstance *ckptInstance;
	struct req_header **queue_msg;
	struct req_header *msg;
	int empty;
	int ignore_dispatch = 0;
	int cont = 1; /* always continue do loop except when set to 0 */

	error = saHandleInstanceGet (&ckptHandleDatabase, *ckptHandle, (void *)&ckptInstance);
	if (error != SA_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ALL
	 */
	if (dispatchFlags & SA_DISPATCH_ALL) {
		timeout = &zerousec;
	}

	do {
		/*
		 * Read data directly from socket
		 */
		FD_ZERO (&read_fds);
		FD_SET (ckptInstance->fd, &read_fds);

		error = saSelectRetry (ckptInstance->fd + 1, &read_fds, 0, 0, timeout);
		if (error != SA_OK) {
			goto error_exit;
		}

		dispatch_avail = FD_ISSET (ckptInstance->fd, &read_fds);
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			continue; /* next select */
		}

		saQueueIsEmpty(&ckptInstance->inq, &empty);
		if (empty == 0) {
			/*
			 * Queue is not empty, read data from queue
			 */
			saQueueItemGet (&ckptInstance->inq, (void **)&queue_msg);
			msg = *queue_msg;
			memcpy (&ckptInstance->message, msg, msg->size);
			saQueueItemRemove (&ckptInstance->inq);
			free (msg);
		} else {
			/*
			 * Queue empty, read response from socket
			 */
			error = saRecvRetry (ckptInstance->fd, &ckptInstance->message.header, sizeof (struct req_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_exit;
			}
			if (ckptInstance->message.header.size > sizeof (struct req_header)) {
				error = saRecvRetry (ckptInstance->fd, &ckptInstance->message.data,
					ckptInstance->message.header.size - sizeof (struct req_header),
					MSG_WAITALL | MSG_NOSIGNAL);
				if (error != SA_OK) {
					goto error_exit;
				}
			}
		}

		/*
		 * Dispatch incoming response
		 */
		switch (ckptInstance->message.header.id) {
#ifdef COMPILE_OUT
		case MESSAGE_RES_CKPT_CHECKPOINT_ACTIVATEPOLL:
			/*
			 * This is a do nothing message which the node executive sends
			 * to activate the file handle in poll when the library has
			 * queued a message into amfHandle->inq
			 * The dispatch is ignored for the following two cases:
			 * 1) setting of timeout to zero for the DISPATCH_ALL case
			 * 2) expiration of the do loop for the DISPATCH_ONE case
			 */
			ignore_dispatch = 1;
			break;

		case MESSAGE_RES_CKPT_CHECKPOINT_HEALTHCHECKCALLBACK:
			res_amf_healthcheckcallback = (struct res_amf_healthcheckcallback *)&ckptInstance->message;
			amfInstance->callbacks.saAmfHealthcheckCallback (
				res_amf_healthcheckcallback->invocation,
				&res_amf_healthcheckcallback->compName,
				res_amf_healthcheckcallback->checkType);
			break;

		case MESSAGE_RES_CKPT_CHECKPOINT_READINESSSTATESETCALLBACK:
			res_amf_readinessstatesetcallback = (struct res_amf_readinessstatesetcallback *)&ckptInstance->message;
			amfInstance->callbacks.saAmfReadinessStateSetCallback (
				res_amf_readinessstatesetcallback->invocation,
				&res_amf_readinessstatesetcallback->compName,
				res_amf_readinessstatesetcallback->readinessState);
			break;

		case MESSAGE_RES_CKPT_CHECKPOINT_CSISETCALLBACK:
			res_amf_csisetcallback = (struct res_amf_csisetcallback *)&ckptInstance->message;
			amfInstance->callbacks.saAmfCSISetCallback (
				res_amf_csisetcallback->invocation,
				&res_amf_csisetcallback->compName,
				&res_amf_csisetcallback->csiName,
				res_amf_csisetcallback->csiFlags,
				&res_amf_csisetcallback->haState,
				&res_amf_csisetcallback->activeCompName,
				res_amf_csisetcallback->transitionDescriptor);
			break;

		case MESSAGE_RES_CKPT_CHECKPOINT_CSIREMOVECALLBACK:
			res_amf_csiremovecallback = (struct res_amf_csiremovecallback *)&ckptInstance->message;
			amfInstance->callbacks.saAmfCSIRemoveCallback (
				res_amf_csiremovecallback->invocation,
				&res_amf_csiremovecallback->compName,
				&res_amf_csiremovecallback->csiName,
				&res_amf_csiremovecallback->csiFlags);
			break;

		case MESSAGE_RES_CKPT_CHECKPOINT_PROTECTIONGROUPTRACKCALLBACK:
			res_amf_protectiongrouptrackcallback = (struct res_amf_protectiongrouptrackcallback *)&ckptInstance->message;
			memcpy (res_amf_protectiongrouptrackcallback->notificationBufferAddress,
				res_amf_protectiongrouptrackcallback->notificationBuffer,
				res_amf_protectiongrouptrackcallback->numberOfItems * sizeof (SaAmfProtectionGroupNotificationT));
			amfInstance->callbacks.saAmfProtectionGroupTrackCallback(
				&res_amf_protectiongrouptrackcallback->csiName,
				res_amf_protectiongrouptrackcallback->notificationBufferAddress,
				res_amf_protectiongrouptrackcallback->numberOfItems,
				res_amf_protectiongrouptrackcallback->numberOfMembers,
				res_amf_protectiongrouptrackcallback->error);
			break;

#endif
		default:
			/* TODO */
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

error_exit:
	return (error);
}
#endif

SaErrorT
saCkptFinalize (
	const SaCkptHandleT *ckptHandle)
{
	struct ckptInstance *ckptInstance;
	SaErrorT error;

	error = saHandleInstanceGet (&ckptHandleDatabase, *ckptHandle,
		(void *)&ckptInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&ckptInstance->mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (ckptInstance->finalize) {
		pthread_mutex_unlock (&ckptInstance->mutex);
		saHandleInstancePut (&ckptHandleDatabase, *ckptHandle);
		return (SA_ERR_BAD_HANDLE);
	}

	ckptInstance->finalize = 1;

	saActivatePoll (ckptInstance->fd);

	pthread_mutex_unlock (&ckptInstance->mutex);

	saHandleInstancePut (&ckptHandleDatabase, *ckptHandle);

    saHandleDestroy (&ckptHandleDatabase, *ckptHandle);

	return (SA_OK);
}

SaErrorT
saCkptCheckpointOpen (
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags,
	SaTimeT timeout,
	SaCkptCheckpointHandleT *checkpointHandle)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointopen req_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopen res_lib_ckpt_checkpointopen;

	error = saHandleCreate (&checkpointHandleDatabase,
		sizeof (struct ckptCheckpointInstance), checkpointHandle);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		goto error_destroy;
	}

	ckptCheckpointInstance->fd = -1;

	ckptCheckpointInstance->maxSectionIdSize =
		checkpointCreationAttributes->maxSectionIdSize;

	error = saServiceConnect (&ckptCheckpointInstance->fd, MESSAGE_REQ_CKPT_CHECKPOINT_INIT);
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	req_lib_ckpt_checkpointopen.header.size = sizeof (struct req_lib_ckpt_checkpointopen);
	req_lib_ckpt_checkpointopen.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPEN;
	memcpy (&req_lib_ckpt_checkpointopen.checkpointName, checkpointName, sizeof (SaNameT));
	memcpy (&ckptCheckpointInstance->checkpointName, checkpointName, sizeof (SaNameT));
	memcpy (&req_lib_ckpt_checkpointopen.checkpointCreationAttributes,
		checkpointCreationAttributes,
		sizeof (SaCkptCheckpointCreationAttributesT));
	req_lib_ckpt_checkpointopen.checkpointOpenFlags = checkpointOpenFlags;

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_checkpointopen,
		sizeof (struct req_lib_ckpt_checkpointopen), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	error = saRecvRetry (ckptCheckpointInstance->fd, &res_lib_ckpt_checkpointopen,
		sizeof (struct res_lib_ckpt_checkpointopen), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_put_destroy;
	}
	
	if (res_lib_ckpt_checkpointopen.header.error != SA_OK) {
		error = res_lib_ckpt_checkpointopen.header.error;
		goto error_put_destroy;
	}

	pthread_mutex_init (&ckptCheckpointInstance->mutex, NULL);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error);

error_put_destroy:
	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);
error_destroy:
	saHandleDestroy (&checkpointHandleDatabase, *checkpointHandle);
error_no_destroy:
	return (error);
}

SaErrorT
saCkptCheckpointOpenAsync (
	const SaCkptHandleT *ckptHandle,
	SaInvocationT invocation,	
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags)
{
	struct ckptInstance *ckptInstance;
	SaErrorT error;
	struct req_lib_ckpt_checkpointopenasync req_lib_ckpt_checkpointopenasync;

	error = saHandleInstanceGet (&ckptHandleDatabase, *ckptHandle, (void *)&ckptInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_ckpt_checkpointopenasync.header.size = sizeof (struct req_lib_ckpt_checkpointopenasync);
	req_lib_ckpt_checkpointopenasync.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC;
	req_lib_ckpt_checkpointopenasync.invocation = invocation;
	memcpy (&req_lib_ckpt_checkpointopenasync.checkpointName, checkpointName, sizeof (SaNameT));
	memcpy (&req_lib_ckpt_checkpointopenasync.checkpointCreationAttributes,
		checkpointCreationAttributes,
		sizeof (SaCkptCheckpointCreationAttributesT));
	
	req_lib_ckpt_checkpointopenasync.checkpointOpenFlags = checkpointOpenFlags;

	pthread_mutex_lock (&ckptInstance->mutex);

        error = saSendRetry (ckptInstance->fd, &req_lib_ckpt_checkpointopenasync,
		sizeof (struct req_lib_ckpt_checkpointopenasync), MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptInstance->mutex);

	saHandleInstancePut (&ckptHandleDatabase, *ckptHandle);

	return (error);
}

SaErrorT
saCkptCheckpointClose (
	const SaCkptCheckpointHandleT *checkpointHandle)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		goto error_exit;
	}

    saHandleDestroy (&checkpointHandleDatabase, *checkpointHandle);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

error_exit:
	return (error);
}

SaErrorT
saCkptCheckpointUnlink (
	const SaNameT *checkpointName)
{
	SaErrorT error;
	struct req_lib_ckpt_checkpointunlink req_lib_ckpt_checkpointunlink;
	struct res_lib_ckpt_checkpointunlink res_lib_ckpt_checkpointunlink;
	int fd;

	error = saServiceConnect (&fd, MESSAGE_REQ_CKPT_CHECKPOINT_INIT);
	if (error != SA_OK) {
		goto exit_noclose;
	}

	req_lib_ckpt_checkpointunlink.header.size = sizeof (struct req_lib_ckpt_checkpointunlink);
	req_lib_ckpt_checkpointunlink.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTUNLINK;
	memcpy (&req_lib_ckpt_checkpointunlink.checkpointName, checkpointName, sizeof (SaNameT));


	error = saSendRetry (fd, &req_lib_ckpt_checkpointunlink, sizeof (struct req_lib_ckpt_checkpointunlink), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto exit_close;
	}

	error = saRecvRetry (fd, &res_lib_ckpt_checkpointunlink,
		sizeof (struct res_lib_ckpt_checkpointunlink), MSG_WAITALL | MSG_NOSIGNAL);

exit_close:
	close (fd);
	return (error == SA_OK ? res_lib_ckpt_checkpointunlink.header.error : error);
exit_noclose:
	return (error);
}

SaErrorT
saCkptCheckpointRetentionDurationSet (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaTimeT retentionDuration)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointretentiondurationset req_lib_ckpt_checkpointretentiondurationset;
	struct res_lib_ckpt_checkpointretentiondurationset res_lib_ckpt_checkpointretentiondurationset;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		goto error_exit_noput;
	}

	req_lib_ckpt_checkpointretentiondurationset.header.size = sizeof (struct req_lib_ckpt_checkpointretentiondurationset);
	req_lib_ckpt_checkpointretentiondurationset.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET;

	req_lib_ckpt_checkpointretentiondurationset.retentionDuration = retentionDuration;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_checkpointretentiondurationset, sizeof (struct req_lib_ckpt_checkpointretentiondurationset), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_checkpointretentiondurationset,
		sizeof (struct res_lib_ckpt_checkpointretentiondurationset),
		MSG_WAITALL | MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

error_exit:
	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);
error_exit_noput:
	return (error == SA_OK ? res_lib_ckpt_checkpointretentiondurationset.header.error : error);
}

SaErrorT
saCkptActiveCheckpointSet (
	const SaCkptCheckpointHandleT *checkpointHandle)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_activecheckpointset req_lib_ckpt_activecheckpointset;
	struct res_lib_ckpt_activecheckpointset res_lib_ckpt_activecheckpointset;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		 (void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		goto error_exit;
	}

	req_lib_ckpt_activecheckpointset.header.size = sizeof (struct req_lib_ckpt_activecheckpointset);
	req_lib_ckpt_activecheckpointset.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_ACTIVECHECKPOINTSET;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_activecheckpointset,
		sizeof (struct req_lib_ckpt_activecheckpointset), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_activecheckpointset,
		sizeof (struct res_lib_ckpt_activecheckpointset),
		MSG_WAITALL | MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

error_exit:
	return (error == SA_OK ? res_lib_ckpt_activecheckpointset.header.error : error);
}

SaErrorT
saCkptCheckpointStatusGet (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptCheckpointStatusT *checkpointStatus)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointstatusget req_lib_ckpt_checkpointstatusget;
	struct res_lib_ckpt_checkpointstatusget res_lib_ckpt_checkpointstatusget;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_ckpt_checkpointstatusget.header.size = sizeof (struct req_lib_ckpt_checkpointstatusget);
	req_lib_ckpt_checkpointstatusget.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_checkpointstatusget,
		sizeof (struct req_lib_ckpt_checkpointstatusget), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_checkpointstatusget,
		sizeof (struct res_lib_ckpt_checkpointstatusget),
		MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

	memcpy (checkpointStatus,
		&res_lib_ckpt_checkpointstatusget.checkpointStatus,
		sizeof (SaCkptCheckpointStatusT));

error_exit:
	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);
	return (error);
}

SaErrorT
saCkptSectionCreate (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptSectionCreationAttributesT *sectionCreationAttributes,
	const void *initialData,
	SaUint32T initialDataSize)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectioncreate req_lib_ckpt_sectioncreate;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		return (error);
	}


	req_lib_ckpt_sectioncreate.header.size =
		sizeof (struct req_lib_ckpt_sectioncreate) +
		sectionCreationAttributes->sectionId->idLen +
		initialDataSize; 

	req_lib_ckpt_sectioncreate.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONCREATE;
	req_lib_ckpt_sectioncreate.idLen = sectionCreationAttributes->sectionId->idLen;
	req_lib_ckpt_sectioncreate.expirationTime = sectionCreationAttributes->expirationTime;
	req_lib_ckpt_sectioncreate.initialDataSize = initialDataSize;


	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_sectioncreate,
		sizeof (struct req_lib_ckpt_sectioncreate), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	/*
	 * Write section identifier to server
	 */
	error = saSendRetry (ckptCheckpointInstance->fd, sectionCreationAttributes->sectionId->id,
		sectionCreationAttributes->sectionId->idLen, MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saSendRetry (ckptCheckpointInstance->fd, initialData,
		initialDataSize, MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_sectioncreate,
		sizeof (struct res_lib_ckpt_sectioncreate),
		MSG_WAITALL | MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

error_exit:
	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error == SA_OK ? res_lib_ckpt_sectioncreate.header.error : error);
}


SaErrorT
saCkptSectionDelete (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptSectionIdT *sectionId)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectiondelete req_lib_ckpt_sectiondelete;
	struct res_lib_ckpt_sectiondelete res_lib_ckpt_sectiondelete;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	req_lib_ckpt_sectiondelete.header.size = sizeof (struct req_lib_ckpt_sectiondelete) + sectionId->idLen; 
	req_lib_ckpt_sectiondelete.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONDELETE;
	req_lib_ckpt_sectiondelete.idLen = sectionId->idLen;

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_sectiondelete,
		sizeof (struct req_lib_ckpt_sectiondelete), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	/*
	 * Write section identifier to server
	 */
	error = saSendRetry (ckptCheckpointInstance->fd, sectionId->id,
		sectionId->idLen, MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}
	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_sectiondelete,
		sizeof (struct res_lib_ckpt_sectiondelete),
		MSG_WAITALL | MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

error_exit:
	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);
	return (error == SA_OK ? res_lib_ckpt_sectiondelete.header.error : error);
}

SaErrorT
saCkptSectionExpirationTimeSet (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptSectionIdT *sectionId,
	SaTimeT expirationTime)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionexpirationtimeset req_lib_ckpt_sectionexpirationtimeset;
	struct res_lib_ckpt_sectionexpirationtimeset res_lib_ckpt_sectionexpirationtimeset;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		goto error_exit_noput;
	}

	req_lib_ckpt_sectionexpirationtimeset.header.size = sizeof (struct req_lib_ckpt_sectionexpirationtimeset) + sectionId->idLen; 
	req_lib_ckpt_sectionexpirationtimeset.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET;
	req_lib_ckpt_sectionexpirationtimeset.idLen = sectionId->idLen;
	req_lib_ckpt_sectionexpirationtimeset.expirationTime = expirationTime;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_sectionexpirationtimeset,
		sizeof (struct req_lib_ckpt_sectionexpirationtimeset), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	/*
	 * Write section identifier to server
	 */
	if (sectionId->idLen) {
		error = saSendRetry (ckptCheckpointInstance->fd, sectionId->id,
			sectionId->idLen, MSG_NOSIGNAL);
		if (error != SA_OK) {
			goto error_exit;
		}
	}

	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_sectionexpirationtimeset,
		sizeof (struct res_lib_ckpt_sectionexpirationtimeset),
		MSG_WAITALL | MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

error_exit:
	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);
error_exit_noput:
	return (error == SA_OK ? res_lib_ckpt_sectionexpirationtimeset.header.error : error);
}

SaErrorT
saCkptSectionIteratorInitialize (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptSectionsChosenT sectionsChosen,
	SaTimeT expirationTime,
	SaCkptSectionIteratorT *sectionIterator)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct ckptSectionIteratorInstance *ckptSectionIteratorInstance;
	struct req_lib_ckpt_sectioniteratorinitialize req_lib_ckpt_sectioniteratorinitialize;
	struct res_lib_ckpt_sectioniteratorinitialize res_lib_ckpt_sectioniteratorinitialize;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleCreate (&ckptSectionIteratorHandleDatabase,
		sizeof (struct ckptSectionIteratorInstance), sectionIterator);
	if (error != SA_OK) {
		goto error_put_checkpoint_db;
	}

	error = saHandleInstanceGet (&ckptSectionIteratorHandleDatabase,
		*sectionIterator,
		(void *)&ckptSectionIteratorInstance);
	if (error != SA_OK) {
		goto error_destroy;
	}

	ckptSectionIteratorInstance->fd = -1;

	pthread_mutex_init (&ckptSectionIteratorInstance->mutex, NULL);

	/*
	 * Setup section id list for iterator next
	 */
	list_init (&ckptSectionIteratorInstance->sectionIdListHead);

	ckptSectionIteratorInstance->maxSectionIdSize =
		ckptCheckpointInstance->maxSectionIdSize;

	error = saServiceConnect (&ckptSectionIteratorInstance->fd,	
		MESSAGE_REQ_CKPT_SECTIONITERATOR_INIT);
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	req_lib_ckpt_sectioniteratorinitialize.header.size = sizeof (struct req_lib_ckpt_sectioniteratorinitialize); 
	req_lib_ckpt_sectioniteratorinitialize.header.id = MESSAGE_REQ_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE;
	req_lib_ckpt_sectioniteratorinitialize.sectionsChosen = sectionsChosen;
	req_lib_ckpt_sectioniteratorinitialize.expirationTime = expirationTime;
	memcpy (&req_lib_ckpt_sectioniteratorinitialize.checkpointName,
		&ckptCheckpointInstance->checkpointName, sizeof (SaNameT));

	pthread_mutex_lock (&ckptSectionIteratorInstance->mutex);

	error = saSendRetry (ckptSectionIteratorInstance->fd,
		&req_lib_ckpt_sectioniteratorinitialize,
		sizeof (struct req_lib_ckpt_sectioniteratorinitialize),
		MSG_NOSIGNAL);

	if (error != SA_OK) {
		goto error_put_destroy;
	}

	error = saRecvRetry (ckptSectionIteratorInstance->fd,
		&res_lib_ckpt_sectioniteratorinitialize,
		sizeof (struct res_lib_ckpt_sectioniteratorinitialize),
		MSG_WAITALL | MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptSectionIteratorInstance->mutex);

	saHandleInstancePut (&ckptSectionIteratorHandleDatabase, *sectionIterator);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error == SA_OK ? res_lib_ckpt_sectioniteratorinitialize.header.error : error);

error_put_destroy:
	saHandleInstancePut (&ckptSectionIteratorHandleDatabase, *sectionIterator);
error_destroy:
	saHandleDestroy (&ckptSectionIteratorHandleDatabase, *sectionIterator);
error_put_checkpoint_db:
	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);
error_no_destroy:
	return (error);
}

struct iteratorSectionIdListEntry {
	struct list_head list;
	char data[0];
};

SaErrorT
saCkptSectionIteratorNext (
	SaCkptSectionIteratorT *sectionIterator,
	SaCkptSectionDescriptorT *sectionDescriptor)
{
	SaErrorT error;
	struct ckptSectionIteratorInstance *ckptSectionIteratorInstance;
	struct req_lib_ckpt_sectioniteratornext req_lib_ckpt_sectioniteratornext;
	struct res_lib_ckpt_sectioniteratornext res_lib_ckpt_sectioniteratornext;
	struct iteratorSectionIdListEntry *iteratorSectionIdListEntry;

	error = saHandleInstanceGet (&ckptSectionIteratorHandleDatabase,
		*sectionIterator, (void *)&ckptSectionIteratorInstance);
	if (error != SA_OK) {
		goto error_exit;
	}
	/*
	 * Allocate section id storage area
	 */
	iteratorSectionIdListEntry = malloc (sizeof (struct list_head) +
		ckptSectionIteratorInstance->maxSectionIdSize);
	if (iteratorSectionIdListEntry == 0) {
		error = SA_ERR_NO_MEMORY;
		goto error_put_nounlock;
	}

	req_lib_ckpt_sectioniteratornext.header.size = sizeof (struct req_lib_ckpt_sectioniteratornext); 
	req_lib_ckpt_sectioniteratornext.header.id = MESSAGE_REQ_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT;

	pthread_mutex_lock (&ckptSectionIteratorInstance->mutex);

	error = saSendRetry (ckptSectionIteratorInstance->fd,
		&req_lib_ckpt_sectioniteratornext,
		sizeof (struct req_lib_ckpt_sectioniteratornext), MSG_NOSIGNAL);

	if (error != SA_OK) {
		goto error_put;
	}

	error = saRecvRetry (ckptSectionIteratorInstance->fd, &res_lib_ckpt_sectioniteratornext,
		sizeof (struct res_lib_ckpt_sectioniteratornext), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_put;
	}

	memcpy (sectionDescriptor,
		&res_lib_ckpt_sectioniteratornext.sectionDescriptor,
		sizeof (SaCkptSectionDescriptorT));

	sectionDescriptor->sectionId.id = &iteratorSectionIdListEntry->data[0];
	
	if ((res_lib_ckpt_sectioniteratornext.header.size - sizeof (struct res_lib_ckpt_sectioniteratornext)) > 0) {
		error = saRecvRetry (ckptSectionIteratorInstance->fd,
			sectionDescriptor->sectionId.id,
			res_lib_ckpt_sectioniteratornext.header.size -
				sizeof (struct res_lib_ckpt_sectioniteratornext),
			MSG_WAITALL | MSG_NOSIGNAL);
	}

	/*
	 * Add to persistent memory list for this sectioniterator
	 */
	if (error == SA_OK && res_lib_ckpt_sectioniteratornext.header.error == SA_OK) {
		list_init (&iteratorSectionIdListEntry->list);
		list_add (&iteratorSectionIdListEntry->list, &ckptSectionIteratorInstance->sectionIdListHead);
	}

error_put:
	pthread_mutex_unlock (&ckptSectionIteratorInstance->mutex);
error_put_nounlock:
	saHandleInstancePut (&ckptSectionIteratorHandleDatabase, *sectionIterator);
error_exit:
	return (error == SA_OK ? res_lib_ckpt_sectioniteratornext.header.error : error);
}
	
SaErrorT
saCkptSectionIteratorFinalize (
	SaCkptSectionIteratorT *sectionIterator)
{
	SaErrorT error;
	struct ckptSectionIteratorInstance *ckptSectionIteratorInstance;
	struct iteratorSectionIdListEntry *iteratorSectionIdListEntry;
	struct list_head *sectionIdIteratorList;
	struct list_head *sectionIdIteratorListNext;

	error = saHandleInstanceGet (&ckptSectionIteratorHandleDatabase,
		*sectionIterator, (void *)&ckptSectionIteratorInstance);
	if (error != SA_OK) {
		goto error_noput;
	}

	/*
	 * iterate list of section ids for this iterator to free the allocated memory
	 * be careful to cache next pointer because free removes memory from use
	 */
	for (sectionIdIteratorList = ckptSectionIteratorInstance->sectionIdListHead.next,
		sectionIdIteratorListNext = sectionIdIteratorList->next;
		sectionIdIteratorList != &ckptSectionIteratorInstance->sectionIdListHead;
		sectionIdIteratorList = sectionIdIteratorListNext,
		sectionIdIteratorListNext = sectionIdIteratorList->next) {

		iteratorSectionIdListEntry = list_entry (sectionIdIteratorList,
			struct iteratorSectionIdListEntry, list);

		free (iteratorSectionIdListEntry);
	}

	saHandleInstancePut (&ckptSectionIteratorHandleDatabase, *sectionIterator);

    saHandleDestroy (&ckptSectionIteratorHandleDatabase, *sectionIterator);

error_noput:
	return (error);
}

SaErrorT
saCkptCheckpointWrite (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex)
{
	SaErrorT error = SA_OK;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionwrite req_lib_ckpt_sectionwrite;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	int i;
	struct iovec iov[3];
	int iov_len = 0;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_ckpt_sectionwrite.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONWRITE;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	for (i = 0; i < numberOfElements; i++) {

		req_lib_ckpt_sectionwrite.header.size = sizeof (struct req_lib_ckpt_sectionwrite) + ioVector[i].sectionId.idLen + ioVector[i].dataSize; 

		req_lib_ckpt_sectionwrite.dataOffset = ioVector[i].dataOffset;
		req_lib_ckpt_sectionwrite.dataSize = ioVector[i].dataSize;
		req_lib_ckpt_sectionwrite.idLen = ioVector[i].sectionId.idLen;

		iov_len = 0;
/* TODO check for zero length stuff */
		iov[0].iov_base = (char *)&req_lib_ckpt_sectionwrite;
		iov[0].iov_len = sizeof (struct req_lib_ckpt_sectionwrite);
		iov[1].iov_base = ioVector[i].sectionId.id;
		iov[1].iov_len = ioVector[i].sectionId.idLen;
		iov[2].iov_base = ioVector[i].dataBuffer;
		iov[2].iov_len = ioVector[i].dataSize;

		error = saSendMsgRetry (ckptCheckpointInstance->fd,
			iov,
			3);
		if (error != SA_OK) {
			goto error_exit;
		}

		/*
		 * Receive response
		 */
		error = saRecvRetry (ckptCheckpointInstance->fd, &res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite), MSG_WAITALL | MSG_NOSIGNAL);
		if (error != SA_OK) {
			goto error_exit;
		}

		if (res_lib_ckpt_sectionwrite.header.error == SA_ERR_TRY_AGAIN) {
			error = SA_ERR_TRY_AGAIN;
			goto error_exit;
		}
		/*
		 * If error, report back erroneous index
		 */
		if (res_lib_ckpt_sectionwrite.header.error != SA_OK) {
			if (erroneousVectorIndex) {
				*erroneousVectorIndex = i;
			}
			goto error_exit;
		}
	}

error_exit:

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error == SA_OK ? res_lib_ckpt_sectionwrite.header.error : error);
}

SaErrorT
saCkptSectionOverwrite (
	const SaCkptCheckpointHandleT *checkpointHandle,
	const SaCkptSectionIdT *sectionId,
	SaUint8T *dataBuffer,
	SaSizeT dataSize)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionoverwrite req_lib_ckpt_sectionoverwrite;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_ckpt_sectionoverwrite.header.size = sizeof (struct req_lib_ckpt_sectionoverwrite) + sectionId->idLen + dataSize; 
	req_lib_ckpt_sectionoverwrite.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONOVERWRITE;
	req_lib_ckpt_sectionoverwrite.idLen = sectionId->idLen;
	req_lib_ckpt_sectionoverwrite.dataSize = dataSize;
	
	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_sectionoverwrite,
		sizeof (struct req_lib_ckpt_sectionoverwrite), MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	if (sectionId->idLen) {
		error = saSendRetry (ckptCheckpointInstance->fd, sectionId->id,
			sectionId->idLen, MSG_NOSIGNAL);
		if (error != SA_OK) {
			goto error_exit;
		}
	}
	error = saSendRetry (ckptCheckpointInstance->fd, dataBuffer, dataSize, MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_sectionoverwrite,
		sizeof (struct res_lib_ckpt_sectionoverwrite),
		MSG_WAITALL | MSG_NOSIGNAL);

error_exit:
	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error == SA_OK ? res_lib_ckpt_sectionoverwrite.header.error : error);
}

SaErrorT
saCkptCheckpointRead (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex)
{
	SaErrorT error = SA_OK;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionread req_lib_ckpt_sectionread;
	struct res_lib_ckpt_sectionread res_lib_ckpt_sectionread;
	int dataLength;
	int i;
	struct iovec iov[3];

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_ckpt_sectionread.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONREAD;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	for (i = 0; i < numberOfElements; i++) {
		req_lib_ckpt_sectionread.header.size = sizeof (struct req_lib_ckpt_sectionread) +
			ioVector[i].sectionId.idLen;

		req_lib_ckpt_sectionread.idLen = ioVector[i].sectionId.idLen;
		req_lib_ckpt_sectionread.dataOffset = ioVector[i].dataOffset;
		req_lib_ckpt_sectionread.dataSize = ioVector[i].dataSize;

		iov[0].iov_base = (char *)&req_lib_ckpt_sectionread;
		iov[0].iov_len = sizeof (struct req_lib_ckpt_sectionread);
		iov[1].iov_base = ioVector[i].sectionId.id;
		iov[1].iov_len = ioVector[i].sectionId.idLen;

		error = saSendMsgRetry (ckptCheckpointInstance->fd,
			iov,
			2);

		/*
		 * Receive response header
		 */
		error = saRecvRetry (ckptCheckpointInstance->fd, &res_lib_ckpt_sectionread,
			sizeof (struct res_lib_ckpt_sectionread), MSG_WAITALL | MSG_NOSIGNAL);
		if (error != SA_OK) {
				goto error_exit;
		}
		
		dataLength = res_lib_ckpt_sectionread.header.size - sizeof (struct res_lib_ckpt_sectionread);

		/*
		 * Receive checkpoint section data
		 */
		if (dataLength > 0) {
			error = saRecvRetry (ckptCheckpointInstance->fd, ioVector[i].dataBuffer,
				dataLength, MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
					goto error_exit;
			}
		}
		if (res_lib_ckpt_sectionread.header.error != SA_OK) {
			if (erroneousVectorIndex) {
				*erroneousVectorIndex = i;
			}
			goto error_exit;
		}

		/*
		 * Report back bytes of data read
		 */
		ioVector[i].readSize = res_lib_ckpt_sectionread.dataRead;
	}

error_exit:
	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error == SA_OK ? res_lib_ckpt_sectionread.header.error : error);
}

SaErrorT
saCkptCheckpointSynchronize (
	const SaCkptCheckpointHandleT *checkpointHandle,
	SaTimeT timeout)
{
	SaErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointsynchronize req_lib_ckpt_checkpointsynchronize;
	struct res_lib_ckpt_checkpointsynchronize res_lib_ckpt_checkpointsynchronize;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_ckpt_checkpointsynchronize.header.size = sizeof (struct req_lib_ckpt_checkpointsynchronize); 
	req_lib_ckpt_checkpointsynchronize.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	error = saSendRetry (ckptCheckpointInstance->fd, &req_lib_ckpt_checkpointsynchronize,
		sizeof (struct req_lib_ckpt_checkpointsynchronize), MSG_NOSIGNAL);

	if (error != SA_OK) {
		goto error_exit;
	}

	error = saRecvRetry (ckptCheckpointInstance->fd,
		&res_lib_ckpt_checkpointsynchronize,
		sizeof (struct res_lib_ckpt_checkpointsynchronize),
		MSG_WAITALL | MSG_NOSIGNAL);

error_exit:
	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error == SA_OK ? res_lib_ckpt_checkpointsynchronize.header.error : error);
}

SaErrorT
saCkptCheckpointSynchronizeAsync (
	const SaCkptHandleT *ckptHandle,
	SaInvocationT invocation,
	const SaCkptCheckpointHandleT *checkpointHandle)
{

	return (SA_OK);

/* TODO not implemented in executive

	struct ckptInstance *ckptInstance;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	SaErrorT error;
	struct req_lib_ckpt_checkpointsynchronizeasync req_lib_ckpt_checkpointsynchronizeasync;

	error = saHandleInstanceGet (&checkpointHandleDatabase, *checkpointHandle,
		(void *)&ckptCheckpointInstance);
	if (error != SA_OK) {

		return (error);
	}

	req_lib_ckpt_checkpointsynchronizeasync.header.size = sizeof (struct req_lib_ckpt_checkpointsynchronizeasync);
	req_lib_ckpt_checkpointsynchronizeasync.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC;
	req_lib_ckpt_checkpointsynchronizeasync.invocation = invocation;

	pthread_mutex_lock (&ckptCheckpointInstance->mutex);

	pthread_mutex_lock (&ckptInstance->mutex);

	error = saSendRetry (ckptInstance->fd, &req_lib_ckpt_checkpointsynchronizeasync,
		sizeof (struct req_lib_ckpt_checkpointsynchronizeasync), MSG_NOSIGNAL);

	pthread_mutex_unlock (&ckptInstance->mutex);

	pthread_mutex_unlock (&ckptCheckpointInstance->mutex);

	saHandleInstancePut (&checkpointHandleDatabase, *checkpointHandle);

	return (error);
*/
}
