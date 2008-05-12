/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include <saAis.h>
#include <list.h>
#include <saMsg.h>
#include <ipc_gen.h>
#include <ipc_msg.h>

#include <ais_util.h>

struct message_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char data[4096];
};

/*
 * Data structure for instance data
 */
struct msgInstance {
	int response_fd;
	int dispatch_fd;
	SaMsgCallbacksT callbacks;
	int finalize;
	SaMsgHandleT msgHandle;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
	struct list_head queue_list;
};

struct msgQueueInstance {
	int response_fd;
	SaMsgHandleT msgHandle;
	SaMsgQueueHandleT queueHandle;
	SaMsgQueueOpenFlagsT openFlags;
	SaNameT queueName;
	struct list_head list;
	struct list_head section_iteration_list_head;
	pthread_mutex_t *response_mutex;
};


void msgHandleInstanceDestructor (void *instance);
void queueHandleInstanceDestructor (void *instance);

/*
 * All MSG instances in this database
 */
static struct saHandleDatabase msgHandleDatabase = {
	.handleCount			= 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= msgHandleInstanceDestructor
};

/*
 *  All Queue instances in this database
 */
static struct saHandleDatabase queueHandleDatabase = {
	.handleCount			= 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= queueHandleInstanceDestructor
};

/*
 * Versions supported
 */
static SaVersionT msgVersionsSupported[] = {
	{ 'B', 1, 1 }
};

static struct saVersionDatabase msgVersionDatabase = {
	sizeof (msgVersionsSupported) / sizeof (SaVersionT),
	msgVersionsSupported
};

struct iteratorSectionIdListEntry {
	struct list_head list;
	unsigned char data[0];
};


/*
 * Implementation
 */
void msgHandleInstanceDestructor (void *instance)
{
	struct msgInstance *msgInstance = instance;

	pthread_mutex_destroy (&msgInstance->response_mutex);
	pthread_mutex_destroy (&msgInstance->dispatch_mutex);
}

void queueHandleInstanceDestructor (void *instance)
{
}

#ifdef COMPILE_OUT

static void msgQueueInstanceFinalize (struct msgQueueInstance *msgQueueInstance)
{
	struct msgSectionIterationInstance *sectionIterationInstance;
	struct list_head *sectionIterationList;
	struct list_head *sectionIterationListNext;

	for (sectionIterationList = msgQueueInstance->section_iteration_list_head.next,
		sectionIterationListNext = sectionIterationList->next;
		sectionIterationList != &msgQueueInstance->section_iteration_list_head;
		sectionIterationList = sectionIterationListNext,
		sectionIterationListNext = sectionIterationList->next) {

		sectionIterationInstance = list_entry (sectionIterationList,
			struct msgSectionIterationInstance, list);

		msgSectionIterationInstanceFinalize (sectionIterationInstance);
	}

	list_del (&msgQueueInstance->list);

	saHandleDestroy (&queueHandleDatabase, msgQueueInstance->queueHandle);
}

static void msgInstanceFinalize (struct msgInstance *msgInstance)
{
	struct msgQueueInstance *msgQueueInstance;
	struct list_head *queueInstanceList;
	struct list_head *queueInstanceListNext;

	for (queueInstanceList = msgInstance->queue_list.next,
		queueInstanceListNext = queueInstanceList->next;
		queueInstanceList != &msgInstance->queue_list;
		queueInstanceList = queueInstanceListNext,
		queueInstanceListNext = queueInstanceList->next) {

		msgQueueInstance = list_entry (queueInstanceList,
			struct msgQueueInstance, list);

		msgQueueInstanceFinalize (msgQueueInstance);
	}

	saHandleDestroy (&msgHandleDatabase, msgInstance->msgHandle);
}

#endif

SaAisErrorT
saMsgInitialize (
	SaMsgHandleT *msgHandle,
	const SaMsgCallbacksT *callbacks,
	SaVersionT *version)
{
	struct msgInstance *msgInstance;
	SaAisErrorT error = SA_AIS_OK;

	if (msgHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saVersionVerify (&msgVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleCreate (&msgHandleDatabase, sizeof (struct msgInstance),
		msgHandle);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&msgHandleDatabase, *msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	msgInstance->response_fd = -1;

	error = saServiceConnect (&msgInstance->response_fd,
		&msgInstance->dispatch_fd, MSG_SERVICE);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (callbacks) {
		memcpy (&msgInstance->callbacks, callbacks, sizeof (SaMsgCallbacksT));
	} else {
		memset (&msgInstance->callbacks, 0, sizeof (SaMsgCallbacksT));
	}

	list_init (&msgInstance->queue_list);

	msgInstance->msgHandle = *msgHandle;

	pthread_mutex_init (&msgInstance->response_mutex, NULL);

	saHandleInstancePut (&msgHandleDatabase, *msgHandle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&msgHandleDatabase, *msgHandle);
error_destroy:
	saHandleDestroy (&msgHandleDatabase, *msgHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
saMsgSelectionObjectGet (
	const SaMsgHandleT msgHandle,
	SaSelectionObjectT *selectionObject)
{
	struct msgInstance *msgInstance;
	SaAisErrorT error;

	if (selectionObject == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	*selectionObject = msgInstance->dispatch_fd;

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	return (SA_AIS_OK);
}

SaAisErrorT
saMsgDispatch (
	const SaMsgHandleT msgHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int poll_fd;
	int timeout = 1;
	SaMsgCallbacksT callbacks;
	SaAisErrorT error;
	int dispatch_avail;
	struct msgInstance *msgInstance;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct message_overlay dispatch_data;
/*
	struct res_lib_msg_queueopenasync *res_lib_msg_queueopenasync;
	struct res_lib_msg_queuesynchronizeasync *res_lib_msg_queuesynchronizeasync;
	struct msgQueueInstance *msgQueueInstance;
*/

	if (dispatchFlags != SA_DISPATCH_ONE &&
		dispatchFlags != SA_DISPATCH_ALL &&
		dispatchFlags != SA_DISPATCH_BLOCKING) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ALL
	 */
	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		/*
		 * Read data directly from socket
		 */
		poll_fd = msgInstance->dispatch_fd;
		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry(&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto error_put;
		}
		pthread_mutex_lock(&msgInstance->dispatch_mutex);

		if (msgInstance->finalize == 1) {
			error = SA_AIS_OK;
			goto error_unlock;
		}

		if ((ufds.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
				error = SA_AIS_ERR_BAD_HANDLE;
				goto error_unlock;
		}
		
		dispatch_avail = (ufds.revents & POLLIN);

		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock(&msgInstance->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock(&msgInstance->dispatch_mutex);
			continue;
		}
		
		memset(&dispatch_data,0, sizeof(struct message_overlay));
		error = saRecvRetry (msgInstance->dispatch_fd, &dispatch_data.header, sizeof (mar_res_header_t));
		if (error != SA_AIS_OK) {
			goto error_unlock;
		}
		if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
			error = saRecvRetry (msgInstance->dispatch_fd, &dispatch_data.data,
				dispatch_data.header.size - sizeof (mar_res_header_t));
			if (error != SA_AIS_OK) {
				goto error_unlock;
			}
		}

		/*
		* Make copy of callbacks, message data, unlock instance,
		* and call callback. A risk of this dispatch method is that
		* the callback routines may operate at the same time that
		* MsgFinalize has been called in another thread.
		*/
		memcpy(&callbacks,&msgInstance->callbacks, sizeof(msgInstance->callbacks));
		pthread_mutex_unlock(&msgInstance->dispatch_mutex);
		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data.header.id) {
#ifdef COMPILE_OUT
		case MESSAGE_RES_MSG_QUEUE_QUEUEOPENASYNC:
			if (callbacks.saMsgQueueOpenCallback == NULL) {
				continue;
			}
			res_lib_msg_queueopenasync = (struct res_lib_msg_queueopenasync *) &dispatch_data;

			/*
			 * This instance get/listadd/put required so that close
			 * later has the proper list of queues
			 */
			if (res_lib_msg_queueopenasync->header.error == SA_AIS_OK) {
				error = saHandleInstanceGet (&queueHandleDatabase,
					res_lib_msg_queueopenasync->queueHandle,
					(void *)&msgQueueInstance);

					assert (error == SA_AIS_OK); /* should only be valid handles here */
				/*
				 * open succeeded without error
				 */
				list_init (&msgQueueInstance->list);
				list_init (&msgQueueInstance->section_iteration_list_head);
				list_add (&msgQueueInstance->list,
					&msgInstance->queue_list);

				callbacks.saMsgQueueOpenCallback(
					res_lib_msg_queueopenasync->invocation,
					res_lib_msg_queueopenasync->queueHandle,
					res_lib_msg_queueopenasync->header.error);
				saHandleInstancePut (&queueHandleDatabase,
					res_lib_msg_queueopenasync->queueHandle);
			} else {
				/*
				 * open failed with error
				 */
				callbacks.saMsgQueueOpenCallback(
					res_lib_msg_queueopenasync->invocation,
					-1,
					res_lib_msg_queueopenasync->header.error);
			}
			break;

		case MESSAGE_RES_MSG_QUEUE_QUEUESYNCHRONIZEASYNC:
			if (callbacks.saMsgQueueSynchronizeCallback == NULL) {
				continue;
			}

			res_lib_msg_queuesynchronizeasync = (struct res_lib_msg_queuesynchronizeasync *) &dispatch_data;

			callbacks.saMsgQueueSynchronizeCallback(
				res_lib_msg_queuesynchronizeasync->invocation,
				res_lib_msg_queuesynchronizeasync->header.error);
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
				cont = 0;
			break;
		case SA_DISPATCH_ALL:
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);
error_unlock:
	pthread_mutex_unlock(&msgInstance->dispatch_mutex);
error_put:
	saHandleInstancePut(&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgFinalize (
	const SaMsgHandleT msgHandle)
{
	struct msgInstance *msgInstance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&msgInstance->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (msgInstance->finalize) {
		pthread_mutex_unlock (&msgInstance->response_mutex);
		saHandleInstancePut (&msgHandleDatabase, msgHandle);
		return (SA_AIS_ERR_BAD_HANDLE);
	}

	msgInstance->finalize = 1;

	pthread_mutex_unlock (&msgInstance->response_mutex);

// TODO	msgInstanceFinalize (msgInstance);

	if (msgInstance->response_fd != -1) {
		shutdown (msgInstance->response_fd, 0);
		close (msgInstance->response_fd);
	}

	if (msgInstance->dispatch_fd != -1) {
		shutdown (msgInstance->dispatch_fd, 0);
		close (msgInstance->dispatch_fd);
	}

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	return (SA_AIS_OK);
}

SaAisErrorT
saMsgQueueOpen (
	SaMsgHandleT msgHandle,
	const SaNameT *queueName,
	const SaMsgQueueCreationAttributesT *creationAttributes,
	SaMsgQueueOpenFlagsT openFlags,
	SaTimeT timeout,
	SaMsgQueueHandleT *queueHandle)
{
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queueopen req_lib_msg_queueopen;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saHandleCreate (&queueHandleDatabase,
		sizeof (struct msgQueueInstance), queueHandle);
	if (error != SA_AIS_OK) {
		goto error_put_msg;
	}

	error = saHandleInstanceGet (&queueHandleDatabase,
		*queueHandle, (void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	msgQueueInstance->response_fd = msgInstance->response_fd;
	msgQueueInstance->response_mutex = &msgInstance->response_mutex;

	msgQueueInstance->msgHandle = msgHandle;
	msgQueueInstance->queueHandle = *queueHandle;
	msgQueueInstance->openFlags = openFlags;

	req_lib_msg_queueopen.header.size = sizeof (struct req_lib_msg_queueopen);
	req_lib_msg_queueopen.header.id = MESSAGE_REQ_MSG_QUEUEOPEN;
	memcpy (&req_lib_msg_queueopen.queueName, queueName, sizeof (SaNameT));
	memcpy (&msgQueueInstance->queueName, queueName, sizeof (SaNameT));
	req_lib_msg_queueopen.creationAttributesSet = 0;
	if (creationAttributes) {
		memcpy (&req_lib_msg_queueopen.creationAttributes,
			creationAttributes,
			sizeof (SaMsgQueueCreationAttributesT));
		req_lib_msg_queueopen.creationAttributesSet = 1;
	}
	req_lib_msg_queueopen.openFlags = openFlags;
	req_lib_msg_queueopen.timeout = timeout;

	pthread_mutex_lock (msgQueueInstance->response_mutex);

	error = saSendReceiveReply (msgQueueInstance->response_fd,
		&req_lib_msg_queueopen,
		sizeof (struct req_lib_msg_queueopen),
		&res_lib_msg_queueopen,
		sizeof (struct res_lib_msg_queueopen));

	pthread_mutex_unlock (msgQueueInstance->response_mutex);

	if (res_lib_msg_queueopen.header.error != SA_AIS_OK) {
		error = res_lib_msg_queueopen.header.error;
		goto error_put_destroy;
	}

	saHandleInstancePut (&queueHandleDatabase, *queueHandle);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	return (error);

error_put_destroy:
	saHandleInstancePut (&queueHandleDatabase, *queueHandle);
error_destroy:
	saHandleDestroy (&queueHandleDatabase, *queueHandle);
error_put_msg:
	saHandleInstancePut (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueOpenAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaNameT *queueName,
	const SaMsgQueueCreationAttributesT *creationAttributes,
	SaMsgQueueOpenFlagsT openFlags)
{
	struct msgQueueInstance *msgQueueInstance;
	struct msgInstance *msgInstance;
	SaMsgQueueHandleT queueHandle;
	SaAisErrorT error;
	struct req_lib_msg_queueopen req_lib_msg_queueopen;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (msgInstance->callbacks.saMsgQueueOpenCallback == NULL) {
		error = SA_AIS_ERR_INIT;
		goto error_put_msg;
	}

	error = saHandleCreate (&queueHandleDatabase,
		sizeof (struct msgQueueInstance), &queueHandle);
	if (error != SA_AIS_OK) {
		goto error_put_msg;
	}

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	msgQueueInstance->response_fd = msgInstance->response_fd;
	msgQueueInstance->response_mutex = &msgInstance->response_mutex;

	msgQueueInstance->msgHandle = msgHandle;
	msgQueueInstance->queueHandle = queueHandle;
	msgQueueInstance->openFlags = openFlags;
	req_lib_msg_queueopen.header.size = sizeof (struct req_lib_msg_queueopen);
	req_lib_msg_queueopen.header.id = MESSAGE_REQ_MSG_QUEUEOPEN;
	req_lib_msg_queueopen.invocation = invocation;
	req_lib_msg_queueopen.creationAttributesSet = 0;
	if (creationAttributes) {
		memcpy (&req_lib_msg_queueopen.creationAttributes,
			creationAttributes,
			sizeof (SaMsgQueueCreationAttributesT));
		req_lib_msg_queueopen.creationAttributesSet = 1;
	}
	
	req_lib_msg_queueopen.openFlags = openFlags;
	req_lib_msg_queueopen.queueHandle = queueHandle;

	pthread_mutex_unlock (msgQueueInstance->response_mutex);

	error = saSendReceiveReply (msgQueueInstance->response_fd,
		&req_lib_msg_queueopen,
		sizeof (struct req_lib_msg_queueopen),
		&res_lib_msg_queueopenasync,
		sizeof (struct res_lib_msg_queueopenasync));
	
	pthread_mutex_unlock (msgQueueInstance->response_mutex);

	if (res_lib_msg_queueopenasync.header.error != SA_AIS_OK) {
		error = res_lib_msg_queueopenasync.header.error;
		goto error_put_destroy;
	}
	
	saHandleInstancePut (&queueHandleDatabase, queueHandle);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	return (error);

error_put_destroy:
	saHandleInstancePut (&queueHandleDatabase, queueHandle);
error_destroy:
	saHandleDestroy (&queueHandleDatabase, queueHandle);
error_put_msg:
	saHandleInstancePut (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueClose (
	SaMsgQueueHandleT queueHandle)
{
	struct req_lib_msg_queueclose req_lib_msg_queueclose;
	struct res_lib_msg_queueclose res_lib_msg_queueclose;
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queueclose.header.size = sizeof (struct req_lib_msg_queueclose);
	req_lib_msg_queueclose.header.id = MESSAGE_REQ_MSG_QUEUECLOSE;
	memcpy (&req_lib_msg_queueclose.queueName,
		&msgQueueInstance->queueName, sizeof (SaNameT));


	pthread_mutex_lock (msgQueueInstance->response_mutex);

	error = saSendReceiveReply (msgQueueInstance->response_fd,
		&req_lib_msg_queueclose,
		sizeof (struct req_lib_msg_queueclose),
		&res_lib_msg_queueclose,
		sizeof (struct res_lib_msg_queueclose));

	pthread_mutex_unlock (msgQueueInstance->response_mutex);

	if (error == SA_AIS_OK) {
		error = res_lib_msg_queueclose.header.error;
	}

	if (error == SA_AIS_OK) {
// TODO		msgQueueInstanceFinalize (msgQueueInstance);
	}

	saHandleInstancePut (&queueHandleDatabase, queueHandle);
	return (error);
}

SaAisErrorT
saMsgQueueStatusGet (
	SaMsgHandleT msgHandle,
	const SaNameT *queueName,
	SaMsgQueueStatusT *queueStatus)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuestatusget req_lib_msg_queuestatusget;
	struct res_lib_msg_queuestatusget res_lib_msg_queuestatusget;
	SaAisErrorT error;

	if (queueName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuestatusget.header.size = sizeof (struct req_lib_msg_queuestatusget);
	req_lib_msg_queuestatusget.header.id = MESSAGE_REQ_MSG_QUEUESTATUSGET;
	memcpy (&req_lib_msg_queuestatusget.queueName, queueName, sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuestatusget,
		sizeof (struct req_lib_msg_queuestatusget),
		&res_lib_msg_queuestatusget,
		sizeof (struct res_lib_msg_queuestatusget));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	if (error == SA_AIS_OK)
		error = res_lib_msg_queuestatusget.header.error;
	if (error == SA_AIS_OK) {
		memcpy (queueStatus, &res_lib_msg_queuestatusget.queueStatus,
			sizeof (SaMsgQueueStatusT));
	}
	return (error);
}


SaAisErrorT
saMsgQueueUnlink (
	SaMsgHandleT msgHandle,
	const SaNameT *queueName)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queueunlink req_lib_msg_queueunlink;
	struct res_lib_msg_queueunlink res_lib_msg_queueunlink;

	if (queueName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queueunlink.header.size = sizeof (struct req_lib_msg_queueunlink);
	req_lib_msg_queueunlink.header.id = MESSAGE_REQ_MSG_QUEUEUNLINK;
	memcpy (&req_lib_msg_queueunlink.queueName, queueName, sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queueunlink,
		sizeof (struct req_lib_msg_queueunlink),
		&res_lib_msg_queueunlink,
		sizeof (struct res_lib_msg_queueunlink));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_queueunlink.header.error : error);
}

SaAisErrorT
saMsgQueueGroupCreate (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	SaMsgQueueGroupPolicyT queueGroupPolicy)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupcreate req_lib_msg_queuegroupcreate;
	struct res_lib_msg_queuegroupcreate res_lib_msg_queuegroupcreate;

	if (queueGroupName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupcreate.header.size = sizeof (struct req_lib_msg_queuegroupcreate);
	req_lib_msg_queuegroupcreate.header.id = MESSAGE_REQ_MSG_QUEUEGROUPCREATE;
	memcpy (&req_lib_msg_queuegroupcreate.queueGroupName, queueGroupName,
		sizeof (SaNameT));
	req_lib_msg_queuegroupcreate.queueGroupPolicy = queueGroupPolicy;

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuegroupcreate,
		sizeof (struct req_lib_msg_queuegroupcreate),
		&res_lib_msg_queuegroupcreate,
		sizeof (struct res_lib_msg_queuegroupcreate));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_queuegroupcreate.header.error : error);
}

SaAisErrorT
saMsgQueueGroupInsert (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	const SaNameT *queueName)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupinsert req_lib_msg_queuegroupinsert;
	struct res_lib_msg_queuegroupinsert res_lib_msg_queuegroupinsert;

	if (queueName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupinsert.header.size = sizeof (struct req_lib_msg_queuegroupinsert);
	req_lib_msg_queuegroupinsert.header.id = MESSAGE_REQ_MSG_QUEUEGROUPINSERT;
	memcpy (&req_lib_msg_queuegroupinsert.queueName, queueName, sizeof (SaNameT));
	memcpy (&req_lib_msg_queuegroupinsert.queueGroupName, queueGroupName,
		sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuegroupinsert,
		sizeof (struct req_lib_msg_queuegroupinsert),
		&res_lib_msg_queuegroupinsert,
		sizeof (struct res_lib_msg_queuegroupinsert));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_queuegroupinsert.header.error : error);
}

SaAisErrorT
saMsgQueueGroupRemove (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	const SaNameT *queueName)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupremove req_lib_msg_queuegroupremove;
	struct res_lib_msg_queuegroupremove res_lib_msg_queuegroupremove;

	if (queueName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupremove.header.size = sizeof (struct req_lib_msg_queuegroupremove);
	req_lib_msg_queuegroupremove.header.id = MESSAGE_REQ_MSG_QUEUEGROUPREMOVE;
	memcpy (&req_lib_msg_queuegroupremove.queueName, queueName, sizeof (SaNameT));
	memcpy (&req_lib_msg_queuegroupremove.queueGroupName, queueGroupName,
		sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuegroupremove,
		sizeof (struct req_lib_msg_queuegroupremove),
		&res_lib_msg_queuegroupremove,
		sizeof (struct res_lib_msg_queuegroupremove));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_queuegroupremove.header.error : error);
}

SaAisErrorT
saMsgQueueGroupDelete (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupdelete req_lib_msg_queuegroupdelete;
	struct res_lib_msg_queuegroupdelete res_lib_msg_queuegroupdelete;

	if (queueGroupName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupdelete.header.size = sizeof (struct req_lib_msg_queuegroupdelete);
	req_lib_msg_queuegroupdelete.header.id = MESSAGE_REQ_MSG_QUEUEGROUPDELETE;
	memcpy (&req_lib_msg_queuegroupdelete.queueGroupName, queueGroupName,
		sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuegroupdelete,
		sizeof (struct req_lib_msg_queuegroupdelete),
		&res_lib_msg_queuegroupdelete,
		sizeof (struct res_lib_msg_queuegroupdelete));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_queuegroupdelete.header.error : error);
}

SaAisErrorT
saMsgQueueGroupTrack (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	SaUint8T trackFlags,
	SaMsgQueueGroupNotificationBufferT *notificationBuffer)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegrouptrack req_lib_msg_queuegrouptrack;
	struct res_lib_msg_queuegrouptrack res_lib_msg_queuegrouptrack;

	if (queueGroupName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegrouptrack.header.size = sizeof (struct req_lib_msg_queuegrouptrack);
	req_lib_msg_queuegrouptrack.header.id = MESSAGE_REQ_MSG_QUEUEGROUPTRACK;
	req_lib_msg_queuegrouptrack.trackFlags = trackFlags;
	memcpy (&req_lib_msg_queuegrouptrack.queueGroupName, queueGroupName,
		sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuegrouptrack,
		sizeof (struct req_lib_msg_queuegrouptrack),
		&res_lib_msg_queuegrouptrack,
		sizeof (struct res_lib_msg_queuegrouptrack));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_queuegrouptrack.header.error : error);
}

SaAisErrorT
saMsgQueueGroupTrackStop (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegrouptrackstop req_lib_msg_queuegrouptrackstop;
	struct res_lib_msg_queuegrouptrackstop res_lib_msg_queuegrouptrackstop;

	if (queueGroupName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegrouptrackstop.header.size = sizeof (struct req_lib_msg_queuegrouptrackstop);
	req_lib_msg_queuegrouptrackstop.header.id = MESSAGE_REQ_MSG_QUEUEGROUPTRACKSTOP;
	memcpy (&req_lib_msg_queuegrouptrackstop.queueGroupName, queueGroupName,
		sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuegrouptrackstop,
		sizeof (struct req_lib_msg_queuegrouptrackstop),
		&res_lib_msg_queuegrouptrackstop,
		sizeof (struct res_lib_msg_queuegrouptrackstop));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_queuegrouptrackstop.header.error : error);
}

SaAisErrorT
saMsgMessageSend (
	SaMsgHandleT msgHandle,
	const SaNameT *destination,
	const SaMsgMessageT *message,
	SaTimeT timeout)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagesend req_lib_msg_messagesend;
	struct res_lib_msg_messagesend res_lib_msg_messagesend;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagesend.header.size = sizeof (struct req_lib_msg_messagesend);
	req_lib_msg_messagesend.header.id = MESSAGE_REQ_MSG_MESSAGESEND;
	memcpy (&req_lib_msg_messagesend.destination, destination, sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_messagesend,
		sizeof (struct req_lib_msg_messagesend),
		&res_lib_msg_messagesend,
		sizeof (struct res_lib_msg_messagesend));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_messagesend.header.error : error);
}

SaAisErrorT
saMsgMessageSendAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaNameT *destination,
	const SaMsgMessageT *message,
	SaMsgAckFlagsT ackFlags)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagesend req_lib_msg_messagesend;
	struct res_lib_msg_messagesendasync res_lib_msg_messagesendasync;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagesend.header.size = sizeof (struct req_lib_msg_messagesend);
	req_lib_msg_messagesend.header.id = MESSAGE_REQ_MSG_MESSAGESEND;
	memcpy (&req_lib_msg_messagesend.destination, destination, sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_messagesend,
		sizeof (struct req_lib_msg_messagesend),
		&res_lib_msg_messagesendasync,
		sizeof (struct res_lib_msg_messagesendasync));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_messagesendasync.header.error : error);
}

SaAisErrorT
saMsgMessageGet (
	SaMsgQueueHandleT queueHandle,
	SaMsgMessageT *message,
	SaTimeT *sendTime,
	SaMsgSenderIdT *senderId,
	SaTimeT timeout)
{
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;
	struct req_lib_msg_messageget req_lib_msg_messageget;
	struct res_lib_msg_messageget res_lib_msg_messageget;

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle, (void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messageget.header.size = sizeof (struct req_lib_msg_messageget);
	req_lib_msg_messageget.header.id = MESSAGE_REQ_MSG_MESSAGEGET;
	req_lib_msg_messageget.timeout = timeout;

	pthread_mutex_lock (msgQueueInstance->response_mutex);

	error = saSendReceiveReply (msgQueueInstance->response_fd,
		&req_lib_msg_messageget,
		sizeof (struct req_lib_msg_messageget),
		&res_lib_msg_messageget,
		sizeof (struct res_lib_msg_messageget));

	pthread_mutex_unlock (msgQueueInstance->response_mutex);

	saHandleInstancePut (&queueHandleDatabase, queueHandle);
	
	if (error == SA_AIS_OK)
		error = res_lib_msg_messageget.header.error;
	if (error == SA_AIS_OK) {
		*sendTime = res_lib_msg_messageget.sendTime;
		memcpy (senderId, &res_lib_msg_messageget.senderId,
			sizeof (SaMsgSenderIdT));
	}
	return (error);
}

SaAisErrorT
saMsgMessageCancel (
	SaMsgQueueHandleT queueHandle)
{
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;
	struct req_lib_msg_messagecancel req_lib_msg_messagecancel;
	struct res_lib_msg_messagecancel res_lib_msg_messagecancel;

	error = saHandleInstanceGet (&msgHandleDatabase, queueHandle, (void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagecancel.header.size = sizeof (struct req_lib_msg_messagecancel);
	req_lib_msg_messagecancel.header.id = MESSAGE_REQ_MSG_MESSAGECANCEL;

	pthread_mutex_lock (msgQueueInstance->response_mutex);

	error = saSendReceiveReply (msgQueueInstance->response_fd,
		&req_lib_msg_messagecancel,
		sizeof (struct req_lib_msg_messagecancel),
		&res_lib_msg_messagecancel,
		sizeof (struct res_lib_msg_messagecancel));

	pthread_mutex_unlock (msgQueueInstance->response_mutex);

	saHandleInstancePut (&queueHandleDatabase, queueHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_messagecancel.header.error : error);
}

SaAisErrorT
saMsgMessageSendReceive (
	SaMsgHandleT msgHandle,
	const SaNameT *destination,
	const SaMsgMessageT *sendMessage,
	SaMsgMessageT *receiveMessage,
	SaTimeT *replySendTime,
	SaTimeT timeout)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagesendreceive req_lib_msg_messagesendreceive;
	struct res_lib_msg_messagesendreceive res_lib_msg_messagesendreceive;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagesendreceive.header.size = sizeof (struct req_lib_msg_messagesendreceive);
	req_lib_msg_messagesendreceive.header.id = MESSAGE_REQ_MSG_MESSAGEREPLY;
	memcpy (&req_lib_msg_messagesendreceive.destination, destination,
		sizeof (SaNameT));
	req_lib_msg_messagesendreceive.timeout = timeout;

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_messagesendreceive,
		sizeof (struct req_lib_msg_messagesendreceive),
		&res_lib_msg_messagesendreceive,
		sizeof (struct res_lib_msg_messagesendreceive));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	if (error == SA_AIS_OK)
		error = res_lib_msg_messagesendreceive.header.error;
	if (error == SA_AIS_OK) {
		*replySendTime = res_lib_msg_messagesendreceive.replySendTime;
	}
	return (error);
}


SaAisErrorT
saMsgMessageReply (
	SaMsgHandleT msgHandle,
	const SaMsgMessageT *replyMessage,
	const SaMsgSenderIdT *senderId,
	SaTimeT timeout)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagereply req_lib_msg_messagereply;
	struct res_lib_msg_messagereply res_lib_msg_messagereply;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagereply.header.size = sizeof (struct req_lib_msg_messagereply);
	req_lib_msg_messagereply.header.id = MESSAGE_REQ_MSG_MESSAGEREPLY;
	memcpy (&req_lib_msg_messagereply.senderId, senderId, sizeof (SaMsgSenderIdT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_messagereply,
		sizeof (struct req_lib_msg_messagereply),
		&res_lib_msg_messagereply,
		sizeof (struct res_lib_msg_messagereply));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_messagereply.header.error : error);
}

SaAisErrorT saMsgMessageReplyAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaMsgMessageT *replyMessage,
	const SaMsgSenderIdT *senderId,
	SaMsgAckFlagsT ackFlags)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagereply req_lib_msg_messagereply;
	struct res_lib_msg_messagereplyasync res_lib_msg_messagereplyasync;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle, (void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagereply.header.size = sizeof (struct req_lib_msg_messagereply);
	req_lib_msg_messagereply.header.id = MESSAGE_REQ_MSG_MESSAGEREPLY;
	memcpy (&req_lib_msg_messagereply.senderId, senderId, sizeof (SaMsgSenderIdT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_messagereply,
		sizeof (struct req_lib_msg_messagereply),
		&res_lib_msg_messagereplyasync,
		sizeof (struct res_lib_msg_messagereplyasync));

	pthread_mutex_unlock (&msgInstance->response_mutex);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);
	
	return (error == SA_AIS_OK ? res_lib_msg_messagereplyasync.header.error : error);
}
