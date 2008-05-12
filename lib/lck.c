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
#include <saLck.h>
#include <ipc_gen.h>
#include <ipc_lck.h>

#include <ais_util.h>

struct message_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char data[4096];
};

/*
 * Data structure for instance data
 */
struct lckInstance {
	int response_fd;
	int dispatch_fd;
	SaLckCallbacksT callbacks;
	int finalize;
	SaLckHandleT lckHandle;
	struct list_head resource_list;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

struct lckResourceInstance {
	int response_fd;
	SaLckHandleT lckHandle;
	SaLckResourceHandleT lckResourceHandle;
	SaLckResourceOpenFlagsT resourceOpenFlags;
	SaNameT lockResourceName;
	struct list_head list;
	mar_message_source_t source;
	pthread_mutex_t *response_mutex;
};

struct lckLockIdInstance {
	int response_fd;
	SaLckResourceHandleT lckResourceHandle;
	struct list_head list;
	void *resource_lock;
	pthread_mutex_t *response_mutex;
};

void lckHandleInstanceDestructor (void *instance);
void lckResourceHandleInstanceDestructor (void *instance);
void lckResourceHandleLockIdInstanceDestructor (void *instance);

/*
 * All LCK instances in this database
 */
static struct saHandleDatabase lckHandleDatabase = {
	.handleCount			= 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= lckHandleInstanceDestructor
};

/*
 *  All Resource instances in this database
 */
static struct saHandleDatabase lckResourceHandleDatabase = {
	.handleCount			= 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= lckResourceHandleInstanceDestructor
};

/*
 *  All Resource Lock Identifier instances in this database
 */
static struct saHandleDatabase lckLockIdHandleDatabase = {
	.handleCount			= 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= lckResourceHandleLockIdInstanceDestructor
};

/*
 * Versions supported
 */
static SaVersionT lckVersionsSupported[] = {
	{ 'B', 1, 1 }
};

static struct saVersionDatabase lckVersionDatabase = {
	sizeof (lckVersionsSupported) / sizeof (SaVersionT),
	lckVersionsSupported
};


/*
 * Implementation
 */
void lckHandleInstanceDestructor (void *instance)
{
	struct lckInstance *lckInstance = instance;

	pthread_mutex_destroy (&lckInstance->response_mutex);
	pthread_mutex_destroy (&lckInstance->dispatch_mutex);
}

void lckResourceHandleInstanceDestructor (void *instance)
{
}

void lckResourceHandleLockIdInstanceDestructor (void *instance)
{
}

#ifdef NOT_DONE
static void lckSectionIterationInstanceFinalize (struct lckSectionIterationInstance *lckSectionIterationInstance)
{
	struct iteratorSectionIdListEntry *iteratorSectionIdListEntry;
	struct list_head *sectionIdIterationList;
	struct list_head *sectionIdIterationListNext;
	/*
	 * iterate list of section ids for this iterator to free the allocated memory
	 * be careful to cache next pointer because free removes memory from use
	 */
	for (sectionIdIterationList = lckSectionIterationInstance->sectionIdListHead.next,
		sectionIdIterationListNext = sectionIdIterationList->next;
		sectionIdIterationList != &lckSectionIterationInstance->sectionIdListHead;
		sectionIdIterationList = sectionIdIterationListNext,
		sectionIdIterationListNext = sectionIdIterationList->next) {

		iteratorSectionIdListEntry = list_entry (sectionIdIterationList,
			struct iteratorSectionIdListEntry, list);

		free (iteratorSectionIdListEntry);
	}

	list_del (&lckSectionIterationInstance->list);

	saHandleDestroy (&lckSectionIterationHandleDatabase,
		lckSectionIterationInstance->sectionIterationHandle);
}

static void lckResourceInstanceFinalize (struct lckResourceInstance *lckResourceInstance)
{
	struct lckSectionIterationInstance *sectionIterationInstance;
	struct list_head *sectionIterationList;
	struct list_head *sectionIterationListNext;

	for (sectionIterationList = lckResourceInstance->section_iteration_list_head.next,
		sectionIterationListNext = sectionIterationList->next;
		sectionIterationList != &lckResourceInstance->section_iteration_list_head;
		sectionIterationList = sectionIterationListNext,
		sectionIterationListNext = sectionIterationList->next) {

		sectionIterationInstance = list_entry (sectionIterationList,
			struct lckSectionIterationInstance, list);

		lckSectionIterationInstanceFinalize (sectionIterationInstance);
	}

	list_del (&lckResourceInstance->list);

	saHandleDestroy (&lckResourceHandleDatabase, lckResourceInstance->lckResourceHandle);
}

static void lckInstanceFinalize (struct lckInstance *lckInstance)
{
	struct lckResourceInstance *lckResourceInstance;
	struct list_head *resourceInstanceList;
	struct list_head *resourceInstanceListNext;

	for (resourceInstanceList = lckInstance->resource_list.next,
		resourceInstanceListNext = resourceInstanceList->next;
		resourceInstanceList != &lckInstance->resource_list;
		resourceInstanceList = resourceInstanceListNext,
		resourceInstanceListNext = resourceInstanceList->next) {

		lckResourceInstance = list_entry (resourceInstanceList,
			struct lckResourceInstance, list);

		lckResourceInstanceFinalize (lckResourceInstance);
	}

	saHandleDestroy (&lckHandleDatabase, lckInstance->lckHandle);
}

#endif

SaAisErrorT
saLckInitialize (
	SaLckHandleT *lckHandle,
	const SaLckCallbacksT *callbacks,
	SaVersionT *version)
{
	struct lckInstance *lckInstance;
	SaAisErrorT error = SA_AIS_OK;

	if (lckHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saVersionVerify (&lckVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleCreate (&lckHandleDatabase, sizeof (struct lckInstance),
		lckHandle);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&lckHandleDatabase, *lckHandle,
		(void *)&lckInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	lckInstance->response_fd = -1;

	error = saServiceConnect (&lckInstance->response_fd,
		&lckInstance->dispatch_fd, LCK_SERVICE);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (callbacks) {
		memcpy (&lckInstance->callbacks, callbacks, sizeof (SaLckCallbacksT));
	} else {
		memset (&lckInstance->callbacks, 0, sizeof (SaLckCallbacksT));
	}

	list_init (&lckInstance->resource_list);

	lckInstance->lckHandle = *lckHandle;

	pthread_mutex_init (&lckInstance->response_mutex, NULL);


	saHandleInstancePut (&lckHandleDatabase, *lckHandle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&lckHandleDatabase, *lckHandle);
error_destroy:
	saHandleDestroy (&lckHandleDatabase, *lckHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
saLckSelectionObjectGet (
	const SaLckHandleT lckHandle,
	SaSelectionObjectT *selectionObject)
{
	struct lckInstance *lckInstance;
	SaAisErrorT error;

	if (selectionObject == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = saHandleInstanceGet (&lckHandleDatabase, lckHandle, (void *)&lckInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	*selectionObject = lckInstance->dispatch_fd;

	saHandleInstancePut (&lckHandleDatabase, lckHandle);

	return (SA_AIS_OK);
}

SaAisErrorT
saLckOptionCheck (
	SaLckHandleT lckHandle,
	SaLckOptionsT *lckOptions)
{
	return (SA_AIS_OK);
}

SaAisErrorT
saLckDispatch (
	const SaLckHandleT lckHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int poll_fd;
	int timeout = 1;
	SaLckCallbacksT callbacks;
	SaAisErrorT error;
	int dispatch_avail;
	struct lckInstance *lckInstance;
	struct lckResourceInstance *lckResourceInstance;
	struct lckLockIdInstance *lckLockIdInstance;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct message_overlay dispatch_data;
	struct res_lib_lck_lockwaitercallback *res_lib_lck_lockwaitercallback;
	struct res_lib_lck_resourceopenasync *res_lib_lck_resourceopenasync = NULL;
	struct res_lib_lck_resourcelockasync *res_lib_lck_resourcelockasync = NULL;
	struct res_lib_lck_resourceunlockasync *res_lib_lck_resourceunlockasync;


	if (dispatchFlags != SA_DISPATCH_ONE &&
		dispatchFlags != SA_DISPATCH_ALL &&
		dispatchFlags != SA_DISPATCH_BLOCKING) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&lckHandleDatabase, lckHandle,
		(void *)&lckInstance);
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
		poll_fd = lckInstance->dispatch_fd;
		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry(&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto error_put;
		}
		pthread_mutex_lock(&lckInstance->dispatch_mutex);

		if (lckInstance->finalize == 1) {
			error = SA_AIS_OK;
			goto error_unlock;
		}

		if ((ufds.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
				error = SA_AIS_ERR_BAD_HANDLE;
				goto error_unlock;
		}
		
		dispatch_avail = (ufds.revents & POLLIN);

		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock(&lckInstance->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock(&lckInstance->dispatch_mutex);
			continue;
		}
		
		memset(&dispatch_data,0, sizeof(struct message_overlay));
		error = saRecvRetry (lckInstance->dispatch_fd, &dispatch_data.header, sizeof (mar_res_header_t));
		if (error != SA_AIS_OK) {
			goto error_unlock;
		}
		if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
			error = saRecvRetry (lckInstance->dispatch_fd, &dispatch_data.data,
				dispatch_data.header.size - sizeof (mar_res_header_t));
			if (error != SA_AIS_OK) {
				goto error_unlock;
			}
		}

		/*
		* Make copy of callbacks, message data, unlock instance,
		* and call callback. A risk of this dispatch method is that
		* the callback routines may operate at the same time that
		* LckFinalize has been called in another thread.
		*/
		memcpy(&callbacks,&lckInstance->callbacks, sizeof(lckInstance->callbacks));
		pthread_mutex_unlock(&lckInstance->dispatch_mutex);
		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data.header.id) {
		case MESSAGE_RES_LCK_LOCKWAITERCALLBACK:
			if (callbacks.saLckResourceOpenCallback == NULL) {
				continue;
			}
			res_lib_lck_lockwaitercallback = (struct res_lib_lck_lockwaitercallback *)&dispatch_data;
			callbacks.saLckLockWaiterCallback (
				res_lib_lck_lockwaitercallback->waiter_signal,
				res_lib_lck_lockwaitercallback->lock_id,
				res_lib_lck_lockwaitercallback->mode_held,
				res_lib_lck_lockwaitercallback->mode_requested);
			break;

		case MESSAGE_RES_LCK_RESOURCEOPENASYNC:
			if (callbacks.saLckLockWaiterCallback == NULL) {
				continue;
			}
			res_lib_lck_resourceopenasync = (struct res_lib_lck_resourceopenasync *)&dispatch_data;
			/*
			 * This instance get/listadd/put required so that close
			 * later has the proper list of resources
			 */
			if (res_lib_lck_resourceopenasync->header.error == SA_AIS_OK) {
				error = saHandleInstanceGet (&lckResourceHandleDatabase,
					res_lib_lck_resourceopenasync->resourceHandle,
					(void *)&lckResourceInstance);

					assert (error == SA_AIS_OK); /* should only be valid handles here */
				/*
				 * open succeeded without error
				 */

				callbacks.saLckResourceOpenCallback(
					res_lib_lck_resourceopenasync->invocation,
					res_lib_lck_resourceopenasync->resourceHandle,
					res_lib_lck_resourceopenasync->header.error);
				saHandleInstancePut (&lckResourceHandleDatabase,
					res_lib_lck_resourceopenasync->resourceHandle);
			} else {
				/*
				 * open failed with error
				 */
				callbacks.saLckResourceOpenCallback(
					res_lib_lck_resourceopenasync->invocation,
					-1,
					res_lib_lck_resourceopenasync->header.error);
			}
			break;
		case MESSAGE_RES_LCK_RESOURCELOCKASYNC:
			DPRINT (("grant\n"));
			if (callbacks.saLckLockGrantCallback == NULL) {
				continue;
			}
			res_lib_lck_resourcelockasync = (struct res_lib_lck_resourcelockasync *)&dispatch_data;
			/*
			 * This instance get/listadd/put required so that close
			 * later has the proper list of resources
			 */
			if (res_lib_lck_resourcelockasync->header.error == SA_AIS_OK) {
				error = saHandleInstanceGet (&lckLockIdHandleDatabase,
					res_lib_lck_resourcelockasync->lockId,
					(void *)&lckLockIdInstance);

					assert (error == SA_AIS_OK); /* should only be valid handles here */
				/*
				 * open succeeded without error
				 */
				lckLockIdInstance->resource_lock = res_lib_lck_resourcelockasync->resource_lock;

				callbacks.saLckLockGrantCallback(
					res_lib_lck_resourcelockasync->invocation,
					res_lib_lck_resourcelockasync->lockStatus,
					res_lib_lck_resourcelockasync->header.error);
				saHandleInstancePut (&lckLockIdHandleDatabase,
					res_lib_lck_resourcelockasync->lockId);
			} else {
				/*
				 * open failed with error
				 */
				callbacks.saLckLockGrantCallback (
					res_lib_lck_resourceopenasync->invocation,
					-1,
					res_lib_lck_resourceopenasync->header.error);
			}
			break;


		case MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC:
			if (callbacks.saLckResourceUnlockCallback == NULL) {
				continue;
			}
			res_lib_lck_resourceunlockasync = (struct res_lib_lck_resourceunlockasync *)&dispatch_data;
			callbacks.saLckResourceUnlockCallback (
				res_lib_lck_resourceunlockasync->invocation,
				res_lib_lck_resourceunlockasync->header.error);

			if (res_lib_lck_resourcelockasync->header.error == SA_AIS_OK) {
				error = saHandleInstanceGet (&lckLockIdHandleDatabase,
							     res_lib_lck_resourceunlockasync->lockId,
							     (void *)&lckLockIdInstance);
				if (error == SA_AIS_OK) {
					saHandleInstancePut (&lckLockIdHandleDatabase, res_lib_lck_resourceunlockasync->lockId);

					saHandleDestroy (&lckLockIdHandleDatabase, res_lib_lck_resourceunlockasync->lockId);
				}
			}
			break;
#ifdef NOT_DONE_YET

		case MESSAGE_RES_LCK_RESOURCESYNCHRONIZEASYNC:
			if (callbacks.saLckResourceSynchronizeCallback == NULL) {
				continue;
			}

			res_lib_lck_resourcesynchronizeasync = (struct res_lib_lck_resourcesynchronizeasync *) &dispatch_data;

			callbacks.saLckResourceSynchronizeCallback(
				res_lib_lck_resourcesynchronizeasync->invocation,
				res_lib_lck_resourcesynchronizeasync->header.error);
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
	pthread_mutex_unlock(&lckInstance->dispatch_mutex);
error_put:
	saHandleInstancePut(&lckHandleDatabase, lckHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckFinalize (
	const SaLckHandleT lckHandle)
{
	struct lckInstance *lckInstance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&lckHandleDatabase, lckHandle,
		(void *)&lckInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&lckInstance->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (lckInstance->finalize) {
		pthread_mutex_unlock (&lckInstance->response_mutex);
		saHandleInstancePut (&lckHandleDatabase, lckHandle);
		return (SA_AIS_ERR_BAD_HANDLE);
	}

	lckInstance->finalize = 1;

	pthread_mutex_unlock (&lckInstance->response_mutex);

// TODO	lckInstanceFinalize (lckInstance);

	if (lckInstance->response_fd != -1) {
		shutdown (lckInstance->response_fd, 0);
		close (lckInstance->response_fd);
	}

	if (lckInstance->dispatch_fd != -1) {
		shutdown (lckInstance->dispatch_fd, 0);
		close (lckInstance->dispatch_fd);
	}

	saHandleInstancePut (&lckHandleDatabase, lckHandle);

	return (SA_AIS_OK);
}

SaAisErrorT
saLckResourceOpen (
	SaLckHandleT lckHandle,
	const SaNameT *lockResourceName,
	SaLckResourceOpenFlagsT resourceOpenFlags,
	SaTimeT timeout,
	SaLckResourceHandleT *lckResourceHandle)
{
	SaAisErrorT error;
	struct lckResourceInstance *lckResourceInstance;
	struct lckInstance *lckInstance;
	struct req_lib_lck_resourceopen req_lib_lck_resourceopen;
	struct res_lib_lck_resourceopen res_lib_lck_resourceopen;

	if (lckResourceHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (lockResourceName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&lckHandleDatabase, lckHandle,
		(void *)&lckInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saHandleCreate (&lckResourceHandleDatabase,
		sizeof (struct lckResourceInstance), lckResourceHandle);
	if (error != SA_AIS_OK) {
		goto error_put_lck;
	}

	error = saHandleInstanceGet (&lckResourceHandleDatabase,
		*lckResourceHandle, (void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	lckResourceInstance->response_fd = lckInstance->response_fd;

	lckResourceInstance->lckHandle = lckHandle;
	lckResourceInstance->lckResourceHandle = *lckResourceHandle;
	lckResourceInstance->response_mutex = &lckInstance->response_mutex;

	req_lib_lck_resourceopen.header.size = sizeof (struct req_lib_lck_resourceopen);
	req_lib_lck_resourceopen.header.id = MESSAGE_REQ_LCK_RESOURCEOPEN;

	marshall_to_mar_name_t (&req_lib_lck_resourceopen.lockResourceName, (SaNameT *)lockResourceName);

	memcpy (&lckResourceInstance->lockResourceName, lockResourceName, sizeof(SaNameT));
	req_lib_lck_resourceopen.resourceOpenFlags = resourceOpenFlags;
	req_lib_lck_resourceopen.resourceHandle = *lckResourceHandle;
	req_lib_lck_resourceopen.async_call = 0;

	pthread_mutex_lock (&lckInstance->response_mutex);

	error = saSendReceiveReply (lckResourceInstance->response_fd, 
		&req_lib_lck_resourceopen,
		sizeof (struct req_lib_lck_resourceopen),
		&res_lib_lck_resourceopen,
		sizeof (struct res_lib_lck_resourceopen));
	
	pthread_mutex_unlock (&lckInstance->response_mutex);

	if (res_lib_lck_resourceopen.header.error != SA_AIS_OK) {
		error = res_lib_lck_resourceopen.header.error;
		goto error_put_destroy;
	}

	memcpy (&lckResourceInstance->source,
		&res_lib_lck_resourceopen.source,
		sizeof (mar_message_source_t));

	saHandleInstancePut (&lckResourceHandleDatabase, *lckResourceHandle);

	saHandleInstancePut (&lckHandleDatabase, lckHandle);

	list_init (&lckResourceInstance->list);

	list_add (&lckResourceInstance->list, &lckInstance->resource_list);
	return (error);

error_put_destroy:
	saHandleInstancePut (&lckResourceHandleDatabase, *lckResourceHandle);
error_destroy:
	saHandleDestroy (&lckResourceHandleDatabase, *lckResourceHandle);
error_put_lck:
	saHandleInstancePut (&lckHandleDatabase, lckHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceOpenAsync (
	SaLckHandleT lckHandle,
	SaInvocationT invocation,
	const SaNameT *lockResourceName,
	SaLckResourceOpenFlagsT resourceOpenFlags)
{
	struct lckResourceInstance *lckResourceInstance;
	struct lckInstance *lckInstance;
	SaLckResourceHandleT lckResourceHandle;
	SaAisErrorT error;
	struct req_lib_lck_resourceopen req_lib_lck_resourceopen;
	struct res_lib_lck_resourceopenasync res_lib_lck_resourceopenasync;

	error = saHandleInstanceGet (&lckHandleDatabase, lckHandle,
		(void *)&lckInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (lckInstance->callbacks.saLckResourceOpenCallback == NULL) {
		error = SA_AIS_ERR_INIT;
		goto error_put_lck;
	}

	error = saHandleCreate (&lckResourceHandleDatabase,
		sizeof (struct lckResourceInstance), &lckResourceHandle);
	if (error != SA_AIS_OK) {
		goto error_put_lck;
	}

	error = saHandleInstanceGet (&lckResourceHandleDatabase, lckResourceHandle,
		(void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	lckResourceInstance->response_fd = lckInstance->response_fd;
	lckResourceInstance->response_mutex = &lckInstance->response_mutex;
	lckResourceInstance->lckHandle = lckHandle;
	lckResourceInstance->lckResourceHandle = lckResourceHandle;
	lckResourceInstance->resourceOpenFlags = resourceOpenFlags;

	memcpy (&lckResourceInstance->lockResourceName, lockResourceName, sizeof (SaNameT));
	req_lib_lck_resourceopen.header.size = sizeof (struct req_lib_lck_resourceopen);
	req_lib_lck_resourceopen.header.id = MESSAGE_REQ_LCK_RESOURCEOPENASYNC;
	req_lib_lck_resourceopen.invocation = invocation;
	req_lib_lck_resourceopen.resourceOpenFlags = resourceOpenFlags;
	req_lib_lck_resourceopen.resourceHandle = lckResourceHandle;
	req_lib_lck_resourceopen.async_call = 1;

	pthread_mutex_lock (&lckInstance->response_mutex);

	error = saSendReceiveReply (lckResourceInstance->response_fd, 
		&req_lib_lck_resourceopen,
		sizeof (struct req_lib_lck_resourceopen),
		&res_lib_lck_resourceopenasync,
		sizeof (struct res_lib_lck_resourceopenasync));

	pthread_mutex_unlock (&lckInstance->response_mutex);

	if (error == SA_AIS_OK) {
		saHandleInstancePut (&lckResourceHandleDatabase,
			lckResourceHandle);
		saHandleInstancePut (&lckHandleDatabase, lckHandle);
		return (res_lib_lck_resourceopenasync.header.error);
	}

	saHandleInstancePut (&lckResourceHandleDatabase, lckResourceHandle);
error_destroy:
	saHandleDestroy (&lckResourceHandleDatabase, lckResourceHandle);
error_put_lck:
	saHandleInstancePut (&lckHandleDatabase, lckHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceClose (
	SaLckResourceHandleT lckResourceHandle)
{
	struct req_lib_lck_resourceclose req_lib_lck_resourceclose;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;
	SaAisErrorT error;
	struct lckResourceInstance *lckResourceInstance;

	error = saHandleInstanceGet (&lckResourceHandleDatabase, lckResourceHandle,
		(void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_lck_resourceclose.header.size = sizeof (struct req_lib_lck_resourceclose);
	req_lib_lck_resourceclose.header.id = MESSAGE_REQ_LCK_RESOURCECLOSE;
	marshall_to_mar_name_t (&req_lib_lck_resourceclose.lockResourceName,
		&lckResourceInstance->lockResourceName);
	req_lib_lck_resourceclose.resourceHandle = lckResourceHandle;

	pthread_mutex_lock (lckResourceInstance->response_mutex);

	error = saSendReceiveReply (lckResourceInstance->response_fd, 
		&req_lib_lck_resourceclose,
		sizeof (struct req_lib_lck_resourceclose),
		&res_lib_lck_resourceclose,
		sizeof (struct res_lib_lck_resourceclose));

	pthread_mutex_unlock (lckResourceInstance->response_mutex);

	saHandleInstancePut (&lckResourceHandleDatabase, lckResourceHandle);

	saHandleDestroy (&lckResourceHandleDatabase, lckResourceHandle);

	return (error == SA_AIS_OK ? res_lib_lck_resourceclose.header.error : error);
}

SaAisErrorT
saLckResourceLock (
	SaLckResourceHandleT lckResourceHandle,
	SaLckLockIdT *lockId,
	SaLckLockModeT lockMode,
	SaLckLockFlagsT lockFlags,
	SaLckWaiterSignalT waiterSignal,	
	SaTimeT timeout,
	SaLckLockStatusT *lockStatus)
{
	struct req_lib_lck_resourcelock req_lib_lck_resourcelock;
	struct res_lib_lck_resourcelock res_lib_lck_resourcelock;
	SaAisErrorT error;
	struct lckResourceInstance *lckResourceInstance;
	struct lckLockIdInstance *lckLockIdInstance;
	int lock_fd;
	int dummy_fd;

	error = saHandleInstanceGet (&lckResourceHandleDatabase, lckResourceHandle,
		(void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = saHandleCreate (&lckLockIdHandleDatabase,
		sizeof (struct lckLockIdInstance), lockId);
	if (error != SA_AIS_OK) {
		goto error_put_lck;
	}

	error = saHandleInstanceGet (&lckLockIdHandleDatabase, *lockId,
		(void *)&lckLockIdInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = saServiceConnect (&lock_fd, &dummy_fd, LCK_SERVICE);
	if (error != SA_AIS_OK) { // TODO error handling
		goto error_destroy;
	}

	lckLockIdInstance->response_mutex = lckResourceInstance->response_mutex;
	lckLockIdInstance->response_fd = lckResourceInstance->response_fd;
	lckLockIdInstance->lckResourceHandle = lckResourceHandle;

	req_lib_lck_resourcelock.header.size = sizeof (struct req_lib_lck_resourcelock);
	req_lib_lck_resourcelock.header.id = MESSAGE_REQ_LCK_RESOURCELOCK;
	marshall_to_mar_name_t (&req_lib_lck_resourcelock.lockResourceName,
		&lckResourceInstance->lockResourceName);
	req_lib_lck_resourcelock.lockMode = lockMode;
	req_lib_lck_resourcelock.lockFlags = lockFlags;
	req_lib_lck_resourcelock.waiterSignal = waiterSignal;
	req_lib_lck_resourcelock.lockId = *lockId;
	req_lib_lck_resourcelock.async_call = 0;
	req_lib_lck_resourcelock.invocation = 0;
	req_lib_lck_resourcelock.resourceHandle = lckResourceHandle;

	memcpy (&req_lib_lck_resourcelock.source,
		&lckResourceInstance->source,
		sizeof (mar_message_source_t));

	/*
	 * no mutex needed here since its a new connection
	 */
	error = saSendReceiveReply (lock_fd, 
		&req_lib_lck_resourcelock,
		sizeof (struct req_lib_lck_resourcelock),
		&res_lib_lck_resourcelock,
		sizeof (struct res_lib_lck_resourcelock));

	close (lock_fd);
	close (dummy_fd);

	if (error == SA_AIS_OK) {
		lckLockIdInstance->resource_lock = res_lib_lck_resourcelock.resource_lock;
		*lockStatus = res_lib_lck_resourcelock.lockStatus;

		return (res_lib_lck_resourcelock.header.error);
	}

	/*
	 * Error
	 */
	saHandleInstancePut (&lckLockIdHandleDatabase, *lockId);

error_destroy:
	saHandleDestroy (&lckLockIdHandleDatabase, *lockId);

error_put_lck:
	saHandleInstancePut (&lckResourceHandleDatabase, lckResourceHandle);
	return (error);

}

SaAisErrorT
saLckResourceLockAsync (
	SaLckResourceHandleT lckResourceHandle,
	SaInvocationT invocation,
	SaLckLockIdT *lockId,
	SaLckLockModeT lockMode,
	SaLckLockFlagsT lockFlags,
	SaLckWaiterSignalT waiterSignal)
{
	struct req_lib_lck_resourcelock req_lib_lck_resourcelock;
	struct res_lib_lck_resourcelockasync res_lib_lck_resourcelockasync;
	SaAisErrorT error;
	struct lckResourceInstance *lckResourceInstance;
	struct lckLockIdInstance *lckLockIdInstance;
	int lock_fd;
	int dummy_fd;

	error = saHandleInstanceGet (&lckResourceHandleDatabase, lckResourceHandle,
		(void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = saHandleCreate (&lckLockIdHandleDatabase,
		sizeof (struct lckLockIdInstance), lockId);
	if (error != SA_AIS_OK) {
		goto error_put_lck;
	}

	error = saHandleInstanceGet (&lckLockIdHandleDatabase, *lockId,
		(void *)&lckLockIdInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = saServiceConnect (&lock_fd, &dummy_fd, LCK_SERVICE);
	if (error != SA_AIS_OK) { // TODO error handling
		goto error_destroy;
	}

	lckLockIdInstance->response_mutex = lckResourceInstance->response_mutex;
	lckLockIdInstance->response_fd = lckResourceInstance->response_fd;
	lckLockIdInstance->lckResourceHandle = lckResourceHandle;

	req_lib_lck_resourcelock.header.size = sizeof (struct req_lib_lck_resourcelock);
	req_lib_lck_resourcelock.header.id = MESSAGE_REQ_LCK_RESOURCELOCKASYNC;
	marshall_to_mar_name_t (&req_lib_lck_resourcelock.lockResourceName,
		&lckResourceInstance->lockResourceName);
	req_lib_lck_resourcelock.lockMode = lockMode;
	req_lib_lck_resourcelock.lockFlags = lockFlags;
	req_lib_lck_resourcelock.waiterSignal = waiterSignal;
	req_lib_lck_resourcelock.lockId = *lockId;
	req_lib_lck_resourcelock.async_call = 1;
	req_lib_lck_resourcelock.invocation = invocation;
	req_lib_lck_resourcelock.resourceHandle = lckResourceHandle;

	memcpy (&req_lib_lck_resourcelock.source,
		&lckResourceInstance->source,
		sizeof (mar_message_source_t));

	/*
	 * no mutex needed here since its a new connection
	 */
	error = saSendReceiveReply (lock_fd, 
		&req_lib_lck_resourcelock,
		sizeof (struct req_lib_lck_resourcelock),
		&res_lib_lck_resourcelockasync,
		sizeof (struct res_lib_lck_resourcelock));

	close (lock_fd);
	close (dummy_fd);

	if (error == SA_AIS_OK) {
		return (res_lib_lck_resourcelockasync.header.error);
	}

	/*
	 * Error
	 */
	saHandleInstancePut (&lckLockIdHandleDatabase, *lockId);

error_destroy:
	saHandleDestroy (&lckLockIdHandleDatabase, *lockId);

error_put_lck:
	saHandleInstancePut (&lckResourceHandleDatabase, lckResourceHandle);
	return (error);
}

SaAisErrorT
saLckResourceUnlock (
	SaLckLockIdT lockId,
	SaTimeT timeout)
{
	struct req_lib_lck_resourceunlock req_lib_lck_resourceunlock;
	struct res_lib_lck_resourceunlock res_lib_lck_resourceunlock;
	SaAisErrorT error;
	struct lckLockIdInstance *lckLockIdInstance;
	struct lckResourceInstance *lckResourceInstance;

	error = saHandleInstanceGet (&lckLockIdHandleDatabase, lockId,
		(void *)&lckLockIdInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/*
	 * Retrieve resource name
	 */
	error = saHandleInstanceGet (&lckResourceHandleDatabase,
		lckLockIdInstance->lckResourceHandle, (void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		saHandleInstancePut (&lckLockIdHandleDatabase, lockId);
		return (error);
	}

	marshall_to_mar_name_t (&req_lib_lck_resourceunlock.lockResourceName,
		&lckResourceInstance->lockResourceName);

	saHandleInstancePut (&lckResourceHandleDatabase,
		lckLockIdInstance->lckResourceHandle);

	req_lib_lck_resourceunlock.header.size = sizeof (struct req_lib_lck_resourceunlock);
	req_lib_lck_resourceunlock.header.id = MESSAGE_REQ_LCK_RESOURCEUNLOCK;
	req_lib_lck_resourceunlock.lockId = lockId;
	req_lib_lck_resourceunlock.timeout = timeout;
	req_lib_lck_resourceunlock.invocation = -1;
	req_lib_lck_resourceunlock.async_call = 0;
	req_lib_lck_resourceunlock.resource_lock = lckLockIdInstance->resource_lock;

	pthread_mutex_lock (lckLockIdInstance->response_mutex);

	error = saSendReceiveReply (lckLockIdInstance->response_fd, 
		&req_lib_lck_resourceunlock,
		sizeof (struct req_lib_lck_resourceunlock),
		&res_lib_lck_resourceunlock,
		sizeof (struct res_lib_lck_resourceunlock));

	pthread_mutex_unlock (lckLockIdInstance->response_mutex);

	saHandleInstancePut (&lckLockIdHandleDatabase, lockId);

	saHandleDestroy (&lckLockIdHandleDatabase, lockId);

	return (error == SA_AIS_OK ? res_lib_lck_resourceunlock.header.error : error);
}

SaAisErrorT
saLckResourceUnlockAsync (
	SaInvocationT invocation,	
	SaLckLockIdT lockId)
{
	struct req_lib_lck_resourceunlock req_lib_lck_resourceunlock;
	struct res_lib_lck_resourceunlockasync res_lib_lck_resourceunlockasync;
	SaAisErrorT error;
	struct lckLockIdInstance *lckLockIdInstance;
	struct lckResourceInstance *lckResourceInstance;

	error = saHandleInstanceGet (&lckLockIdHandleDatabase, lockId,
		(void *)&lckLockIdInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/*
	 * Retrieve resource name
	 */
	error = saHandleInstanceGet (&lckResourceHandleDatabase,
		lckLockIdInstance->lckResourceHandle, (void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		saHandleInstancePut (&lckLockIdHandleDatabase, lockId);
		return (error);
	}

	marshall_to_mar_name_t (&req_lib_lck_resourceunlock.lockResourceName,
		&lckResourceInstance->lockResourceName);

	saHandleInstancePut (&lckResourceHandleDatabase,
		lckLockIdInstance->lckResourceHandle);


	/*
	 * Build and send request
	 */
	req_lib_lck_resourceunlock.header.size = sizeof (struct req_lib_lck_resourceunlock);
	req_lib_lck_resourceunlock.header.id = MESSAGE_REQ_LCK_RESOURCEUNLOCKASYNC;
	req_lib_lck_resourceunlock.invocation = invocation;
	req_lib_lck_resourceunlock.lockId = lockId;
	req_lib_lck_resourceunlock.async_call = 1;

	pthread_mutex_lock (lckLockIdInstance->response_mutex);

	error = saSendReceiveReply (lckLockIdInstance->response_fd, 
		&req_lib_lck_resourceunlock,
		sizeof (struct req_lib_lck_resourceunlock),
		&res_lib_lck_resourceunlockasync,
		sizeof (struct res_lib_lck_resourceunlockasync));

	pthread_mutex_unlock (lckLockIdInstance->response_mutex);

	saHandleInstancePut (&lckLockIdHandleDatabase, lockId);

	return (error == SA_AIS_OK ? res_lib_lck_resourceunlockasync.header.error : error);
}

SaAisErrorT
saLckLockPurge (
	SaLckResourceHandleT lckResourceHandle)
{
	struct req_lib_lck_lockpurge req_lib_lck_lockpurge;
	struct res_lib_lck_lockpurge res_lib_lck_lockpurge;
	SaAisErrorT error;
	struct lckResourceInstance *lckResourceInstance;

	error = saHandleInstanceGet (&lckResourceHandleDatabase, lckResourceHandle,
		(void *)&lckResourceInstance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_lck_lockpurge.header.size = sizeof (struct req_lib_lck_lockpurge);
	req_lib_lck_lockpurge.header.id = MESSAGE_REQ_LCK_LOCKPURGE;
	marshall_to_mar_name_t (&req_lib_lck_lockpurge.lockResourceName,
		&lckResourceInstance->lockResourceName);

	pthread_mutex_lock (lckResourceInstance->response_mutex);

	error = saSendReceiveReply (lckResourceInstance->response_fd, 
		&req_lib_lck_lockpurge,
		sizeof (struct req_lib_lck_lockpurge),
		&res_lib_lck_lockpurge,
		sizeof (struct res_lib_lck_lockpurge));

	pthread_mutex_unlock (lckResourceInstance->response_mutex);

	saHandleInstancePut (&lckResourceHandleDatabase, lckResourceHandle);

	return (error == SA_AIS_OK ? res_lib_lck_lockpurge.header.error : error);
}
