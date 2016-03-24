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

enum qdevice_net_poll_pfd {
	QDEVICE_NET_POLL_VOTEQUORUM,
	QDEVICE_NET_POLL_CMAP,
	QDEVICE_NET_POLL_LOCAL_SOCKET,
	QDEVICE_NET_POLL_SOCKET,
	QDEVICE_NET_POLL_MAX_PFDS
};

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
qdevice_net_poll_read_local_socket(struct qdevice_net_instance *instance)
{

//	qdevice_log(LOG_DEBUG, "READ ON LOCAL SOCKET");
}

int
qdevice_net_poll(struct qdevice_net_instance *instance)
{
	PRPollDesc pfds[QDEVICE_NET_POLL_MAX_PFDS];
	PRInt32 poll_res;
	PRIntn no_pfds;
	int i;

	no_pfds = 0;

	pfds[QDEVICE_NET_POLL_VOTEQUORUM].fd = instance->votequorum_poll_fd;
	pfds[QDEVICE_NET_POLL_VOTEQUORUM].in_flags = PR_POLL_READ;
	no_pfds++;

	pfds[QDEVICE_NET_POLL_CMAP].fd = instance->cmap_poll_fd;
	pfds[QDEVICE_NET_POLL_CMAP].in_flags = PR_POLL_READ;
	no_pfds++;

	pfds[QDEVICE_NET_POLL_LOCAL_SOCKET].fd = instance->local_socket_poll_fd;
	pfds[QDEVICE_NET_POLL_LOCAL_SOCKET].in_flags = PR_POLL_READ;
	no_pfds++;

	if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT &&
	    !instance->non_blocking_client.destroyed) {
		pfds[QDEVICE_NET_POLL_SOCKET].fd = instance->non_blocking_client.socket;
		pfds[QDEVICE_NET_POLL_SOCKET].in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
		no_pfds++;
	} else {
		pfds[QDEVICE_NET_POLL_SOCKET].fd = instance->socket;
		pfds[QDEVICE_NET_POLL_SOCKET].in_flags = PR_POLL_READ;
		if (!send_buffer_list_empty(&instance->send_buffer_list)) {
			pfds[QDEVICE_NET_POLL_SOCKET].in_flags |= PR_POLL_WRITE;
		}
		no_pfds++;
	}

	instance->schedule_disconnect = 0;

	if ((poll_res = PR_Poll(pfds, no_pfds,
	    timer_list_time_to_expire(&instance->main_timer_list))) > 0) {
		for (i = 0; i < no_pfds; i++) {
			if (pfds[i].out_flags & PR_POLL_READ) {
				switch (i) {
				case QDEVICE_NET_POLL_SOCKET:
					qdevice_net_poll_read_socket(instance);
					break;
				case QDEVICE_NET_POLL_VOTEQUORUM:
					qdevice_net_poll_read_votequorum(instance);
					break;
				case QDEVICE_NET_POLL_CMAP:
					qdevice_net_poll_read_cmap(instance);
					break;
				case QDEVICE_NET_POLL_LOCAL_SOCKET:
					qdevice_net_poll_read_local_socket(instance);
					break;
				default:
					qdevice_log(LOG_CRIT, "Unhandled read on poll descriptor %u", i);
					exit(1);
					break;
				}
			}

			if (!instance->schedule_disconnect && pfds[i].out_flags & PR_POLL_WRITE) {
				switch (i) {
				case QDEVICE_NET_POLL_SOCKET:
					qdevice_net_poll_write_socket(instance, &pfds[i]);
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
				switch (i) {
				case QDEVICE_NET_POLL_SOCKET:
					qdevice_net_poll_err_socket(instance, &pfds[i]);
					break;
				case QDEVICE_NET_POLL_LOCAL_SOCKET:
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
				default:
					qdevice_log(LOG_CRIT, "Unhandled error on poll descriptor %u", i);
					exit(1);
					break;
				}
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
