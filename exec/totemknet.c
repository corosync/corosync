
/*
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)

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
#include <corosync/swab.h>
#include <corosync/logsys.h>
#include <corosync/totem/totemip.h>
#include "totemknet.h"

#include "util.h"

#include <nss.h>
#include <pk11pub.h>
#include <pkcs11.h>
#include <prerror.h>

#include <libknet.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MCAST_SOCKET_BUFFER_SIZE (TRANSMITS_ALLOWED * FRAME_SIZE_MAX)

/* Buffers for sendmmsg/recvmmsg */
#define MAX_BUFFERS 10

/* Should match that used by cfg */
#define CFG_INTERFACE_STATUS_MAX_LEN 512

struct totemknet_instance {
	struct crypto_instance *crypto_inst;

	qb_loop_t *poll_handle;

        knet_handle_t knet_handle;

	int link_mode;

	void *context;

	void (*totemknet_deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*totemknet_iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address,
		unsigned int link_no);

	void (*totemknet_mtu_changed) (
		void *context,
		int net_mtu);

	void (*totemknet_target_set_completed) (void *context);

	/*
	 * Function and data used to log messages
	 */
	int totemknet_log_level_security;

	int totemknet_log_level_error;

	int totemknet_log_level_warning;

	int totemknet_log_level_notice;

	int totemknet_log_level_debug;

	int totemknet_subsys_id;

	int knet_subsys_id;

	void (*totemknet_log_printf) (
		int level,
		int subsys,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 6, 7)));

	void *knet_context;

	char iov_buffer[MAX_BUFFERS][FRAME_SIZE_MAX];

	int stats_sent;

	int stats_recv;

	int stats_delv;

	int stats_remcasts;

	int stats_orf_token;

	struct timeval stats_tv_start;

	char *link_status[INTERFACE_MAX];

	int num_links;

	struct totem_ip_address my_ids[INTERFACE_MAX];

	uint16_t ip_port[INTERFACE_MAX];

	int our_nodeid;

	struct totem_config *totem_config;

	totemsrp_stats_t *stats;

	struct totem_ip_address token_target;

	qb_loop_timer_handle timer_netif_check_timeout;

	qb_loop_timer_handle timer_merge_detect_timeout;

	int send_merge_detect_message;

	unsigned int merge_detect_messages_sent_before_timeout;

	int logpipes[2];
	int knet_fd;
};

struct work_item {
	const void *msg;
	unsigned int msg_len;
	struct totemknet_instance *instance;
};

int totemknet_member_list_rebind_ip (
	void *knet_context);

static void totemknet_start_merge_detect_timeout(
	void *knet_context);

static void totemknet_stop_merge_detect_timeout(
	void *knet_context);

static void totemknet_instance_initialize (struct totemknet_instance *instance)
{
	memset (instance, 0, sizeof (struct totemknet_instance));
}

#define knet_log_printf(level, format, args...)		\
do {							\
        instance->totemknet_log_printf (		\
		level, instance->totemknet_subsys_id,	\
                __FUNCTION__, __FILE__, __LINE__,	\
		(const char *)format, ##args);		\
} while (0);

#define libknet_log_printf(level, format, args...)		\
do {							\
        instance->totemknet_log_printf (		\
		level, instance->knet_subsys_id,	\
                __FUNCTION__, "libknet.h", __LINE__,	\
		(const char *)format, ##args);		\
} while (0);

#define KNET_LOGSYS_PERROR(err_num, level, fmt, args...)						\
do {												\
	char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];						\
	const char *_error_ptr = qb_strerror_r(err_num, _error_str, sizeof(_error_str));	\
        instance->totemknet_log_printf (							\
		level, instance->totemknet_subsys_id,						\
                __FUNCTION__, __FILE__, __LINE__,						\
		fmt ": %s (%d)", ##args, _error_ptr, err_num);				\
	} while(0)


static int dst_host_filter_callback_fn(void *private_data,
				       const unsigned char *outdata,
				       ssize_t outdata_len,
				       uint8_t tx_rx,
				       uint16_t this_host_id,
				       uint16_t src_host_id,
				       int8_t *channel,
				       uint16_t *dst_host_ids,
				       size_t *dst_host_ids_entries)
{
	struct totem_message_header *header = (struct totem_message_header *)outdata;
//	struct totemknet_instance *instance = (struct totemknet_instance *)private_data;
	int res;

//	knet_log_printf (LOGSYS_LEVEL_DEBUG, "Filter notification called: %s target nodeid=%d, len=%ld", tx_rx==KNET_NOTIFY_RX?"RX":"TX", header->target_nodeid, outdata_len);

	*channel = 0;
	if (header->target_nodeid) {
		dst_host_ids[0] = header->target_nodeid;
		*dst_host_ids_entries = 1;
		res = 0; /* unicast message */
	}
	else {
		*dst_host_ids_entries = 0;
		res = 1; /* multicast message */
	}
	return res;
}

static void socket_error_callback_fn(void *private_data, int datafd, int8_t channel, uint8_t tx_rx, int error, int errorno)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)private_data;

	knet_log_printf (LOGSYS_LEVEL_DEBUG, "Knet socket ERROR notification called: txrx=%d, error=%d, errorno=%d", tx_rx, error, errorno);
	if ((error == -1 && errorno != EAGAIN) || (error == 0)) {
		knet_handle_remove_datafd(instance->knet_handle, datafd);
	}
}

static void host_change_callback_fn(void *private_data, uint16_t host_id, uint8_t reachable, uint8_t remote, uint8_t external)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)private_data;

	// TODO: what? if anything.
	knet_log_printf (LOGSYS_LEVEL_DEBUG, "Knet host change callback. nodeid: %d reachable: %d", host_id, reachable);
}

static void pmtu_change_callback_fn(void *private_data, unsigned int data_mtu)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)private_data;
	knet_log_printf (LOGSYS_LEVEL_DEBUG, "Knet pMTU change: %d", data_mtu);

	// TODO: Check this
	instance->totemknet_mtu_changed(instance->context, data_mtu - totemip_udpip_header_size(AF_INET));
}

int totemknet_crypto_set (
	void *knet_context,
	const char *cipher_type,
	const char *hash_type)
{
	return (0);
}


static inline void ucast_sendmsg (
	struct totemknet_instance *instance,
	struct totem_ip_address *system_to,
	const void *msg,
	unsigned int msg_len)
{
	int res = 0;
	struct totem_message_header *header = (struct totem_message_header *)msg;

	header->target_nodeid = system_to->nodeid;

	/*
	 * Transmit unicast message
	 * An error here is recovered by totemsrp
	 */

	/*
	 * If sending to ourself then just pass it through knet back to
	 * the receive fn. knet does not do local->local delivery
	 */
	if (system_to->nodeid == instance->our_nodeid) {
		res = write (instance->knet_fd+1, msg, msg_len);
		if (res < 0) {
			KNET_LOGSYS_PERROR (errno, instance->totemknet_log_level_debug,
				       "sendmsg(ucast-local) failed (non-critical)");
		}
	}
	else {
		res = write (instance->knet_fd, msg, msg_len);
		if (res < 0) {
			KNET_LOGSYS_PERROR (errno, instance->totemknet_log_level_debug,
				       "sendmsg(ucast) failed (non-critical)");
		}
	}
}

static inline void mcast_sendmsg (
	struct totemknet_instance *instance,
	const void *msg,
	unsigned int msg_len,
	int only_active)
{
	int res;
	struct totem_message_header *header = (struct totem_message_header *)msg;

	header->target_nodeid = 0;

//	log_printf (LOGSYS_LEVEL_DEBUG, "totemknet: mcast_sendmsg. only_active=%d, len=%d", only_active, msg_len);

	res = write (instance->knet_fd, msg, msg_len);
	if (res < msg_len) {
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "totemknet: mcast_send writev returned %d", res);
	}

	/*
	 * Also send it to ourself, directly into
	 * the receive fn. knet does not to local->local delivery
	 */
	res = write (instance->knet_fd+1, msg, msg_len);
	if (res < msg_len) {
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "totemknet: mcast_send writev (local) returned %d", res);
	}

	if (!only_active || instance->send_merge_detect_message) {
		/*
		 * Current message was sent to all nodes
		 */
		instance->merge_detect_messages_sent_before_timeout++;
		instance->send_merge_detect_message = 0;
	}
}

static int node_compare(const void *aptr, const void *bptr)
{
	uint16_t a,b;

	a = *(uint16_t *)aptr;
	b = *(uint16_t *)bptr;

	return a > b;
}

int totemknet_ifaces_get (void *knet_context,
	char ***status,
	unsigned int *iface_count)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	struct knet_link_status link_status;
	uint16_t host_list[KNET_MAX_HOST];
	size_t num_hosts;
	int i,j;
	char *ptr;
	int res = 0;

	/*
	 * Don't do the whole 'link_info' bit if the caller just wants
	 * a count of interfaces.
	 */
	if (status) {
		res = knet_host_get_host_list(instance->knet_handle,
					      host_list, &num_hosts);
		if (res) {
			return (-1);
		}
		qsort(host_list, num_hosts, sizeof(uint16_t), node_compare);

		/* num_links is actually the highest link ID */
		for (i=0; i <= instance->num_links; i++) {
			ptr = instance->link_status[i];

			for (j=0; j<num_hosts; j++) {
				res = knet_link_get_status(instance->knet_handle,
							   host_list[j],
							   i,
							   &link_status);
				if (res == 0) {
					ptr[j] = '0' + (link_status.enabled |
							link_status.connected<<1 |
							link_status.dynconnected<<2);
				}
				else {
					ptr[j] += '?';
				}
			}
			ptr[num_hosts] = '\0';
		}
		*status = instance->link_status;
	}

	*iface_count = instance->num_links+1;

	return (res);
}

int totemknet_finalize (
	void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	knet_log_printf(LOG_DEBUG, "totemknet: finalize");

	qb_loop_poll_del (instance->poll_handle, instance->logpipes[0]);
	qb_loop_poll_del (instance->poll_handle, instance->knet_fd);
	knet_handle_free(instance->knet_handle);

	totemknet_stop_merge_detect_timeout(instance);

	return (res);
}

static int log_deliver_fn (
	int fd,
	int revents,
	void *data)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)data;
	char buffer[KNET_MAX_LOG_MSG_SIZE*4];
	char *bufptr = buffer;
	int done = 0;
	int len;

	len = read(fd, buffer, sizeof(buffer));
	while (done < len) {
		struct knet_log_msg *msg = (struct knet_log_msg *)bufptr;
		switch (msg->msglevel) {
		case KNET_LOG_ERR:
			libknet_log_printf (LOGSYS_LEVEL_ERROR, "%s", msg->msg);
			break;
		case KNET_LOG_WARN:
			libknet_log_printf (LOGSYS_LEVEL_WARNING, "%s", msg->msg);
			break;
		case KNET_LOG_INFO:
			libknet_log_printf (LOGSYS_LEVEL_INFO, "%s", msg->msg);
			break;
		case KNET_LOG_DEBUG:
			libknet_log_printf (LOGSYS_LEVEL_DEBUG, "%s", msg->msg);
			break;
		}
		bufptr += KNET_MAX_LOG_MSG_SIZE;
		done += KNET_MAX_LOG_MSG_SIZE;
	}
	return 0;
}

static int data_deliver_fn (
	int fd,
	int revents,
	void *data)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)data;
	struct mmsghdr msg_recv[MAX_BUFFERS];
	struct iovec iov_recv[MAX_BUFFERS];
	struct sockaddr_storage system_from;
	int msgs_received;
	int i;

	for (i=0; i<MAX_BUFFERS; i++) {
		iov_recv[i].iov_base = instance->iov_buffer[i];
		iov_recv[i].iov_len = FRAME_SIZE_MAX;

		msg_recv[i].msg_hdr.msg_name = &system_from;
		msg_recv[i].msg_hdr.msg_namelen = sizeof (struct sockaddr_storage);
		msg_recv[i].msg_hdr.msg_iov = &iov_recv[i];
		msg_recv[i].msg_hdr.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
		msg_recv[i].msg_hdr.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
		msg_recv[i].msg_hdr.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
		msg_recv[i].msg_hdr.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
		msg_recv[i].msg_hdr.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
		msg_recv[i].msg_hdr.msg_accrightslen = 0;
#endif
	}

	msgs_received = recvmmsg (fd, msg_recv, MAX_BUFFERS, MSG_NOSIGNAL | MSG_DONTWAIT, NULL);
	if (msgs_received == -1) {
		return (0);
	}

	for (i=0; i<msgs_received; i++) {

		instance->stats_recv += msg_recv[i].msg_len;

		/*
		 * Handle incoming message
		 */
		instance->totemknet_deliver_fn (
			instance->context,
			instance->iov_buffer[i],
			msg_recv[i].msg_len);
	}
	return (0);
}

static void timer_function_netif_check_timeout (
	void *data)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)data;
	int i;

	for (i=0; i<instance->totem_config->interface_count; i++)
		instance->totemknet_iface_change_fn (instance->context,
						     &instance->my_ids[i],
						     i);
}


/*
 * Create an instance
 */
int totemknet_initialize (
	qb_loop_t *poll_handle,
	void **knet_context,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address,
		unsigned int link_no),

	void (*mtu_changed) (
		void *context,
		int net_mtu),

	void (*target_set_completed) (
		void *context))
{
	struct totemknet_instance *instance;
	int8_t channel=0;
	int res;
	int i;

	instance = malloc (sizeof (struct totemknet_instance));
	if (instance == NULL) {
		return (-1);
	}

	totemknet_instance_initialize (instance);

	instance->totem_config = totem_config;
	instance->stats = stats;

	/*
	* Configure logging
	*/
	instance->totemknet_log_level_security = 1; //totem_config->totem_logging_configuration.log_level_security;
	instance->totemknet_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemknet_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemknet_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemknet_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemknet_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemknet_log_printf = totem_config->totem_logging_configuration.log_printf;

	instance->knet_subsys_id = _logsys_subsys_create("KNET", "libknet.h");

	/*
	 * Initialize local variables for totemknet
	 */

	instance->our_nodeid = instance->totem_config->node_id;

	for (i=0; i< instance->totem_config->interface_count; i++) {
		totemip_copy(&instance->my_ids[i], &totem_config->interfaces[i].bindnet);
		instance->my_ids[i].nodeid = instance->our_nodeid;
		instance->ip_port[i] = totem_config->interfaces[i].ip_port;

		/* Needed for totemsrp */
		totem_config->interfaces[i].boundto.nodeid = instance->our_nodeid;
	}

	instance->poll_handle = poll_handle;

	instance->context = context;
	instance->totemknet_deliver_fn = deliver_fn;

	instance->totemknet_iface_change_fn = iface_change_fn;

	instance->totemknet_mtu_changed = mtu_changed;

	instance->totemknet_target_set_completed = target_set_completed;

	pipe(instance->logpipes);
	fcntl(instance->logpipes[0], F_SETFL, O_NONBLOCK);
	fcntl(instance->logpipes[1], F_SETFL, O_NONBLOCK);

	instance->knet_handle = knet_handle_new(instance->totem_config->node_id, instance->logpipes[1], KNET_LOG_DEBUG);

	if (!instance->knet_handle) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_CRIT, "knet_handle_new failed");
		return (-1);
	}
	res = knet_handle_pmtud_setfreq(instance->knet_handle, 5);
	if (res) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_handle_pmtud_setfreq failed");
	}
	res = knet_handle_enable_filter(instance->knet_handle, instance, dst_host_filter_callback_fn);
	if (res) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_handle_enable_filter failed");
	}
	res = knet_handle_enable_sock_notify(instance->knet_handle, instance, socket_error_callback_fn);
	if (res) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_handle_enable_sock_notify failed");
	}
	res = knet_host_enable_status_change_notify(instance->knet_handle, instance, host_change_callback_fn);
	if (res) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_host_enable_status_change_notify failed");
	}
	res = knet_handle_enable_pmtud_notify(instance->knet_handle, instance, pmtu_change_callback_fn);
	if (res) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_handle_enable_pmtud_notify failed");
	}

	/* Get an fd into knet */
	instance->knet_fd = 0;
	res = knet_handle_add_datafd(instance->knet_handle, &instance->knet_fd, &channel);
	if (res) {
		knet_log_printf(LOG_DEBUG, "knet_handle_add_datafd failed: %s", strerror(errno));
		return -1;
	}

	/* Enable crypto if requested */
	if (strcmp(instance->totem_config->crypto_cipher_type, "none") != 0) {
		struct knet_handle_crypto_cfg crypto_cfg;

		strcpy(crypto_cfg.crypto_model, "nss");
		strcpy(crypto_cfg.crypto_cipher_type, instance->totem_config->crypto_cipher_type);
		strcpy(crypto_cfg.crypto_hash_type, instance->totem_config->crypto_hash_type);
		memcpy(crypto_cfg.private_key, instance->totem_config->private_key, instance->totem_config->private_key_len);
		crypto_cfg.private_key_len = instance->totem_config->private_key_len;

		res = knet_handle_crypto(instance->knet_handle, &crypto_cfg);
		if (res == -1) {
			knet_log_printf(LOG_ERR, "knet_handle_crypto failed: %s", strerror(errno));
			return -1;
		}
		if (res == -2) {
			knet_log_printf(LOG_ERR, "knet_handle_crypto failed: -2");
			return -1;
		}
		knet_log_printf(LOG_INFO, "kronosnet crypto initialized: %s/%s", crypto_cfg.crypto_cipher_type, crypto_cfg.crypto_hash_type);
	}
	knet_handle_setfwd(instance->knet_handle, 1);

	instance->link_mode = KNET_LINK_POLICY_PASSIVE;
	if (strcmp(instance->totem_config->link_mode, "active")==0) {
		instance->link_mode = KNET_LINK_POLICY_ACTIVE;
	}
	if (strcmp(instance->totem_config->link_mode, "rr")==0) {
		instance->link_mode = KNET_LINK_POLICY_RR;
	}

	for (i=0; i<INTERFACE_MAX; i++) {
		instance->link_status[i] = malloc(CFG_INTERFACE_STATUS_MAX_LEN);
		if (!instance->link_status[i]) {
			return -1;
		}
	}

	qb_loop_poll_add (instance->poll_handle,
		QB_LOOP_MED,
		instance->logpipes[0],
		POLLIN, instance, log_deliver_fn);

	qb_loop_poll_add (instance->poll_handle,
		QB_LOOP_HIGH,
		instance->knet_fd,
		POLLIN, instance, data_deliver_fn);

	/*
	 * Upper layer isn't ready to receive message because it hasn't
	 * initialized yet.  Add short timer to check the interfaces.
	 */
	qb_loop_timer_add (instance->poll_handle,
		QB_LOOP_MED,
		100*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		timer_function_netif_check_timeout,
		&instance->timer_netif_check_timeout);

	totemknet_start_merge_detect_timeout(instance);

	knet_log_printf (LOGSYS_LEVEL_INFO, "totemknet initialized");
	*knet_context = instance;
	return (0);
}

void *totemknet_buffer_alloc (void)
{
	return malloc (FRAME_SIZE_MAX);
}

void totemknet_buffer_release (void *ptr)
{
	return free (ptr);
}

int totemknet_processor_count_set (
	void *knet_context,
	int processor_count)
{
	return (0);
}

int totemknet_recv_flush (void *knet_context)
{
	return (0);
}

int totemknet_send_flush (void *knet_context)
{
	return (0);
}

int totemknet_token_send (
	void *knet_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	ucast_sendmsg (instance, &instance->token_target, msg, msg_len);

	return (res);
}
int totemknet_mcast_flush_send (
	void *knet_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len, 0);

	return (res);
}

int totemknet_mcast_noflush_send (
	void *knet_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len, 1);

	return (res);
}


extern int totemknet_iface_check (void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	knet_log_printf(LOG_DEBUG, "totmeknet: iface_check");

	return (res);
}

extern void totemknet_net_mtu_adjust (void *knet_context, struct totem_config *totem_config)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	totem_config->net_mtu -= totemip_udpip_header_size(AF_INET) + 23;
	knet_log_printf(LOG_DEBUG, "totemknet: Returning MTU of %d", totem_config->net_mtu);
}

int totemknet_token_target_set (
	void *knet_context,
	const struct totem_ip_address *token_target)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	memcpy (&instance->token_target, token_target,
		sizeof (struct totem_ip_address));

	instance->totemknet_target_set_completed (instance->context);

	return (res);
}

extern int totemknet_recv_mcast_empty (
	void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	unsigned int res;
	struct sockaddr_storage system_from;
	struct mmsghdr msg_recv[MAX_BUFFERS];
	struct iovec iov_recv[MAX_BUFFERS];
	struct pollfd ufd;
	int nfds;
	int msg_processed = 0;
	int i;

	for (i=0; i<MAX_BUFFERS; i++) {
		iov_recv[i].iov_base = instance->iov_buffer[i];
		iov_recv[i].iov_len = FRAME_SIZE_MAX;

		msg_recv[i].msg_hdr.msg_name = &system_from;
		msg_recv[i].msg_hdr.msg_namelen = sizeof (struct sockaddr_storage);
		msg_recv[i].msg_hdr.msg_iov = &iov_recv[i];
		msg_recv[i].msg_hdr.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
		msg_recv[i].msg_hdr.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
		msg_recv[i].msg_hdr.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
		msg_recv[i].msg_hdr.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
		msg_recv[i].msg_hdr.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
		msg_recv[i].msg_hdr.msg_accrightslen = 0;
#endif
	}

	do {
		ufd.fd = instance->knet_fd;
		ufd.events = POLLIN;
		nfds = poll (&ufd, 1, 0);
		if (nfds == 1 && ufd.revents & POLLIN) {
			res = recvmmsg (instance->knet_fd, msg_recv, MAX_BUFFERS, MSG_NOSIGNAL | MSG_DONTWAIT, NULL);
			if (res != -1) {
				msg_processed = 1;
			} else {
				msg_processed = -1;
			}
		}
	} while (nfds == 1);

	return (msg_processed);
}

int totemknet_member_add (
	void *knet_context,
	const struct totem_ip_address *local,
	const struct totem_ip_address *member,
	int link_no)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int err;
	int port = instance->ip_port[link_no];
	struct sockaddr_storage remote_ss;
	struct sockaddr_storage local_ss;
	int addrlen;

	if (member->nodeid == instance->our_nodeid) {
		return 0; /* Don't add ourself, we send loopback messages directly */
	}

	/* Keep track of the number of links */
	if (link_no > instance->num_links) {
		instance->num_links = link_no;
	}

	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet: member_add: %d (%s), link=%d", member->nodeid, totemip_print(member), link_no);
	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet:      local: %d (%s)", local->nodeid, totemip_print(local));
	if (link_no == 0) {
		if (knet_host_add(instance->knet_handle, member->nodeid)) {
			KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_host_add");
			return -1;
		}

		if (knet_host_set_policy(instance->knet_handle, member->nodeid, instance->link_mode)) {
			KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_set_policy failed");
			return -1;
		}
	}

	/* Casts to remove const */
	totemip_totemip_to_sockaddr_convert((struct totem_ip_address *)member, port+link_no, &remote_ss, &addrlen);
	totemip_totemip_to_sockaddr_convert((struct totem_ip_address *)local, port+link_no, &local_ss, &addrlen);
	err = knet_link_set_config(instance->knet_handle, member->nodeid, link_no, &local_ss, &remote_ss);
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_config failed");
		return -1;
	}

	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet: member_add: Setting link prio to %d",
		    instance->totem_config->interfaces[link_no].knet_link_priority);

	err = knet_link_set_priority(instance->knet_handle, member->nodeid, link_no,
			       instance->totem_config->interfaces[link_no].knet_link_priority);
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_priority for nodeid %d, link %d failed", member->nodeid, link_no);
	}

	err = knet_link_set_ping_timers(instance->knet_handle, member->nodeid, link_no,
				  instance->totem_config->interfaces[link_no].knet_ping_interval,
				  instance->totem_config->interfaces[link_no].knet_ping_timeout,
				  instance->totem_config->interfaces[link_no].knet_ping_precision);
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_ping_timers for nodeid %d, link %d failed", member->nodeid, link_no);
	}

	err = knet_link_set_enable(instance->knet_handle, member->nodeid, link_no, 1);
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_enable for nodeid %d, link %d failed", member->nodeid, link_no);
		return -1;
	}

	return (0);
}

int totemknet_member_remove (
	void *knet_context,
	const struct totem_ip_address *token_target,
	int link_no)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	return knet_host_remove(instance->knet_handle, token_target->nodeid);

}

int totemknet_member_list_rebind_ip (
	void *knet_context)
{
	return (0);
}

static void timer_function_merge_detect_timeout (
	void *data)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)data;

	if (instance->merge_detect_messages_sent_before_timeout == 0) {
		instance->send_merge_detect_message = 1;
	}

	instance->merge_detect_messages_sent_before_timeout = 0;

	totemknet_start_merge_detect_timeout(instance);
}

static void totemknet_start_merge_detect_timeout(
	void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	qb_loop_timer_add(instance->poll_handle,
	    QB_LOOP_MED,
	    instance->totem_config->merge_timeout * 2 * QB_TIME_NS_IN_MSEC,
	    (void *)instance,
	    timer_function_merge_detect_timeout,
	    &instance->timer_merge_detect_timeout);

}

static void totemknet_stop_merge_detect_timeout(
	void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	qb_loop_timer_del(instance->poll_handle,
	    instance->timer_merge_detect_timeout);
}
