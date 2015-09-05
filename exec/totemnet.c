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

#ifdef HAVE_RDMA
#include <totemiba.h>
#endif
#include <totemudp.h>
#include <totemudpu.h>
#include <totemnet.h>
#include <qb/qbloop.h>

#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>

struct transport {
	const char *name;
	
	int (*initialize) (
		qb_loop_t *loop_pt,
		void **transport_instance,
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
			void *context));

	void *(*buffer_alloc) (void);

	void (*buffer_release) (void *ptr);

	int (*processor_count_set) (
		void *transport_context,
		int processor_count);

	int (*token_send) (
		void *transport_context,
		const void *msg,
		unsigned int msg_len);

	int (*mcast_flush_send) (
		void *transport_context,
		const void *msg,
		unsigned int msg_len);


	int (*mcast_noflush_send) (
		void *transport_context,
		const void *msg,
		unsigned int msg_len);

	int (*recv_flush) (void *transport_context);

	int (*send_flush) (void *transport_context);

	int (*iface_check) (void *transport_context);

	int (*finalize) (void *transport_context);

	void (*net_mtu_adjust) (void *transport_context, struct totem_config *totem_config);

	const char *(*iface_print) (void *transport_context);

	int (*iface_get) (
		void *transport_context,
		struct totem_ip_address *addr);

	int (*token_target_set) (
		void *transport_context,
		const struct totem_ip_address *token_target);

	int (*crypto_set) (
		void *transport_context,
		const char *cipher_type,
		const char *hash_type);

	int (*recv_mcast_empty) (
		void *transport_context);

	int (*member_add) (
		void *transport_context,
		const struct totem_ip_address *member);

	int (*member_remove) (
		void *transport_context,
		const struct totem_ip_address *member);

	int (*member_set_active) (
		void *transport_context,
		const struct totem_ip_address *member,
		int active);
};

struct transport transport_entries[] = {
	{
		.name = "UDP/IP Multicast",
		.initialize = totemudp_initialize,
		.buffer_alloc = totemudp_buffer_alloc,
		.buffer_release = totemudp_buffer_release,
		.processor_count_set = totemudp_processor_count_set,
		.token_send = totemudp_token_send,
		.mcast_flush_send = totemudp_mcast_flush_send,
		.mcast_noflush_send = totemudp_mcast_noflush_send,
		.recv_flush = totemudp_recv_flush,
		.send_flush = totemudp_send_flush,
		.iface_check = totemudp_iface_check,
		.finalize = totemudp_finalize,
		.net_mtu_adjust = totemudp_net_mtu_adjust,
		.iface_print = totemudp_iface_print,
		.iface_get = totemudp_iface_get,
		.token_target_set = totemudp_token_target_set,
		.crypto_set = totemudp_crypto_set,
		.recv_mcast_empty = totemudp_recv_mcast_empty
	},
	{
		.name = "UDP/IP Unicast",
		.initialize = totemudpu_initialize,
		.buffer_alloc = totemudpu_buffer_alloc,
		.buffer_release = totemudpu_buffer_release,
		.processor_count_set = totemudpu_processor_count_set,
		.token_send = totemudpu_token_send,
		.mcast_flush_send = totemudpu_mcast_flush_send,
		.mcast_noflush_send = totemudpu_mcast_noflush_send,
		.recv_flush = totemudpu_recv_flush,
		.send_flush = totemudpu_send_flush,
		.iface_check = totemudpu_iface_check,
		.finalize = totemudpu_finalize,
		.net_mtu_adjust = totemudpu_net_mtu_adjust,
		.iface_print = totemudpu_iface_print,
		.iface_get = totemudpu_iface_get,
		.token_target_set = totemudpu_token_target_set,
		.crypto_set = totemudpu_crypto_set,
		.recv_mcast_empty = totemudpu_recv_mcast_empty,
		.member_add = totemudpu_member_add,
		.member_remove = totemudpu_member_remove,
		.member_set_active = totemudpu_member_set_active
	},
#ifdef HAVE_RDMA
	{
		.name = "Infiniband/IP",
		.initialize = totemiba_initialize,
		.buffer_alloc = totemiba_buffer_alloc,
		.buffer_release = totemiba_buffer_release,
		.processor_count_set = totemiba_processor_count_set,
		.token_send = totemiba_token_send,
		.mcast_flush_send = totemiba_mcast_flush_send,
		.mcast_noflush_send = totemiba_mcast_noflush_send,
		.recv_flush = totemiba_recv_flush,
		.send_flush = totemiba_send_flush,
		.iface_check = totemiba_iface_check,
		.finalize = totemiba_finalize,
		.net_mtu_adjust = totemiba_net_mtu_adjust,
		.iface_print = totemiba_iface_print,
		.iface_get = totemiba_iface_get,
		.token_target_set = totemiba_token_target_set,
		.crypto_set = totemiba_crypto_set,
		.recv_mcast_empty = totemiba_recv_mcast_empty

	}
#endif
};
	
struct totemnet_instance {
	void *transport_context;

	struct transport *transport;

        void (*totemnet_log_printf) (
                int level,
		int subsys,
                const char *function,
                const char *file,
                int line,
                const char *format,
                ...)__attribute__((format(printf, 6, 7)));

        int totemnet_subsys_id;
};

#define log_printf(level, format, args...)				\
do {									\
	instance->totemnet_log_printf (					\
		level,							\
		instance->totemnet_subsys_id,				\
		__FUNCTION__, __FILE__, __LINE__,			\
		(const char *)format, ##args);				\
} while (0);

static void totemnet_instance_initialize (
	struct totemnet_instance *instance,
	struct totem_config *config)
{
	int transport;

	instance->totemnet_log_printf = config->totem_logging_configuration.log_printf;
	instance->totemnet_subsys_id = config->totem_logging_configuration.log_subsys_id;


	transport = config->transport_number;

	log_printf (LOGSYS_LEVEL_NOTICE,
		"Initializing transport (%s).", transport_entries[transport].name);

	instance->transport = &transport_entries[transport];
}

int totemnet_crypto_set (
	void *net_context,
	const char *cipher_type,
	const char *hash_type)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->crypto_set (instance->transport_context,
	    cipher_type, hash_type);

	return res;
}

int totemnet_finalize (
	void *net_context)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->finalize (instance->transport_context);

	return (res);
}

int totemnet_initialize (
	qb_loop_t *loop_pt,
	void **net_context,
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
	struct totemnet_instance *instance;
	unsigned int res;

	instance = malloc (sizeof (struct totemnet_instance));
	if (instance == NULL) {
		return (-1);
	}
	totemnet_instance_initialize (instance, totem_config);

	res = instance->transport->initialize (loop_pt,
		&instance->transport_context, totem_config, stats,
		interface_no, context, deliver_fn, iface_change_fn, target_set_completed);

	if (res == -1) {
		goto error_destroy;
	}

	*net_context = instance;
	return (0);

error_destroy:
	free (instance);
	return (-1);
}

void *totemnet_buffer_alloc (void *net_context)
{
	struct totemnet_instance *instance = net_context;
	assert (instance != NULL);
	assert (instance->transport != NULL);
	return instance->transport->buffer_alloc();
}

void totemnet_buffer_release (void *net_context, void *ptr)
{
	struct totemnet_instance *instance = net_context;
	assert (instance != NULL);
	assert (instance->transport != NULL);
	instance->transport->buffer_release (ptr);
}

int totemnet_processor_count_set (
	void *net_context,
	int processor_count)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->processor_count_set (instance->transport_context, processor_count);
	return (res);
}

int totemnet_recv_flush (void *net_context)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->recv_flush (instance->transport_context);

	return (res);
}

int totemnet_send_flush (void *net_context)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->send_flush (instance->transport_context);

	return (res);
}

int totemnet_token_send (
	void *net_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->token_send (instance->transport_context, msg, msg_len);

	return (res);
}
int totemnet_mcast_flush_send (
	void *net_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->mcast_flush_send (instance->transport_context, msg, msg_len);

	return (res);
}

int totemnet_mcast_noflush_send (
	void *net_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->mcast_noflush_send (instance->transport_context, msg, msg_len);

	return (res);
}

extern int totemnet_iface_check (void *net_context)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	res = instance->transport->iface_check (instance->transport_context);

	return (res);
}

extern int totemnet_net_mtu_adjust (void *net_context, struct totem_config *totem_config)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	int res = 0;

	instance->transport->net_mtu_adjust (instance->transport_context, totem_config);
	return (res);
}

const char *totemnet_iface_print (void *net_context)  {
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	const char *ret_char;

	ret_char = instance->transport->iface_print (instance->transport_context);
	return (ret_char);
}

int totemnet_iface_get (
	void *net_context,
	struct totem_ip_address *addr)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	unsigned int res;

	res = instance->transport->iface_get (instance->transport_context, addr);
	
	return (res);
}

int totemnet_token_target_set (
	void *net_context,
	const struct totem_ip_address *token_target)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	unsigned int res;

	res = instance->transport->token_target_set (instance->transport_context, token_target);

	return (res);
}

extern int totemnet_recv_mcast_empty (
	void *net_context)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	unsigned int res;

	res = instance->transport->recv_mcast_empty (instance->transport_context);

	return (res);
}

extern int totemnet_member_add (
	void *net_context,
	const struct totem_ip_address *member)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	unsigned int res = 0;

	if (instance->transport->member_add) {
		res = instance->transport->member_add (
			instance->transport_context,
			member);
	}

	return (res);
}

extern int totemnet_member_remove (
	void *net_context,
	const struct totem_ip_address *member)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	unsigned int res = 0;

	if (instance->transport->member_remove) {
		res = instance->transport->member_remove (
			instance->transport_context,
			member);
	}

	return (res);
}

int totemnet_member_set_active (
	void *net_context,
	const struct totem_ip_address *member,
	int active)
{
	struct totemnet_instance *instance = (struct totemnet_instance *)net_context;
	unsigned int res = 0;

	if (instance->transport->member_set_active) {
		res = instance->transport->member_set_active (
			instance->transport_context,
			member,
			active);
	}

	return (res);
}
