/*
 * Copyright (c) 2009 Red Hat, Inc.
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
#ifndef COROIPC_IPC_H_DEFINED
#define COROIPC_IPC_H_DEFINED

#include <unistd.h>
#include <poll.h>
#include <time.h>
#include "corotypes.h"
#include "config.h"

/*
 * Darwin claims to support process shared synchronization
 * but it really does not.  The unistd.h header file is wrong.
 */
#if defined(COROSYNC_DARWIN) || defined(__UCLIBC__)
#undef _POSIX_THREAD_PROCESS_SHARED
#define _POSIX_THREAD_PROCESS_SHARED -1
#endif

#ifndef _POSIX_THREAD_PROCESS_SHARED
#define _POSIX_THREAD_PROCESS_SHARED -1
#endif

#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#else
#include <sys/sem.h>

#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
  int val;
  struct semid_ds *buf;
  unsigned short int *array;
  struct seminfo *__buf;
};
#endif
#endif

/*
 * Define sem_wait timeout (real timeout will be (n-1;n) )
 */
#define IPC_SEMWAIT_TIMEOUT 2

#define IPC_SEMWAIT_NOFILE 0

enum req_init_types {
	MESSAGE_REQ_RESPONSE_INIT = 0,
	MESSAGE_REQ_DISPATCH_INIT = 1
};

#define MESSAGE_REQ_CHANGE_EUID		1

enum ipc_semaphore_identifiers {
	SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT 	= 0,
	SEMAPHORE_REQUEST			= 1,
	SEMAPHORE_RESPONSE			= 2,
	SEMAPHORE_DISPATCH			= 3
};

struct control_buffer {
	unsigned int read;
	unsigned int write;
	int flow_control_enabled;
#if _POSIX_THREAD_PROCESS_SHARED > 0
	sem_t sem_request_or_flush_or_exit;
	sem_t sem_response;
	sem_t sem_dispatch;
	sem_t sem_request;
#else
	int semid;
#endif
	int ipc_closed;
};

enum res_init_types {
	MESSAGE_RES_INIT
};

typedef struct {
	int service __attribute__((aligned(8)));
	unsigned long long semkey __attribute__((aligned(8)));
	char control_file[PATH_MAX] __attribute__((aligned(8)));
	char request_file[PATH_MAX] __attribute__((aligned(8)));
	char response_file[PATH_MAX] __attribute__((aligned(8)));
	char dispatch_file[PATH_MAX] __attribute__((aligned(8)));
	size_t control_size __attribute__((aligned(8)));
	size_t request_size __attribute__((aligned(8)));
	size_t response_size __attribute__((aligned(8)));
	size_t dispatch_size __attribute__((aligned(8)));
} mar_req_setup_t __attribute__((aligned(8)));

typedef struct {
	int error __attribute__((aligned(8)));
} mar_res_setup_t __attribute__((aligned(8)));

typedef struct {
        uid_t euid __attribute__((aligned(8)));
        gid_t egid __attribute__((aligned(8)));
} mar_req_priv_change __attribute__((aligned(8)));

typedef struct {
	coroipc_response_header_t header __attribute__((aligned(8)));
	uint64_t conn_info __attribute__((aligned(8)));
} mar_res_lib_response_init_t __attribute__((aligned(8)));

typedef struct {
	coroipc_response_header_t header __attribute__((aligned(8)));
} mar_res_lib_dispatch_init_t __attribute__((aligned(8)));

typedef struct {
	uint32_t nodeid __attribute__((aligned(8)));
	void *conn __attribute__((aligned(8)));
} mar_message_source_t __attribute__((aligned(8)));

typedef struct {
        coroipc_request_header_t header __attribute__((aligned(8)));
        size_t map_size __attribute__((aligned(8)));
        char path_to_file[128] __attribute__((aligned(8)));
} mar_req_coroipcc_zc_alloc_t __attribute__((aligned(8)));

typedef struct {
        coroipc_request_header_t header __attribute__((aligned(8)));
        size_t map_size __attribute__((aligned(8)));
	uint64_t server_address __attribute__((aligned(8)));
} mar_req_coroipcc_zc_free_t __attribute__((aligned(8)));

typedef struct {
        coroipc_request_header_t header __attribute__((aligned(8)));
	uint64_t server_address __attribute__((aligned(8)));
} mar_req_coroipcc_zc_execute_t __attribute__((aligned(8)));

struct coroipcs_zc_header {
	int map_size;
	uint64_t server_address;
};

#define SOCKET_SERVICE_INIT					0xFFFFFFFF
#define SOCKET_SERVICE_SECURITY_VIOLATION	0xFFFFFFFE

#define ZC_ALLOC_HEADER		0xFFFFFFFF
#define ZC_FREE_HEADER		0xFFFFFFFE
#define ZC_EXECUTE_HEADER	0xFFFFFFFD

static inline cs_error_t
ipc_sem_wait (
	struct control_buffer *control_buffer,
	enum ipc_semaphore_identifiers sem_id,
	int fd)
{
#if _POSIX_THREAD_PROCESS_SHARED < 1
	struct sembuf sop;
#else
	struct timespec timeout;
	struct pollfd pfd;
	sem_t *sem = NULL;
#endif
	int res;

#if _POSIX_THREAD_PROCESS_SHARED > 0
	switch (sem_id) {
	case SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT:
		sem = &control_buffer->sem_request_or_flush_or_exit;
		break;
	case SEMAPHORE_RESPONSE:
		sem = &control_buffer->sem_request;
		break;
	case SEMAPHORE_DISPATCH:
		sem = &control_buffer->sem_response;
		break;
	case SEMAPHORE_REQUEST:
		sem = &control_buffer->sem_dispatch;
		break;
	}

	if (fd == IPC_SEMWAIT_NOFILE) {
retry_sem_wait:
		res = sem_wait (sem);
		if (res == -1 && errno == EINTR) {
			goto retry_sem_wait;
		} else
		if (res == -1) {
			return (CS_ERR_LIBRARY);
		}
	} else {
		if (control_buffer->ipc_closed) {
			return (CS_ERR_LIBRARY);
		}

retry_sem_timedwait:
		timeout.tv_sec = time(NULL) + IPC_SEMWAIT_TIMEOUT;
		timeout.tv_nsec = 0;

		res = sem_timedwait (sem, &timeout);
		if (res == -1 && errno == ETIMEDOUT) {
			pfd.fd = fd;
			pfd.events = 0;

			/*
			 * Determine if server has failed (ERR_LIBRARY) or
			 * is just performing slowly or in configuration change
			 * (retry sem op)
			 */
			 
retry_poll:
			res = poll (&pfd, 1, 0);
			if (res == -1 && errno == EINTR) {
				goto retry_poll;
			} else
			if (res == -1) {
				return (CS_ERR_LIBRARY);
			}

			if (res == 1) {
				if (pfd.revents & (POLLERR|POLLHUP|POLLNVAL)) {

					return (CS_ERR_LIBRARY);
				}
			}
                	goto retry_sem_timedwait;
		} else
		if (res == -1 && errno == EINTR) {
			goto retry_sem_timedwait;
		} else
		if (res == -1) {
			return (CS_ERR_LIBRARY);
		}

		if (res == 0 && control_buffer->ipc_closed) {
			return (CS_ERR_LIBRARY);
		}
	}
#else
	sop.sem_num = sem_id;
	sop.sem_op = -1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (control_buffer->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		return (CS_ERR_TRY_AGAIN);
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		return (CS_ERR_TRY_AGAIN);
	} else
	if (res == -1) {
		return (CS_ERR_LIBRARY);
	}
#endif
	return (CS_OK);
}

static inline cs_error_t
ipc_sem_post (
	struct control_buffer *control_buffer,
	enum ipc_semaphore_identifiers sem_id)
{
#if _POSIX_THREAD_PROCESS_SHARED < 1
	struct sembuf sop;
#else
	sem_t *sem = NULL;
#endif
	int res;
	
#if _POSIX_THREAD_PROCESS_SHARED > 0
	switch (sem_id) {
	case SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT:
		sem = &control_buffer->sem_request_or_flush_or_exit;
		break;
	case SEMAPHORE_RESPONSE:
		sem = &control_buffer->sem_request;
		break;
	case SEMAPHORE_DISPATCH:
		sem = &control_buffer->sem_response;
		break;
	case SEMAPHORE_REQUEST:
		sem = &control_buffer->sem_dispatch;
		break;
	}

	res = sem_post (sem);
	if (res == -1) {
		return (CS_ERR_LIBRARY);
	}
#else
	sop.sem_num = sem_id;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (control_buffer->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1) {
		return (CS_ERR_LIBRARY);
	}
#endif
	return (CS_OK);
}

static inline cs_error_t
ipc_sem_getvalue (
	struct control_buffer *control_buffer,
	enum ipc_semaphore_identifiers sem_id,
	int *sem_value)
{
#if _POSIX_THREAD_PROCESS_SHARED < 1
	int sem_value_hold;
	union semun semun;
#else
	sem_t *sem = NULL;
	int res;
#endif
	
#if _POSIX_THREAD_PROCESS_SHARED > 0
	switch (sem_id) {
	case SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT:
		sem = &control_buffer->sem_request_or_flush_or_exit;
		break;
	case SEMAPHORE_RESPONSE:
		sem = &control_buffer->sem_request;
		break;
	case SEMAPHORE_DISPATCH:
		sem = &control_buffer->sem_response;
		break;
	case SEMAPHORE_REQUEST:
		sem = &control_buffer->sem_dispatch;
		break;
	}

	res = sem_getvalue (sem, sem_value);
	if (res == -1) {
		return (CS_ERR_LIBRARY);
	}
#else
retry_semctl:
	sem_value_hold = semctl (control_buffer->semid, sem_id, GETVAL, semun);
	if (sem_value_hold == -1 && errno == EINTR) {
		goto retry_semctl;
	} else
	if (sem_value_hold == -1) {
		return (CS_ERR_LIBRARY);
	}
	*sem_value = sem_value_hold;
#endif
	return (CS_OK);
}

#endif /* COROIPC_IPC_H_DEFINED */
