
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>

#include "../include/ais_types.h"
#include "../include/ais_msg.h"
#include "util.h"

SaErrorT
saServiceConnect (
	int *fdOut,
	enum req_init_types initType)
{
	int fd;
	int result;
	struct sockaddr_un address;
	struct req_lib_init req_lib_init;
	struct res_lib_init res_lib_init;
	SaErrorT error;
	gid_t egid;

	/*
	 * Allow set group id binaries to be authenticated
	 */
	egid = getegid();
	setregid (egid, -1);

	memset (&address, 0, sizeof (struct sockaddr_un));
	address.sun_family = PF_UNIX;
	strcpy (address.sun_path + 1, "libais.socket");
	fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		return (SA_ERR_SYSTEM);
	}
	result = connect (fd, (struct sockaddr *)&address, sizeof (address));
	if (result == -1) {
		return (SA_ERR_TRY_AGAIN);
	}

	req_lib_init.header.magic = MESSAGE_MAGIC;
	req_lib_init.header.size = sizeof (req_lib_init);
	req_lib_init.header.id = initType;

	error = saSendRetry (fd, &req_lib_init, sizeof (struct req_lib_init),
		MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}
	error = saRecvRetry (fd, &res_lib_init,
		sizeof (struct res_lib_init), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_OK) {
		goto error_exit;
	}

	/*
	 * Check for security errors
	 */
	if (res_lib_init.error != SA_OK) {
		error = res_lib_init.error;
		goto error_exit;
	}

	*fdOut = fd;
	return (SA_OK);
error_exit:
	close (fd);
	return (error);
}

SaErrorT
saRecvRetry (
	int s,
	void *msg,
	size_t len,
	int flags)
{
	SaErrorT error = SA_OK;
	int result;
	struct msghdr msg_recv;
	struct iovec iov_recv;

	iov_recv.iov_base = (void *)msg;
	iov_recv.iov_len = len;

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;

retry_recv:
	result = recvmsg (s, &msg_recv, flags);
	if (result == -1 && errno == EINTR) {
		goto retry_recv;
	}
	if (result == -1 || result != len) {
		error = SA_ERR_SYSTEM;
	}
	return (error);
}

struct message_overlay {
	struct message_header header;
	char instance[4096];
};

SaErrorT
saRecvQueue (
	int s,
	void *msg,
	struct queue *queue,
	int findMessageId)
{
	struct message_overlay *message_overlay = (struct message_overlay *)msg;
	void *inq_msg;
	int match;
	SaErrorT error;

	do {
		error = saRecvRetry (s, &message_overlay->header, sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
		if (error != SA_OK) {
			goto error_exit;
		}
		if (message_overlay->header.size > sizeof (struct message_header)) {
			error = saRecvRetry (s, &message_overlay->instance, message_overlay->header.size - sizeof (struct message_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_exit;
			}
		}
		match = (message_overlay->header.id == findMessageId);

		if (match == 0 && queue) {
			inq_msg = (void *)malloc (message_overlay->header.size);
			if (inq_msg == 0) {
				error = SA_ERR_NO_MEMORY;
				goto error_exit;
			}
			memcpy (inq_msg, msg, message_overlay->header.size);
			error = saQueueItemAdd (queue, &inq_msg);
			if (error != SA_OK) {
				free (inq_msg);
				goto error_exit;
			}

			error = saActivatePoll (s);
			if (error != SA_OK) {
				goto error_exit;
			}
		}
	} while (match == 0);

error_exit:
	return (error);
}

SaErrorT
saActivatePoll (int s) {
	struct req_lib_activatepoll req_lib_activatepoll;
	SaErrorT error;

	/*
	 * Send activate poll to tell nodeexec to activate poll
	 * on this file descriptor
	 */
	req_lib_activatepoll.header.magic = MESSAGE_MAGIC;
	req_lib_activatepoll.header.size = sizeof (req_lib_activatepoll);
	req_lib_activatepoll.header.id = MESSAGE_REQ_LIB_ACTIVATEPOLL;

	error = saSendRetry (s, &req_lib_activatepoll,
		sizeof (struct req_lib_activatepoll), MSG_NOSIGNAL);
	return (error);
}

SaErrorT
saSendRetry (
	int s,
	const void *msg,
	size_t len,
	int flags)
{
	SaErrorT error = SA_OK;
	int result;

	struct msghdr msg_send;
	struct iovec iov_send;

	iov_send.iov_base = (void *)msg;
	iov_send.iov_len = len;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_iovlen = 1;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

retry_send:
	result = sendmsg (s, &msg_send, flags);
	if (result == -1 && errno == EINTR) {
		goto retry_send;
	}
	if (result == -1) {
		error = SA_ERR_SYSTEM;
	}
	return (error);
}

SaErrorT saSendMsgRetry (
        int s,
        struct iovec *iov,
        int iov_len)
{
	SaErrorT error = SA_OK;
	int result;

	struct msghdr msg_send;

	msg_send.msg_iov = iov;
	msg_send.msg_iovlen = iov_len;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

retry_send:
	result = sendmsg (s, &msg_send, MSG_NOSIGNAL);
	if (result == -1 && errno == EINTR) {
		goto retry_send;
	}
	if (result == -1) {
		error = SA_ERR_SYSTEM;
	}
	return (error);
}

SaErrorT
saSelectRetry (
	int s,
	fd_set *readfds,
	fd_set *writefds,
	fd_set *exceptfds,
	struct timeval *timeout)
{
	SaErrorT error = SA_OK;
	int result;

retry_select:
	result = select (s, readfds, writefds, exceptfds, timeout);
	if (result == -1 && errno == EINTR) {
		goto retry_select;
	}
	if (result == -1) {
		error = SA_ERR_SYSTEM;
	}

	return (error);
}

SaErrorT
saPollRetry (
        struct pollfd *ufds,
        unsigned int nfds,
        int timeout) 
{
	SaErrorT error = SA_OK;
	int result;

retry_poll:
	result = poll (ufds, nfds, timeout);
	if (result == -1 && errno == EINTR) {
		goto retry_poll;
	}
	if (result == -1) {
		error = SA_ERR_SYSTEM;
	}

	return (error);
}


SaErrorT
saHandleCreate (
	struct saHandleDatabase *handleDatabase,
	int instanceSize,
	int *handleOut)
{
	int handle;
	void *newHandles;
	int found = 0;
	void *instance;

	pthread_mutex_lock (&handleDatabase->mutex);

	for (handle = 0; handle < handleDatabase->handleCount; handle++) {
		if (handleDatabase->handles[handle].state == SA_HANDLE_STATE_EMPTY) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		handleDatabase->handleCount += 1;
		newHandles = (struct saHandle *)realloc (handleDatabase->handles,
			sizeof (struct saHandle) * handleDatabase->handleCount);
		if (newHandles == 0) {
			pthread_mutex_unlock (&handleDatabase->mutex);
			return (SA_ERR_NO_MEMORY);
		}
		handleDatabase->handles = newHandles;
	}

	instance = malloc (instanceSize);
	if (instance == 0) {
		return (SA_ERR_NO_MEMORY);
	}
	memset (instance, 0, instanceSize);

	handleDatabase->handles[handle].state = SA_HANDLE_STATE_ACTIVE;

	handleDatabase->handles[handle].instance = instance;

	handleDatabase->handles[handle].refCount = 1;

	*handleOut = handle;

	pthread_mutex_unlock (&handleDatabase->mutex);

	return (SA_OK);
}


SaErrorT
saHandleDestroy (
	struct saHandleDatabase *handleDatabase,
	unsigned int handle)
{
	pthread_mutex_lock (&handleDatabase->mutex);
	handleDatabase->handles[handle].state = SA_HANDLE_STATE_PENDINGREMOVAL;
	pthread_mutex_unlock (&handleDatabase->mutex);
	saHandleInstancePut (handleDatabase, handle);

	return (SA_OK);
}


SaErrorT
saHandleInstanceGet (
	struct saHandleDatabase *handleDatabase,
	unsigned int handle,
	void **instance)
{ 
	pthread_mutex_lock (&handleDatabase->mutex);

	if (handle > handleDatabase->handleCount) {
		return (SA_ERR_BAD_HANDLE);
	}
	if (handleDatabase->handles[handle].state != SA_HANDLE_STATE_ACTIVE) {
		return (SA_ERR_BAD_HANDLE);
	}

	*instance = handleDatabase->handles[handle].instance;

	handleDatabase->handles[handle].refCount += 1;

	pthread_mutex_unlock (&handleDatabase->mutex);

	return (SA_OK);
}


SaErrorT
saHandleInstancePut (
	struct saHandleDatabase *handleDatabase,
	unsigned int handle)
{
	pthread_mutex_lock (&handleDatabase->mutex);
	void *instance;

	handleDatabase->handles[handle].refCount -= 1;
	assert (handleDatabase->handles[handle].refCount >= 0);

	if (handleDatabase->handles[handle].refCount == 0) {
		instance = (handleDatabase->handles[handle].instance);
		handleDatabase->handleInstanceDestructor (instance);
		free (instance);
		memset (&handleDatabase->handles[handle], 0, sizeof (struct saHandle));
	}

	pthread_mutex_unlock (&handleDatabase->mutex);

	return (SA_OK);
}


SaErrorT
saVersionVerify (
        struct saVersionDatabase *versionDatabase,
	const SaVersionT *version)
{
	int found = 0;
	int i;

	if (version == 0) {
		return (SA_ERR_VERSION);
	}

	for (i = 0; i < versionDatabase->versionCount; i++) {
		if (memcmp (&versionDatabase->versionsSupported[i], version, sizeof (SaVersionT)) == 0) {
			found = 1;
			break;
		}
	}
	return (found ? SA_OK : SA_ERR_VERSION);
}


SaErrorT
saQueueInit (
	struct queue *queue,
	int queueItems,
	int bytesPerItem)
{
	queue->head = 0;
	queue->tail = queueItems - 1;
	queue->used = 0;
	queue->usedhw = 0;
	queue->size = queueItems;
	queue->bytesPerItem = bytesPerItem;
	queue->items = (void *)malloc (queueItems * bytesPerItem);
	if (queue->items == 0) {
		return (SA_ERR_NO_MEMORY);
	}
	memset (queue->items, 0, queueItems * bytesPerItem);
	return (SA_OK);
}

SaErrorT
saQueueIsFull (
	struct queue *queue,
	int *isFull)
{
	*isFull = ((queue->size - 1) == queue->used);
	return (SA_OK);
}


SaErrorT
saQueueIsEmpty (
	struct queue *queue,
	int *isEmpty)
{
	*isEmpty = (queue->used == 0);
	return (SA_OK);
}


SaErrorT
saQueueItemAdd (
	struct queue *queue,
	void *item)
{
	char *queueItem;
	int queuePosition;

	queuePosition = queue->head;
	queueItem = queue->items;
	queueItem += queuePosition * queue->bytesPerItem;
	memcpy (queueItem, item, queue->bytesPerItem);

	if (queue->tail == queue->head) {
		return (SA_ERR_LIBRARY);
	}
	queue->head = (queue->head + 1) % queue->size;
	queue->used++;
	if (queue->used > queue->usedhw) {
		queue->usedhw = queue->used;
	}
	return (SA_OK);
}

SaErrorT
saQueueItemGet (struct queue *queue, void **item)
{
	char *queueItem;
	int queuePosition;

	queuePosition = (queue->tail + 1) % queue->size;
	queueItem = queue->items;
	queueItem += queuePosition * queue->bytesPerItem;
	*item = (void *)queueItem;
	return (SA_OK);
}

SaErrorT
saQueueItemRemove (struct queue *queue)
{
	queue->tail = (queue->tail + 1) % queue->size;
	if (queue->tail == queue->head) {
		return (SA_ERR_LIBRARY);
	}
	queue->used--;
	return (SA_OK);
}
