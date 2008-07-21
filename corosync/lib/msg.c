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
#include <saMsg.h>

#include <ipc_gen.h>
#include <ipc_msg.h>
#include <ais_util.h>

#include <list.h>

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
	return;
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

#endif	/* COMPILE_OUT */

SaAisErrorT
saMsgInitialize (
	SaMsgHandleT *msgHandle,
	const SaMsgCallbacksT *callbacks,
	SaVersionT *version)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgInitialize { msgHandle = %llx }\n",
		(unsigned long long) *msgHandle);

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgSelectionObjectGet { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	if (selectionObject == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
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
	struct msgQueueInstance *msgQueueInstance;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct message_overlay dispatch_data;

	struct res_lib_msg_queueopenasync *res_lib_msg_queueopenasync;
	struct res_lib_msg_messagesendasync *res_lib_msg_messagesendasync;
	struct res_lib_msg_queuegrouptrack *res_lib_msg_queuegrouptrack;

	if (dispatchFlags != SA_DISPATCH_ONE &&
	    dispatchFlags != SA_DISPATCH_ALL &&
	    dispatchFlags != SA_DISPATCH_BLOCKING)
	{
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
		
		memset(&dispatch_data, 0, sizeof(struct message_overlay));

		error = saRecvRetry (msgInstance->dispatch_fd, &dispatch_data.header,
			sizeof (mar_res_header_t));
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
		memcpy(&callbacks, &msgInstance->callbacks,
		       sizeof(msgInstance->callbacks));

		pthread_mutex_unlock(&msgInstance->dispatch_mutex);

		/* DEBUG */
		printf ("[DEBUG]: saMsgDispatch { id = %d }\n",
			dispatch_data.header.id);

		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data.header.id)
		{
		case MESSAGE_RES_MSG_QUEUEOPENASYNC:

			if (callbacks.saMsgQueueOpenCallback == NULL) {
				continue;
			}
			res_lib_msg_queueopenasync =
				(struct res_lib_msg_queueopenasync *) &dispatch_data;

			/*
			 * This instance get/listadd/put required so that close
			 * later has the proper list of queues
			 */
			if (res_lib_msg_queueopenasync->header.error == SA_AIS_OK) {
				error = saHandleInstanceGet (&queueHandleDatabase,
					res_lib_msg_queueopenasync->queueHandle,
					(void *)&msgQueueInstance);

				assert (error == SA_AIS_OK);

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

		case MESSAGE_RES_MSG_MESSAGESENDASYNC:

			if (callbacks.saMsgMessageDeliveredCallback == NULL) {
				continue;
			}
			res_lib_msg_messagesendasync =
				(struct res_lib_msg_messagesendasync *) &dispatch_data;

			callbacks.saMsgMessageDeliveredCallback (
				res_lib_msg_messagesendasync->invocation,
				res_lib_msg_messagesendasync->header.error);

			break;

		case MESSAGE_RES_MSG_QUEUEGROUPTRACK:

			if (callbacks.saMsgQueueGroupTrackCallback == NULL) {
				continue;
			}
			res_lib_msg_queuegrouptrack =
				(struct res_lib_msg_queuegrouptrack *) &dispatch_data;

			res_lib_msg_queuegrouptrack->notificationBuffer.notification =
				(SaMsgQueueGroupNotificationT *)
				(((char *) &dispatch_data) + sizeof (struct res_lib_msg_queuegrouptrack));

			callbacks.saMsgQueueGroupTrackCallback (
				&res_lib_msg_queuegrouptrack->queueGroupName,
				&res_lib_msg_queuegrouptrack->notificationBuffer,
				res_lib_msg_queuegrouptrack->numberOfMembers,
				res_lib_msg_queuegrouptrack->header.error);

			break;

		default:
			/* TODO */
			break;
		}

		/*
		 * Determine if more messages should be processed
		 */
		switch (dispatchFlags)
		{
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
	SaAisErrorT error;
	struct msgInstance *msgInstance;

	/* DEBUG */
	printf ("[DEBUG]: saMsgFinalize { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

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

	/* TODO */
	/* msgInstanceFinalize (msgInstance); */

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
	struct msgInstance *msgInstance;
	struct msgQueueInstance *msgQueueInstance;
	struct req_lib_msg_queueopen req_lib_msg_queueopen;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueOpen { queueName = %s }\n",
		(char *) queueName->value);

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

	req_lib_msg_queueopen.header.size =
		sizeof (struct req_lib_msg_queueopen);
	req_lib_msg_queueopen.header.id =
		MESSAGE_REQ_MSG_QUEUEOPEN;

	memcpy (&req_lib_msg_queueopen.queueName, queueName,
		sizeof (SaNameT));
	memcpy (&msgQueueInstance->queueName, queueName,
		sizeof (SaNameT));

	req_lib_msg_queueopen.invocation = 0;
	req_lib_msg_queueopen.creationAttributesSet = 0;

	if (creationAttributes) {
		memcpy (&req_lib_msg_queueopen.creationAttributes,
			creationAttributes,
			sizeof (SaMsgQueueCreationAttributesT));
		req_lib_msg_queueopen.creationAttributesSet = 1;
	}

	req_lib_msg_queueopen.openFlags = openFlags; /* ? */
	req_lib_msg_queueopen.queueHandle = *queueHandle; /* ? */
	req_lib_msg_queueopen.timeout = timeout; /* ? */

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
	SaAisErrorT error;
	SaMsgQueueHandleT queueHandle;
	struct msgInstance *msgInstance;
	struct msgQueueInstance *msgQueueInstance;
	struct req_lib_msg_queueopen req_lib_msg_queueopen;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueOpenAsync { queueName = %s }\n",
		(char *) queueName->value);

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

	req_lib_msg_queueopen.header.size =
		sizeof (struct req_lib_msg_queueopen);
	req_lib_msg_queueopen.header.id =
		MESSAGE_REQ_MSG_QUEUEOPENASYNC;

	memcpy (&req_lib_msg_queueopen.queueName, queueName,
		sizeof (SaNameT));
	memcpy (&msgQueueInstance->queueName, queueName,
		sizeof (SaNameT));

	req_lib_msg_queueopen.invocation = invocation;
	req_lib_msg_queueopen.creationAttributesSet = 0;

	if (creationAttributes) {
		memcpy (&req_lib_msg_queueopen.creationAttributes,
			creationAttributes,
			sizeof (SaMsgQueueCreationAttributesT));
		req_lib_msg_queueopen.creationAttributesSet = 1;
	}
	
	req_lib_msg_queueopen.openFlags = openFlags; /* ? */
	req_lib_msg_queueopen.queueHandle = queueHandle; /* ? */
	req_lib_msg_queueopen.timeout = 0; /* ? */

	pthread_mutex_lock (msgQueueInstance->response_mutex);

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
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;
	struct req_lib_msg_queueclose req_lib_msg_queueclose;
	struct res_lib_msg_queueclose res_lib_msg_queueclose;

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queueclose.header.size =
		sizeof (struct req_lib_msg_queueclose);
	req_lib_msg_queueclose.header.id =
		MESSAGE_REQ_MSG_QUEUECLOSE;

	memcpy (&req_lib_msg_queueclose.queueName, &msgQueueInstance->queueName,
		sizeof (SaNameT));

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
		/* TODO	*/
		/* msgQueueInstanceFinalize (msgQueueInstance); */
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
	SaAisErrorT error;
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuestatusget req_lib_msg_queuestatusget;
	struct res_lib_msg_queuestatusget res_lib_msg_queuestatusget;

	if (queueName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueStatusGet { queueName = %s }\n",
		(char *) queueName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuestatusget.header.size =
		sizeof (struct req_lib_msg_queuestatusget);
	req_lib_msg_queuestatusget.header.id =
		MESSAGE_REQ_MSG_QUEUESTATUSGET;

	memcpy (&req_lib_msg_queuestatusget.queueName, queueName,
		sizeof (SaNameT));

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
saMsgQueueRetentionTimeSet (
	SaMsgQueueHandleT queueHandle,
	SaTimeT *retentionTime)
{
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* TODO */

	saHandleInstancePut (&queueHandleDatabase, queueHandle);

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueUnlink { queueName = %s }\n",
		(char *) queueName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queueunlink.header.size =
		sizeof (struct req_lib_msg_queueunlink);
	req_lib_msg_queueunlink.header.id =
		MESSAGE_REQ_MSG_QUEUEUNLINK;

	memcpy (&req_lib_msg_queueunlink.queueName, queueName,
		sizeof (SaNameT));

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupCreate { queueGroupName = %s }\n",
		(char *) queueGroupName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupcreate.header.size =
		sizeof (struct req_lib_msg_queuegroupcreate);
	req_lib_msg_queuegroupcreate.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPCREATE;

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupInsert { queueGroupName = %s }\n",
		(char *) queueGroupName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupinsert.header.size =
		sizeof (struct req_lib_msg_queuegroupinsert);
	req_lib_msg_queuegroupinsert.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPINSERT;

	memcpy (&req_lib_msg_queuegroupinsert.queueName, queueName,
		sizeof (SaNameT));
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

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupRemove { queueGroupName = %s }\n",
		(char *) queueGroupName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupremove.header.size =
		sizeof (struct req_lib_msg_queuegroupremove);
	req_lib_msg_queuegroupremove.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPREMOVE;

	memcpy (&req_lib_msg_queuegroupremove.queueName, queueName,
		sizeof (SaNameT));
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

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupDelete { queueGroupName = %s }\n",
		(char *) queueGroupName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegroupdelete.header.size =
		sizeof (struct req_lib_msg_queuegroupdelete);
	req_lib_msg_queuegroupdelete.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPDELETE;

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

	if ((notificationBuffer != NULL) &&
	    (notificationBuffer->notification != NULL) &&
	    (notificationBuffer->numberOfItems == 0)) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if ((notificationBuffer != NULL) &&
	    (notificationBuffer->notification == NULL)) {
		notificationBuffer->numberOfItems = 0;
	}

	if ((trackFlags & SA_TRACK_CHANGES) &&
	    (trackFlags & SA_TRACK_CHANGES_ONLY)) {
		return (SA_AIS_ERR_BAD_FLAGS);
	}

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupTrack { queueGroupName = %s }\n",
		(char *) queueGroupName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegrouptrack.header.size =
		sizeof (struct req_lib_msg_queuegrouptrack);
	req_lib_msg_queuegrouptrack.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPTRACK;

	req_lib_msg_queuegrouptrack.trackFlags = trackFlags;
	req_lib_msg_queuegrouptrack.bufferFlag = (notificationBuffer != NULL);

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupTrack { bufferFlag = %d }\n",
		(int)(req_lib_msg_queuegrouptrack.bufferFlag));

	memcpy (&req_lib_msg_queuegrouptrack.queueGroupName, queueGroupName,
		sizeof (SaNameT));

	pthread_mutex_lock (&msgInstance->response_mutex);

	/*
	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_queuegrouptrack,
		sizeof (struct req_lib_msg_queuegrouptrack),
		&res_lib_msg_queuegrouptrack,
		sizeof (struct res_lib_msg_queuegrouptrack));
	*/

	error = saSendRetry (msgInstance->response_fd, &req_lib_msg_queuegrouptrack,
		sizeof (struct req_lib_msg_queuegrouptrack));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saRecvRetry (msgInstance->response_fd, &res_lib_msg_queuegrouptrack,
		sizeof (struct res_lib_msg_queuegrouptrack));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if ((trackFlags & SA_TRACK_CURRENT) && (notificationBuffer != NULL)) {
		if (notificationBuffer->notification != NULL) {
			if (notificationBuffer->numberOfItems < res_lib_msg_queuegrouptrack.numberOfMembers) {
				error = SA_AIS_ERR_NO_SPACE;
				goto error_exit;
			}
		} else {
			notificationBuffer->notification =
				malloc (sizeof (SaMsgQueueGroupNotificationT) *
					res_lib_msg_queuegrouptrack.numberOfMembers);

			if (notificationBuffer->notification == NULL) {
				error = SA_AIS_ERR_NO_MEMORY;
				goto error_exit;
			}

			memset (notificationBuffer->notification, 0,
				(sizeof (SaMsgQueueGroupNotificationT) *
				 res_lib_msg_queuegrouptrack.numberOfMembers));
		}

		error = saRecvRetry (msgInstance->response_fd,
				     notificationBuffer->notification,
				     (sizeof (SaMsgQueueGroupNotificationT) *
				      res_lib_msg_queuegrouptrack.numberOfMembers));
	}

error_exit:
	pthread_mutex_unlock (&msgInstance->response_mutex);
error_put_msg:
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

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupTrackStop { queueGroupName = %s }\n",
		(char *) queueGroupName->value);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_queuegrouptrackstop.header.size =
		sizeof (struct req_lib_msg_queuegrouptrackstop);
	req_lib_msg_queuegrouptrackstop.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPTRACKSTOP;

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
saMsgQueueGroupNotificationFree (
	SaMsgHandleT msgHandle,
	SaMsgQueueGroupNotificationT *notification)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* TODO */

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	return (error);
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

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageSend { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagesend.header.size =
		sizeof (struct req_lib_msg_messagesend) + message->size;
	req_lib_msg_messagesend.header.id =
		MESSAGE_REQ_MSG_MESSAGESEND;

	memcpy (&req_lib_msg_messagesend.destination, destination,
		sizeof (SaNameT));
	memcpy (&req_lib_msg_messagesend.message, message,
		sizeof (SaMsgMessageT));

	req_lib_msg_messagesend.invocation = 0;
	req_lib_msg_messagesend.ackFlags = 0;
	req_lib_msg_messagesend.async_call = 0;
	req_lib_msg_messagesend.timeout = timeout;

	pthread_mutex_lock (&msgInstance->response_mutex);

	/*
	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_messagesend,
		sizeof (struct req_lib_msg_messagesend),
		&res_lib_msg_messagesend,
		sizeof (struct res_lib_msg_messagesend));
	*/

	error = saSendRetry (msgInstance->response_fd, &req_lib_msg_messagesend,
		sizeof (struct req_lib_msg_messagesend));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saSendRetry (msgInstance->response_fd,
		message->data, message->size);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saRecvRetry (msgInstance->response_fd, &res_lib_msg_messagesend,
		sizeof (struct res_lib_msg_messagesend));

error_exit:
	pthread_mutex_unlock (&msgInstance->response_mutex);
error_put_msg:
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

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageSendAsync { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagesend.header.size =
		sizeof (struct req_lib_msg_messagesend) + message->size;
	req_lib_msg_messagesend.header.id =
		MESSAGE_REQ_MSG_MESSAGESENDASYNC;

	memcpy (&req_lib_msg_messagesend.destination, destination,
		sizeof (SaNameT));
	memcpy (&req_lib_msg_messagesend.message, message,
		sizeof (SaMsgMessageT));

	req_lib_msg_messagesend.invocation = invocation;
	req_lib_msg_messagesend.ackFlags = ackFlags;
	req_lib_msg_messagesend.async_call = 1;
	req_lib_msg_messagesend.timeout = 0;

	pthread_mutex_lock (&msgInstance->response_mutex);

	/*
	error = saSendReceiveReply (msgInstance->response_fd,
		&req_lib_msg_messagesend,
		sizeof (struct req_lib_msg_messagesend),
		&res_lib_msg_messagesendasync,
		sizeof (struct res_lib_msg_messagesendasync));
	*/

	error = saSendRetry (msgInstance->response_fd, &req_lib_msg_messagesend,
		sizeof (struct req_lib_msg_messagesend));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saSendRetry (msgInstance->response_fd,
		message->data, message->size);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saRecvRetry (msgInstance->response_fd, &res_lib_msg_messagesendasync,
		sizeof (struct res_lib_msg_messagesendasync));

error_exit:
	pthread_mutex_unlock (&msgInstance->response_mutex);
error_put_msg:
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

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageGet { queueHandle = %llx }\n",
		(unsigned long long) queueHandle);

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messageget.header.size =
		sizeof (struct req_lib_msg_messageget);
	req_lib_msg_messageget.header.id =
		MESSAGE_REQ_MSG_MESSAGEGET;

	memcpy (&req_lib_msg_messageget.queueName, &msgQueueInstance->queueName,
		sizeof (SaNameT));

	req_lib_msg_messageget.timeout = timeout;

	pthread_mutex_lock (msgQueueInstance->response_mutex);

	/*
	error = saSendReceiveReply (msgQueueInstance->response_fd,
		&req_lib_msg_messageget,
		sizeof (struct req_lib_msg_messageget),
		&res_lib_msg_messageget,
		sizeof (struct res_lib_msg_messageget));
	*/

	error = saSendRetry (msgQueueInstance->response_fd, &req_lib_msg_messageget,
		sizeof (struct req_lib_msg_messageget));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saRecvRetry (msgQueueInstance->response_fd, &res_lib_msg_messageget,
		sizeof (struct res_lib_msg_messageget));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (message->data == NULL) {
		message->size = res_lib_msg_messageget.message.size;
		message->data = malloc (message->size);
		if (message->data == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
	} else {
		if (res_lib_msg_messageget.message.size > message->size) {
			error = SA_AIS_ERR_NO_SPACE;
			goto error_exit;
		}
	}

	error = saRecvRetry (msgQueueInstance->response_fd,
		message->data, message->size);

error_exit:
	pthread_mutex_unlock (msgQueueInstance->response_mutex);
error_put_msg:
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
saMsgMessageDataFree (
	SaMsgHandleT msgHandle,
	void *data)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;

	if (data == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageDataFree { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	free (data);

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageCancel { queueHandle = %llx }\n",
		(unsigned long long) queueHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagecancel.header.size =
		sizeof (struct req_lib_msg_messagecancel);
	req_lib_msg_messagecancel.header.id =
		MESSAGE_REQ_MSG_MESSAGECANCEL;

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageSendReceive { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagesendreceive.header.size =
		sizeof (struct req_lib_msg_messagesendreceive);
	req_lib_msg_messagesendreceive.header.id =
		MESSAGE_REQ_MSG_MESSAGEREPLY;

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

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageReply { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagereply.header.size =
		sizeof (struct req_lib_msg_messagereply);
	req_lib_msg_messagereply.header.id =
		MESSAGE_REQ_MSG_MESSAGEREPLY;

	memcpy (&req_lib_msg_messagereply.senderId, senderId,
		sizeof (SaMsgSenderIdT));

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

SaAisErrorT
saMsgMessageReplyAsync (
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

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageReplyAsync { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_msg_messagereply.header.size =
		sizeof (struct req_lib_msg_messagereply);
	req_lib_msg_messagereply.header.id =
		MESSAGE_REQ_MSG_MESSAGEREPLY;

	memcpy (&req_lib_msg_messagereply.senderId, senderId,
		sizeof (SaMsgSenderIdT));

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

SaAisErrorT
saMsgQueueCapacityThresholdSet (
	SaMsgQueueHandleT queueHandle,
	const SaMsgQueueThresholdsT *thresholds)
{
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueCapacityThresholdsSet { queueHandle = %llx }\n",
		(unsigned long long) queueHandle);

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* TODO */

	saHandleInstancePut (&queueHandleDatabase, queueHandle);

	return (error);
}

SaAisErrorT
saMsgQueueCapacityThresholdGet (
	SaMsgQueueHandleT queueHandle,
	SaMsgQueueThresholdsT *thresholds)
{
	SaAisErrorT error;
	struct msgQueueInstance *msgQueueInstance;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueCapacityThresholdGet { queueHandle = %llx }\n",
		(unsigned long long) queueHandle);

	error = saHandleInstanceGet (&queueHandleDatabase, queueHandle,
		(void *)&msgQueueInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* TODO */

	saHandleInstancePut (&queueHandleDatabase, queueHandle);

	return (error);
}

SaAisErrorT
saMsgMetadataSizeGet (
	SaMsgHandleT msgHandle,
	SaUint32T *metadataSize)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMetadataSizeGet { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* TODO */

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	return (error);
}

SaAisErrorT
saMsgLimitGet (
	SaMsgHandleT msgHandle,
	SaMsgLimitIdT limitId,
	SaLimitValueT *limitValue)
{
	SaAisErrorT error;
	struct msgInstance *msgInstance;

	/* DEBUG */
	printf ("[DEBUG]: saMsgLimitGet { msgHandle = %llx }\n",
		(unsigned long long) msgHandle);

	error = saHandleInstanceGet (&msgHandleDatabase, msgHandle,
		(void *)&msgInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/* TODO */

	saHandleInstancePut (&msgHandleDatabase, msgHandle);

	return (error);
}
