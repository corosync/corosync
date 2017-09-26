/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)

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

#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <limits.h>

#include <corosync/sq.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <qb/qbdefs.h>
#include <qb/qbloop.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>
#include "totemudp.h"

#include "util.h"
#include "totemcrypto.h"

#include <nss.h>
#include <pk11pub.h>
#include <pkcs11.h>
#include <prerror.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MCAST_SOCKET_BUFFER_SIZE (TRANSMITS_ALLOWED * FRAME_SIZE_MAX)
#define NETIF_STATE_REPORT_UP		1
#define NETIF_STATE_REPORT_DOWN		2

#define BIND_STATE_UNBOUND	0
#define BIND_STATE_REGULAR	1
#define BIND_STATE_LOOPBACK	2

struct totemudp_socket {
	int mcast_recv;
	int mcast_send;
	int token;
	/*
	 * Socket used for local multicast delivery. We don't rely on multicast
	 * loop and rather this UNIX DGRAM socket is used. Socket is created by
	 * socketpair call and they are used in same way as pipe (so [0] is read
	 * end and [1] is write end)
	 */
	int local_mcast_loop[2];
};

struct totemudp_instance {
	struct crypto_instance *crypto_inst;

	qb_loop_t *totemudp_poll_handle;

	struct totem_interface *totem_interface;

	int netif_state_report;

	int netif_bind_state;

	void *context;

	void (*totemudp_deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*totemudp_iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address);

	void (*totemudp_target_set_completed) (void *context);

	/*
	 * Function and data used to log messages
	 */
	int totemudp_log_level_security;

	int totemudp_log_level_error;

	int totemudp_log_level_warning;

	int totemudp_log_level_notice;

	int totemudp_log_level_debug;

	int totemudp_subsys_id;

	void (*totemudp_log_printf) (
		int level,
		int subsys,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 6, 7)));

	void *udp_context;

	char iov_buffer[FRAME_SIZE_MAX];

	char iov_buffer_flush[FRAME_SIZE_MAX];

	struct iovec totemudp_iov_recv;

	struct iovec totemudp_iov_recv_flush;

	struct totemudp_socket totemudp_sockets;

	struct totem_ip_address mcast_address;

	int stats_sent;

	int stats_recv;

	int stats_delv;

	int stats_remcasts;

	int stats_orf_token;

	struct timeval stats_tv_start;

	struct totem_ip_address my_id;

	int firstrun;

	qb_loop_timer_handle timer_netif_check_timeout;

	unsigned int my_memb_entries;

	int flushing;

	struct totem_config *totem_config;

	totemsrp_stats_t *stats;

	struct totem_ip_address token_target;
};

struct work_item {
	const void *msg;
	unsigned int msg_len;
	struct totemudp_instance *instance;
};

static int totemudp_build_sockets (
	struct totemudp_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *mcastaddress,
	struct totemudp_socket *sockets,
	struct totem_ip_address *bound_to);

static struct totem_ip_address localhost;

static void totemudp_instance_initialize (struct totemudp_instance *instance)
{
	memset (instance, 0, sizeof (struct totemudp_instance));

	instance->netif_state_report = NETIF_STATE_REPORT_UP | NETIF_STATE_REPORT_DOWN;

	instance->totemudp_iov_recv.iov_base = instance->iov_buffer;

	instance->totemudp_iov_recv.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);
	instance->totemudp_iov_recv_flush.iov_base = instance->iov_buffer_flush;

	instance->totemudp_iov_recv_flush.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);

	/*
	 * There is always atleast 1 processor
	 */
	instance->my_memb_entries = 1;
}

#define log_printf(level, format, args...)				\
do {									\
        instance->totemudp_log_printf (					\
		level, instance->totemudp_subsys_id,			\
                __FUNCTION__, __FILE__, __LINE__,			\
		(const char *)format, ##args);				\
} while (0);

#define LOGSYS_PERROR(err_num, level, fmt, args...)						\
do {												\
	char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];						\
	const char *_error_ptr = qb_strerror_r(err_num, _error_str, sizeof(_error_str));	\
        instance->totemudp_log_printf (								\
		level, instance->totemudp_subsys_id,						\
                __FUNCTION__, __FILE__, __LINE__,						\
		fmt ": %s (%d)\n", ##args, _error_ptr, err_num);				\
	} while(0)

int totemudp_crypto_set (
	void *udp_context,
	const char *cipher_type,
	const char *hash_type)
{

	return (0);
}


static inline void ucast_sendmsg (
	struct totemudp_instance *instance,
	struct totem_ip_address *system_to,
	const void *msg,
	unsigned int msg_len)
{
	struct msghdr msg_ucast;
	int res = 0;
	size_t buf_out_len;
	unsigned char buf_out[FRAME_SIZE_MAX];
	struct sockaddr_storage sockaddr;
	struct iovec iovec;
	int addrlen;

	/*
	 * Encrypt and digest the message
	 */
	if (crypto_encrypt_and_sign (
		instance->crypto_inst,
		(const unsigned char *)msg,
		msg_len,
		buf_out,
		&buf_out_len) != 0) {
		log_printf(LOGSYS_LEVEL_CRIT, "Error encrypting/signing packet (non-critical)");
		return;
	}

	iovec.iov_base = (void *)buf_out;
	iovec.iov_len = buf_out_len;

	/*
	 * Build unicast message
	 */
	memset(&msg_ucast, 0, sizeof(msg_ucast));
	totemip_totemip_to_sockaddr_convert(system_to,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	msg_ucast.msg_name = &sockaddr;
	msg_ucast.msg_namelen = addrlen;
	msg_ucast.msg_iov = (void *)&iovec;
	msg_ucast.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
	msg_ucast.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
	msg_ucast.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
	msg_ucast.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
	msg_ucast.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
	msg_ucast.msg_accrightslen = 0;
#endif


	/*
	 * Transmit unicast message
	 * An error here is recovered by totemsrp
	 */
	res = sendmsg (instance->totemudp_sockets.mcast_send, &msg_ucast,
		MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"sendmsg(ucast) failed (non-critical)");
	}
}

static inline void mcast_sendmsg (
	struct totemudp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	struct msghdr msg_mcast;
	int res = 0;
	size_t buf_out_len;
	unsigned char buf_out[FRAME_SIZE_MAX];
	struct iovec iovec;
	struct sockaddr_storage sockaddr;
	int addrlen;

	/*
	 * Encrypt and digest the message
	 */
	if (crypto_encrypt_and_sign (
		instance->crypto_inst,
		(const unsigned char *)msg,
		msg_len,
		buf_out,
		&buf_out_len) != 0) {
		log_printf(LOGSYS_LEVEL_CRIT, "Error encrypting/signing packet (non-critical)");
		return;
	}

	iovec.iov_base = (void *)&buf_out;
	iovec.iov_len = buf_out_len;

	/*
	 * Build multicast message
	 */
	totemip_totemip_to_sockaddr_convert(&instance->mcast_address,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	memset(&msg_mcast, 0, sizeof(msg_mcast));
	msg_mcast.msg_name = &sockaddr;
	msg_mcast.msg_namelen = addrlen;
	msg_mcast.msg_iov = (void *)&iovec;
	msg_mcast.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
	msg_mcast.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
	msg_mcast.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
	msg_mcast.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
	msg_mcast.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
	msg_mcast.msg_accrightslen = 0;
#endif

	/*
	 * Transmit multicast message
	 * An error here is recovered by totemsrp
	 */
	res = sendmsg (instance->totemudp_sockets.mcast_send, &msg_mcast,
		MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"sendmsg(mcast) failed (non-critical)");
		instance->stats->continuous_sendmsg_failures++;
	} else {
		instance->stats->continuous_sendmsg_failures = 0;
	}

	/*
	 * Transmit multicast message to local unix mcast loop
	 * An error here is recovered by totemsrp
	 */
	msg_mcast.msg_name = NULL;
	msg_mcast.msg_namelen = 0;

	res = sendmsg (instance->totemudp_sockets.local_mcast_loop[1], &msg_mcast,
		MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"sendmsg(local mcast loop) failed (non-critical)");
	}
}


int totemudp_finalize (
	void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	if (instance->totemudp_sockets.mcast_recv > 0) {
	 	qb_loop_poll_del (instance->totemudp_poll_handle,
			instance->totemudp_sockets.mcast_recv);
		close (instance->totemudp_sockets.mcast_recv);
	}
	if (instance->totemudp_sockets.mcast_send > 0) {
		close (instance->totemudp_sockets.mcast_send);
	}
	if (instance->totemudp_sockets.local_mcast_loop[0] > 0) {
		qb_loop_poll_del (instance->totemudp_poll_handle,
			instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[1]);
	}
	if (instance->totemudp_sockets.token > 0) {
		qb_loop_poll_del (instance->totemudp_poll_handle,
			instance->totemudp_sockets.token);
		close (instance->totemudp_sockets.token);
	}

	return (res);
}

/*
 * Only designed to work with a message with one iov
 */

static int net_deliver_fn (
	int fd,
	int revents,
	void *data)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)data;
	struct msghdr msg_recv;
	struct iovec *iovec;
	struct sockaddr_storage system_from;
	int bytes_received;
	int res = 0;

	if (instance->flushing == 1) {
		iovec = &instance->totemudp_iov_recv_flush;
	} else {
		iovec = &instance->totemudp_iov_recv;
	}

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_storage);
	msg_recv.msg_iov = iovec;
	msg_recv.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
	msg_recv.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
	msg_recv.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
	msg_recv.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
	msg_recv.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
	msg_recv.msg_accrightslen = 0;
#endif

	bytes_received = recvmsg (fd, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (bytes_received == -1) {
		return (0);
	} else {
		instance->stats_recv += bytes_received;
	}

	/*
	 * Authenticate and if authenticated, decrypt datagram
	 */
	res = crypto_authenticate_and_decrypt (instance->crypto_inst, iovec->iov_base, &bytes_received);
	if (res == -1) {
		log_printf (instance->totemudp_log_level_security, "Received message has invalid digest... ignoring.");
		log_printf (instance->totemudp_log_level_security,
			"Invalid packet data");
		iovec->iov_len = FRAME_SIZE_MAX;
		return 0;
	}
	iovec->iov_len = bytes_received;

	/*
	 * Handle incoming message
	 */
	instance->totemudp_deliver_fn (
		instance->context,
		iovec->iov_base,
		iovec->iov_len);

	iovec->iov_len = FRAME_SIZE_MAX;
	return (0);
}

static int netif_determine (
	struct totemudp_instance *instance,
	struct totem_ip_address *bindnet,
	struct totem_ip_address *bound_to,
	int *interface_up,
	int *interface_num)
{
	int res;

	res = totemip_iface_check (bindnet, bound_to,
		interface_up, interface_num,
                instance->totem_config->clear_node_high_bit);


	return (res);
}


/*
 * If the interface is up, the sockets for totem are built.  If the interface is down
 * this function is requeued in the timer list to retry building the sockets later.
 */
static void timer_function_netif_check_timeout (
	void *data)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)data;
	int interface_up;
	int interface_num;
	struct totem_ip_address *bind_address;

	/*
	 * Build sockets for every interface
	 */
	netif_determine (instance,
		&instance->totem_interface->bindnet,
		&instance->totem_interface->boundto,
		&interface_up, &interface_num);
	/*
	 * If the network interface isn't back up and we are already
	 * in loopback mode, add timer to check again and return
	 */
	if ((instance->netif_bind_state == BIND_STATE_LOOPBACK &&
		interface_up == 0) ||

	(instance->my_memb_entries == 1 &&
		instance->netif_bind_state == BIND_STATE_REGULAR &&
		interface_up == 1)) {

		qb_loop_timer_add (instance->totemudp_poll_handle,
			QB_LOOP_MED,
			instance->totem_config->downcheck_timeout*QB_TIME_NS_IN_MSEC,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);

		/*
		 * Add a timer to check for a downed regular interface
		 */
		return;
	}

	if (instance->totemudp_sockets.mcast_recv > 0) {
	 	qb_loop_poll_del (instance->totemudp_poll_handle,
			instance->totemudp_sockets.mcast_recv);
		close (instance->totemudp_sockets.mcast_recv);
	}
	if (instance->totemudp_sockets.mcast_send > 0) {
		close (instance->totemudp_sockets.mcast_send);
	}
	if (instance->totemudp_sockets.local_mcast_loop[0] > 0) {
		qb_loop_poll_del (instance->totemudp_poll_handle,
			instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[1]);
	}
	if (instance->totemudp_sockets.token > 0) {
		qb_loop_poll_del (instance->totemudp_poll_handle,
			instance->totemudp_sockets.token);
		close (instance->totemudp_sockets.token);
	}

	if (interface_up == 0) {
		/*
		 * Interface is not up
		 */
		instance->netif_bind_state = BIND_STATE_LOOPBACK;
		bind_address = &localhost;

		/*
		 * Add a timer to retry building interfaces and request memb_gather_enter
		 */
		qb_loop_timer_add (instance->totemudp_poll_handle,
			QB_LOOP_MED,
			instance->totem_config->downcheck_timeout*QB_TIME_NS_IN_MSEC,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
	} else {
		/*
		 * Interface is up
		 */
		instance->netif_bind_state = BIND_STATE_REGULAR;
		bind_address = &instance->totem_interface->bindnet;
	}
	/*
	 * Create and bind the multicast and unicast sockets
	 */
	(void)totemudp_build_sockets (instance,
		&instance->mcast_address,
		bind_address,
		&instance->totemudp_sockets,
		&instance->totem_interface->boundto);

	qb_loop_poll_add (
		instance->totemudp_poll_handle,
		QB_LOOP_MED,
		instance->totemudp_sockets.mcast_recv,
		POLLIN, instance, net_deliver_fn);

	qb_loop_poll_add (
		instance->totemudp_poll_handle,
		QB_LOOP_MED,
		instance->totemudp_sockets.local_mcast_loop[0],
		POLLIN, instance, net_deliver_fn);

	qb_loop_poll_add (
		instance->totemudp_poll_handle,
		QB_LOOP_MED,
		instance->totemudp_sockets.token,
		POLLIN, instance, net_deliver_fn);

	totemip_copy (&instance->my_id, &instance->totem_interface->boundto);

	/*
	 * This reports changes in the interface to the user and totemsrp
	 */
	if (instance->netif_bind_state == BIND_STATE_REGULAR) {
		if (instance->netif_state_report & NETIF_STATE_REPORT_UP) {
			log_printf (instance->totemudp_log_level_notice,
				"The network interface [%s] is now up.",
				totemip_print (&instance->totem_interface->boundto));
			instance->netif_state_report = NETIF_STATE_REPORT_DOWN;
			instance->totemudp_iface_change_fn (instance->context, &instance->my_id);
		}
		/*
		 * Add a timer to check for interface going down in single membership
		 */
		if (instance->my_memb_entries == 1) {
			qb_loop_timer_add (instance->totemudp_poll_handle,
				QB_LOOP_MED,
				instance->totem_config->downcheck_timeout*QB_TIME_NS_IN_MSEC,
				(void *)instance,
				timer_function_netif_check_timeout,
				&instance->timer_netif_check_timeout);
		}

	} else {
		if (instance->netif_state_report & NETIF_STATE_REPORT_DOWN) {
			log_printf (instance->totemudp_log_level_notice,
				"The network interface is down.");
			instance->totemudp_iface_change_fn (instance->context, &instance->my_id);
		}
		instance->netif_state_report = NETIF_STATE_REPORT_UP;

	}
}

/* Set the socket priority to INTERACTIVE to ensure
   that our messages don't get queued behind anything else */
static void totemudp_traffic_control_set(struct totemudp_instance *instance, int sock)
{
#ifdef SO_PRIORITY
	int prio = 6; /* TC_PRIO_INTERACTIVE */

	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int))) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning, "Could not set traffic priority");
    }
#endif
}

static int totemudp_build_sockets_ip (
	struct totemudp_instance *instance,
	struct totem_ip_address *mcast_address,
	struct totem_ip_address *bindnet_address,
	struct totemudp_socket *sockets,
	struct totem_ip_address *bound_to,
	int interface_num)
{
	struct sockaddr_storage sockaddr;
	struct ipv6_mreq mreq6;
	struct ip_mreq mreq;
	struct sockaddr_storage mcast_ss, boundto_ss;
	struct sockaddr_in6 *mcast_sin6 = (struct sockaddr_in6 *)&mcast_ss;
	struct sockaddr_in  *mcast_sin = (struct sockaddr_in *)&mcast_ss;
	struct sockaddr_in  *boundto_sin = (struct sockaddr_in *)&boundto_ss;
	unsigned int sendbuf_size;
	unsigned int recvbuf_size;
	unsigned int optlen = sizeof (sendbuf_size);
	unsigned int retries;
	int addrlen;
	int res;
	int flag;
	uint8_t sflag;
	int i;

	/*
	 * Create multicast recv socket
	 */
	sockets->mcast_recv = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->mcast_recv == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (sockets->mcast_recv);
	res = fcntl (sockets->mcast_recv, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Could not set non-blocking operation on multicast socket");
		return (-1);
	}

	/*
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->mcast_recv, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"setsockopt(SO_REUSEADDR) failed");
		return (-1);
	}

	/*
	 * Create local multicast loop socket
	 */
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets->local_mcast_loop) == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	for (i = 0; i < 2; i++) {
		totemip_nosigpipe (sockets->local_mcast_loop[i]);
		res = fcntl (sockets->local_mcast_loop[i], F_SETFL, O_NONBLOCK);
		if (res == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"Could not set non-blocking operation on multicast socket");
			return (-1);
		}
	}



	/*
	 * Setup mcast send socket
	 */
	sockets->mcast_send = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->mcast_send == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (sockets->mcast_send);
	res = fcntl (sockets->mcast_send, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Could not set non-blocking operation on multicast socket");
		return (-1);
	}

	/*
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->mcast_send, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"setsockopt(SO_REUSEADDR) failed");
		return (-1);
	}

	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port - 1,
		&sockaddr, &addrlen);

	retries = 0;
	while (1) {
		res = bind (sockets->mcast_send, (struct sockaddr *)&sockaddr, addrlen);
		if (res == 0) {
			break;
		}
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Unable to bind the socket to send multicast packets");
		if (++retries > BIND_MAX_RETRIES) {
			break;
		}

		/*
		 * Wait for a while
		 */
		(void)poll(NULL, 0, BIND_RETRIES_INTERVAL * retries);
	}
	if (res == -1) {
		return (-1);
	}

	/*
	 * Setup unicast socket
	 */
	sockets->token = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->token == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (sockets->token);
	res = fcntl (sockets->token, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Could not set non-blocking operation on token socket");
		return (-1);
	}

	/*
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->token, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"setsockopt(SO_REUSEADDR) failed");
		return (-1);
	}

	/*
	 * Bind to unicast socket used for token send/receives
	 * This has the side effect of binding to the correct interface
	 */
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &sockaddr, &addrlen);

	retries = 0;
	while (1) {
		res = bind (sockets->token, (struct sockaddr *)&sockaddr, addrlen);
		if (res == 0) {
			break;
		}
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Unable to bind UDP unicast socket");
		if (++retries > BIND_MAX_RETRIES) {
			break;
		}

		/*
		 * Wait for a while
		 */
		(void)poll(NULL, 0, BIND_RETRIES_INTERVAL * retries);
	}
	if (res == -1) {
		return (-1);
	}

	recvbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	sendbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	/*
	 * Set buffer sizes to avoid overruns
	 */
	res = setsockopt (sockets->mcast_recv, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_RCVBUF size on UDP mcast socket");
		return (-1);
	}
	res = setsockopt (sockets->mcast_send, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_SNDBUF size on UDP mcast socket");
		return (-1);
	}
	res = setsockopt (sockets->local_mcast_loop[0], SOL_SOCKET, SO_RCVBUF, &recvbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_RCVBUF size on UDP local mcast loop socket");
		return (-1);
	}
	res = setsockopt (sockets->local_mcast_loop[1], SOL_SOCKET, SO_SNDBUF, &sendbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_SNDBUF size on UDP local mcast loop socket");
		return (-1);
	}

	res = getsockopt (sockets->mcast_recv, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Receive multicast socket recv buffer size (%d bytes).", recvbuf_size);
	}

	res = getsockopt (sockets->mcast_send, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Transmit multicast socket send buffer size (%d bytes).", sendbuf_size);
	}

	res = getsockopt (sockets->local_mcast_loop[0], SOL_SOCKET, SO_RCVBUF, &recvbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Local receive multicast loop socket recv buffer size (%d bytes).", recvbuf_size);
	}

	res = getsockopt (sockets->local_mcast_loop[1], SOL_SOCKET, SO_SNDBUF, &sendbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Local transmit multicast loop socket send buffer size (%d bytes).", sendbuf_size);
	}


	/*
	 * Join group membership on socket
	 */
	totemip_totemip_to_sockaddr_convert(mcast_address, instance->totem_interface->ip_port, &mcast_ss, &addrlen);
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &boundto_ss, &addrlen);

	if (instance->totem_config->broadcast_use == 1) {
		unsigned int broadcast = 1;

		if ((setsockopt(sockets->mcast_recv, SOL_SOCKET,
			SO_BROADCAST, &broadcast, sizeof (broadcast))) == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"setting broadcast option failed");
			return (-1);
		}
		if ((setsockopt(sockets->mcast_send, SOL_SOCKET,
			SO_BROADCAST, &broadcast, sizeof (broadcast))) == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"setting broadcast option failed");
			return (-1);
		}
	} else {
		switch (bindnet_address->family) {
			case AF_INET:
			memset(&mreq, 0, sizeof(mreq));
			mreq.imr_multiaddr.s_addr = mcast_sin->sin_addr.s_addr;
			mreq.imr_interface.s_addr = boundto_sin->sin_addr.s_addr;
			res = setsockopt (sockets->mcast_recv, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				&mreq, sizeof (mreq));
			if (res == -1) {
				LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
					"join ipv4 multicast group failed");
				return (-1);
			}
			break;
			case AF_INET6:
			memset(&mreq6, 0, sizeof(mreq6));
			memcpy(&mreq6.ipv6mr_multiaddr, &mcast_sin6->sin6_addr, sizeof(struct in6_addr));
			mreq6.ipv6mr_interface = interface_num;

			res = setsockopt (sockets->mcast_recv, IPPROTO_IPV6, IPV6_JOIN_GROUP,
				&mreq6, sizeof (mreq6));
			if (res == -1) {
				LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
					"join ipv6 multicast group failed");
				return (-1);
			}
			break;
		}
	}

	/*
	 * Turn off multicast loopback
	 */

	flag = 0;
	switch ( bindnet_address->family ) {
		case AF_INET:
		sflag = 0;
		res = setsockopt (sockets->mcast_send, IPPROTO_IP, IP_MULTICAST_LOOP,
			&sflag, sizeof (sflag));
		break;
		case AF_INET6:
		res = setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
			&flag, sizeof (flag));
	}
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Unable to turn off multicast loopback");
		return (-1);
	}

	/*
	 * Set multicast packets TTL
	 */
	flag = instance->totem_interface->ttl;
	if (bindnet_address->family == AF_INET6) {
		res = setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
			&flag, sizeof (flag));
		if (res == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"set mcast v6 TTL failed");
			return (-1);
		}
	} else {
		sflag = flag;
		res = setsockopt(sockets->mcast_send, IPPROTO_IP, IP_MULTICAST_TTL,
			&sflag, sizeof(sflag));
		if (res == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"set mcast v4 TTL failed");
			return (-1);
		}
	}

	/*
	 * Bind to a specific interface for multicast send and receive
	 */
	switch ( bindnet_address->family ) {
		case AF_INET:
		if (setsockopt (sockets->mcast_send, IPPROTO_IP, IP_MULTICAST_IF,
			&boundto_sin->sin_addr, sizeof (boundto_sin->sin_addr)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (send)");
			return (-1);
		}
		if (setsockopt (sockets->mcast_recv, IPPROTO_IP, IP_MULTICAST_IF,
			&boundto_sin->sin_addr, sizeof (boundto_sin->sin_addr)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (recv)");
			return (-1);
		}
		break;
		case AF_INET6:
		if (setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_IF,
			&interface_num, sizeof (interface_num)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (send v6)");
			return (-1);
		}
		if (setsockopt (sockets->mcast_recv, IPPROTO_IPV6, IPV6_MULTICAST_IF,
			&interface_num, sizeof (interface_num)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (recv v6)");
			return (-1);
		}
		break;
	}

	/*
	 * Bind to multicast socket used for multicast receives
	 * This needs to happen after all of the multicast setsockopt() calls
	 * as the kernel seems to only put them into effect (for IPV6) when bind()
	 * is called.
	 */
	totemip_totemip_to_sockaddr_convert(mcast_address,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);

	retries = 0;
	while (1) {
		res = bind (sockets->mcast_recv, (struct sockaddr *)&sockaddr, addrlen);
		if (res == 0) {
			break;
		}
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"Unable to bind the socket to receive multicast packets");
		if (++retries > BIND_MAX_RETRIES) {
			break;
		}

		/*
		 * Wait for a while
		 */
		(void)poll(NULL, 0, BIND_RETRIES_INTERVAL * retries);
	}

	if (res == -1) {
		return (-1);
	}
	return 0;
}

static int totemudp_build_sockets (
	struct totemudp_instance *instance,
	struct totem_ip_address *mcast_address,
	struct totem_ip_address *bindnet_address,
	struct totemudp_socket *sockets,
	struct totem_ip_address *bound_to)
{
	int interface_num;
	int interface_up;
	int res;

	/*
	 * Determine the ip address bound to and the interface name
	 */
	res = netif_determine (instance,
		bindnet_address,
		bound_to,
		&interface_up,
		&interface_num);

	if (res == -1) {
		return (-1);
	}

	totemip_copy(&instance->my_id, bound_to);

	res = totemudp_build_sockets_ip (instance, mcast_address,
		bindnet_address, sockets, bound_to, interface_num);

	if (res == -1) {
		/* if we get here, corosync won't work anyway, so better leaving than faking to work */
		LOGSYS_PERROR (errno, instance->totemudp_log_level_error,
			"Unable to create sockets, exiting");
		exit(EXIT_FAILURE);
	}

	/* We only send out of the token socket */
	totemudp_traffic_control_set(instance, sockets->token);
	return res;
}

/*
 * Totem Network interface - also does encryption/decryption
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
int totemudp_initialize (
	qb_loop_t *poll_handle,
	void **udp_context,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	int interface_no,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address),

	void (*target_set_completed) (
		void *context))
{
	struct totemudp_instance *instance;

	instance = malloc (sizeof (struct totemudp_instance));
	if (instance == NULL) {
		return (-1);
	}

	totemudp_instance_initialize (instance);

	instance->totem_config = totem_config;
	instance->stats = stats;

	/*
	* Configure logging
	*/
	instance->totemudp_log_level_security = 1; //totem_config->totem_logging_configuration.log_level_security;
	instance->totemudp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemudp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemudp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemudp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemudp_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemudp_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	* Initialize random number generator for later use to generate salt
	*/
	instance->crypto_inst = crypto_init (totem_config->private_key,
			totem_config->private_key_len,
			totem_config->crypto_cipher_type,
			totem_config->crypto_hash_type,
			instance->totemudp_log_printf,
			instance->totemudp_log_level_security,
			instance->totemudp_log_level_notice,
			instance->totemudp_log_level_error,
			instance->totemudp_subsys_id);
	if (instance->crypto_inst == NULL) {
		free(instance);
		return (-1);
	}
	/*
	 * Initialize local variables for totemudp
	 */
	instance->totem_interface = &totem_config->interfaces[interface_no];
	totemip_copy (&instance->mcast_address, &instance->totem_interface->mcast_addr);
	memset (instance->iov_buffer, 0, FRAME_SIZE_MAX);

	instance->totemudp_poll_handle = poll_handle;

	instance->totem_interface->bindnet.nodeid = instance->totem_config->node_id;

	instance->context = context;
	instance->totemudp_deliver_fn = deliver_fn;

	instance->totemudp_iface_change_fn = iface_change_fn;

	instance->totemudp_target_set_completed = target_set_completed;

	totemip_localhost (instance->mcast_address.family, &localhost);
	localhost.nodeid = instance->totem_config->node_id;

	/*
	 * RRP layer isn't ready to receive message because it hasn't
	 * initialized yet.  Add short timer to check the interfaces.
	 */
	qb_loop_timer_add (instance->totemudp_poll_handle,
		QB_LOOP_MED,
		100*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		timer_function_netif_check_timeout,
		&instance->timer_netif_check_timeout);

	*udp_context = instance;
	return (0);
}

void *totemudp_buffer_alloc (void)
{
	return malloc (FRAME_SIZE_MAX);
}

void totemudp_buffer_release (void *ptr)
{
	return free (ptr);
}

int totemudp_processor_count_set (
	void *udp_context,
	int processor_count)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	instance->my_memb_entries = processor_count;
	qb_loop_timer_del (instance->totemudp_poll_handle,
		instance->timer_netif_check_timeout);
	if (processor_count == 1) {
		qb_loop_timer_add (instance->totemudp_poll_handle,
			QB_LOOP_MED,
			instance->totem_config->downcheck_timeout*QB_TIME_NS_IN_MSEC,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
	}

	return (res);
}

int totemudp_recv_flush (void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	struct pollfd ufd;
	int nfds;
	int res = 0;
	int i;
	int sock;

	instance->flushing = 1;

	for (i = 0; i < 2; i++) {
		sock = -1;
		if (i == 0) {
		    sock = instance->totemudp_sockets.mcast_recv;
		}
		if (i == 1) {
		    sock = instance->totemudp_sockets.local_mcast_loop[0];
		}
		assert(sock != -1);

		do {
			ufd.fd = sock;
			ufd.events = POLLIN;
			nfds = poll (&ufd, 1, 0);
			if (nfds == 1 && ufd.revents & POLLIN) {
			net_deliver_fn (sock, ufd.revents, instance);
			}
		} while (nfds == 1);
	}

	instance->flushing = 0;

	return (res);
}

int totemudp_send_flush (void *udp_context)
{
	return 0;
}

int totemudp_token_send (
	void *udp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	ucast_sendmsg (instance, &instance->token_target, msg, msg_len);

	return (res);
}
int totemudp_mcast_flush_send (
	void *udp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len);

	return (res);
}

int totemudp_mcast_noflush_send (
	void *udp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len);

	return (res);
}

extern int totemudp_iface_check (void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	timer_function_netif_check_timeout (instance);

	return (res);
}

extern void totemudp_net_mtu_adjust (void *udp_context, struct totem_config *totem_config)
{

	assert(totem_config->interface_count > 0);

	totem_config->net_mtu -= crypto_sec_header_size(totem_config->crypto_cipher_type,
							totem_config->crypto_hash_type) +
				 totemip_udpip_header_size(totem_config->interfaces[0].bindnet.family);
}

const char *totemudp_iface_print (void *udp_context)  {
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	const char *ret_char;

	ret_char = totemip_print (&instance->my_id);

	return (ret_char);
}

int totemudp_iface_get (
	void *udp_context,
	struct totem_ip_address *addr)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	memcpy (addr, &instance->my_id, sizeof (struct totem_ip_address));

	return (res);
}

int totemudp_token_target_set (
	void *udp_context,
	const struct totem_ip_address *token_target)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	memcpy (&instance->token_target, token_target,
		sizeof (struct totem_ip_address));

	instance->totemudp_target_set_completed (instance->context);

	return (res);
}

extern int totemudp_recv_mcast_empty (
	void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	unsigned int res;
	struct sockaddr_storage system_from;
	struct msghdr msg_recv;
	struct pollfd ufd;
	int nfds;
	int msg_processed = 0;
	int i;
	int sock;

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_storage);
	msg_recv.msg_iov = &instance->totemudp_iov_recv_flush;
	msg_recv.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
	msg_recv.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
	msg_recv.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
	msg_recv.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
	msg_recv.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
	msg_recv.msg_accrightslen = 0;
#endif

	for (i = 0; i < 2; i++) {
		sock = -1;
		if (i == 0) {
		    sock = instance->totemudp_sockets.mcast_recv;
		}
		if (i == 1) {
		    sock = instance->totemudp_sockets.local_mcast_loop[0];
		}
		assert(sock != -1);

		do {
			ufd.fd = sock;
			ufd.events = POLLIN;
			nfds = poll (&ufd, 1, 0);
			if (nfds == 1 && ufd.revents & POLLIN) {
				res = recvmsg (sock, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
				if (res != -1) {
					msg_processed = 1;
				} else {
					msg_processed = -1;
				}
			}
		} while (nfds == 1);
	}

	return (msg_processed);
}

