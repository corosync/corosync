/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#include "qdevice-cmap.h"
#include "qdevice-net-poll.h"
#include "qdevice-log.h"
#include "qdevice-net-send.h"
#include "qdevice-net-socket.h"
#include "qdevice-votequorum.h"
#include "qdevice-ipc.h"
#include "qdevice-net-poll-array-user-data.h"

/*
 * Needed for creating nspr handle from unix fd
 */
#include <private/pprio.h>

static void
qdevice_net_poll_read_socket(struct qdevice_net_instance *instance)
{

	if (qdevice_net_socket_read(instance) == -1) {
		instance->schedule_disconnect = 1;
	}
}

static void
qdevice_net_poll_read_votequorum(struct qdevice_net_instance *instance)
{

	if (qdevice_votequorum_dispatch(instance->qdevice_instance_ptr) == -1) {
		instance->schedule_disconnect = 1;
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_COROSYNC_CONNECTION_CLOSED;
	}
}

static void
qdevice_net_poll_read_cmap(struct qdevice_net_instance *instance)
{

	if (qdevice_cmap_dispatch(instance->qdevice_instance_ptr) == -1) {
		instance->schedule_disconnect = 1;
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_COROSYNC_CONNECTION_CLOSED;
	}
}

static void
qdevice_net_poll_write_socket(struct qdevice_net_instance *instance, const PRPollDesc *pfd)
{
	int res;

	if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT) {
		res = nss_sock_non_blocking_client_succeeded(pfd);
		if (res == -1) {
			/*
			 * Connect failed -> try next
			 */
			res = nss_sock_non_blocking_client_try_next(&instance->non_blocking_client);
			if (res == -1) {
				qdevice_log_nss(LOG_ERR, "Can't connect to qnetd host.");
				nss_sock_non_blocking_client_destroy(&instance->non_blocking_client);
			}
		} else if (res == 0) {
			/*
			 * Poll again
			 */
		} else if (res == 1) {
			/*
			 * Connect success
			 */
			instance->socket = instance->non_blocking_client.socket;
			nss_sock_non_blocking_client_destroy(&instance->non_blocking_client);
			instance->non_blocking_client.socket = NULL;

			instance->state = QDEVICE_NET_INSTANCE_STATE_SENDING_PREINIT_REPLY;

			qdevice_log(LOG_DEBUG, "Sending preinit msg to qnetd");
			if (qdevice_net_send_preinit(instance) != 0) {
				instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
				instance->schedule_disconnect = 1;
			}
		} else {
			qdevice_log(LOG_CRIT, "Unhandled nss_sock_non_blocking_client_succeeded");
			exit(1);
		}
	} else {
		if (qdevice_net_socket_write(instance) == -1) {
			instance->schedule_disconnect = 1;
		}
	}
}

static void
qdevice_net_poll_err_socket(struct qdevice_net_instance *instance, const PRPollDesc *pfd)
{

	if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT) {
		/*
		 * Workaround for RHEL<7. Pollout is never set for nonblocking connect (doesn't work
		 * only with poll, select works as expected!???).
		 * So test if client is still valid and if pollout was not already called (ensured
		 * by default because of order in PR_Poll).
		 * If both applies it's possible to emulate pollout set by calling poll_write.
		 */
		if (!instance->non_blocking_client.destroyed) {
			qdevice_net_poll_write_socket(instance, pfd);
		}
	} else {
		qdevice_log(LOG_ERR, "POLL_ERR (%u) on main socket", pfd->out_flags);

		instance->schedule_disconnect = 1;
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_CLOSED_CONNECTION;
	}
}

static void
qdevice_net_poll_read_ipc_socket(struct qdevice_net_instance *instance)
{
	struct unix_socket_client *client;
	PRFileDesc *prfd;
	struct qdevice_ipc_user_data *user_data;

	if (qdevice_ipc_accept(instance->qdevice_instance_ptr, &client) != 0) {
		return ;
	}

	prfd = PR_CreateSocketPollFd(client->socket);
	if (prfd == NULL) {
		qdevice_log_nss(LOG_CRIT, "Can't create NSPR poll fd for IPC client. "
		    "Disconnecting client");
		qdevice_ipc_client_disconnect(instance->qdevice_instance_ptr, client);

		return ;
	}

	user_data = (struct qdevice_ipc_user_data *)client->user_data;
	user_data->model_data = (void *)prfd;
}

static PRPollDesc *
qdevice_net_pr_poll_array_create(struct qdevice_net_instance *instance)
{
	struct pr_poll_array *poll_array;
	PRPollDesc *poll_desc;
	struct qdevice_net_poll_array_user_data *user_data;
	struct unix_socket_client *ipc_client;
	const struct unix_socket_client_list *ipc_client_list;
	struct qdevice_ipc_user_data *qdevice_ipc_user_data;

	poll_array = &instance->poll_array;
	ipc_client_list = &instance->qdevice_instance_ptr->local_ipc.clients;

	pr_poll_array_clean(poll_array);

	if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
		return (NULL);
	}
	poll_desc->fd = instance->votequorum_poll_fd;
	poll_desc->in_flags = PR_POLL_READ;
	user_data->type = QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_VOTEQUORUM;

	if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
		return (NULL);
	}
	poll_desc->fd = instance->cmap_poll_fd;
	poll_desc->in_flags = PR_POLL_READ;
	user_data->type = QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_CMAP;

	if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
		return (NULL);
	}
	poll_desc->fd = instance->ipc_socket_poll_fd;
	poll_desc->in_flags = PR_POLL_READ;
	user_data->type = QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET;

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT ||
	    !instance->non_blocking_client.destroyed) {
		if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
			return (NULL);
		}

		user_data->type = QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_SOCKET;

		if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT) {
			poll_desc->fd = instance->non_blocking_client.socket;
			poll_desc->in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
		} else {
			poll_desc->fd = instance->socket;
			poll_desc->in_flags = PR_POLL_READ;

			if (!send_buffer_list_empty(&instance->send_buffer_list)) {
				poll_desc->in_flags |= PR_POLL_WRITE;
			}
		}
	}

	TAILQ_FOREACH(ipc_client, ipc_client_list, entries) {
		if (!ipc_client->reading_line && !ipc_client->writing_buffer) {
			continue;
		}

		if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
			return (NULL);
		}

		qdevice_ipc_user_data = (struct qdevice_ipc_user_data *)ipc_client->user_data;
		poll_desc->fd = (PRFileDesc *)qdevice_ipc_user_data->model_data;
		if (ipc_client->reading_line) {
			poll_desc->in_flags |= PR_POLL_READ;
		}

		if (ipc_client->writing_buffer) {
			poll_desc->in_flags |= PR_POLL_WRITE;
		}

		user_data->type = QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT;
		user_data->ipc_client = ipc_client;
	}

	pr_poll_array_gc(poll_array);

	return (poll_array->array);
}

int
qdevice_net_poll(struct qdevice_net_instance *instance)
{
	PRPollDesc *pfds;
	PRFileDesc *prfd;
	PRInt32 poll_res;
	ssize_t i;
	struct qdevice_net_poll_array_user_data *user_data;
	struct unix_socket_client *ipc_client;
	struct qdevice_ipc_user_data *qdevice_ipc_user_data;

	pfds = qdevice_net_pr_poll_array_create(instance);
	if (pfds == NULL) {
		return (-1);
	}

	instance->schedule_disconnect = 0;

	if ((poll_res = PR_Poll(pfds, pr_poll_array_size(&instance->poll_array),
	    timer_list_time_to_expire(&instance->main_timer_list))) > 0) {
		for (i = 0; i < pr_poll_array_size(&instance->poll_array); i++) {
			user_data = pr_poll_array_get_user_data(&instance->poll_array, i);

			ipc_client = user_data->ipc_client;

			if (pfds[i].out_flags & PR_POLL_READ) {
				switch (user_data->type) {
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_SOCKET:
					qdevice_net_poll_read_socket(instance);
					break;
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_VOTEQUORUM:
					qdevice_net_poll_read_votequorum(instance);
					break;
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_CMAP:
					qdevice_net_poll_read_cmap(instance);
					break;
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET:
					qdevice_net_poll_read_ipc_socket(instance);
					break;
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT:
					qdevice_ipc_io_read(instance->qdevice_instance_ptr, ipc_client);
					break;
				default:
					qdevice_log(LOG_CRIT, "Unhandled read on poll descriptor %u", i);
					exit(1);
					break;
				}
			}

			if (!instance->schedule_disconnect && pfds[i].out_flags & PR_POLL_WRITE) {
				switch (user_data->type) {
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_SOCKET:
					qdevice_net_poll_write_socket(instance, &pfds[i]);
					break;
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT:
					qdevice_ipc_io_write(instance->qdevice_instance_ptr, ipc_client);
					break;
				default:
					qdevice_log(LOG_CRIT, "Unhandled write on poll descriptor %u", i);
					exit(1);
					break;
				}
			}

			if (!instance->schedule_disconnect &&
			    (pfds[i].out_flags & (PR_POLL_ERR|PR_POLL_NVAL|PR_POLL_HUP|PR_POLL_EXCEPT)) &&
			    !(pfds[i].out_flags & (PR_POLL_READ|PR_POLL_WRITE))) {
				switch (user_data->type) {
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_SOCKET:
					qdevice_net_poll_err_socket(instance, &pfds[i]);
					break;
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET:
					if (pfds[i].out_flags != PR_POLL_NVAL) {
						qdevice_log(LOG_CRIT, "POLLERR (%u) on local socket",
						    pfds[i].out_flags);
						exit(1);
					} else {
						qdevice_log(LOG_DEBUG, "Local socket is closed");
						instance->schedule_disconnect = 1;
						instance->disconnect_reason =
						    QDEVICE_NET_DISCONNECT_REASON_LOCAL_SOCKET_CLOSED;
					}
					break;
				case QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT:
					qdevice_log(LOG_DEBUG, "POLL_ERR (%u) on ipc client socket. "
					    "Disconnecting.",  pfds[i].out_flags);
					ipc_client->schedule_disconnect = 1;
					break;
				default:
					qdevice_log(LOG_CRIT, "Unhandled error on poll descriptor %u", i);
					exit(1);
					break;
				}
			}

			if (user_data->type == QDEVICE_NET_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT &&
			    ipc_client->schedule_disconnect) {
				qdevice_ipc_user_data = (struct qdevice_ipc_user_data *)ipc_client->user_data;
				prfd = (PRFileDesc *)qdevice_ipc_user_data->model_data;

				if (PR_DestroySocketPollFd(prfd) != PR_SUCCESS) {
					qdevice_log_nss(LOG_WARNING, "Unable to destroy client IPC poll socket fd");
				}

				qdevice_ipc_client_disconnect(instance->qdevice_instance_ptr, ipc_client);
			}
		}
	}

	if (!instance->schedule_disconnect) {
		timer_list_expire(&instance->main_timer_list);
	}

	if (instance->schedule_disconnect) {
		/*
		 * Schedule disconnect can be set by this function, by some timer_list callback
		 * or cmap/votequorum callbacks
		 */
		return (-1);
	}

	return (0);
}
