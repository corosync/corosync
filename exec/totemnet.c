/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
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

#include <totemiba.h>
#include <totemudp.h>
#include <totemnet.h>


struct transport {
	const char *name;
	
	int (*initialize) (
		hdb_handle_t poll_handle,
		hdb_handle_t *handle,
		struct totem_config *totem_config,
		int interface_no,
		void *context,

		void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

		void (*iface_change_fn) (
			void *context,
			const struct totem_ip_address *iface_address));

	int (*processor_count_set) (
		hdb_handle_t handle,
		int processor_count);

	int (*token_send) (
		hdb_handle_t handle,
		const void *msg,
		unsigned int msg_len);

	int (*mcast_flush_send) (
		hdb_handle_t handle,
		const void *msg,
		unsigned int msg_len);


	int (*mcast_noflush_send) (
		hdb_handle_t handle,
		const void *msg,
		unsigned int msg_len);

	int (*recv_flush) (hdb_handle_t handle);

	int (*send_flush) (hdb_handle_t handle);

	int (*iface_check) (hdb_handle_t handle);

	int (*finalize) (hdb_handle_t handle);

	void (*net_mtu_adjust) (hdb_handle_t handle, struct totem_config *totem_config);

	const char *(*iface_print) (hdb_handle_t handle);

	int (*iface_get) (
		hdb_handle_t handle,
		struct totem_ip_address *addr);

	int (*token_target_set) (
		hdb_handle_t handle,
		const struct totem_ip_address *token_target);

	int (*crypto_set) (
		hdb_handle_t handle,
		unsigned int type);

	int (*recv_mcast_empty) (
		hdb_handle_t handle);
};

struct transport transport_entries[] = {
	{
		.name = "UDP/IP",
		.initialize = totemudp_initialize,
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
		.name = "Infiniband/IP",
		.initialize = totemiba_initialize,
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
};
	
struct totemnet_instance {
	hdb_handle_t transport_handle;

	struct transport *transport;
};

DECLARE_HDB_DATABASE (totemnet_instance_database,NULL);

static void totemnet_instance_initialize (struct totemnet_instance *instance)
{
	instance->transport = &transport_entries[0];
}

int totemnet_crypto_set (hdb_handle_t handle,
			 unsigned int type)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->crypto_set (instance->transport_handle, type);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return res;
}

int totemnet_finalize (
	hdb_handle_t handle)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->finalize (instance->transport_handle);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_initialize (
	hdb_handle_t poll_handle,
	hdb_handle_t *handle,
	struct totem_config *totem_config,
	int interface_no,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address))
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

	res = instance->transport->initialize (poll_handle, &instance->transport_handle, totem_config,
		interface_no, context, deliver_fn, iface_change_fn);

error_exit:
	hdb_handle_put (&totemnet_instance_database, *handle);
	return (0);

error_destroy:
	hdb_handle_destroy (&totemnet_instance_database, *handle);
	return (-1);
}

int totemnet_processor_count_set (
	hdb_handle_t handle,
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

	res = instance->transport->processor_count_set (handle, processor_count);

	hdb_handle_put (&totemnet_instance_database, instance->transport_handle);

error_exit:
	return (res);
}

int totemnet_recv_flush (hdb_handle_t handle)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->recv_flush (instance->transport_handle);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_send_flush (hdb_handle_t handle)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->send_flush (instance->transport_handle);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_token_send (
	hdb_handle_t handle,
	const void *msg,
	unsigned int msg_len)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->token_send (instance->transport_handle, msg, msg_len);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}
int totemnet_mcast_flush_send (
	hdb_handle_t handle,
	const void *msg,
	unsigned int msg_len)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->mcast_flush_send (instance->transport_handle, msg, msg_len);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_mcast_noflush_send (
	hdb_handle_t handle,
	const void *msg,
	unsigned int msg_len)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->mcast_noflush_send (instance->transport_handle, msg, msg_len);

	hdb_handle_put (&totemnet_instance_database, handle);
error_exit:
	return (res);
}

extern int totemnet_iface_check (hdb_handle_t handle)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	res = instance->transport->iface_check (instance->transport_handle);

	hdb_handle_put (&totemnet_instance_database, handle);
error_exit:
	return (res);
}

extern int totemnet_net_mtu_adjust (hdb_handle_t handle, struct totem_config *totem_config)
{
	struct totemnet_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	instance->transport->net_mtu_adjust (instance->transport_handle, totem_config);

	hdb_handle_put (&totemnet_instance_database, handle);
error_exit:
	return (res);
}

const char *totemnet_iface_print (hdb_handle_t handle)  {
	struct totemnet_instance *instance;
	int res = 0;
	const char *ret_char;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		ret_char = "Invalid totemnet handle";
		goto error_exit;
	}

	ret_char = instance->transport->iface_print (instance->transport_handle);

	hdb_handle_put (&totemnet_instance_database, handle);
error_exit:
	return (ret_char);
}

int totemnet_iface_get (
	hdb_handle_t handle,
	struct totem_ip_address *addr)
{
	struct totemnet_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	res = instance->transport->iface_get (instance->transport_handle, addr);
	
	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

int totemnet_token_target_set (
	hdb_handle_t handle,
	const struct totem_ip_address *token_target)
{
	struct totemnet_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	res = instance->transport->token_target_set (instance->transport_handle, token_target);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}

extern int totemnet_recv_mcast_empty (
	hdb_handle_t handle)
{
	struct totemnet_instance *instance;
	unsigned int res = 0;

	res = hdb_handle_get (&totemnet_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	res = instance->transport->recv_mcast_empty (instance->transport_handle);

	hdb_handle_put (&totemnet_instance_database, handle);

error_exit:
	return (res);
}
