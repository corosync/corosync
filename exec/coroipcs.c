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

#include <sys/shm.h>
#include <sys/sem.h>
#include <corosync/corotypes.h>
#include <corosync/list.h>

#include "coroipcs.h"
#include <corosync/ipc_gen.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define SERVER_BACKLOG 5

#define MSG_SEND_LOCKED		0
#define MSG_SEND_UNLOCKED	1

static struct coroipcs_init_state *api;

DECLARE_LIST_INIT (conn_info_list_head);

struct outq_item {
	void *msg;
	size_t mlen;
	struct list_head list;
};

#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};
#endif

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
	pthread_attr_t thread_attr;
	unsigned int service;
	enum conn_state state;
	int notify_flow_control_enabled;
	int refcount;
	key_t shmkey;
	key_t semkey;
	int shmid;
	int semid;
	unsigned int pending_semops;
	pthread_mutex_t mutex;
	struct shared_memory *mem;
	char *dispatch_buffer;
	struct list_head outq_head;
	void *private_data;
	struct list_head list;
	char setup_msg[sizeof (mar_req_setup_t)];
	unsigned int setup_bytes_read;
	char *sending_allowed_private_data[64];
};

static int shared_mem_dispatch_bytes_left (const struct conn_info *conn_info);

static void outq_flush (struct conn_info *conn_info);

static int priv_change (struct conn_info *conn_info);

static void ipc_disconnect (struct conn_info *conn_info);

static void msg_send (void *conn, const struct iovec *iov, unsigned int iov_len,
		      int locked);

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
		pthread_kill (conn_info->thread, SIGUSR1);
		return (0);
	}

	api->serialize_lock ();
	/*
	 * Retry library exit function if busy
	 */
	if (conn_info->state == CONN_STATE_THREAD_DESTROYED) {
		res = api->exit_fn_get (conn_info->service) (conn_info);
		if (res == -1) {
			api->serialize_unlock ();
			return (0);
		} else {
			conn_info->state = CONN_STATE_LIB_EXIT_CALLED;
		}
	}

	pthread_mutex_lock (&conn_info->mutex);
	if (conn_info->refcount > 0) {
		pthread_mutex_unlock (&conn_info->mutex);
		api->serialize_unlock ();
		return (0);
	}
	list_del (&conn_info->list);
	pthread_mutex_unlock (&conn_info->mutex);

	/*
	 * Destroy shared memory segment and semaphore
	 */
	shmdt (conn_info->mem);
	res = shmctl (conn_info->shmid, IPC_RMID, NULL);
	semctl (conn_info->semid, 0, IPC_RMID);

	/*
	 * Free allocated data needed to retry exiting library IPC connection
	 */
	if (conn_info->private_data) {
		api->free (conn_info->private_data);
	}
	close (conn_info->fd);
	munmap (conn_info->dispatch_buffer, (DISPATCH_SIZE));
	api->free (conn_info);
	api->serialize_unlock ();
	return (-1);
}

struct res_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char buf[4096];
};

static void *pthread_ipc_consumer (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	struct sembuf sop;
	int res;
	mar_req_header_t *header;
	struct res_overlay res_overlay;
	int send_ok;

	if (api->sched_priority != 0) {
		struct sched_param sched_param;

		sched_param.sched_priority = api->sched_priority;
		res = pthread_setschedparam (conn_info->thread, SCHED_RR, &sched_param);
	}

	for (;;) {
		sop.sem_num = 0;
		sop.sem_op = -1;
		sop.sem_flg = 0;
retry_semop:
		if (ipc_thread_active (conn_info) == 0) {
			coroipcs_refcount_dec (conn_info);
			pthread_exit (0);
		}
		res = semop (conn_info->semid, &sop, 1);
		if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
			goto retry_semop;
		} else
		if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
			coroipcs_refcount_dec (conn_info);
			pthread_exit (0);
		}

		coroipcs_refcount_inc (conn_info);

                header = (mar_req_header_t *)conn_info->mem->req_buffer;

		send_ok = api->sending_allowed (conn_info->service,
			header->id,
			header,	
			conn_info->sending_allowed_private_data);
	
		if (send_ok) {
			api->serialize_lock();
			api->handler_fn_get (conn_info->service, header->id) (conn_info, header);
			api->serialize_unlock();
		} else {
			/*
			 * Overload, tell library to retry
			 */
			res_overlay.header.size =
				api->response_size_get (conn_info->service, header->id);
			res_overlay.header.id =
				api->response_id_get (conn_info->service, header->id);
			res_overlay.header.error = CS_ERR_TRY_AGAIN;
			coroipcs_response_send (conn_info, &res_overlay, 
				res_overlay.header.size);
		}

		api->sending_allowed_release (conn_info->sending_allowed_private_data);
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

	res_setup.error = error;

retry_send:
	res = send (conn_info->fd, &res_setup, sizeof (mar_res_setup_t), MSG_WAITALL);
	if (res == -1 && errno == EINTR) {
		goto retry_send;
	} else
	if (res == -1 && errno == EAGAIN) {
		goto retry_send;
	}
	return (0);
}

static int
req_setup_recv (
	struct conn_info *conn_info)
{
	int res;
	struct msghdr msg_recv;
	struct iovec iov_recv;
#ifdef COROSYNC_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE (sizeof (struct ucred))];
	struct ucred *cred;
	int off = 0;
	int on = 1;
#endif

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#ifdef COROSYNC_LINUX
	msg_recv.msg_control = (void *)cmsg_cred;
	msg_recv.msg_controllen = sizeof (cmsg_cred);
#endif

#ifdef PORTABILITY_WORK_TODO
#ifdef COROSYNC_SOLARIS
	msg_recv.msg_flags = 0;
	uid_t euid;
	gid_t egid;
		                
	euid = -1;
	egid = -1;
	if (getpeereid(conn_info->fd, &euid, &egid) != -1 &&
	    (api->security_valid (euid, egid)) {
		if (conn_info->state == CONN_IO_STATE_INITIALIZING) {
			api->log_printf ("Invalid security authentication\n");
			return (-1);
		}
	}
	msg_recv.msg_accrights = 0;
	msg_recv.msg_accrightslen = 0;
#else /* COROSYNC_SOLARIS */

#ifdef HAVE_GETPEERUCRED
	ucred_t *uc;
	uid_t euid = -1;
	gid_t egid = -1;

	if (getpeerucred (conn_info->fd, &uc) == 0) {
		euid = ucred_geteuid (uc);
		egid = ucred_getegid (uc);
		if (api->security_valid (euid, egid) {
			conn_info->authenticated = 1;
		}
		ucred_free(uc);
	}
	if (conn_info->authenticated == 0) {
		api->log_printf ("Invalid security authentication\n");
 	}
#else /* HAVE_GETPEERUCRED */
 	api->log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated "
 		"because platform does not support "
 		"authentication with sockets, continuing "
 		"with a fake authentication\n");
#endif /* HAVE_GETPEERUCRED */
#endif /* COROSYNC_SOLARIS */

#endif

	iov_recv.iov_base = &conn_info->setup_msg[conn_info->setup_bytes_read];
	iov_recv.iov_len = sizeof (mar_req_setup_t) - conn_info->setup_bytes_read;
#ifdef COROSYNC_LINUX
	setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
#endif

retry_recv:
	res = recvmsg (conn_info->fd, &msg_recv, MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	} else
	if (res == -1 && errno != EAGAIN) {
		return (0);
	} else
	if (res == 0) {
#if defined(COROSYNC_SOLARIS) || defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		ipc_disconnect (conn_info);
#endif
		return (-1);
	}
	conn_info->setup_bytes_read += res;

#ifdef COROSYNC_LINUX

	cmsg = CMSG_FIRSTHDR (&msg_recv);
	assert (cmsg);
	cred = (struct ucred *)CMSG_DATA (cmsg);
	if (cred) {
		if (api->security_valid (cred->uid, cred->gid)) {
		} else {
			ipc_disconnect (conn_info);
			api->log_printf ("Invalid security authentication\n");
			return (-1);
		}
	}
#endif
	if (conn_info->setup_bytes_read == sizeof (mar_req_setup_t)) {
#ifdef COROSYNC_LINUX
		setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED,
			&off, sizeof (off));
#endif
		return (1);
	}
	return (0);
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

	pthread_kill (conn_info->thread, SIGUSR1);
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
	conn_info->service = SOCKET_SERVICE_INIT;
	conn_info->state = CONN_STATE_THREAD_INACTIVE;
	list_init (&conn_info->outq_head);
	list_init (&conn_info->list);
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
extern void coroipcs_ipc_init (
        struct coroipcs_init_state *init_state)
{
	int server_fd;
	struct sockaddr_un un_addr;
	int res;

	api = init_state;

	/*
	 * Create socket for IPC clients, name socket, listen for connections
	 */
	server_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (server_fd == -1) {
		api->log_printf ("Cannot create client connections socket.\n");
		api->fatal_error ("Can't create library listen socket");
	};

	res = fcntl (server_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		api->log_printf ("Could not set non-blocking operation on server socket: %s\n", strerror (errno));
		api->fatal_error ("Could not set non-blocking operation on server socket");
	}

	memset (&un_addr, 0, sizeof (struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
	un_addr.sun_len = sizeof(struct sockaddr_un);
#endif

#if defined(COROSYNC_LINUX)
	sprintf (un_addr.sun_path + 1, "%s", api->socket_name);
#else
	sprintf (un_addr.sun_path, "%s/%s", SOCKETDIR, api->socket_name);
	unlink (un_addr.sun_path);
#endif

	res = bind (server_fd, (struct sockaddr *)&un_addr, COROSYNC_SUN_LEN(&un_addr));
	if (res) {
		api->log_printf ("Could not bind AF_UNIX: %s.\n", strerror (errno));
		api->fatal_error ("Could not bind to AF_UNIX socket\n");
	}
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

	for (list = conn_info_list_head.next; list != &conn_info_list_head;
		list = list->next) {

		conn_info = list_entry (list, struct conn_info, list);

		shmdt (conn_info->mem);
		shmctl (conn_info->shmid, IPC_RMID, NULL);
		semctl (conn_info->semid, 0, IPC_RMID);
	
		pthread_kill (conn_info->thread, SIGUSR1);
	}
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
	struct sembuf sop;
	int res;

	memcpy (conn_info->mem->res_buffer, msg, mlen);
	sop.sem_num = 1;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		goto retry_semop;
	} else
	if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return (0);
	}
	return (0);
}

int coroipcs_response_iov_send (void *conn, const struct iovec *iov, unsigned int iov_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	struct sembuf sop;
	int res;
	int write_idx = 0;
	int i;

	for (i = 0; i < iov_len; i++) {
		memcpy (&conn_info->mem->res_buffer[write_idx], iov[i].iov_base, iov[i].iov_len);
		write_idx += iov[i].iov_len;
	}

	sop.sem_num = 1;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		goto retry_semop;
	} else
	if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return (0);
	}
	return (0);
}

static int shared_mem_dispatch_bytes_left (const struct conn_info *conn_info)
{
	unsigned int n_read;
	unsigned int n_write;
	unsigned int bytes_left;

	n_read = conn_info->mem->read;
	n_write = conn_info->mem->write;

	if (n_read <= n_write) {
		bytes_left = DISPATCH_SIZE - n_write + n_read;
	} else {
		bytes_left = n_read - n_write;
	}
	return (bytes_left);
}

static void memcpy_dwrap (struct conn_info *conn_info, void *msg, unsigned int len)
{
	unsigned int write_idx;

	write_idx = conn_info->mem->write;

	memcpy (&conn_info->dispatch_buffer[write_idx], msg, len);
	conn_info->mem->write = (write_idx + len) % (DISPATCH_SIZE);
}

static void msg_send (void *conn, const struct iovec *iov, unsigned int iov_len,
		      int locked)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	struct sembuf sop;
	int res;
	int i;
	char buf;

	for (i = 0; i < iov_len; i++) {
		memcpy_dwrap (conn_info, iov[i].iov_base, iov[i].iov_len);
	}

	buf = !list_empty (&conn_info->outq_head);
	res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
	if (res == -1 && errno == EAGAIN) {
		if (locked == 0) {
			pthread_mutex_lock (&conn_info->mutex);
		}
		conn_info->pending_semops += 1;
		if (locked == 0) {
			pthread_mutex_unlock (&conn_info->mutex);
		}
		api->poll_dispatch_modify (conn_info->fd,
			POLLIN|POLLOUT|POLLNVAL);
	} else
	if (res == -1) {
		ipc_disconnect (conn_info);
	}
	sop.sem_num = 2;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		goto retry_semop;
	} else
	if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return;
	}
}

static void outq_flush (struct conn_info *conn_info) {
	struct list_head *list, *list_next;
	struct outq_item *outq_item;
	unsigned int bytes_left;
	struct iovec iov;
	char buf;
	int res;

	pthread_mutex_lock (&conn_info->mutex);
	if (list_empty (&conn_info->outq_head)) {
		buf = 3;
		res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
		pthread_mutex_unlock (&conn_info->mutex);
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
		} else {
			break;
		}
	}
	pthread_mutex_unlock (&conn_info->mutex);
}

static int priv_change (struct conn_info *conn_info)
{
	mar_req_priv_change req_priv_change;
	unsigned int res;
	union semun semun;
	struct semid_ds ipc_set;
	int i;

retry_recv:
	res = recv (conn_info->fd, &req_priv_change,
		sizeof (mar_req_priv_change),
		MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	}
	if (res == -1 && errno == EAGAIN) {
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

	ipc_set.sem_perm.uid = req_priv_change.euid;
	ipc_set.sem_perm.gid = req_priv_change.egid;
	ipc_set.sem_perm.mode = 0600;

	semun.buf = &ipc_set;

	for (i = 0; i < 3; i++) {
		res = semctl (conn_info->semid, 0, IPC_SET, semun);
		if (res == -1) {
			return (-1);
		}
	}
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
		if (list_empty (&conn_info->outq_head)) {
			conn_info->notify_flow_control_enabled = 1;
			api->poll_dispatch_modify (conn_info->fd,
				POLLIN|POLLOUT|POLLNVAL);
		}
		list_add_tail (&outq_item->list, &conn_info->outq_head);
		pthread_mutex_unlock (&conn_info->mutex);
		return;
	}
	msg_send (conn, iov, iov_len, MSG_SEND_LOCKED);
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
		api->log_printf ("Could not accept Library connection: %s\n", strerror (errno));
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = fcntl (new_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		api->log_printf ("Could not set non-blocking operation on library connection: %s\n", strerror (errno));
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
	setsockopt(new_fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
#endif

	res = conn_info_create (new_fd);
	if (res != 0) {
		close (new_fd);
	}

	return (0);
}

static int
coroipcs_memory_map (char *path, void **buf, size_t bytes)
{
	int fd;
	void *addr_orig;
	void *addr;
	int res;
 
	fd = open (path, O_RDWR, 0600);

	unlink (path);

	res = ftruncate (fd, bytes);

	addr_orig = mmap (NULL, bytes << 1, PROT_NONE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
 
	if (addr_orig == MAP_FAILED) {
		return (-1);
	}
 
	addr = mmap (addr_orig, bytes, PROT_READ | PROT_WRITE,
		MAP_FIXED | MAP_SHARED, fd, 0);
 
	if (addr != addr_orig) {
		return (-1);
	}
 
	addr = mmap (((char *)addr_orig) + bytes,
                  bytes, PROT_READ | PROT_WRITE,
                  MAP_FIXED | MAP_SHARED, fd, 0);
 
	res = close (fd);
	if (res) {
		return (-1);
	}
	*buf = addr_orig;
	return (0);
}

int coroipcs_handler_dispatch (
	int fd,
	int revent,
	void *context)
{
	mar_req_setup_t *req_setup;
	struct conn_info *conn_info = (struct conn_info *)context;
	int res;
	char buf;


	if (ipc_thread_exiting (conn_info)) {
		return conn_info_destroy (conn_info);
	}

	/*
	 * If an error occurs, request exit
	 */
	if (revent & (POLLERR|POLLHUP)) {
		ipc_disconnect (conn_info);
		return (0);
	}

	/*
	 * Read the header and process it
	 */
	if (conn_info->service == SOCKET_SERVICE_INIT && (revent & POLLIN)) {
		/*
		 * Receive in a nonblocking fashion the request
		 * IF security invalid, send TRY_AGAIN, otherwise
		 * send OK
		 */
		res = req_setup_recv (conn_info);
		if (res == -1) {
			req_setup_send (conn_info, CS_ERR_TRY_AGAIN);
		}
		if (res != 1) {
			return (0);
		}
		req_setup_send (conn_info, CS_OK);

		pthread_mutex_init (&conn_info->mutex, NULL);
		req_setup = (mar_req_setup_t *)conn_info->setup_msg;
		/*
		 * Is the service registered ?
		 */
		if (api->service_available (req_setup->service) == 0) {
			ipc_disconnect (conn_info);
			return (0);
		}

		conn_info->shmkey = req_setup->shmkey;
		conn_info->semkey = req_setup->semkey;
		res = coroipcs_memory_map (
			req_setup->dispatch_file,
			(void *)&conn_info->dispatch_buffer,
			DISPATCH_SIZE);

		conn_info->service = req_setup->service;
		conn_info->refcount = 0;
		conn_info->notify_flow_control_enabled = 0;
		conn_info->setup_bytes_read = 0;

		conn_info->shmid = shmget (conn_info->shmkey,
			sizeof (struct shared_memory), 0600);
		conn_info->mem = shmat (conn_info->shmid, NULL, 0);
		conn_info->semid = semget (conn_info->semkey, 3, 0600);
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

		pthread_attr_init (&conn_info->thread_attr);
		/*
		* IA64 needs more stack space then other arches
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
			conn_info->service = -1;
		}
	} else
	if (revent & POLLIN) {
		coroipcs_refcount_inc (conn_info);
		res = recv (fd, &buf, 1, MSG_NOSIGNAL);
		if (res == 1) {
			switch (buf) {
			case MESSAGE_REQ_OUTQ_FLUSH:
				outq_flush (conn_info);
				break;
			case MESSAGE_REQ_CHANGE_EUID:
				if (priv_change (conn_info) == -1) {
					ipc_disconnect (conn_info);
				}
				break;
			default:
				res = 0;
				break;
			}
			coroipcs_refcount_dec (conn_info);
		}
#if defined(COROSYNC_SOLARIS) || defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		if (res == 0) {
			ipc_disconnect (conn_info);
			return (0);
		}
#endif
	}

	coroipcs_refcount_inc (conn_info);
	pthread_mutex_lock (&conn_info->mutex);
	if ((conn_info->state == CONN_STATE_THREAD_ACTIVE) && (revent & POLLOUT)) {
		buf = !list_empty (&conn_info->outq_head);
		for (; conn_info->pending_semops;) {
			res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
			if (res == 1) {
				conn_info->pending_semops--;
			} else {
				break;
			}
		}
		if (conn_info->notify_flow_control_enabled) {
			buf = 2;
			res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
			if (res == 1) {
				conn_info->notify_flow_control_enabled = 0;
			}
		}
		if (conn_info->notify_flow_control_enabled == 0 &&
			conn_info->pending_semops == 0) {

			api->poll_dispatch_modify (conn_info->fd,
				POLLIN|POLLNVAL);
		}
	}
	pthread_mutex_unlock (&conn_info->mutex);
	coroipcs_refcount_dec (conn_info);

	return (0);
}
