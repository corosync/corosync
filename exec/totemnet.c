/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/sockios.h>
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

#include "aispoll.h"
#include "totemnet.h"
#include "wthread.h"
#include "../include/queue.h"
#include "../include/sq.h"
#include "../include/list.h"
#include "hdb.h"
#include "swab.h"

#include "crypto.h"

#define MCAST_SOCKET_BUFFER_SIZE (16 * 9000) /* where 16 is the transmits allowed, 9000 is mtu size */

#define NETIF_STATE_REPORT_UP		1	
#define NETIF_STATE_REPORT_DOWN		2

#define BIND_STATE_UNBOUND	0
#define BIND_STATE_REGULAR	1
#define BIND_STATE_LOOPBACK	2

#define LOCALHOST_IP inet_addr("127.0.0.1")

#define HMAC_HASH_SIZE 20
struct security_header {
	unsigned char hash_digest[HMAC_HASH_SIZE]; /* The hash *MUST* be first in the data structure */
	unsigned char salt[16]; /* random number */
	char msg[0];
} __attribute__((packed));

struct totemnet_mcast_thread_state {
	char iobuf[9000];
	prng_state prng_state;
};

struct totemnet_socket {
	int mcast;
	int token;
};

struct totemnet_instance {
	hmac_state totemnet_hmac_state;

	prng_state totemnet_prng_state;

	unsigned char totemnet_private_key[1024];

	unsigned int totemnet_private_key_len;

	poll_handle totemnet_poll_handle;

	struct totem_interface *totemnet_interface;

	int netif_state_report;

	int netif_bind_state;

	struct worker_thread_group worker_thread_group;

	void *context;

	void (*totemnet_deliver_fn) (
		void *context,
		struct in_addr *system_from,
		void *msg,
		int msg_len);

	void (*totemnet_iface_change_fn) (
		void *context,
		struct sockaddr_in *iface_sockaddr_in);

	/*
	 * Function and data used to log messages
	 */
	int totemnet_log_level_security;

	int totemnet_log_level_error;

	int totemnet_log_level_warning;

	int totemnet_log_level_notice;

	int totemnet_log_level_debug;

	void (*totemnet_log_printf) (int level, char *format, ...);

	totemnet_handle handle;

	char iov_buffer[FRAME_SIZE_MAX];

	char iov_buffer_flush[FRAME_SIZE_MAX];

	struct iovec totemnet_iov_recv;

	struct iovec totemnet_iov_recv_flush;

	struct totemnet_socket totemnet_sockets;

	struct sockaddr_in sockaddr_in_mcast;

	struct in_addr in_addr_mcast;

	int stats_sent;

	int stats_recv;

	int stats_delv;

	int stats_remcasts;

	int stats_orf_token;

	struct timeval stats_tv_start;

	struct sockaddr_in my_id;

	int firstrun;

	poll_timer_handle timer_netif_check_timeout;

	unsigned int my_memb_entries;

	int flushing;

	struct totem_config *totem_config;
};

struct work_item {
	struct iovec iovec[20];
	int iov_len;
	struct totemnet_instance *instance;
};

/*
 * All instances in one database
 */
static struct saHandleDatabase totemnet_instance_database = {
	.handleCount			= 0,
	.handles			= 0,
	.handleInstanceDestructor	= 0
};

static int loopback_determine (struct sockaddr_in *bound_to);
static void netif_down_check (struct totemnet_instance *instance);

static int totemnet_build_sockets (
	struct totemnet_instance *instance,
	struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	struct totemnet_socket *sockets,
	struct sockaddr_in *bound_to,
	int *interface_up);

static int totemnet_build_sockets_loopback (
	struct totemnet_instance *instance,
	struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	struct totemnet_socket *sockets,
	struct sockaddr_in *bound_to);

static void totemnet_instance_initialize (struct totemnet_instance *instance)
{
	memset (instance, 0, sizeof (struct totemnet_instance));

	instance->netif_state_report = NETIF_STATE_REPORT_UP | NETIF_STATE_REPORT_DOWN;

	instance->totemnet_iov_recv.iov_base = instance->iov_buffer;

	instance->totemnet_iov_recv.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);
	instance->totemnet_iov_recv_flush.iov_base = instance->iov_buffer_flush;

	instance->totemnet_iov_recv_flush.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);

}

static int authenticate_and_decrypt (
	struct totemnet_instance *instance,
	struct iovec *iov)
{
	char keys[48];
	struct security_header *header = iov[0].iov_base;
	prng_state keygen_prng_state;
	prng_state stream_prng_state;
	char *hmac_key = &keys[32];
	char *cipher_key = &keys[16];
	char *initial_vector = &keys[0];
	char digest_comparison[HMAC_HASH_SIZE];
	unsigned long len;
	int res = 0;

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
		instance->totemnet_log_printf (instance->totemnet_log_level_security, "Received message has invalid digest... ignoring.\n");
		res = -1;
		return (-1);
	}
	
	/*
	 * Decrypt the contents of the message with the cipher key
	 */
	sober128_read (iov->iov_base + sizeof (struct security_header),
		iov->iov_len - sizeof (struct security_header),
		&stream_prng_state);

	return (res);
	return (0);
}
static void encrypt_and_sign_worker (
	struct totemnet_instance *instance,
	char *buf,
	int *buf_len,
	struct iovec *iovec,
	int iov_len,
	prng_state *prng_state_in)
{
	int i;
	char *addr;
	char keys[48];
	struct security_header *header;
	char *hmac_key = &keys[32];
	char *cipher_key = &keys[16];
	char *initial_vector = &keys[0];
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

void totemnet_iovec_send (
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
	int iov_len;

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
	msg_mcast.msg_name = &instance->sockaddr_in_mcast;
	msg_mcast.msg_namelen = sizeof (struct sockaddr_in);
	msg_mcast.msg_iov = iovec_sendmsg;
	msg_mcast.msg_iovlen = iov_len;
	msg_mcast.msg_control = 0;
	msg_mcast.msg_controllen = 0;
	msg_mcast.msg_flags = 0;

	/*
	 * Transmit token or multicast message
	 * An error here is recovered by totemnet
	 */
	res = sendmsg (instance->totemnet_sockets.mcast, &msg_mcast, MSG_NOSIGNAL | MSG_DONTWAIT);
}

void totemnet_msg_send (
	struct totemnet_instance *instance,
	struct in_addr *system_to,
	void *msg,
	int msg_len)
{
	struct sockaddr_in next_addr;
	struct msghdr msg_mcast;
	int res = 0;
	int buf_len;
	unsigned char sheader[sizeof (struct security_header)];
	unsigned char encrypt_data[FRAME_SIZE_MAX];
	struct iovec iovec[2];
	struct iovec iovec_sendmsg;
	int fd;

	if (instance->totem_config->secauth == 1) {

		iovec[0].iov_base = sheader;
		iovec[0].iov_len = sizeof (struct security_header);
		iovec[1].iov_base = msg;
		iovec[1].iov_len = msg_len;

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign_worker (
			instance,
			encrypt_data,
			&buf_len,
			iovec,
			2,
			&instance->totemnet_prng_state);

		iovec_sendmsg.iov_base = encrypt_data;
		iovec_sendmsg.iov_len = buf_len;
	} else {
		iovec_sendmsg.iov_base = msg;
		iovec_sendmsg.iov_len = msg_len;
	}

	/*
	 * Build multicast message
	 */
	if (system_to) {
		/*
		 * system_to is non-zero, so its a token send operation
		 */
		next_addr.sin_addr.s_addr = system_to->s_addr;
		next_addr.sin_port = instance->sockaddr_in_mcast.sin_port;
		next_addr.sin_family = AF_INET;

		fd = instance->totemnet_sockets.token;
		msg_mcast.msg_name = &next_addr;
	} else {
		/*
		 * system_to is zero, so its a mcast send operation
		 */
		fd = instance->totemnet_sockets.mcast;
		msg_mcast.msg_name = &instance->sockaddr_in_mcast;
	}

	msg_mcast.msg_namelen = sizeof (struct sockaddr_in);
	msg_mcast.msg_iov = &iovec_sendmsg;
	msg_mcast.msg_iovlen = 1;
	msg_mcast.msg_control = 0;
	msg_mcast.msg_controllen = 0;
	msg_mcast.msg_flags = 0;

	/*
	 * Transmit token or multicast message
	 * An error here is recovered by totemnet
	 */
	res = sendmsg (fd, &msg_mcast, MSG_NOSIGNAL | MSG_DONTWAIT);
//printf ("sent %d bytes\n", res);
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
	unsigned int iovs;

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

	msg_mcast.msg_name = &instance->sockaddr_in_mcast;
	msg_mcast.msg_namelen = sizeof (struct sockaddr_in);
	msg_mcast.msg_iov = iovec_sendmsg;
	msg_mcast.msg_iovlen = iovs;
	msg_mcast.msg_control = 0;
	msg_mcast.msg_controllen = 0;
	msg_mcast.msg_flags = 0;

	/*
	 * Transmit token or multicast message
	 * An error here is recovered by totemnet
	 */
	res = sendmsg (instance->totemnet_sockets.mcast, &msg_mcast, MSG_NOSIGNAL | MSG_DONTWAIT);

	if (res > 0) {
		instance->stats_sent += res;
	}
}


int totemnet_finalize (
	totemnet_handle handle)
{
	struct totemnet_instance *instance;
	SaErrorT error;
	int res = 0;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		res = ENOENT;
		goto error_exit;
	}

	worker_thread_group_exit (&instance->worker_thread_group);

	saHandleInstancePut (&totemnet_instance_database, handle);

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
	void *data,
	unsigned int *prio)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)data;
	struct msghdr msg_recv;
	struct iovec *iovec;
	struct security_header *security_header;
	struct sockaddr_in system_from;
	int bytes_received;
	int res = 0;
	unsigned char *msg_offset;
	unsigned int size_delv;

	*prio = UINT_MAX;

	if (instance->flushing == 1) {
		iovec = &instance->totemnet_iov_recv_flush;
	} else {
		iovec = &instance->totemnet_iov_recv;
	}

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_in);
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

		instance->totemnet_log_printf (instance->totemnet_log_level_security, "Received message is too short...  ignoring %d.\n", bytes_received);
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
			printf ("Invalid packet data\n");
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
		&system_from.sin_addr,
		msg_offset,
		size_delv);
		
	iovec->iov_len = FRAME_SIZE_MAX;
	return (0);
}

static int netif_determine (
	struct totemnet_instance *instance,
	struct sockaddr_in *bindnet,
	struct sockaddr_in *bound_to,
	int *interface_up)
{
	struct sockaddr_in *sockaddr_in;
	int id_fd;
	struct ifconf ifc;
	int numreqs = 0;
	int res;
	int i;
	in_addr_t mask_addr;

	*interface_up = 0;

	/*
	 * Generate list of local interfaces in ifc.ifc_req structure
	 */
	id_fd = socket (AF_INET, SOCK_STREAM, 0);
	ifc.ifc_buf = 0;
	do {
		numreqs += 32;
		ifc.ifc_len = sizeof (struct ifreq) * numreqs;
		ifc.ifc_buf = (void *)realloc(ifc.ifc_buf, ifc.ifc_len);
		res = ioctl (id_fd, SIOCGIFCONF, &ifc);
		if (res < 0) {
			close (id_fd);
			return -1;
		}
	} while (ifc.ifc_len == sizeof (struct ifreq) * numreqs);
	res = -1;

	/*
	 * Find interface address to bind to
	 */
	for (i = 0; i < ifc.ifc_len / sizeof (struct ifreq); i++) {
		sockaddr_in = (struct sockaddr_in *)&ifc.ifc_ifcu.ifcu_req[i].ifr_ifru.ifru_addr;
		mask_addr = inet_addr ("255.255.255.0");

		if ((sockaddr_in->sin_family == AF_INET) &&
			(sockaddr_in->sin_addr.s_addr & mask_addr) ==
			(bindnet->sin_addr.s_addr & mask_addr)) {

			bound_to->sin_addr.s_addr = sockaddr_in->sin_addr.s_addr;
			res = i;

			if (ioctl(id_fd, SIOCGIFFLAGS, &ifc.ifc_ifcu.ifcu_req[i]) < 0) {
				printf ("couldn't do ioctl\n");
			}

			*interface_up = ifc.ifc_ifcu.ifcu_req[i].ifr_ifru.ifru_flags & IFF_UP;
			break; /* for */
		}
	}
	free (ifc.ifc_buf);
	close (id_fd);
	
	return (res);
}

static int loopback_determine (struct sockaddr_in *bound_to)
{

	bound_to->sin_addr.s_addr = LOCALHOST_IP;
	if (&bound_to->sin_addr.s_addr == 0) {
		return -1;
	}
	return 1;
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

	/*
	* Build sockets for every interface
	*/
	netif_determine (instance,
		&instance->totemnet_interface->bindnet,
		&instance->totemnet_interface->boundto,
		&interface_up);

	if (instance->totemnet_sockets.mcast > 0) {
		close (instance->totemnet_sockets.mcast);
	 	poll_dispatch_delete (instance->totemnet_poll_handle,
		instance->totemnet_sockets.mcast);
	}
	if (instance->totemnet_sockets.token > 0) {
		close (instance->totemnet_sockets.token);
		poll_dispatch_delete (instance->totemnet_poll_handle,
		instance->totemnet_sockets.token);
	}

	if (!interface_up) {
		instance->netif_bind_state = BIND_STATE_LOOPBACK;
		res = totemnet_build_sockets_loopback(instance,
			&instance->sockaddr_in_mcast,
			&instance->totemnet_interface->bindnet,
			&instance->totemnet_sockets,
			&instance->totemnet_interface->boundto);

		poll_dispatch_add (
			instance->totemnet_poll_handle,
			instance->totemnet_sockets.token,
			POLLIN, instance, net_deliver_fn, UINT_MAX);

		instance->netif_bind_state = BIND_STATE_REGULAR;
	} else {
		/*
		* Create and bind the multicast and unicast sockets
		*/
		memcpy (&instance->sockaddr_in_mcast.sin_addr,
			&instance->in_addr_mcast, sizeof (struct in_addr));
		res = totemnet_build_sockets (instance,
			&instance->sockaddr_in_mcast,
			&instance->totemnet_interface->bindnet,
			&instance->totemnet_sockets,
			&instance->totemnet_interface->boundto,
			&interface_up);

		poll_dispatch_add (
			instance->totemnet_poll_handle,
			instance->totemnet_sockets.mcast,
			POLLIN, instance, net_deliver_fn, UINT_MAX);

		poll_dispatch_add (
			instance->totemnet_poll_handle,
			instance->totemnet_sockets.token,
			POLLIN, instance, net_deliver_fn, UINT_MAX);
	}

	memcpy (&instance->my_id, &instance->totemnet_interface->boundto,
		sizeof (struct sockaddr_in));	

	/*
	* This stuff depends on totemnet_build_sockets
	*/
	if (interface_up) {
		if (instance->netif_state_report & NETIF_STATE_REPORT_UP) {
			instance->totemnet_log_printf (instance->totemnet_log_level_notice,
				" The network interface [%s] is now up.\n",
				inet_ntoa (instance->totemnet_interface->boundto.sin_addr));
			instance->netif_state_report = NETIF_STATE_REPORT_DOWN;
			instance->totemnet_iface_change_fn (instance->context, &instance->my_id);
		}

		/*
		 * If this is a single processor, detect downs which may not 
		 * be detected by token loss when the interface is downed
		 */
		/*
		if (instance->my_memb_entries <= 1) {
			poll_timer_add (instance->totemnet_poll_handle,
				instance->timeout_downcheck,
				(void *)instance,
				timer_function_netif_check_timeout,
				&instance->timer_netif_check_timeout);
		}
		*/
	} else {		
		if (instance->netif_state_report & NETIF_STATE_REPORT_DOWN) {
			instance->totemnet_log_printf (instance->totemnet_log_level_notice,
				"The network interface is down.\n");
			instance->totemnet_iface_change_fn (instance->context, &instance->my_id);
		}
		instance->netif_state_report = NETIF_STATE_REPORT_UP;

		/*
		 * Add a timer to retry building interfaces and request memb_gather_enter
		 */
		poll_timer_add (instance->totemnet_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
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

	struct sockaddr_in sockaddr_in_test;
static int totemnet_build_sockets_loopback (
	struct totemnet_instance *instance,
	struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	struct totemnet_socket *sockets,
	struct sockaddr_in *bound_to)
{
	struct ip_mreq mreq;
	int res;

	memset (&mreq, 0, sizeof (struct ip_mreq));

	/*
	 * Determine the ip address bound to and the interface name
	 */
	res = loopback_determine (bound_to);

	if (res == -1) {
		return (-1);
	}

	/* TODO this should be somewhere else */
	instance->my_id.sin_addr.s_addr = bound_to->sin_addr.s_addr;
	instance->my_id.sin_family = AF_INET;
	instance->my_id.sin_port = sockaddr_mcast->sin_port;

	 /*
	 * Setup unicast socket
	 */
	sockets->token = socket (AF_INET, SOCK_DGRAM, 0);
	if (sockets->token == -1) {
		perror ("socket2");
		return (-1);
	}

	/*
	 * Bind to unicast socket used for token send/receives	
	 * This has the side effect of binding to the correct interface
	 */
	sockaddr_in_test.sin_addr.s_addr = bound_to->sin_addr.s_addr;
	sockaddr_in_test.sin_family = AF_INET;
	sockaddr_in_test.sin_port = sockaddr_mcast->sin_port;

	res = bind (sockets->token, (struct sockaddr *)&sockaddr_in_test,
			sizeof (struct sockaddr_in));
	if (res == -1) {
		perror ("bind2 failed");
		return (-1);
	}

	memcpy(&instance->sockaddr_in_mcast, &sockaddr_in_test, sizeof(struct sockaddr_in));
	sockets->mcast = sockets->token;

	return (0);
}


static int totemnet_build_sockets (
	struct totemnet_instance *instance,
	struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	struct totemnet_socket *sockets,
	struct sockaddr_in *bound_to,
	int *interface_up)
{
	struct ip_mreq mreq;
	struct sockaddr_in sockaddr_in_test;
	char flag;
	int res;
	unsigned int sendbuf_size;
	unsigned int recvbuf_size;
	unsigned int optlen = sizeof (sendbuf_size);
	
	memset (&mreq, 0, sizeof (struct ip_mreq));

	/*
	 * Determine the ip address bound to and the interface name
	 */
	res = netif_determine (instance,
		sockaddr_bindnet,
		bound_to,
		interface_up);

	if (res == -1) {
		return (-1);
	}

	/* TODO this should be somewhere else */
	instance->my_id.sin_addr.s_addr = bound_to->sin_addr.s_addr;
	instance->my_id.sin_family = AF_INET;
	instance->my_id.sin_port = sockaddr_mcast->sin_port;

	/*
	 * Create multicast socket
	 */
	sockets->mcast = socket (AF_INET, SOCK_DGRAM, 0);
	if (sockets->mcast == -1) {
		perror ("socket");
		return (-1);
	}

	if (setsockopt (sockets->mcast, SOL_IP, IP_MULTICAST_IF,
		&bound_to->sin_addr, sizeof (struct in_addr)) < 0) {

		instance->totemnet_log_printf (instance->totemnet_log_level_warning, "Could not bind to device for multicast, group messaging may not work properly. (%s)\n", strerror (errno));
	}

	recvbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	sendbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	/*
	 * Set buffer sizes to avoid overruns
	 */
	res = setsockopt (sockets->mcast, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, optlen);
	res = setsockopt (sockets->mcast, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, optlen);

	res = getsockopt (sockets->mcast, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, &optlen);
	if (res == 0) {
		instance->totemnet_log_printf (instance->totemnet_log_level_notice,
			"Multicast socket recv buffer size (%d bytes).\n", recvbuf_size);
	}

	res = getsockopt (sockets->mcast, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, &optlen);
	if (res == 0) {
		instance->totemnet_log_printf (instance->totemnet_log_level_notice,
			"Multicast socket send buffer size (%d bytes).\n", sendbuf_size);
	}

	/*
	 * Bind to multicast socket used for multicast send/receives
	 */
	sockaddr_in_test.sin_family = AF_INET;
	sockaddr_in_test.sin_addr.s_addr = sockaddr_mcast->sin_addr.s_addr;
	sockaddr_in_test.sin_port = sockaddr_mcast->sin_port;
	printf ("binding to %s\n", inet_ntoa (sockaddr_in_test.sin_addr));
		printf ("%d\n", sockaddr_in_test.sin_port);
	res = bind (sockets->mcast, (struct sockaddr *)&sockaddr_in_test,
		sizeof (struct sockaddr_in));
	if (res == -1) {
		perror ("bind failed");
		return (-1);
	}

	/*
	 * Setup unicast socket
	 */
	sockets->token = socket (AF_INET, SOCK_DGRAM, 0);
	if (sockets->token == -1) {
		perror ("socket2");
		return (-1);
	}

	/*
	 * Bind to unicast socket used for token send/receives
	 * This has the side effect of binding to the correct interface
	 */
	sockaddr_in_test.sin_family = AF_INET;
	sockaddr_in_test.sin_addr.s_addr = bound_to->sin_addr.s_addr;
	sockaddr_in_test.sin_port = sockaddr_mcast->sin_port;
	printf ("binding to %s\n", inet_ntoa (sockaddr_in_test.sin_addr));
		printf ("%d\n", sockaddr_in_test.sin_port);
	res = bind (sockets->token, (struct sockaddr *)&sockaddr_in_test,
		sizeof (struct sockaddr_in));
	if (res == -1) {
		perror ("bind2 failed");
		return (-1);
	}

#ifdef CONFIG_USE_BROADCAST
/* This config option doesn't work */
{
	int on = 1;
	setsockopt (sockets->mcast, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof (on));
}
#else
	/*
	 * Join group membership on socket
	 */
	mreq.imr_multiaddr.s_addr = sockaddr_mcast->sin_addr.s_addr;
	mreq.imr_interface.s_addr = bound_to->sin_addr.s_addr;

	res = setsockopt (sockets->mcast, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		&mreq, sizeof (mreq));
	if (res == -1) {
		perror ("join multicast group failed");
		return (-1);
	}

#endif
	/*
	 * Turn on multicast loopback
	 */
	flag = 1;
	res = setsockopt (sockets->mcast, IPPROTO_IP, IP_MULTICAST_LOOP,
		&flag, sizeof (flag));
	if (res == -1) {
		perror ("turn off loopback");
		return (-1);
	}

	return (0);
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
		struct in_addr *system_from,
		void *msg,
		int msg_len),

	void (*iface_change_fn) (
		void *context,
		struct sockaddr_in *iface_sockaddr_in))
{
	SaAisErrorT error;
	struct totemnet_instance *instance;

	memset (&sockaddr_in_test, 0, sizeof (struct sockaddr_in));
	error = saHandleCreate (&totemnet_instance_database,
	sizeof (struct totemnet_instance), handle);
	if (error != SA_OK) {
		goto error_exit;
	}
	error = saHandleInstanceGet (&totemnet_instance_database, *handle,
		(void *)&instance);
	if (error != SA_OK) {
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

	memcpy (&instance->sockaddr_in_mcast, &totem_config->mcast_addr,
		sizeof (struct sockaddr_in));

	memcpy (&instance->in_addr_mcast, &totem_config->mcast_addr.sin_addr,
		sizeof (struct in_addr));

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
	memcpy (&instance->sockaddr_in_mcast, &totem_config->mcast_addr, 
	sizeof (struct sockaddr_in));
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

	instance->totemnet_interface = &totem_config->interfaces[interface_no];
	instance->totemnet_poll_handle = poll_handle;

	instance->context = context;
	instance->totemnet_deliver_fn = deliver_fn;

	instance->totemnet_iface_change_fn = iface_change_fn;

	instance->handle = *handle;

	rng_make_prng (128, PRNG_SOBER, &instance->totemnet_prng_state, NULL);

	netif_down_check (instance);

error_exit:
	saHandleInstancePut (&totemnet_instance_database, *handle);
	return (0);

error_destroy:
	saHandleDestroy (&totemnet_instance_database, *handle);
	return (-1);
}

int totemnet_processor_count_set (
	totemnet_handle handle,
	int processor_count)
{
	SaAisErrorT error;
	struct totemnet_instance *instance;
	int res = 0;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		res = ENOENT;
		goto error_exit;
	}

	instance->my_memb_entries = processor_count;

	saHandleInstancePut (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_recv_flush (totemnet_handle handle)
{
	SaAisErrorT error;
	struct totemnet_instance *instance;
	struct pollfd ufd;
	int nfds;
	int res = 0;
	int prio;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		res = ENOENT;
		goto error_exit;
	}

	instance->flushing = 1;

	do {
		ufd.fd = instance->totemnet_sockets.mcast;
		ufd.events = POLLIN;
		nfds = poll (&ufd, 1, 0);
		if (nfds == 1 && ufd.revents & POLLIN) {
		net_deliver_fn (0, instance->totemnet_sockets.mcast,
			ufd.revents, instance, &prio);
		}
	} while (nfds == 1);

	instance->flushing = 0;

	saHandleInstancePut (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_send_flush (totemnet_handle handle)
{
	SaAisErrorT error;
	struct totemnet_instance *instance;
	int res = 0;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		res = ENOENT;
		goto error_exit;
	}
	
	worker_thread_group_wait (&instance->worker_thread_group);

	saHandleInstancePut (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_token_send (
	totemnet_handle handle,
	struct in_addr *system_to,
	void *msg,
	int msg_len)
{
	SaAisErrorT error;
	struct totemnet_instance *instance;
	int res = 0;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		res = ENOENT;
		goto error_exit;
	}

	totemnet_msg_send (instance, system_to, msg, msg_len);

	saHandleInstancePut (&totemnet_instance_database, handle);

error_exit:
	return (res);
}
int totemnet_mcast_flush_send (
	totemnet_handle handle,
	void *msg,
	int msg_len)
{
	SaAisErrorT error;
	struct totemnet_instance *instance;
	int res = 0;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		res = ENOENT;
		goto error_exit;
	}
	
	totemnet_msg_send (instance, 0, msg, msg_len);

	saHandleInstancePut (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_mcast_noflush_send (
	totemnet_handle handle,
	struct iovec *iovec,
	int iov_len)
{
	SaAisErrorT error;
	struct totemnet_instance *instance;
	struct work_item work_item;
	int res = 0;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
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
		totemnet_iovec_send (instance, iovec, iov_len);
	}
	
	saHandleInstancePut (&totemnet_instance_database, handle);
error_exit:
	return (res);
}
extern int totemnet_iface_check (totemnet_handle handle)
{
	SaAisErrorT error;
	struct totemnet_instance *instance;
	int res = 0;

	error = saHandleInstanceGet (&totemnet_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		res = ENOENT;
		goto error_exit;
	}
	
	timer_function_netif_check_timeout (instance);

	saHandleInstancePut (&totemnet_instance_database, handle);
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
printf ("adjusted frame size is %d\n", totem_config->net_mtu);
}
