/*
 * Copyright (c) 2006-2009 Red Hat, Inc.
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

#include <config.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <pthread.h>
#include <limits.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#if defined(HAVE_GETPEERUCRED)
#include <ucred.h>
#endif
#include <string.h>

#include <sys/shm.h>

#include <corosync/corotypes.h>
#include <corosync/list.h>

#include <corosync/coroipc_types.h>
#include <corosync/hdb.h>
#include <corosync/coroipcs.h>
#include <corosync/coroipc_ipc.h>

#define LOGSYS_UTILS_ONLY 1
#include <corosync/engine/logsys.h>

#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#else
#include <sys/sem.h>
#endif
#include "util.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define SERVER_BACKLOG 5

#define MSG_SEND_LOCKED		0
#define MSG_SEND_UNLOCKED	1

#define POLL_STATE_IN		1
#define POLL_STATE_INOUT	2

static struct coroipcs_init_state_v2 *api = NULL;

DECLARE_LIST_INIT (conn_info_list_head);

DECLARE_LIST_INIT (conn_info_exit_list_head);

struct outq_item {
	void *msg;
	size_t mlen;
	struct list_head list;
};

struct zcb_mapped {
	struct list_head list;
	void *addr;
	size_t size;
};


enum conn_state {
	CONN_STATE_THREAD_INACTIVE = 0,
	CONN_STATE_THREAD_ACTIVE = 1,
	CONN_STATE_THREAD_REQUEST_EXIT = 2,
	CONN_STATE_THREAD_DESTROYED = 3,
	CONN_STATE_LIB_EXIT_CALLED = 4,
	CONN_STATE_DISCONNECT_INACTIVE = 5
};

struct conn_info {
	int fd;
	pthread_t thread;
	pid_t client_pid;
	pthread_attr_t thread_attr;
	unsigned int service;
	enum conn_state state;
	int refcount;
	hdb_handle_t stats_handle;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	key_t semkey;
#endif
	unsigned int pending_semops;
	pthread_mutex_t pending_semops_mutex;
	pthread_mutex_t mutex;
	struct control_buffer *control_buffer;
	char *request_buffer;
	char *response_buffer;
	char *dispatch_buffer;
	size_t control_size;
	size_t request_size;
	size_t response_size;
	size_t dispatch_size;
	struct list_head outq_head;
	void *private_data;
	struct list_head list;
	char setup_msg[sizeof (mar_req_setup_t)];
	unsigned int setup_bytes_read;
	struct list_head zcb_mapped_list_head;
	char *sending_allowed_private_data[64];
	int poll_state;
};

static int shared_mem_dispatch_bytes_left (const struct conn_info *conn_info);

static void outq_flush (struct conn_info *conn_info);

static void outq_destroy (struct conn_info *conn_info);

static int priv_change (struct conn_info *conn_info);

static void ipc_disconnect (struct conn_info *conn_info);

static void msg_send (void *conn, const struct iovec *iov, unsigned int iov_len,
		      int locked);

static void _corosync_ipc_init(void);

#define log_printf(level, format, args...) \
do { \
	if (api->log_printf) \
        api->log_printf ( \
                LOGSYS_ENCODE_RECID(level, \
                                    api->log_subsys_id, \
                                    LOGSYS_RECID_LOG), \
                __FUNCTION__, __FILE__, __LINE__, \
                (const char *)format, ##args); \
	else \
        api->old_log_printf ((const char *)format, ##args); \
} while (0)

static hdb_handle_t dummy_stats_create_connection (
	const char *name,
	pid_t pid,
	int fd)
{
	return (0ULL);
}

static void dummy_stats_destroy_connection (
	hdb_handle_t handle)
{
}

static void dummy_stats_update_value (
	hdb_handle_t handle,
	const char *name,
	const void *value,
	size_t value_size)
{
}

static void dummy_stats_increment_value (
	hdb_handle_t handle,
	const char *name)
{
}

static int
memory_map (
	const char *path,
	size_t bytes,
	void **buf)
{
	int32_t fd;
	void *addr;
	int32_t res;

	fd = open (path, O_RDWR, 0600);

	unlink (path);

	if (fd == -1) {
		return (-1);
	}

	res = ftruncate (fd, bytes);
	if (res == -1) {
		goto error_close_unlink;
	}

	addr = mmap (NULL, bytes, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);

	if (addr == MAP_FAILED) {
		goto error_close_unlink;
	}
#if (defined COROSYNC_BSD && defined MADV_NOSYNC)
	madvise(addr, bytes, MADV_NOSYNC);
#endif

	res = close (fd);
	if (res) {
		return (-1);
	}
	*buf = addr;
	return (0);

error_close_unlink:
	close (fd);
	unlink(path);
	return -1;
}

static int
circular_memory_map (
	const char *path,
	size_t bytes,
	void **buf)
{
	int32_t fd;
	void *addr_orig;
	void *addr;
	int32_t res;

	fd = open (path, O_RDWR, 0600);

	unlink (path);

	if (fd == -1) {
		return (-1);
	}
	res = ftruncate (fd, bytes);
	if (res == -1) {
		goto error_close_unlink;
	}

	addr_orig = mmap (NULL, bytes << 1, PROT_NONE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (addr_orig == MAP_FAILED) {
		munmap(addr_orig, bytes);
		goto error_close_unlink;
	}

	addr = mmap (addr_orig, bytes, PROT_READ | PROT_WRITE,
		MAP_FIXED | MAP_SHARED, fd, 0);

	if (addr != addr_orig) {
		munmap(addr_orig, bytes);
		goto error_close_unlink;
	}
#if (defined COROSYNC_BSD && defined MADV_NOSYNC)
	madvise(addr_orig, bytes, MADV_NOSYNC);
#endif

	addr = mmap (((char *)addr_orig) + bytes,
                  bytes, PROT_READ | PROT_WRITE,
                  MAP_FIXED | MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		munmap(addr_orig, bytes);
		munmap(addr, bytes);
		goto error_close_unlink;
	}
#if (defined COROSYNC_BSD && defined MADV_NOSYNC)
	madvise(((char *)addr_orig) + bytes, bytes, MADV_NOSYNC);
#endif

	res = close (fd);
	if (res) {
		munmap(addr_orig, bytes);
		munmap(addr, bytes);
		return (-1);
	}
	*buf = addr_orig;
	return (0);

error_close_unlink:
	close (fd);
	unlink(path);
	return (-1);
}

static inline int
circular_memory_unmap (void *buf, size_t bytes)
{
	int res;

	res = munmap (buf, bytes << 1);

	return (res);
}

static int32_t flow_control_state_set (
	struct conn_info *conn_info,
	int flow_control_state)
{
	if (conn_info->control_buffer->flow_control_enabled == flow_control_state) {
		return 0;
	}
	if (flow_control_state == 0) {
		log_printf (LOGSYS_LEVEL_DEBUG,
			"Disabling flow control for %d\n",
			conn_info->client_pid);
	} else
	if (flow_control_state == 1) {
		log_printf (LOGSYS_LEVEL_DEBUG,
			"Enabling flow control for %d\n",
			conn_info->client_pid);
	}

	conn_info->control_buffer->flow_control_enabled = flow_control_state;
	return 1;
}

static void flow_control_stats_update (
	hdb_handle_t stats_handle,
	int flow_control_state)
{
	uint32_t fc_state = flow_control_state;
	api->stats_update_value (stats_handle, "flow_control",
				 &fc_state, sizeof(fc_state));
	api->stats_increment_value (stats_handle, "flow_control_count");
}

static inline int zcb_free (struct zcb_mapped *zcb_mapped)
{
	unsigned int res;

	res = munmap (zcb_mapped->addr, zcb_mapped->size);
	list_del (&zcb_mapped->list);
	free (zcb_mapped);
	return (res);
}

static inline int zcb_by_addr_free (struct conn_info *conn_info, void *addr)
{
	struct list_head *list;
	struct zcb_mapped *zcb_mapped;
	unsigned int res = 0;

	for (list = conn_info->zcb_mapped_list_head.next;
		list != &conn_info->zcb_mapped_list_head; list = list->next) {

		zcb_mapped = list_entry (list, struct zcb_mapped, list);

		if (zcb_mapped->addr == addr) {
			res = zcb_free (zcb_mapped);
			break;
		}

	}
	return (res);
}

static inline int zcb_all_free (
	struct conn_info *conn_info)
{
	struct list_head *list;
	struct zcb_mapped *zcb_mapped;

	for (list = conn_info->zcb_mapped_list_head.next;
		list != &conn_info->zcb_mapped_list_head;) {

		zcb_mapped = list_entry (list, struct zcb_mapped, list);

		list = list->next;

		zcb_free (zcb_mapped);
	}
	return (0);
}

static inline int zcb_alloc (
	struct conn_info *conn_info,
	const char *path_to_file,
	size_t size,
	void **addr)
{
	struct zcb_mapped *zcb_mapped;
	unsigned int res;

	zcb_mapped = malloc (sizeof (struct zcb_mapped));
	if (zcb_mapped == NULL) {
		return (-1);
	}

	res = memory_map (
		path_to_file,
		size,
		addr);
	if (res == -1) {
		free (zcb_mapped);
		return (-1);
	}

	list_init (&zcb_mapped->list);
	zcb_mapped->addr = *addr;
	zcb_mapped->size = size;
	list_add_tail (&zcb_mapped->list, &conn_info->zcb_mapped_list_head);
	return (0);
}

static int ipc_thread_active (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int retval = 0;

	pthread_mutex_lock (&conn_info->mutex);
	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		retval = 1;
	}
	pthread_mutex_unlock (&conn_info->mutex);
	return (retval);
}

static int ipc_thread_exiting (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int retval = 1;

	pthread_mutex_lock (&conn_info->mutex);
	if (conn_info->state == CONN_STATE_THREAD_INACTIVE) {
		retval = 0;
	} else
	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		retval = 0;
	}
	pthread_mutex_unlock (&conn_info->mutex);
	return (retval);
}

/*
 * returns 0 if should be called again, -1 if finished
 */
static inline int conn_info_destroy (struct conn_info *conn_info)
{
	unsigned int res;
	void *retval;

	list_del (&conn_info->list);
	list_init (&conn_info->list);
	list_add (&conn_info->list, &conn_info_exit_list_head);

	if (conn_info->state == CONN_STATE_THREAD_REQUEST_EXIT) {
		res = pthread_join (conn_info->thread, &retval);
		conn_info->state = CONN_STATE_THREAD_DESTROYED;
		return (0);
	}

	if (conn_info->state == CONN_STATE_THREAD_INACTIVE ||
		conn_info->state == CONN_STATE_DISCONNECT_INACTIVE) {
		list_del (&conn_info->list);
		close (conn_info->fd);
		api->free (conn_info);
		return (-1);
	}

	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		ipc_sem_post (conn_info->control_buffer, SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT);
		return (0);
	}

	/*
	 * Retry library exit function if busy
	 */
	if (conn_info->state == CONN_STATE_THREAD_DESTROYED) {
		api->serialize_lock ();
		res = api->exit_fn_get (conn_info->service) (conn_info);
		api->serialize_unlock ();
		api->stats_destroy_connection (conn_info->stats_handle);
		if (res == -1) {
			return (0);
		} else {
			conn_info->state = CONN_STATE_LIB_EXIT_CALLED;
		}
	}

	pthread_mutex_lock (&conn_info->mutex);
	if (conn_info->refcount > 0) {
		pthread_mutex_unlock (&conn_info->mutex);
		return (0);
	}
	list_del (&conn_info->list);
	pthread_mutex_unlock (&conn_info->mutex);

	/*
	 * Let library know, that connection is now closed
	 */
	conn_info->control_buffer->ipc_closed = 1;
	ipc_sem_post (conn_info->control_buffer, SEMAPHORE_RESPONSE);
	ipc_sem_post (conn_info->control_buffer, SEMAPHORE_DISPATCH);

#if _POSIX_THREAD_PROCESS_SHARED > 0
	sem_destroy (&conn_info->control_buffer->sem_request_or_flush_or_exit);
	sem_destroy (&conn_info->control_buffer->sem_request);
	sem_destroy (&conn_info->control_buffer->sem_response);
	sem_destroy (&conn_info->control_buffer->sem_dispatch);
#else
	semctl (conn_info->control_buffer->semid, 0, IPC_RMID);
#endif
	/*
	 * Destroy shared memory segment and semaphore
	 */
	res = munmap ((void *)conn_info->control_buffer, conn_info->control_size);
	res = munmap ((void *)conn_info->request_buffer, conn_info->request_size);
	res = munmap ((void *)conn_info->response_buffer, conn_info->response_size);

	/*
	 * Free allocated data needed to retry exiting library IPC connection
	 */
	if (conn_info->private_data) {
		api->free (conn_info->private_data);
	}

	/*
	 * Free outq list
	 */
	outq_destroy(conn_info);

	close (conn_info->fd);
	res = circular_memory_unmap (conn_info->dispatch_buffer, conn_info->dispatch_size);
	zcb_all_free (conn_info);
	api->free (conn_info);
	return (-1);
}

union u {
	uint64_t server_addr;
	void *server_ptr;
};

static uint64_t void2serveraddr (void *server_ptr)
{
	union u u;

	u.server_ptr = server_ptr;
	return (u.server_addr);
}

static void *serveraddr2void (uint64_t server_addr)
{
	union u u;

	u.server_addr = server_addr;
	return (u.server_ptr);
};

static inline void zerocopy_operations_process (
	struct conn_info *conn_info,
	coroipc_request_header_t **header_out,
	unsigned int *new_message)
{
	coroipc_request_header_t *header;

	header = (coroipc_request_header_t *)conn_info->request_buffer;
	if (header->id == ZC_ALLOC_HEADER) {
		mar_req_coroipcc_zc_alloc_t *hdr = (mar_req_coroipcc_zc_alloc_t *)header;
		coroipc_response_header_t res_header;
		void *addr = NULL;
		struct coroipcs_zc_header *zc_header;
		unsigned int res;

		res = zcb_alloc (conn_info, hdr->path_to_file, hdr->map_size,
			&addr);

		zc_header = (struct coroipcs_zc_header *)addr;
		zc_header->server_address = void2serveraddr(addr);

		res_header.size = sizeof (coroipc_response_header_t);
		res_header.id = 0;
		coroipcs_response_send (
			conn_info, &res_header,
			res_header.size);
		*new_message = 0;
		return;
	} else
	if (header->id == ZC_FREE_HEADER) {
		mar_req_coroipcc_zc_free_t *hdr = (mar_req_coroipcc_zc_free_t *)header;
		coroipc_response_header_t res_header;
		void *addr = NULL;

		addr = serveraddr2void (hdr->server_address);

		zcb_by_addr_free (conn_info, addr);

		res_header.size = sizeof (coroipc_response_header_t);
		res_header.id = 0;
		coroipcs_response_send (
			conn_info, &res_header,
			res_header.size);

		*new_message = 0;
		return;
	} else
	if (header->id == ZC_EXECUTE_HEADER) {
		mar_req_coroipcc_zc_execute_t *hdr = (mar_req_coroipcc_zc_execute_t *)header;

		header = (coroipc_request_header_t *)(((char *)serveraddr2void(hdr->server_address) + sizeof (struct coroipcs_zc_header)));
	}
	*header_out = header;
	*new_message = 1;
}

static void *pthread_ipc_consumer (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int res;
	coroipc_request_header_t *header;
	coroipc_response_header_t coroipc_response_header;
	int send_ok;
	unsigned int new_message;
	int sem_value = 0;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && defined(HAVE_SCHED_GET_PRIORITY_MAX)
	if (api->sched_policy != 0) {
		res = pthread_setschedparam (conn_info->thread,
			api->sched_policy, api->sched_param);
	}
#endif

	for (;;) {
		ipc_sem_wait (conn_info->control_buffer, SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT, IPC_SEMWAIT_NOFILE);
		if (ipc_thread_active (conn_info) == 0) {
			coroipcs_refcount_dec (conn_info);
			pthread_exit (0);
		}

		outq_flush (conn_info);

		ipc_sem_getvalue (conn_info->control_buffer, SEMAPHORE_REQUEST, &sem_value);
		if (sem_value > 0) {
		
			res = ipc_sem_wait (conn_info->control_buffer, SEMAPHORE_REQUEST, IPC_SEMWAIT_NOFILE);
		} else {
			continue;
		}
	
		zerocopy_operations_process (conn_info, &header, &new_message);
		/*
		 * There is no new message to process, continue for loop
		 */
		if (new_message == 0) {
			continue;
		}

		coroipcs_refcount_inc (conn);

		api->serialize_lock();

		send_ok = api->sending_allowed (conn_info->service,
			header->id,
			header,
			conn_info->sending_allowed_private_data);

		/*
		 * This happens when the message contains some kind of invalid
		 * parameter, such as an invalid size
		 */
		if (send_ok == -1) {
			api->stats_increment_value (conn_info->stats_handle, "invalid_request");
			coroipc_response_header.size = sizeof (coroipc_response_header_t);
			coroipc_response_header.id = 0;
			coroipc_response_header.error = CS_ERR_INVALID_PARAM;
			coroipcs_response_send (conn_info,
				&coroipc_response_header,
				sizeof (coroipc_response_header_t));
		} else 
		if (send_ok) {
			api->stats_increment_value (conn_info->stats_handle, "requests");
			api->handler_fn_get (conn_info->service, header->id) (conn_info, header);
		} else {
			/*
			 * Overload, tell library to retry
			 */
			api->stats_increment_value (conn_info->stats_handle, "overload");
			coroipc_response_header.size = sizeof (coroipc_response_header_t);
			coroipc_response_header.id = 0;
			coroipc_response_header.error = CS_ERR_TRY_AGAIN;
			coroipcs_response_send (conn_info,
				&coroipc_response_header,
				sizeof (coroipc_response_header_t));
		}

		api->sending_allowed_release (conn_info->sending_allowed_private_data);

		api->serialize_unlock();

		coroipcs_refcount_dec (conn);
	}
	pthread_exit (0);
}

static int
req_setup_send (
	struct conn_info *conn_info,
	int error)
{
	mar_res_setup_t res_setup;
	unsigned int res;

	memset (&res_setup, 0, sizeof (res_setup));
	res_setup.error = error;

retry_send:
	res = send (conn_info->fd, &res_setup, sizeof (mar_res_setup_t), MSG_WAITALL|MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		api->stats_increment_value (conn_info->stats_handle, "send_retry_count");
		goto retry_send;
	} else
	if (res == -1 && errno == EAGAIN) {
		api->stats_increment_value (conn_info->stats_handle, "send_retry_count");
		goto retry_send;
	}
	return (0);
}

static cs_error_t
req_setup_recv (
	struct conn_info *conn_info)
{
	int res;
	struct msghdr msg_recv;
	struct iovec iov_recv;
	cs_error_t auth_res = CS_ERR_LIBRARY;

#ifdef COROSYNC_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE (sizeof (struct ucred))];
	int off = 0;
	int on = 1;
	struct ucred cred;
#endif
	msg_recv.msg_flags = 0;
	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#ifdef COROSYNC_LINUX
	msg_recv.msg_control = (void *)cmsg_cred;
	msg_recv.msg_controllen = sizeof (cmsg_cred);
#endif
#ifdef COROSYNC_SOLARIS
	msg_recv.msg_accrights = 0;
	msg_recv.msg_accrightslen = 0;
#endif /* COROSYNC_SOLARIS */

	iov_recv.iov_base = &conn_info->setup_msg[conn_info->setup_bytes_read];
	iov_recv.iov_len = sizeof (mar_req_setup_t) - conn_info->setup_bytes_read;
#ifdef COROSYNC_LINUX
	res = setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"Can't set SO_PASSCRED socket option for IPC connection.\n");
		ipc_disconnect (conn_info);
		return (CS_ERR_LIBRARY);
	}
#endif

retry_recv:
	res = recvmsg (conn_info->fd, &msg_recv, MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		api->stats_increment_value (conn_info->stats_handle, "recv_retry_count");
		goto retry_recv;
	} else
	if (res == -1 && errno != EAGAIN) {
		return (CS_ERR_LIBRARY);
	} else
	if (res == 0) {
#if defined(COROSYNC_SOLARIS) || defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		ipc_disconnect (conn_info);
		return (CS_ERR_LIBRARY);
#else
		return (CS_ERR_SECURITY);
#endif
	}
	conn_info->setup_bytes_read += res;

/*
 * currently support getpeerucred, getpeereid, and SO_PASSCRED credential
 * retrieval mechanisms for various Platforms
 */
#ifdef HAVE_GETPEERUCRED
/*
 * Solaris and some BSD systems
 */
	{
		ucred_t *uc = NULL;
		uid_t euid = -1;
		gid_t egid = -1;

		if (getpeerucred (conn_info->fd, &uc) == 0) {
			euid = ucred_geteuid (uc);
			egid = ucred_getegid (uc);
			conn_info->client_pid = ucred_getpid (uc);
			if (api->security_valid (euid, egid)) {
				auth_res = CS_OK;
			} else {
				auth_res = hdb_error_to_cs(errno);
			}
			ucred_free(uc);
		}
	}
#elif HAVE_GETPEEREID
/*
 * Usually MacOSX systems
 */

	{
		uid_t euid;
		gid_t egid;

		/*
		 * TODO get the peer's pid.
		 * conn_info->client_pid = ?;
		 */
		euid = -1;
		egid = -1;
		if (getpeereid (conn_info->fd, &euid, &egid) == 0) {
			if (api->security_valid (euid, egid)) {
				auth_res = CS_OK;
			} else {
				auth_res = hdb_error_to_cs(errno);
			}
		}
	}

#elif SO_PASSCRED
/*
 * Usually Linux systems
 */
	for (cmsg = CMSG_FIRSTHDR(&msg_recv); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg_recv, cmsg)) {
		if (cmsg->cmsg_type != SCM_CREDENTIALS)
			continue;

		memcpy(&cred, CMSG_DATA(cmsg), sizeof(struct ucred));

		log_printf(LOGSYS_LEVEL_DEBUG, "IPC credentials for client pid %u: uid %u. gid %u\n",
		    cred.pid, cred.uid, cred.gid);

		break;
       }

	if (cmsg != NULL) {
		conn_info->client_pid = cred.pid;
		if (api->security_valid (cred.uid, cred.gid)) {
			auth_res = CS_OK;
		} else {
			auth_res = hdb_error_to_cs(errno);
		}
	} else {
		/*
		 * We were unable to find valid credentials
		 */
		log_printf(LOGSYS_LEVEL_ERROR, "Platform should support credentials, but no credentials found!\n");
	}

#else /* no credentials */
	auth_res = CS_OK;
	log_printf (LOGSYS_LEVEL_ERROR, "Platform does not support IPC authentication.  Using no authentication\n");
#endif /* no credentials */

	if (auth_res != CS_OK) {
		ipc_disconnect (conn_info);
		if (auth_res == CS_ERR_NO_RESOURCES) {
			log_printf (LOGSYS_LEVEL_ERROR,
				"Not enough file desciptors for IPC connection.\n");
		} else {
			log_printf (LOGSYS_LEVEL_ERROR, "Invalid IPC credentials.\n");
		}
		return auth_res;
	}

	if (conn_info->setup_bytes_read == sizeof (mar_req_setup_t)) {
#ifdef COROSYNC_LINUX
		res = setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED,
			&off, sizeof (off));
		if (res == -1) {
			log_printf (LOGSYS_LEVEL_ERROR,
				"Can't set SO_PASSCRED socket option for IPC connection.\n");
			ipc_disconnect (conn_info);
			return (CS_ERR_LIBRARY);
		}
#endif
		return (CS_OK);
	}
	return (CS_ERR_LIBRARY);
}

static void ipc_disconnect (struct conn_info *conn_info)
{
	if (conn_info->state == CONN_STATE_THREAD_INACTIVE) {
		conn_info->state = CONN_STATE_DISCONNECT_INACTIVE;
		return;
	}
	if (conn_info->state != CONN_STATE_THREAD_ACTIVE) {
		return;
	}
	pthread_mutex_lock (&conn_info->mutex);
	conn_info->state = CONN_STATE_THREAD_REQUEST_EXIT;
	pthread_mutex_unlock (&conn_info->mutex);

	ipc_sem_post (conn_info->control_buffer, SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT);
}

static int conn_info_create (int fd)
{
	struct conn_info *conn_info;

	conn_info = api->malloc (sizeof (struct conn_info));
	if (conn_info == NULL) {
		return (-1);
	}
	memset (conn_info, 0, sizeof (struct conn_info));

	conn_info->fd = fd;
	conn_info->client_pid = 0;
	conn_info->service = SOCKET_SERVICE_INIT;
	conn_info->state = CONN_STATE_THREAD_INACTIVE;
	conn_info->poll_state = POLL_STATE_IN;
	list_init (&conn_info->outq_head);
	list_init (&conn_info->list);
	list_init (&conn_info->zcb_mapped_list_head);
	list_add (&conn_info->list, &conn_info_list_head);

	api->poll_dispatch_add (fd, conn_info);

	return (0);
}

#if defined(COROSYNC_LINUX) || defined(COROSYNC_SOLARIS)
/* SUN_LEN is broken for abstract namespace
 */
#define COROSYNC_SUN_LEN(a) sizeof(*(a))
#else
#define COROSYNC_SUN_LEN(a) SUN_LEN(a)
#endif


/*
 * Exported functions
 */
extern void coroipcs_ipc_init_v2 (
	struct coroipcs_init_state_v2 *init_state_v2)
{
	api = init_state_v2;
	api->old_log_printf	= NULL;

	log_printf (LOGSYS_LEVEL_DEBUG, "you are using ipc api v2\n");
	_corosync_ipc_init ();
}

extern void coroipcs_ipc_init (
        struct coroipcs_init_state *init_state)
{
	api = calloc (sizeof(struct coroipcs_init_state_v2), 1);
	/* v2 api */
	api->stats_create_connection	= dummy_stats_create_connection;
	api->stats_destroy_connection	= dummy_stats_destroy_connection;
	api->stats_update_value		= dummy_stats_update_value;
	api->stats_increment_value	= dummy_stats_increment_value;
	api->log_printf			= NULL;

	/* v1 api */
	api->socket_name		= init_state->socket_name;
	api->sched_policy		= init_state->sched_policy;
	api->sched_param		= init_state->sched_param;
	api->malloc			= init_state->malloc;
	api->free			= init_state->free;
	api->old_log_printf		= init_state->log_printf;
	api->fatal_error		= init_state->fatal_error;
	api->security_valid		= init_state->security_valid;
	api->service_available		= init_state->service_available;
	api->private_data_size_get	= init_state->private_data_size_get;
	api->serialize_lock		= init_state->serialize_lock;
	api->serialize_unlock		= init_state->serialize_unlock;
	api->sending_allowed		= init_state->sending_allowed;
	api->sending_allowed_release	= init_state->sending_allowed_release;
	api->poll_accept_add		= init_state->poll_accept_add;
	api->poll_dispatch_add		= init_state->poll_dispatch_add;
	api->poll_dispatch_modify	= init_state->poll_dispatch_modify;
	api->init_fn_get		= init_state->init_fn_get;
	api->exit_fn_get		= init_state->exit_fn_get;
	api->handler_fn_get		= init_state->handler_fn_get;

	log_printf (LOGSYS_LEVEL_DEBUG, "you are using ipc api v1\n");

	_corosync_ipc_init ();
}

static void _corosync_ipc_init(void)
{
	int server_fd;
	struct sockaddr_un un_addr;
	int res;
#if SO_PASSCRED
	int on = 1;
#endif

	/*
	 * Create socket for IPC clients, name socket, listen for connections
	 */
#if defined(COROSYNC_SOLARIS)
	server_fd = socket (PF_UNIX, SOCK_STREAM, 0);
#else
	server_fd = socket (PF_LOCAL, SOCK_STREAM, 0);
#endif
	if (server_fd == -1) {
		log_printf (LOGSYS_LEVEL_CRIT, "Cannot create client connections socket.\n");
		api->fatal_error ("Can't create library listen socket");
	}

	res = fcntl (server_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, LOGSYS_LEVEL_CRIT,
			"Could not set non-blocking operation on server socket");
		api->fatal_error ("Could not set non-blocking operation on server socket");
	}

	memset (&un_addr, 0, sizeof (struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
	un_addr.sun_len = SUN_LEN(&un_addr);
#endif

#if defined(COROSYNC_LINUX)
	sprintf (un_addr.sun_path + 1, "%s", api->socket_name);
#else
	{
		struct stat stat_out;
		res = stat (SOCKETDIR, &stat_out);
		if (res == -1 || (res == 0 && !S_ISDIR(stat_out.st_mode))) {
			log_printf (LOGSYS_LEVEL_CRIT, "Required directory not present %s\n", SOCKETDIR);
			api->fatal_error ("Please create required directory.");
		}
		sprintf (un_addr.sun_path, "%s/%s", SOCKETDIR, api->socket_name);
		unlink (un_addr.sun_path);
	}
#endif

	res = bind (server_fd, (struct sockaddr *)&un_addr, COROSYNC_SUN_LEN(&un_addr));
	if (res) {
		LOGSYS_PERROR (errno, LOGSYS_LEVEL_CRIT,
				"Could not bind AF_UNIX (%s)", un_addr.sun_path);
		api->fatal_error ("Could not bind to AF_UNIX socket\n");
	}

	/*
	 * Allow eveyrone to write to the socket since the IPC layer handles
	 * security automatically
	 */
#if !defined(COROSYNC_LINUX)
	res = chmod (un_addr.sun_path, S_IRWXU|S_IRWXG|S_IRWXO);
#endif
#if SO_PASSCRED
	res = setsockopt(server_fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
	if (res == -1) {
		LOGSYS_PERROR (errno, LOGSYS_LEVEL_CRIT, "Could not set SO_PASSCRED to AF_UNIX_SOCKET");
		api->fatal_error ("Could not set SO_PASSCRED to AF_UNIX_SOCKET\n");
	}
#endif
	listen (server_fd, SERVER_BACKLOG);

	/*
	 * Setup connection dispatch routine
	 */
	api->poll_accept_add (server_fd);
}

void coroipcs_ipc_exit (void)
{
	struct list_head *list;
	struct conn_info *conn_info;
	unsigned int res;

	for (list = conn_info_list_head.next; list != &conn_info_list_head;
		list = list->next) {

		conn_info = list_entry (list, struct conn_info, list);

		if (conn_info->state != CONN_STATE_THREAD_ACTIVE)
			continue;

		ipc_disconnect (conn_info);

#if _POSIX_THREAD_PROCESS_SHARED > 0
		sem_destroy (&conn_info->control_buffer->sem_request_or_flush_or_exit);
		sem_destroy (&conn_info->control_buffer->sem_request);
		sem_destroy (&conn_info->control_buffer->sem_response);
		sem_destroy (&conn_info->control_buffer->sem_dispatch);
#else
		semctl (conn_info->control_buffer->semid, 0, IPC_RMID);
#endif

		/*
		 * Unmap memory segments
		 */
		res = munmap ((void *)conn_info->control_buffer,
			conn_info->control_size);
		res = munmap ((void *)conn_info->request_buffer,
			conn_info->request_size);
		res = munmap ((void *)conn_info->response_buffer,
			conn_info->response_size);
		res = circular_memory_unmap (conn_info->dispatch_buffer,
			conn_info->dispatch_size);
	}
}

int coroipcs_ipc_service_exit (unsigned int service)
{
	struct list_head *list, *list_next;
	struct conn_info *conn_info;

	for (list = conn_info_list_head.next; list != &conn_info_list_head;
		list = list_next) {

		list_next = list->next;

		conn_info = list_entry (list, struct conn_info, list);

		if (conn_info->service != service ||
		    (conn_info->state != CONN_STATE_THREAD_ACTIVE && conn_info->state != CONN_STATE_THREAD_REQUEST_EXIT)) {
			continue;
		}

		ipc_disconnect (conn_info);
		api->poll_dispatch_destroy (conn_info->fd, NULL);
		while (conn_info_destroy (conn_info) != -1)
			;

		/*
		 * We will return to prevent token loss. Schedwrk will call us again.
		 */
		return (-1);
	}

	/*
	 * No conn info left in active list. We will traverse thru exit list. If there is any
	 * conn_info->service == service, we will wait to proper end -> return -1
	 */

	for (list = conn_info_exit_list_head.next; list != &conn_info_exit_list_head; list = list->next) {
		conn_info = list_entry (list, struct conn_info, list);

		if (conn_info->service == service) {
			return (-1);
		}
	}

	return (0);
}

/*
 * Get the conn info private data
 */
void *coroipcs_private_data_get (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	return (conn_info->private_data);
}

int coroipcs_response_send (void *conn, const void *msg, size_t mlen)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	memcpy (conn_info->response_buffer, msg, mlen);

	ipc_sem_post (conn_info->control_buffer, SEMAPHORE_RESPONSE);

	api->stats_increment_value (conn_info->stats_handle, "responses");
	return (0);
}

int coroipcs_response_iov_send (void *conn, const struct iovec *iov, unsigned int iov_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int write_idx = 0;
	int i;

	for (i = 0; i < iov_len; i++) {
		memcpy (&conn_info->response_buffer[write_idx],
			iov[i].iov_base, iov[i].iov_len);
		write_idx += iov[i].iov_len;
	}

	ipc_sem_post (conn_info->control_buffer, SEMAPHORE_RESPONSE);

	api->stats_increment_value (conn_info->stats_handle, "responses");
	return (0);
}

static int shared_mem_dispatch_bytes_left (const struct conn_info *conn_info)
{
	unsigned int n_read;
	unsigned int n_write;
	unsigned int bytes_left;

	n_read = conn_info->control_buffer->read;
	n_write = conn_info->control_buffer->write;

	if (n_read <= n_write) {
		bytes_left = conn_info->dispatch_size - n_write + n_read;
	} else {
		bytes_left = n_read - n_write;
	}

	/*
	 * Pointers in ring buffer are 64-bit alignment (in memcpy_dwrap)
	 * To ensure we will not overwrite previous data,
	 * 9 bytes (64-bit + 1 byte) are subtracted from bytes_left
	 */
	if (bytes_left < 9) {
		bytes_left = 0;
	} else {
		bytes_left = bytes_left - 9;
	}

	return (bytes_left);
}

static void memcpy_dwrap (struct conn_info *conn_info, void *msg, unsigned int len)
{
	unsigned int write_idx;

	write_idx = conn_info->control_buffer->write;

	memcpy (&conn_info->dispatch_buffer[write_idx], msg, len);
	conn_info->control_buffer->write = ((write_idx + len + 7) & 0xFFFFFFF8) % conn_info->dispatch_size;
}

static void msg_send (void *conn, const struct iovec *iov, unsigned int iov_len,
		      int locked)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int res;
	int i;
	char buf;

	for (i = 0; i < iov_len; i++) {
		memcpy_dwrap (conn_info, iov[i].iov_base, iov[i].iov_len);
	}

	buf = list_empty (&conn_info->outq_head);
	res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
	if (res != 1) {
		pthread_mutex_lock(&conn_info->pending_semops_mutex);
		conn_info->pending_semops += 1;
		if (conn_info->poll_state == POLL_STATE_IN) {
			conn_info->poll_state = POLL_STATE_INOUT;
			api->poll_dispatch_modify (conn_info->fd,
				POLLIN|POLLOUT|POLLNVAL);
		}
		pthread_mutex_unlock(&conn_info->pending_semops_mutex);
	}

	ipc_sem_post (conn_info->control_buffer, SEMAPHORE_DISPATCH);
}

static void outq_destroy (struct conn_info *conn_info) {
	struct list_head *list, *list_next;
	struct outq_item *outq_item;

	for (list = conn_info->outq_head.next;
		list != &conn_info->outq_head; list = list_next) {

		list_next = list->next;
		outq_item = list_entry (list, struct outq_item, list);
		list_del (list);
		api->free (outq_item->msg);
		api->free (outq_item);
	}

	list_init (&conn_info->outq_head);
}

static void outq_flush (struct conn_info *conn_info) {
	struct list_head *list, *list_next;
	struct outq_item *outq_item;
	unsigned int bytes_left;
	struct iovec iov;
	int32_t q_size_dec = 0;
	int32_t i;
	int32_t fc_set;

	pthread_mutex_lock (&conn_info->mutex);
	if (list_empty (&conn_info->outq_head)) {
		fc_set = flow_control_state_set (conn_info, 0);
		pthread_mutex_unlock (&conn_info->mutex);
		if (fc_set) {
			flow_control_stats_update (conn_info->stats_handle, 0);
		}
		return;
	}
	for (list = conn_info->outq_head.next;
		list != &conn_info->outq_head; list = list_next) {

		list_next = list->next;
		outq_item = list_entry (list, struct outq_item, list);
		bytes_left = shared_mem_dispatch_bytes_left (conn_info);
		if (bytes_left > outq_item->mlen) {
			iov.iov_base = outq_item->msg;
			iov.iov_len = outq_item->mlen;
			msg_send (conn_info, &iov, 1, MSG_SEND_UNLOCKED);
			list_del (list);
			api->free (iov.iov_base);
			api->free (outq_item);
			q_size_dec++;
		} else {
			break;
		}
	}
	pthread_mutex_unlock (&conn_info->mutex);

	/*
	 * these need to be sent out of the conn_info->mutex
	 */
	for (i = 0; i < q_size_dec; i++) {
		api->stats_decrement_value (conn_info->stats_handle, "queue_size");
		api->stats_increment_value (conn_info->stats_handle, "dispatched");
	}
}

static int priv_change (struct conn_info *conn_info)
{
	mar_req_priv_change req_priv_change;
	unsigned int res;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	union semun semun;
	struct semid_ds ipc_set;
	int i;
#endif

retry_recv:
	res = recv (conn_info->fd, &req_priv_change,
		sizeof (mar_req_priv_change),
		MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		api->stats_increment_value (conn_info->stats_handle, "recv_retry_count");
		goto retry_recv;
	}
	if (res == -1 && errno == EAGAIN) {
		api->stats_increment_value (conn_info->stats_handle, "recv_retry_count");
		goto retry_recv;
	}
	if (res == -1 && errno != EAGAIN) {
		return (-1);
	}
#if defined(COROSYNC_SOLARIS) || defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
	/* Error on socket, EOF is detected when recv return 0
	 */
	if (res == 0) {
		return (-1);
	}
#endif

#if _POSIX_THREAD_PROCESS_SHARED < 1
	ipc_set.sem_perm.uid = req_priv_change.euid;
	ipc_set.sem_perm.gid = req_priv_change.egid;
	ipc_set.sem_perm.mode = 0600;

	semun.buf = &ipc_set;

	for (i = 0; i < 3; i++) {
		res = semctl (conn_info->control_buffer->semid, 0, IPC_SET, semun);
		if (res == -1) {
			return (-1);
		}
	}
#endif
	return (0);
}

static void msg_send_or_queue (void *conn, const struct iovec *iov, unsigned int iov_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	unsigned int bytes_left;
	unsigned int bytes_msg = 0;
	int i;
	struct outq_item *outq_item;
	char *write_buf = 0;

	/*
	 * Exit transmission if the connection is dead
	 */
	if (ipc_thread_active (conn) == 0) {
		return;
	}

	bytes_left = shared_mem_dispatch_bytes_left (conn_info);
	for (i = 0; i < iov_len; i++) {
		bytes_msg += iov[i].iov_len;
	}
	if (bytes_left < bytes_msg || list_empty (&conn_info->outq_head) == 0) {
		if (flow_control_state_set (conn_info, 1)) {
			flow_control_stats_update(conn_info->stats_handle, 1);
		}
		outq_item = api->malloc (sizeof (struct outq_item));
		if (outq_item == NULL) {
			ipc_disconnect (conn);
			return;
		}
		outq_item->msg = api->malloc (bytes_msg);
		if (outq_item->msg == 0) {
			api->free (outq_item);
			ipc_disconnect (conn);
			return;
		}

		write_buf = outq_item->msg;
		for (i = 0; i < iov_len; i++) {
			memcpy (write_buf, iov[i].iov_base, iov[i].iov_len);
			write_buf += iov[i].iov_len;
		}
		outq_item->mlen = bytes_msg;
		list_init (&outq_item->list);
		pthread_mutex_lock (&conn_info->mutex);
		list_add_tail (&outq_item->list, &conn_info->outq_head);
		pthread_mutex_unlock (&conn_info->mutex);
		api->stats_increment_value (conn_info->stats_handle, "queue_size");
		return;
	}
	msg_send (conn, iov, iov_len, MSG_SEND_LOCKED);
	api->stats_increment_value (conn_info->stats_handle, "dispatched");
}

void coroipcs_refcount_inc (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock (&conn_info->mutex);
	conn_info->refcount++;
	pthread_mutex_unlock (&conn_info->mutex);
}

void coroipcs_refcount_dec (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock (&conn_info->mutex);
	conn_info->refcount--;
	pthread_mutex_unlock (&conn_info->mutex);
}

int coroipcs_dispatch_send (void *conn, const void *msg, size_t mlen)
{
	struct iovec iov;

	iov.iov_base = (void *)msg;
	iov.iov_len = mlen;

	msg_send_or_queue (conn, &iov, 1);
	return (0);
}

int coroipcs_dispatch_iov_send (void *conn, const struct iovec *iov, unsigned int iov_len)
{
	msg_send_or_queue (conn, iov, iov_len);
	return (0);
}

int coroipcs_handler_accept (
	int fd,
	int revent,
	void *data)
{
	socklen_t addrlen;
	struct sockaddr_un un_addr;
	int new_fd;
#ifdef COROSYNC_LINUX
	int on = 1;
#endif
	int res;

	addrlen = sizeof (struct sockaddr_un);

retry_accept:
	new_fd = accept (fd, (struct sockaddr *)&un_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1) {
		LOGSYS_PERROR (errno, LOGSYS_LEVEL_ERROR,
			"Could not accept Library connection");
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = fcntl (new_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, LOGSYS_LEVEL_ERROR,
			"Could not set non-blocking operation on library connection");
		close (new_fd);
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	/*
	 * Valid accept
	 */

	/*
	 * Request credentials of sender provided by kernel
	 */
#ifdef COROSYNC_LINUX
	res = setsockopt(new_fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"Can't set SO_PASSCRED socket option for IPC connection.\n");
		close(new_fd);
		return (0);
	}
#endif

	res = conn_info_create (new_fd);
	if (res != 0) {
		close (new_fd);
	}

	return (0);
}

static char * pid_to_name (pid_t pid, char *out_name, size_t name_len)
{
	char *name;
	char *rest;
	FILE *fp;
	char fname[32];
	char buf[256];

	snprintf (fname, 32, "/proc/%d/stat", pid);
	fp = fopen (fname, "r");
	if (!fp) {
		return NULL;
	}

	if (fgets (buf, sizeof (buf), fp) == NULL) {
		fclose (fp);
		return NULL;
	}
	fclose (fp);

	name = strrchr (buf, '(');
	if (!name) {
		return NULL;
	}

	/* move past the bracket */
	name++;

	rest = strrchr (buf, ')');

	if (rest == NULL || rest[1] != ' ') {
		return NULL;
	}

	*rest = '\0';
	/* move past the NULL and space */
	rest += 2;

	/* copy the name */
	strncpy (out_name, name, name_len);
	out_name[name_len - 1] = '\0';
	return out_name;
}

static void coroipcs_init_conn_stats (
	struct conn_info *conn)
{
	char conn_name[CS_MAX_NAME_LENGTH];
	char proc_name[CS_MAX_NAME_LENGTH];
	char int_str[4];

	if (conn->client_pid > 0) {
		if (pid_to_name (conn->client_pid, proc_name, sizeof(proc_name))) {
			snprintf (conn_name, sizeof(conn_name),
				"%s:%s:%d:%d", proc_name,
				short_service_name_get(conn->service, int_str, 4),
				conn->client_pid, conn->fd);
		} else {
			snprintf (conn_name, sizeof(conn_name),
				"proc:%s:%d:%d",
				short_service_name_get(conn->service, int_str, 4),
				conn->client_pid,
				conn->fd);
		}
	} else {
		snprintf (conn_name, sizeof(conn_name),
			"proc:%s:pid:%d",
			short_service_name_get(conn->service, int_str, 4),
			conn->fd);
	}
	conn->stats_handle = api->stats_create_connection (conn_name, conn->client_pid, conn->fd);
	api->stats_update_value (conn->stats_handle, "service_id",
		&conn->service, sizeof(conn->service));
}

int coroipcs_handler_dispatch (
	int fd,
	int revent,
	void *context)
{
	mar_req_setup_t *req_setup;
	struct conn_info *conn_info = (struct conn_info *)context;
	int res;
	char buf = 0;


	if (ipc_thread_exiting (conn_info)) {
		return conn_info_destroy (conn_info);
	}

	/*
	 * If an error occurs, request exit
	 */
	if (revent & (POLLERR|POLLHUP|POLLNVAL)) {
		ipc_disconnect (conn_info);
		return (0);
	}

	/*
	 * Read the header and process it
	 */
	if (conn_info->service == SOCKET_SERVICE_INIT && (revent & POLLIN)) {
		/*
		 * Receive in a nonblocking fashion the request
		 * IF security invalid, send ERR_SECURITY, otherwise
		 * send OK
		 */
		res = req_setup_recv (conn_info);
		if (res != CS_OK && res != CS_ERR_LIBRARY) {
			req_setup_send (conn_info, res);
		}
		if (res != CS_OK) {
			return (0);
		}

		pthread_mutex_init (&conn_info->mutex, NULL);
		pthread_mutex_init (&conn_info->pending_semops_mutex, NULL);
		req_setup = (mar_req_setup_t *)conn_info->setup_msg;
		/*
		 * Is the service registered ?
		 * Has service init function ?
		 */
		if (api->service_available (req_setup->service) == 0 ||
		    api->init_fn_get (req_setup->service) == NULL) {
			req_setup_send (conn_info, CS_ERR_NOT_EXIST);
			ipc_disconnect (conn_info);
			return (0);
		}
#if _POSIX_THREAD_PROCESS_SHARED < 1
		conn_info->semkey = req_setup->semkey;
#endif
		res = memory_map (
			req_setup->control_file,
			req_setup->control_size,
			(void *)&conn_info->control_buffer);
		if (res == -1) {
			goto send_setup_response;
		}
		conn_info->control_size = req_setup->control_size;

		res = memory_map (
			req_setup->request_file,
			req_setup->request_size,
			(void *)&conn_info->request_buffer);
		if (res == -1) {
			goto send_setup_response;
		}
		conn_info->request_size = req_setup->request_size;

		res = memory_map (
			req_setup->response_file,
			req_setup->response_size,
			(void *)&conn_info->response_buffer);
		if (res == -1) {
			goto send_setup_response;
		}
		conn_info->response_size = req_setup->response_size;

		res = circular_memory_map (
			req_setup->dispatch_file,
			req_setup->dispatch_size,
			(void *)&conn_info->dispatch_buffer);
		if (res == -1) {
			goto send_setup_response;
		}
		conn_info->dispatch_size = req_setup->dispatch_size;

 send_setup_response:
		if (res == 0) {
			req_setup_send (conn_info, CS_OK);
		} else {
			req_setup_send (conn_info, CS_ERR_LIBRARY);
			ipc_disconnect (conn_info);
			return (0);
		}

		conn_info->service = req_setup->service;
		conn_info->refcount = 0;
		conn_info->setup_bytes_read = 0;

#if _POSIX_THREAD_PROCESS_SHARED < 1
		conn_info->control_buffer->semid = semget (conn_info->semkey, 3, 0600);
#endif
		conn_info->pending_semops = 0;

		/*
		 * ipc thread is the only reference at startup
		 */
		conn_info->refcount = 1;
		conn_info->state = CONN_STATE_THREAD_ACTIVE;

		conn_info->private_data = api->malloc (api->private_data_size_get (conn_info->service));
		memset (conn_info->private_data, 0,
			api->private_data_size_get (conn_info->service));

		api->init_fn_get (conn_info->service) (conn_info);

		/* create stats objects */
		coroipcs_init_conn_stats (conn_info);

		pthread_attr_init (&conn_info->thread_attr);
		/*
		* IA64 needs more stack space than other arches
		*/
		#if defined(__ia64__)
		pthread_attr_setstacksize (&conn_info->thread_attr, 400000);
		#else
		pthread_attr_setstacksize (&conn_info->thread_attr, 200000);
		#endif

		pthread_attr_setdetachstate (&conn_info->thread_attr, PTHREAD_CREATE_JOINABLE);
		res = pthread_create (&conn_info->thread,
			&conn_info->thread_attr,
			pthread_ipc_consumer,
			conn_info);

		/*
		 * Security check - disallow multiple configurations of
		 * the ipc connection
		 */
		if (conn_info->service == SOCKET_SERVICE_INIT) {
			conn_info->service = SOCKET_SERVICE_SECURITY_VIOLATION;
		}
	} else
	if (revent & POLLIN) {
		coroipcs_refcount_inc (conn_info);
		res = recv (fd, &buf, 1, MSG_NOSIGNAL);
		if (res == 1) {
			switch (buf) {
			case MESSAGE_REQ_CHANGE_EUID:
				if (priv_change (conn_info) == -1) {
					ipc_disconnect (conn_info);
				}
				break;
			default:
				res = 0;
				break;
			}
		}
#if defined(COROSYNC_SOLARIS) || defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		if (res == 0) {
			ipc_disconnect (conn_info);
			coroipcs_refcount_dec (conn_info);
			return (0);
		}
#endif
		coroipcs_refcount_dec (conn_info);
	}

	if (revent & POLLOUT) {
		pthread_mutex_lock(&conn_info->pending_semops_mutex);

		assert(conn_info->pending_semops != 0);

		while (conn_info->pending_semops > 0 &&
			((res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL)) == 1)) {

			conn_info->pending_semops -= 1;
		}

		if (conn_info->pending_semops == 0 && conn_info->poll_state == POLL_STATE_INOUT) {
			conn_info->poll_state = POLL_STATE_IN;
			api->poll_dispatch_modify (conn_info->fd, POLLIN|POLLNVAL);
		}
		pthread_mutex_unlock(&conn_info->pending_semops_mutex);
	}

	return (0);
}

