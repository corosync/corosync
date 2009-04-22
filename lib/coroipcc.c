/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/mman.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipc_ipc.h>
#include <corosync/coroipcc.h>

struct ipc_segment {
	int fd;
	int shmid;
	int semid;
	int flow_control_state;
	struct control_buffer *control_buffer;
	char *request_buffer;
	char *response_buffer;
	char *dispatch_buffer;
	size_t control_size;
	size_t request_size;
	size_t response_size;
	size_t dispatch_size;
	uid_t euid;
};


#if defined(COROSYNC_LINUX)
/* SUN_LEN is broken for abstract namespace
 */
#define AIS_SUN_LEN(a) sizeof(*(a))
#else
#define AIS_SUN_LEN(a) SUN_LEN(a)
#endif

#ifdef SO_NOSIGPIPE
void socket_nosigpipe(int s)
{
	int on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
}
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static int
coroipcc_send (
	int s,
	void *msg,
	size_t len)
{
	int result;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *rbuf = msg;
	int processed = 0;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_iovlen = 1;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

retry_send:
	iov_send.iov_base = &rbuf[processed];
	iov_send.iov_len = len - processed;

	result = sendmsg (s, &msg_send, MSG_NOSIGNAL);

	/*
	 * return immediately on any kind of syscall error that maps to
	 * CS_ERR if no part of message has been sent
	 */
	if (result == -1 && processed == 0) {
		if (errno == EINTR) {
			goto error_exit;
		}
		if (errno == EAGAIN) {
			goto error_exit;
		}
		if (errno == EFAULT) {
			goto error_exit;
		}
	}

	/*
	 * retry read operations that are already started except
	 * for fault in that case, return ERR_LIBRARY
	 */
	if (result == -1 && processed > 0) {
		if (errno == EINTR) {
			goto retry_send;
		}
		if (errno == EAGAIN) {
			goto retry_send;
		}
		if (errno == EFAULT) {
			goto error_exit;
		}
	}

	/*
	 * return ERR_LIBRARY on any other syscall error
	 */
	if (result == -1) {
		goto error_exit;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}

	return (0);

error_exit:
	return (-1);
}

static int
coroipcc_recv (
	int s,
	void *msg,
	size_t len)
{
	int error = 0;
	int result;
	struct msghdr msg_recv;
	struct iovec iov_recv;
	char *rbuf = msg;
	int processed = 0;

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;

retry_recv:
	iov_recv.iov_base = (void *)&rbuf[processed];
	iov_recv.iov_len = len - processed;

	result = recvmsg (s, &msg_recv, MSG_NOSIGNAL|MSG_WAITALL);
	if (result == -1 && errno == EINTR) {
		goto retry_recv;
	}
	if (result == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
#if defined(COROSYNC_SOLARIS) || defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
	/* On many OS poll never return POLLHUP or POLLERR.
	 * EOF is detected when recvmsg return 0.
	 */
	if (result == 0) {
		error = -1;
		goto error_exit;
	}
#endif
	if (result == -1 || result == 0) {
		error = -1;
		goto error_exit;
	}
	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	assert (processed == len);
error_exit:
	return (0);
}

static int
priv_change_send (struct ipc_segment *ipc_segment)
{
	char buf_req;
	mar_req_priv_change req_priv_change;
	unsigned int res;

	req_priv_change.euid = geteuid();
	/*
	 * Don't resend request unless euid has changed
	*/
	if (ipc_segment->euid == req_priv_change.euid) {
		return (0);
	}
	req_priv_change.egid = getegid();

	buf_req = MESSAGE_REQ_CHANGE_EUID;
	res = coroipcc_send (ipc_segment->fd, &buf_req, 1);
	if (res == -1) {
		return (-1);
	}

	res = coroipcc_send (ipc_segment->fd, &req_priv_change,
		sizeof (req_priv_change));
	if (res == -1) {
		return (-1);
	}

	ipc_segment->euid = req_priv_change.euid;
	return (0);
}

#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
        int val;
        struct semid_ds *buf;
        unsigned short int *array;
        struct seminfo *__buf;
};
#endif

static int
circular_memory_map (char *path, const char *file, void **buf, size_t bytes)
{
	int fd;
	void *addr_orig;
	void *addr;
	int res;

	sprintf (path, "/dev/shm/%s", file);

	fd = mkstemp (path);
	if (fd == -1) {
		sprintf (path, LOCALSTATEDIR "/run/%s", file);
		fd = mkstemp (path);
		if (fd == -1) {
			return (-1);
		}
	}

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

static void
memory_unmap (void *addr, size_t bytes)
{
	int res;

	res = munmap (addr, bytes);
}

static int
memory_map (char *path, const char *file, void **buf, size_t bytes)
{
	int fd;
	void *addr_orig;
	void *addr;
	int res;

	sprintf (path, "/dev/shm/%s", file);

	fd = mkstemp (path);
	if (fd == -1) {
		sprintf (path, LOCALSTATEDIR "/run/%s", file);
		fd = mkstemp (path);
		if (fd == -1) {
			return (-1);
		}
	}

	res = ftruncate (fd, bytes);

	addr_orig = mmap (NULL, bytes, PROT_NONE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (addr_orig == MAP_FAILED) {
		return (-1);
	}

	addr = mmap (addr_orig, bytes, PROT_READ | PROT_WRITE,
		MAP_FIXED | MAP_SHARED, fd, 0);

	if (addr != addr_orig) {
		return (-1);
	}

	res = close (fd);
	if (res) {
		return (-1);
	}
	*buf = addr_orig;
	return (0);
}

extern cs_error_t
coroipcc_service_connect (
	const char *socket_name,
	unsigned int service,
	size_t request_size,
	size_t response_size,
	size_t dispatch_size,
	void **ipc_context)

{
	int request_fd;
	struct sockaddr_un address;
	cs_error_t error;
	struct ipc_segment *ipc_segment;
	key_t semkey = 0;
	int res;
	mar_req_setup_t req_setup;
	mar_res_setup_t res_setup;
	union semun semun;
	char control_map_path[128];
	char request_map_path[128];
	char response_map_path[128];
	char dispatch_map_path[128];

	res_setup.error = CS_ERR_LIBRARY;

	request_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (request_fd == -1) {
		return (-1);
	}

	memset (&address, 0, sizeof (struct sockaddr_un));
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
	address.sun_len = sizeof(struct sockaddr_un);
#endif
	address.sun_family = PF_UNIX;

#if defined(COROSYNC_LINUX)
	sprintf (address.sun_path + 1, "%s", socket_name);
#else
	sprintf (address.sun_path, "%s/%s", SOCKETDIR, socket_name);
#endif
	res = connect (request_fd, (struct sockaddr *)&address,
		AIS_SUN_LEN(&address));
	if (res == -1) {
		close (request_fd);
		return (CS_ERR_TRY_AGAIN);
	}

	ipc_segment = malloc (sizeof (struct ipc_segment));
	if (ipc_segment == NULL) {
		close (request_fd);
		return (-1);
	}
	bzero (ipc_segment, sizeof (struct ipc_segment));

	/*
	 * Allocate a semaphore segment
	 */
	while (1) {
		semkey = random();
		ipc_segment->euid = geteuid ();
		if ((ipc_segment->semid
		     = semget (semkey, 3, IPC_CREAT|IPC_EXCL|0600)) != -1) {
		      break;
		}
		if (errno != EEXIST) {
			goto error_exit;
		}
	}

	semun.val = 0;
	res = semctl (ipc_segment->semid, 0, SETVAL, semun);
	if (res != 0) {
		goto error_exit;
	}

	res = semctl (ipc_segment->semid, 1, SETVAL, semun);
	if (res != 0) {
		goto error_exit;
	}

	res = memory_map (
		control_map_path,
		"control_buffer-XXXXXX",
		(void *)&ipc_segment->control_buffer,
		8192);

	res = memory_map (
		request_map_path,
		"request_buffer-XXXXXX",
		(void *)&ipc_segment->request_buffer,
		request_size);

	res = memory_map (
		response_map_path,
		"response_buffer-XXXXXX",
		(void *)&ipc_segment->response_buffer,
		response_size);

	res = circular_memory_map (
		dispatch_map_path,
		"dispatch_buffer-XXXXXX",
		(void *)&ipc_segment->dispatch_buffer,
		dispatch_size);

	/*
	 * Initialize IPC setup message
	 */
	req_setup.service = service;
	strcpy (req_setup.control_file, control_map_path);
	strcpy (req_setup.request_file, request_map_path);
	strcpy (req_setup.response_file, response_map_path);
	strcpy (req_setup.dispatch_file, dispatch_map_path);
	req_setup.control_size = 8192;
	req_setup.request_size = request_size;
	req_setup.response_size = response_size;
	req_setup.dispatch_size = dispatch_size;
	req_setup.semkey = semkey;

	error = coroipcc_send (request_fd, &req_setup, sizeof (mar_req_setup_t));
	if (error != 0) {
		goto error_exit;
	}
	error = coroipcc_recv (request_fd, &res_setup, sizeof (mar_res_setup_t));
	if (error != 0) {
		goto error_exit;
	}

	ipc_segment->fd = request_fd;
	ipc_segment->flow_control_state = 0;

	if (res_setup.error == CS_ERR_TRY_AGAIN) {
		goto error_exit;
	}

	ipc_segment->control_size = 8192;
	ipc_segment->request_size = request_size;
	ipc_segment->response_size = response_size;
	ipc_segment->dispatch_size = dispatch_size;

	*ipc_context = ipc_segment;
	return (res_setup.error);

error_exit:
	close (request_fd);
	if (ipc_segment->semid > 0)
		semctl (ipc_segment->semid, 0, IPC_RMID);
	return (res_setup.error);
}

cs_error_t
coroipcc_service_disconnect (
	void *ipc_context)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;

	shutdown (ipc_segment->fd, SHUT_RDWR);
	close (ipc_segment->fd);
	/*
	 * << 1 (or multiplied by 2) because this is a wrapped memory buffer
	 */
	memory_unmap (ipc_segment->control_buffer, ipc_segment->control_size);
	memory_unmap (ipc_segment->request_buffer, ipc_segment->request_size);
	memory_unmap (ipc_segment->response_buffer, ipc_segment->response_size);
	memory_unmap (ipc_segment->dispatch_buffer, (ipc_segment->dispatch_size) << 1);
	free (ipc_segment);
	return (CS_OK);
}

int
coroipcc_dispatch_flow_control_get (
        void *ipc_context)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;

	return (ipc_segment->flow_control_state);
}

int
coroipcc_fd_get (void *ipc_ctx)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_ctx;

	return (ipc_segment->fd);
}

int
coroipcc_dispatch_get (void *ipc_ctx, void **data, int timeout)
{
	struct pollfd ufds;
	int poll_events;
	char buf;
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_ctx;
	int res;
	char buf_two = 1;
	char *data_addr;

	ufds.fd = ipc_segment->fd;
	ufds.events = POLLIN;
	ufds.revents = 0;

retry_poll:
	poll_events = poll (&ufds, 1, timeout);
	if (poll_events == -1 && errno == EINTR) {
		goto retry_poll;
	} else
	if (poll_events == -1) {
		return (-1);
	} else
	if (poll_events == 0) {
		return (0);
	}
	if (poll_events == 1 && (ufds.revents & (POLLERR|POLLHUP))) {
		return (-1);
	}
retry_recv:
	res = recv (ipc_segment->fd, &buf, 1, 0);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	} else
	if (res == -1) {
		return (-1);
	}
	if (res == 0) {
		return (-1);
	}
	ipc_segment->flow_control_state = 0;
	if (buf == 1 || buf == 2) {
		ipc_segment->flow_control_state = 1;
	}
	/*
	 * Notify executive to flush any pending dispatch messages
	 */
	if (ipc_segment->flow_control_state) {
		buf_two = MESSAGE_REQ_OUTQ_FLUSH;
		res = coroipcc_send (ipc_segment->fd, &buf_two, 1);
		assert (res == 0); //TODO
	}
	/*
	 * This is just a notification of flow control starting at the addition
	 * of a new pending message, not a message to dispatch
	 */
	if (buf == 2) {
		return (0);
	}
	if (buf == 3) {
		return (0);
	}

	data_addr = ipc_segment->dispatch_buffer;

	data_addr = &data_addr[ipc_segment->control_buffer->read];

	*data = (void *)data_addr;
	return (1);
}

int
coroipcc_dispatch_put (void *ipc_ctx)
{
	struct sembuf sop;
	coroipc_response_header_t *header;
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_ctx;
	int res;
	char *addr;
	unsigned int read_idx;

	sop.sem_num = 2;
	sop.sem_op = -1;
	sop.sem_flg = 0;
retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (-1);
	}

	addr = ipc_segment->dispatch_buffer;

	read_idx = ipc_segment->control_buffer->read;
	header = (coroipc_response_header_t *) &addr[read_idx];
	ipc_segment->control_buffer->read =
		(read_idx + header->size) % ipc_segment->dispatch_size;
	return (0);
}

static cs_error_t
coroipcc_msg_send (
	void *ipc_context,
	const struct iovec *iov,
	unsigned int iov_len)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;
	struct sembuf sop;
	int i;
	int res;
	int req_buffer_idx = 0;

	for (i = 0; i < iov_len; i++) {
		memcpy (&ipc_segment->request_buffer[req_buffer_idx],
			iov[i].iov_base,
			iov[i].iov_len);
		req_buffer_idx += iov[i].iov_len;
	}
	/*
	 * Signal semaphore #0 indicting a new message from client
	 * to server request queue
	 */
	sop.sem_num = 0;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (CS_ERR_LIBRARY);
	}
	return (CS_OK);
}

static cs_error_t
coroipcc_reply_receive (
	void *ipc_context,
	void *res_msg, size_t res_len)
{
	struct sembuf sop;
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;
	int res;

	/*
	 * Wait for semaphore #1 indicating a new message from server
	 * to client in the response queue
	 */
	sop.sem_num = 1;
	sop.sem_op = -1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (CS_ERR_LIBRARY);
	}

	memcpy (res_msg, ipc_segment->response_buffer, res_len);
	return (CS_OK);
}

static cs_error_t
coroipcc_reply_receive_in_buf (
	void *ipc_context,
	void **res_msg)
{
	struct sembuf sop;
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;
	int res;

	/*
	 * Wait for semaphore #1 indicating a new message from server
	 * to client in the response queue
	 */
	sop.sem_num = 1;
	sop.sem_op = -1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (CS_ERR_LIBRARY);
	}

	*res_msg = (char *)ipc_segment->response_buffer;
	return (CS_OK);
}

cs_error_t
coroipcc_msg_send_reply_receive (
	void *ipc_context,
	const struct iovec *iov,
	unsigned int iov_len,
	void *res_msg,
	size_t res_len)
{
	cs_error_t res;

	res = coroipcc_msg_send (ipc_context, iov, iov_len);
	if (res != CS_OK) {
		return (res);
	}

	res = coroipcc_reply_receive (ipc_context, res_msg, res_len);
	if (res != CS_OK) {
		return (res);
	}

	return (CS_OK);
}

cs_error_t
coroipcc_msg_send_reply_receive_in_buf (
	void *ipc_context,
	const struct iovec *iov,
	unsigned int iov_len,
	void **res_msg)
{
	unsigned int res;

	res = coroipcc_msg_send (ipc_context, iov, iov_len);
	if (res != CS_OK) {
		return (res);
	}

	res = coroipcc_reply_receive_in_buf (ipc_context, res_msg);
	if (res != CS_OK) {
		return (res);
	}

	return (CS_OK);
}

cs_error_t
coroipcc_zcb_alloc (
	void *ipc_context,
	void **buffer,
	size_t size,
	size_t header_size)
{
	void *buf = NULL;
	char path[128];
	unsigned int res;
	mar_req_coroipcc_zc_alloc_t req_coroipcc_zc_alloc;
	coroipc_response_header_t res_coroipcs_zc_alloc;
	size_t map_size;
	struct iovec iovec;
	struct coroipcs_zc_header *hdr;

	map_size = size + header_size + sizeof (struct coroipcs_zc_header);
	res = memory_map (path, "cpg_zc-XXXXXX", &buf, size);
	assert (res != -1);

	req_coroipcc_zc_alloc.header.size = sizeof (mar_req_coroipcc_zc_alloc_t);
	req_coroipcc_zc_alloc.header.id = ZC_ALLOC_HEADER;
	req_coroipcc_zc_alloc.map_size = map_size;
	strcpy (req_coroipcc_zc_alloc.path_to_file, path);


	iovec.iov_base = &req_coroipcc_zc_alloc;
	iovec.iov_len = sizeof (mar_req_coroipcc_zc_alloc_t);

	res = coroipcc_msg_send_reply_receive (
		ipc_context,
		&iovec,
		1,
		&res_coroipcs_zc_alloc,
		sizeof (coroipc_response_header_t));

	hdr = (struct coroipcs_zc_header *)buf;
	hdr->map_size = map_size;
	*buffer = ((char *)buf) + sizeof (struct coroipcs_zc_header);
	return (CS_OK);
}

cs_error_t
coroipcc_zcb_free (
	void *ipc_context,
	void *buffer)
{
	mar_req_coroipcc_zc_free_t req_coroipcc_zc_free;
	coroipc_response_header_t res_coroipcs_zc_free;
	struct iovec iovec;
	unsigned int res;

	struct coroipcs_zc_header *header = (struct coroipcs_zc_header *)((char *)buffer - sizeof (struct coroipcs_zc_header));

	req_coroipcc_zc_free.header.size = sizeof (mar_req_coroipcc_zc_free_t);
	req_coroipcc_zc_free.header.id = ZC_FREE_HEADER;
	req_coroipcc_zc_free.map_size = header->map_size;
	req_coroipcc_zc_free.server_address = header->server_address;

	iovec.iov_base = &req_coroipcc_zc_free;
	iovec.iov_len = sizeof (mar_req_coroipcc_zc_free_t);

	res = coroipcc_msg_send_reply_receive (
		ipc_context,
		&iovec,
		1,
		&res_coroipcs_zc_free,
		sizeof (coroipc_response_header_t));

	munmap (header, header->map_size);

	return (CS_OK);
}

cs_error_t
coroipcc_zcb_msg_send_reply_receive (
        void *ipc_context,
        void *msg,
        void *res_msg,
        size_t res_len)
{
	mar_req_coroipcc_zc_execute_t req_coroipcc_zc_execute;
	struct coroipcs_zc_header *hdr;
	struct iovec iovec;
	cs_error_t res;

	hdr = (struct coroipcs_zc_header *)(((char *)msg) - sizeof (struct coroipcs_zc_header));

	req_coroipcc_zc_execute.header.size = sizeof (mar_req_coroipcc_zc_execute_t);
	req_coroipcc_zc_execute.header.id = ZC_EXECUTE_HEADER;
	req_coroipcc_zc_execute.server_address = hdr->server_address;

	iovec.iov_base = &req_coroipcc_zc_execute;
	iovec.iov_len = sizeof (mar_req_coroipcc_zc_execute_t);

	res = coroipcc_msg_send_reply_receive (
		ipc_context,
		&iovec,
		1,
		res_msg,
		res_len);

	return (res);
}
