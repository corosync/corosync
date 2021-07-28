/*
 * Copyright (c) 2016-2020 Red Hat, Inc.
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
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <limits.h>

#include <qb/qbdefs.h>
#include <qb/qbloop.h>
#ifdef HAVE_LIBNOZZLE
#include <libgen.h>
#include <libnozzle.h>
#endif

#include <corosync/sq.h>
#include <corosync/swab.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>
#include <corosync/totem/totemip.h>
#include "totemknet.h"

#include "main.h"
#include "util.h"

#include <libknet.h>
#include <corosync/totem/totemstats.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef HAVE_LIBNOZZLE
static int setup_nozzle(void *knet_context);
#endif

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
		unsigned int msg_len,
		const struct sockaddr_storage *system_from);

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

	char iov_buffer[KNET_MAX_PACKET_SIZE];

	char *link_status[INTERFACE_MAX];

	struct totem_ip_address my_ids[INTERFACE_MAX];

	uint16_t ip_port[INTERFACE_MAX];

	int our_nodeid;

	int loopback_link;

	struct totem_config *totem_config;

	struct totem_ip_address token_target;

	qb_loop_timer_handle timer_netif_check_timeout;

	qb_loop_timer_handle timer_merge_detect_timeout;

	int send_merge_detect_message;

	unsigned int merge_detect_messages_sent_before_timeout;

	int logpipes[2];
	int knet_fd;

	pthread_mutex_t log_mutex;
#ifdef HAVE_LIBNOZZLE
	char *nozzle_name;
	char *nozzle_ipaddr;
	char *nozzle_prefix;
	char *nozzle_macaddr;
	nozzle_t nozzle_handle;
#endif
};

/* Awkward. But needed to get stats from knet */
struct totemknet_instance *global_instance;

struct work_item {
	const void *msg;
	unsigned int msg_len;
	struct totemknet_instance *instance;
};

int totemknet_member_list_rebind_ip (
	void *knet_context);


static int totemknet_configure_compression (
	struct totemknet_instance *instance,
	struct totem_config *totem_config);

static void totemknet_start_merge_detect_timeout(
	void *knet_context);

static void totemknet_stop_merge_detect_timeout(
	void *knet_context);

static void log_flush_messages (
        void *knet_context);

static void totemknet_instance_initialize (struct totemknet_instance *instance)
{
	int res;

	memset (instance, 0, sizeof (struct totemknet_instance));
	res = pthread_mutex_init(&instance->log_mutex, NULL);
	/*
	 * There is not too much else what can be done.
	 */
	assert(res == 0);
}

#define knet_log_printf_lock(level, subsys, function, file, line, format, args...)	\
do {											\
	(void)pthread_mutex_lock(&instance->log_mutex);					\
	instance->totemknet_log_printf (						\
		level, subsys, function, file, line,					\
		(const char *)format, ##args);						\
	(void)pthread_mutex_unlock(&instance->log_mutex);				\
} while (0);

#define knet_log_printf(level, format, args...)		\
do {							\
        knet_log_printf_lock (				\
		level, instance->totemknet_subsys_id,	\
                __FUNCTION__, __FILE__, __LINE__,	\
		(const char *)format, ##args);		\
} while (0);

#define libknet_log_printf(level, format, args...)	\
do {							\
        knet_log_printf_lock (				\
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


#ifdef HAVE_LIBNOZZLE
static inline int is_ether_addr_multicast(const uint8_t *addr)
{
	return (addr[0] & 0x01);
}
static inline int is_ether_addr_zero(const uint8_t *addr)
{
	return (!addr[0] && !addr[1] && !addr[2] && !addr[3] && !addr[4] && !addr[5]);
}

static int ether_host_filter_fn(void *private_data,
				const unsigned char *outdata,
				ssize_t outdata_len,
				uint8_t tx_rx,
				knet_node_id_t this_host_id,
				knet_node_id_t src_host_id,
				int8_t *channel,
				knet_node_id_t *dst_host_ids,
				size_t *dst_host_ids_entries)
{
	struct ether_header *eth_h = (struct ether_header *)outdata;
	uint8_t *dst_mac = (uint8_t *)eth_h->ether_dhost;
	uint16_t dst_host_id;

	if (is_ether_addr_zero(dst_mac))
		return -1;

	if (is_ether_addr_multicast(dst_mac)) {
		return 1;
	}

	memmove(&dst_host_id, &dst_mac[4], 2);

	dst_host_ids[0] = ntohs(dst_host_id);
	*dst_host_ids_entries = 1;

	return 0;
}
#endif

static int dst_host_filter_callback_fn(void *private_data,
				       const unsigned char *outdata,
				       ssize_t outdata_len,
				       uint8_t tx_rx,
				       knet_node_id_t this_host_id,
				       knet_node_id_t src_host_id,
				       int8_t *channel,
				       knet_node_id_t *dst_host_ids,
				       size_t *dst_host_ids_entries)
{
	struct totem_message_header *header = (struct totem_message_header *)outdata;
	int res;

#ifdef HAVE_LIBNOZZLE
	if (*channel != 0) {
		return ether_host_filter_fn(private_data,
					    outdata, outdata_len,
					    tx_rx,
					    this_host_id, src_host_id,
					    channel,
					    dst_host_ids,
					    dst_host_ids_entries);
	}
#endif
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

static void host_change_callback_fn(void *private_data, knet_node_id_t host_id, uint8_t reachable, uint8_t remote, uint8_t external)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)private_data;

	// TODO: what? if anything.
	knet_log_printf (LOGSYS_LEVEL_DEBUG, "Knet host change callback. nodeid: " CS_PRI_NODE_ID " reachable: %d", host_id, reachable);
}

static void pmtu_change_callback_fn(void *private_data, unsigned int data_mtu)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)private_data;
	knet_log_printf (LOGSYS_LEVEL_DEBUG, "Knet pMTU change: %d", data_mtu);

	/* We don't need to tell corosync the actual knet MTU */
//	instance->totemknet_mtu_changed(instance->context, data_mtu);
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
	struct msghdr msg_ucast;
	struct iovec iovec;

	header->target_nodeid = system_to->nodeid;

	iovec.iov_base = (void *)msg;
	iovec.iov_len = msg_len;

	/*
	 * Build unicast message
	 */
	memset(&msg_ucast, 0, sizeof(msg_ucast));
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

	res = sendmsg (instance->knet_fd, &msg_ucast, MSG_NOSIGNAL);
	if (res < 0) {
		KNET_LOGSYS_PERROR (errno, instance->totemknet_log_level_debug,
				    "sendmsg(ucast) failed (non-critical)");
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
	struct msghdr msg_mcast;
	struct iovec iovec;

	iovec.iov_base = (void *)msg;
	iovec.iov_len = msg_len;

	header->target_nodeid = 0;

	/*
	 * Build multicast message
	 */
	memset(&msg_mcast, 0, sizeof(msg_mcast));
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


//	log_printf (LOGSYS_LEVEL_DEBUG, "totemknet: mcast_sendmsg. only_active=%d, len=%d", only_active, msg_len);

	res = sendmsg (instance->knet_fd, &msg_mcast, MSG_NOSIGNAL);
	if (res < msg_len) {
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "totemknet: mcast_send sendmsg returned %d", res);
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

#ifndef OWN_INDEX_NONE
#define OWN_INDEX_NONE -1
#endif

int totemknet_nodestatus_get (
	void *knet_context,
	unsigned int nodeid,
	struct totem_node_status *node_status)
{
	int i;
	int res = 0;
	struct knet_link_status link_status;
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	struct knet_host_status knet_host_status;
	uint8_t link_list[KNET_MAX_LINK];
	size_t num_links;

	if (!instance->knet_handle) {
		return CS_ERR_NOT_EXIST; /* Not using knet */
	}

	if (!node_status) {
		return CS_ERR_INVALID_PARAM;
	}

	res = knet_host_get_status(instance->knet_handle,
				   nodeid,
				   &knet_host_status);
	if (res) {
		knet_log_printf (LOGSYS_LEVEL_WARNING, "knet_handle_get_host_status(%d) failed: %d", nodeid, res);
		return (-1);
	}
	node_status->nodeid = nodeid;
	node_status->reachable = knet_host_status.reachable;
	node_status->remote = knet_host_status.remote;
	node_status->external = knet_host_status.external;

#ifdef HAVE_KNET_ONWIRE_VER
	res = knet_handle_get_onwire_ver(instance->knet_handle,
					 nodeid,
					 &node_status->onwire_min,
					 &node_status->onwire_max,
					 &node_status->onwire_ver);
	if (res) {
		knet_log_printf (LOGSYS_LEVEL_WARNING, "knet_handle_get_onwire_ver(%d) failed: %d", nodeid, res);
		return (-1);
	}
#endif
	/* Get link info */
	res = knet_link_get_link_list(instance->knet_handle,
				      nodeid, link_list, &num_links);
	if (res) {
		knet_log_printf (LOGSYS_LEVEL_WARNING, "knet_link_get_link_list(%d) failed: %d", nodeid, res);
		return (-1);
	}

	/* node_status[] has been zeroed for us in totempg.c */
	for (i=0; i < num_links; i++) {
		if (!instance->totem_config->interfaces[link_list[i]].configured) {
			continue;
		}
		res = knet_link_get_status(instance->knet_handle,
					   nodeid,
					   link_list[i],
					   &link_status,
					   sizeof(link_status));
		if (res == 0) {
			node_status->link_status[link_list[i]].enabled = link_status.enabled;
			node_status->link_status[link_list[i]].connected = link_status.connected;
			node_status->link_status[link_list[i]].dynconnected = link_status.dynconnected;
			node_status->link_status[link_list[i]].mtu = link_status.mtu;
			memcpy(node_status->link_status[link_list[i]].src_ipaddr, link_status.src_ipaddr, KNET_MAX_HOST_LEN);
			memcpy(node_status->link_status[link_list[i]].dst_ipaddr, link_status.dst_ipaddr, KNET_MAX_HOST_LEN);
		} else {
			knet_log_printf (LOGSYS_LEVEL_WARNING, "knet_link_get_link_status(%d, %d) failed: %d", nodeid, link_list[i], res);
		}
	}
	return res;
}



int totemknet_ifaces_get (void *knet_context,
	char ***status,
	unsigned int *iface_count)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	struct knet_link_status link_status;
	knet_node_id_t host_list[KNET_MAX_HOST];
	uint8_t link_list[KNET_MAX_LINK];
	size_t num_hosts;
	size_t num_links;
	size_t link_idx;
	int i,j;
	char *ptr;
	int res = 0;

	/*
	 * Don't do the whole 'link_info' bit if the caller just wants
	 * a count of interfaces.
	 */
	if (status) {
		int own_idx = OWN_INDEX_NONE;

		res = knet_host_get_host_list(instance->knet_handle,
					      host_list, &num_hosts);
		if (res) {
			return (-1);
		}
		qsort(host_list, num_hosts, sizeof(uint16_t), node_compare);

		for (j=0; j<num_hosts; j++) {
			if (host_list[j] == instance->our_nodeid) {
				own_idx = j;
				break;
			}
		}

		for (i=0; i<INTERFACE_MAX; i++) {
			memset(instance->link_status[i], 'd', CFG_INTERFACE_STATUS_MAX_LEN-1);
			if (own_idx != OWN_INDEX_NONE) {
				instance->link_status[i][own_idx] = 'n';
			}
			instance->link_status[i][num_hosts] = '\0';
		}

		/* This is all a bit "inside-out" because "status" is a set of strings per link
		 * and knet orders things by host
		 */
		for (j=0; j<num_hosts; j++) {
			if (own_idx != OWN_INDEX_NONE && j == own_idx) {
				continue ;
			}

			res = knet_link_get_link_list(instance->knet_handle,
						      host_list[j], link_list, &num_links);
			if (res) {
				return (-1);
			}

			link_idx = 0;
			for (i=0; i < num_links; i++) {
				/*
				 * Skip over links that are unconfigured to corosync. This is basically
				 * link0 if corosync isn't using it for comms, as we will still
				 * have it set up for loopback.
				 */
				if (!instance->totem_config->interfaces[link_list[i]].configured) {
					continue;
				}
				ptr = instance->link_status[link_idx++];

				res = knet_link_get_status(instance->knet_handle,
							   host_list[j],
							   link_list[i],
							   &link_status,
							   sizeof(link_status));
				if (res == 0) {
					ptr[j] = '0' + (link_status.enabled |
							link_status.connected<<1 |
							link_status.dynconnected<<2);
				}
				else {
					knet_log_printf (LOGSYS_LEVEL_ERROR,
					    "totemknet_ifaces_get: Cannot get link status: %s", strerror(errno));
					ptr[j] = '?';
				}
			}
		}
		*status = instance->link_status;
	}

	*iface_count = INTERFACE_MAX;

	return (res);
}

int totemknet_finalize (
	void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;
	int i,j;
	static knet_node_id_t nodes[KNET_MAX_HOST]; /* static to save stack */
	uint8_t links[KNET_MAX_LINK];
	size_t num_nodes;
	size_t num_links;

	knet_log_printf(LOG_DEBUG, "totemknet: finalize");

	qb_loop_poll_del (instance->poll_handle, instance->logpipes[0]);
	qb_loop_poll_del (instance->poll_handle, instance->knet_fd);

	/*
	 * Disable forwarding to make knet flush send queue. This ensures that the LEAVE message will be sent.
	 */
	res = knet_handle_setfwd(instance->knet_handle, 0);
	if (res) {
		knet_log_printf (LOGSYS_LEVEL_CRIT, "totemknet: knet_handle_setfwd failed: %s", strerror(errno));
	}

	res = knet_host_get_host_list(instance->knet_handle, nodes, &num_nodes);
	if (res) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Cannot get knet node list for shutdown: %s", strerror(errno));
		/* Crash out anyway */
		goto finalise_error;
	}

	/* Tidily shut down all nodes & links. */
	for (i=0; i<num_nodes; i++) {

		res = knet_link_get_link_list(instance->knet_handle, nodes[i], links, &num_links);
		if (res) {
			knet_log_printf (LOGSYS_LEVEL_ERROR, "Cannot get knet link list for node " CS_PRI_NODE_ID ": %s", nodes[i], strerror(errno));
			goto finalise_error;
		}
		for (j=0; j<num_links; j++) {
			res = knet_link_set_enable(instance->knet_handle, nodes[i], links[j], 0);
			if (res) {
				knet_log_printf (LOGSYS_LEVEL_ERROR, "totemknet: knet_link_set_enable(node " CS_PRI_NODE_ID ", link %d) failed: %s", nodes[i], links[j], strerror(errno));
			}
			res = knet_link_clear_config(instance->knet_handle, nodes[i], links[j]);
			if (res) {
				knet_log_printf (LOGSYS_LEVEL_ERROR, "totemknet: knet_link_clear_config(node " CS_PRI_NODE_ID ", link %d) failed: %s", nodes[i], links[j], strerror(errno));
			}
		}
		res = knet_host_remove(instance->knet_handle, nodes[i]);
		if (res) {
			knet_log_printf (LOGSYS_LEVEL_ERROR, "totemknet: knet_host_remove(node " CS_PRI_NODE_ID ") failed: %s", nodes[i], strerror(errno));
		}
	}

finalise_error:
	res = knet_handle_free(instance->knet_handle);
	if (res) {
		knet_log_printf (LOGSYS_LEVEL_CRIT, "totemknet: knet_handle_free failed: %s", strerror(errno));
	}

	totemknet_stop_merge_detect_timeout(instance);

	log_flush_messages(instance);

	/*
	 * Error is deliberately ignored
	 */
	(void)pthread_mutex_destroy(&instance->log_mutex);

	return (res);
}

static int log_deliver_fn (
	int fd,
	int revents,
	void *data)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)data;
	char buffer[sizeof(struct knet_log_msg)*4];
	char *bufptr = buffer;
	int done = 0;
	int len;

	len = read(fd, buffer, sizeof(buffer));
	while (done < len) {
		struct knet_log_msg *msg = (struct knet_log_msg *)bufptr;
		switch (msg->msglevel) {
		case KNET_LOG_ERR:
			libknet_log_printf (LOGSYS_LEVEL_ERROR, "%s: %s",
					    knet_log_get_subsystem_name(msg->subsystem),
					    msg->msg);
			break;
		case KNET_LOG_WARN:
			libknet_log_printf (LOGSYS_LEVEL_WARNING, "%s: %s",
					    knet_log_get_subsystem_name(msg->subsystem),
					    msg->msg);
			break;
		case KNET_LOG_INFO:
			libknet_log_printf (LOGSYS_LEVEL_INFO, "%s: %s",
					    knet_log_get_subsystem_name(msg->subsystem),
					    msg->msg);
			break;
		case KNET_LOG_DEBUG:
			libknet_log_printf (LOGSYS_LEVEL_DEBUG, "%s: %s",
					    knet_log_get_subsystem_name(msg->subsystem),
					    msg->msg);
			break;
		}
		bufptr += sizeof(struct knet_log_msg);
		done += sizeof(struct knet_log_msg);
	}
	return 0;
}

static int data_deliver_fn (
	int fd,
	int revents,
	void *data)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)data;
	struct msghdr msg_hdr;
	struct iovec iov_recv;
	struct sockaddr_storage system_from;
	ssize_t msg_len;
	int truncated_packet;

	iov_recv.iov_base = instance->iov_buffer;
	iov_recv.iov_len = KNET_MAX_PACKET_SIZE;

	msg_hdr.msg_name = &system_from;
	msg_hdr.msg_namelen = sizeof (struct sockaddr_storage);
	msg_hdr.msg_iov = &iov_recv;
	msg_hdr.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
	msg_hdr.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
	msg_hdr.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
	msg_hdr.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
	msg_hdr.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
	msg_hdr.msg_accrightslen = 0;
#endif

	msg_len = recvmsg (fd, &msg_hdr, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (msg_len <= 0) {
		return (0);
	}

	truncated_packet = 0;

#ifdef HAVE_MSGHDR_FLAGS
	if (msg_hdr.msg_flags & MSG_TRUNC) {
		truncated_packet = 1;
	}
#else
	/*
	 * We don't have MSGHDR_FLAGS, but we can (hopefully) safely make assumption that
	 * if bytes_received == KNET_MAX_PACKET_SIZE then packet is truncated
	 */
	if (bytes_received == KNET_MAX_PACKET_SIZE) {
		truncated_packet = 1;
	}
#endif

	if (truncated_packet) {
		knet_log_printf(instance->totemknet_log_level_error,
				"Received too big message. This may be because something bad is happening"
				"on the network (attack?), or you tried join more nodes than corosync is"
				"compiled with (%u) or bug in the code (bad estimation of "
				"the KNET_MAX_PACKET_SIZE). Dropping packet.", PROCESSOR_COUNT_MAX);
		return (0);
	}

	/*
	 * Handle incoming message
	 */
	instance->totemknet_deliver_fn (
		instance->context,
		instance->iov_buffer,
		msg_len,
		&system_from);

	return (0);
}

static void timer_function_netif_check_timeout (
	void *data)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)data;
	int i;

	for (i=0; i < INTERFACE_MAX; i++) {
		if (!instance->totem_config->interfaces[i].configured) {
			continue;
		}
		instance->totemknet_iface_change_fn (instance->context,
						     &instance->my_ids[i],
						     i);
	}
}

static void knet_set_access_list_config(struct totemknet_instance *instance)
{
#ifdef HAVE_KNET_ACCESS_LIST
	uint32_t value;
	cs_error_t err;

	value = instance->totem_config->block_unlisted_ips;
	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet_enable access list: %d", value);

	err = knet_handle_enable_access_lists(instance->knet_handle, value);
	if (err) {
	        KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_handle_enable_access_lists failed");
	}
#endif
}


/* NOTE: this relies on the fact that totem_reload_notify() is called first */
static void totemknet_refresh_config(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	uint8_t reloading;
	uint32_t value;
	uint32_t link_no;
	size_t num_nodes;
	knet_node_id_t host_ids[KNET_MAX_HOST];
	int i;
	int err;
	struct totemknet_instance *instance = (struct totemknet_instance *)user_data;

	ENTER();

	/*
	 * If a full reload is in progress then don't do anything until it's done and
	 * can reconfigure it all atomically
	 */
	if (icmap_get_uint8("config.totemconfig_reload_in_progress", &reloading) == CS_OK && reloading) {
		return;
	}

	knet_set_access_list_config(instance);

	if (icmap_get_uint32("totem.knet_pmtud_interval", &value) == CS_OK) {

		instance->totem_config->knet_pmtud_interval = value;
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet_pmtud_interval now %d", value);
		err = knet_handle_pmtud_setfreq(instance->knet_handle, instance->totem_config->knet_pmtud_interval);
		if (err) {
			KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_handle_pmtud_setfreq failed");
		}
	}

	/* Configure link parameters for each node */
	err = knet_host_get_host_list(instance->knet_handle, host_ids, &num_nodes);
	if (err != 0) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_host_get_host_list failed");
	}

	for (i=0; i<num_nodes; i++) {
		for (link_no = 0; link_no < INTERFACE_MAX; link_no++) {
			if (host_ids[i] == instance->our_nodeid || !instance->totem_config->interfaces[link_no].configured) {
				continue;
			}

			err = knet_link_set_ping_timers(instance->knet_handle, host_ids[i], link_no,
							instance->totem_config->interfaces[link_no].knet_ping_interval,
							instance->totem_config->interfaces[link_no].knet_ping_timeout,
							instance->totem_config->interfaces[link_no].knet_ping_precision);
			if (err) {
				KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_ping_timers for node " CS_PRI_NODE_ID " link %d failed", host_ids[i], link_no);
			}
			err = knet_link_set_pong_count(instance->knet_handle, host_ids[i], link_no,
						       instance->totem_config->interfaces[link_no].knet_pong_count);
			if (err) {
				KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_pong_count for node " CS_PRI_NODE_ID " link %d failed",host_ids[i], link_no);
			}
			err = knet_link_set_priority(instance->knet_handle, host_ids[i], link_no,
						     instance->totem_config->interfaces[link_no].knet_link_priority);
			if (err) {
				KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_priority for node " CS_PRI_NODE_ID " link %d failed", host_ids[i], link_no);
			}

		}
	}

	LEAVE();
}

static void totemknet_add_config_notifications(struct totemknet_instance *instance)
{
	icmap_track_t icmap_track_totem = NULL;
	icmap_track_t icmap_track_reload = NULL;

	ENTER();

	icmap_track_add("totem.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		totemknet_refresh_config,
		instance,
		&icmap_track_totem);

	icmap_track_add("config.totemconfig_reload_in_progress",
		ICMAP_TRACK_ADD | ICMAP_TRACK_MODIFY,
		totemknet_refresh_config,
		instance,
		&icmap_track_reload);

	LEAVE();
}

static int totemknet_is_crypto_enabled(const struct totemknet_instance *instance)
{

	return (!(strcmp(instance->totem_config->crypto_cipher_type, "none") == 0 &&
	    strcmp(instance->totem_config->crypto_hash_type, "none") == 0));

}

static int totemknet_set_knet_crypto(struct totemknet_instance *instance)
{
	struct knet_handle_crypto_cfg crypto_cfg;
	int res;

	/* These have already been validated */
	memcpy(crypto_cfg.crypto_model, instance->totem_config->crypto_model, sizeof(crypto_cfg.crypto_model));
	memcpy(crypto_cfg.crypto_cipher_type, instance->totem_config->crypto_cipher_type, sizeof(crypto_cfg.crypto_model));
	memcpy(crypto_cfg.crypto_hash_type, instance->totem_config->crypto_hash_type, sizeof(crypto_cfg.crypto_model));
	memcpy(crypto_cfg.private_key, instance->totem_config->private_key, instance->totem_config->private_key_len);
	crypto_cfg.private_key_len = instance->totem_config->private_key_len;

#ifdef HAVE_KNET_CRYPTO_RECONF

	knet_log_printf(LOGSYS_LEVEL_DEBUG, "Configuring crypto %s/%s/%s on index %d",
			crypto_cfg.crypto_model,
			crypto_cfg.crypto_cipher_type,
			crypto_cfg.crypto_hash_type,
			instance->totem_config->crypto_index
		);

	/* If crypto is being disabled we need to explicitly allow cleartext traffic in knet */
	if (!totemknet_is_crypto_enabled(instance)) {
		res = knet_handle_crypto_rx_clear_traffic(instance->knet_handle, KNET_CRYPTO_RX_ALLOW_CLEAR_TRAFFIC);
		if (res) {
			knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto_rx_clear_traffic(ALLOW) failed %s", strerror(errno));
		}
	}

	/* use_config will be called later when all nodes are synced */
	res = knet_handle_crypto_set_config(instance->knet_handle, &crypto_cfg, instance->totem_config->crypto_index);
	if (res == -1) {
		knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto_set_config (index %d) failed: %s", instance->totem_config->crypto_index, strerror(errno));
		goto exit_error;
	}
	if (res == -2) {
		knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto_set_config (index %d) failed: -2", instance->totem_config->crypto_index);
		goto exit_error;
	}
#else
	knet_log_printf(LOGSYS_LEVEL_DEBUG, "Configuring crypto %s/%s/%s",
			crypto_cfg.crypto_model,
			crypto_cfg.crypto_cipher_type,
			crypto_cfg.crypto_hash_type
		);

	res = knet_handle_crypto(instance->knet_handle, &crypto_cfg);
	if (res == -1) {
	knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto failed: %s", strerror(errno));
		goto exit_error;
	}
	if (res == -2) {
		knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto failed: -2");
		goto exit_error;
	}
#endif


exit_error:
	return res;
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
		unsigned int msg_len,
		const struct sockaddr_storage *system_from),

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
	char *tmp_str;
	int8_t channel=0;
	int allow_knet_handle_fallback=0;
	int res;
	int i;

	instance = malloc (sizeof (struct totemknet_instance));
	if (instance == NULL) {
		return (-1);
	}

	totemknet_instance_initialize (instance);

	instance->totem_config = totem_config;

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

	for (i=0; i< INTERFACE_MAX; i++) {
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

	instance->loopback_link = 0;

	res = pipe(instance->logpipes);
	if (res == -1) {
	    KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_CRIT, "failed to create pipe for instance->logpipes");
	    goto exit_error;
	}
	if (fcntl(instance->logpipes[0], F_SETFL, O_NONBLOCK) == -1 ||
	    fcntl(instance->logpipes[1], F_SETFL, O_NONBLOCK) == -1) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_CRIT, "failed to set O_NONBLOCK flag for instance->logpipes");
		goto exit_error;
	}

	if (icmap_get_string("system.allow_knet_handle_fallback", &tmp_str) == CS_OK) {
		if (strcmp(tmp_str, "yes") == 0) {
			allow_knet_handle_fallback = 1;
		}
		free(tmp_str);
	}

#if defined(KNET_API_VER) && (KNET_API_VER == 2)
	instance->knet_handle = knet_handle_new(instance->totem_config->node_id, instance->logpipes[1], KNET_LOG_DEBUG, KNET_HANDLE_FLAG_PRIVILEGED);
#else
	instance->knet_handle = knet_handle_new(instance->totem_config->node_id, instance->logpipes[1], KNET_LOG_DEBUG);
#endif

	if (allow_knet_handle_fallback && !instance->knet_handle && errno == ENAMETOOLONG) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING, "knet_handle_new failed, trying unprivileged");
#if defined(KNET_API_VER) && (KNET_API_VER == 2)
		instance->knet_handle = knet_handle_new(instance->totem_config->node_id, instance->logpipes[1], KNET_LOG_DEBUG, 0);
#else
		instance->knet_handle = knet_handle_new_ex(instance->totem_config->node_id, instance->logpipes[1], KNET_LOG_DEBUG, 0);
#endif
	}

	if (!instance->knet_handle) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_CRIT, "knet_handle_new failed");
		goto exit_error;
	}

	knet_set_access_list_config(instance);

	res = knet_handle_pmtud_setfreq(instance->knet_handle, instance->totem_config->knet_pmtud_interval);
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
	global_instance = instance;

	/* Get an fd into knet */
	instance->knet_fd = 0;
	res = knet_handle_add_datafd(instance->knet_handle, &instance->knet_fd, &channel);
	if (res) {
		knet_log_printf(LOG_DEBUG, "knet_handle_add_datafd failed: %s", strerror(errno));
		goto exit_error;
	}

	/* Enable crypto if requested */
#ifdef HAVE_KNET_CRYPTO_RECONF
	if (totemknet_is_crypto_enabled(instance)) {
	        res = totemknet_set_knet_crypto(instance);
		if (res == 0) {
			res = knet_handle_crypto_use_config(instance->knet_handle, totem_config->crypto_index);
			if (res) {
				knet_log_printf(LOG_DEBUG, "knet_handle_crypto_use_config failed: %s", strerror(errno));
				goto exit_error;
			}
		} else {
			knet_log_printf(LOG_DEBUG, "Failed to set up knet crypto");
			goto exit_error;
		}
		res = knet_handle_crypto_rx_clear_traffic(instance->knet_handle, KNET_CRYPTO_RX_DISALLOW_CLEAR_TRAFFIC);
		if (res) {
			knet_log_printf(LOG_DEBUG, "knet_handle_crypto_rx_clear_traffic (DISALLOW) failed: %s", strerror(errno));
			goto exit_error;
		}

	} else {
		res = knet_handle_crypto_rx_clear_traffic(instance->knet_handle, KNET_CRYPTO_RX_ALLOW_CLEAR_TRAFFIC);
		if (res) {
			knet_log_printf(LOG_DEBUG, "knet_handle_crypto_rx_clear_traffic (ALLOW) failed: %s", strerror(errno));
			goto exit_error;
		}
	}
#else
	if (totemknet_is_crypto_enabled(instance)) {
		res = totemknet_set_knet_crypto(instance);
		if (res) {
			knet_log_printf(LOG_DEBUG, "Failed to set up knet crypto");
			goto exit_error;
		}
	}
#endif

	/* Set up compression */
	if (strcmp(totem_config->knet_compression_model, "none") != 0) {
		/* Not fatal, but will log */
		(void)totemknet_configure_compression(instance, totem_config);
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
			goto exit_error;
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

	/* Start listening for config changes */
	totemknet_add_config_notifications(instance);

	/* Add stats keys to icmap */
	stats_knet_add_handle();

	knet_log_printf (LOGSYS_LEVEL_INFO, "totemknet initialized");
	*knet_context = instance;

	return (0);

exit_error:
	log_flush_messages(instance);
	free(instance);
	return (-1);
}

void *totemknet_buffer_alloc (void)
{
	/* Need to have space for a message AND a struct mcast in case of encapsulated messages */
	return malloc(KNET_MAX_PACKET_SIZE + 512);
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

	knet_log_printf(LOG_DEBUG, "totemknet: iface_check");

	return (res);
}

extern void totemknet_net_mtu_adjust (void *knet_context, struct totem_config *totem_config)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	knet_log_printf(LOG_DEBUG, "totemknet: Returning MTU of %d", totem_config->net_mtu);
}

int totemknet_token_target_set (
	void *knet_context,
	unsigned int nodeid)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	instance->token_target.nodeid = nodeid;

	instance->totemknet_target_set_completed (instance->context);

	return (res);
}

extern int totemknet_recv_mcast_empty (
	void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	unsigned int res;
	struct sockaddr_storage system_from;
	struct msghdr msg_hdr;
	struct iovec iov_recv;
	struct pollfd ufd;
	int nfds;
	int msg_processed = 0;

	iov_recv.iov_base = instance->iov_buffer;
	iov_recv.iov_len = KNET_MAX_PACKET_SIZE;

	msg_hdr.msg_name = &system_from;
	msg_hdr.msg_namelen = sizeof (struct sockaddr_storage);
	msg_hdr.msg_iov = &iov_recv;
	msg_hdr.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_CONTROL
	msg_hdr.msg_control = 0;
#endif
#ifdef HAVE_MSGHDR_CONTROLLEN
	msg_hdr.msg_controllen = 0;
#endif
#ifdef HAVE_MSGHDR_FLAGS
	msg_hdr.msg_flags = 0;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTS
	msg_msg_hdr.msg_accrights = NULL;
#endif
#ifdef HAVE_MSGHDR_ACCRIGHTSLEN
	msg_msg_hdr.msg_accrightslen = 0;
#endif

	do {
		ufd.fd = instance->knet_fd;
		ufd.events = POLLIN;
		nfds = poll (&ufd, 1, 0);
		if (nfds == 1 && ufd.revents & POLLIN) {
			res = recvmsg (instance->knet_fd, &msg_hdr, MSG_NOSIGNAL | MSG_DONTWAIT);
			if (res != -1) {
				msg_processed = 1;
			} else {
				msg_processed = -1;
			}
		}
	} while (nfds == 1);

	return (msg_processed);
}

int totemknet_iface_set (void *knet_context,
	const struct totem_ip_address *local_addr,
	unsigned short ip_port,
	unsigned int iface_no)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	totemip_copy(&instance->my_ids[iface_no], local_addr);

	knet_log_printf(LOG_INFO, "Configured link number %d: local addr: %s, port=%d", iface_no, totemip_print(local_addr), ip_port);

	instance->ip_port[iface_no] = ip_port;

	return 0;
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
	int i;
	int host_found = 0;
	knet_node_id_t host_ids[KNET_MAX_HOST];
	size_t num_host_ids;

	/* Only create 1 loopback link and use link 0 */
	if (member->nodeid == instance->our_nodeid) {
		if (!instance->loopback_link) {
			link_no = 0;
			instance->loopback_link = 1;
		} else {
			/* Already done */
			return 0;
		}
	}

	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet: member_add: " CS_PRI_NODE_ID " (%s), link=%d", member->nodeid, totemip_print(member), link_no);
	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet:      local: " CS_PRI_NODE_ID " (%s)", local->nodeid, totemip_print(local));


	/* Only add the host if it doesn't already exist in knet */
	err = knet_host_get_host_list(instance->knet_handle, host_ids, &num_host_ids);
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_host_get_host_list");
		return -1;
	}
	for (i=0; i<num_host_ids; i++) {
		if (host_ids[i] == member->nodeid) {
			host_found = 1;
		}
	}

	if (!host_found) {
		err = knet_host_add(instance->knet_handle, member->nodeid);
		if (err != 0 && errno != EEXIST) {
			KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_host_add");
			return -1;
		}
	} else {
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "nodeid " CS_PRI_NODE_ID " already added", member->nodeid);
	}


	if (err == 0) {
		if (knet_host_set_policy(instance->knet_handle, member->nodeid, instance->link_mode)) {
			KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_set_policy failed");
			return -1;
		}
	}

	memset(&local_ss, 0, sizeof(local_ss));
	memset(&remote_ss, 0, sizeof(remote_ss));
	/* Casts to remove const */
	totemip_totemip_to_sockaddr_convert((struct totem_ip_address *)member, port, &remote_ss, &addrlen);
	totemip_totemip_to_sockaddr_convert((struct totem_ip_address *)local, port, &local_ss, &addrlen);

	if (member->nodeid == instance->our_nodeid) {
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet: loopback link is %d\n", link_no);

		err = knet_link_set_config(instance->knet_handle, member->nodeid, link_no,
					   KNET_TRANSPORT_LOOPBACK,
					   &local_ss, &remote_ss, KNET_LINK_FLAG_TRAFFICHIPRIO);
	}
	else {
		err = knet_link_set_config(instance->knet_handle, member->nodeid, link_no,
					   instance->totem_config->interfaces[link_no].knet_transport,
					   &local_ss, &remote_ss, KNET_LINK_FLAG_TRAFFICHIPRIO);
	}
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_config failed");
		return -1;
	}

	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet: member_add: Setting link prio to %d",
		    instance->totem_config->interfaces[link_no].knet_link_priority);

	err = knet_link_set_priority(instance->knet_handle, member->nodeid, link_no,
			       instance->totem_config->interfaces[link_no].knet_link_priority);
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_priority for nodeid " CS_PRI_NODE_ID ", link %d failed", member->nodeid, link_no);
	}

	/* ping timeouts maybe 0 here for a newly added interface so we leave this till later, it will
	   get done in totemknet_refresh_config */
	if (instance->totem_config->interfaces[link_no].knet_ping_interval != 0) {
		err = knet_link_set_ping_timers(instance->knet_handle, member->nodeid, link_no,
						instance->totem_config->interfaces[link_no].knet_ping_interval,
						instance->totem_config->interfaces[link_no].knet_ping_timeout,
						instance->totem_config->interfaces[link_no].knet_ping_precision);
		if (err) {
			KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_ping_timers for nodeid " CS_PRI_NODE_ID ", link %d failed", member->nodeid, link_no);
		}
		err = knet_link_set_pong_count(instance->knet_handle, member->nodeid, link_no,
					       instance->totem_config->interfaces[link_no].knet_pong_count);
		if (err) {
			KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_pong_count for nodeid " CS_PRI_NODE_ID ", link %d failed", member->nodeid, link_no);
		}
	}

	err = knet_link_set_enable(instance->knet_handle, member->nodeid, link_no, 1);
	if (err) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set_enable for nodeid " CS_PRI_NODE_ID ", link %d failed", member->nodeid, link_no);
		return -1;
	}

	/* register stats */
	stats_knet_add_member(member->nodeid, link_no);
	return (0);
}

int totemknet_member_remove (
	void *knet_context,
	const struct totem_ip_address *token_target,
	int link_no)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res;
	uint8_t link_list[KNET_MAX_LINK];
	size_t num_links;

	knet_log_printf (LOGSYS_LEVEL_DEBUG, "knet: member_remove: " CS_PRI_NODE_ID ", link=%d", token_target->nodeid, link_no);

	/* Don't remove the link with the loopback on it until we shut down */
	if (token_target->nodeid == instance->our_nodeid) {
		return 0;
	}

	/* Tidy stats */
	stats_knet_del_member(token_target->nodeid, link_no);

	/* Remove the link first */
	res = knet_link_set_enable(instance->knet_handle, token_target->nodeid, link_no, 0);
	if (res != 0) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_set enable(off) for nodeid " CS_PRI_NODE_ID ", link %d failed", token_target->nodeid, link_no);
		return res;
	}

	res = knet_link_clear_config(instance->knet_handle, token_target->nodeid, link_no);
	if (res != 0) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_link_clear_config for nodeid " CS_PRI_NODE_ID ", link %d failed", token_target->nodeid, link_no);
		return res;
	}

	/* If this is the last link, then remove the node */
	res = knet_link_get_link_list(instance->knet_handle,
				      token_target->nodeid, link_list, &num_links);
	if (res) {
		return (0); /* not really  failure */
	}

	if (num_links == 0) {
		res = knet_host_remove(instance->knet_handle, token_target->nodeid);
	}
	return res;
}

int totemknet_member_list_rebind_ip (
	void *knet_context)
{
	return (0);
}


static int totemknet_configure_compression (
	struct totemknet_instance *instance,
	struct totem_config *totem_config)
{
	struct knet_handle_compress_cfg compress_cfg;
	int res = 0;

	assert(strlen(totem_config->knet_compression_model) < sizeof(compress_cfg.compress_model));
	strcpy(compress_cfg.compress_model, totem_config->knet_compression_model);

	compress_cfg.compress_threshold = totem_config->knet_compression_threshold;
	compress_cfg.compress_level = totem_config->knet_compression_level;

	res = knet_handle_compress(instance->knet_handle, &compress_cfg);
	if (res) {
		KNET_LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR, "knet_handle_compress failed");
	}
	return res;
}

int totemknet_reconfigure (
	void *knet_context,
	struct totem_config *totem_config)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res = 0;

	(void)totemknet_configure_compression(instance, totem_config);

#ifdef HAVE_LIBNOZZLE
	/* Set up nozzle device(s). Return code is ignored, because inability
	 * configure nozzle is not fatal problem, errors are logged and
	 * there is not much else we can do */
	(void)setup_nozzle(instance);
#endif

	if (totem_config->crypto_changed) {
		/* Flip crypto_index */
		totem_config->crypto_index = 3-totem_config->crypto_index;
		res = totemknet_set_knet_crypto(instance);

		knet_log_printf(LOG_INFO, "kronosnet crypto reconfigured on index %d: %s/%s/%s", totem_config->crypto_index,
				totem_config->crypto_model,
				totem_config->crypto_cipher_type,
				totem_config->crypto_hash_type);
	}
	return (res);
}


int totemknet_crypto_reconfigure_phase (
	void *knet_context,
	struct totem_config *totem_config,
	cfg_message_crypto_reconfig_phase_t phase)
{
#ifdef HAVE_KNET_CRYPTO_RECONF
	int res;
	int config_to_use;
	int config_to_clear;
	struct knet_handle_crypto_cfg crypto_cfg;
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	knet_log_printf(LOGSYS_LEVEL_DEBUG, "totemknet_crypto_reconfigure_phase %d, index=%d\n", phase, totem_config->crypto_index);

	switch (phase) {
		case CRYPTO_RECONFIG_PHASE_ACTIVATE:
			config_to_use = totem_config->crypto_index;
			if (!totemknet_is_crypto_enabled(instance)) {
				config_to_use = 0; /* we are clearing it */
			}

			/* Enable the new config on this node */
			res = knet_handle_crypto_use_config(instance->knet_handle, config_to_use);
			if (res == -1) {
				knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto_use_config %d failed: %s", config_to_use, strerror(errno));
			}
			break;

		case CRYPTO_RECONFIG_PHASE_CLEANUP:
			/*
			 * All nodes should now have the new config. clear the old one out
			 * OR disable crypto entirely if that's what the new config insists on.
			 */
			config_to_clear = 3-totem_config->crypto_index;
			knet_log_printf(LOGSYS_LEVEL_DEBUG, "Clearing old knet crypto config %d\n", config_to_clear);

			strcpy(crypto_cfg.crypto_model, "none");
			strcpy(crypto_cfg.crypto_cipher_type, "none");
			strcpy(crypto_cfg.crypto_hash_type, "none");
			res = knet_handle_crypto_set_config(instance->knet_handle, &crypto_cfg, config_to_clear);
			if (res == -1) {
				knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto_set_config to clear index %d failed: %s", config_to_clear, strerror(errno));
			}
			if (res == -2) {
				knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto_set_config to clear index %d failed: -2", config_to_clear);
			}

			/* If crypto is enabled then disable all cleartext reception */
			if (totemknet_is_crypto_enabled(instance)) {
				res = knet_handle_crypto_rx_clear_traffic(instance->knet_handle, KNET_CRYPTO_RX_DISALLOW_CLEAR_TRAFFIC);
				if (res) {
					knet_log_printf(LOGSYS_LEVEL_ERROR, "knet_handle_crypto_rx_clear_traffic(DISALLOW) failed %s", strerror(errno));
				}
			}
	}
#endif
	return 0;
}

void totemknet_stats_clear (
	void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;

	(void) knet_handle_clear_stats(instance->knet_handle, KNET_CLEARSTATS_HANDLE_AND_LINK);
}

/* For the stats module */
int totemknet_link_get_status (
	knet_node_id_t node, uint8_t link_no,
	struct knet_link_status *status)
{
	int res;
	int ret = CS_OK;

	/* We are probably not using knet */
	if (!global_instance) {
		return CS_ERR_NOT_EXIST;
	}

	if (link_no >= INTERFACE_MAX) {
		return CS_ERR_NOT_EXIST; /* Invalid link number */
	}

	res = knet_link_get_status(global_instance->knet_handle, node, link_no, status, sizeof(struct knet_link_status));
	if (res) {
		switch (errno) {
			case EINVAL:
				ret = CS_ERR_INVALID_PARAM;
				break;
			case EBUSY:
				ret = CS_ERR_BUSY;
				break;
			case EDEADLK:
				ret = CS_ERR_TRY_AGAIN;
				break;
			default:
				ret = CS_ERR_LIBRARY;
				break;
		}
	}

	return (ret);
}

int totemknet_handle_get_stats (
	struct knet_handle_stats *stats)
{
	int res;

	/* We are probably not using knet */
	if (!global_instance) {
		return CS_ERR_NOT_EXIST;
	}

	res = knet_handle_get_stats(global_instance->knet_handle, stats, sizeof(struct knet_handle_stats));
	if (res != 0) {
		return (qb_to_cs_error(-errno));
	}

	return CS_OK;
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

static void log_flush_messages (void *knet_context)
{
	struct pollfd pfd;
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int cont;

	cont = 1;

	while (cont) {
		pfd.fd = instance->logpipes[0];
		pfd.events = POLLIN;
		pfd.revents = 0;

		if ((poll(&pfd, 1, 0) > 0) &&
		    (pfd.revents & POLLIN) &&
		    (log_deliver_fn(instance->logpipes[0], POLLIN, instance) == 0)) {
			cont = 1;
		} else {
			cont = 0;
		}
	}
}


#ifdef HAVE_LIBNOZZLE
#define NOZZLE_NAME    "nozzle.name"
#define NOZZLE_IPADDR  "nozzle.ipaddr"
#define NOZZLE_PREFIX  "nozzle.ipprefix"
#define NOZZLE_MACADDR "nozzle.macaddr"

#define NOZZLE_CHANNEL 1


static char *get_nozzle_script_dir(void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	char filename[PATH_MAX + FILENAME_MAX + 1];
	static char updown_dirname[PATH_MAX + FILENAME_MAX + 1];
	int res;
	const char *dirname_res;

	/*
	 * Build script directory based on corosync.conf file location
	 */
	res = snprintf(filename, sizeof(filename), "%s",
	    corosync_get_config_file());
	if (res >= sizeof(filename)) {
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "nozzle up/down path too long");
		return NULL;
	}

	dirname_res = dirname(filename);

	res = snprintf(updown_dirname, sizeof(updown_dirname), "%s/%s",
	    dirname_res, "updown.d");
	if (res >= sizeof(updown_dirname)) {
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "nozzle up/down path too long");
		return NULL;
	}
	return updown_dirname;
}

/*
 * Deliberately doesn't return the status as caller doesn't care.
 * The result will be logged though
 */
static void run_nozzle_script(struct totemknet_instance *instance, int type, const char *typename)
{
	int res;
	char *exec_string;

	res = nozzle_run_updown(instance->nozzle_handle, type, &exec_string);
	if (res == -1 && errno != ENOENT) {
		knet_log_printf (LOGSYS_LEVEL_INFO, "exec nozzle %s script failed: %s", typename, strerror(errno));
	} else if (res == -2) {
		knet_log_printf (LOGSYS_LEVEL_INFO, "nozzle %s script failed", typename);
		knet_log_printf (LOGSYS_LEVEL_INFO, "%s", exec_string);
	}
}

/*
 * Reparse IP address to add in our node ID
 * IPv6 addresses must end in '::'
 * IPv4 addresses must just be valid
 * '/xx' lengths are optional for IPv6, mandatory for IPv4
 *
 * Returns the modified IP address as a string to pass into libnozzle
 */
static int reparse_nozzle_ip_address(struct totemknet_instance *instance,
				     const char *input_addr,
				     const char *prefix, int nodeid,
				     char *output_addr, size_t output_len)
{
	char *coloncolon;
	int bits;
	int max_prefix = 64;
	uint32_t nodeid_mask;
	uint32_t addr_mask;
	uint32_t masked_nodeid;
	struct in_addr *addr;
	struct totem_ip_address totemip;

	coloncolon = strstr(input_addr, "::");
	if (!coloncolon) {
		max_prefix = 30;
	}

	bits = atoi(prefix);
	if (bits < 8 || bits > max_prefix) {
		knet_log_printf(LOGSYS_LEVEL_ERROR, "nozzle IP address prefix must be >= 8 and <= %d (got %d)", max_prefix, bits);
		return -1;
	}

	/* IPv6 is easy */
	if (coloncolon) {
		memcpy(output_addr, input_addr, coloncolon-input_addr);
		sprintf(output_addr + (coloncolon-input_addr), "::%x", nodeid);
		return 0;
	}

	/* For IPv4 we need to parse the address into binary, mask off the required bits,
	 * add in the masked_nodeid and 'print' it out again
	 */
	nodeid_mask = UINT32_MAX & ((1<<(32 - bits)) - 1);
	addr_mask   = UINT32_MAX ^ nodeid_mask;
	masked_nodeid = nodeid & nodeid_mask;

	if (totemip_parse(&totemip, input_addr, AF_INET)) {
		knet_log_printf(LOGSYS_LEVEL_ERROR, "Failed to parse IPv4 nozzle IP address");
		return -1;
	}
	addr = (struct in_addr *)&totemip.addr;
	addr->s_addr &= htonl(addr_mask);
	addr->s_addr |= htonl(masked_nodeid);

	inet_ntop(AF_INET, addr, output_addr, output_len);
	return 0;
}

static int create_nozzle_device(void *knet_context, const char *name,
				const char *ipaddr, const char *prefix,
				const char *macaddr)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	char device_name[IFNAMSIZ+1];
	size_t size = IFNAMSIZ;
	int8_t channel = NOZZLE_CHANNEL;
	nozzle_t nozzle_dev;
	int nozzle_fd;
	int res;
	char *updown_dir;
	char parsed_ipaddr[INET6_ADDRSTRLEN];
	char mac[19];

	memset(device_name, 0, size);
	memset(&mac, 0, sizeof(mac));
	strncpy(device_name, name, size);

	updown_dir = get_nozzle_script_dir(knet_context);
	knet_log_printf (LOGSYS_LEVEL_INFO, "nozzle script dir is %s", updown_dir);

	nozzle_dev = nozzle_open(device_name, size, updown_dir);
	if (!nozzle_dev) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Unable to init nozzle device %s: %s", device_name, strerror(errno));
		return -1;
	}
	instance->nozzle_handle = nozzle_dev;

	if (nozzle_set_mac(nozzle_dev, macaddr) < 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Unable to add set nozzle MAC to %s: %s", mac, strerror(errno));
		goto out_clean;
	}

	if (reparse_nozzle_ip_address(instance, ipaddr, prefix, instance->our_nodeid, parsed_ipaddr, sizeof(parsed_ipaddr))) {
		/* Prints its own errors */
		goto out_clean;
	}
	knet_log_printf (LOGSYS_LEVEL_INFO, "Local nozzle IP address is %s / %d", parsed_ipaddr, atoi(prefix));
	if (nozzle_add_ip(nozzle_dev, parsed_ipaddr, prefix) < 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Unable to add set nozzle IP addr to %s/%s: %s", parsed_ipaddr, prefix, strerror(errno));
		goto out_clean;
	}

	nozzle_fd = nozzle_get_fd(nozzle_dev);
	knet_log_printf (LOGSYS_LEVEL_INFO, "Opened '%s' on fd %d", device_name, nozzle_fd);

	res = knet_handle_add_datafd(instance->knet_handle, &nozzle_fd, &channel);
	if (res != 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Unable to add nozzle FD to knet: %s", strerror(errno));
		goto out_clean;
	}

	run_nozzle_script(instance, NOZZLE_PREUP, "pre-up");

	res = nozzle_set_up(nozzle_dev);
	if (res != 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Unable to set nozzle interface UP: %s", strerror(errno));
		goto out_clean;
	}
	run_nozzle_script(instance, NOZZLE_UP, "up");

	return 0;

out_clean:
	nozzle_close(nozzle_dev);
	return -1;
}

static int remove_nozzle_device(void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	int res;
	int datafd;

	res = knet_handle_get_datafd(instance->knet_handle, NOZZLE_CHANNEL, &datafd);
	if (res != 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Can't find datafd for channel %d: %s", NOZZLE_CHANNEL, strerror(errno));
		return -1;
	}

	res = knet_handle_remove_datafd(instance->knet_handle, datafd);
	if (res != 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Can't remove datafd for nozzle channel %d: %s", NOZZLE_CHANNEL, strerror(errno));
		return -1;
	}

	run_nozzle_script(instance, NOZZLE_DOWN, "pre-down");
	res = nozzle_set_down(instance->nozzle_handle);
	if (res != 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Can't set nozzle device down: %s", strerror(errno));
		return -1;
	}
	run_nozzle_script(instance, NOZZLE_POSTDOWN, "post-down");

	res = nozzle_close(instance->nozzle_handle);
	if (res != 0) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "Can't close nozzle device: %s", strerror(errno));
		return -1;
	}
	knet_log_printf (LOGSYS_LEVEL_INFO, "Removed nozzle device");
	return 0;
}

static void free_nozzle(struct totemknet_instance *instance)
{
	free(instance->nozzle_name);
	free(instance->nozzle_ipaddr);
	free(instance->nozzle_prefix);
	free(instance->nozzle_macaddr);

	instance->nozzle_name =	instance->nozzle_ipaddr = instance->nozzle_prefix =
		instance->nozzle_macaddr = NULL;
}

static int setup_nozzle(void *knet_context)
{
	struct totemknet_instance *instance = (struct totemknet_instance *)knet_context;
	char *ipaddr_str = NULL;
	char *name_str = NULL;
	char *prefix_str = NULL;
	char *macaddr_str = NULL;
	char mac[32];
	int name_res;
	int macaddr_res;
	int res = -1;

	/*
	 * Return value ignored on purpose. icmap_get_string changes
	 * ipaddr_str/prefix_str only on success.
	 */
	(void)icmap_get_string(NOZZLE_IPADDR, &ipaddr_str);
	(void)icmap_get_string(NOZZLE_PREFIX, &prefix_str);
	macaddr_res = icmap_get_string(NOZZLE_MACADDR, &macaddr_str);
	name_res = icmap_get_string(NOZZLE_NAME, &name_str);

	/* Is is being removed? */
	if (name_res == CS_ERR_NOT_EXIST && instance->nozzle_handle) {
		remove_nozzle_device(instance);
		free_nozzle(instance);
		goto out_free;
	}

	if (!name_str) {
		/* no nozzle */
		goto out_free;
	}

	if (!ipaddr_str) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "No IP address supplied for Nozzle device");
		goto out_free;
	}

	if (!prefix_str) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "No prefix supplied for Nozzle IP address");
		goto out_free;
	}

	if (macaddr_str && strlen(macaddr_str) != 17) {
		knet_log_printf (LOGSYS_LEVEL_ERROR, "macaddr for nozzle device is not in the correct format '%s'", macaddr_str);
		goto out_free;
	}
	if (!macaddr_str) {
		macaddr_str = (char*)"54:54:01:00:00:00";
	}

	if (instance->nozzle_name &&
	    (strcmp(name_str, instance->nozzle_name) == 0) &&
	    (strcmp(ipaddr_str, instance->nozzle_ipaddr) == 0) &&
	    (strcmp(prefix_str, instance->nozzle_prefix) == 0) &&
	    (instance->nozzle_macaddr == NULL ||
	     strcmp(macaddr_str, instance->nozzle_macaddr) == 0)) {
		/* Nothing has changed */
		knet_log_printf (LOGSYS_LEVEL_DEBUG, "Nozzle device info not changed");
		goto out_free;
	}

	/* Add nodeid into MAC address */
	memcpy(mac, macaddr_str, 12);
	snprintf(mac+12, sizeof(mac) - 13, "%02x:%02x",
		 instance->our_nodeid >> 8,
		 instance->our_nodeid & 0xFF);
	knet_log_printf (LOGSYS_LEVEL_INFO, "Local nozzle MAC address is %s", mac);

	if (name_res == CS_OK && name_str) {
		/* Reconfigure */
		if (instance->nozzle_name) {
			remove_nozzle_device(instance);
			free_nozzle(instance);
		}

		res = create_nozzle_device(knet_context, name_str, ipaddr_str, prefix_str,
					   mac);

		instance->nozzle_name = strdup(name_str);
		instance->nozzle_ipaddr = strdup(ipaddr_str);
		instance->nozzle_prefix = strdup(prefix_str);
		instance->nozzle_macaddr = strdup(macaddr_str);
		if (!instance->nozzle_name || !instance->nozzle_ipaddr ||
		    !instance->nozzle_prefix) {
			knet_log_printf (LOGSYS_LEVEL_ERROR, "strdup failed in nozzle allocation");
			/*
			 * This 'free' will cause a complete reconfigure of the device next time we reload
			 * but will also let the the current device keep working until then.
			 * remove_nozzle() only needs the, statically-allocated, nozzle_handle
			 */
			free_nozzle(instance);
		}
	}

out_free:
	free(name_str);
	free(ipaddr_str);
	free(prefix_str);
	if (macaddr_res == CS_OK) {
		free(macaddr_str);
	}

	return res;
}
#endif // HAVE_LIBNOZZLE
