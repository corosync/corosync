/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
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
	struct totemrrp_instance *rrp_instance;
	unsigned int *faulty;
	unsigned int *token_recv_count;
	unsigned int *mcast_recv_count;
	unsigned char token[15000];
	unsigned int token_len;
        poll_timer_handle timer_expired_token;
        poll_timer_handle timer_problem_decrementer;
	void *totemrrp_context;
	unsigned int token_xmit_iface;
	unsigned int msg_xmit_iface;
};

struct active_instance {
	struct totemrrp_instance *rrp_instance;
	unsigned int *faulty;
	unsigned int *last_token_recv;
	unsigned int *counter_problems;
	unsigned char token[15000];
	unsigned int token_len;
	unsigned int last_token_seq;
        poll_timer_handle timer_expired_token;
        poll_timer_handle timer_problem_decrementer;
	void *totemrrp_context;
};

struct rrp_algo {
	char *name;

	void * (*initialize) (
		struct totemrrp_instance *rrp_instance,
		int interface_count);

	void (*mcast_recv) (
		struct totemrrp_instance *instance,
		unsigned int iface_no,
		void *context,
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
		unsigned int iface_no,
		void *context,
		void *msg,
		unsigned int msg_len,
		unsigned int token_seqid);

	void (*token_send) (
		struct totemrrp_instance *instance,
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

	void (*token_target_set) (
		struct totemrrp_instance *instance,
		struct totem_ip_address *token_target,
		unsigned int iface_no);
};

struct totemrrp_instance {
	poll_handle totemrrp_poll_handle;

	struct totem_interface *interfaces;

	struct rrp_algo *rrp_algo;

	void *context;
	
	void (*totemrrp_deliver_fn) (
		void *context,
		void *msg,
		int msg_len);

	void (*totemrrp_iface_change_fn) (
		void *context,
		struct totem_ip_address *iface_addr,
		unsigned int iface_no);

	void (*totemrrp_token_seqid_get) (
		void *msg,
		unsigned int *seqid,
		unsigned int *token_is);


	unsigned int (*totemrrp_msgs_missing) (void);

	/*
	 * Function and data used to log messages
	 */
	int totemrrp_log_level_security;

	int totemrrp_log_level_error;

	int totemrrp_log_level_warning;

	int totemrrp_log_level_notice;

	int totemrrp_log_level_debug;

	void (*totemrrp_log_printf) (char *file, int line, int level, char *format, ...);

	totemrrp_handle handle;

	totemnet_handle *net_handles;

	void *rrp_algo_instance;

	int interface_count;

	int poll_handle;

	int processor_count;

	struct totem_config *totem_config;
};

/*
 * None Replication Forward Declerations
 */
static void none_mcast_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len);

static void none_mcast_noflush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);

static void none_mcast_flush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);

static void none_token_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void none_token_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);	

static void none_recv_flush (
	struct totemrrp_instance *instance);

static void none_send_flush (
	struct totemrrp_instance *instance);

static void none_iface_check (
	struct totemrrp_instance *instance);

static void none_processor_count_set (
	struct totemrrp_instance *instance,
	unsigned int processor_count_set);

static void none_token_target_set (
	struct totemrrp_instance *instance,
	struct totem_ip_address *token_target,
	unsigned int iface_no);

/*
 * Passive Replication Forward Declerations
 */
static void *passive_instance_initialize (
	struct totemrrp_instance *rrp_instance,
	int interface_count);

static void passive_mcast_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
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
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void passive_token_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len);	

static void passive_recv_flush (
	struct totemrrp_instance *instance);

static void passive_send_flush (
	struct totemrrp_instance *instance);

static void passive_iface_check (
	struct totemrrp_instance *instance);

static void passive_processor_count_set (
	struct totemrrp_instance *instance,
	unsigned int processor_count_set);

static void passive_token_target_set (
	struct totemrrp_instance *instance,
	struct totem_ip_address *token_target,
	unsigned int iface_no);

/*
 * Active Replication Forward Definitions
 */
static void *active_instance_initialize (
	struct totemrrp_instance *rrp_instance,
	int interface_count);

static void active_mcast_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
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
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void active_token_send (
	struct totemrrp_instance *instance,
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

static void active_token_target_set (
	struct totemrrp_instance *instance,
	struct totem_ip_address *token_target,
	unsigned int iface_no);

static void active_timer_expired_token_start (
	struct active_instance *active_instance);

static void active_timer_expired_token_cancel (
	struct active_instance *active_instance);

static void active_timer_problem_decrementer_start (
	struct active_instance *active_instance);

static void active_timer_problem_decrementer_cancel (
	struct active_instance *active_instance);

struct rrp_algo none_algo = {
	.name			= "none",
	.initialize		= NULL,
	.mcast_recv		= none_mcast_recv,
	.mcast_noflush_send	= none_mcast_noflush_send,
	.mcast_flush_send	= none_mcast_flush_send,
	.token_recv		= none_token_recv,
	.token_send		= none_token_send,
	.recv_flush		= none_recv_flush,
	.send_flush		= none_send_flush,
	.iface_check		= none_iface_check,
	.processor_count_set	= none_processor_count_set,
	.token_target_set	= none_token_target_set
};

struct rrp_algo passive_algo = {
	.name			= "passive",
	.initialize		= passive_instance_initialize,
	.mcast_recv		= passive_mcast_recv,
	.mcast_noflush_send	= passive_mcast_noflush_send,
	.mcast_flush_send	= passive_mcast_flush_send,
	.token_recv		= passive_token_recv,
	.token_send		= passive_token_send,
	.recv_flush		= passive_recv_flush,
	.send_flush		= passive_send_flush,
	.iface_check		= passive_iface_check,
	.processor_count_set	= passive_processor_count_set,
	.token_target_set	= passive_token_target_set
};

struct rrp_algo active_algo = {
	.name			= "active",
	.initialize		= active_instance_initialize,
	.mcast_recv		= active_mcast_recv,
	.mcast_noflush_send	= active_mcast_noflush_send,
	.mcast_flush_send	= active_mcast_flush_send,
	.token_recv		= active_token_recv,
	.token_send		= active_token_send,
	.recv_flush		= active_recv_flush,
	.send_flush		= active_send_flush,
	.iface_check		= active_iface_check,
	.processor_count_set	= active_processor_count_set,
	.token_target_set	= active_token_target_set
};

struct rrp_algo *rrp_algos[] = {
	&none_algo,
	&passive_algo,
	&active_algo
};

#define RRP_ALGOS_COUNT 3

/*
 * All instances in one database
 */
static struct hdb_handle_database totemrrp_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0,
	.mutex		= PTHREAD_MUTEX_INITIALIZER
};

#define log_printf(level, format, args...) \
    rrp_instance->totemrrp_log_printf (__FILE__, __LINE__, level, format, ##args)

/*
 * None Replication Implementation
 */

static void none_mcast_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len)
{
	rrp_instance->totemrrp_deliver_fn (
		context,
		msg,
		msg_len);
}

static void none_mcast_flush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	totemnet_mcast_flush_send (instance->net_handles[0], iovec, iov_len);
}

static void none_mcast_noflush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	totemnet_mcast_noflush_send (instance->net_handles[0], iovec, iov_len);
}

static void none_token_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seq)
{
	rrp_instance->totemrrp_deliver_fn (
		context,
		msg,
		msg_len);
}

static void none_token_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	totemnet_token_send (
		instance->net_handles[0],
		iovec, iov_len);
}

static void none_recv_flush (struct totemrrp_instance *instance)
{
	totemnet_recv_flush (instance->net_handles[0]);
}

static void none_send_flush (struct totemrrp_instance *instance)
{
	totemnet_send_flush (instance->net_handles[0]);
}

static void none_iface_check (struct totemrrp_instance *instance)
{
	totemnet_iface_check (instance->net_handles[0]);
}

static void none_processor_count_set (
	struct totemrrp_instance *instance,
	unsigned int processor_count)
{
	totemnet_processor_count_set (instance->net_handles[0],
		processor_count);
}

static void none_token_target_set (
	struct totemrrp_instance *instance,
	struct totem_ip_address *token_target,
	unsigned int iface_no)
{
	totemnet_token_target_set (instance->net_handles[0], token_target);
}

/*
 * Passive Replication Implementation
 */
void *passive_instance_initialize (
	struct totemrrp_instance *rrp_instance,
	int interface_count)
{
	struct passive_instance *instance;

	instance = malloc (sizeof (struct passive_instance));
	if (instance == 0) {
		goto error_exit;
	}
	memset (instance, 0, sizeof (struct passive_instance));

	instance->faulty = malloc (sizeof (int) * interface_count);
	if (instance->faulty == 0) {
		free (instance);
		instance = 0;
		goto error_exit;
	}
	memset (instance->faulty, 0, sizeof (int) * interface_count);

	instance->token_recv_count = malloc (sizeof (int) * interface_count);
	if (instance->token_recv_count == 0) {
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}
	memset (instance->token_recv_count, 0, sizeof (int) * interface_count);

	instance->mcast_recv_count = malloc (sizeof (int) * interface_count);
	if (instance->mcast_recv_count == 0) {
		free (instance->token_recv_count);
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}
	memset (instance->mcast_recv_count, 0, sizeof (int) * interface_count);

error_exit:
	return ((void *)instance);
}

static void timer_function_passive_token_expired (void *context)
{
	struct passive_instance *passive_instance = (struct passive_instance *)context;
	struct totemrrp_instance *rrp_instance = passive_instance->rrp_instance;

	rrp_instance->totemrrp_deliver_fn (
		passive_instance->totemrrp_context,
		passive_instance->token,
		passive_instance->token_len);
}

/* TODO
static void timer_function_passive_problem_decrementer (void *context)
{
//	struct passive_instance *passive_instance = (struct passive_instance *)context;
//	struct totemrrp_instance *rrp_instance = passive_instance->rrp_instance;

}
*/


static void passive_timer_expired_token_start (
	struct passive_instance *passive_instance)
{
        poll_timer_add (
		passive_instance->rrp_instance->poll_handle,
		passive_instance->rrp_instance->totem_config->rrp_token_expired_timeout,
		(void *)passive_instance,
		timer_function_passive_token_expired,
		&passive_instance->timer_expired_token);
}

static void passive_timer_expired_token_cancel (
	struct passive_instance *passive_instance)
{
        poll_timer_delete (
		passive_instance->rrp_instance->poll_handle,
		passive_instance->timer_expired_token);
}

/*
static void passive_timer_problem_decrementer_start (
	struct passive_instance *passive_instance)
{
        poll_timer_add (
		passive_instance->rrp_instance->poll_handle,
		passive_instance->rrp_instance->totem_config->rrp_problem_count_timeout,
		(void *)passive_instance,
		timer_function_passive_problem_decrementer,
		&passive_instance->timer_problem_decrementer);
}

static void passive_timer_problem_decrementer_cancel (
	struct passive_instance *passive_instance)
{
        poll_timer_delete (
		passive_instance->rrp_instance->poll_handle,
		passive_instance->timer_problem_decrementer);
}
*/


static void passive_mcast_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)rrp_instance->rrp_algo_instance;
	unsigned int max;
	unsigned int i;

	rrp_instance->totemrrp_deliver_fn (
		context,
		msg,
		msg_len);

	if (rrp_instance->totemrrp_msgs_missing() == 0 &&
		passive_instance->timer_expired_token) {
		/*
		 * Delivers the last token
		 */
		rrp_instance->totemrrp_deliver_fn (
			passive_instance->totemrrp_context,
			passive_instance->token,
			passive_instance->token_len);
		passive_timer_expired_token_cancel (passive_instance);
	}

	/*
	 * Monitor for failures
	 * TODO doesn't handle wrap-around of the mcast recv count
	 */
	passive_instance->mcast_recv_count[iface_no] += 1;
	max = 0;
	for (i = 0; i < rrp_instance->interface_count; i++) {
		if (max < passive_instance->mcast_recv_count[i]) {
			max = passive_instance->mcast_recv_count[i];
		}
	}

	for (i = 0; i < rrp_instance->interface_count; i++) {
		if ((passive_instance->faulty[i] == 0) &&
			(max - passive_instance->mcast_recv_count[i] > 
			rrp_instance->totem_config->rrp_problem_count_threshold)) {
			passive_instance->faulty[i] = 1;
			log_printf (
				rrp_instance->totemrrp_log_level_error,
				"Marking ringid %d interface %s FAULTY - adminisrtative intervention required.",
				i,
				totemnet_iface_print (rrp_instance->net_handles[i]));
		}
	}
}

static void passive_mcast_flush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)instance->rrp_algo_instance;

	do {
		passive_instance->msg_xmit_iface = (passive_instance->msg_xmit_iface + 1) % instance->interface_count;
	} while (passive_instance->faulty[passive_instance->msg_xmit_iface] == 1);
	
	totemnet_mcast_flush_send (instance->net_handles[passive_instance->msg_xmit_iface], iovec, iov_len);
}

static void passive_mcast_noflush_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)instance->rrp_algo_instance;

	do {
		passive_instance->msg_xmit_iface = (passive_instance->msg_xmit_iface + 1) % instance->interface_count;
	} while (passive_instance->faulty[passive_instance->msg_xmit_iface] == 1);
	
	
	totemnet_mcast_noflush_send (instance->net_handles[passive_instance->msg_xmit_iface], iovec, iov_len);
}

static void passive_token_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seq)
{
	struct passive_instance *passive_instance = (struct passive_instance *)rrp_instance->rrp_algo_instance;
	unsigned int max;
	unsigned int i;

	passive_instance->totemrrp_context = context; // this should be in totemrrp_instance ? TODO

	if (rrp_instance->totemrrp_msgs_missing() == 0) {
		rrp_instance->totemrrp_deliver_fn (
			context,
			msg,
			msg_len);
	} else {
		memcpy (passive_instance->token, msg, msg_len);
		passive_timer_expired_token_start (passive_instance);

	}

	/*
	 * Monitor for failures
	 * TODO doesn't handle wrap-around of the token
	 */
	passive_instance->token_recv_count[iface_no] += 1;
	max = 0;
	for (i = 0; i < rrp_instance->interface_count; i++) {
		if (max < passive_instance->token_recv_count[i]) {
			max = passive_instance->token_recv_count[i];
		}
	}

	for (i = 0; i < rrp_instance->interface_count; i++) {
		if ((passive_instance->faulty[i] == 0) &&
			(max - passive_instance->token_recv_count[i] > 
			rrp_instance->totem_config->rrp_problem_count_threshold)) {
			passive_instance->faulty[i] = 1;
			log_printf (
				rrp_instance->totemrrp_log_level_error,
				"Marking seqid %d ringid %d interface %s FAULTY - adminisrtative intervention required.",
				token_seq,
				i,
				totemnet_iface_print (rrp_instance->net_handles[i]));
		}
	}
}

static void passive_token_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)instance->rrp_algo_instance;

	do {
		passive_instance->token_xmit_iface = (passive_instance->token_xmit_iface + 1) % instance->interface_count;
	} while (passive_instance->faulty[passive_instance->token_xmit_iface] == 1);
	
	totemnet_token_send (
		instance->net_handles[passive_instance->token_xmit_iface],
		iovec, iov_len);

}

static void passive_recv_flush (struct totemrrp_instance *instance)
{
	struct passive_instance *rrp_algo_instance = (struct passive_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_recv_flush (instance->net_handles[i]);
		}
	}
}

static void passive_send_flush (struct totemrrp_instance *instance)
{
	struct passive_instance *rrp_algo_instance = (struct passive_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_send_flush (instance->net_handles[i]);
		}
	}
}

static void passive_iface_check (struct totemrrp_instance *instance)
{
	struct passive_instance *rrp_algo_instance = (struct passive_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_iface_check (instance->net_handles[i]);
		}
	}
}

static void passive_processor_count_set (
	struct totemrrp_instance *instance,
	unsigned int processor_count)
{
	struct passive_instance *rrp_algo_instance = (struct passive_instance *)instance->rrp_algo_instance;
	unsigned int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {

			totemnet_processor_count_set (instance->net_handles[i],
				processor_count);
		}
	}
}

static void passive_token_target_set (
	struct totemrrp_instance *instance,
	struct totem_ip_address *token_target,
	unsigned int iface_no)
{
	totemnet_token_target_set (instance->net_handles[iface_no], token_target);
}

/*
 * Active Replication Implementation
 */
void *active_instance_initialize (
	struct totemrrp_instance *rrp_instance,
	int interface_count)
{
	struct active_instance *instance;

	instance = malloc (sizeof (struct active_instance));
	if (instance == 0) {
		goto error_exit;
	}
	memset (instance, 0, sizeof (struct active_instance));

	instance->faulty = malloc (sizeof (int) * interface_count);
	if (instance->faulty == 0) {
		free (instance);
		instance = 0;
		goto error_exit;
	}
	memset (instance->faulty, 0, sizeof (unsigned int) * interface_count);

	instance->last_token_recv = malloc (sizeof (int) * interface_count);
	if (instance->last_token_recv == 0) {
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}
	memset (instance->last_token_recv, 0, sizeof (unsigned int) * interface_count);

	instance->counter_problems = malloc (sizeof (int) * interface_count);
	if (instance->counter_problems == 0) {
		free (instance->last_token_recv);
		free (instance->faulty);
		free (instance);
		instance = 0;
		goto error_exit;
	}
	memset (instance->counter_problems, 0, sizeof (unsigned int) * interface_count);

	instance->timer_expired_token = 0;

	instance->timer_problem_decrementer = 0;

	instance->rrp_instance = rrp_instance;

error_exit:
	return ((void *)instance);
}
static void timer_function_active_problem_decrementer (void *context)
{
	struct active_instance *active_instance = (struct active_instance *)context;
	struct totemrrp_instance *rrp_instance = active_instance->rrp_instance;
	unsigned int problem_found = 0;
	unsigned int i;
	
	for (i = 0; i < rrp_instance->interface_count; i++) {
		if (active_instance->counter_problems[i] > 0) {
			problem_found = 1;
			active_instance->counter_problems[i] -= 1;
			log_printf (
				rrp_instance->totemrrp_log_level_warning,
				"Decrementing problem counter for iface %s to [%d of %d]",
				totemnet_iface_print (rrp_instance->net_handles[i]),
				active_instance->counter_problems[i],
				rrp_instance->totem_config->rrp_problem_count_threshold);
		}
	}
	if (problem_found) {
		active_timer_problem_decrementer_start (active_instance);
	} else {
		active_instance->timer_problem_decrementer = 0;
	}
}

static void timer_function_active_token_expired (void *context)
{
	struct active_instance *active_instance = (struct active_instance *)context;
	struct totemrrp_instance *rrp_instance = active_instance->rrp_instance;
	unsigned int i;

	for (i = 0; i < rrp_instance->interface_count; i++) {
		if (active_instance->last_token_recv[i] == 0) {
			active_instance->counter_problems[i] += 1;

			if (active_instance->timer_problem_decrementer == 0) {
				active_timer_problem_decrementer_start (active_instance);
			}
			log_printf (
				rrp_instance->totemrrp_log_level_warning,
				"Incrementing problem counter for seqid %d iface %s to [%d of %d]",
				active_instance->last_token_seq,
				totemnet_iface_print (rrp_instance->net_handles[i]),
				active_instance->counter_problems[i],
				rrp_instance->totem_config->rrp_problem_count_threshold);
		}
	}
	for (i = 0; i < rrp_instance->interface_count; i++) {
		if (active_instance->counter_problems[i] >= rrp_instance->totem_config->rrp_problem_count_threshold)
		{
			active_instance->faulty[i] = 1;
			log_printf (
				rrp_instance->totemrrp_log_level_error,
				"Marking seqid %d ringid %d interface %s FAULTY - adminisrtative intervention required.",
				active_instance->last_token_seq,
				i,
				totemnet_iface_print (rrp_instance->net_handles[i]));
			active_timer_problem_decrementer_cancel (active_instance);
		}
	}

	rrp_instance->totemrrp_deliver_fn (
		active_instance->totemrrp_context,
		active_instance->token,
		active_instance->token_len);
}

static void active_timer_expired_token_start (
	struct active_instance *active_instance)
{
        poll_timer_add (
		active_instance->rrp_instance->poll_handle,
		active_instance->rrp_instance->totem_config->rrp_token_expired_timeout,
		(void *)active_instance,
		timer_function_active_token_expired,
		&active_instance->timer_expired_token);
}

static void active_timer_expired_token_cancel (
	struct active_instance *active_instance)
{
        poll_timer_delete (
		active_instance->rrp_instance->poll_handle,
		active_instance->timer_expired_token);
}

static void active_timer_problem_decrementer_start (
	struct active_instance *active_instance)
{
        poll_timer_add (
		active_instance->rrp_instance->poll_handle,
		active_instance->rrp_instance->totem_config->rrp_problem_count_timeout,
		(void *)active_instance,
		timer_function_active_problem_decrementer,
		&active_instance->timer_problem_decrementer);
}

static void active_timer_problem_decrementer_cancel (
	struct active_instance *active_instance)
{
        poll_timer_delete (
		active_instance->rrp_instance->poll_handle,
		active_instance->timer_problem_decrementer);
}


/*
 * active replication
 */
static void active_mcast_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len)
{
	instance->totemrrp_deliver_fn (
		context,
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
	unsigned int iface_no,
	void *context,
	void *msg,
	unsigned int msg_len,
	unsigned int token_seq)
{
	int i;
	struct active_instance *active_instance = (struct active_instance *)instance->rrp_algo_instance;

	active_instance->totemrrp_context = context; // this should be in totemrrp_instance ?
	if (token_seq > active_instance->last_token_seq) {
		memcpy (active_instance->token, msg, msg_len);
		active_instance->token_len = msg_len;
		for (i = 0; i < instance->interface_count; i++) {
			active_instance->last_token_recv[i] = 0;
		}

		active_instance->last_token_recv[iface_no] = 1;
		active_timer_expired_token_start (active_instance);
	}

	active_instance->last_token_seq = token_seq;

	if (token_seq == active_instance->last_token_seq) {
		active_instance->last_token_recv[iface_no] = 1;
		for (i = 0; i < instance->interface_count; i++) {
			if ((active_instance->last_token_recv[i] == 0) &&
				active_instance->faulty[i] == 0) {
				return; /* don't deliver token */
			}
		}
		active_timer_expired_token_cancel (active_instance);

		instance->totemrrp_deliver_fn (
			context,
			msg,
			msg_len);
	}
}

static void active_token_send (
	struct totemrrp_instance *instance,
	struct iovec *iovec,
	unsigned int iov_len)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	int i;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {
			totemnet_token_send (
				instance->net_handles[i],
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

static void active_token_target_set (
	struct totemrrp_instance *instance,
	struct totem_ip_address *token_target,
	unsigned int iface_no)
{
	totemnet_token_target_set (instance->net_handles[iface_no], token_target);
}

struct deliver_fn_context {
	struct totemrrp_instance *instance;
	void *context;
	int iface_no;
};

static void totemrrp_instance_initialize (struct totemrrp_instance *instance)
{
	memset (instance, 0, sizeof (struct totemrrp_instance));
}

static int totemrrp_algorithm_set (
	struct totem_config *totem_config,
	struct totemrrp_instance *instance)
{
	unsigned int res = -1;
	unsigned int i;

	for (i = 0; i < RRP_ALGOS_COUNT; i++) {
		if (strcmp (totem_config->rrp_mode, rrp_algos[i]->name) == 0) {
			instance->rrp_algo = rrp_algos[i];
			if (rrp_algos[i]->initialize) {
				instance->rrp_algo_instance = rrp_algos[i]->initialize (
					instance,
					totem_config->interface_count);
			}
			res = 0;
			break;
		}
	}
	return (res);
}

void rrp_deliver_fn (
	void *context,
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
			deliver_fn_context->iface_no,
			deliver_fn_context->context,
			msg,
			msg_len,
			token_seqid);
	} else {
		/*
		 * Deliver to the mcast receiver for this rrp algorithm 
		 */
		deliver_fn_context->instance->rrp_algo->mcast_recv (
			deliver_fn_context->instance,
			deliver_fn_context->iface_no,
			deliver_fn_context->context,
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
		iface_addr,
		deliver_fn_context->iface_no);
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
		void *msg,
		int msg_len),

	void (*iface_change_fn) (
		void *context,
		struct totem_ip_address *iface_addr,
		unsigned int iface_no),

	void (*token_seqid_get) (
		void *msg,
		unsigned int *seqid,
		unsigned int *token_is),

	unsigned int (*msgs_missing) (void))
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

	instance->totem_config = totem_config;

	res = totemrrp_algorithm_set (
		instance->totem_config,
		instance);
	if (res == -1) {
		goto error_put;
		return (-1);
	}

	/*
	* Configure logging
	*/
	instance->totemrrp_log_level_security = totem_config->totem_logging_configuration.log_level_security;
	instance->totemrrp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemrrp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemrrp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemrrp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemrrp_log_printf = totem_config->totem_logging_configuration.log_printf;

	instance->interfaces = totem_config->interfaces;

	instance->totemrrp_poll_handle = poll_handle;

	instance->totemrrp_deliver_fn = deliver_fn;

	instance->totemrrp_iface_change_fn = iface_change_fn;

	instance->totemrrp_token_seqid_get = token_seqid_get;

	instance->totemrrp_msgs_missing = msgs_missing;

	instance->interface_count = totem_config->interface_count;

	instance->net_handles = malloc (sizeof (totemnet_handle) * totem_config->interface_count);

	instance->context = context;

	instance->poll_handle = poll_handle;

	for (i = 0; i < totem_config->interface_count; i++) {
		struct deliver_fn_context *deliver_fn_context;

		deliver_fn_context = malloc (sizeof (struct deliver_fn_context));
		assert (deliver_fn_context);
		deliver_fn_context->instance = instance;
		deliver_fn_context->context = context;
		deliver_fn_context->iface_no = i;

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

error_put:
	hdb_handle_put (&totemrrp_instance_database, *handle);
error_destroy:
	hdb_handle_destroy (&totemrrp_instance_database, *handle);
	return (res);
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

int totemrrp_token_target_set (
	totemrrp_handle handle,
	struct totem_ip_address *addr,
	unsigned int iface_no)
{
	struct totemrrp_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	instance->rrp_algo->token_target_set (instance, addr, iface_no);

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

	instance->rrp_algo->token_send (instance, iovec, iov_len);

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

int totemrrp_interfaces_get (
        totemrrp_handle handle,
        struct totem_ip_address *interfaces,
        unsigned int *iface_count)
{
	struct totemrrp_instance *instance;
	int res = 0;
	unsigned int i;

	res = hdb_handle_get (&totemrrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}
	
	for (i = 0; i < instance->interface_count; i++) {
		totemnet_iface_get (instance->net_handles[i], &interfaces[i]);
	}

	hdb_handle_put (&totemrrp_instance_database, handle);
error_exit:
	return (res);
}
