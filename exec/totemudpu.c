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

#include <qb/qbdefs.h>
#include <qb/qbloop.h>

#include <corosync/sq.h>
#include <corosync/list.h>
#include <corosync/swab.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>
#include "totemudpu.h"

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

struct totemudpu_member {
	struct list_head list;
	struct totem_ip_address member;
	int fd;
	int active;
};

struct totemudpu_instance {
	struct crypto_instance *crypto_inst;

	qb_loop_t *totemudpu_poll_handle;

	struct totem_interface *totem_interface;

	int netif_state_report;

	int netif_bind_state;

	void *context;

	void (*totemudpu_deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*totemudpu_iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address);

	void (*totemudpu_target_set_completed) (void *context);

	/*
	 * Function and data used to log messages
	 */
	int totemudpu_log_level_security;

	int totemudpu_log_level_error;

	int totemudpu_log_level_warning;

	int totemudpu_log_level_notice;

	int totemudpu_log_level_debug;

	int totemudpu_subsys_id;

	void (*totemudpu_log_printf) (
		int level,
		int subsys,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 6, 7)));

	void *udpu_context;

	char iov_buffer[FRAME_SIZE_MAX];

	struct iovec totemudpu_iov_recv;

	struct list_head member_list;

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

	struct totem_config *totem_config;

	totemsrp_stats_t *stats;

	struct totem_ip_address token_target;

	int token_socket;

	qb_loop_timer_handle timer_merge_detect_timeout;

	int send_merge_detect_message;

	unsigned int merge_detect_messages_sent_before_timeout;
};

struct work_item {
	const void *msg;
	unsigned int msg_len;
	struct totemudpu_instance *instance;
};

static int totemudpu_build_sockets (
	struct totemudpu_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *bound_to);

static int totemudpu_create_sending_socket(
	void *udpu_context,
	const struct totem_ip_address *member);

int totemudpu_member_list_rebind_ip (
	void *udpu_context);

static void totemudpu_start_merge_detect_timeout(
	void *udpu_context);

static void totemudpu_stop_merge_detect_timeout(
	void *udpu_context);

static struct totem_ip_address localhost;

static void totemudpu_instance_initialize (struct totemudpu_instance *instance)
{
	memset (instance, 0, sizeof (struct totemudpu_instance));

	instance->netif_state_report = NETIF_STATE_REPORT_UP | NETIF_STATE_REPORT_DOWN;

	instance->totemudpu_iov_recv.iov_base = instance->iov_buffer;

	instance->totemudpu_iov_recv.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);

	/*
	 * There is always atleast 1 processor
	 */
	instance->my_memb_entries = 1;

	list_init (&instance->member_list);
}

#define log_printf(level, format, args...)		\
do {							\
        instance->totemudpu_log_printf (		\
		level, instance->totemudpu_subsys_id,	\
                __FUNCTION__, __FILE__, __LINE__,	\
		(const char *)format, ##args);		\
} while (0);
#define LOGSYS_PERROR(err_num, level, fmt, args...)						\
do {												\
	char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];						\
	const char *_error_ptr = qb_strerror_r(err_num, _error_str, sizeof(_error_str));	\
        instance->totemudpu_log_printf (							\
		level, instance->totemudpu_subsys_id,						\
                __FUNCTION__, __FILE__, __LINE__,						\
		fmt ": %s (%d)", ##args, _error_ptr, err_num);				\
	} while(0)

int totemudpu_crypto_set (
	void *udpu_context,
	const char *cipher_type,
	const char *hash_type)
{

	return (0);
}


static inline void ucast_sendmsg (
	struct totemudpu_instance *instance,
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
	totemip_totemip_to_sockaddr_convert(system_to,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	memset(&msg_ucast, 0, sizeof(msg_ucast));
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
	res = sendmsg (instance->token_socket, &msg_ucast, MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_debug,
				"sendmsg(ucast) failed (non-critical)");
	}
}

static inline void mcast_sendmsg (
	struct totemudpu_instance *instance,
	const void *msg,
	unsigned int msg_len,
	int only_active)
{
	struct msghdr msg_mcast;
	int res = 0;
	size_t buf_out_len;
	unsigned char buf_out[FRAME_SIZE_MAX];
	struct iovec iovec;
	struct sockaddr_storage sockaddr;
	int addrlen;
        struct list_head *list;
	struct totemudpu_member *member;

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

	memset(&msg_mcast, 0, sizeof(msg_mcast));
	/*
	 * Build multicast message
	 */
        for (list = instance->member_list.next;
		list != &instance->member_list;
		list = list->next) {

                member = list_entry (list,
			struct totemudpu_member,
			list);

		/*
		 * Do not send multicast message if message is not "flush", member
		 * is inactive and timeout for sending merge message didn't expired.
		 */
		if (only_active && !member->active && !instance->send_merge_detect_message)
			continue ;

		totemip_totemip_to_sockaddr_convert(&member->member,
			instance->totem_interface->ip_port, &sockaddr, &addrlen);
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
		res = sendmsg (member->fd, &msg_mcast, MSG_NOSIGNAL);
		if (res < 0) {
			LOGSYS_PERROR (errno, instance->totemudpu_log_level_debug,
				"sendmsg(mcast) failed (non-critical)");
		}
	}

	if (!only_active || instance->send_merge_detect_message) {
		/*
		 * Current message was sent to all nodes
		 */
		instance->merge_detect_messages_sent_before_timeout++;
		instance->send_merge_detect_message = 0;
	}
}

int totemudpu_finalize (
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	if (instance->token_socket > 0) {
		qb_loop_poll_del (instance->totemudpu_poll_handle,
			instance->token_socket);
		close (instance->token_socket);
	}

	totemudpu_stop_merge_detect_timeout(instance);

	return (res);
}

static int net_deliver_fn (
	int fd,
	int revents,
	void *data)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)data;
	struct msghdr msg_recv;
	struct iovec *iovec;
	struct sockaddr_storage system_from;
	int bytes_received;
	int res = 0;

	iovec = &instance->totemudpu_iov_recv;

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
		log_printf (instance->totemudpu_log_level_security, "Received message has invalid digest... ignoring.");
		log_printf (instance->totemudpu_log_level_security,
			"Invalid packet data");
		iovec->iov_len = FRAME_SIZE_MAX;
		return 0;
	}
	iovec->iov_len = bytes_received;

	/*
	 * Handle incoming message
	 */
	instance->totemudpu_deliver_fn (
		instance->context,
		iovec->iov_base,
		iovec->iov_len);

	iovec->iov_len = FRAME_SIZE_MAX;
	return (0);
}

static int netif_determine (
	struct totemudpu_instance *instance,
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
	struct totemudpu_instance *instance = (struct totemudpu_instance *)data;
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

		qb_loop_timer_add (instance->totemudpu_poll_handle,
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

	if (instance->token_socket > 0) {
		qb_loop_poll_del (instance->totemudpu_poll_handle,
			instance->token_socket);
		close (instance->token_socket);
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
		qb_loop_timer_add (instance->totemudpu_poll_handle,
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
	totemudpu_build_sockets (instance,
		bind_address,
		&instance->totem_interface->boundto);

	qb_loop_poll_add (instance->totemudpu_poll_handle,
		QB_LOOP_MED,
		instance->token_socket,
		POLLIN, instance, net_deliver_fn);

	totemip_copy (&instance->my_id, &instance->totem_interface->boundto);

	/*
	 * This reports changes in the interface to the user and totemsrp
	 */
	if (instance->netif_bind_state == BIND_STATE_REGULAR) {
		if (instance->netif_state_report & NETIF_STATE_REPORT_UP) {
			log_printf (instance->totemudpu_log_level_notice,
				"The network interface [%s] is now up.",
				totemip_print (&instance->totem_interface->boundto));
			instance->netif_state_report = NETIF_STATE_REPORT_DOWN;
			instance->totemudpu_iface_change_fn (instance->context, &instance->my_id);
		}
		/*
		 * Add a timer to check for interface going down in single membership
		 */
		if (instance->my_memb_entries == 1) {
			qb_loop_timer_add (instance->totemudpu_poll_handle,
				QB_LOOP_MED,
				instance->totem_config->downcheck_timeout*QB_TIME_NS_IN_MSEC,
				(void *)instance,
				timer_function_netif_check_timeout,
				&instance->timer_netif_check_timeout);
		}

	} else {
		if (instance->netif_state_report & NETIF_STATE_REPORT_DOWN) {
			log_printf (instance->totemudpu_log_level_notice,
				"The network interface is down.");
			instance->totemudpu_iface_change_fn (instance->context, &instance->my_id);
		}
		instance->netif_state_report = NETIF_STATE_REPORT_UP;

	}
}

/* Set the socket priority to INTERACTIVE to ensure
   that our messages don't get queued behind anything else */
static void totemudpu_traffic_control_set(struct totemudpu_instance *instance, int sock)
{
#ifdef SO_PRIORITY
	int prio = 6; /* TC_PRIO_INTERACTIVE */

	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int))) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not set traffic priority");
    }
#endif
}

static int totemudpu_build_sockets_ip (
	struct totemudpu_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *bound_to,
	int interface_num)
{
	struct sockaddr_storage sockaddr;
	int addrlen;
	int res;
	unsigned int recvbuf_size;
	unsigned int optlen = sizeof (recvbuf_size);
	unsigned int retries = 0;

	/*
	 * Setup unicast socket
	 */
	instance->token_socket = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (instance->token_socket == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (instance->token_socket);
	res = fcntl (instance->token_socket, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not set non-blocking operation on token socket");
		return (-1);
	}

	/*
	 * Bind to unicast socket used for token send/receives
	 * This has the side effect of binding to the correct interface
	 */
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &sockaddr, &addrlen);
	while (1) {
		res = bind (instance->token_socket, (struct sockaddr *)&sockaddr, addrlen);
		if (res == 0) {
			break;
		}
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"bind token socket failed");
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
	 * the token_socket can receive many messages.  Allow a large number
	 * of receive messages on this socket
	 */
	recvbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	res = setsockopt (instance->token_socket, SOL_SOCKET, SO_RCVBUF,
		&recvbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_notice,
			"Could not set recvbuf size");
	}

	return 0;
}

static int totemudpu_build_sockets (
	struct totemudpu_instance *instance,
	struct totem_ip_address *bindnet_address,
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

	res = totemudpu_build_sockets_ip (instance,
		bindnet_address, bound_to, interface_num);

	if (res == -1) {
		/* if we get here, corosync won't work anyway, so better leaving than faking to work */
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_error,
					"Unable to create sockets, exiting");
		exit(EXIT_FAILURE);
	}

	/* We only send out of the token socket */
	totemudpu_traffic_control_set(instance, instance->token_socket);

	/*
	 * Rebind all members to new ips
	 */
	totemudpu_member_list_rebind_ip(instance);

	return res;
}

/*
 * Totem Network interface - also does encryption/decryption
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
int totemudpu_initialize (
	qb_loop_t *poll_handle,
	void **udpu_context,
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
	struct totemudpu_instance *instance;

	instance = malloc (sizeof (struct totemudpu_instance));
	if (instance == NULL) {
		return (-1);
	}

	totemudpu_instance_initialize (instance);

	instance->totem_config = totem_config;
	instance->stats = stats;

	/*
	* Configure logging
	*/
	instance->totemudpu_log_level_security = 1; //totem_config->totem_logging_configuration.log_level_security;
	instance->totemudpu_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemudpu_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemudpu_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemudpu_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemudpu_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemudpu_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	* Initialize random number generator for later use to generate salt
	*/
	instance->crypto_inst = crypto_init (totem_config->private_key,
		totem_config->private_key_len,
		totem_config->crypto_cipher_type,
		totem_config->crypto_hash_type,
		instance->totemudpu_log_printf,
		instance->totemudpu_log_level_security,
		instance->totemudpu_log_level_notice,
		instance->totemudpu_log_level_error,
		instance->totemudpu_subsys_id);
	if (instance->crypto_inst == NULL) {
		free(instance);
		return (-1);
	}
	/*
	 * Initialize local variables for totemudpu
	 */
	instance->totem_interface = &totem_config->interfaces[interface_no];
	memset (instance->iov_buffer, 0, FRAME_SIZE_MAX);

	instance->totemudpu_poll_handle = poll_handle;

	instance->totem_interface->bindnet.nodeid = instance->totem_config->node_id;

	instance->context = context;
	instance->totemudpu_deliver_fn = deliver_fn;

	instance->totemudpu_iface_change_fn = iface_change_fn;

	instance->totemudpu_target_set_completed = target_set_completed;

        totemip_localhost (AF_INET, &localhost);
	localhost.nodeid = instance->totem_config->node_id;

	/*
	 * RRP layer isn't ready to receive message because it hasn't
	 * initialized yet.  Add short timer to check the interfaces.
	 */
	qb_loop_timer_add (instance->totemudpu_poll_handle,
		QB_LOOP_MED,
		100*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		timer_function_netif_check_timeout,
		&instance->timer_netif_check_timeout);

	totemudpu_start_merge_detect_timeout(instance);

	*udpu_context = instance;
	return (0);
}

void *totemudpu_buffer_alloc (void)
{
	return malloc (FRAME_SIZE_MAX);
}

void totemudpu_buffer_release (void *ptr)
{
	return free (ptr);
}

int totemudpu_processor_count_set (
	void *udpu_context,
	int processor_count)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	instance->my_memb_entries = processor_count;
	qb_loop_timer_del (instance->totemudpu_poll_handle,
		instance->timer_netif_check_timeout);
	if (processor_count == 1) {
		qb_loop_timer_add (instance->totemudpu_poll_handle,
			QB_LOOP_MED,
			instance->totem_config->downcheck_timeout*QB_TIME_NS_IN_MSEC,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
	}

	return (res);
}

int totemudpu_recv_flush (void *udpu_context)
{
	int res = 0;

	return (res);
}

int totemudpu_send_flush (void *udpu_context)
{
	int res = 0;

	return (res);
}

int totemudpu_token_send (
	void *udpu_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	ucast_sendmsg (instance, &instance->token_target, msg, msg_len);

	return (res);
}
int totemudpu_mcast_flush_send (
	void *udpu_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len, 0);

	return (res);
}

int totemudpu_mcast_noflush_send (
	void *udpu_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len, 1);

	return (res);
}

extern int totemudpu_iface_check (void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	timer_function_netif_check_timeout (instance);

	return (res);
}

extern void totemudpu_net_mtu_adjust (void *udpu_context, struct totem_config *totem_config)
{

	assert(totem_config->interface_count > 0);

	totem_config->net_mtu -= crypto_sec_header_size(totem_config->crypto_cipher_type,
							totem_config->crypto_hash_type) +
				 totemip_udpip_header_size(totem_config->interfaces[0].bindnet.family);
}

const char *totemudpu_iface_print (void *udpu_context)  {
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	const char *ret_char;

	ret_char = totemip_print (&instance->my_id);

	return (ret_char);
}

int totemudpu_iface_get (
	void *udpu_context,
	struct totem_ip_address *addr)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	memcpy (addr, &instance->my_id, sizeof (struct totem_ip_address));

	return (res);
}

int totemudpu_token_target_set (
	void *udpu_context,
	const struct totem_ip_address *token_target)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	memcpy (&instance->token_target, token_target,
		sizeof (struct totem_ip_address));

	instance->totemudpu_target_set_completed (instance->context);

	return (res);
}

extern int totemudpu_recv_mcast_empty (
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	unsigned int res;
	struct sockaddr_storage system_from;
	struct msghdr msg_recv;
	struct pollfd ufd;
	int nfds;
	int msg_processed = 0;

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_storage);
	msg_recv.msg_iov = &instance->totemudpu_iov_recv;
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

	do {
		ufd.fd = instance->token_socket;
		ufd.events = POLLIN;
		nfds = poll (&ufd, 1, 0);
		if (nfds == 1 && ufd.revents & POLLIN) {
			res = recvmsg (instance->token_socket, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
			if (res != -1) {
				msg_processed = 1;
			} else {
				msg_processed = -1;
			}
		}
	} while (nfds == 1);

	return (msg_processed);
}

static int totemudpu_create_sending_socket(
	void *udpu_context,
	const struct totem_ip_address *member)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int fd;
	int res;
	unsigned int sendbuf_size;
	unsigned int optlen = sizeof (sendbuf_size);
	struct sockaddr_storage sockaddr;
	int addrlen;

	fd = socket (member->family, SOCK_DGRAM, 0);
	if (fd == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not create socket for new member");
		return (-1);
	}
	totemip_nosigpipe (fd);
	res = fcntl (fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not set non-blocking operation on token socket");
		goto error_close_fd;
	}

	/*
 	 * These sockets are used to send multicast messages, so their buffers
 	 * should be large
 	 */
	sendbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	res = setsockopt (fd, SOL_SOCKET, SO_SNDBUF,
		&sendbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_notice,
			"Could not set sendbuf size");
		/*
		 * Fail in setting sendbuf size is not fatal -> don't exit
		 */
	}

	/*
	 * Bind to sending interface
	 */
	totemip_totemip_to_sockaddr_convert(&instance->my_id, 0, &sockaddr, &addrlen);
	res = bind (fd, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"bind token socket failed");
		goto error_close_fd;
	}

	return (fd);

error_close_fd:
	close(fd);
	return (-1);
}

int totemudpu_member_add (
	void *udpu_context,
	const struct totem_ip_address *member)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	struct totemudpu_member *new_member;

	new_member = malloc (sizeof (struct totemudpu_member));
	if (new_member == NULL) {
		return (-1);
	}

	memset(new_member, 0, sizeof(*new_member));

	log_printf (LOGSYS_LEVEL_NOTICE, "adding new UDPU member {%s}",
		totemip_print(member));
	list_init (&new_member->list);
	list_add_tail (&new_member->list, &instance->member_list);
	memcpy (&new_member->member, member, sizeof (struct totem_ip_address));
	new_member->fd = totemudpu_create_sending_socket(udpu_context, member);
	new_member->active = 0;

	return (0);
}

int totemudpu_member_remove (
	void *udpu_context,
	const struct totem_ip_address *token_target)
{
	int found = 0;
	struct list_head *list;
	struct totemudpu_member *member;

	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	/*
	 * Find the member to remove and close its socket
	 */
	for (list = instance->member_list.next;
		list != &instance->member_list;
		list = list->next) {

		member = list_entry (list,
			struct totemudpu_member,
			list);

		if (totemip_compare (token_target, &member->member)==0) {
			log_printf(LOGSYS_LEVEL_NOTICE,
				"removing UDPU member {%s}",
				totemip_print(&member->member));

			if (member->fd > 0) {
				log_printf(LOGSYS_LEVEL_DEBUG,
					"Closing socket to: {%s}",
					totemip_print(&member->member));
				qb_loop_poll_del (instance->totemudpu_poll_handle,
					member->fd);
				close (member->fd);
			}
			found = 1;
			break;
		}
	}

	/*
	 * Delete the member from the list
	 */
	if (found) {
		list_del (list);
	}

	instance = NULL;
	return (0);
}

int totemudpu_member_list_rebind_ip (
	void *udpu_context)
{
	struct list_head *list;
	struct totemudpu_member *member;

	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	for (list = instance->member_list.next;
		list != &instance->member_list;
		list = list->next) {

		member = list_entry (list,
			struct totemudpu_member,
			list);

		if (member->fd > 0) {
			close (member->fd);
		}

		member->fd = totemudpu_create_sending_socket(udpu_context, &member->member);
	}

	return (0);
}

int totemudpu_member_set_active (
	void *udpu_context,
	const struct totem_ip_address *member_ip,
	int active)
{
	struct list_head *list;
	struct totemudpu_member *member;
	int addr_found = 0;

	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	/*
	 * Find the member to set active flag
	 */
	for (list = instance->member_list.next; list != &instance->member_list;	list = list->next) {
		member = list_entry (list, struct totemudpu_member, list);

		if (totemip_compare (member_ip, &member->member) == 0) {
			log_printf(LOGSYS_LEVEL_DEBUG,
			    "Marking UDPU member %s %s",
			    totemip_print(&member->member),
			    (active ? "active" : "inactive"));

			member->active = active;
			addr_found = 1;

			break;
		}
	}

	if (!addr_found) {
		log_printf(LOGSYS_LEVEL_DEBUG,
		    "Can't find UDPU member %s (should be marked as %s)",
			    totemip_print(member_ip),
			    (active ? "active" : "inactive"));
	}

	return (0);
}

static void timer_function_merge_detect_timeout (
	void *data)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)data;

	if (instance->merge_detect_messages_sent_before_timeout == 0) {
		instance->send_merge_detect_message = 1;
	}

	instance->merge_detect_messages_sent_before_timeout = 0;

	totemudpu_start_merge_detect_timeout(instance);
}

static void totemudpu_start_merge_detect_timeout(
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	qb_loop_timer_add(instance->totemudpu_poll_handle,
	    QB_LOOP_MED,
	    instance->totem_config->merge_timeout * 2 * QB_TIME_NS_IN_MSEC,
	    (void *)instance,
	    timer_function_merge_detect_timeout,
	    &instance->timer_merge_detect_timeout);

}

static void totemudpu_stop_merge_detect_timeout(
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	qb_loop_timer_del(instance->totemudpu_poll_handle,
	    instance->timer_merge_detect_timeout);
}
