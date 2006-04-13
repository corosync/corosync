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

#include "../include/queue.h"
#include "../include/sq.h"
#include "../include/list.h"
#include "../include/hdb.h"
#include "swab.h"
#include "aispoll.h"
#include "totemnet.h"
#include "totemrrp.h"

struct totemrrp_instance;
struct passive_instance {
	unsigned int *faulty;
	unsigned int *last_seq;
	unsigned int *counter_problems;
	unsigned char token[15000];
	unsigned int token_len;
};

struct active_instance {
	struct totemrrp_instance *rrp_instance;
	unsigned int *faulty;
	unsigned int *last_token_recv;
	unsigned int *counter_problems;
	unsigned char token[15000];
	unsigned int token_len;
        poll_timer_handle timer_active_token;
};

struct rrp_algo {
	void (*mcast_recv) (
		struct totemrrp_instance *instance,
		void *context,
		struct totem_ip_address *system_from,
		void *msg,
		unsigned int msg_len);

	void (*mcast_noflush_send) (
		struct totemrrp_instance *instance,
		struct iovec *iovec,
		unsigned int iov_len);

	void (*mcast_flush_send) (
		struct totemrrp_instance *instance,
		struct iovec *iovec,
		unsigned int iov_len);

	void (*token_recv) (
		struct totemrrp_instance *instance,
		unsigned int interface_no,
		void *context,
		struct totem_ip_address *system_from,
		void *msg,
		unsigned int msg_len,
		unsigned int token_seqid);

	void (*token_send) (
		struct totemrrp_instance *instance,
		struct totem_ip_address *system_to,
		struct iovec *iovec,
		unsigned int iov_len);	

	void (*recv_flush) (
		struct totemrrp_instance *instance);

	void (*send_flush) (
		struct totemrrp_instance *instance);

	void (*iface_check) (
		struct totemrrp_instance *instance);

	void (*processor_count_set) (
		struct totemrrp_instance *instance,
		unsigned int processor_count);
};

struct totemrrp_instance {
	poll_handle totemrrp_poll_handle;

	struct totem_interface *totemrrp_interfaces;

	struct rrp_algo *rrp_algo;

	void *context;
	
	void (*totemrrp_deliver_fn) (
		void *context,
		struct totem_ip_address *system_from,
		void *msg,
		int msg_len);

	void (*totemrrp_iface_change_fn) (
		void *context,
		struct totem_ip_address *iface_addr);

	void (*totemrrp_token_seqid_get) (
		void *msg,
		unsigned int *seqid,
		unsigned int *token_is);

	/*
	 * Function and data used to log messages
	 */
	int totemrrp_log_level_security;

	int totemrrp_log_level_error;

	int totemrrp_log_level_warning;

	int totemrrp_log_level_notice;

	int totemrrp_log_level_debug;

	void (*totemrrp_log_printf) (int level, char *format, ...);

	totemrrp_handle handle;

	totemnet_handle *net_handles;

	void *rrp_algo_instance;

	int interface_count;

	int poll_handle;

	int processor_count;
};

static void passive_mcast_recv (
	struct totemrrp_instance *instance,
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	unsigned int msg_len);

static void passive_mcast_noflush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);

static void passive_mcast_flush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);

static void passive_token_recv (
	struct totemrrp_instance *instance,
	unsigned int interface_no,
	struct totem_ip_address *system_from,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void passive_token_send (
	struct totemrrp_instance *instance,
	struct totem_ip_address *system_to,
	struct iovec *iovec,
	unsigned int iov_len);	

static void active_mcast_recv (
	struct totemrrp_instance *instance,
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	unsigned int msg_len);

static void active_mcast_noflush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);

static void active_mcast_flush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);

static void active_token_recv (
	struct totemrrp_instance *instance,
	unsigned int interface_no,
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void active_token_send (
	struct totemrrp_instance *instance,
	struct totem_ip_address *system_to,
	struct iovec *iovec,
	unsigned int iov_len);	

static void active_recv_flush (
	struct totemrrp_instance *instance);

static void active_send_flush (
	struct totemrrp_instance *instance);

static void active_iface_check (
	struct totemrrp_instance *instance);

static void active_processor_count_set (
	struct totemrrp_instance *instance,
	unsigned int processor_count_set);
/*
struct rrp_algo passive_algo = {
	.mcast_recv		= passive_mcast_recv,
	.mcast_noflush_send	= passive_mcast_noflush_send,
	.mcast_flush_send	= passive_mcast_flush_send,
	.token_recv		= passive_token_recv,
	.token_send		= passive_token_send
};
*/

struct rrp_algo active_algo = {
	.mcast_recv		= active_mcast_recv,
	.mcast_noflush_send	= active_mcast_noflush_send,
	.mcast_flush_send	= active_mcast_flush_send,
	.token_recv		= active_token_recv,
	.token_send		= active_token_send,
	.recv_flush		= active_recv_flush,
	.send_flush		= active_recv_flush,
	.iface_check		= active_iface_check,
	.processor_count_set	= active_processor_count_set,
};

/*
 * All instances in one database
 */
static struct hdb_handle_database totemrrp_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0
};

struct passive_instance *passive_instance_initialize (
	int interface_count)
{
	struct passive_instance *instance;
	int i;

	instance = malloc (sizeof (struct passive_instance));
	if (instance == 0) {
		goto error_exit;
	}

	instance->faulty = malloc (sizeof (int) * interface_count);
	if (instance->faulty == 0) {
		free (instance);
		instance = 0;
		goto error_exit;
	}

	instance->last_seq = malloc (sizeof (int) * interface_count);
	if (instance->last_seq == 0) {
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}

	instance->counter_problems = malloc (sizeof (int) * interface_count);
	if (instance->counter_problems == 0) {
		free (instance->last_seq);
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}

	for (i = 0; i < interface_count; i++) {
		instance->faulty[i] = 0;
		instance->last_seq[i] = 0;
		instance->counter_problems[i] = 0;
	}
error_exit:
	return (instance);
}


struct active_instance *active_instance_initialize (
	struct totemrrp_instance *rrp_instance,
	int interface_count)
{
	struct active_instance *instance;
	int i;

	instance = malloc (sizeof (struct active_instance));
	if (instance == 0) {
		goto error_exit;
	}

	instance->faulty = malloc (sizeof (int) * interface_count);
	if (instance->faulty == 0) {
		free (instance);
		instance = 0;
		goto error_exit;
	}

	instance->last_token_recv = malloc (sizeof (int) * interface_count);
	if (instance->last_token_recv == 0) {
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}

	instance->counter_problems = malloc (sizeof (int) * interface_count);
	if (instance->counter_problems == 0) {
		free (instance->last_token_recv);
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}

	instance->faulty = malloc (sizeof (int) * interface_count);
	instance->last_token_recv = malloc (sizeof (int) * interface_count);
	instance->counter_problems = malloc (sizeof (int) * interface_count);
	for (i = 0; i < interface_count; i++) {
		instance->faulty[i] = 0;
		instance->last_token_recv[i] = 0;
		instance->counter_problems[i] = 0;
	}

	instance->timer_active_token = 0;

	instance->rrp_instance = rrp_instance;

error_exit:
	return (instance);
}

static void timer_function_active_token (void *context)
{
//	struct active_instance *instance = (struct active_instance *)context;
}


static void active_token_timer_start (struct active_instance *active_instance)
{
        poll_timer_add (
		active_instance->rrp_instance->poll_handle,
		10, /* 10 msec */
		(void *)active_instance,
		timer_function_active_token,
		&active_instance->timer_active_token);
}

static void active_token_timer_cancel (struct active_instance *active_instance)
{
        poll_timer_delete (
		active_instance->rrp_instance->poll_handle,
		active_instance->timer_active_token);
}

static void active_mcast_recv (
	struct totemrrp_instance *instance,
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	unsigned int msg_len)
{
	instance->totemrrp_deliver_fn (
		context,
		system_from,
		msg,
		msg_len);
}

static void active_mcast_flush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	int i;
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {
			totemnet_mcast_flush_send (instance->net_handles[i], iovec, iov_len);
		}
	}
}

static void active_mcast_noflush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	int i;
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {
			totemnet_mcast_noflush_send (instance->net_handles[i], iovec, iov_len);
		}
	}
}

static void active_token_recv (
	struct totemrrp_instance *instance,
	unsigned int interface_no,
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seqid)
{
	unsigned int cur_token_seq;
	unsigned int last_token_seq;
	unsigned int token_is;
	int i;
	struct active_instance *active_instance = (struct active_instance *)instance->rrp_algo_instance;

	instance->totemrrp_token_seqid_get (
		msg,
		&cur_token_seq,
		&token_is);
	assert (token_is);

	if (token_seqid > cur_token_seq) {
		memcpy (active_instance->token, msg, msg_len);
		active_instance->token_len = msg_len;
		for (i = 0; i < instance->interface_count; i++) {
			active_instance->last_token_recv[i] = 0;
		}
		active_instance->last_token_recv[interface_no] = 1;
		active_token_timer_start (active_instance);
	
	}

	instance->totemrrp_token_seqid_get (
		msg,
		&last_token_seq,
		&token_is);
	assert (token_is);

	if (cur_token_seq == last_token_seq) {
		active_instance->last_token_recv[interface_no] = 1;
		for (i = 0; i < instance->interface_count; i++) {
			if ((active_instance->last_token_recv[i] == 0) &&
				active_instance->faulty[i] == 0) {
				return; /* don't deliver token */
			}
		}
		active_token_timer_cancel (active_instance);

		instance->totemrrp_deliver_fn (
			context,
			system_from,
			msg,
			msg_len);
	}
}

static void active_token_send (
	struct totemrrp_instance *instance,
	struct totem_ip_address *system_to,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {
			totemnet_token_send (
				instance->net_handles[i], system_to,
				iovec, iov_len);
		}
	}
}

static void active_recv_flush (struct totemrrp_instance *instance)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_recv_flush (instance->net_handles[i]);
		}
	}
}

static void active_send_flush (struct totemrrp_instance *instance)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_send_flush (instance->net_handles[i]);
		}
	}
}

static void active_iface_check (struct totemrrp_instance *instance)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_iface_check (instance->net_handles[i]);
		}
	}
}

static void active_processor_count_set (
	struct totemrrp_instance *instance,
	unsigned int processor_count)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_processor_count_set (instance->net_handles[i],
				processor_count);
		}
	}
}

struct deliver_fn_context {
	struct totemrrp_instance *instance;
	void *context;
	int interface_no;
};

static void totemrrp_instance_initialize (struct totemrrp_instance *instance)
{
	memset (instance, 0, sizeof (struct totemrrp_instance));
	instance->rrp_algo = &active_algo;
}

void rrp_deliver_fn (
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len)
{
	unsigned int token_seqid;
	unsigned int token_is;

	struct deliver_fn_context *deliver_fn_context = (struct deliver_fn_context *)context;

	deliver_fn_context->instance->totemrrp_token_seqid_get (
		msg,
		&token_seqid,
		&token_is);

	if (token_is) {
		/*
		 * Deliver to the token receiver for this rrp algorithm 
		 */
		deliver_fn_context->instance->rrp_algo->token_recv (
			deliver_fn_context->instance,
			deliver_fn_context->interface_no,
			deliver_fn_context->context,
			system_from,
			msg,
			msg_len,
			token_seqid);
	} else {
		/*
		 * Deliver to the mcast receiver for this rrp algorithm 
		 */
		deliver_fn_context->instance->rrp_algo->mcast_recv (
			deliver_fn_context->instance,
			deliver_fn_context->context,
			system_from,
			msg,
			msg_len);
	}
}

void rrp_iface_change_fn (
	void *context,
	struct totem_ip_address *iface_addr)
{
	struct deliver_fn_context *deliver_fn_context = (struct deliver_fn_context *)context;

	deliver_fn_context->instance->totemrrp_iface_change_fn (
		deliver_fn_context->context,
		iface_addr);
}

int totemrrp_finalize (
	totemrrp_handle handle)
{
	struct totemrrp_instance *instance;
	int res = 0;
	int i;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	for (i = 0; i < instance->interface_count; i++) {
		totemnet_finalize (instance->net_handles[i]);
	}

	hdb_handle_put (&totemrrp_instance_database, handle);

error_exit:
	return (res);
}

/*
 * Totem Redundant Ring interface
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
int totemrrp_initialize (
	poll_handle poll_handle,
	totemrrp_handle *handle,
	struct totem_config *totem_config,
	void *context,

	void (*deliver_fn) (
		void *context,
		struct totem_ip_address *system_from,
		void *msg,
		int msg_len),

	void (*iface_change_fn) (
		void *context,
		struct totem_ip_address *iface_addr),

	void (*token_seqid_get) (
		void *msg,
		unsigned int *seqid,
		unsigned int *token_is))
	
{
	struct totemrrp_instance *instance;
	unsigned int res;
	int i;

	res = hdb_handle_create (&totemrrp_instance_database,
	sizeof (struct totemrrp_instance), handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&totemrrp_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}

	totemrrp_instance_initialize (instance);

	/*
	* Configure logging
	*/
	instance->totemrrp_log_level_security = totem_config->totem_logging_configuration.log_level_security;
	instance->totemrrp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemrrp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemrrp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemrrp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemrrp_log_printf = totem_config->totem_logging_configuration.log_printf;

	instance->totemrrp_interfaces = totem_config->interfaces;

	instance->totemrrp_poll_handle = poll_handle;

	instance->totemrrp_deliver_fn = deliver_fn;

	instance->totemrrp_iface_change_fn = iface_change_fn;

	instance->totemrrp_token_seqid_get = token_seqid_get;

	instance->interface_count = totem_config->interface_count;

	instance->net_handles = malloc (sizeof (totemnet_handle) * totem_config->interface_count);

	instance->context = context;

	instance->poll_handle = poll_handle;

	instance->rrp_algo_instance = malloc (sizeof (struct active_instance));
	assert (instance->rrp_algo_instance);

	instance->rrp_algo_instance = active_instance_initialize (
		instance,
		totem_config->interface_count);

	for (i = 0; i < totem_config->interface_count; i++) {
		struct deliver_fn_context *deliver_fn_context;

		deliver_fn_context = malloc (sizeof (struct deliver_fn_context));
		assert (deliver_fn_context);
		deliver_fn_context->instance = instance;
		deliver_fn_context->context = context;
		deliver_fn_context->interface_no = i;

		totemnet_initialize (
			poll_handle,
			&instance->net_handles[i],
			totem_config,
			i,
			(void *)deliver_fn_context,
			rrp_deliver_fn,
			rrp_iface_change_fn);
	}

	totemnet_net_mtu_adjust (totem_config);

error_exit:
	hdb_handle_put (&totemrrp_instance_database, *handle);
	return (0);

error_destroy:
	hdb_handle_destroy (&totemrrp_instance_database, *handle);
	return (-1);
}

int totemrrp_processor_count_set (
	totemrrp_handle handle,
	unsigned int processor_count)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	instance->rrp_algo->processor_count_set (instance, processor_count);

	instance->processor_count = processor_count;

	hdb_handle_put (&totemrrp_instance_database, handle);

error_exit:
	return (res);
}

int totemrrp_recv_flush (totemrrp_handle handle)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	instance->rrp_algo->recv_flush (instance);

	hdb_handle_put (&totemrrp_instance_database, handle);

error_exit:
	return (res);
}

int totemrrp_send_flush (totemrrp_handle handle)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	instance->rrp_algo->send_flush (instance);

	hdb_handle_put (&totemrrp_instance_database, handle);

error_exit:
	return (res);
}

int totemrrp_token_send (
	totemrrp_handle handle,
	struct totem_ip_address *system_to,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	instance->rrp_algo->token_send (instance, system_to, iovec, iov_len);

	hdb_handle_put (&totemrrp_instance_database, handle);

error_exit:
	return (res);
}

int totemrrp_mcast_flush_send (
	totemrrp_handle handle,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
// TODO this needs to return the result
	instance->rrp_algo->mcast_flush_send (instance, iovec, iov_len);

	hdb_handle_put (&totemrrp_instance_database, handle);
error_exit:
	return (res);
}

int totemrrp_mcast_noflush_send (
	totemrrp_handle handle,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	/*
	 * merge detects go out through mcast_flush_send so it is safe to
	 * flush these messages if we are only one processor.  This avoids
	 * an encryption/hmac and decryption/hmac
	 */
	if (instance->processor_count > 1) {

// TODO this needs to return the result
		instance->rrp_algo->mcast_noflush_send (instance, iovec, iov_len);
	}

	hdb_handle_put (&totemrrp_instance_database, handle);
error_exit:
	return (res);
}

int totemrrp_iface_check (totemrrp_handle handle)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	instance->rrp_algo->iface_check (instance);

	hdb_handle_put (&totemrrp_instance_database, handle);
error_exit:
	return (res);
}
