/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2008 Red Hat, Inc.
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
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>

#include "coropoll.h"
#include "totemnet.h"
#include "wthread.h"
#include "../include/queue.h"
#include "../include/sq.h"
#include "../include/list.h"
#include "../include/hdb.h"
#include "swab.h"

#include "crypto.h"

#define MCAST_SOCKET_BUFFER_SIZE (TRANSMITS_ALLOWED * FRAME_SIZE_MAX) 

#define NETIF_STATE_REPORT_UP		1	
#define NETIF_STATE_REPORT_DOWN		2

#define BIND_STATE_UNBOUND	0
#define BIND_STATE_REGULAR	1
#define BIND_STATE_LOOPBACK	2

#define HMAC_HASH_SIZE 20
struct security_header {
	unsigned char hash_digest[HMAC_HASH_SIZE]; /* The hash *MUST* be first in the data structure */
	unsigned char salt[16]; /* random number */
	char msg[0];
} __attribute__((packed));

struct totemnet_mcast_thread_state {
	unsigned char iobuf[FRAME_SIZE_MAX];
	prng_state prng_state;
};

struct totemnet_socket {
	int mcast_recv;
	int mcast_send;
	int token;
};

struct totemnet_instance {
	hmac_state totemnet_hmac_state;

	prng_state totemnet_prng_state;

	unsigned char totemnet_private_key[1024];

	unsigned int totemnet_private_key_len;

	poll_handle totemnet_poll_handle;

	struct totem_interface *totem_interface;

	int netif_state_report;

	int netif_bind_state;

	struct worker_thread_group worker_thread_group;

	void *context;

	void (*totemnet_deliver_fn) (
		void *context,
		void *msg,
		int msg_len);

	void (*totemnet_iface_change_fn) (
		void *context,
		struct totem_ip_address *iface_address);

	/*
	 * Function and data used to log messages
	 */
	int totemnet_log_level_security;

	int totemnet_log_level_error;

	int totemnet_log_level_warning;

	int totemnet_log_level_notice;

	int totemnet_log_level_debug;

	void (*totemnet_log_printf) (char *file, int line, int level, char *format, ...) __attribute__((format(printf, 4, 5)));

	totemnet_handle handle;

	char iov_buffer[FRAME_SIZE_MAX];

	char iov_buffer_flush[FRAME_SIZE_MAX];

	struct iovec totemnet_iov_recv;

	struct iovec totemnet_iov_recv_flush;

	struct totemnet_socket totemnet_sockets;

	struct totem_ip_address mcast_address;

	int stats_sent;

	int stats_recv;

	int stats_delv;

	int stats_remcasts;

	int stats_orf_token;

	struct timeval stats_tv_start;

	struct totem_ip_address my_id;

	int firstrun;

	poll_timer_handle timer_netif_check_timeout;

	unsigned int my_memb_entries;

	int flushing;

	struct totem_config *totem_config;

	struct totem_ip_address token_target;
};

struct work_item {
	struct iovec iovec[20];
	int iov_len;
	struct totemnet_instance *instance;
};

static void netif_down_check (struct totemnet_instance *instance);

static int totemnet_build_sockets (
	struct totemnet_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *mcastaddress,
	struct totemnet_socket *sockets,
	struct totem_ip_address *bound_to);

static struct totem_ip_address localhost;

/*
 * All instances in one database
 */
static struct hdb_handle_database totemnet_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0,
	.mutex		= PTHREAD_MUTEX_INITIALIZER
};

static void totemnet_instance_initialize (struct totemnet_instance *instance)
{
	memset (instance, 0, sizeof (struct totemnet_instance));

	instance->netif_state_report = NETIF_STATE_REPORT_UP | NETIF_STATE_REPORT_DOWN;

	instance->totemnet_iov_recv.iov_base = instance->iov_buffer;

	instance->totemnet_iov_recv.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);
	instance->totemnet_iov_recv_flush.iov_base = instance->iov_buffer_flush;

	instance->totemnet_iov_recv_flush.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);

	/*
	 * There is always atleast 1 processor
	 */
	instance->my_memb_entries = 1;
}

#define log_printf(level, format, args...) \
    instance->totemnet_log_printf (__FILE__, __LINE__, level, format, ##args)

static int authenticate_and_decrypt (
	struct totemnet_instance *instance,
	struct iovec *iov)
{
	unsigned char keys[48];
	struct security_header *header = iov[0].iov_base;
	prng_state keygen_prng_state;
	prng_state stream_prng_state;
	unsigned char *hmac_key = &keys[32];
	unsigned char *cipher_key = &keys[16];
	unsigned char *initial_vector = &keys[0];
	unsigned char digest_comparison[HMAC_HASH_SIZE];
	unsigned long len;

	/*
	 * Generate MAC, CIPHER, IV keys from private key
	 */
	memset (keys, 0, sizeof (keys));
	sober128_start (&keygen_prng_state);
	sober128_add_entropy (instance->totemnet_private_key,
		instance->totemnet_private_key_len, &keygen_prng_state);	
	sober128_add_entropy (header->salt, sizeof (header->salt), &keygen_prng_state);

	sober128_read (keys, sizeof (keys), &keygen_prng_state);

	/*
	 * Setup stream cipher
	 */
	sober128_start (&stream_prng_state);
	sober128_add_entropy (cipher_key, 16, &stream_prng_state);	
	sober128_add_entropy (initial_vector, 16, &stream_prng_state);	

	/*
	 * Authenticate contents of message
	 */
	hmac_init (&instance->totemnet_hmac_state, DIGEST_SHA1, hmac_key, 16);

	hmac_process (&instance->totemnet_hmac_state, 
		iov->iov_base + HMAC_HASH_SIZE,
		iov->iov_len - HMAC_HASH_SIZE);

	len = hash_descriptor[DIGEST_SHA1]->hashsize;
	assert (HMAC_HASH_SIZE >= len);
	hmac_done (&instance->totemnet_hmac_state, digest_comparison, &len);

	if (memcmp (digest_comparison, header->hash_digest, len) != 0) {
		log_printf (instance->totemnet_log_level_security, "Received message has invalid digest... ignoring.\n");
		return (-1);
	}
	
	/*
	 * Decrypt the contents of the message with the cipher key
	 */
	sober128_read (iov->iov_base + sizeof (struct security_header),
		iov->iov_len - sizeof (struct security_header),
		&stream_prng_state);

	return (0);
}
static void encrypt_and_sign_worker (
	struct totemnet_instance *instance,
	unsigned char *buf,
	int *buf_len,
	struct iovec *iovec,
	int iov_len,
	prng_state *prng_state_in)
{
	int i;
	unsigned char *addr;
	unsigned char keys[48];
	struct security_header *header;
	unsigned char *hmac_key = &keys[32];
	unsigned char *cipher_key = &keys[16];
	unsigned char *initial_vector = &keys[0];
	unsigned long len;
	int outlen = 0;
	hmac_state hmac_state;
	prng_state keygen_prng_state;
	prng_state stream_prng_state;

	header = (struct security_header *)buf;
	addr = buf + sizeof (struct security_header);

	memset (keys, 0, sizeof (keys));
	memset (header->salt, 0, sizeof (header->salt));

	/*
	 * Generate MAC, CIPHER, IV keys from private key
	 */
	sober128_read (header->salt, sizeof (header->salt), prng_state_in);
	sober128_start (&keygen_prng_state);
	sober128_add_entropy (instance->totemnet_private_key,
		instance->totemnet_private_key_len,
		&keygen_prng_state);	
	sober128_add_entropy (header->salt, sizeof (header->salt),
		&keygen_prng_state);

	sober128_read (keys, sizeof (keys), &keygen_prng_state);

	/*
	 * Setup stream cipher
	 */
	sober128_start (&stream_prng_state);
	sober128_add_entropy (cipher_key, 16, &stream_prng_state);	
	sober128_add_entropy (initial_vector, 16, &stream_prng_state);	

	outlen = sizeof (struct security_header);
	/*
	 * Copy remainder of message, then encrypt it
	 */
	for (i = 1; i < iov_len; i++) {
		memcpy (addr, iovec[i].iov_base, iovec[i].iov_len);
		addr += iovec[i].iov_len;
		outlen += iovec[i].iov_len;
	}

	/*
 	 * Encrypt message by XORing stream cipher data
	 */
	sober128_read (buf + sizeof (struct security_header),
		outlen - sizeof (struct security_header),
		&stream_prng_state);

	memset (&hmac_state, 0, sizeof (hmac_state));

	/*
	 * Sign the contents of the message with the hmac key and store signature in message
	 */
	hmac_init (&hmac_state, DIGEST_SHA1, hmac_key, 16);

	hmac_process (&hmac_state, 
		buf + HMAC_HASH_SIZE,
		outlen - HMAC_HASH_SIZE);

	len = hash_descriptor[DIGEST_SHA1]->hashsize;

	hmac_done (&hmac_state, header->hash_digest, &len);

	*buf_len = outlen;
}

static inline void ucast_sendmsg (
	struct totemnet_instance *instance,
	struct totem_ip_address *system_to,
	struct iovec *iovec_in,
	int iov_len_in)
{
	struct msghdr msg_ucast;
	int res = 0;
	int buf_len;
	unsigned char sheader[sizeof (struct security_header)];
	unsigned char encrypt_data[FRAME_SIZE_MAX];
	struct iovec iovec_encrypt[20];
	struct iovec *iovec_sendmsg;
	struct sockaddr_storage sockaddr;
	int iov_len;
	int addrlen;

	if (instance->totem_config->secauth == 1) {

		iovec_encrypt[0].iov_base = sheader;
		iovec_encrypt[0].iov_len = sizeof (struct security_header);
		memcpy (&iovec_encrypt[1], &iovec_in[0],
			sizeof (struct iovec) * iov_len_in);

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign_worker (
			instance,
			encrypt_data,
			&buf_len,
			iovec_encrypt,
			iov_len_in + 1,
			&instance->totemnet_prng_state);

		iovec_encrypt[0].iov_base = encrypt_data;
		iovec_encrypt[0].iov_len = buf_len;
		iovec_sendmsg = &iovec_encrypt[0];
		iov_len = 1;
	} else {
		iovec_sendmsg = iovec_in;
		iov_len = iov_len_in;
	}

	/*
	 * Build unicast message
	 */
	totemip_totemip_to_sockaddr_convert(system_to,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	msg_ucast.msg_name = &sockaddr;
	msg_ucast.msg_namelen = addrlen;
	msg_ucast.msg_iov = iovec_sendmsg;
	msg_ucast.msg_iovlen = iov_len;
	msg_ucast.msg_control = 0;
	msg_ucast.msg_controllen = 0;
	msg_ucast.msg_flags = 0;

	/*
	 * Transmit multicast message
	 * An error here is recovered by totemsrp
	 */
	res = sendmsg (instance->totemnet_sockets.mcast_send, &msg_ucast,
		MSG_NOSIGNAL);
}

static inline void mcast_sendmsg (
	struct totemnet_instance *instance,
	struct iovec *iovec_in,
	int iov_len_in)
{
	struct msghdr msg_mcast;
	int res = 0;
	int buf_len;
	unsigned char sheader[sizeof (struct security_header)];
	unsigned char encrypt_data[FRAME_SIZE_MAX];
	struct iovec iovec_encrypt[20];
	struct iovec *iovec_sendmsg;
	struct sockaddr_storage sockaddr;
	int iov_len;
	int addrlen;

	if (instance->totem_config->secauth == 1) {

		iovec_encrypt[0].iov_base = sheader;
		iovec_encrypt[0].iov_len = sizeof (struct security_header);
		memcpy (&iovec_encrypt[1], &iovec_in[0],
			sizeof (struct iovec) * iov_len_in);

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign_worker (
			instance,
			encrypt_data,
			&buf_len,
			iovec_encrypt,
			iov_len_in + 1,
			&instance->totemnet_prng_state);

		iovec_encrypt[0].iov_base = encrypt_data;
		iovec_encrypt[0].iov_len = buf_len;
		iovec_sendmsg = &iovec_encrypt[0];
		iov_len = 1;
	} else {
		iovec_sendmsg = iovec_in;
		iov_len = iov_len_in;
	}

	/*
	 * Build multicast message
	 */
	totemip_totemip_to_sockaddr_convert(&instance->mcast_address,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	msg_mcast.msg_name = &sockaddr;
	msg_mcast.msg_namelen = addrlen;
	msg_mcast.msg_iov = iovec_sendmsg;
	msg_mcast.msg_iovlen = iov_len;
	msg_mcast.msg_control = 0;
	msg_mcast.msg_controllen = 0;
	msg_mcast.msg_flags = 0;

	/*
	 * Transmit multicast message
	 * An error here is recovered by totemsrp
	 */
	res = sendmsg (instance->totemnet_sockets.mcast_send, &msg_mcast,
		MSG_NOSIGNAL);
}

static void totemnet_mcast_thread_state_constructor (
	void *totemnet_mcast_thread_state_in)
{
	struct totemnet_mcast_thread_state *totemnet_mcast_thread_state =
		(struct totemnet_mcast_thread_state *)totemnet_mcast_thread_state_in;
	memset (totemnet_mcast_thread_state, 0,
		sizeof (totemnet_mcast_thread_state));

	rng_make_prng (128, PRNG_SOBER,
		&totemnet_mcast_thread_state->prng_state, NULL);
}


static void totemnet_mcast_worker_fn (void *thread_state, void *work_item_in)
{
	struct work_item *work_item = (struct work_item *)work_item_in;
	struct totemnet_mcast_thread_state *totemnet_mcast_thread_state =
		(struct totemnet_mcast_thread_state *)thread_state;
	struct totemnet_instance *instance = work_item->instance;
	struct msghdr msg_mcast;
	unsigned char sheader[sizeof (struct security_header)];
	int res = 0;
	int buf_len;
	struct iovec iovec_encrypted;
	struct iovec *iovec_sendmsg;
	struct sockaddr_storage sockaddr;
	unsigned int iovs;
	int addrlen;

	if (instance->totem_config->secauth == 1) {
		memmove (&work_item->iovec[1], &work_item->iovec[0],
			work_item->iov_len * sizeof (struct iovec));
		work_item->iovec[0].iov_base = sheader;
		work_item->iovec[0].iov_len = sizeof (struct security_header);

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign_worker (
			instance,
			totemnet_mcast_thread_state->iobuf, &buf_len,
			work_item->iovec, work_item->iov_len + 1,
			&totemnet_mcast_thread_state->prng_state);

			iovec_sendmsg = &iovec_encrypted;
			iovec_sendmsg->iov_base = totemnet_mcast_thread_state->iobuf;
			iovec_sendmsg->iov_len = buf_len;
			iovs = 1;
	} else {
		iovec_sendmsg = work_item->iovec;
		iovs = work_item->iov_len;
	}

	totemip_totemip_to_sockaddr_convert(&instance->mcast_address,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);

	msg_mcast.msg_name = &sockaddr;
	msg_mcast.msg_namelen = addrlen;
	msg_mcast.msg_iov = iovec_sendmsg;
	msg_mcast.msg_iovlen = iovs;
	msg_mcast.msg_control = 0;
	msg_mcast.msg_controllen = 0;
	msg_mcast.msg_flags = 0;

	/*
	 * Transmit multicast message
	 * An error here is recovered by totemnet
	 */
	res = sendmsg (instance->totemnet_sockets.mcast_send, &msg_mcast,
		MSG_NOSIGNAL);
	if (res > 0) {
		instance->stats_sent += res;
	}
}

int totemnet_finalize (
	totemnet_handle handle)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	worker_thread_group_exit (&instance->worker_thread_group);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

/*
 * Only designed to work with a message with one iov
 */

static int net_deliver_fn (
	poll_handle handle,
	int fd,
	int revents,
	void *data)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)data;
	struct msghdr msg_recv;
	struct iovec *iovec;
	struct security_header *security_header;
	struct sockaddr_storage system_from;
	int bytes_received;
	int res = 0;
	unsigned char *msg_offset;
	unsigned int size_delv;

	if (instance->flushing == 1) {
		iovec = &instance->totemnet_iov_recv_flush;
	} else {
		iovec = &instance->totemnet_iov_recv;
	}

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_storage);
	msg_recv.msg_iov = iovec;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;

	bytes_received = recvmsg (fd, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (bytes_received == -1) {
		return (0);
	} else {
		instance->stats_recv += bytes_received;
	}

	if ((instance->totem_config->secauth == 1) &&
		(bytes_received < sizeof (struct security_header))) {

		log_printf (instance->totemnet_log_level_security, "Received message is too short...  ignoring %d.\n", bytes_received);
		return (0);
	}

	security_header = (struct security_header *)iovec->iov_base;

	iovec->iov_len = bytes_received;
	if (instance->totem_config->secauth == 1) {
		/*
		 * Authenticate and if authenticated, decrypt datagram
		 */

		res = authenticate_and_decrypt (instance, iovec);
		if (res == -1) {
			log_printf (instance->totemnet_log_level_security,
				"Invalid packet data\n");
			iovec->iov_len = FRAME_SIZE_MAX;
			return 0;
		}
		msg_offset = iovec->iov_base +
			sizeof (struct security_header);
		size_delv = bytes_received - sizeof (struct security_header);
	} else {
		msg_offset = iovec->iov_base;
		size_delv = bytes_received;
	}

	/*
	 * Handle incoming message
	 */
	instance->totemnet_deliver_fn (
		instance->context,
		msg_offset,
		size_delv);
		
	iovec->iov_len = FRAME_SIZE_MAX;
	return (0);
}

static int netif_determine (
	struct totemnet_instance *instance,
	struct totem_ip_address *bindnet,
	struct totem_ip_address *bound_to,
	int *interface_up,
	int *interface_num)
{
	int res;

	res = totemip_iface_check (bindnet, bound_to,
		interface_up, interface_num);

	/*
	 * If the desired binding is to an IPV4 network and nodeid isn't
	 * specified, retrieve the node id from this_ip network address
	 *
	 * IPV6 networks must have a node ID specified since the node id
	 * field is only 32 bits.
	 */
	if (bound_to->family == AF_INET && bound_to->nodeid == 0) {
		memcpy (&bound_to->nodeid, bound_to->addr, sizeof (int));
	}

	return (res);
}
	

/*
 * If the interface is up, the sockets for totem are built.  If the interface is down
 * this function is requeued in the timer list to retry building the sockets later.
 */
static void timer_function_netif_check_timeout (
	void *data)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)data;
	int res;
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

		poll_timer_add (instance->totemnet_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);

		/*
		 * Add a timer to check for a downed regular interface
		 */
		return;
	}

	if (instance->totemnet_sockets.mcast_recv > 0) {
		close (instance->totemnet_sockets.mcast_recv);
	 	poll_dispatch_delete (instance->totemnet_poll_handle,
			instance->totemnet_sockets.mcast_recv);
	}
	if (instance->totemnet_sockets.mcast_send > 0) {
		close (instance->totemnet_sockets.mcast_send);
	}
	if (instance->totemnet_sockets.token > 0) {
		close (instance->totemnet_sockets.token);
		poll_dispatch_delete (instance->totemnet_poll_handle,
			instance->totemnet_sockets.token);
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
		poll_timer_add (instance->totemnet_poll_handle,
			instance->totem_config->downcheck_timeout,
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
	res = totemnet_build_sockets (instance,
		&instance->mcast_address,
		bind_address,
		&instance->totemnet_sockets,
		&instance->totem_interface->boundto);

	poll_dispatch_add (
		instance->totemnet_poll_handle,
		instance->totemnet_sockets.mcast_recv,
		POLLIN, instance, net_deliver_fn);

	poll_dispatch_add (
		instance->totemnet_poll_handle,
		instance->totemnet_sockets.token,
		POLLIN, instance, net_deliver_fn);

	totemip_copy (&instance->my_id, &instance->totem_interface->boundto);

	/*
	 * This reports changes in the interface to the user and totemsrp
	 */
	if (instance->netif_bind_state == BIND_STATE_REGULAR) {
		if (instance->netif_state_report & NETIF_STATE_REPORT_UP) {
			log_printf (instance->totemnet_log_level_notice,
				"The network interface [%s] is now up.\n",
				totemip_print (&instance->totem_interface->boundto));
			instance->netif_state_report = NETIF_STATE_REPORT_DOWN;
			instance->totemnet_iface_change_fn (instance->context, &instance->my_id);
		}
		/*
		 * Add a timer to check for interface going down in single membership
		 */
		if (instance->my_memb_entries == 1) {
			poll_timer_add (instance->totemnet_poll_handle,
				instance->totem_config->downcheck_timeout,
				(void *)instance,
				timer_function_netif_check_timeout,
				&instance->timer_netif_check_timeout);
		}

	} else {		
		if (instance->netif_state_report & NETIF_STATE_REPORT_DOWN) {
			log_printf (instance->totemnet_log_level_notice,
				"The network interface is down.\n");
			instance->totemnet_iface_change_fn (instance->context, &instance->my_id);
		}
		instance->netif_state_report = NETIF_STATE_REPORT_UP;

	}
}


/*
 * Check if an interface is down and reconfigure
 * totemnet waiting for it to come back up
 */
static void netif_down_check (struct totemnet_instance *instance)
{
	timer_function_netif_check_timeout (instance);
}

/* Set the socket priority to INTERACTIVE to ensure
   that our messages don't get queued behind anything else */
static void totemnet_traffic_control_set(struct totemnet_instance *instance, int sock)
{
#ifdef SO_PRIORITY
    int prio = 6; /* TC_PRIO_INTERACTIVE */

    if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int)))
		log_printf (instance->totemnet_log_level_warning, "Could not set traffic priority. (%s)\n", strerror (errno));
#endif
}

static int totemnet_build_sockets_ip (
	struct totemnet_instance *instance,
	struct totem_ip_address *mcast_address,
	struct totem_ip_address *bindnet_address,
	struct totemnet_socket *sockets,
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
	int addrlen;
	int res;
	int flag;
	
	/*
	 * Create multicast recv socket
	 */
	sockets->mcast_recv = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->mcast_recv == -1) {
		perror ("socket");
		return (-1);
	}

	totemip_nosigpipe (sockets->mcast_recv);
	res = fcntl (sockets->mcast_recv, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		log_printf (instance->totemnet_log_level_warning, "Could not set non-blocking operation on multicast socket: %s\n", strerror (errno));
		return (-1);
	}

	/* 
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->mcast_recv, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) { 
	 	perror("setsockopt reuseaddr");
		return (-1);
	}

	/*
	 * Bind to multicast socket used for multicast receives
	 */
	totemip_totemip_to_sockaddr_convert(mcast_address,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	res = bind (sockets->mcast_recv, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		perror ("bind mcast recv socket failed");
		return (-1);
	}

	/*
	 * Setup mcast send socket
	 */
	sockets->mcast_send = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->mcast_send == -1) {
		perror ("socket");
		return (-1);
	}

	totemip_nosigpipe (sockets->mcast_send);
	res = fcntl (sockets->mcast_send, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		log_printf (instance->totemnet_log_level_warning, "Could not set non-blocking operation on multicast socket: %s\n", strerror (errno));
		return (-1);
	}

	/* 
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->mcast_send, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) { 
	 	perror("setsockopt reuseaddr");
		return (-1);
	}

	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port - 1,
		&sockaddr, &addrlen);
	res = bind (sockets->mcast_send, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		perror ("bind mcast send socket failed");
		return (-1);
	}

	/*
	 * Setup unicast socket
	 */
	sockets->token = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->token == -1) {
		perror ("socket2");
		return (-1);
	}

	totemip_nosigpipe (sockets->token);
	res = fcntl (sockets->token, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		log_printf (instance->totemnet_log_level_warning, "Could not set non-blocking operation on token socket: %s\n", strerror (errno));
		return (-1);
	}

	/* 
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->token, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) { 
	 	perror("setsockopt reuseaddr");
		return (-1);
	}

	/*
	 * Bind to unicast socket used for token send/receives
	 * This has the side effect of binding to the correct interface
	 */
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &sockaddr, &addrlen);
	res = bind (sockets->token, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		perror ("bind token socket failed");
		return (-1);
	}

	recvbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	sendbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	/*
	 * Set buffer sizes to avoid overruns
	 */
	 res = setsockopt (sockets->mcast_recv, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, optlen);
	 res = setsockopt (sockets->mcast_send, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, optlen);

	res = getsockopt (sockets->mcast_recv, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, &optlen);
	if (res == 0) {
	 	log_printf (instance->totemnet_log_level_notice,
			"Receive multicast socket recv buffer size (%d bytes).\n", recvbuf_size);
	}

	res = getsockopt (sockets->mcast_send, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemnet_log_level_notice,
			"Transmit multicast socket send buffer size (%d bytes).\n", sendbuf_size);
	}

	/*
	 * Join group membership on socket
	 */
	totemip_totemip_to_sockaddr_convert(mcast_address, instance->totem_interface->ip_port, &mcast_ss, &addrlen);
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &boundto_ss, &addrlen);

	switch ( bindnet_address->family ) {
		case AF_INET:
		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr.s_addr = mcast_sin->sin_addr.s_addr;
		mreq.imr_interface.s_addr = boundto_sin->sin_addr.s_addr;
		res = setsockopt (sockets->mcast_recv, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			&mreq, sizeof (mreq));
		if (res == -1) {
			perror ("join ipv4 multicast group failed");
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
			perror ("join ipv6 multicast group failed");
			return (-1);
		}
		break;
	}
	
	/*
	 * Turn on multicast loopback
	 */

	flag = 1;
	switch ( bindnet_address->family ) {
		case AF_INET:
		res = setsockopt (sockets->mcast_send, IPPROTO_IP, IP_MULTICAST_LOOP,
			&flag, sizeof (flag));
		break;
		case AF_INET6:
		res = setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
			&flag, sizeof (flag));
	}
	if (res == -1) {
		perror ("turn off loopback");
		return (-1);
	}

	/*
	 * Set multicast packets TTL
	 */

	if ( bindnet_address->family == AF_INET6 )
	{
		flag = 255;
		res = setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
			&flag, sizeof (flag));
		if (res == -1) {
			perror ("setp mcast hops");
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
			perror ("cannot select interface");
			return (-1);
		}
		if (setsockopt (sockets->mcast_recv, IPPROTO_IP, IP_MULTICAST_IF,
			&boundto_sin->sin_addr, sizeof (boundto_sin->sin_addr)) < 0) {
			perror ("cannot select interface");
			return (-1);
		}
		break;
		case AF_INET6:
		if (setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_IF,
			&interface_num, sizeof (interface_num)) < 0) {
			perror ("cannot select interface");
			return (-1);
		}
		if (setsockopt (sockets->mcast_recv, IPPROTO_IPV6, IPV6_MULTICAST_IF,
			&interface_num, sizeof (interface_num)) < 0) {
			perror ("cannot select interface");
			return (-1);
		}
		break;
	}
	
	return 0;
}

static int totemnet_build_sockets (
	struct totemnet_instance *instance,
	struct totem_ip_address *mcast_address,
	struct totem_ip_address *bindnet_address,
	struct totemnet_socket *sockets,
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

	res = totemnet_build_sockets_ip (instance, mcast_address,
		bindnet_address, sockets, bound_to, interface_num);

	/* We only send out of the token socket */
	totemnet_traffic_control_set(instance, sockets->token);
	return res;
}
	
/*
 * Totem Network interface - also does encryption/decryption
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
int totemnet_initialize (
	poll_handle poll_handle,
	totemnet_handle *handle,
	struct totem_config *totem_config,
	int interface_no,
	void *context,

	void (*deliver_fn) (
		void *context,
		void *msg,
		int msg_len),

	void (*iface_change_fn) (
		void *context,
		struct totem_ip_address *iface_address))
{
	struct totemnet_instance *instance;
	unsigned int res;

	res = hdb_handle_create (&totemnet_instance_database,
		sizeof (struct totemnet_instance), handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&totemnet_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}

	totemnet_instance_initialize (instance);

	instance->totem_config = totem_config;
	/*
	* Configure logging
	*/
	instance->totemnet_log_level_security = 1; //totem_config->totem_logging_configuration.log_level_security;
	instance->totemnet_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemnet_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemnet_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemnet_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemnet_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	* Initialize random number generator for later use to generate salt
	*/
	memcpy (instance->totemnet_private_key, totem_config->private_key,
		totem_config->private_key_len);

	instance->totemnet_private_key_len = totem_config->private_key_len;

        rng_make_prng (128, PRNG_SOBER, &instance->totemnet_prng_state, NULL);

	/*
	 * Initialize local variables for totemnet
	 */
	instance->totem_interface = &totem_config->interfaces[interface_no];
	totemip_copy (&instance->mcast_address, &instance->totem_interface->mcast_addr);
	memset (instance->iov_buffer, 0, FRAME_SIZE_MAX);

	/*
	* If threaded send requested, initialize thread group data structure
	*/
	if (totem_config->threads) {
		worker_thread_group_init (
			&instance->worker_thread_group,
			totem_config->threads, 128,
			sizeof (struct work_item),
			sizeof (struct totemnet_mcast_thread_state),
			totemnet_mcast_thread_state_constructor,
			totemnet_mcast_worker_fn);
	}

	instance->totemnet_poll_handle = poll_handle;

	instance->totem_interface->bindnet.nodeid = instance->totem_config->node_id;

	instance->context = context;
	instance->totemnet_deliver_fn = deliver_fn;

	instance->totemnet_iface_change_fn = iface_change_fn;

	instance->handle = *handle;

	rng_make_prng (128, PRNG_SOBER, &instance->totemnet_prng_state, NULL);

	totemip_localhost (instance->mcast_address.family, &localhost);

	netif_down_check (instance);

error_exit:
	hdb_handle_put (&totemnet_instance_database, *handle);
	return (0);

error_destroy:
	hdb_handle_destroy (&totemnet_instance_database, *handle);
	return (-1);
}

int totemnet_processor_count_set (
	totemnet_handle handle,
	int processor_count)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	instance->my_memb_entries = processor_count;
	poll_timer_delete (instance->totemnet_poll_handle,
		instance->timer_netif_check_timeout);
	if (processor_count == 1) {
		poll_timer_add (instance->totemnet_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
	}
	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_recv_flush (totemnet_handle handle)
{
	struct totemnet_instance *instance;
	struct pollfd ufd;
	int nfds;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	instance->flushing = 1;

	do {
		ufd.fd = instance->totemnet_sockets.mcast_recv;
		ufd.events = POLLIN;
		nfds = poll (&ufd, 1, 0);
		if (nfds == 1 && ufd.revents & POLLIN) {
		net_deliver_fn (0, instance->totemnet_sockets.mcast_recv,
			ufd.revents, instance);
		}
	} while (nfds == 1);

	instance->flushing = 0;

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_send_flush (totemnet_handle handle)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	worker_thread_group_wait (&instance->worker_thread_group);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_token_send (
	totemnet_handle handle,
	struct iovec *iovec,
	int iov_len)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	ucast_sendmsg (instance, &instance->token_target, iovec, iov_len);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}
int totemnet_mcast_flush_send (
	totemnet_handle handle,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	mcast_sendmsg (instance, iovec, iov_len);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_mcast_noflush_send (
	totemnet_handle handle,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct totemnet_instance *instance;
	struct work_item work_item;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	if (instance->totem_config->threads) {
		memcpy (&work_item.iovec[0], iovec, iov_len * sizeof (struct iovec));
		work_item.iov_len = iov_len;
		work_item.instance = instance;

		worker_thread_group_work_add (&instance->worker_thread_group,
			&work_item);         
	} else {
		mcast_sendmsg (instance, iovec, iov_len);
	}
	
	hdb_handle_put (&totemnet_instance_database, handle);
error_exit:
	return (res);
}

extern int totemnet_iface_check (totemnet_handle handle)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	timer_function_netif_check_timeout (instance);

	hdb_handle_put (&totemnet_instance_database, handle);
error_exit:
	return (res);
}

extern void totemnet_net_mtu_adjust (struct totem_config *totem_config)
{
#define UDPIP_HEADER_SIZE (20 + 8) /* 20 bytes for ip 8 bytes for udp */
	if (totem_config->secauth == 1) {
		totem_config->net_mtu -= sizeof (struct security_header) +
			UDPIP_HEADER_SIZE;
	} else {
		totem_config->net_mtu -= UDPIP_HEADER_SIZE;
	}
}

char *totemnet_iface_print (totemnet_handle handle)  {
	struct totemnet_instance *instance;
	int res = 0;
	char *ret_char;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		ret_char = "Invalid totemnet handle";
		goto error_exit;
	}
	
	ret_char = (char *)totemip_print (&instance->my_id);

	hdb_handle_put (&totemnet_instance_database, handle);
error_exit:
	return (ret_char);
}

int totemnet_iface_get (
	totemnet_handle handle,
	struct totem_ip_address *addr)
{
	struct totemnet_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}
	
	memcpy (addr, &instance->my_id, sizeof (struct totem_ip_address));

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_token_target_set (
	totemnet_handle handle,
	struct totem_ip_address *token_target)
{
	struct totemnet_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}
	
	memcpy (&instance->token_target, token_target,
		sizeof (struct totem_ip_address));

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}


