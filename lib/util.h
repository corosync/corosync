
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

#ifndef AIS_UTIL_H_DEFINED
#define AIS_UTIL_H_DEFINED

#include <pthread.h>
#include <sys/poll.h>
#include "../include/ais_msg.h"

struct saHandleDatabase {
	unsigned int handleCount;
	struct saHandle *handles;
	pthread_mutex_t mutex;
	void (*handleInstanceDestructor) (void *);
};


struct saVersionDatabase {
	int versionCount;
	SaVersionT *versionsSupported;
};

struct queue {
	int head;
	int tail;
	int used;
	int usedhw;
	int size;
	void *items;
	int bytesPerItem;
};

SaErrorT
saServiceConnect (
	int *fdOut,
	enum req_init_types init_type);

SaErrorT
saRecvRetry (
	int s,
	void *msg,
	size_t len,
	int flags);

SaErrorT
saRecvQueue (
	int s,
	void *msg,
	struct queue *queue,
	int findMessageId);

SaErrorT
saActivatePoll (int s);

SaErrorT
saSendRetry (
	int s,
	const void *msg,
	size_t len,
	int flags);

SaErrorT saSendMsgRetry (
	int s,
	struct iovec *iov,
	int iov_len);

SaErrorT
saSelectRetry (
	int s,
	fd_set *readfds,
	fd_set *writefds,
	fd_set *exceptfds,
	struct timeval *timeout);

SaErrorT
saPollRetry (
	struct pollfd *ufds,
	unsigned int nfds,
	int timeout);

SaErrorT
saHandleCreate (
	struct saHandleDatabase *handleDatabase,
	int instanceSize,
	unsigned int *handleOut);

SaErrorT
saHandleDestroy (
	struct saHandleDatabase *handleDatabase,
	unsigned int handle);

SaErrorT
saHandleInstanceGet (
	struct saHandleDatabase *handleDatabase,
	unsigned int handle,
	void **instance);

SaErrorT
saHandleInstancePut (
	struct saHandleDatabase *handleDatabase,
	unsigned int handle);

SaErrorT
saVersionVerify (
	struct saVersionDatabase *versionDatabase,
	const SaVersionT *version);

SaErrorT
saQueueInit (
	struct queue *queue,
	int queueItems,
	int sizePerItem);

SaErrorT
saQueueIsFull (
	struct queue *queue,
	int *isFull);

SaErrorT
saQueueIsEmpty (
	struct queue *queue,
	int *isEmpty);

SaErrorT
saQueueItemAdd (
	struct queue *queue,
	void *item);

SaErrorT
saQueueItemGet (struct queue *queue, void **item);

SaErrorT
saQueueItemRemove (struct queue *queue);

#define offset_of(type,member) (int)(&(((type *)0)->member))

SaTimeT
clustTimeNow(void);

#endif /* AIS_UTIL_H_DEFINED */
