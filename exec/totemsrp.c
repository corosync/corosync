/*
 * Copyright (c) 2003-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
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

/*
 * The first version of this code was based upon Yair Amir's PhD thesis:
 *	http://www.cs.jhu.edu/~yairamir/phd.ps) (ch4,5). 
 *
 * The current version of totemsrp implements the Totem protocol specified in:
 * 	http://citeseer.ist.psu.edu/amir95totem.html
 *
 * The deviations from the above published protocols are:
 * - encryption of message contents with SOBER128
 * - authentication of meessage contents with SHA1/HMAC
 * - token hold mode where token doesn't rotate on unused ring - reduces cpu
 *   usage on 1.6ghz xeon from 35% to less then .1 % as measured by top
 */

#ifndef OPENAIS_BSD
#include <alloca.h>
#endif
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
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>

#include "aispoll.h"
#include "totemsrp.h"
#include "totemrrp.h"
#include "wthread.h"
#include "../include/queue.h"
#include "../include/sq.h"
#include "../include/list.h"
#include "../include/hdb.h"
#include "swab.h"

#include "crypto.h"

#define LOCALHOST_IP				inet_addr("127.0.0.1")
#define QUEUE_RTR_ITEMS_SIZE_MAX		256 /* allow 256 retransmit items */
#define RETRANS_MESSAGE_QUEUE_SIZE_MAX		500 /* allow 500 messages to be queued */
#define RECEIVED_MESSAGE_QUEUE_SIZE_MAX		500 /* allow 500 messages to be queued */
#define MAXIOVS					5	
#define RETRANSMIT_ENTRIES_MAX			30

/*
 * Rollover handling:
 * SEQNO_START_MSG is the starting sequence number after a new configuration
 *	This should remain zero, unless testing overflow in which case
 *	0x7ffff000 and 0xfffff000 are good starting values.
 *
 * SEQNO_START_TOKEN is the starting sequence number after a new configuration
 *	for a token.  This should remain zero, unless testing overflow in which
 *	case 07fffff00 or 0xffffff00 are good starting values.
 *
 * SEQNO_START_MSG is the starting sequence number after a new configuration
 *	This should remain zero, unless testing overflow in which case
 *	0x7ffff000 and 0xfffff000 are good values to start with
 */
#define SEQNO_START_MSG 0x0
#define SEQNO_START_TOKEN 0x0

/*
 * These can be used ot test different rollover points
 * #define SEQNO_START_MSG 0xfffffe00 
 * #define SEQNO_START_TOKEN 0xfffffe00
 */

/*
 * These can be used to test the error recovery algorithms
 * #define TEST_DROP_ORF_TOKEN_PERCENTAGE 30
 * #define TEST_DROP_COMMIT_TOKEN_PERCENTAGE 30
 * #define TEST_DROP_MCAST_PERCENTAGE 50
 * #define TEST_RECOVERY_MSG_COUNT 300
 */

/*
 * we compare incoming messages to determine if their endian is
 * different - if so convert them
 *
 * do not change
 */
#define ENDIAN_LOCAL					 0xff22

enum message_type {
	MESSAGE_TYPE_ORF_TOKEN = 0,		/* Ordering, Reliability, Flow (ORF) control Token */
	MESSAGE_TYPE_MCAST = 1,			/* ring ordered multicast message */
	MESSAGE_TYPE_MEMB_MERGE_DETECT = 2,	/* merge rings if there are available rings */
	MESSAGE_TYPE_MEMB_JOIN = 3, 		/* membership join message */
	MESSAGE_TYPE_MEMB_COMMIT_TOKEN = 4,	/* membership commit token */
	MESSAGE_TYPE_TOKEN_HOLD_CANCEL = 5,	/* cancel the holding of the token */
};

/* 
 * New membership algorithm local variables
 */
struct srp_addr {
	struct totem_ip_address addr[INTERFACE_MAX];
};


struct consensus_list_item {
	struct srp_addr addr;
	int set;
};


struct token_callback_instance {
	struct list_head list;
	int (*callback_fn) (enum totem_callback_token_type type, void *);
	enum totem_callback_token_type callback_type;
	int delete;
	void *data;
};


struct totemsrp_socket {
	int mcast;
	int token;
};

struct message_header {
	char type;
	char encapsulated;
	unsigned short endian_detector;
	unsigned int nodeid;
} __attribute__((packed));


struct mcast {
	struct message_header header;
	struct srp_addr system_from;
	unsigned int seq;
	int this_seqno;
	struct memb_ring_id ring_id;
	unsigned int node_id;
	int guarantee;
} __attribute__((packed));


/*
 * MTU - multicast message header - IP header - UDP header
 *
 * On lossy switches, making use of the DF UDP flag can lead to loss of
 * forward progress.  So the packets must be fragmented by a higher layer
 *
 * This layer can only handle packets of MTU size.
 */
#define FRAGMENT_SIZE (FRAME_SIZE_MAX - sizeof (struct mcast) - 20 - 8)

struct rtr_item  {
	struct memb_ring_id ring_id;
	unsigned int seq;
}__attribute__((packed));


struct orf_token {
	struct message_header header;
	unsigned int seq;
	unsigned int token_seq;
	unsigned int aru;
	unsigned int aru_addr;
	struct memb_ring_id ring_id; 
	unsigned int backlog;
	unsigned int fcc;
	int retrans_flg;
	int rtr_list_entries;
	struct rtr_item rtr_list[0];
}__attribute__((packed));


struct memb_join {
	struct message_header header;
	struct srp_addr system_from;
	unsigned int proc_list_entries;
	unsigned int failed_list_entries;
	unsigned long long ring_seq;
	unsigned char end_of_memb_join[0];
/*
 * These parts of the data structure are dynamic:
 * struct srp_addr proc_list[];
 * struct srp_addr failed_list[];
 */
} __attribute__((packed));

 
struct memb_merge_detect {
	struct message_header header;
	struct srp_addr system_from;
	struct memb_ring_id ring_id;
} __attribute__((packed));


struct token_hold_cancel {
	struct message_header header;
	struct memb_ring_id ring_id;
} __attribute__((packed));


struct memb_commit_token_memb_entry {
	struct memb_ring_id ring_id;
	unsigned int aru;
	unsigned int high_delivered;
	unsigned int received_flg;
}__attribute__((packed));


struct memb_commit_token {
	struct message_header header;
	unsigned int token_seq;
	struct memb_ring_id ring_id;
	unsigned int retrans_flg;
	int memb_index;
	int addr_entries;
	unsigned char end_of_commit_token[0];
/*
 * These parts of the data structure are dynamic:
 *
 * 	struct srp_addr addr[PROCESSOR_COUNT_MAX];
 *	struct memb_commit_token_memb_entry memb_list[PROCESSOR_COUNT_MAX];
 */
}__attribute__((packed));

struct message_item {
	struct mcast *mcast;
	struct iovec iovec[MAXIOVS];
	int iov_len;
};

struct sort_queue_item {
	struct iovec iovec[MAXIOVS];
	int iov_len;
};

struct orf_token_mcast_thread_state {
	char iobuf[9000];
	prng_state prng_state;
};

enum memb_state {
	MEMB_STATE_OPERATIONAL = 1,
	MEMB_STATE_GATHER = 2,
	MEMB_STATE_COMMIT = 3,
	MEMB_STATE_RECOVERY = 4
};

struct totemsrp_instance {
	int iface_changes;

	/*
	 * Flow control mcasts and remcasts on last and current orf_token
	 */
	int fcc_remcast_last;

	int fcc_mcast_last;

	int fcc_remcast_current;

	struct consensus_list_item consensus_list[PROCESSOR_COUNT_MAX];

	int consensus_list_entries;

	struct srp_addr my_id;

	struct srp_addr my_proc_list[PROCESSOR_COUNT_MAX];

	struct srp_addr my_failed_list[PROCESSOR_COUNT_MAX];

	struct srp_addr my_new_memb_list[PROCESSOR_COUNT_MAX];

	struct srp_addr my_trans_memb_list[PROCESSOR_COUNT_MAX];

	struct srp_addr my_memb_list[PROCESSOR_COUNT_MAX];

	struct srp_addr my_deliver_memb_list[PROCESSOR_COUNT_MAX];

	int my_proc_list_entries;

	int my_failed_list_entries;

	int my_new_memb_entries;

	int my_trans_memb_entries;

	int my_memb_entries;

	int my_deliver_memb_entries;

	int my_nodeid_lookup_entries;

	struct memb_ring_id my_ring_id;

	struct memb_ring_id my_old_ring_id;

	int my_aru_count;

	int my_merge_detect_timeout_outstanding;

	unsigned int my_last_aru;

	int my_seq_unchanged;

	int my_received_flg;

	unsigned int my_high_seq_received;

	unsigned int my_install_seq;

	int my_rotation_counter;

	int my_set_retrans_flg;

	int my_retrans_flg_count;

	unsigned int my_high_ring_delivered;
	
	int heartbeat_timeout;

	/*
	 * Queues used to order, deliver, and recover messages
	 */
	struct queue new_message_queue;

	struct queue retrans_message_queue;

	struct sq regular_sort_queue;

	struct sq recovery_sort_queue;

	/*
	 * Received up to and including
	 */
	unsigned int my_aru;

	unsigned int my_high_delivered;

	struct list_head token_callback_received_listhead;

	struct list_head token_callback_sent_listhead;

	char *orf_token_retransmit; // sizeof (struct orf_token) + sizeof (struct rtr_item) * RETRANSMIT_ENTRIES_MAX];

	int orf_token_retransmit_size;

	unsigned int my_token_seq;

	unsigned int my_commit_token_seq;

	/*
	 * Timers
	 */
	poll_timer_handle timer_orf_token_timeout;

	poll_timer_handle timer_orf_token_retransmit_timeout;

	poll_timer_handle timer_orf_token_hold_retransmit_timeout;

	poll_timer_handle timer_merge_detect_timeout;

	poll_timer_handle memb_timer_state_gather_join_timeout;

	poll_timer_handle memb_timer_state_gather_consensus_timeout;

	poll_timer_handle memb_timer_state_commit_timeout;

	poll_timer_handle timer_heartbeat_timeout;

	/*
	 * Function and data used to log messages
	 */
	int totemsrp_log_level_security;

	int totemsrp_log_level_error;

	int totemsrp_log_level_warning;

	int totemsrp_log_level_notice;

	int totemsrp_log_level_debug;

	void (*totemsrp_log_printf) (char *file, int line, int level, char *format, ...) __attribute__((format(printf, 4, 5)));

	enum memb_state memb_state;

//TODO	struct srp_addr next_memb;

	char iov_buffer[FRAME_SIZE_MAX];

	struct iovec totemsrp_iov_recv;

	poll_handle totemsrp_poll_handle;

	/*
	 * Function called when new message received
	 */
	int (*totemsrp_recv) (char *group, struct iovec *iovec, int iov_len);

	struct totem_ip_address mcast_address;

	void (*totemsrp_deliver_fn) (
		unsigned int nodeid,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required);

	void (*totemsrp_confchg_fn) (
		enum totem_configuration_type configuration_type,
		unsigned int *member_list, int member_list_entries,
		unsigned int *left_list, int left_list_entries,
		unsigned int *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);

	int global_seqno;

	int my_token_held;

	unsigned long long token_ring_id_seq;

	unsigned int last_released;

	unsigned int set_aru;

	int old_ring_state_saved;

	int old_ring_state_aru;

	unsigned int old_ring_state_high_seq_received;

	int ring_saved;

	unsigned int my_last_seq;

	struct timeval tv_old;

	totemrrp_handle totemrrp_handle;

	struct totem_config *totem_config;

	unsigned int use_heartbeat;

	unsigned int my_trc;

	unsigned int my_pbl;

	unsigned int my_cbl;
};

struct message_handlers {
	int count;
	int (*handler_functions[6]) (
		struct totemsrp_instance *instance,
		void *msg,
		int msg_len,
		int endian_conversion_needed);
};

/*
 * forward decls
 */
static int message_handler_orf_token (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_mcast (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_memb_merge_detect (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_memb_join (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_memb_commit_token (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_token_hold_cancel (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static void memb_ring_id_create_or_load (struct totemsrp_instance *, struct memb_ring_id *);

static void token_callbacks_execute (struct totemsrp_instance *instance, enum totem_callback_token_type type);
static void memb_state_gather_enter (struct totemsrp_instance *instance);
static void messages_deliver_to_app (struct totemsrp_instance *instance, int skip, unsigned int end_point);
static int orf_token_mcast (struct totemsrp_instance *instance, struct orf_token *oken,
	int fcc_mcasts_allowed);
static void messages_free (struct totemsrp_instance *instance, unsigned int token_aru);

static void memb_ring_id_store (struct totemsrp_instance *instance, struct memb_commit_token *commit_token);
static void memb_state_commit_token_update (struct totemsrp_instance *instance, struct memb_commit_token *commit_token);
static int memb_state_commit_token_send (struct totemsrp_instance *instance, struct memb_commit_token *memb_commit_token);
static void memb_state_commit_token_create (struct totemsrp_instance *instance, struct memb_commit_token *commit_token);
static int token_hold_cancel_send (struct totemsrp_instance *instance);
static void orf_token_endian_convert (struct orf_token *in, struct orf_token *out);
static void memb_commit_token_endian_convert (struct memb_commit_token *in, struct memb_commit_token *out);
static void memb_join_endian_convert (struct memb_join *in, struct memb_join *out);
static void mcast_endian_convert (struct mcast *in, struct mcast *out);
static void memb_merge_detect_endian_convert (
	struct memb_merge_detect *in,
	struct memb_merge_detect *out);
static void srp_addr_copy_endian_convert (struct srp_addr *out, struct srp_addr *in);
static void timer_function_orf_token_timeout (void *data);
static void timer_function_heartbeat_timeout (void *data);
static void timer_function_token_retransmit_timeout (void *data);
static void timer_function_token_hold_retransmit_timeout (void *data);
static void timer_function_merge_detect_timeout (void *data);

void main_deliver_fn (
	void *context,
	void *msg,
	int msg_len);

void main_iface_change_fn (
	void *context,
	struct totem_ip_address *iface_address,
	unsigned int iface_no);

/*
 * All instances in one database
 */
static struct hdb_handle_database totemsrp_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0,
	.mutex		= PTHREAD_MUTEX_INITIALIZER
};
struct message_handlers totemsrp_message_handlers = {
	6,
	{
		message_handler_orf_token,
		message_handler_mcast,
		message_handler_memb_merge_detect,
		message_handler_memb_join,
		message_handler_memb_commit_token,
		message_handler_token_hold_cancel
	}
};

#define log_printf(level, format, args...) \
    instance->totemsrp_log_printf (__FILE__, __LINE__, level, format, ##args)

void totemsrp_instance_initialize (struct totemsrp_instance *instance)
{
	memset (instance, 0, sizeof (struct totemsrp_instance));

	list_init (&instance->token_callback_received_listhead);

	list_init (&instance->token_callback_sent_listhead);

	instance->my_received_flg = 0;

	instance->my_token_seq = SEQNO_START_TOKEN - 1;

	instance->my_commit_token_seq = SEQNO_START_TOKEN - 1;

	instance->orf_token_retransmit = malloc (15000);

	instance->memb_state = MEMB_STATE_OPERATIONAL;

	instance->set_aru = -1;

	instance->my_aru = SEQNO_START_MSG;

	instance->my_high_seq_received = SEQNO_START_MSG;

	instance->my_high_delivered = SEQNO_START_MSG;
}

void main_token_seqid_get (
	void *msg,
	unsigned int *seqid,
	unsigned int *token_is)
{
	struct orf_token *token = (struct orf_token *)msg;

	*seqid = 0;
	*token_is = 0;
	if (token->header.type == MESSAGE_TYPE_ORF_TOKEN) {
		*seqid = token->token_seq;
		*token_is = 1;
	}
}

unsigned int main_msgs_missing (void)
{
// TODO
	return (0);
}

/*
 * Exported interfaces
 */
int totemsrp_initialize (
	poll_handle poll_handle,
	totemsrp_handle *handle,
	struct totem_config *totem_config,

	void (*deliver_fn) (
		unsigned int nodeid,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required),

	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		unsigned int *member_list, int member_list_entries,
		unsigned int *left_list, int left_list_entries,
		unsigned int *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id))
{
	struct totemsrp_instance *instance;
	unsigned int res;

	res = hdb_handle_create (&totemsrp_instance_database,
		sizeof (struct totemsrp_instance), handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&totemsrp_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}

	totemsrp_instance_initialize (instance);

	instance->totem_config = totem_config;

	/*
	 * Configure logging
	 */
	instance->totemsrp_log_level_security = totem_config->totem_logging_configuration.log_level_security;
	instance->totemsrp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemsrp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemsrp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemsrp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemsrp_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	 * Initialize local variables for totemsrp
	 */
	totemip_copy (&instance->mcast_address, &totem_config->interfaces[0].mcast_addr);

	memset (instance->iov_buffer, 0, FRAME_SIZE_MAX);

	/*
	 * Display totem configuration
	 */
	log_printf (instance->totemsrp_log_level_notice,
		"Token Timeout (%d ms) retransmit timeout (%d ms)\n",
		totem_config->token_timeout, totem_config->token_retransmit_timeout);
	log_printf (instance->totemsrp_log_level_notice,
		"token hold (%d ms) retransmits before loss (%d retrans)\n",
		totem_config->token_hold_timeout, totem_config->token_retransmits_before_loss_const);
	log_printf (instance->totemsrp_log_level_notice,
		"join (%d ms) send_join (%d ms) consensus (%d ms) merge (%d ms)\n",
		totem_config->join_timeout,
		totem_config->send_join_timeout,
		totem_config->consensus_timeout,

		totem_config->merge_timeout);
	log_printf (instance->totemsrp_log_level_notice,
		"downcheck (%d ms) fail to recv const (%d msgs)\n",
		totem_config->downcheck_timeout, totem_config->fail_to_recv_const);
	log_printf (instance->totemsrp_log_level_notice,
		"seqno unchanged const (%d rotations) Maximum network MTU %d\n", totem_config->seqno_unchanged_const, totem_config->net_mtu);

	log_printf (instance->totemsrp_log_level_notice,
		"window size per rotation (%d messages) maximum messages per rotation (%d messages)\n",
		totem_config->window_size, totem_config->max_messages);

	log_printf (instance->totemsrp_log_level_notice,
		"send threads (%d threads)\n", totem_config->threads);
	log_printf (instance->totemsrp_log_level_notice,
		"RRP token expired timeout (%d ms)\n",
		totem_config->rrp_token_expired_timeout);
	log_printf (instance->totemsrp_log_level_notice,
		"RRP token problem counter (%d ms)\n",
		totem_config->rrp_problem_count_timeout);
	log_printf (instance->totemsrp_log_level_notice,
		"RRP threshold (%d problem count)\n",
		totem_config->rrp_problem_count_threshold);
	log_printf (instance->totemsrp_log_level_notice,
		"RRP mode set to %s.\n", instance->totem_config->rrp_mode);

	log_printf (instance->totemsrp_log_level_notice,
		"heartbeat_failures_allowed (%d)\n", totem_config->heartbeat_failures_allowed);
	log_printf (instance->totemsrp_log_level_notice,
		"max_network_delay (%d ms)\n", totem_config->max_network_delay);


	queue_init (&instance->retrans_message_queue, RETRANS_MESSAGE_QUEUE_SIZE_MAX,
		sizeof (struct message_item));

	sq_init (&instance->regular_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	sq_init (&instance->recovery_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	instance->totemsrp_poll_handle = poll_handle;

	instance->totemsrp_deliver_fn = deliver_fn;

	instance->totemsrp_confchg_fn = confchg_fn;
	instance->use_heartbeat = 1;

	if ( totem_config->heartbeat_failures_allowed == 0 ) {
		log_printf (instance->totemsrp_log_level_notice,
			"HeartBeat is Disabled. To enable set heartbeat_failures_allowed > 0\n");
		instance->use_heartbeat = 0;
	}

	if (instance->use_heartbeat) {
		instance->heartbeat_timeout 
			= (totem_config->heartbeat_failures_allowed) * totem_config->token_retransmit_timeout 
				+ totem_config->max_network_delay;

		if (instance->heartbeat_timeout >= totem_config->token_timeout) {
			log_printf (instance->totemsrp_log_level_notice,
				"total heartbeat_timeout (%d ms) is not less than token timeout (%d ms)\n", 
				instance->heartbeat_timeout,
				totem_config->token_timeout);
			log_printf (instance->totemsrp_log_level_notice,
				"heartbeat_timeout = heartbeat_failures_allowed * token_retransmit_timeout + max_network_delay\n");
			log_printf (instance->totemsrp_log_level_notice,
				"heartbeat timeout should be less than the token timeout. HeartBeat is Diabled !!\n");
			instance->use_heartbeat = 0;
		}
		else {
			log_printf (instance->totemsrp_log_level_notice,
                		"total heartbeat_timeout (%d ms)\n", instance->heartbeat_timeout);
		}
	}
	
	totemrrp_initialize (
		poll_handle,
		&instance->totemrrp_handle,
		totem_config,
		instance,
		main_deliver_fn,
		main_iface_change_fn,
		main_token_seqid_get,
		main_msgs_missing);

	/*
	 * Must have net_mtu adjusted by totemrrp_initialize first
	 */
	queue_init (&instance->new_message_queue,
		(MESSAGE_SIZE_MAX / (totem_config->net_mtu - 25) /* for totempg_mcat header */),
		sizeof (struct message_item));

	return (0);

error_destroy:
	hdb_handle_destroy (&totemsrp_instance_database, *handle);

error_exit:
	return (-1);
}

void totemsrp_finalize (
	totemsrp_handle handle)
{
	struct totemsrp_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		return;
	}

	hdb_handle_put (&totemsrp_instance_database, handle);
}

int totemsrp_ifaces_get (
	totemsrp_handle handle,
	unsigned int nodeid,
	struct totem_ip_address *interfaces,
	char ***status,
	unsigned int *iface_count)
{
	struct totemsrp_instance *instance;
	int res;
	unsigned int found = 0;
	unsigned int i;

	res = hdb_handle_get (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	for (i = 0; i < instance->my_memb_entries; i++) {
		if (instance->my_memb_list[i].addr[0].nodeid == nodeid) {
			found = 1;
			break;
		}
	}

	if (found) {
		memcpy (interfaces, &instance->my_memb_list[i],
			sizeof (struct srp_addr));
		*iface_count = instance->totem_config->interface_count;
	} else {
		res = -1;
	}

	totemrrp_ifaces_get (instance->totemrrp_handle, status, NULL);

	hdb_handle_put (&totemsrp_instance_database, handle);
error_exit:
	return (res);
}

int totemsrp_ring_reenable (
        totemsrp_handle handle)
{
	struct totemsrp_instance *instance;
	int res;

	res = hdb_handle_get (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	totemrrp_ring_reenable (instance->totemrrp_handle);

	hdb_handle_put (&totemsrp_instance_database, handle);
error_exit:
	return (res);
}


/*
 * Set operations for use by the membership algorithm
 */
int srp_addr_equal (struct srp_addr *a, struct srp_addr *b)
{
	unsigned int i;
	unsigned int res;

	for (i = 0; i < 1; i++) {
		res = totemip_equal (&a->addr[i], &b->addr[i]);
		if (res == 0) {
			return (0);
		}
	}
	return (1);
}

void srp_addr_copy (struct srp_addr *dest, struct srp_addr *src)
{
	unsigned int i;

	for (i = 0; i < INTERFACE_MAX; i++) {
		totemip_copy (&dest->addr[i], &src->addr[i]);
	}
}

void srp_addr_to_nodeid (
	unsigned int *nodeid_out,
	struct srp_addr *srp_addr_in,
	unsigned int entries)
{
	unsigned int i;

	for (i = 0; i < entries; i++) {
		nodeid_out[i] = srp_addr_in[i].addr[0].nodeid;
	}
}

static void srp_addr_copy_endian_convert (struct srp_addr *out, struct srp_addr *in)
{
	int i;

	for (i = 0; i < INTERFACE_MAX; i++) {
		totemip_copy_endian_convert (&out->addr[i], &in->addr[i]);
	}
}

static void memb_consensus_reset (struct totemsrp_instance *instance)
{
	instance->consensus_list_entries = 0;
}

static void memb_set_subtract (
        struct srp_addr *out_list, int *out_list_entries,
        struct srp_addr *one_list, int one_list_entries,
        struct srp_addr *two_list, int two_list_entries)
{
	int found = 0;
	int i;
	int j;

	*out_list_entries = 0;

	for (i = 0; i < one_list_entries; i++) {
		for (j = 0; j < two_list_entries; j++) {
			if (srp_addr_equal (&one_list[i], &two_list[j])) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			srp_addr_copy (&out_list[*out_list_entries], &one_list[i]);
			*out_list_entries = *out_list_entries + 1;
		}
		found = 0;
	}
}

/*
 * Set consensus for a specific processor
 */
static void memb_consensus_set (
	struct totemsrp_instance *instance,
	struct srp_addr *addr)
{
	int found = 0;
	int i;

	for (i = 0; i < instance->consensus_list_entries; i++) {
		if (srp_addr_equal(addr, &instance->consensus_list[i].addr)) {
			found = 1;
			break; /* found entry */
		}
	}
	srp_addr_copy (&instance->consensus_list[i].addr, addr);
	instance->consensus_list[i].set = 1;
	if (found == 0) {
		instance->consensus_list_entries++;
	}
	return;
}

/*
 * Is consensus set for a specific processor
 */
static int memb_consensus_isset (
	struct totemsrp_instance *instance,
	struct srp_addr *addr)
{
	int i;

	for (i = 0; i < instance->consensus_list_entries; i++) {
		if (srp_addr_equal (addr, &instance->consensus_list[i].addr)) {
			return (instance->consensus_list[i].set);
		}
	}
	return (0);
}

/*
 * Is consensus agreed upon based upon consensus database
 */
static int memb_consensus_agreed (
	struct totemsrp_instance *instance)
{
	struct srp_addr token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;
	int agreed = 1;
	int i;

	memb_set_subtract (token_memb, &token_memb_entries,
		instance->my_proc_list, instance->my_proc_list_entries,
		instance->my_failed_list, instance->my_failed_list_entries);

	for (i = 0; i < token_memb_entries; i++) {
		if (memb_consensus_isset (instance, &token_memb[i]) == 0) {
			agreed = 0;
			break;
		}
	}
	assert (token_memb_entries >= 1);

	return (agreed);
}

static void memb_consensus_notset (
	struct totemsrp_instance *instance,
	struct srp_addr *no_consensus_list,
	int *no_consensus_list_entries,
	struct srp_addr *comparison_list,
	int comparison_list_entries)
{
	int i;

	*no_consensus_list_entries = 0;

	for (i = 0; i < instance->my_proc_list_entries; i++) {
		if (memb_consensus_isset (instance, &instance->my_proc_list[i]) == 0) {
			srp_addr_copy (&no_consensus_list[*no_consensus_list_entries], &instance->my_proc_list[i]);
			*no_consensus_list_entries = *no_consensus_list_entries + 1;
		}
	}
}

/*
 * Is set1 equal to set2 Entries can be in different orders
 */
static int memb_set_equal (
	struct srp_addr *set1, int set1_entries,
	struct srp_addr *set2, int set2_entries)
{
	int i;
	int j;

	int found = 0;

	if (set1_entries != set2_entries) {
		return (0);
	}
	for (i = 0; i < set2_entries; i++) {
		for (j = 0; j < set1_entries; j++) {
			if (srp_addr_equal (&set1[j], &set2[i])) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			return (0);
		}
		found = 0;
	}
	return (1);
}

/*
 * Is subset fully contained in fullset
 */
static int memb_set_subset (
	struct srp_addr *subset, int subset_entries,
	struct srp_addr *fullset, int fullset_entries)
{
	int i;
	int j;
	int found = 0;

	if (subset_entries > fullset_entries) {
		return (0);
	}
	for (i = 0; i < subset_entries; i++) {
		for (j = 0; j < fullset_entries; j++) {
			if (srp_addr_equal (&subset[i], &fullset[j])) {
				found = 1;
			}
		}
		if (found == 0) {
			return (0);
		}
		found = 0;
	}
	return (1);
}
/*
 * merge subset into fullset taking care not to add duplicates
 */
static void memb_set_merge (
	struct srp_addr *subset, int subset_entries,
	struct srp_addr *fullset, int *fullset_entries)
{
	int found = 0;
	int i;
	int j;

	for (i = 0; i < subset_entries; i++) {
		for (j = 0; j < *fullset_entries; j++) {
			if (srp_addr_equal (&fullset[j], &subset[i])) {
				found = 1;
				break;
			}	
		}
		if (found == 0) {
			srp_addr_copy (&fullset[j], &subset[i]);
			*fullset_entries = *fullset_entries + 1;
		}
		found = 0;
	}
	return;
}

static void memb_set_and (
        struct srp_addr *set1, int set1_entries,
        struct srp_addr *set2, int set2_entries,
        struct srp_addr *and, int *and_entries)
{
	int i;
	int j;
	int found = 0;

	*and_entries = 0;

	for (i = 0; i < set2_entries; i++) {
		for (j = 0; j < set1_entries; j++) {
			if (srp_addr_equal (&set1[j], &set2[i])) {
				found = 1;
				break;
			}
		}
		if (found) {
			srp_addr_copy (&and[*and_entries], &set1[j]);
			*and_entries = *and_entries + 1;
		}
		found = 0;
	}
	return;
}

#ifdef CODE_COVERAGE
static void memb_set_print (
	char *string,
        struct srp_addr *list,
	int list_entries)
{
	int i;
	int j;
	printf ("List '%s' contains %d entries:\n", string, list_entries);

	for (i = 0; i < list_entries; i++) {
		for (j = 0; j < INTERFACE_MAX; j++) {
			printf ("Address %d\n", i);
			printf ("\tiface %d %s\n", j, totemip_print (&list[i].addr[j]));
			printf ("family %d\n", list[i].addr[j].family);
		}
	}
}
#endif

static void reset_token_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle,
		instance->timer_orf_token_retransmit_timeout);
	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->token_retransmit_timeout,
		(void *)instance,
		timer_function_token_retransmit_timeout,
		&instance->timer_orf_token_retransmit_timeout);

}

static void start_merge_detect_timeout (struct totemsrp_instance *instance)
{
	if (instance->my_merge_detect_timeout_outstanding == 0) {
		poll_timer_add (instance->totemsrp_poll_handle,
			instance->totem_config->merge_timeout,
			(void *)instance,
			timer_function_merge_detect_timeout,
			&instance->timer_merge_detect_timeout);

		instance->my_merge_detect_timeout_outstanding = 1;
	}
}

static void cancel_merge_detect_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_merge_detect_timeout);
	instance->my_merge_detect_timeout_outstanding = 0;
}

/*
 * ring_state_* is used to save and restore the sort queue
 * state when a recovery operation fails (and enters gather)
 */
static void old_ring_state_save (struct totemsrp_instance *instance)
{
	if (instance->old_ring_state_saved == 0) {
		instance->old_ring_state_saved = 1;
		instance->old_ring_state_aru = instance->my_aru;
		instance->old_ring_state_high_seq_received = instance->my_high_seq_received;
		log_printf (instance->totemsrp_log_level_notice,
			"Saving state aru %x high seq received %x\n",
			instance->my_aru, instance->my_high_seq_received);
	}
}

static void ring_save (struct totemsrp_instance *instance)
{
	if (instance->ring_saved == 0) {
		instance->ring_saved = 1;
		memcpy (&instance->my_old_ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id));
	}
}

static void ring_reset (struct totemsrp_instance *instance)
{
	instance->ring_saved = 0;
}

static void ring_state_restore (struct totemsrp_instance *instance)
{
	if (instance->old_ring_state_saved) {
		totemip_zero_set(&instance->my_ring_id.rep);
		instance->my_aru = instance->old_ring_state_aru;
		instance->my_high_seq_received = instance->old_ring_state_high_seq_received;
		log_printf (instance->totemsrp_log_level_notice,
			"Restoring instance->my_aru %x my high seq received %x\n",
			instance->my_aru, instance->my_high_seq_received);
	}
}

static void old_ring_state_reset (struct totemsrp_instance *instance)
{
	instance->old_ring_state_saved = 0;
}

static void reset_token_timeout (struct totemsrp_instance *instance) {
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_orf_token_timeout);
	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->token_timeout,
		(void *)instance,
		timer_function_orf_token_timeout,
		&instance->timer_orf_token_timeout);
}

static void reset_heartbeat_timeout (struct totemsrp_instance *instance) {
        poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_heartbeat_timeout);
        poll_timer_add (instance->totemsrp_poll_handle,
                instance->heartbeat_timeout,
                (void *)instance,
                timer_function_heartbeat_timeout,
                &instance->timer_heartbeat_timeout);
}


static void cancel_token_timeout (struct totemsrp_instance *instance) {
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_orf_token_timeout);
}

static void cancel_heartbeat_timeout (struct totemsrp_instance *instance) {
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_heartbeat_timeout);
}

static void cancel_token_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_orf_token_retransmit_timeout);
}

static void start_token_hold_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->token_hold_timeout,
		(void *)instance,
		timer_function_token_hold_retransmit_timeout,
		&instance->timer_orf_token_hold_retransmit_timeout);
}

static void cancel_token_hold_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle,
		instance->timer_orf_token_hold_retransmit_timeout);
}

static void memb_state_consensus_timeout_expired (
		struct totemsrp_instance *instance)
{
        struct srp_addr no_consensus_list[PROCESSOR_COUNT_MAX];
	int no_consensus_list_entries;

	if (memb_consensus_agreed (instance)) {
		memb_consensus_reset (instance);

		memb_consensus_set (instance, &instance->my_id);

		reset_token_timeout (instance); // REVIEWED
	} else {
		memb_consensus_notset (
			instance,
			no_consensus_list,
			&no_consensus_list_entries,
			instance->my_proc_list,
			instance->my_proc_list_entries);

		memb_set_merge (no_consensus_list, no_consensus_list_entries,
			instance->my_failed_list, &instance->my_failed_list_entries);
		memb_state_gather_enter (instance);
	}
}

static void memb_join_message_send (struct totemsrp_instance *instance);

static void memb_merge_detect_transmit (struct totemsrp_instance *instance);

/*
 * Timers used for various states of the membership algorithm
 */
static void timer_function_orf_token_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	log_printf (instance->totemsrp_log_level_notice,
		"The token was lost in state %d from timer %p\n", instance->memb_state, data);
	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			totemrrp_iface_check (instance->totemrrp_handle);
			memb_state_gather_enter (instance);
			break;

		case MEMB_STATE_GATHER:
			memb_state_consensus_timeout_expired (instance);
			memb_state_gather_enter (instance);
			break;

		case MEMB_STATE_COMMIT:
			memb_state_gather_enter (instance);
			break;
		
		case MEMB_STATE_RECOVERY:
			ring_state_restore (instance);
			memb_state_gather_enter (instance);
			break;
	}
}

static void timer_function_heartbeat_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;
	log_printf (instance->totemsrp_log_level_notice,
		"HeartBeat Timer expired Invoking token loss mechanism in state %d \n", instance->memb_state);
	timer_function_orf_token_timeout(data);
}

static void memb_timer_function_state_gather (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	switch (instance->memb_state) {
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		assert (0); /* this should never happen */
		break;
	case MEMB_STATE_GATHER:
	case MEMB_STATE_COMMIT:
		memb_join_message_send (instance);

		/*
		 * Restart the join timeout
		`*/
		poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);
	
		poll_timer_add (instance->totemsrp_poll_handle,
			instance->totem_config->join_timeout,
			(void *)instance,
			memb_timer_function_state_gather,
			&instance->memb_timer_state_gather_join_timeout);
		break;
	}
}

static void memb_timer_function_gather_consensus_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;
	memb_state_consensus_timeout_expired (instance);
}

static void deliver_messages_from_recovery_to_regular (struct totemsrp_instance *instance)
{
	unsigned int i;
	struct sort_queue_item *recovery_message_item;
	struct sort_queue_item regular_message_item;
	unsigned int range = 0;
	int res;
	void *ptr;
	struct mcast *mcast;

	log_printf (instance->totemsrp_log_level_debug,
		"recovery to regular %x-%x\n", SEQNO_START_MSG + 1, instance->my_aru);

	range = instance->my_aru - SEQNO_START_MSG;
	/*
	 * Move messages from recovery to regular sort queue
	 */
// todo should i be initialized to 0 or 1 ?
	for (i = 1; i <= range; i++) {
		res = sq_item_get (&instance->recovery_sort_queue,
			i + SEQNO_START_MSG, &ptr);
		if (res != 0) {
			continue;
		}
		recovery_message_item = (struct sort_queue_item *)ptr;

		/*
		 * Convert recovery message into regular message
		 */
		if (recovery_message_item->iov_len > 1) {
			mcast = (struct mcast *)recovery_message_item->iovec[1].iov_base;
			memcpy (&regular_message_item.iovec[0],
				&recovery_message_item->iovec[1],
				sizeof (struct iovec) * recovery_message_item->iov_len);
		} else {
			mcast = (struct mcast *)recovery_message_item->iovec[0].iov_base;
			if (mcast->header.encapsulated == 1) {
				/*
				 * Message is a recovery message encapsulated
				 * in a new ring message
				 */
				regular_message_item.iovec[0].iov_base =
					recovery_message_item->iovec[0].iov_base + sizeof (struct mcast);
				regular_message_item.iovec[0].iov_len =
				recovery_message_item->iovec[0].iov_len - sizeof (struct mcast);
				regular_message_item.iov_len = 1;
				mcast = (struct mcast *)regular_message_item.iovec[0].iov_base;
			} else {
				continue; /* TODO this case shouldn't happen */
				/*
				 * Message is originated on new ring and not
				 * encapsulated
				 */
				regular_message_item.iovec[0].iov_base =
					recovery_message_item->iovec[0].iov_base;
				regular_message_item.iovec[0].iov_len =
				recovery_message_item->iovec[0].iov_len;
			}
		}

		log_printf (instance->totemsrp_log_level_debug,
			"comparing if ring id is for this processors old ring seqno %d\n",
			 mcast->seq);

		/*
		 * Only add this message to the regular sort
		 * queue if it was originated with the same ring
		 * id as the previous ring
		 */
		if (memcmp (&instance->my_old_ring_id, &mcast->ring_id,
			sizeof (struct memb_ring_id)) == 0) {

			regular_message_item.iov_len = recovery_message_item->iov_len;
			res = sq_item_inuse (&instance->regular_sort_queue, mcast->seq);
			if (res == 0) {
				sq_item_add (&instance->regular_sort_queue,
					&regular_message_item, mcast->seq);
				if (sq_lt_compare (instance->old_ring_state_high_seq_received, mcast->seq)) {
					instance->old_ring_state_high_seq_received = mcast->seq;
				}
			}
		} else {
			log_printf (instance->totemsrp_log_level_notice,
				"-not adding msg with seq no %x\n", mcast->seq);
		}
	}
}

/*
 * Change states in the state machine of the membership algorithm
 */
static void memb_state_operational_enter (struct totemsrp_instance *instance)
{
	struct srp_addr joined_list[PROCESSOR_COUNT_MAX];
	int joined_list_entries = 0;
	struct srp_addr left_list[PROCESSOR_COUNT_MAX];
	int left_list_entries = 0;
	unsigned int aru_save;
	unsigned int left_list_totemip[PROCESSOR_COUNT_MAX];
	unsigned int joined_list_totemip[PROCESSOR_COUNT_MAX];
	unsigned int trans_memb_list_totemip[PROCESSOR_COUNT_MAX];
	unsigned int new_memb_list_totemip[PROCESSOR_COUNT_MAX];

	old_ring_state_reset (instance);
	ring_reset (instance);
	deliver_messages_from_recovery_to_regular (instance);

	log_printf (instance->totemsrp_log_level_debug,
		"Delivering to app %x to %x\n",
		instance->my_high_delivered + 1, instance->old_ring_state_high_seq_received);

	aru_save = instance->my_aru;
	instance->my_aru = instance->old_ring_state_aru;

	messages_deliver_to_app (instance, 0, instance->old_ring_state_high_seq_received);

	/*
	 * Calculate joined and left list
	 */
	memb_set_subtract (left_list, &left_list_entries,
		instance->my_memb_list, instance->my_memb_entries,
		instance->my_trans_memb_list, instance->my_trans_memb_entries);

	memb_set_subtract (joined_list, &joined_list_entries,
		instance->my_new_memb_list, instance->my_new_memb_entries,
		instance->my_trans_memb_list, instance->my_trans_memb_entries);

	/*
	 * Install new membership
	 */
	instance->my_memb_entries = instance->my_new_memb_entries;
	memcpy (&instance->my_memb_list, instance->my_new_memb_list,
		sizeof (struct srp_addr) * instance->my_memb_entries);
	instance->last_released = 0;
	instance->my_set_retrans_flg = 0;

	/*
	 * Deliver transitional configuration to application
	 */
	srp_addr_to_nodeid (left_list_totemip, left_list, left_list_entries);
	srp_addr_to_nodeid (trans_memb_list_totemip,
		instance->my_trans_memb_list, instance->my_trans_memb_entries);
	instance->totemsrp_confchg_fn (TOTEM_CONFIGURATION_TRANSITIONAL,
		trans_memb_list_totemip, instance->my_trans_memb_entries,
		left_list_totemip, left_list_entries,
		0, 0, &instance->my_ring_id);
		
// TODO we need to filter to ensure we only deliver those
// messages which are part of instance->my_deliver_memb
	messages_deliver_to_app (instance, 1, instance->old_ring_state_high_seq_received);

	instance->my_aru = aru_save;

	/*
	 * Deliver regular configuration to application
	 */
	srp_addr_to_nodeid (new_memb_list_totemip,
		instance->my_new_memb_list, instance->my_new_memb_entries);
	srp_addr_to_nodeid (joined_list_totemip, joined_list,
		joined_list_entries);
	instance->totemsrp_confchg_fn (TOTEM_CONFIGURATION_REGULAR,
		new_memb_list_totemip, instance->my_new_memb_entries,
		0, 0,
		joined_list_totemip, joined_list_entries, &instance->my_ring_id);

	/*
	 * The recovery sort queue now becomes the regular
	 * sort queue.  It is necessary to copy the state
	 * into the regular sort queue.
	 */
	sq_copy (&instance->regular_sort_queue, &instance->recovery_sort_queue);
	instance->my_last_aru = SEQNO_START_MSG;
	sq_items_release (&instance->regular_sort_queue, SEQNO_START_MSG - 1);

	instance->my_proc_list_entries = instance->my_new_memb_entries;
	memcpy (instance->my_proc_list, instance->my_new_memb_list,
		sizeof (struct srp_addr) * instance->my_memb_entries);

	instance->my_failed_list_entries = 0;
	instance->my_high_delivered = instance->my_aru;
// TODO the recovery messages are leaked

	log_printf (instance->totemsrp_log_level_notice,
		"entering OPERATIONAL state.\n");
	instance->memb_state = MEMB_STATE_OPERATIONAL;

	instance->my_received_flg = 0;

	return;
}

static void memb_state_gather_enter (struct totemsrp_instance *instance)
{
	instance->my_commit_token_seq = SEQNO_START_TOKEN - 1;

	memb_set_merge (
		&instance->my_id, 1,
		instance->my_proc_list, &instance->my_proc_list_entries);
// AAA
	assert (srp_addr_equal (&instance->my_proc_list[0], &instance->my_proc_list[1]) == 0);

	memb_join_message_send (instance);

	/*
	 * Restart the join timeout
	 */
	poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);

	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->join_timeout,
		(void *)instance,
		memb_timer_function_state_gather,
		&instance->memb_timer_state_gather_join_timeout);

	/*
	 * Restart the consensus timeout
	 */
	poll_timer_delete (instance->totemsrp_poll_handle,
		instance->memb_timer_state_gather_consensus_timeout);

	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->consensus_timeout,
		(void *)instance,
		memb_timer_function_gather_consensus_timeout,
		&instance->memb_timer_state_gather_consensus_timeout);

	/*
	 * Cancel the token loss and token retransmission timeouts
	 */
	cancel_token_retransmit_timeout (instance); // REVIEWED
	cancel_token_timeout (instance); // REVIEWED
	cancel_merge_detect_timeout (instance);

	memb_consensus_reset (instance);

	memb_consensus_set (instance, &instance->my_id);

	log_printf (instance->totemsrp_log_level_notice,
		"entering GATHER state.\n");

	instance->memb_state = MEMB_STATE_GATHER;

	return;
}

static void timer_function_token_retransmit_timeout (void *data);

static void memb_state_commit_enter (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	ring_save (instance);

	old_ring_state_save (instance); 

// ABC
	memb_state_commit_token_update (instance, commit_token);

	memb_state_commit_token_send (instance, commit_token);

	memb_ring_id_store (instance, commit_token);

	poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);

	instance->memb_timer_state_gather_join_timeout = 0;

	poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_consensus_timeout);

	instance->memb_timer_state_gather_consensus_timeout = 0;

	reset_token_timeout (instance); // REVIEWED
	reset_token_retransmit_timeout (instance); // REVIEWED

	log_printf (instance->totemsrp_log_level_notice,
		"entering COMMIT state.\n");

	instance->memb_state = MEMB_STATE_COMMIT;

	instance->my_commit_token_seq = SEQNO_START_TOKEN - 1;

	/*
	 * reset all flow control variables since we are starting a new ring
	 */
	instance->my_trc = 0;
	instance->my_pbl = 0;
	instance->my_cbl = 0;
	return;
}

static void memb_state_recovery_enter (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	int i;
	int local_received_flg = 1;
	unsigned int low_ring_aru;
	unsigned int range = 0;
	unsigned int messages_originated = 0;
	char is_originated[4096];
	char not_originated[4096];
	char seqno_string_hex[10];
	struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;

	addr = (struct srp_addr *)commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + commit_token->addr_entries);

	log_printf (instance->totemsrp_log_level_notice,
		"entering RECOVERY state.\n");

	instance->my_high_ring_delivered = 0;

	sq_reinit (&instance->recovery_sort_queue, SEQNO_START_MSG);
	queue_reinit (&instance->retrans_message_queue);

	low_ring_aru = instance->old_ring_state_high_seq_received;

	memb_state_commit_token_send (instance, commit_token);

	instance->my_token_seq = SEQNO_START_TOKEN - 1;

	/*
	 * Build regular configuration
	 */
	instance->my_new_memb_entries = commit_token->addr_entries;

 	totemrrp_processor_count_set (
		instance->totemrrp_handle,
		commit_token->addr_entries);

	memcpy (instance->my_new_memb_list, addr,
		sizeof (struct srp_addr) * instance->my_new_memb_entries);

	/*
	 * Build transitional configuration
	 */
	memb_set_and (instance->my_new_memb_list, instance->my_new_memb_entries,
		instance->my_memb_list, instance->my_memb_entries,
		instance->my_trans_memb_list, &instance->my_trans_memb_entries);

	for (i = 0; i < instance->my_new_memb_entries; i++) {
		log_printf (instance->totemsrp_log_level_notice,
			"position [%d] member %s:\n", i, totemip_print (&addr[i].addr[0]));
		log_printf (instance->totemsrp_log_level_notice,
			"previous ring seq %lld rep %s\n",
			memb_list[i].ring_id.seq,
			totemip_print (&memb_list[i].ring_id.rep));

		log_printf (instance->totemsrp_log_level_notice,
			"aru %x high delivered %x received flag %d\n",
			memb_list[i].aru,
			memb_list[i].high_delivered,
			memb_list[i].received_flg);

// TODO		assert (!totemip_zero_check(&memb_list[i].ring_id.rep));
	}
	/*
	 * Determine if any received flag is false
	 */
	for (i = 0; i < commit_token->addr_entries; i++) {
		if (memb_set_subset (&instance->my_new_memb_list[i], 1,
			instance->my_trans_memb_list, instance->my_trans_memb_entries) &&

			memb_list[i].received_flg == 0) {
			instance->my_deliver_memb_entries = instance->my_trans_memb_entries;
			memcpy (instance->my_deliver_memb_list, instance->my_trans_memb_list,
				sizeof (struct srp_addr) * instance->my_trans_memb_entries);
			local_received_flg = 0;
			break;
		}
	}
	if (local_received_flg == 1) {
		goto no_originate;
	} /* Else originate messages if we should */

	/*
	 * Calculate my_low_ring_aru, instance->my_high_ring_delivered for the transitional membership
	 */
	for (i = 0; i < commit_token->addr_entries; i++) {
		if (memb_set_subset (&instance->my_new_memb_list[i], 1,
			instance->my_deliver_memb_list,
			 instance->my_deliver_memb_entries) &&

		memcmp (&instance->my_old_ring_id,
			&memb_list[i].ring_id,
			sizeof (struct memb_ring_id)) == 0) {

			if (sq_lt_compare (memb_list[i].aru, low_ring_aru)) {

				low_ring_aru = memb_list[i].aru;
			}
			if (sq_lt_compare (instance->my_high_ring_delivered, memb_list[i].high_delivered)) {
				instance->my_high_ring_delivered = memb_list[i].high_delivered;
			}
		}
	}

	/*
	 * Copy all old ring messages to instance->retrans_message_queue
	 */
	range = instance->old_ring_state_high_seq_received - low_ring_aru;
	if (range == 0) {
		/*
		 * No messages to copy
		 */
		goto no_originate;
	}
	assert (range < 1024);

	log_printf (instance->totemsrp_log_level_notice,
		"copying all old ring messages from %x-%x.\n",
		low_ring_aru + 1, instance->old_ring_state_high_seq_received);
	strcpy (not_originated, "Not Originated for recovery: ");
	strcpy (is_originated, "Originated for recovery: ");
		
	for (i = 1; i <= range; i++) {
		struct sort_queue_item *sort_queue_item;
		struct message_item message_item;
		void *ptr;
		int res;

		sprintf (seqno_string_hex, "%x ", low_ring_aru + i);
		res = sq_item_get (&instance->regular_sort_queue,
			low_ring_aru + i, &ptr);
		if (res != 0) {
			strcat (not_originated, seqno_string_hex);
		continue;
	}
	strcat (is_originated, seqno_string_hex);
	sort_queue_item = ptr;
	assert (sort_queue_item->iov_len > 0);
	assert (sort_queue_item->iov_len <= MAXIOVS);
	messages_originated++;
	memset (&message_item, 0, sizeof (struct message_item));
// TODO	 LEAK
	message_item.mcast = malloc (sizeof (struct mcast));
	assert (message_item.mcast);
	memcpy (message_item.mcast, sort_queue_item->iovec[0].iov_base,
		sizeof (struct mcast));
	memcpy (&message_item.mcast->ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id));
	message_item.mcast->header.encapsulated = 1;
	message_item.mcast->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (message_item.mcast->header.nodeid);
	message_item.iov_len = sort_queue_item->iov_len;
	memcpy (&message_item.iovec, &sort_queue_item->iovec,
		sizeof (struct iovec) * sort_queue_item->iov_len);
		queue_item_add (&instance->retrans_message_queue, &message_item);
	}
	log_printf (instance->totemsrp_log_level_notice,
		"Originated %d messages in RECOVERY.\n", messages_originated);
	strcat (not_originated, "\n");
	strcat (is_originated, "\n");
	log_printf (instance->totemsrp_log_level_notice, is_originated);
	log_printf (instance->totemsrp_log_level_notice, not_originated);
	goto originated;

no_originate:
	log_printf (instance->totemsrp_log_level_notice,
		"Did not need to originate any messages in recovery.\n");

originated:
	instance->my_aru = SEQNO_START_MSG;
	instance->my_aru_count = 0;
	instance->my_seq_unchanged = 0;
	instance->my_high_seq_received = SEQNO_START_MSG;
	instance->my_install_seq = SEQNO_START_MSG;
	instance->last_released = SEQNO_START_MSG;

	reset_token_timeout (instance); // REVIEWED
	reset_token_retransmit_timeout (instance); // REVIEWED

	instance->memb_state = MEMB_STATE_RECOVERY;
	return;
}

int totemsrp_new_msg_signal (totemsrp_handle handle)
{
	struct totemsrp_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	token_hold_cancel_send (instance);

	hdb_handle_put (&totemsrp_instance_database, handle);
	return (0);
error_exit:
	return (-1);
}

int totemsrp_mcast (
	totemsrp_handle handle,
	struct iovec *iovec,
	int iov_len,
	int guarantee)
{
	int i;
	int j;
	struct message_item message_item;
	struct totemsrp_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}
	
	if (queue_is_full (&instance->new_message_queue)) {
		log_printf (instance->totemsrp_log_level_warning, "queue full\n");
		return (-1);
	}
	for (j = 0, i = 0; i < iov_len; i++) {
		j+= iovec[i].iov_len;
	}

	memset (&message_item, 0, sizeof (struct message_item));

	/*
	 * Allocate pending item
	 */
// TODO LEAK
	message_item.mcast = malloc (sizeof (struct mcast));
	if (message_item.mcast == 0) {
		goto error_mcast;
	}

	/*
	 * Set mcast header
	 */
	message_item.mcast->header.type = MESSAGE_TYPE_MCAST;
	message_item.mcast->header.endian_detector = ENDIAN_LOCAL;
	message_item.mcast->header.encapsulated = 2;
	message_item.mcast->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (message_item.mcast->header.nodeid);

	message_item.mcast->guarantee = guarantee;
	srp_addr_copy (&message_item.mcast->system_from, &instance->my_id);

	for (i = 0; i < iov_len; i++) {
// TODO LEAK
		message_item.iovec[i].iov_base = malloc (iovec[i].iov_len);

		if (message_item.iovec[i].iov_base == 0) {
			goto error_iovec;
		}

		memcpy (message_item.iovec[i].iov_base, iovec[i].iov_base,
			iovec[i].iov_len);

		message_item.iovec[i].iov_len = iovec[i].iov_len;
	}

	message_item.iov_len = iov_len;

	log_printf (instance->totemsrp_log_level_debug, "mcasted message added to pending queue\n");
	queue_item_add (&instance->new_message_queue, &message_item);

	hdb_handle_put (&totemsrp_instance_database, handle);
	return (0);

error_iovec:
	for (j = 0; j < i; j++) {
		free (message_item.iovec[j].iov_base);
	}

error_mcast:
	hdb_handle_put (&totemsrp_instance_database, handle);

error_exit:
	return (-1);
}

/*
 * Determine if there is room to queue a new message
 */
int totemsrp_avail (totemsrp_handle handle)
{
	int avail;
	struct totemsrp_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	queue_avail (&instance->new_message_queue, &avail);

	hdb_handle_put (&totemsrp_instance_database, handle);

	return (avail);

error_exit:
	return (0);
}

/*
 * ORF Token Management
 */
/* 
 * Recast message to mcast group if it is available
 */
static int orf_token_remcast (
	struct totemsrp_instance *instance,
	int seq)
{
	struct sort_queue_item *sort_queue_item;
	int res;
	void *ptr;

	struct sq *sort_queue;

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}

	res = sq_in_range (sort_queue, seq);
	if (res == 0) {
		log_printf (instance->totemsrp_log_level_debug, "sq not in range\n");
		return (-1);
	}
	
	/*
	 * Get RTR item at seq, if not available, return
	 */
	res = sq_item_get (sort_queue, seq, &ptr);
	if (res != 0) {
		return -1;
	}

	sort_queue_item = ptr;

	totemrrp_mcast_noflush_send (instance->totemrrp_handle,
		sort_queue_item->iovec,
		sort_queue_item->iov_len);

	return (0);
}


/*
 * Free all freeable messages from ring
 */
static void messages_free (
	struct totemsrp_instance *instance,
	unsigned int token_aru)
{
	struct sort_queue_item *regular_message;
	unsigned int i, j;
	int res;
	int log_release = 0;
	unsigned int release_to;
	unsigned int range = 0;

	release_to = token_aru;
	if (sq_lt_compare (instance->my_last_aru, release_to)) {
		release_to = instance->my_last_aru;
	}
	if (sq_lt_compare (instance->my_high_delivered, release_to)) {
		release_to = instance->my_high_delivered;
	}

	/*
	 * Ensure we dont try release before an already released point
	 */
	if (sq_lt_compare (release_to, instance->last_released)) {
		return;
	}

	range = release_to - instance->last_released;
	assert (range < 1024);

	/*
	 * Release retransmit list items if group aru indicates they are transmitted
	 */
	for (i = 1; i <= range; i++) {
		void *ptr;

		res = sq_item_get (&instance->regular_sort_queue,
			instance->last_released + i, &ptr);
		if (res == 0) {
			regular_message = ptr;
			for (j = 0; j < regular_message->iov_len; j++) {
				free (regular_message->iovec[j].iov_base);
			}
		}
		sq_items_release (&instance->regular_sort_queue,
			instance->last_released + i);

		log_release = 1;
	}
	instance->last_released += range;

 	if (log_release) {
		log_printf (instance->totemsrp_log_level_debug,
			"releasing messages up to and including %x\n", release_to);
	}
}

static void update_aru (
	struct totemsrp_instance *instance)
{
	unsigned int i;
	int res;
	struct sq *sort_queue;
	unsigned int range;
	unsigned int my_aru_saved = 0;

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}

	range = instance->my_high_seq_received - instance->my_aru;
	if (range > 1024) {
		return;
	}

	my_aru_saved = instance->my_aru;
	for (i = 1; i <= range; i++) {

		void *ptr;

		res = sq_item_get (sort_queue, my_aru_saved + i, &ptr);
		/*
		 * If hole, stop updating aru
		 */
		if (res != 0) {
			break;
		}
	}
	instance->my_aru += i - 1;
}

/*
 * Multicasts pending messages onto the ring (requires orf_token possession)
 */
static int orf_token_mcast (
	struct totemsrp_instance *instance,
	struct orf_token *token,
	int fcc_mcasts_allowed)
{
	struct message_item *message_item = 0;
	struct queue *mcast_queue;
	struct sq *sort_queue;
	struct sort_queue_item sort_queue_item;
	struct sort_queue_item *sort_queue_item_ptr;
	struct mcast *mcast;
	unsigned int fcc_mcast_current;

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		mcast_queue = &instance->retrans_message_queue;
		sort_queue = &instance->recovery_sort_queue;
		reset_token_retransmit_timeout (instance); // REVIEWED
	} else {
		mcast_queue = &instance->new_message_queue;
		sort_queue = &instance->regular_sort_queue;
	}

	for (fcc_mcast_current = 0; fcc_mcast_current < fcc_mcasts_allowed; fcc_mcast_current++) {
		if (queue_is_empty (mcast_queue)) {
			break;
		}
		message_item = (struct message_item *)queue_item_get (mcast_queue);
		/* preincrement required by algo */
		if (instance->old_ring_state_saved &&
			(instance->memb_state == MEMB_STATE_GATHER ||
			instance->memb_state == MEMB_STATE_COMMIT)) {

			log_printf (instance->totemsrp_log_level_debug,
				"not multicasting at seqno is %d\n",
			token->seq);
			return (0);
		}

		message_item->mcast->seq = ++token->seq;
		message_item->mcast->this_seqno = instance->global_seqno++;

		/*
		 * Build IO vector
		 */
		memset (&sort_queue_item, 0, sizeof (struct sort_queue_item));
		sort_queue_item.iovec[0].iov_base = (char *)message_item->mcast;
		sort_queue_item.iovec[0].iov_len = sizeof (struct mcast);
	
		mcast = (struct mcast *)sort_queue_item.iovec[0].iov_base;
	
		memcpy (&sort_queue_item.iovec[1], message_item->iovec,
			message_item->iov_len * sizeof (struct iovec));

		memb_ring_id_copy (&mcast->ring_id, &instance->my_ring_id);

		sort_queue_item.iov_len = message_item->iov_len + 1;

		assert (sort_queue_item.iov_len < 16);

		/*
		 * Add message to retransmit queue
		 */
		sort_queue_item_ptr = sq_item_add (sort_queue,
			&sort_queue_item, message_item->mcast->seq);

		totemrrp_mcast_noflush_send (instance->totemrrp_handle,
			sort_queue_item_ptr->iovec,
			sort_queue_item_ptr->iov_len);
		
		/*
		 * Delete item from pending queue
		 */
		queue_item_remove (mcast_queue);
	}

	/*
	 * If messages mcasted, deliver any new messages to totempg
	 */
	instance->my_high_seq_received = token->seq;
		
	update_aru (instance);

	/*
	 * Return 1 if more messages are available for single node clusters
	 */
	return (fcc_mcast_current);
}

/*
 * Remulticasts messages in orf_token's retransmit list (requires orf_token)
 * Modify's orf_token's rtr to include retransmits required by this process
 */
static int orf_token_rtr (
	struct totemsrp_instance *instance,
	struct orf_token *orf_token,
	unsigned int *fcc_allowed)
{
	unsigned int res;
	unsigned int i, j;
	unsigned int found;
	unsigned int total_entries;
	struct sq *sort_queue;
	struct rtr_item *rtr_list;
	unsigned int range = 0;
	char retransmit_msg[1024];
	char value[64];

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}

	rtr_list = &orf_token->rtr_list[0];
	
	strcpy (retransmit_msg, "Retransmit List: ");
	if (orf_token->rtr_list_entries) {
		log_printf (instance->totemsrp_log_level_debug,
			"Retransmit List %d\n", orf_token->rtr_list_entries);
		for (i = 0; i < orf_token->rtr_list_entries; i++) {
			sprintf (value, "%x ", rtr_list[i].seq);
			strcat (retransmit_msg, value);
		}
		strcat (retransmit_msg, "\n");
		log_printf (instance->totemsrp_log_level_notice,
			"%s", retransmit_msg);
	}

	total_entries = orf_token->rtr_list_entries;

	/*
	 * Retransmit messages on orf_token's RTR list from RTR queue
	 */
	for (instance->fcc_remcast_current = 0, i = 0;
		instance->fcc_remcast_current < *fcc_allowed && i < orf_token->rtr_list_entries;) {

		/*
		 * If this retransmit request isn't from this configuration,
		 * try next rtr entry
		 */
 		if (memcmp (&rtr_list[i].ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id)) != 0) {

			i += 1;
			continue;
		}

		res = orf_token_remcast (instance, rtr_list[i].seq);
		if (res == 0) {
			/*
			 * Multicasted message, so no need to copy to new retransmit list
			 */
			orf_token->rtr_list_entries -= 1;
			assert (orf_token->rtr_list_entries >= 0);
			memmove (&rtr_list[i], &rtr_list[i + 1],
				sizeof (struct rtr_item) * (orf_token->rtr_list_entries));

			instance->fcc_remcast_current++;
		} else {
			i += 1;
		}
	}
	*fcc_allowed = *fcc_allowed - instance->fcc_remcast_current;

	/*
	 * Add messages to retransmit to RTR list
	 * but only retry if there is room in the retransmit list
	 */

	range = instance->my_high_seq_received - instance->my_aru;
	assert (range < 100000);

	for (i = 1; (orf_token->rtr_list_entries < RETRANSMIT_ENTRIES_MAX) &&
		(i <= range); i++) {

		/*
		 * Ensure message is within the sort queue range
		 */
		res = sq_in_range (sort_queue, instance->my_aru + i);
		if (res == 0) {
			break;
		}

		/*
		 * Find if a message is missing from this processor
		 */
		res = sq_item_inuse (sort_queue, instance->my_aru + i);
		if (res == 0) {
			/*
			 * Determine if missing message is already in retransmit list
			 */
			found = 0;
			for (j = 0; j < orf_token->rtr_list_entries; j++) {
				if (instance->my_aru + i == rtr_list[j].seq) {
					found = 1;
				}
			}
			if (found == 0) {
				/*
				 * Missing message not found in current retransmit list so add it
				 */
				memb_ring_id_copy (
					&rtr_list[orf_token->rtr_list_entries].ring_id,
					&instance->my_ring_id);
				rtr_list[orf_token->rtr_list_entries].seq = instance->my_aru + i;
				orf_token->rtr_list_entries++;
			}
		}
	}
	return (instance->fcc_remcast_current);
}

static void token_retransmit (struct totemsrp_instance *instance)
{
	struct iovec iovec;

	iovec.iov_base = instance->orf_token_retransmit;
	iovec.iov_len = instance->orf_token_retransmit_size;

	totemrrp_token_send (instance->totemrrp_handle,
		&iovec,
		1);
}

/*
 * Retransmit the regular token if no mcast or token has
 * been received in retransmit token period retransmit
 * the token to the next processor
 */
static void timer_function_token_retransmit_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	switch (instance->memb_state) {
	case MEMB_STATE_GATHER:
		break;
	case MEMB_STATE_COMMIT:
		break;
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		token_retransmit (instance);
		reset_token_retransmit_timeout (instance); // REVIEWED
		break;
	}
}

static void timer_function_token_hold_retransmit_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	switch (instance->memb_state) {
	case MEMB_STATE_GATHER:
		break;
	case MEMB_STATE_COMMIT:
		break;
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		token_retransmit (instance);
		break;
	}
}

static void timer_function_merge_detect_timeout(void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	instance->my_merge_detect_timeout_outstanding = 0;

	switch (instance->memb_state) {
	case MEMB_STATE_OPERATIONAL:
		if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id.addr[0])) {
			memb_merge_detect_transmit (instance);
		}
		break;
	case MEMB_STATE_GATHER:
	case MEMB_STATE_COMMIT:
	case MEMB_STATE_RECOVERY:
		break;
	}
}

/*
 * Send orf_token to next member (requires orf_token)
 */
static int token_send (
	struct totemsrp_instance *instance,
	struct orf_token *orf_token,
	int forward_token)
{
	struct iovec iovec;
	int res = 0;
	int iov_len = sizeof (struct orf_token) +
		(orf_token->rtr_list_entries * sizeof (struct rtr_item));

	memcpy (instance->orf_token_retransmit, orf_token, iov_len);
	instance->orf_token_retransmit_size = iov_len;
	orf_token->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (orf_token->header.nodeid);

	if (forward_token == 0) {
		return (0);
	}

	iovec.iov_base = (char *)orf_token;
	iovec.iov_len = iov_len;

	totemrrp_token_send (instance->totemrrp_handle,
		&iovec,
		1);

	return (res);
}

static int token_hold_cancel_send (struct totemsrp_instance *instance)
{
	struct token_hold_cancel token_hold_cancel;
	struct iovec iovec[2];

	/*
	 * Only cancel if the token is currently held
	 */
	if (instance->my_token_held == 0) {
		return (0);
	}
	instance->my_token_held = 0;

	/*
	 * Build message
	 */
	token_hold_cancel.header.type = MESSAGE_TYPE_TOKEN_HOLD_CANCEL;
	token_hold_cancel.header.endian_detector = ENDIAN_LOCAL;
	token_hold_cancel.header.nodeid = instance->my_id.addr[0].nodeid;
	assert (token_hold_cancel.header.nodeid);

	iovec[0].iov_base = (char *)&token_hold_cancel;
	iovec[0].iov_len = sizeof (struct token_hold_cancel) -
		sizeof (struct memb_ring_id);
	iovec[1].iov_base = (char *)&instance->my_ring_id;
	iovec[1].iov_len = sizeof (struct memb_ring_id);

	totemrrp_mcast_flush_send (instance->totemrrp_handle, iovec, 2);

	return (0);
}
//AAA

static int orf_token_send_initial (struct totemsrp_instance *instance)
{
	struct orf_token orf_token;
	int res;

	orf_token.header.type = MESSAGE_TYPE_ORF_TOKEN;
	orf_token.header.endian_detector = ENDIAN_LOCAL;
	orf_token.header.encapsulated = 0;
	orf_token.header.nodeid = instance->my_id.addr[0].nodeid;
	assert (orf_token.header.nodeid);
	orf_token.seq = 0;
	orf_token.seq = SEQNO_START_MSG;
	orf_token.token_seq = SEQNO_START_TOKEN;
	orf_token.retrans_flg = 1;
	instance->my_set_retrans_flg = 1;
/*
	if (queue_is_empty (&instance->retrans_message_queue) == 1) {
		orf_token.retrans_flg = 0;
	} else {
		orf_token.retrans_flg = 1;
		instance->my_set_retrans_flg = 1;
	}
*/
		
	orf_token.aru = 0;
	orf_token.aru = SEQNO_START_MSG - 1;
	orf_token.aru_addr = instance->my_id.addr[0].nodeid;

	memb_ring_id_copy (&orf_token.ring_id, &instance->my_ring_id);
	orf_token.fcc = 0;
	orf_token.backlog = 0;

	orf_token.rtr_list_entries = 0;

	res = token_send (instance, &orf_token, 1);

	return (res);
}

static void memb_state_commit_token_update (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	int memb_index_this;
	struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;

	addr = (struct srp_addr *)commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + commit_token->addr_entries);

	memb_index_this = (commit_token->memb_index + 1) % commit_token->addr_entries;
	memb_ring_id_copy (&memb_list[memb_index_this].ring_id,
		&instance->my_old_ring_id);
	assert (!totemip_zero_check(&instance->my_old_ring_id.rep));

	memb_list[memb_index_this].aru = instance->old_ring_state_aru;
	/*
	 *  TODO high delivered is really instance->my_aru, but with safe this
	 * could change?
	 */
	memb_list[memb_index_this].high_delivered = instance->my_high_delivered;
	memb_list[memb_index_this].received_flg = instance->my_received_flg;

	commit_token->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (commit_token->header.nodeid);
}

static int memb_state_commit_token_send (struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	struct iovec iovec;
	int memb_index_this;
	int memb_index_next;
	struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;
	unsigned int i;

	addr = (struct srp_addr *)commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + commit_token->addr_entries);

	commit_token->token_seq++;
	memb_index_this = (commit_token->memb_index + 1) % commit_token->addr_entries;
	memb_index_next = (memb_index_this + 1) % commit_token->addr_entries;
	commit_token->memb_index = memb_index_this;


	iovec.iov_base = (char *)commit_token;
	iovec.iov_len = sizeof (struct memb_commit_token) +
		((sizeof (struct srp_addr) +
			sizeof (struct memb_commit_token_memb_entry)) * commit_token->addr_entries);

	for (i = 0; i < instance->totem_config->interface_count; i++) {
		totemrrp_token_target_set (
			instance->totemrrp_handle,
			&addr[memb_index_next].addr[i],
			i);
	}

	totemrrp_token_send (instance->totemrrp_handle,
		&iovec,
		1);

	return (0);
}


static int memb_lowest_in_config (struct totemsrp_instance *instance)
{
	struct srp_addr token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;
	int i;
	struct totem_ip_address *lowest_addr;

	memb_set_subtract (token_memb, &token_memb_entries,
		instance->my_proc_list, instance->my_proc_list_entries,
		instance->my_failed_list, instance->my_failed_list_entries);

	/*
	 * find representative by searching for smallest identifier
	 */
	
	lowest_addr = &token_memb[0].addr[0];
	for (i = 1; i < token_memb_entries; i++) {
		if (totemip_compare(lowest_addr, &token_memb[i].addr[0]) > 0) {
			totemip_copy (lowest_addr, &token_memb[i].addr[0]);
		}
	}
	return (totemip_compare (lowest_addr, &instance->my_id.addr[0]) == 0);
}

static int srp_addr_compare (const void *a, const void *b)
{
	struct srp_addr *srp_a = (struct srp_addr *)a;
	struct srp_addr *srp_b = (struct srp_addr *)b;

	return (totemip_compare (&srp_a->addr[0], &srp_b->addr[0]));
}

static void memb_state_commit_token_create (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	struct srp_addr token_memb[PROCESSOR_COUNT_MAX];
	struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;
	int token_memb_entries = 0;

	log_printf (instance->totemsrp_log_level_notice,
		"Creating commit token because I am the rep.\n");

	memb_set_subtract (token_memb, &token_memb_entries,
		instance->my_proc_list, instance->my_proc_list_entries,
		instance->my_failed_list, instance->my_failed_list_entries);

	memset (commit_token, 0, sizeof (struct memb_commit_token));
	commit_token->header.type = MESSAGE_TYPE_MEMB_COMMIT_TOKEN;
	commit_token->header.endian_detector = ENDIAN_LOCAL;
	commit_token->header.encapsulated = 0;
	commit_token->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (commit_token->header.nodeid);

	totemip_copy(&commit_token->ring_id.rep, &instance->my_id.addr[0]);

	commit_token->ring_id.seq = instance->token_ring_id_seq + 4;

	/*
	 * This qsort is necessary to ensure the commit token traverses
	 * the ring in the proper order
	 */
	qsort (token_memb, token_memb_entries, sizeof (struct srp_addr),
		srp_addr_compare);

	commit_token->memb_index = token_memb_entries - 1;
	commit_token->addr_entries = token_memb_entries;

	addr = (struct srp_addr *)commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + commit_token->addr_entries);

	memcpy (addr, token_memb,
		token_memb_entries * sizeof (struct srp_addr));
	memset (memb_list, 0,
		sizeof (struct memb_commit_token_memb_entry) * token_memb_entries);
}

static void memb_join_message_send (struct totemsrp_instance *instance)
{
	struct memb_join memb_join;
	struct iovec iovec[3];

	memb_join.header.type = MESSAGE_TYPE_MEMB_JOIN;
	memb_join.header.endian_detector = ENDIAN_LOCAL;
	memb_join.header.encapsulated = 0;
	memb_join.header.nodeid = instance->my_id.addr[0].nodeid;
	assert (memb_join.header.nodeid);


	assert (srp_addr_equal (&instance->my_proc_list[0], &instance->my_proc_list[1]) == 0);
	memb_join.ring_seq = instance->my_ring_id.seq;
	memb_join.proc_list_entries = instance->my_proc_list_entries;
	memb_join.failed_list_entries = instance->my_failed_list_entries;
	srp_addr_copy (&memb_join.system_from, &instance->my_id);
		
	iovec[0].iov_base = (char *)&memb_join;
	iovec[0].iov_len = sizeof (struct memb_join);
	iovec[1].iov_base = (char *)&instance->my_proc_list;
	iovec[1].iov_len = instance->my_proc_list_entries *
		sizeof (struct srp_addr);
	iovec[2].iov_base = (char *)&instance->my_failed_list;
	iovec[2].iov_len = instance->my_failed_list_entries *
		sizeof (struct srp_addr);

	if (instance->totem_config->send_join_timeout) {
		usleep (random() % (instance->totem_config->send_join_timeout * 1000));
	}

	totemrrp_mcast_flush_send (
		instance->totemrrp_handle,
		iovec,
		3);
}

static void memb_merge_detect_transmit (struct totemsrp_instance *instance) 
{
	struct memb_merge_detect memb_merge_detect;
	struct iovec iovec[2];

	memb_merge_detect.header.type = MESSAGE_TYPE_MEMB_MERGE_DETECT;
	memb_merge_detect.header.endian_detector = ENDIAN_LOCAL;
	memb_merge_detect.header.encapsulated = 0;
	memb_merge_detect.header.nodeid = instance->my_id.addr[0].nodeid;
	srp_addr_copy (&memb_merge_detect.system_from, &instance->my_id);
	assert (memb_merge_detect.header.nodeid);

	iovec[0].iov_base = (char *)&memb_merge_detect;
	iovec[0].iov_len = sizeof (struct memb_merge_detect) -
		sizeof (struct memb_ring_id);
	iovec[1].iov_base = (char *)&instance->my_ring_id;
	iovec[1].iov_len = sizeof (struct memb_ring_id);

	totemrrp_mcast_flush_send (instance->totemrrp_handle, iovec, 2);
}

static void memb_ring_id_create_or_load (
	struct totemsrp_instance *instance,
	struct memb_ring_id *memb_ring_id)
{
	int fd;
	int res;
	char filename[256];

	sprintf (filename, "/tmp/ringid_%s",
		totemip_print (&instance->my_id.addr[0]));
	fd = open (filename, O_RDONLY, 0777);
	if (fd > 0) {
		res = read (fd, &memb_ring_id->seq, sizeof (unsigned long long));
		assert (res == sizeof (unsigned long long));
		close (fd);
	} else
	if (fd == -1 && errno == ENOENT) {
		memb_ring_id->seq = 0;
		umask(0);
		fd = open (filename, O_CREAT|O_RDWR, 0777);
		if (fd == -1) {
			log_printf (instance->totemsrp_log_level_warning,
				"Couldn't create %s %s\n", filename, strerror (errno));
		}
		res = write (fd, &memb_ring_id->seq, sizeof (unsigned long long));
		assert (res == sizeof (unsigned long long));
		close (fd);
	} else {
		log_printf (instance->totemsrp_log_level_warning,
			"Couldn't open %s %s\n", filename, strerror (errno));
	}
	
	totemip_copy(&memb_ring_id->rep, &instance->my_id.addr[0]);
	assert (!totemip_zero_check(&memb_ring_id->rep));
	instance->token_ring_id_seq = memb_ring_id->seq;
}

static void memb_ring_id_store (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	char filename[256];
	int fd;
	int res;

	sprintf (filename, "/tmp/ringid_%s",
		totemip_print (&instance->my_id.addr[0]));

	fd = open (filename, O_WRONLY, 0777);
	if (fd == -1) {
		fd = open (filename, O_CREAT|O_RDWR, 0777);
	}
	if (fd == -1) {
		log_printf (instance->totemsrp_log_level_warning,
			"Couldn't store new ring id %llx to stable storage (%s)\n",
				commit_token->ring_id.seq, strerror (errno));
		assert (0);
		return;
	}
	log_printf (instance->totemsrp_log_level_notice,
		"Storing new sequence id for ring %llx\n", commit_token->ring_id.seq);
	//assert (fd > 0);
	res = write (fd, &commit_token->ring_id.seq, sizeof (unsigned long long));
	assert (res == sizeof (unsigned long long));
	close (fd);
	memcpy (&instance->my_ring_id, &commit_token->ring_id, sizeof (struct memb_ring_id));
	instance->token_ring_id_seq = instance->my_ring_id.seq;
}

int totemsrp_callback_token_create (
	totemsrp_handle handle,
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, void *),
	void *data)
{
	struct token_callback_instance *callback_handle;
	struct totemsrp_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	callback_handle = (struct token_callback_instance *)malloc (sizeof (struct token_callback_instance));
	if (callback_handle == 0) {
		return (-1);
	}
	*handle_out = (void *)callback_handle;
	list_init (&callback_handle->list);
	callback_handle->callback_fn = callback_fn;
	callback_handle->data = data;
	callback_handle->callback_type = type;
	callback_handle->delete = delete;
	switch (type) {
	case TOTEM_CALLBACK_TOKEN_RECEIVED:
		list_add (&callback_handle->list, &instance->token_callback_received_listhead);
		break;
	case TOTEM_CALLBACK_TOKEN_SENT:
		list_add (&callback_handle->list, &instance->token_callback_sent_listhead);
		break;
	}

	hdb_handle_put (&totemsrp_instance_database, handle);

error_exit:
	return (0);
}

void totemsrp_callback_token_destroy (totemsrp_handle handle, void **handle_out)
{
	struct token_callback_instance *h;

	if (*handle_out) {
 		h = (struct token_callback_instance *)*handle_out;
		list_del (&h->list);
		free (h);
		h = NULL;
		*handle_out = 0;
	}
}

void totem_callback_token_type (struct totemsrp_instance *instance, void *handle)
{
	struct token_callback_instance *token_callback_instance = (struct token_callback_instance *)handle;

	list_del (&token_callback_instance->list);
	free (token_callback_instance);
}

static void token_callbacks_execute (
	struct totemsrp_instance *instance,
	enum totem_callback_token_type type)
{
	struct list_head *list;
	struct list_head *list_next;
	struct list_head *callback_listhead = 0;
	struct token_callback_instance *token_callback_instance;
	int res;
	int del;

	switch (type) {
	case TOTEM_CALLBACK_TOKEN_RECEIVED:
		callback_listhead = &instance->token_callback_received_listhead;
		break;
	case TOTEM_CALLBACK_TOKEN_SENT:
		callback_listhead = &instance->token_callback_sent_listhead;
		break;
	default:
		assert (0);
	}
	
	for (list = callback_listhead->next; list != callback_listhead;
		list = list_next) {

		token_callback_instance = list_entry (list, struct token_callback_instance, list);

		list_next = list->next;
		del = token_callback_instance->delete;
		if (del == 1) {
			list_del (list);
		}

		res = token_callback_instance->callback_fn (
			token_callback_instance->callback_type,
			token_callback_instance->data);
		/*
		 * This callback failed to execute, try it again on the next token
		 */
		if (res == -1 && del == 1) {
			list_add (list, callback_listhead);
		} else	if (del) {
			free (token_callback_instance);
		}
	}
}

/*
 * Flow control functions
 */
static unsigned int backlog_get (struct totemsrp_instance *instance)
{
	unsigned int backlog = 0;

	if (instance->memb_state == MEMB_STATE_OPERATIONAL) {
		backlog = queue_used (&instance->new_message_queue);
	} else
	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		backlog = queue_used (&instance->retrans_message_queue);
	}
	return (backlog);
}

static int fcc_calculate (
	struct totemsrp_instance *instance,
	struct orf_token *token)
{
	unsigned int transmits_allowed;
	unsigned int backlog_calc;

	transmits_allowed = instance->totem_config->max_messages;

	if (transmits_allowed > instance->totem_config->window_size - token->fcc) {
		transmits_allowed = instance->totem_config->window_size - token->fcc;
	}

	instance->my_cbl = backlog_get (instance);

	/*
	 * Only do backlog calculation if there is a backlog otherwise
	 * we would result in div by zero
	 */
	if (token->backlog + instance->my_cbl - instance->my_pbl) {
		backlog_calc = (instance->totem_config->window_size * instance->my_pbl) /
			(token->backlog + instance->my_cbl - instance->my_pbl);
		if (backlog_calc > 0 && transmits_allowed > backlog_calc) {
			transmits_allowed = backlog_calc;
		}
	}

	return (transmits_allowed);
}

/*
 * don't overflow the RTR sort queue
 */
static void fcc_rtr_limit (
	struct totemsrp_instance *instance,
	struct orf_token *token,
	unsigned int *transmits_allowed)
{
	assert ((QUEUE_RTR_ITEMS_SIZE_MAX - *transmits_allowed - instance->totem_config->window_size) >= 0);
	if (sq_lt_compare (instance->last_released +
		QUEUE_RTR_ITEMS_SIZE_MAX - *transmits_allowed -
		instance->totem_config->window_size,

			token->seq)) {

			*transmits_allowed = 0;
	}
}

static void fcc_token_update (
	struct totemsrp_instance *instance,
	struct orf_token *token,
	unsigned int msgs_transmitted)
{
	token->fcc += msgs_transmitted - instance->my_trc;
	token->backlog += instance->my_cbl - instance->my_pbl;
	assert (token->backlog >= 0);
	instance->my_trc = msgs_transmitted;
	instance->my_pbl = instance->my_cbl;
}

/*
 * Message Handlers
 */

/*
 * message handler called when TOKEN message type received
 */
static int message_handler_orf_token (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	char token_storage[1500];
	char token_convert[1500];
	struct orf_token *token = NULL;
	int forward_token;
	unsigned int transmits_allowed;
	unsigned int mcasted_retransmit;
	unsigned int mcasted_regular;
	unsigned int last_aru;
	unsigned int low_water;

#ifdef GIVEINFO
	struct timeval tv_current;
	struct timeval tv_diff;

	gettimeofday (&tv_current, NULL);
	timersub (&tv_current, &tv_old, &tv_diff);
	memcpy (&tv_old, &tv_current, sizeof (struct timeval));

	if ((((float)tv_diff.tv_usec) / 100.0) > 5.0) {
		printf ("OTHERS %0.4f ms\n", ((float)tv_diff.tv_usec) / 100.0);
	}
#endif

#ifdef TEST_DROP_ORF_TOKEN_PERCENTAGE
	if (random()%100 < TEST_DROP_ORF_TOKEN_PERCENTAGE) {
		return (0);
	}
#endif

	if (endian_conversion_needed) {
		orf_token_endian_convert ((struct orf_token *)msg,
			(struct orf_token *)token_convert);
		msg = (struct orf_token *)token_convert;
	}

	/*
	 * Make copy of token and retransmit list in case we have
	 * to flush incoming messages from the kernel queue
	 */
	token = (struct orf_token *)token_storage;
	memcpy (token, msg, sizeof (struct orf_token));
	memcpy (&token->rtr_list[0], msg + sizeof (struct orf_token),
		sizeof (struct rtr_item) * RETRANSMIT_ENTRIES_MAX);


	/*
	 * Handle merge detection timeout
	 */
	if (token->seq == instance->my_last_seq) {
		start_merge_detect_timeout (instance);
		instance->my_seq_unchanged += 1;
	} else {
		cancel_merge_detect_timeout (instance);
		cancel_token_hold_retransmit_timeout (instance);
		instance->my_seq_unchanged = 0;
	}

	instance->my_last_seq = token->seq;

#ifdef TEST_RECOVERY_MSG_COUNT
	if (instance->memb_state == MEMB_STATE_OPERATIONAL && token->seq > TEST_RECOVERY_MSG_COUNT) {
		return (0);
	}
#endif

	totemrrp_recv_flush (instance->totemrrp_handle);

	/*
	 * Determine if we should hold (in reality drop) the token
	 */
	instance->my_token_held = 0;
	if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id.addr[0]) &&
		instance->my_seq_unchanged > instance->totem_config->seqno_unchanged_const) {
		instance->my_token_held = 1;
	} else
		if (!totemip_equal(&instance->my_ring_id.rep,  &instance->my_id.addr[0]) &&
		instance->my_seq_unchanged >= instance->totem_config->seqno_unchanged_const) {
		instance->my_token_held = 1;
	}

	/*
	 * Hold onto token when there is no activity on ring and
	 * this processor is the ring rep
	 */
	forward_token = 1;
	if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id.addr[0])) {
		if (instance->my_token_held) {
			forward_token = 0;			
		}
	}

	token_callbacks_execute (instance, TOTEM_CALLBACK_TOKEN_RECEIVED);

	switch (instance->memb_state) {
	case MEMB_STATE_COMMIT:
		 /* Discard token */
		break;

	case MEMB_STATE_OPERATIONAL:
		messages_free (instance, token->aru);
	case MEMB_STATE_GATHER:
		/*
		 * DO NOT add break, we use different free mechanism in recovery state
		 */

	case MEMB_STATE_RECOVERY:
		last_aru = instance->my_last_aru;
		instance->my_last_aru = token->aru;

		/*
		 * Discard tokens from another configuration
		 */
		if (memcmp (&token->ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id)) != 0) {

			if ((forward_token)
				&& instance->use_heartbeat) {
				reset_heartbeat_timeout(instance);
			} 
			else {
				cancel_heartbeat_timeout(instance);
			}

			return (0); /* discard token */
		}

		/*
		 * Discard retransmitted tokens
		 */
		if (sq_lte_compare (token->token_seq, instance->my_token_seq)) {
			/*
			 * If this processor receives a retransmitted token, it is sure
		 	 * the previous processor is still alive.  As a result, it can
			 * reset its token timeout.  If some processor previous to that
			 * has failed, it will eventually not execute a reset of the
			 * token timeout, and will cause a reconfiguration to occur.
			 */
			reset_token_timeout (instance);

			if ((forward_token)
				&& instance->use_heartbeat) {
				reset_heartbeat_timeout(instance);
			}
			else {
				cancel_heartbeat_timeout(instance);
			}

			return (0); /* discard token */
		}		

		transmits_allowed = fcc_calculate (instance, token);
		mcasted_retransmit = orf_token_rtr (instance, token, &transmits_allowed);

		fcc_rtr_limit (instance, token, &transmits_allowed);
		mcasted_regular = orf_token_mcast (instance, token, transmits_allowed);
		fcc_token_update (instance, token, mcasted_retransmit +
			mcasted_regular);
			
		if (sq_lt_compare (instance->my_aru, token->aru) ||
			instance->my_id.addr[0].nodeid ==  token->aru_addr ||
			token->aru_addr == 0) {
			
			token->aru = instance->my_aru;
			if (token->aru == token->seq) {
				token->aru_addr = 0;
			} else {
				token->aru_addr = instance->my_id.addr[0].nodeid;
			}
		}
		if (token->aru == last_aru && token->aru_addr != 0) {
			instance->my_aru_count += 1;
		} else {
			instance->my_aru_count = 0;
		}

		if (instance->my_aru_count > instance->totem_config->fail_to_recv_const &&
			token->aru_addr != instance->my_id.addr[0].nodeid) {
			
			log_printf (instance->totemsrp_log_level_error,
				"FAILED TO RECEIVE\n");
// TODO if we fail to receive, it may be possible to end with a gather
// state of proc == failed = 0 entries
/* THIS IS A BIG TODO
			memb_set_merge (&token->aru_addr, 1,
				instance->my_failed_list,
				&instance->my_failed_list_entries);
*/

			ring_state_restore (instance);

printf ("gather 1");
			memb_state_gather_enter (instance);
		} else {
			instance->my_token_seq = token->token_seq;
			token->token_seq += 1;

			if (instance->memb_state == MEMB_STATE_RECOVERY) {
				/*
				 * instance->my_aru == instance->my_high_seq_received means this processor
				 * has recovered all messages it can recover
				 * (ie: its retrans queue is empty)
				 */
				low_water = instance->my_aru;
				if (sq_lt_compare (last_aru, low_water)) {
					low_water = last_aru;
				}
// TODO is this code right
				if (queue_is_empty (&instance->retrans_message_queue) == 0 ||
					low_water != instance->my_high_seq_received) {

					if (token->retrans_flg == 0) {
						token->retrans_flg = 1;
						instance->my_set_retrans_flg = 1;
					}
				} else
				if (token->retrans_flg == 1 && instance->my_set_retrans_flg) {
					token->retrans_flg = 0;
				}
				log_printf (instance->totemsrp_log_level_debug,
					"token retrans flag is %d my set retrans flag%d retrans queue empty %d count %d, low_water %x aru %x\n", 
					token->retrans_flg, instance->my_set_retrans_flg,
					queue_is_empty (&instance->retrans_message_queue),
					instance->my_retrans_flg_count, low_water, token->aru);
				if (token->retrans_flg == 0) { 
					instance->my_retrans_flg_count += 1;
				} else {
					instance->my_retrans_flg_count = 0;
				}
				if (instance->my_retrans_flg_count == 2) {
					instance->my_install_seq = token->seq;
				}
				log_printf (instance->totemsrp_log_level_debug,
					"install seq %x aru %x high seq received %x\n",
					instance->my_install_seq, instance->my_aru, instance->my_high_seq_received);
				if (instance->my_retrans_flg_count >= 2 && instance->my_aru >= instance->my_install_seq && instance->my_received_flg == 0) {
					instance->my_received_flg = 1;
					instance->my_deliver_memb_entries = instance->my_trans_memb_entries;
					memcpy (instance->my_deliver_memb_list, instance->my_trans_memb_list,
						sizeof (struct totem_ip_address) * instance->my_trans_memb_entries);
				}
				if (instance->my_retrans_flg_count >= 3 && token->aru >= instance->my_install_seq) {
					instance->my_rotation_counter += 1;
				} else {
					instance->my_rotation_counter = 0;
				}
				if (instance->my_rotation_counter == 2) {
				log_printf (instance->totemsrp_log_level_debug,
					"retrans flag count %x token aru %x install seq %x aru %x %x\n",
					instance->my_retrans_flg_count, token->aru, instance->my_install_seq,
					instance->my_aru, token->seq);

					memb_state_operational_enter (instance);
					instance->my_rotation_counter = 0;
					instance->my_retrans_flg_count = 0;
				}
			}
	
			totemrrp_send_flush (instance->totemrrp_handle);
			token_send (instance, token, forward_token); 

#ifdef GIVEINFO
			gettimeofday (&tv_current, NULL);
			timersub (&tv_current, &tv_old, &tv_diff);
			memcpy (&tv_old, &tv_current, sizeof (struct timeval));
			if ((((float)tv_diff.tv_usec) / 100.0) > 5.0) {
				printf ("I held %0.4f ms\n", ((float)tv_diff.tv_usec) / 100.0);
			}
#endif
			if (instance->memb_state == MEMB_STATE_OPERATIONAL) {
				messages_deliver_to_app (instance, 0,
					instance->my_high_seq_received);
			}

			/*
			 * Deliver messages after token has been transmitted
			 * to improve performance
			 */
			reset_token_timeout (instance); // REVIEWED
			reset_token_retransmit_timeout (instance); // REVIEWED
			if (totemip_equal(&instance->my_id.addr[0], &instance->my_ring_id.rep) &&
				instance->my_token_held == 1) {

				start_token_hold_retransmit_timeout (instance);
			}

			token_callbacks_execute (instance, TOTEM_CALLBACK_TOKEN_SENT);
		}
		break;
	}

	if ((forward_token)
		&& instance->use_heartbeat) {
		reset_heartbeat_timeout(instance);
	}
	else {
		cancel_heartbeat_timeout(instance);
	}

	return (0);
}

static void messages_deliver_to_app (
	struct totemsrp_instance *instance,
	int skip,
	unsigned int end_point)
{
	struct sort_queue_item *sort_queue_item_p;
	unsigned int i;
	int res;
	struct mcast *mcast;
	unsigned int range = 0;
	int endian_conversion_required = 0 ;
	unsigned int my_high_delivered_stored = 0;


	range = end_point - instance->my_high_delivered;

	if (range) {
		log_printf (instance->totemsrp_log_level_debug,
			"Delivering %x to %x\n", instance->my_high_delivered,
			end_point);
	}
	assert (range < 10240);
	my_high_delivered_stored = instance->my_high_delivered;

	/*
	 * Deliver messages in order from rtr queue to pending delivery queue
	 */
	for (i = 1; i <= range; i++) {

		void *ptr = 0;

		/*
		 * If out of range of sort queue, stop assembly
		 */
		res = sq_in_range (&instance->regular_sort_queue,
			my_high_delivered_stored + i);
		if (res == 0) {
			break;
		}

		res = sq_item_get (&instance->regular_sort_queue,
			my_high_delivered_stored + i, &ptr);
		/*
		 * If hole, stop assembly
		 */
		if (res != 0 && skip == 0) {
			break;
		}

		instance->my_high_delivered = my_high_delivered_stored + i;

		if (res != 0) {
			continue;

		}

		sort_queue_item_p = ptr;

		mcast = (struct mcast *)sort_queue_item_p->iovec[0].iov_base;
		assert (mcast != (struct mcast *)0xdeadbeef);

		/*
		 * Skip messages not originated in instance->my_deliver_memb
		 */
		if (skip &&
			memb_set_subset (&mcast->system_from,
				1,
				instance->my_deliver_memb_list,
				instance->my_deliver_memb_entries) == 0) {
		instance->my_high_delivered = my_high_delivered_stored + i;

			continue;
		}

		/*
		 * Message found
		 */
		log_printf (instance->totemsrp_log_level_debug,
			"Delivering MCAST message with seq %x to pending delivery queue\n",
			mcast->seq);

		if (mcast->header.endian_detector != ENDIAN_LOCAL) {
			endian_conversion_required = 1;
			mcast_endian_convert (mcast, mcast);
		}

		/*
		 * Message is locally originated multicast
		 */
	 	if (sort_queue_item_p->iov_len > 1 &&
			sort_queue_item_p->iovec[0].iov_len == sizeof (struct mcast)) {
			instance->totemsrp_deliver_fn (
				mcast->header.nodeid,
				&sort_queue_item_p->iovec[1],
				sort_queue_item_p->iov_len - 1,
				endian_conversion_required);
		} else {
			sort_queue_item_p->iovec[0].iov_len -= sizeof (struct mcast);
			sort_queue_item_p->iovec[0].iov_base += sizeof (struct mcast);

			instance->totemsrp_deliver_fn (
				mcast->header.nodeid,
				sort_queue_item_p->iovec,
				sort_queue_item_p->iov_len,
				endian_conversion_required);

			sort_queue_item_p->iovec[0].iov_len += sizeof (struct mcast);
			sort_queue_item_p->iovec[0].iov_base -= sizeof (struct mcast);
		}
//TODO	instance->stats_delv += 1;
	}
}

/*
 * recv message handler called when MCAST message type received
 */
static int message_handler_mcast (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct sort_queue_item sort_queue_item;
	struct sq *sort_queue;
	struct mcast mcast_header;
	

	if (endian_conversion_needed) {
		mcast_endian_convert (msg, &mcast_header);
	} else {
		memcpy (&mcast_header, msg, sizeof (struct mcast));
	}

/*
	if (mcast_header.header.encapsulated == 1) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}
*/
	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}
	assert (msg_len < FRAME_SIZE_MAX);

#ifdef TEST_DROP_MCAST_PERCENTAGE
	if (random()%100 < TEST_DROP_MCAST_PERCENTAGE) {
		printf ("dropping message %d\n", mcast_header.seq);
		return (0);
	} else {
		printf ("accepting message %d\n", mcast_header.seq);
	}
#endif

        if (srp_addr_equal (&mcast_header.system_from, &instance->my_id) == 0) {
		cancel_token_retransmit_timeout (instance);
	}

	/*
	 * If the message is foreign execute the switch below
	 */
	if (memcmp (&instance->my_ring_id, &mcast_header.ring_id,
		sizeof (struct memb_ring_id)) != 0) {

		switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			memb_set_merge (
				&mcast_header.system_from, 1,
				instance->my_proc_list, &instance->my_proc_list_entries);
printf ("gather 2");
			memb_state_gather_enter (instance);
			break;

		case MEMB_STATE_GATHER:
			if (!memb_set_subset (
				&mcast_header.system_from,
				1,
				instance->my_proc_list,
				instance->my_proc_list_entries)) {

				memb_set_merge (&mcast_header.system_from, 1,
					instance->my_proc_list, &instance->my_proc_list_entries);
				memb_state_gather_enter (instance);
				return (0);
			}
			break;

		case MEMB_STATE_COMMIT:
			/* discard message */
			break;

		case MEMB_STATE_RECOVERY:
			/* discard message */
			break;
		}
		return (0);
	}

	log_printf (instance->totemsrp_log_level_debug,
		"Received ringid(%s:%lld) seq %x\n",
		totemip_print (&mcast_header.ring_id.rep),
		mcast_header.ring_id.seq,
		mcast_header.seq);

	/*
	 * Add mcast message to rtr queue if not already in rtr queue
	 * otherwise free io vectors
	 */
	if (msg_len > 0 && msg_len < FRAME_SIZE_MAX &&
		sq_in_range (sort_queue, mcast_header.seq) && 
		sq_item_inuse (sort_queue, mcast_header.seq) == 0) {

		/*
		 * Allocate new multicast memory block
		 */
// TODO LEAK
		sort_queue_item.iovec[0].iov_base = malloc (msg_len);
		if (sort_queue_item.iovec[0].iov_base == 0) {
			return (-1); /* error here is corrected by the algorithm */
		}
		memcpy (sort_queue_item.iovec[0].iov_base, msg, msg_len);
		sort_queue_item.iovec[0].iov_len = msg_len;
		assert (sort_queue_item.iovec[0].iov_len > 0);
		assert (sort_queue_item.iovec[0].iov_len < FRAME_SIZE_MAX);
		sort_queue_item.iov_len = 1;
		
		if (sq_lt_compare (instance->my_high_seq_received,
			mcast_header.seq)) {
			instance->my_high_seq_received = mcast_header.seq;
		}

		sq_item_add (sort_queue, &sort_queue_item, mcast_header.seq);
	}

	update_aru (instance);
	if (instance->memb_state == MEMB_STATE_OPERATIONAL) {
		messages_deliver_to_app (instance, 0, instance->my_high_seq_received);
	}

/* TODO remove from retrans message queue for old ring in recovery state */
	return (0);
}

static int message_handler_memb_merge_detect (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct memb_merge_detect *memb_merge_detect = (struct memb_merge_detect *)msg;

	if (endian_conversion_needed) {
		memb_merge_detect_endian_convert (msg, msg);
	}

	/*
	 * do nothing if this is a merge detect from this configuration
	 */
	if (memcmp (&instance->my_ring_id, &memb_merge_detect->ring_id,
		sizeof (struct memb_ring_id)) == 0) {

		return (0);
	}

	/*
	 * Execute merge operation
	 */
	switch (instance->memb_state) {
	case MEMB_STATE_OPERATIONAL:
		memb_set_merge (&memb_merge_detect->system_from, 1,
			instance->my_proc_list, &instance->my_proc_list_entries);
printf ("gather 3");
		memb_state_gather_enter (instance);
		break;

	case MEMB_STATE_GATHER:
		if (!memb_set_subset (
			&memb_merge_detect->system_from,
			1,
			instance->my_proc_list,
			instance->my_proc_list_entries)) {

			memb_set_merge (&memb_merge_detect->system_from, 1,
				instance->my_proc_list, &instance->my_proc_list_entries);
printf ("gather 4");
			memb_state_gather_enter (instance);
			return (0);
		}
		break;

	case MEMB_STATE_COMMIT:
		/* do nothing in commit */
		break;

	case MEMB_STATE_RECOVERY:
		/* do nothing in recovery */
		break;
	}
	return (0);
}

static int memb_join_process (
	struct totemsrp_instance *instance,
	struct memb_join *memb_join)
{
	unsigned char *commit_token_storage[32000];
	struct memb_commit_token *my_commit_token =
		(struct memb_commit_token *)commit_token_storage;
	struct srp_addr *proc_list;
	struct srp_addr *failed_list;

	proc_list = (struct srp_addr *)memb_join->end_of_memb_join;
	failed_list = proc_list + memb_join->proc_list_entries;

	if (memb_set_equal (proc_list,
		memb_join->proc_list_entries,
		instance->my_proc_list,
		instance->my_proc_list_entries) &&

	memb_set_equal (failed_list,
		memb_join->failed_list_entries,
		instance->my_failed_list,
		instance->my_failed_list_entries)) {

		memb_consensus_set (instance, &memb_join->system_from);
	
		if (memb_consensus_agreed (instance) &&
			memb_lowest_in_config (instance)) {

			memb_state_commit_token_create (instance, my_commit_token);
	
			memb_state_commit_enter (instance, my_commit_token);
		} else {
			return (0);
		}
	} else
	if (memb_set_subset (proc_list,
		memb_join->proc_list_entries,
		instance->my_proc_list,
		instance->my_proc_list_entries) &&

		memb_set_subset (failed_list,
		memb_join->failed_list_entries,
		instance->my_failed_list,
		instance->my_failed_list_entries)) {

		return (0);
	} else
	if (memb_set_subset (&memb_join->system_from, 1,
		instance->my_failed_list, instance->my_failed_list_entries)) {

		return (0);
	} else {
		memb_set_merge (proc_list,
			memb_join->proc_list_entries,
			instance->my_proc_list, &instance->my_proc_list_entries);

		if (memb_set_subset (
			&instance->my_id, 1,
			failed_list, memb_join->failed_list_entries)) {

			memb_set_merge (
				&memb_join->system_from, 1,
				instance->my_failed_list, &instance->my_failed_list_entries);
		} else {
			memb_set_merge (failed_list,
				memb_join->failed_list_entries,
				instance->my_failed_list, &instance->my_failed_list_entries);
		}
		memb_state_gather_enter (instance);
		return (1); /* gather entered */
	}
	return (0); /* gather not entered */
}

static void memb_join_endian_convert (struct memb_join *in, struct memb_join *out)
{
	int i;
	struct srp_addr *in_proc_list;
	struct srp_addr *in_failed_list;
	struct srp_addr *out_proc_list;
	struct srp_addr *out_failed_list;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	srp_addr_copy_endian_convert (&out->system_from, &in->system_from);
	out->proc_list_entries = swab32 (in->proc_list_entries);
	out->failed_list_entries = swab32 (in->failed_list_entries);
	out->ring_seq = swab64 (in->ring_seq);

	in_proc_list = (struct srp_addr *)in->end_of_memb_join;
	in_failed_list = in_proc_list + out->proc_list_entries;
	out_proc_list = (struct srp_addr *)out->end_of_memb_join;
	out_failed_list = out_proc_list + out->proc_list_entries;

	for (i = 0; i < out->proc_list_entries; i++) {
		srp_addr_copy_endian_convert (&out_proc_list[i], &in_proc_list[i]);
	}
	for (i = 0; i < out->failed_list_entries; i++) {
		srp_addr_copy_endian_convert (&out_failed_list[i], &in_failed_list[i]);
	}
}

static void memb_commit_token_endian_convert (struct memb_commit_token *in, struct memb_commit_token *out)
{
	int i;
	struct srp_addr *in_addr = (struct srp_addr *)in->end_of_commit_token;
	struct srp_addr *out_addr = (struct srp_addr *)out->end_of_commit_token;
	struct memb_commit_token_memb_entry *in_memb_list;
	struct memb_commit_token_memb_entry *out_memb_list;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->token_seq = swab32 (in->token_seq);
	memb_ring_id_copy_endian_convert (&out->ring_id, &in->ring_id);
	out->retrans_flg = swab32 (in->retrans_flg);
	out->memb_index = swab32 (in->memb_index);
	out->addr_entries = swab32 (in->addr_entries);

	in_memb_list = (struct memb_commit_token_memb_entry *)(in_addr + out->addr_entries);
	out_memb_list = (struct memb_commit_token_memb_entry *)(out_addr + out->addr_entries);
	for (i = 0; i < out->addr_entries; i++) {
		srp_addr_copy_endian_convert (&out_addr[i], &in_addr[i]);

		/*
		 * Only convert the memb entry if it has been set
		 */
		if (in_memb_list[i].ring_id.rep.family != 0) {
			memb_ring_id_copy_endian_convert (
			    &out_memb_list[i].ring_id,
			    &in_memb_list[i].ring_id);
			out_memb_list[i].aru = swab32 (in_memb_list[i].aru);
			out_memb_list[i].high_delivered = swab32 (in_memb_list[i].high_delivered);
			out_memb_list[i].received_flg = swab32 (in_memb_list[i].received_flg);
		}
	}
}

static void orf_token_endian_convert (struct orf_token *in, struct orf_token *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->seq = swab32 (in->seq);
	out->token_seq = swab32 (in->token_seq);
	out->aru = swab32 (in->aru);
	memb_ring_id_copy_endian_convert (&out->ring_id, &in->ring_id);
	out->aru_addr = swab32(in->aru_addr);
	out->fcc = swab32 (in->fcc);
	out->backlog = swab32 (in->backlog);
	out->retrans_flg = swab32 (in->retrans_flg);
	out->rtr_list_entries = swab32 (in->rtr_list_entries);
	for (i = 0; i < out->rtr_list_entries; i++) {
		memb_ring_id_copy_endian_convert(&out->rtr_list[i].ring_id,
			&in->rtr_list[i].ring_id);
		out->rtr_list[i].seq = swab32 (in->rtr_list[i].seq);
	}
}

static void mcast_endian_convert (struct mcast *in, struct mcast *out)
{
	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->seq = swab32 (in->seq);
	out->this_seqno = swab32 (in->this_seqno);
	memb_ring_id_copy_endian_convert(&out->ring_id, &in->ring_id);
	out->node_id = swab32 (in->node_id);
	out->guarantee = swab32 (in->guarantee);
	srp_addr_copy_endian_convert (&out->system_from, &in->system_from);
}

static void memb_merge_detect_endian_convert (
	struct memb_merge_detect *in,
	struct memb_merge_detect *out)
{
	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	memb_ring_id_copy_endian_convert(&out->ring_id, &in->ring_id);
	srp_addr_copy_endian_convert (&out->system_from, &in->system_from);
}

static int message_handler_memb_join (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct memb_join *memb_join;
	struct memb_join *memb_join_convert = alloca (msg_len);
	int gather_entered;

	if (endian_conversion_needed) {
		memb_join = memb_join_convert;
		memb_join_endian_convert (msg, memb_join_convert);

	} else {
		memb_join = (struct memb_join *)msg;
	}

	if (instance->token_ring_id_seq < memb_join->ring_seq) {
		instance->token_ring_id_seq = memb_join->ring_seq;
	}
	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			gather_entered = memb_join_process (instance,
				memb_join);
			if (gather_entered == 0) {
				memb_state_gather_enter (instance);
			}
			break;

		case MEMB_STATE_GATHER:
			memb_join_process (instance, memb_join);
			break;
	
		case MEMB_STATE_COMMIT:
			if (memb_set_subset (&memb_join->system_from,
				1,
				instance->my_new_memb_list,
				instance->my_new_memb_entries) &&

				memb_join->ring_seq >= instance->my_ring_id.seq) {

				memb_join_process (instance, memb_join);
				memb_state_gather_enter (instance);
			}
			break;

		case MEMB_STATE_RECOVERY:
			if (memb_set_subset (&memb_join->system_from,
				1,
				instance->my_new_memb_list,
				instance->my_new_memb_entries) &&

				memb_join->ring_seq >= instance->my_ring_id.seq) {

				ring_state_restore (instance);

				memb_join_process (instance, memb_join);
				memb_state_gather_enter (instance);
			}
			break;
	}
	return (0);
}

static int message_handler_memb_commit_token (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct memb_commit_token *memb_commit_token_convert = alloca (msg_len);
	struct memb_commit_token *memb_commit_token;
	struct srp_addr sub[PROCESSOR_COUNT_MAX];
	int sub_entries;

	struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;

	if (endian_conversion_needed) {
		memb_commit_token = memb_commit_token_convert;
		memb_commit_token_endian_convert (msg, memb_commit_token);
	} else {
		memb_commit_token = (struct memb_commit_token *)msg;
	}
	addr = (struct srp_addr *)memb_commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + memb_commit_token->addr_entries);

	if (sq_lte_compare (memb_commit_token->token_seq,
		instance->my_commit_token_seq)) {
		/*
		 * discard token
		 */
		return (0);
	}
	instance->my_commit_token_seq = memb_commit_token->token_seq;


#ifdef TEST_DROP_COMMIT_TOKEN_PERCENTAGE
	if (random()%100 < TEST_DROP_COMMIT_TOKEN_PERCENTAGE) {
		return (0);
	}
#endif
	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			/* discard token */
			break;

		case MEMB_STATE_GATHER:
			memb_set_subtract (sub, &sub_entries,
				instance->my_proc_list, instance->my_proc_list_entries,
				instance->my_failed_list, instance->my_failed_list_entries);
			
			if (memb_set_equal (addr,
				memb_commit_token->addr_entries,
				sub,
				sub_entries) &&

				memb_commit_token->ring_id.seq > instance->my_ring_id.seq) {

				memb_state_commit_enter (instance, memb_commit_token);
			}
			break;

		case MEMB_STATE_COMMIT:
//			if (memcmp (&memb_commit_token->ring_id, &instance->my_ring_id,
//				sizeof (struct memb_ring_id)) == 0) {
			 if (memb_commit_token->ring_id.seq == instance->my_ring_id.seq) {
				memb_state_recovery_enter (instance, memb_commit_token);
			}
			break;

		case MEMB_STATE_RECOVERY:
			log_printf (instance->totemsrp_log_level_notice,
				"Sending initial ORF token\n");

			// TODO convert instead of initiate
			orf_token_send_initial (instance);
			reset_token_timeout (instance); // REVIEWED
			reset_token_retransmit_timeout (instance); // REVIEWED
			break;
	}
	return (0);
}

static int message_handler_token_hold_cancel (
	struct totemsrp_instance *instance,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct token_hold_cancel *token_hold_cancel = (struct token_hold_cancel *)msg;

	if (memcmp (&token_hold_cancel->ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id)) == 0) {

		instance->my_seq_unchanged = 0;
		if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id.addr[0])) {
			timer_function_token_retransmit_timeout (instance);
		}
	}
	return (0);
}

void main_deliver_fn (
	void *context,
	void *msg,
	int msg_len)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;
	struct message_header *message_header = (struct message_header *)msg;

	if (msg_len < sizeof (struct message_header)) {
		log_printf (instance->totemsrp_log_level_security, "Received message is too short...  ignoring %d.\n", msg_len);
		return;
	}

	/*
	 * Handle incoming message
	 */
	totemsrp_message_handlers.handler_functions[(int)message_header->type] (
		instance,
		msg,
		msg_len,
		message_header->endian_detector != ENDIAN_LOCAL);
}

void main_iface_change_fn (
	void *context,
	struct totem_ip_address *iface_addr,
	unsigned int iface_no)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;

	totemip_copy (&instance->my_id.addr[iface_no], iface_addr);
	assert (instance->my_id.addr[iface_no].nodeid);

	totemip_copy (&instance->my_memb_list[0].addr[iface_no], iface_addr);

	if (instance->iface_changes++ == 0) {
		memb_ring_id_create_or_load (instance, &instance->my_ring_id);
		log_printf (
			instance->totemsrp_log_level_notice,
			"Created or loaded sequence id %lld.%s for this ring.\n",
			instance->my_ring_id.seq,
			totemip_print (&instance->my_ring_id.rep));

	}
	if (instance->iface_changes >= instance->totem_config->interface_count) {
		memb_state_gather_enter (instance);
	}
}

void totemsrp_net_mtu_adjust (struct totem_config *totem_config) {
	totem_config->net_mtu -= sizeof (struct mcast);
}



