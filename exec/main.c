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
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
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

#include "../include/ais_types.h"
#include "../include/ais_msg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "poll.h"
#include "gmi.h"
#include "mempool.h"
#include "parse.h"
#include "main.h"
#include "handlers.h"
#include "clm.h"
#include "amf.h"
#include "ckpt.h"
#include "evt.h"
#include "print.h"

#define SERVER_BACKLOG 5

int ais_uid = 0;
int gid_valid = 0;

struct gmi_groupname aisexec_groupname = { "0123" };

/*
 * All service handlers in the AIS
 */
struct service_handler *ais_service_handlers[] = {
    &clm_service_handler,
    &amf_service_handler,
    &ckpt_service_handler,
    &ckpt_checkpoint_service_handler,
    &ckpt_sectioniterator_service_handler,
    &evt_service_handler
};

#define AIS_SERVICE_HANDLERS_COUNT 6
#define AIS_SERVICE_HANDLER_AISEXEC_FUNCTIONS_MAX 40

static int poll_handler_libais_deliver (poll_handle handle, int fd, int revent, void *data, unsigned int *prio);

enum e_ais_done {
	AIS_DONE_EXIT = -1,
	AIS_DONE_UID_DETERMINE = -2,
	AIS_DONE_GID_DETERMINE = -3,
	AIS_DONE_MEMPOOL_INIT = -4,
	AIS_DONE_FORK = -5,
	AIS_DONE_LIBAIS_SOCKET = -6,
	AIS_DONE_LIBAIS_BIND = -7,
	AIS_DONE_READKEY = -8,
	AIS_DONE_READNETWORK = -9,
	AIS_DONE_READGROUPS = -10,
};

static inline void ais_done (enum e_ais_done err)
{
	log_printf (LOG_LEVEL_ERROR, "AIS Executive exiting.\n");
	exit (1);
}

static inline struct conn_info *conn_info_create (int fd) {
	struct conn_info *conn_info;
	int res;

	conn_info = malloc (sizeof (struct conn_info));
	if (conn_info == 0) {
		return (0);
	}

	memset (conn_info, 0, sizeof (struct conn_info));
	res = queue_init (&conn_info->outq, SIZEQUEUE,
		sizeof (struct outq_item));
	if (res != 0) {
		free (conn_info);
		return (0);
	}
	conn_info->inb = malloc (sizeof (char) * SIZEINB);
	if (conn_info->inb == 0) {
		queue_free (&conn_info->outq);
		free (conn_info);
		return (0);
	}
	
	conn_info->state = CONN_STATE_ACTIVE;
	conn_info->fd = fd;
	conn_info->service = SOCKET_SERVICE_INIT;
	return (conn_info);
}


struct sockaddr_in this_ip;
#define LOCALHOST_IP inet_addr("127.0.0.1")

char *socketname = "libais.socket";

static int libais_connection_active (struct conn_info *conn_info)
{
	return (conn_info->state == CONN_STATE_ACTIVE);
}

static void libais_disconnect_delayed (struct conn_info *conn_info)
{
	conn_info->state = CONN_STATE_DISCONNECTING_DELAYED;
}

static int libais_disconnect (struct conn_info *conn_info)
{
	int res = 0;
	struct outq_item *outq_item;

	if (ais_service_handlers[conn_info->service - 1]->libais_exit_fn) {
		res = ais_service_handlers[conn_info->service - 1]->libais_exit_fn (conn_info);
	}

	/*
	 * Close the library connection and free its
	 * data if it hasn't already been freed
	 */
	if (conn_info->state != CONN_STATE_DISCONNECTING) {
		conn_info->state = CONN_STATE_DISCONNECTING;

		close (conn_info->fd);
		/*
		 * Free the outq queued items
		 */
		while (!queue_is_empty (&conn_info->outq)) {
			outq_item = queue_item_get (&conn_info->outq);
			mempool_free (outq_item->msg);
			queue_item_remove (&conn_info->outq);
		}

		queue_free (&conn_info->outq);
		free (conn_info->inb);
	}

	/*
	 * If exit_fn didn't request a retry,
	 * free the conn_info structure
	 */
	if (res != -1) {
		free (conn_info);
	}

	/*
	 * Inverse res from libais exit fn handler
	 */
	return (res != -1 ? -1 : 0);
}

extern int libais_send_response (struct conn_info *conn_info,
	void *msg, int mlen)
{
	struct queue *outq;
	char *cmsg;
	int res = 0;
	int queue_empty;
	struct outq_item *queue_item;
	struct outq_item queue_item_out;
	struct msghdr msg_send;
	struct iovec iov_send;

	if (!libais_connection_active (conn_info)) {
		return (-1);
	}
	outq = &conn_info->outq;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_iovlen = 1;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

	if (queue_is_full (outq)) {
		/*
		 * Start a disconnect if we have not already started one
		 * and report that the outgoing queue is full
		 */
		log_printf (LOG_LEVEL_ERROR, "Library queue is full, disconnecting library connection.\n");
		libais_disconnect_delayed (conn_info);
		return (-1);
	}
	while (!queue_is_empty (outq)) {
		queue_item = queue_item_get (outq);
		iov_send.iov_base = (void *)conn_info->byte_start;
		iov_send.iov_len = queue_item->mlen;

retry_sendmsg:
		res = sendmsg (conn_info->fd, &msg_send, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg;
		}
		if (res == -1 && errno == EAGAIN) {
			break; /* outgoing kernel queue full */
		}
		if (res == -1) {
			return (-1); /* message couldn't be sent */
		}

		/*
		 * Message sent, try sending another message
		 */
		queue_item_remove (outq);
		conn_info->byte_start = 0;
		mempool_free (queue_item->msg);
	} /* while queue not empty */

	queue_empty = queue_is_empty (outq);
	/*
	 * Send requested message
	 */
	if (queue_empty) {
		iov_send.iov_base = msg;
		iov_send.iov_len = mlen;
retry_sendmsg_two:
		res = sendmsg (conn_info->fd, &msg_send, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg_two;
		}
		if (res == -1 && errno != EAGAIN) {
			return (-1);
		}
	}

	/*
	 * If res == -1 , errrno == EAGAIN which means kernel queue full
	 */
	if (res == -1)  {
		cmsg = mempool_malloc (mlen);
		if (cmsg == 0) {
			log_printf (LOG_LEVEL_ERROR, "Library queue couldn't allocate a message, disconnecting library connection.\n");
			libais_disconnect_delayed (conn_info);
			return (-1);
		}
		queue_item_out.msg = cmsg;
		queue_item_out.mlen = mlen;
		memcpy (cmsg, msg, mlen);
		queue_item_add (outq, &queue_item_out);
	}
	return (0);
}
		
static int poll_handler_libais_accept (
	poll_handle handle,
	int fd,
	int revent,
	void *data,
	unsigned int *prio)
{
	socklen_t addrlen;
	struct conn_info *conn_info;
	struct sockaddr_un un_addr;
	int new_fd;
	int on = 1;

	addrlen = sizeof (struct sockaddr_un);

retry_accept:
	new_fd = accept (fd, (struct sockaddr *)&un_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: Could not accept Library connection: %s\n", strerror (errno));
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}
		
	/*
	 * Valid accept
	 */

	/*
	 * Request credentials of sender provided by kernel
	 */
	setsockopt(new_fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));

	log_printf (LOG_LEVEL_DEBUG, "connection received from libais client %d.\n", new_fd);

	conn_info = conn_info_create (new_fd);
	if (conn_info == 0) {
		close (new_fd);
		return (0); /* This is an error, but -1 would indicate disconnect from poll */
	}

	poll_dispatch_add (aisexec_poll_handle, new_fd, POLLIN|POLLNVAL, conn_info,
		poll_handler_libais_deliver, 0);

// TODO is this needed, or shouldn't it be in conn_info_create ?
	memcpy (&conn_info->ais_ci.un_addr, &un_addr, sizeof (struct sockaddr_un));
	return (0);
}

struct message_overlay {
	struct res_header header;
	char buf[4096];
};

static int poll_handler_libais_deliver (poll_handle handle, int fd, int revent, void *data, unsigned int *prio)
{
	int res;
	struct conn_info *conn_info = (struct conn_info *)data;
	struct req_header *header;
	int service;
	struct msghdr msg_recv;
	struct iovec iov_recv;
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE (sizeof (struct ucred))];
	struct ucred *cred;
	int on = 0;
	int send_ok = 0;
	struct message_overlay msg_overlay;

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
	msg_recv.msg_flags = 0;

	/*
	 * Handle delayed disconnections
	 */
	if (conn_info->state != CONN_STATE_ACTIVE) {
		res = libais_disconnect (conn_info);
		return (res);
	}

	if (conn_info->authenticated) {
		msg_recv.msg_control = 0;
		msg_recv.msg_controllen = 0;
	} else {
		msg_recv.msg_control = (void *)cmsg_cred;
		msg_recv.msg_controllen = sizeof (cmsg_cred);
	}

	iov_recv.iov_base = &conn_info->inb[conn_info->inb_start];
	iov_recv.iov_len = (SIZEINB) - conn_info->inb_start;
	assert (iov_recv.iov_len != 0);
//printf ("inb start inb inuse %d %d\n", conn_info->inb_start, conn_info->inb_inuse);

retry_recv:
	res = recvmsg (fd, &msg_recv, MSG_DONTWAIT | MSG_NOSIGNAL);
//printf ("received %d bytes\n", res);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	} else
	if (res == -1) {
printf ("res-1 errno = %d\n", errno);
		goto error_disconnect;
	} else
	if (res == 0) {
printf ("Res0 errno = %d\n", errno);
		goto error_disconnect;
		return (-1);
	}

	/*
	 * Authenticate if this connection has not been authenticated
	 */
	if (conn_info->authenticated == 0) {
		cmsg = CMSG_FIRSTHDR (&msg_recv);
		cred = (struct ucred *)CMSG_DATA (cmsg);
		if (cred) {
			if (cred->uid == 0 || cred->gid == gid_valid) {
				setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
				conn_info->authenticated = 1;
			}
		}
		if (conn_info->authenticated == 0) {
			log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", cred->gid, gid_valid);
		}
	}
	/*
	 * Dispatch all messages received in recvmsg that can be dispatched
	 * sizeof (struct req_header) needed at minimum to do any processing
	 */
	conn_info->inb_inuse += res;
	conn_info->inb_start += res;

	while (conn_info->inb_inuse >= sizeof (struct req_header) && res != -1) {
		header = (struct req_header *)&conn_info->inb[conn_info->inb_start - conn_info->inb_inuse];

		if (header->size > conn_info->inb_inuse) {
			break;
		}
		service = conn_info->service;

		/*
		 * If this service is in init phase, initialize service
		 * else handle message using service handlers
		 */
		if (service == SOCKET_SERVICE_INIT) {
			/*
			 * Initializing service
			 */
			res = ais_service_handlers[header->id]->libais_init_fn (conn_info, header);
		} else  {
			/*
			 * Not an init service, but a standard service
			 */
			if (header->id < 0 || header->id > ais_service_handlers[service - 1]->libais_handlers_count) {
				log_printf (LOG_LEVEL_SECURITY, "Invalid header id is %d min 0 max %d\n",
				header->id, ais_service_handlers[service - 1]->libais_handlers_count);
				res = -1;
				goto error_disconnect;
			}

			/*
			 * Determine if a message can be queued with gmi and if so
			 * deliver it, otherwise tell the library we are too busy
			 */
	
			send_ok = gmi_send_ok (ais_service_handlers[service - 1]->libais_handlers[header->id].gmi_prio, 1000 + header->size);
			if (send_ok) {
				*prio = 0;
				res = ais_service_handlers[service - 1]->libais_handlers[header->id].libais_handler_fn(conn_info, header);
			} else {
				*prio = (*prio) + 1;

				/*
				 * Overload, tell library to retry
				 */
				msg_overlay.header.size = 
					ais_service_handlers[service - 1]->libais_handlers[header->id].response_size;
				msg_overlay.header.id = 
					ais_service_handlers[service - 1]->libais_handlers[header->id].response_id;
				msg_overlay.header.error = SA_ERR_TRY_AGAIN;
				libais_send_response (conn_info, &msg_overlay,
					msg_overlay.header.size);
			}
		}
		conn_info->inb_inuse -= header->size;
	} /* while */

	if (conn_info->inb_inuse == 0) {
		conn_info->inb_start = 0;
	} else 
// BUG	if (connections[fd].inb_start + connections[fd].inb_inuse >= SIZEINB) {
	if (conn_info->inb_start >= SIZEINB) {
		/*
		 * If in buffer is full, move it back to start
		 */
		memmove (conn_info->inb,
			&conn_info->inb[conn_info->inb_start - conn_info->inb_inuse],
			sizeof (char) * conn_info->inb_inuse);
		conn_info->inb_start = conn_info->inb_inuse;
	}
	
	return (res);

error_disconnect:
	res = libais_disconnect (conn_info);
	return (res);
}

extern void print_stats (void);

void sigintr_handler (int signum)
{

#ifdef DEBUG_MEMPOOL
	int stats_inuse[MEMPOOL_GROUP_SIZE];
	int stats_avail[MEMPOOL_GROUP_SIZE];
	int stats_memoryused[MEMPOOL_GROUP_SIZE];
	int i;

	mempool_getstats (stats_inuse, stats_avail, stats_memoryused);
	log_printf (LOG_LEVEL_DEBUG, "Memory pools:\n");
	for (i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
	log_printf (LOG_LEVEL_DEBUG, "order %d size %d inuse %d avail %d memory used %d\n",
		i, 1<<i, stats_inuse[i], stats_avail[i], stats_memoryused[i]);
	}
#endif

	print_stats ();
	ais_done (AIS_DONE_EXIT);
}

static struct sched_param sched_param = { 
	sched_priority: 99
};

static int pool_sizes[] = { 0, 0, 0, 0, 0, 4096, 0, 1, 0, /* 256 */
					1024, 0, 1, 4096, 0, 0, 0, 0, /* 65536 */
					1, 1, 1, 1, 1, 1, 1, 1, 1 };

static int (*aisexec_handler_fns[AIS_SERVICE_HANDLER_AISEXEC_FUNCTIONS_MAX]) (void *msg, struct in_addr source_addr);
static int aisexec_handler_fns_count = 0;

/*
 * Builds the handler table as an optimization
 */
static void aisexec_handler_fns_build (void)
{
	int i, j;

	for (i = 0; i < AIS_SERVICE_HANDLERS_COUNT; i++) {
		for (j = 0; j < ais_service_handlers[i]->aisexec_handler_fns_count; j++) {
			aisexec_handler_fns[aisexec_handler_fns_count++] = 
				ais_service_handlers[i]->aisexec_handler_fns[j];
		}
	}
	log_printf (LOG_LEVEL_DEBUG, "built %d handler functions\n", aisexec_handler_fns_count);
}

char delivery_data[MESSAGE_SIZE_MAX];

static void deliver_fn (
	struct gmi_groupname *groupname,
	struct in_addr source_addr,
	struct iovec *iovec,
	int iov_len)
{
	struct req_header *header;
	int res;
	int pos = 0;
	int i;

	/*
	 * Build buffer without iovecs to make processing easier
	 * This is only used for messages which are multicast with iovecs
	 * and self-delivered.  All other mechanisms avoid the copy.
	 */
	if (iov_len > 1) {
		for (i = 0; i < iov_len; i++) {
			memcpy (&delivery_data[pos], iovec[i].iov_base, iovec[i].iov_len);
			pos += iovec[i].iov_len;
			assert (pos < MESSAGE_SIZE_MAX);
		}
		header = (struct req_header *)delivery_data;
	} else {
		header = (struct req_header *)iovec[0].iov_base;
	}
	res = aisexec_handler_fns[header->id](header, source_addr);
}

static void confchg_fn (
	struct sockaddr_in *member_list, int member_list_entries,
	struct sockaddr_in *left_list, int left_list_entries,
	struct sockaddr_in *joined_list, int joined_list_entries)
{
	int i;

	/*
	 * Call configure change for all APIs
	 */
	for (i = 0; i < AIS_SERVICE_HANDLERS_COUNT; i++) {
		if (ais_service_handlers[i]->confchg_fn) {
			ais_service_handlers[i]->confchg_fn (member_list, member_list_entries,
				left_list, left_list_entries, joined_list, joined_list_entries);
		}
	}
}

static void aisexec_uid_determine (void)
{
	struct passwd *passwd;

	passwd = getpwnam("ais");
	if (passwd == 0) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: The 'ais' user is not found in /etc/passwd, please read the documentation.\n");
		ais_done (AIS_DONE_UID_DETERMINE);
	}
	ais_uid = passwd->pw_uid;
}

static void aisexec_gid_determine (void)
{
	struct group *group;
	group = getgrnam ("ais");
	if (group == 0) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: The 'ais' group is not found in /etc/group, please read the documentation.\n");
		ais_done (AIS_DONE_GID_DETERMINE);
	}
	gid_valid = group->gr_gid;
}

static void aisexec_priv_drop (void)
{
	setuid (ais_uid);
	setegid (ais_uid);
}

static void aisexec_mempool_init (void)
{
	int res;

	res = mempool_init (pool_sizes);
	if (res == ENOMEM) {
		log_printf (LOG_LEVEL_ERROR, "Couldn't allocate memory pools, not enough memory");
		ais_done (AIS_DONE_MEMPOOL_INIT);
	}
}

static void aisexec_tty_detach (void)
{
#define DEBUG
#ifndef DEBUG
	/*
	 * Disconnect from TTY if this is not a debug run
	 */
	switch (fork ()) {
		case -1:
			ais_done (AIS_DONE_FORK);
			break;
		case 0:
			/*
			 * child which is disconnected, run this process
			 */
			break;
		default:
			exit (0);
			break;
	}
#endif
#undef DEBUG
}

static void aisexec_service_handlers_init (void)
{
	int i;
	/*
	 * Initialize all services
	 */
	for (i = 0; i < AIS_SERVICE_HANDLERS_COUNT; i++) {
		if (ais_service_handlers[i]->aisexec_init_fn) {
			ais_service_handlers[i]->aisexec_init_fn ();
		}
	}
}

static void aisexec_libais_bind (int *server_fd)
{
	int libais_server_fd;
	struct sockaddr_un un_addr;
	int res;

	/*
	 * Create socket for libais clients, name socket, listen for connections
	 */
	libais_server_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (libais_server_fd == -1) {
		log_printf (LOG_LEVEL_ERROR ,"Cannot create libais client connections socket.\n");
		ais_done (AIS_DONE_LIBAIS_SOCKET);
	};

	memset (&un_addr, 0, sizeof (struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
	strcpy (un_addr.sun_path + 1, socketname);

	res = bind (libais_server_fd, (struct sockaddr *)&un_addr, sizeof (struct sockaddr_un));
	if (res) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: Could not bind AF_UNIX: %s.\n", strerror (errno));
		ais_done (AIS_DONE_LIBAIS_BIND);
	}
	listen (libais_server_fd, SERVER_BACKLOG);

	*server_fd = libais_server_fd;
}

static void aisexec_setscheduler (void)
{
	int res;

return;
	res = sched_setscheduler (0, SCHED_RR, &sched_param);
	if (res == -1) {
		log_printf (LOG_LEVEL_WARNING, "Could not set SCHED_RR at priority 99: %s\n", strerror (errno));
	}
}

static void aisexec_mlockall (void)
{
	int res;

	res = mlockall (MCL_CURRENT | MCL_FUTURE);
	if (res == -1) {
		log_printf (LOG_LEVEL_WARNING, "Could not lock memory of service to avoid page faults: %s\n", strerror (errno));
	};
}

void aisexec_keyread (unsigned char *key)
{
	int fd;
	int res;

	fd = open ("/etc/ais/authkey", O_RDONLY);
	if (fd == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not open /etc/ais/authkey: %s\n", strerror (errno));
		ais_done (AIS_DONE_READKEY);
	}
	res = read (fd, key, 128);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not read /etc/ais/authkey: %s\n", strerror (errno));
		ais_done (AIS_DONE_READKEY);
	}
	if (res != 128) {
		log_printf (LOG_LEVEL_ERROR, "Could only read %d bits of 1024 bits from /etc/ais/authkey.\n", res * 8);
		ais_done (AIS_DONE_READKEY);
	}

	close (fd);
}



int main (int argc, char **argv)
{
	int libais_server_fd;
	int res;
	struct sockaddr_in sockaddr_in_mcast;
	struct sockaddr_in sockaddr_in_bindnet;
	gmi_join_handle handle;
	unsigned char private_key[128];

	char *error_string;

	log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: Copyright (C) 2002-2004 MontaVista Software, Inc.\n");

	aisexec_uid_determine ();

	aisexec_gid_determine ();

	aisexec_poll_handle = poll_create ();

	/*
	 * if gmi_init doesn't have root priveleges, it cannot
	 * bind to a specific interface.  This only matters if
	 * there is more then one interface in a system, so
	 * in this case, only a warning is printed
	 */
	/*
	 * Initialize group messaging interface with multicast address
	 */
	res = amfReadNetwork (&error_string, &sockaddr_in_mcast, &sockaddr_in_bindnet);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		ais_done (AIS_DONE_READNETWORK);
	}

	/*
	 * Set round robin realtime scheduling with priority 99
	 * Lock all memory to avoid page faults which may interrupt
	 * application healthchecking
	 */
	aisexec_setscheduler ();

	aisexec_mlockall ();

	aisexec_keyread (private_key);

	gmi_log_printf_init (internal_log_printf_checkdebug,
		LOG_LEVEL_SECURITY, LOG_LEVEL_ERROR, LOG_LEVEL_WARNING,
		LOG_LEVEL_NOTICE, LOG_LEVEL_DEBUG);
	gmi_init (&sockaddr_in_mcast, &sockaddr_in_bindnet,
		&aisexec_poll_handle, &this_ip,
		private_key,
		sizeof (private_key));
	
	/*
	 * Drop root privleges to user 'ais'
	 * TODO: Don't really need full root capabilities;
	 *       needed capabilities are:
	 * CAP_NET_RAW (bindtodevice)
	 * CAP_SYS_NICE (setscheduler)
	 * CAP_IPC_LOCK (mlockall)
	 */
	aisexec_priv_drop ();

	aisexec_handler_fns_build ();

	aisexec_mempool_init ();

	res = amfReadGroups(&error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		ais_done (AIS_DONE_READGROUPS);
	}
	
	aisexec_tty_detach ();

	signal (SIGINT, sigintr_handler);

	aisexec_service_handlers_init ();

	aisexec_libais_bind (&libais_server_fd);

	log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: started and ready to receive connections.\n");

	/*
	 * Setup libais connection dispatch routine
	 */
	poll_dispatch_add (aisexec_poll_handle, libais_server_fd,
		POLLIN, 0, poll_handler_libais_accept, 0);

	/*
	 * Join multicast group and setup delivery
	 *  and configuration change functions
	 */
	gmi_join (0, deliver_fn, confchg_fn, &handle);

	/*
	 * Start main processing loop
	 */
	poll_run (aisexec_poll_handle);

	return (0);
}
