/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#include <limits.h>

#include <corosync/sq.h>
#include <corosync/list.h>
#include <corosync/swab.h>
#include <qb/qbdefs.h>
#include <qb/qbloop.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>

#include "totemnet.h"
#include "totemrrp.h"

void rrp_deliver_fn (
	void *context,
	const void *msg,
	unsigned int msg_len);

void rrp_iface_change_fn (
	void *context,
	const struct totem_ip_address *iface_addr);

struct totemrrp_instance;
struct passive_instance {
	struct totemrrp_instance *rrp_instance;
	unsigned int *faulty;
	unsigned int *token_recv_count;
	unsigned int *mcast_recv_count;
	unsigned char token[15000];
	unsigned int token_len;
        qb_loop_timer_handle timer_expired_token;
        qb_loop_timer_handle timer_problem_decrementer;
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
        qb_loop_timer_handle timer_expired_token;
        qb_loop_timer_handle timer_problem_decrementer;
	void *totemrrp_context;
};

struct rrp_algo {
	const char *name;

	void * (*initialize) (
		struct totemrrp_instance *rrp_instance,
		int interface_count);

	void (*mcast_recv) (
		struct totemrrp_instance *instance,
		unsigned int iface_no,
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*mcast_noflush_send) (
		struct totemrrp_instance *instance,
		const void *msg,
		unsigned int msg_len);

	void (*mcast_flush_send) (
		struct totemrrp_instance *instance,
		const void *msg,
		unsigned int msg_len);

	void (*token_recv) (
		struct totemrrp_instance *instance,
		unsigned int iface_no,
		void *context,
		const void *msg,
		unsigned int msg_len,
		unsigned int token_seqid);

	void (*token_send) (
		struct totemrrp_instance *instance,
		const void *msg,
		unsigned int msg_len);

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

	void (*ring_reenable) (
		struct totemrrp_instance *instance,
		unsigned int iface_no);

	int (*mcast_recv_empty) (
		struct totemrrp_instance *instance);

	int (*member_add) (
		struct totemrrp_instance *instance,
		const struct totem_ip_address *member,
		unsigned int iface_no);

	int (*member_remove) (
		struct totemrrp_instance *instance,
		const struct totem_ip_address *member,
		unsigned int iface_no);

	void (*membership_changed) (
		struct totemrrp_instance *instance,
	        enum totem_configuration_type configuration_type,
		const struct srp_addr *member_list, size_t member_list_entries,
		const struct srp_addr *left_list, size_t left_list_entries,
		const struct srp_addr *joined_list, size_t joined_list_entries,
		const struct memb_ring_id *ring_id);
};

#define STATUS_STR_LEN 512
struct totemrrp_instance {
	qb_loop_t *poll_handle;

	struct totem_interface *interfaces;

	struct rrp_algo *rrp_algo;

	void *context;

	char *status[INTERFACE_MAX];

	void (*totemrrp_deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*totemrrp_iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_addr,
		unsigned int iface_no);

	void (*totemrrp_token_seqid_get) (
		const void *msg,
		unsigned int *seqid,
		unsigned int *token_is);

	void (*totemrrp_target_set_completed) (
		void *context);

	unsigned int (*totemrrp_msgs_missing) (void);

	/*
	 * Function and data used to log messages
	 */
	int totemrrp_log_level_security;

	int totemrrp_log_level_error;

	int totemrrp_log_level_warning;

	int totemrrp_log_level_notice;

	int totemrrp_log_level_debug;

	int totemrrp_subsys_id;

	void (*totemrrp_log_printf) (
		int level,
		int subsys,
		const char *function,
		const char *file,
		int line,
		const char *format, ...)__attribute__((format(printf, 6, 7)));

	void **net_handles;

	void *rrp_algo_instance;

	int interface_count;

	int processor_count;

	int my_nodeid;

	struct totem_config *totem_config;

	void *deliver_fn_context[INTERFACE_MAX];

	qb_loop_timer_handle timer_active_test_ring_timeout[INTERFACE_MAX];

	totemrrp_stats_t stats;
};

static void stats_set_interface_faulty(struct totemrrp_instance *rrp_instance,
		unsigned int iface_no, int is_faulty);

/*
 * None Replication Forward Declerations
 */
static void none_mcast_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len);

static void none_mcast_noflush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

static void none_mcast_flush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

static void none_token_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void none_token_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

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

static void none_ring_reenable (
	struct totemrrp_instance *instance,
	unsigned int iface_no);

static int none_mcast_recv_empty (
	struct totemrrp_instance *instance);

static int none_member_add (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no);

static int none_member_remove (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no);

static void none_membership_changed (
	struct totemrrp_instance *instance,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

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
	const void *msg,
	unsigned int msg_len);

static void passive_mcast_noflush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

static void passive_mcast_flush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

static void passive_monitor (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	int is_token_recv_count);

static void passive_token_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void passive_token_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

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

static void passive_ring_reenable (
	struct totemrrp_instance *instance,
	unsigned int iface_no);

static int passive_mcast_recv_empty (
	struct totemrrp_instance *instance);

static int passive_member_add (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no);

static int passive_member_remove (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no);

static void passive_membership_changed (
	struct totemrrp_instance *instance,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

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
	const void *msg,
	unsigned int msg_len);

static void active_mcast_noflush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

static void active_mcast_flush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

static void active_token_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len,
	unsigned int token_seqid);

static void active_token_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len);

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

static void active_ring_reenable (
	struct totemrrp_instance *instance,
	unsigned int iface_no);

static int active_mcast_recv_empty (
	struct totemrrp_instance *instance);

static int active_member_add (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no);

static int active_member_remove (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no);

static void active_membership_changed (
	struct totemrrp_instance *instance,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

static void active_timer_expired_token_start (
	struct active_instance *active_instance);

static void active_timer_expired_token_cancel (
	struct active_instance *active_instance);

static void active_timer_problem_decrementer_start (
	struct active_instance *active_instance);

static void active_timer_problem_decrementer_cancel (
	struct active_instance *active_instance);

/*
 * 0-5 reserved for totemsrp.c
 */
#define MESSAGE_TYPE_RING_TEST_ACTIVE		6
#define MESSAGE_TYPE_RING_TEST_ACTIVATE		7

#define ENDIAN_LOCAL				0xff22

/*
 * Rollover handling:
 *
 * ARR_SEQNO_START_TOKEN is the starting sequence number of last seen sequence
 * for a token for active redundand ring.  This should remain zero, unless testing
 * overflow in which case 07fffff00 or 0xffffff00 are good starting values.
 * It should be same as on defined in totemsrp.c
 */

#define ARR_SEQNO_START_TOKEN 0x0

/*
 * These can be used ot test different rollover points
 * #define ARR_SEQNO_START_MSG 0xfffffe00
 */

/*
 * Threshold value when recv_count for passive rrp should be adjusted.
 * Set this value to some smaller for testing of adjusting proper
 * functionality. Also keep in mind that this value must be smaller
 * then rrp_problem_count_threshold
 */
#define PASSIVE_RECV_COUNT_THRESHOLD		(INT_MAX / 2)

struct message_header {
	char type;
	char encapsulated;
	unsigned short endian_detector;
	int ring_number;
	int nodeid_activator;
} __attribute__((packed));

struct deliver_fn_context {
	struct totemrrp_instance *instance;
	void *context;
	int iface_no;
};

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
	.token_target_set	= none_token_target_set,
	.ring_reenable		= none_ring_reenable,
	.mcast_recv_empty	= none_mcast_recv_empty,
	.member_add		= none_member_add,
	.member_remove		= none_member_remove,
	.membership_changed	= none_membership_changed
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
	.token_target_set	= passive_token_target_set,
	.ring_reenable		= passive_ring_reenable,
	.mcast_recv_empty	= passive_mcast_recv_empty,
	.member_add		= passive_member_add,
	.member_remove		= passive_member_remove,
	.membership_changed	= passive_membership_changed
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
	.token_target_set	= active_token_target_set,
	.ring_reenable		= active_ring_reenable,
	.mcast_recv_empty	= active_mcast_recv_empty,
	.member_add		= active_member_add,
	.member_remove		= active_member_remove,
	.membership_changed	= active_membership_changed
};

struct rrp_algo *rrp_algos[] = {
	&none_algo,
	&passive_algo,
	&active_algo
};

#define RRP_ALGOS_COUNT 3

#define log_printf(level, format, args...)			\
do {								\
	rrp_instance->totemrrp_log_printf (			\
		level, rrp_instance->totemrrp_subsys_id,	\
		__FUNCTION__, __FILE__, __LINE__,		\
		format, ##args);				\
} while (0);

static void stats_set_interface_faulty(struct totemrrp_instance *rrp_instance,
		unsigned int iface_no, int is_faulty)
{
	rrp_instance->stats.faulty[iface_no] = (is_faulty ? 1 : 0);
}

static void test_active_msg_endian_convert(const struct message_header *in, struct message_header *out)
{
	out->type = in->type;
	out->encapsulated = in->encapsulated;
	out->endian_detector = ENDIAN_LOCAL;
	out->ring_number = swab32 (in->ring_number);
	out->nodeid_activator = swab32(in->nodeid_activator);
}

static void timer_function_test_ring_timeout (void *context)
{
	struct deliver_fn_context *deliver_fn_context = (struct deliver_fn_context *)context;
	struct totemrrp_instance *rrp_instance = deliver_fn_context->instance;
	unsigned int *faulty = NULL;
	int iface_no = deliver_fn_context->iface_no;
	struct message_header msg = {
		.type = MESSAGE_TYPE_RING_TEST_ACTIVE,
		.endian_detector = ENDIAN_LOCAL,
	};

	if (strcmp(rrp_instance->totem_config->rrp_mode, "active") == 0)
		faulty = ((struct active_instance *)(rrp_instance->rrp_algo_instance))->faulty;
	if (strcmp(rrp_instance->totem_config->rrp_mode, "passive") == 0)
		faulty = ((struct passive_instance *)(rrp_instance->rrp_algo_instance))->faulty;

	assert (faulty != NULL);

	if (faulty[iface_no] == 1) {
		msg.ring_number = iface_no;
		msg.nodeid_activator = rrp_instance->my_nodeid;
		totemnet_token_send (
			rrp_instance->net_handles[iface_no],
			&msg, sizeof (struct message_header));
		qb_loop_timer_add (rrp_instance->poll_handle,
			QB_LOOP_MED,
			rrp_instance->totem_config->rrp_autorecovery_check_timeout*QB_TIME_NS_IN_MSEC,
			(void *)deliver_fn_context,
			timer_function_test_ring_timeout,
			&rrp_instance->timer_active_test_ring_timeout[iface_no]);
	}
}

/*
 * None Replication Implementation
 */

static void none_mcast_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len)
{
	rrp_instance->totemrrp_deliver_fn (
		context,
		msg,
		msg_len);
}

static void none_mcast_flush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	totemnet_mcast_flush_send (instance->net_handles[0], msg, msg_len);
}

static void none_mcast_noflush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	totemnet_mcast_noflush_send (instance->net_handles[0], msg, msg_len);
}

static void none_token_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
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
	const void *msg,
	unsigned int msg_len)
{
	totemnet_token_send (
		instance->net_handles[0],
		msg, msg_len);
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

static void none_ring_reenable (
	struct totemrrp_instance *instance,
	unsigned int iface_no)
{
	/*
	 * No operation
	 */
}

static int none_mcast_recv_empty (
	struct totemrrp_instance *instance)
{
	int res;

	res = totemnet_recv_mcast_empty (instance->net_handles[0]);

	return (res);
}

static int none_member_add (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no)
{
	int res;
	res = totemnet_member_add (instance->net_handles[0], member);
	return (res);
}

static int none_member_remove (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no)
{
	int res;
	res = totemnet_member_remove (instance->net_handles[0], member);
	return (res);
}

static void none_membership_changed (
	struct totemrrp_instance *rrp_instance,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;

	for (i = 0; i < left_list_entries; i++) {
		if (left_list->no_addrs < 1 ||
		    (left_list[i].addr[0].family != AF_INET && left_list[i].addr[0].family != AF_INET6)) {
			log_printf(rrp_instance->totemrrp_log_level_error,
				"Membership left list contains incorrect address. "
				"This is sign of misconfiguration between nodes!");
		} else {
			totemnet_member_set_active(rrp_instance->net_handles[0],
			    &left_list[i].addr[0], 0);
		}
	}

	for (i = 0; i < joined_list_entries; i++) {
		if (joined_list->no_addrs < 1 ||
		    (joined_list[i].addr[0].family != AF_INET && joined_list[i].addr[0].family != AF_INET6)) {
			log_printf(rrp_instance->totemrrp_log_level_error,
				"Membership join list contains incorrect address. "
				"This is sign of misconfiguration between nodes!");
		} else {
			totemnet_member_set_active(rrp_instance->net_handles[0],
			    &joined_list[i].addr[0], 1);
		}
	}
}

/*
 * Passive Replication Implementation
 */
void *passive_instance_initialize (
	struct totemrrp_instance *rrp_instance,
	int interface_count)
{
	struct passive_instance *instance;
	int i;

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

	for (i = 0; i < interface_count; i++) {
		stats_set_interface_faulty (rrp_instance, i, 0);
	}

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
        qb_loop_timer_add (
		passive_instance->rrp_instance->poll_handle,
		QB_LOOP_MED,
		passive_instance->rrp_instance->totem_config->rrp_token_expired_timeout*QB_TIME_NS_IN_MSEC,
		(void *)passive_instance,
		timer_function_passive_token_expired,
		&passive_instance->timer_expired_token);
}

static void passive_timer_expired_token_cancel (
	struct passive_instance *passive_instance)
{
        qb_loop_timer_del (
		passive_instance->rrp_instance->poll_handle,
		passive_instance->timer_expired_token);
}

/*
static void passive_timer_problem_decrementer_start (
	struct passive_instance *passive_instance)
{
        qb_loop_timer_add (
		QB_LOOP_MED,
		passive_instance->rrp_instance->poll_handle,
		passive_instance->rrp_instance->totem_config->rrp_problem_count_timeout*QB_TIME_NS_IN_MSEC,
		(void *)passive_instance,
		timer_function_passive_problem_decrementer,
		&passive_instance->timer_problem_decrementer);
}

static void passive_timer_problem_decrementer_cancel (
	struct passive_instance *passive_instance)
{
        qb_loop_timer_del (
		passive_instance->rrp_instance->poll_handle,
		passive_instance->timer_problem_decrementer);
}
*/

/*
 * Monitor function implementation from rrp paper.
 * rrp_instance is passive rrp instance, iface_no is interface with received messgae/token and
 * is_token_recv_count is boolean variable which donates if message is token (>1) or regular
 * message (= 0)
 */
static void passive_monitor (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	int is_token_recv_count)
{
	struct passive_instance *passive_instance = (struct passive_instance *)rrp_instance->rrp_algo_instance;
	unsigned int *recv_count;
	unsigned int max;
	unsigned int i;
	unsigned int min_all, min_active;
	unsigned int threshold;

	/*
	 * Monitor for failures
	 */
	if (is_token_recv_count) {
		recv_count = passive_instance->token_recv_count;
		threshold = rrp_instance->totem_config->rrp_problem_count_threshold;
	} else {
		recv_count = passive_instance->mcast_recv_count;
		threshold = rrp_instance->totem_config->rrp_problem_count_mcast_threshold;
	}

	recv_count[iface_no] += 1;

	max = 0;
	for (i = 0; i < rrp_instance->interface_count; i++) {
		if (max < recv_count[i]) {
			max = recv_count[i];
		}
	}

	/*
	 * Max is larger than threshold -> start adjusting process
	 */
	if (max > PASSIVE_RECV_COUNT_THRESHOLD) {
		min_all = min_active = recv_count[iface_no];

		for (i = 0; i < rrp_instance->interface_count; i++) {
			if (recv_count[i] < min_all) {
				min_all = recv_count[i];
			}

			if (passive_instance->faulty[i] == 0 &&
			    recv_count[i] < min_active) {
				min_active = recv_count[i];
			}
		}

		if (min_all > 0) {
			/*
			 * There is one or more faulty device with recv_count > 0
			 */
			for (i = 0; i < rrp_instance->interface_count; i++) {
				recv_count[i] -= min_all;
			}
		} else {
			/*
			 * No faulty device with recv_count > 0, adjust only active
			 * devices
			 */
			for (i = 0; i < rrp_instance->interface_count; i++) {
				if (passive_instance->faulty[i] == 0) {
					recv_count[i] -= min_active;
				}
			}
		}

		/*
		 * Find again max
		 */
		max = 0;

		for (i = 0; i < rrp_instance->interface_count; i++) {
			if (max < recv_count[i]) {
				max = recv_count[i];
			}
		}
	}

	for (i = 0; i < rrp_instance->interface_count; i++) {
		if ((passive_instance->faulty[i] == 0) &&
		    (max - recv_count[i] > threshold)) {
			passive_instance->faulty[i] = 1;

			qb_loop_timer_add (rrp_instance->poll_handle,
				QB_LOOP_MED,
				rrp_instance->totem_config->rrp_autorecovery_check_timeout*QB_TIME_NS_IN_MSEC,
				rrp_instance->deliver_fn_context[i],
				timer_function_test_ring_timeout,
				&rrp_instance->timer_active_test_ring_timeout[i]);

			stats_set_interface_faulty (rrp_instance, i, passive_instance->faulty[i]);

			snprintf (rrp_instance->status[i], STATUS_STR_LEN,
				"Marking ringid %u interface %s FAULTY",
				i,
				totemnet_iface_print (rrp_instance->net_handles[i]));
			log_printf (
				rrp_instance->totemrrp_log_level_error,
				"%s",
				rrp_instance->status[i]);
		}
	}
}

static void passive_mcast_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)rrp_instance->rrp_algo_instance;

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

	passive_monitor (rrp_instance, iface_no, 0);
}

static void passive_mcast_flush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)instance->rrp_algo_instance;
	int i = 0;

	do {
		passive_instance->msg_xmit_iface = (passive_instance->msg_xmit_iface + 1) % instance->interface_count;
		i++;
	} while ((i <= instance->interface_count) && (passive_instance->faulty[passive_instance->msg_xmit_iface] == 1));

	if (i > instance->interface_count) {
		/*
		 * All interfaces are faulty. It's still needed to send mcast
		 * message to local host so use first interface.
		 */
		passive_instance->msg_xmit_iface = 0;
	}

	totemnet_mcast_flush_send (instance->net_handles[passive_instance->msg_xmit_iface], msg, msg_len);
}

static void passive_mcast_noflush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)instance->rrp_algo_instance;
	int i = 0;

	do {
		passive_instance->msg_xmit_iface = (passive_instance->msg_xmit_iface + 1) % instance->interface_count;
		i++;
	} while ((i <= instance->interface_count) && (passive_instance->faulty[passive_instance->msg_xmit_iface] == 1));


	if (i > instance->interface_count) {
		/*
		 * All interfaces are faulty. It's still needed to send mcast
		 * message to local host so use first interface.
		 */
		passive_instance->msg_xmit_iface = 0;
	}

	totemnet_mcast_noflush_send (instance->net_handles[passive_instance->msg_xmit_iface], msg, msg_len);
}

static void passive_token_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len,
	unsigned int token_seq)
{
	struct passive_instance *passive_instance = (struct passive_instance *)rrp_instance->rrp_algo_instance;

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

	passive_monitor (rrp_instance, iface_no, 1);
}

static void passive_token_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	struct passive_instance *passive_instance = (struct passive_instance *)instance->rrp_algo_instance;
	int i = 0;

	do {
		passive_instance->token_xmit_iface = (passive_instance->token_xmit_iface + 1) % instance->interface_count;
		i++;
	} while ((i <= instance->interface_count) && (passive_instance->faulty[passive_instance->token_xmit_iface] == 1));

	if (i > instance->interface_count) {
		/*
		 * All interfaces are faulty. It's still needed to send token
		 * message to (potentionally) local host so use first interface.
		 */
		passive_instance->msg_xmit_iface = 0;
	}

	totemnet_token_send (
	    instance->net_handles[passive_instance->token_xmit_iface],
	    msg, msg_len);
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

static int passive_mcast_recv_empty (
	struct totemrrp_instance *instance)
{
	int res;
	int msgs_emptied = 0;
	int i;

	for (i = 0; i < instance->interface_count; i++) {
		res = totemnet_recv_mcast_empty (instance->net_handles[i]);
		if (res == -1) {
			return (-1);
		}
		if (res == 1) {
			msgs_emptied = 1;
		}
	}

	return (msgs_emptied);
}

static int passive_member_add (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no)
{
	int res;
	res = totemnet_member_add (instance->net_handles[iface_no], member);
	return (res);
}

static int passive_member_remove (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no)
{
	int res;
	res = totemnet_member_remove (instance->net_handles[iface_no], member);
	return (res);
}

static void passive_membership_changed (
	struct totemrrp_instance *rrp_instance,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;
	int interface;

	for (interface = 0; interface < rrp_instance->interface_count; interface++) {
		for (i = 0; i < left_list_entries; i++) {
			if (left_list->no_addrs < interface + 1 ||
			    (left_list[i].addr[interface].family != AF_INET &&
			     left_list[i].addr[interface].family != AF_INET6)) {
				log_printf(rrp_instance->totemrrp_log_level_error,
					"Membership left list contains incorrect address. "
					"This is sign of misconfiguration between nodes!");
			} else {
				totemnet_member_set_active(rrp_instance->net_handles[interface],
				    &left_list[i].addr[interface], 0);
			}
		}

		for (i = 0; i < joined_list_entries; i++) {
			if (joined_list->no_addrs < interface + 1 ||
			    (joined_list[i].addr[interface].family != AF_INET &&
			     joined_list[i].addr[interface].family != AF_INET6)) {
				log_printf(rrp_instance->totemrrp_log_level_error,
					"Membership join list contains incorrect address. "
					"This is sign of misconfiguration between nodes!");
			} else {
				totemnet_member_set_active(rrp_instance->net_handles[interface],
				    &joined_list[i].addr[interface], 1);
			}
		}
	}
}

static void passive_ring_reenable (
	struct totemrrp_instance *instance,
	unsigned int iface_no)
{
	struct passive_instance *rrp_algo_instance = (struct passive_instance *)instance->rrp_algo_instance;
	int i;

	memset (rrp_algo_instance->mcast_recv_count, 0, sizeof (unsigned int) *
		instance->interface_count);
	memset (rrp_algo_instance->token_recv_count, 0, sizeof (unsigned int) *
		instance->interface_count);

	if (iface_no == instance->interface_count) {
		memset (rrp_algo_instance->faulty, 0, sizeof (unsigned int) *
			instance->interface_count);
		for (i = 0; i < instance->interface_count; i++) {
			stats_set_interface_faulty (instance, i, 0);
		}
	} else {
		rrp_algo_instance->faulty[iface_no] = 0;
		stats_set_interface_faulty (instance, iface_no, 0);
	}
}

/*
 * Active Replication Implementation
 */
void *active_instance_initialize (
	struct totemrrp_instance *rrp_instance,
	int interface_count)
{
	struct active_instance *instance;
	int i;

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

	for (i = 0; i < interface_count; i++) {
		stats_set_interface_faulty (rrp_instance, i, 0);
	}

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

	instance->last_token_seq = ARR_SEQNO_START_TOKEN - 1;

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
			if (active_instance->counter_problems[i] == 0) {
				snprintf (rrp_instance->status[i], STATUS_STR_LEN,
					"ring %d active with no faults", i);
			} else {
				snprintf (rrp_instance->status[i], STATUS_STR_LEN,
					"Decrementing problem counter for iface %s to [%d of %d]",
					totemnet_iface_print (rrp_instance->net_handles[i]),
					active_instance->counter_problems[i],
					rrp_instance->totem_config->rrp_problem_count_threshold);
			}
				log_printf (
					rrp_instance->totemrrp_log_level_warning,
					"%s",
					rrp_instance->status[i]);
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
			snprintf (rrp_instance->status[i], STATUS_STR_LEN,
				"Incrementing problem counter for seqid %d iface %s to [%d of %d]",
				active_instance->last_token_seq,
				totemnet_iface_print (rrp_instance->net_handles[i]),
				active_instance->counter_problems[i],
				rrp_instance->totem_config->rrp_problem_count_threshold);
			log_printf (
				rrp_instance->totemrrp_log_level_warning,
				"%s",
				rrp_instance->status[i]);
		}
	}
	for (i = 0; i < rrp_instance->interface_count; i++) {
		if (active_instance->counter_problems[i] >= rrp_instance->totem_config->rrp_problem_count_threshold &&
		    active_instance->faulty[i] == 0) {
			active_instance->faulty[i] = 1;

			qb_loop_timer_add (rrp_instance->poll_handle,
				QB_LOOP_MED,
				rrp_instance->totem_config->rrp_autorecovery_check_timeout*QB_TIME_NS_IN_MSEC,
				rrp_instance->deliver_fn_context[i],
				timer_function_test_ring_timeout,
				&rrp_instance->timer_active_test_ring_timeout[i]);

			stats_set_interface_faulty (rrp_instance, i, active_instance->faulty[i]);

			snprintf (rrp_instance->status[i], STATUS_STR_LEN,
				"Marking seqid %d ringid %u interface %s FAULTY",
				active_instance->last_token_seq,
				i,
				totemnet_iface_print (rrp_instance->net_handles[i]));
			log_printf (
				rrp_instance->totemrrp_log_level_error,
				"%s",
				rrp_instance->status[i]);
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
        qb_loop_timer_add (
		active_instance->rrp_instance->poll_handle,
		QB_LOOP_MED,
		active_instance->rrp_instance->totem_config->rrp_token_expired_timeout*QB_TIME_NS_IN_MSEC,
		(void *)active_instance,
		timer_function_active_token_expired,
		&active_instance->timer_expired_token);
}

static void active_timer_expired_token_cancel (
	struct active_instance *active_instance)
{
        qb_loop_timer_del (
		active_instance->rrp_instance->poll_handle,
		active_instance->timer_expired_token);
}

static void active_timer_problem_decrementer_start (
	struct active_instance *active_instance)
{
        qb_loop_timer_add (
		active_instance->rrp_instance->poll_handle,
		QB_LOOP_MED,
		active_instance->rrp_instance->totem_config->rrp_problem_count_timeout*QB_TIME_NS_IN_MSEC,
		(void *)active_instance,
		timer_function_active_problem_decrementer,
		&active_instance->timer_problem_decrementer);
}

static void active_timer_problem_decrementer_cancel (
	struct active_instance *active_instance)
{
        qb_loop_timer_del (
		active_instance->rrp_instance->poll_handle,
		active_instance->timer_problem_decrementer);
	active_instance->timer_problem_decrementer = 0;
}


/*
 * active replication
 */
static void active_mcast_recv (
	struct totemrrp_instance *instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len)
{
	instance->totemrrp_deliver_fn (
		context,
		msg,
		msg_len);
}

static void active_mcast_flush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	int i;
	int msg_sent;
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;

	msg_sent = 0;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {
			msg_sent = 1;
			totemnet_mcast_flush_send (instance->net_handles[i], msg, msg_len);
		}
	}

	if (!msg_sent) {
		/*
		 * All interfaces are faulty. It's still needed to send mcast
		 * message to local host so use first interface.
		 */
		totemnet_mcast_flush_send (instance->net_handles[0], msg, msg_len);
	}
}

static void active_mcast_noflush_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	int i;
	int msg_sent;
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;

	msg_sent = 0;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {
			msg_sent = 1;
			totemnet_mcast_noflush_send (instance->net_handles[i], msg, msg_len);
		}
	}

	if (!msg_sent) {
		/*
		 * All interfaces are faulty. It's still needed to send mcast
		 * message to local host so use first interface.
		 */
		totemnet_mcast_noflush_send (instance->net_handles[0], msg, msg_len);
	}
}

static void active_token_recv (
	struct totemrrp_instance *rrp_instance,
	unsigned int iface_no,
	void *context,
	const void *msg,
	unsigned int msg_len,
	unsigned int token_seq)
{
	int i;
	struct active_instance *active_instance = (struct active_instance *)rrp_instance->rrp_algo_instance;

	active_instance->totemrrp_context = context;
	if (sq_lt_compare (active_instance->last_token_seq, token_seq)) {
		memcpy (active_instance->token, msg, msg_len);
		active_instance->token_len = msg_len;
		for (i = 0; i < rrp_instance->interface_count; i++) {
			active_instance->last_token_recv[i] = 0;
		}

		active_instance->last_token_recv[iface_no] = 1;
		active_timer_expired_token_start (active_instance);
	}

	/*
	 * This doesn't follow spec because the spec assumes we will know
	 * when token resets occur.
	 */
	active_instance->last_token_seq = token_seq;

	if (token_seq == active_instance->last_token_seq) {
		active_instance->last_token_recv[iface_no] = 1;
		for (i = 0; i < rrp_instance->interface_count; i++) {
			if ((active_instance->last_token_recv[i] == 0) &&
				active_instance->faulty[i] == 0) {
				return; /* don't deliver token */
			}
		}
		active_timer_expired_token_cancel (active_instance);

		rrp_instance->totemrrp_deliver_fn (
			context,
			msg,
			msg_len);
	}
}

static void active_token_send (
	struct totemrrp_instance *instance,
	const void *msg,
	unsigned int msg_len)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	int i;
	int msg_sent;

	msg_sent = 0;

	for (i = 0; i < instance->interface_count; i++) {
		if (rrp_algo_instance->faulty[i] == 0) {
			msg_sent = 1;
			totemnet_token_send (
				instance->net_handles[i],
				msg, msg_len);

		}
	}

	if (!msg_sent) {
		/*
		 * All interfaces are faulty. It's still needed to send token
		 * message to (potentionally) local host so use first interface.
		 */
		totemnet_token_send (
			instance->net_handles[0],
			msg, msg_len);

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

static int active_member_add (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no)
{
	int res;
	res = totemnet_member_add (instance->net_handles[iface_no], member);
	return (res);
}

static int active_member_remove (
	struct totemrrp_instance *instance,
	const struct totem_ip_address *member,
	unsigned int iface_no)
{
	int res;
	res = totemnet_member_remove (instance->net_handles[iface_no], member);
	return (res);
}

static void active_membership_changed (
	struct totemrrp_instance *rrp_instance,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;
	int interface;

	for (interface = 0; interface < rrp_instance->interface_count; interface++) {
		for (i = 0; i < left_list_entries; i++) {
			if (left_list->no_addrs < interface + 1 ||
			    (left_list[i].addr[interface].family != AF_INET &&
			     left_list[i].addr[interface].family != AF_INET6)) {
				log_printf(rrp_instance->totemrrp_log_level_error,
					"Membership left list contains incorrect address. "
					"This is sign of misconfiguration between nodes!");
			} else {
				totemnet_member_set_active(rrp_instance->net_handles[interface],
				    &left_list[i].addr[interface], 0);
			}
		}

		for (i = 0; i < joined_list_entries; i++) {
			if (joined_list->no_addrs < interface + 1 ||
			    (joined_list[i].addr[interface].family != AF_INET &&
			     joined_list[i].addr[interface].family != AF_INET6)) {
				log_printf(rrp_instance->totemrrp_log_level_error,
					"Membership join list contains incorrect address. "
					"This is sign of misconfiguration between nodes!");
			} else {
				totemnet_member_set_active(rrp_instance->net_handles[interface],
				    &joined_list[i].addr[interface], 1);
			}
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

static int active_mcast_recv_empty (
	struct totemrrp_instance *instance)
{
	int res;
	int msgs_emptied = 0;
	int i;

	for (i = 0; i < instance->interface_count; i++) {
		res = totemnet_recv_mcast_empty (instance->net_handles[i]);
		if (res == -1) {
			return (-1);
		}
		if (res == 1) {
			msgs_emptied = 1;
		}
	}

	return (msgs_emptied);
}

static void active_ring_reenable (
	struct totemrrp_instance *instance,
	unsigned int iface_no)
{
	struct active_instance *rrp_algo_instance = (struct active_instance *)instance->rrp_algo_instance;
	int i;

	if (iface_no == instance->interface_count) {
		memset (rrp_algo_instance->last_token_recv, 0, sizeof (unsigned int) *
			instance->interface_count);
		memset (rrp_algo_instance->faulty, 0, sizeof (unsigned int) *
			instance->interface_count);
		memset (rrp_algo_instance->counter_problems, 0, sizeof (unsigned int) *
			instance->interface_count);

		for (i = 0; i < instance->interface_count; i++) {
			stats_set_interface_faulty (instance, i, 0);
		}
	} else {
		rrp_algo_instance->last_token_recv[iface_no] = 0;
		rrp_algo_instance->faulty[iface_no] = 0;
		rrp_algo_instance->counter_problems[iface_no] = 0;

		stats_set_interface_faulty (instance, iface_no, 0);
	}
}

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
	for (i = 0; i < totem_config->interface_count; i++) {
		instance->status[i] = malloc (STATUS_STR_LEN+1);
		snprintf (instance->status[i], STATUS_STR_LEN,
			  "ring %d active with no faults", i);
	}
	return (res);
}

void rrp_deliver_fn (
	void *context,
	const void *msg,
	unsigned int msg_len)
{
	unsigned int token_seqid;
	unsigned int token_is;

	struct deliver_fn_context *deliver_fn_context = (struct deliver_fn_context *)context;
	struct totemrrp_instance *rrp_instance = deliver_fn_context->instance;
	const struct message_header *hdr = msg;
	struct message_header tmp_msg, activate_msg;

	memset(&tmp_msg, 0, sizeof(struct message_header));
	memset(&activate_msg, 0, sizeof(struct message_header));

	rrp_instance->totemrrp_token_seqid_get (
		msg,
		&token_seqid,
		&token_is);

	if (hdr->type == MESSAGE_TYPE_RING_TEST_ACTIVE) {
		log_printf (
			rrp_instance->totemrrp_log_level_debug,
			"received message requesting test of ring now active");

		if (hdr->endian_detector != ENDIAN_LOCAL) {
			test_active_msg_endian_convert(hdr, &tmp_msg);
			hdr = &tmp_msg;
		}

		if (hdr->nodeid_activator == rrp_instance->my_nodeid) {
			/*
			 * Send an activate message
			 */
			activate_msg.type = MESSAGE_TYPE_RING_TEST_ACTIVATE;
			activate_msg.endian_detector = ENDIAN_LOCAL;
			activate_msg.ring_number = hdr->ring_number;
			activate_msg.nodeid_activator = rrp_instance->my_nodeid;
			totemnet_token_send (
				rrp_instance->net_handles[deliver_fn_context->iface_no],
				&activate_msg, sizeof (struct message_header));
		} else {
			/*
			 * Send a ring test message
			 */
			totemnet_token_send (
				rrp_instance->net_handles[deliver_fn_context->iface_no],
				msg, msg_len);
		}
	} else 
	if (hdr->type == MESSAGE_TYPE_RING_TEST_ACTIVATE) {

		if (hdr->endian_detector != ENDIAN_LOCAL) {
			test_active_msg_endian_convert(hdr, &tmp_msg);
			hdr = &tmp_msg;
		}

		log_printf (
			rrp_instance->totemrrp_log_level_debug,
			"Received ring test activate message for ring %d sent by node %u",
			hdr->ring_number,
			hdr->nodeid_activator);

		if (rrp_instance->stats.faulty[deliver_fn_context->iface_no]) {
			log_printf (rrp_instance->totemrrp_log_level_notice,
			    "Automatically recovered ring %d", hdr->ring_number);
		}

		totemrrp_ring_reenable (rrp_instance, deliver_fn_context->iface_no);
		if (hdr->nodeid_activator != rrp_instance->my_nodeid) {
			totemnet_token_send (
				rrp_instance->net_handles[deliver_fn_context->iface_no],
				msg, msg_len);
		}
	} else 
	if (token_is) {
		/*
		 * Deliver to the token receiver for this rrp algorithm
		 */
		rrp_instance->rrp_algo->token_recv (
			rrp_instance,
			deliver_fn_context->iface_no,
			deliver_fn_context->context,
			msg,
			msg_len,
			token_seqid);
	} else {
		/*
		 * Deliver to the mcast receiver for this rrp algorithm
		 */
		rrp_instance->rrp_algo->mcast_recv (
			rrp_instance,
			deliver_fn_context->iface_no,
			deliver_fn_context->context,
			msg,
			msg_len);
	}
}

void rrp_iface_change_fn (
	void *context,
	const struct totem_ip_address *iface_addr)
{
	struct deliver_fn_context *deliver_fn_context = (struct deliver_fn_context *)context;

	deliver_fn_context->instance->my_nodeid = iface_addr->nodeid;
	deliver_fn_context->instance->totemrrp_iface_change_fn (
		deliver_fn_context->context,
		iface_addr,
		deliver_fn_context->iface_no);
}

int totemrrp_finalize (
	void *rrp_context)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	int i;

	for (i = 0; i < instance->interface_count; i++) {
		totemnet_finalize (instance->net_handles[i]);
	}
	free (instance->net_handles);
	free (instance);
	return (0);
}

static void rrp_target_set_completed (void *context)
{
	struct deliver_fn_context *deliver_fn_context = (struct deliver_fn_context *)context;

	deliver_fn_context->instance->totemrrp_target_set_completed (deliver_fn_context->context);
}

/*
 * Totem Redundant Ring interface
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
int totemrrp_initialize (
	qb_loop_t *poll_handle,
	void **rrp_context,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_addr,
		unsigned int iface_no),

	void (*token_seqid_get) (
		const void *msg,
		unsigned int *seqid,
		unsigned int *token_is),

	unsigned int (*msgs_missing) (void),

	void (*target_set_completed) (void *context))
{
	struct totemrrp_instance *instance;
	unsigned int res;
	int i;

	instance = malloc (sizeof (struct totemrrp_instance));
	if (instance == 0) {
		return (-1);
	}

	totemrrp_instance_initialize (instance);

	instance->totem_config = totem_config;
	stats->rrp = &instance->stats;
	instance->stats.interface_count = totem_config->interface_count;
	instance->stats.faulty = calloc(instance->stats.interface_count, sizeof(uint8_t));

	res = totemrrp_algorithm_set (
		instance->totem_config,
		instance);
	if (res == -1) {
		goto error_destroy;
	}

	/*
	* Configure logging
	*/
	instance->totemrrp_log_level_security = totem_config->totem_logging_configuration.log_level_security;
	instance->totemrrp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemrrp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemrrp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemrrp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemrrp_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemrrp_log_printf = totem_config->totem_logging_configuration.log_printf;

	instance->interfaces = totem_config->interfaces;

	instance->poll_handle = poll_handle;

	instance->totemrrp_deliver_fn = deliver_fn;

	instance->totemrrp_iface_change_fn = iface_change_fn;

	instance->totemrrp_token_seqid_get = token_seqid_get;

	instance->totemrrp_target_set_completed = target_set_completed;

	instance->totemrrp_msgs_missing = msgs_missing;

	instance->interface_count = totem_config->interface_count;

	instance->net_handles = malloc (sizeof (void *) * totem_config->interface_count);

	instance->context = context;

	instance->poll_handle = poll_handle;


	for (i = 0; i < totem_config->interface_count; i++) {
		struct deliver_fn_context *deliver_fn_context;

		deliver_fn_context = malloc (sizeof (struct deliver_fn_context));
		assert (deliver_fn_context);
		deliver_fn_context->instance = instance;
		deliver_fn_context->context = context;
		deliver_fn_context->iface_no = i;
		instance->deliver_fn_context[i] = (void *)deliver_fn_context;

		res = totemnet_initialize (
			poll_handle,
			&instance->net_handles[i],
			totem_config,
			stats,
			i,
			(void *)deliver_fn_context,
			rrp_deliver_fn,
			rrp_iface_change_fn,
			rrp_target_set_completed);

		if (res == -1) {
			goto error_destroy;
		}

		totemnet_net_mtu_adjust (instance->net_handles[i], totem_config);
	}

	*rrp_context = instance;
	return (0);

error_destroy:
	free (instance);
	return (res);
}

void *totemrrp_buffer_alloc (void *rrp_context)
{
	struct totemrrp_instance *instance = rrp_context;
	assert (instance != NULL);
	return totemnet_buffer_alloc (instance->net_handles[0]);
}

void totemrrp_buffer_release (void *rrp_context, void *ptr)
{
	struct totemrrp_instance *instance = rrp_context;
	assert (instance != NULL);
	totemnet_buffer_release (instance->net_handles[0], ptr);
}

int totemrrp_processor_count_set (
	void *rrp_context,
	unsigned int processor_count)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	instance->rrp_algo->processor_count_set (instance, processor_count);

	instance->processor_count = processor_count;

	return (0);
}

int totemrrp_token_target_set (
	void *rrp_context,
	struct totem_ip_address *addr,
	unsigned int iface_no)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	instance->rrp_algo->token_target_set (instance, addr, iface_no);

	return (0);
}
int totemrrp_recv_flush (void *rrp_context)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;

	instance->rrp_algo->recv_flush (instance);

	return (0);
}

int totemrrp_send_flush (void *rrp_context)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	instance->rrp_algo->send_flush (instance);

	return (0);
}

int totemrrp_token_send (
	void *rrp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	instance->rrp_algo->token_send (instance, msg, msg_len);

	return (0);
}

int totemrrp_mcast_flush_send (
	void *rrp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	int res = 0;

// TODO this needs to return the result
	instance->rrp_algo->mcast_flush_send (instance, msg, msg_len);

	return (res);
}

int totemrrp_mcast_noflush_send (
	void *rrp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	/*
	 * merge detects go out through mcast_flush_send so it is safe to
	 * flush these messages if we are only one processor.  This avoids
	 * an encryption/hmac and decryption/hmac
	 */
	if (instance->processor_count > 1) {

// TODO this needs to return the result
		instance->rrp_algo->mcast_noflush_send (instance, msg, msg_len);
	}

	return (0);
}

int totemrrp_iface_check (void *rrp_context)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;

	instance->rrp_algo->iface_check (instance);

	return (0);
}

int totemrrp_ifaces_get (
	void *rrp_context,
	char ***status,
	unsigned int *iface_count)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	*status = instance->status;

	if (iface_count) {
		*iface_count = instance->interface_count;
	}

	return (0);
}

int totemrrp_crypto_set (
	void *rrp_context,
	const char *cipher_type,
	const char *hash_type)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	int res;

	res = totemnet_crypto_set(instance->net_handles[0], cipher_type, hash_type);

	return (res);
}


/*
 * iface_no indicates the interface number [0, ..., interface_count-1] of the
 * specific ring which will be reenabled. We specify iface_no == interface_count
 * means reenabling all the rings.
 */
int totemrrp_ring_reenable (
        void *rrp_context,
	unsigned int iface_no)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	int res = 0;
	unsigned int i;

	instance->rrp_algo->ring_reenable (instance, iface_no);

	if (iface_no == instance->interface_count) {
		for (i = 0; i < instance->interface_count; i++) {
			snprintf (instance->status[i], STATUS_STR_LEN,
				"ring %d active with no faults", i);
		}
	} else {
		snprintf (instance->status[iface_no], STATUS_STR_LEN,
			"ring %d active with no faults", iface_no);
	}

	return (res);
}

extern int totemrrp_mcast_recv_empty (
	void *rrp_context)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	int res;

	res = instance->rrp_algo->mcast_recv_empty (instance);

	return (res);
}

int totemrrp_member_add (
        void *rrp_context,
        const struct totem_ip_address *member,
	int iface_no)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	int res;

	res = instance->rrp_algo->member_add (instance, member, iface_no);

	return (res);
}

int totemrrp_member_remove (
        void *rrp_context,
        const struct totem_ip_address *member,
	int iface_no)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;
	int res;

	res = instance->rrp_algo->member_remove (instance, member, iface_no);

	return (res);
}

void totemrrp_membership_changed (
        void *rrp_context,
	enum totem_configuration_type configuration_type,
	const struct srp_addr *member_list, size_t member_list_entries,
	const struct srp_addr *left_list, size_t left_list_entries,
	const struct srp_addr *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	struct totemrrp_instance *instance = (struct totemrrp_instance *)rrp_context;

	instance->rrp_algo->membership_changed (instance,
	    configuration_type,
	    member_list, member_list_entries,
	    left_list, left_list_entries,
	    joined_list, joined_list_entries,
	    ring_id);
}
