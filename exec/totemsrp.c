/*
 * Copyright (c) 2003-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
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

/*
 * The first version of this code was based upon Yair Amir's PhD thesis:
 *	http://www.cs.jhu.edu/~yairamir/phd.ps) (ch4,5).
 *
 * The current version of totemsrp implements the Totem protocol specified in:
 * 	http://citeseer.ist.psu.edu/amir95totem.html
 *
 * The deviations from the above published protocols are:
 * - encryption of message contents with nss
 * - authentication of meessage contents with SHA1/HMAC
 * - token hold mode where token doesn't rotate on unused ring - reduces cpu
 *   usage on 1.6ghz xeon from 35% to less then .1 % as measured by top
 */

#include <config.h>

#include <assert.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
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
#include <qb/qbutil.h>
#include <qb/qbloop.h>

#include <corosync/swab.h>
#include <corosync/sq.h>
#include <corosync/list.h>

#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>

#include "totemsrp.h"
#include "totemrrp.h"
#include "totemnet.h"

#include "cs_queue.h"

#define LOCALHOST_IP				inet_addr("127.0.0.1")
#define QUEUE_RTR_ITEMS_SIZE_MAX		16384 /* allow 16384 retransmit items */
#define RETRANS_MESSAGE_QUEUE_SIZE_MAX		16384 /* allow 500 messages to be queued */
#define RECEIVED_MESSAGE_QUEUE_SIZE_MAX		500 /* allow 500 messages to be queued */
#define MAXIOVS					5
#define RETRANSMIT_ENTRIES_MAX			30
#define TOKEN_SIZE_MAX				64000 /* bytes */
#define LEAVE_DUMMY_NODEID                      0

/*
 * Rollover handling:
 * SEQNO_START_MSG is the starting sequence number after a new configuration
 *	This should remain zero, unless testing overflow in which case
 *	0x7ffff000 and 0xfffff000 are good starting values.
 *
 * SEQNO_START_TOKEN is the starting sequence number after a new configuration
 *	for a token.  This should remain zero, unless testing overflow in which
 *	case 07fffff00 or 0xffffff00 are good starting values.
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
	MESSAGE_TYPE_ORF_TOKEN = 0,			/* Ordering, Reliability, Flow (ORF) control Token */
	MESSAGE_TYPE_MCAST = 1,				/* ring ordered multicast message */
	MESSAGE_TYPE_MEMB_MERGE_DETECT = 2,	/* merge rings if there are available rings */
	MESSAGE_TYPE_MEMB_JOIN = 3,			/* membership join message */
	MESSAGE_TYPE_MEMB_COMMIT_TOKEN = 4,	/* membership commit token */
	MESSAGE_TYPE_TOKEN_HOLD_CANCEL = 5,	/* cancel the holding of the token */
};

enum encapsulation_type {
	MESSAGE_ENCAPSULATED = 1,
	MESSAGE_NOT_ENCAPSULATED = 2
};

/*
 * New membership algorithm local variables
 */
struct consensus_list_item {
	struct srp_addr addr;
	int set;
};


struct token_callback_instance {
	struct list_head list;
	int (*callback_fn) (enum totem_callback_token_type type, const void *);
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
 *	struct srp_addr addr[PROCESSOR_COUNT_MAX];
 *	struct memb_commit_token_memb_entry memb_list[PROCESSOR_COUNT_MAX];
 */
}__attribute__((packed));

struct message_item {
	struct mcast *mcast;
	unsigned int msg_len;
};

struct sort_queue_item {
	struct mcast *mcast;
	unsigned int msg_len;
};

enum memb_state {
	MEMB_STATE_OPERATIONAL = 1,
	MEMB_STATE_GATHER = 2,
	MEMB_STATE_COMMIT = 3,
	MEMB_STATE_RECOVERY = 4
};

struct totemsrp_instance {
	int iface_changes;

	int failed_to_recv;

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

	struct srp_addr my_left_memb_list[PROCESSOR_COUNT_MAX];

	unsigned int my_leave_memb_list[PROCESSOR_COUNT_MAX];
	
	int my_proc_list_entries;

	int my_failed_list_entries;

	int my_new_memb_entries;

	int my_trans_memb_entries;

	int my_memb_entries;

	int my_deliver_memb_entries;

	int my_left_memb_entries;
	
	int my_leave_memb_entries;

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
	struct cs_queue new_message_queue;

	struct cs_queue new_message_queue_trans;

	struct cs_queue retrans_message_queue;

	struct sq regular_sort_queue;

	struct sq recovery_sort_queue;

	/*
	 * Received up to and including
	 */
	unsigned int my_aru;

	unsigned int my_high_delivered;

	struct list_head token_callback_received_listhead;

	struct list_head token_callback_sent_listhead;

	char orf_token_retransmit[TOKEN_SIZE_MAX];

	int orf_token_retransmit_size;

	unsigned int my_token_seq;

	/*
	 * Timers
	 */
	qb_loop_timer_handle timer_pause_timeout;

	qb_loop_timer_handle timer_orf_token_timeout;

	qb_loop_timer_handle timer_orf_token_retransmit_timeout;

	qb_loop_timer_handle timer_orf_token_hold_retransmit_timeout;

	qb_loop_timer_handle timer_merge_detect_timeout;

	qb_loop_timer_handle memb_timer_state_gather_join_timeout;

	qb_loop_timer_handle memb_timer_state_gather_consensus_timeout;

	qb_loop_timer_handle memb_timer_state_commit_timeout;

	qb_loop_timer_handle timer_heartbeat_timeout;

	/*
	 * Function and data used to log messages
	 */
	int totemsrp_log_level_security;

	int totemsrp_log_level_error;

	int totemsrp_log_level_warning;

	int totemsrp_log_level_notice;

	int totemsrp_log_level_debug;

	int totemsrp_log_level_trace;

	int totemsrp_subsys_id;

	void (*totemsrp_log_printf) (
		int level,
		int sybsys,
		const char *function,
		const char *file,
		int line,
		const char *format, ...)__attribute__((format(printf, 6, 7)));;

	enum memb_state memb_state;

//TODO	struct srp_addr next_memb;

	qb_loop_t *totemsrp_poll_handle;

	struct totem_ip_address mcast_address;

	void (*totemsrp_deliver_fn) (
		unsigned int nodeid,
		const void *msg,
		unsigned int msg_len,
		int endian_conversion_required);

	void (*totemsrp_confchg_fn) (
		enum totem_configuration_type configuration_type,
		const unsigned int *member_list, size_t member_list_entries,
		const unsigned int *left_list, size_t left_list_entries,
		const unsigned int *joined_list, size_t joined_list_entries,
		const struct memb_ring_id *ring_id);

        void (*totemsrp_service_ready_fn) (void);

	void (*totemsrp_waiting_trans_ack_cb_fn) (
		int waiting_trans_ack);

	void (*memb_ring_id_create_or_load) (
		struct memb_ring_id *memb_ring_id,
		const struct totem_ip_address *addr);

	void (*memb_ring_id_store) (
		const struct memb_ring_id *memb_ring_id,
		const struct totem_ip_address *addr);

	int global_seqno;

	int my_token_held;

	unsigned long long token_ring_id_seq;

	unsigned int last_released;

	unsigned int set_aru;

	int old_ring_state_saved;

	int old_ring_state_aru;

	unsigned int old_ring_state_high_seq_received;

	unsigned int my_last_seq;

	struct timeval tv_old;

	void *totemrrp_context;

	struct totem_config *totem_config;

	unsigned int use_heartbeat;

	unsigned int my_trc;

	unsigned int my_pbl;

	unsigned int my_cbl;

	uint64_t pause_timestamp;

	struct memb_commit_token *commit_token;

	totemsrp_stats_t stats;

	uint32_t orf_token_discard;

	uint32_t originated_orf_token;

	uint32_t threaded_mode_enabled;

	uint32_t waiting_trans_ack;

	int 	flushing;
	
	void * token_recv_event_handle;
	void * token_sent_event_handle;
	char commit_token_storage[40000];
};

struct message_handlers {
	int count;
	int (*handler_functions[6]) (
		struct totemsrp_instance *instance,
		const void *msg,
		size_t msg_len,
		int endian_conversion_needed);
};

enum gather_state_from {
	TOTEMSRP_GSFROM_CONSENSUS_TIMEOUT = 0,
	TOTEMSRP_GSFROM_GATHER_MISSING1 = 1,
	TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_OPERATIONAL_STATE = 2,
	TOTEMSRP_GSFROM_THE_CONSENSUS_TIMEOUT_EXPIRED = 3,
	TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_COMMIT_STATE = 4,
	TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_RECOVERY_STATE = 5,
	TOTEMSRP_GSFROM_FAILED_TO_RECEIVE = 6,
	TOTEMSRP_GSFROM_FOREIGN_MESSAGE_IN_OPERATIONAL_STATE = 7,
	TOTEMSRP_GSFROM_FOREIGN_MESSAGE_IN_GATHER_STATE = 8,
	TOTEMSRP_GSFROM_MERGE_DURING_OPERATIONAL_STATE = 9,
	TOTEMSRP_GSFROM_MERGE_DURING_GATHER_STATE = 10,
	TOTEMSRP_GSFROM_MERGE_DURING_JOIN = 11,
	TOTEMSRP_GSFROM_JOIN_DURING_OPERATIONAL_STATE = 12,
	TOTEMSRP_GSFROM_JOIN_DURING_COMMIT_STATE = 13,
	TOTEMSRP_GSFROM_JOIN_DURING_RECOVERY = 14,
	TOTEMSRP_GSFROM_INTERFACE_CHANGE = 15,
	TOTEMSRP_GSFROM_MAX = TOTEMSRP_GSFROM_INTERFACE_CHANGE,
};

const char* gather_state_from_desc [] = {
	[TOTEMSRP_GSFROM_CONSENSUS_TIMEOUT] = "consensus timeout",
	[TOTEMSRP_GSFROM_GATHER_MISSING1] = "MISSING",
	[TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_OPERATIONAL_STATE] = "The token was lost in the OPERATIONAL state.",
	[TOTEMSRP_GSFROM_THE_CONSENSUS_TIMEOUT_EXPIRED] = "The consensus timeout expired.",
	[TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_COMMIT_STATE] = "The token was lost in the COMMIT state.",
	[TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_RECOVERY_STATE] = "The token was lost in the RECOVERY state.",
	[TOTEMSRP_GSFROM_FAILED_TO_RECEIVE] = "failed to receive",
	[TOTEMSRP_GSFROM_FOREIGN_MESSAGE_IN_OPERATIONAL_STATE] = "foreign message in operational state",
	[TOTEMSRP_GSFROM_FOREIGN_MESSAGE_IN_GATHER_STATE] = "foreign message in gather state",
	[TOTEMSRP_GSFROM_MERGE_DURING_OPERATIONAL_STATE] = "merge during operational state",
	[TOTEMSRP_GSFROM_MERGE_DURING_GATHER_STATE] = "merge during gather state",
	[TOTEMSRP_GSFROM_MERGE_DURING_JOIN] = "merge during join",
	[TOTEMSRP_GSFROM_JOIN_DURING_OPERATIONAL_STATE] = "join during operational state",
	[TOTEMSRP_GSFROM_JOIN_DURING_COMMIT_STATE] = "join during commit state",
	[TOTEMSRP_GSFROM_JOIN_DURING_RECOVERY] = "join during recovery",
	[TOTEMSRP_GSFROM_INTERFACE_CHANGE] = "interface change",
};

/*
 * forward decls
 */
static int message_handler_orf_token (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed);

static int message_handler_mcast (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed);

static int message_handler_memb_merge_detect (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed);

static int message_handler_memb_join (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed);

static int message_handler_memb_commit_token (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed);

static int message_handler_token_hold_cancel (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed);

static void totemsrp_instance_initialize (struct totemsrp_instance *instance);

static unsigned int main_msgs_missing (void);

static void main_token_seqid_get (
	const void *msg,
	unsigned int *seqid,
	unsigned int *token_is);

static void srp_addr_copy (struct srp_addr *dest, const struct srp_addr *src);

static void srp_addr_to_nodeid (
	unsigned int *nodeid_out,
	struct srp_addr *srp_addr_in,
	unsigned int entries);

static int srp_addr_equal (const struct srp_addr *a, const struct srp_addr *b);

static void memb_leave_message_send (struct totemsrp_instance *instance);

static void token_callbacks_execute (struct totemsrp_instance *instance, enum totem_callback_token_type type);
static void memb_state_gather_enter (struct totemsrp_instance *instance, enum gather_state_from gather_from);
static void messages_deliver_to_app (struct totemsrp_instance *instance, int skip, unsigned int end_point);
static int orf_token_mcast (struct totemsrp_instance *instance, struct orf_token *oken,
	int fcc_mcasts_allowed);
static void messages_free (struct totemsrp_instance *instance, unsigned int token_aru);

static void memb_ring_id_set (struct totemsrp_instance *instance,
	const struct memb_ring_id *ring_id);
static void target_set_completed (void *context);
static void memb_state_commit_token_update (struct totemsrp_instance *instance);
static void memb_state_commit_token_target_set (struct totemsrp_instance *instance);
static int memb_state_commit_token_send (struct totemsrp_instance *instance);
static int memb_state_commit_token_send_recovery (struct totemsrp_instance *instance, struct memb_commit_token *memb_commit_token);
static void memb_state_commit_token_create (struct totemsrp_instance *instance);
static int token_hold_cancel_send (struct totemsrp_instance *instance);
static void orf_token_endian_convert (const struct orf_token *in, struct orf_token *out);
static void memb_commit_token_endian_convert (const struct memb_commit_token *in, struct memb_commit_token *out);
static void memb_join_endian_convert (const struct memb_join *in, struct memb_join *out);
static void mcast_endian_convert (const struct mcast *in, struct mcast *out);
static void memb_merge_detect_endian_convert (
	const struct memb_merge_detect *in,
	struct memb_merge_detect *out);
static void srp_addr_copy_endian_convert (struct srp_addr *out, const struct srp_addr *in);
static void timer_function_orf_token_timeout (void *data);
static void timer_function_pause_timeout (void *data);
static void timer_function_heartbeat_timeout (void *data);
static void timer_function_token_retransmit_timeout (void *data);
static void timer_function_token_hold_retransmit_timeout (void *data);
static void timer_function_merge_detect_timeout (void *data);
static void *totemsrp_buffer_alloc (struct totemsrp_instance *instance);
static void totemsrp_buffer_release (struct totemsrp_instance *instance, void *ptr);
static const char* gsfrom_to_msg(enum gather_state_from gsfrom);

void main_deliver_fn (
	void *context,
	const void *msg,
	unsigned int msg_len);

void main_iface_change_fn (
	void *context,
	const struct totem_ip_address *iface_address,
	unsigned int iface_no);

struct message_handlers totemsrp_message_handlers = {
	6,
	{
		message_handler_orf_token,            /* MESSAGE_TYPE_ORF_TOKEN */
		message_handler_mcast,                /* MESSAGE_TYPE_MCAST */
		message_handler_memb_merge_detect,    /* MESSAGE_TYPE_MEMB_MERGE_DETECT */
		message_handler_memb_join,            /* MESSAGE_TYPE_MEMB_JOIN */
		message_handler_memb_commit_token,    /* MESSAGE_TYPE_MEMB_COMMIT_TOKEN */
		message_handler_token_hold_cancel     /* MESSAGE_TYPE_TOKEN_HOLD_CANCEL */
	}
};

#define log_printf(level, format, args...)		\
do {							\
	instance->totemsrp_log_printf (			\
		level, instance->totemsrp_subsys_id,	\
		__FUNCTION__, __FILE__, __LINE__,	\
		format, ##args);			\
} while (0);
#define LOGSYS_PERROR(err_num, level, fmt, args...)						\
do {												\
	char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];						\
	const char *_error_ptr = qb_strerror_r(err_num, _error_str, sizeof(_error_str));	\
        instance->totemsrp_log_printf (								\
		level, instance->totemsrp_subsys_id,						\
                __FUNCTION__, __FILE__, __LINE__,						\
		fmt ": %s (%d)\n", ##args, _error_ptr, err_num);				\
	} while(0)

static const char* gsfrom_to_msg(enum gather_state_from gsfrom)
{
	if (gsfrom <= TOTEMSRP_GSFROM_MAX) {
		return gather_state_from_desc[gsfrom];
	}
	else {
		return "UNKNOWN";
	}
}

static void totemsrp_instance_initialize (struct totemsrp_instance *instance)
{
	memset (instance, 0, sizeof (struct totemsrp_instance));

	list_init (&instance->token_callback_received_listhead);

	list_init (&instance->token_callback_sent_listhead);

	instance->my_received_flg = 1;

	instance->my_token_seq = SEQNO_START_TOKEN - 1;

	instance->memb_state = MEMB_STATE_OPERATIONAL;

	instance->set_aru = -1;

	instance->my_aru = SEQNO_START_MSG;

	instance->my_high_seq_received = SEQNO_START_MSG;

	instance->my_high_delivered = SEQNO_START_MSG;

	instance->orf_token_discard = 0;

	instance->originated_orf_token = 0;

	instance->commit_token = (struct memb_commit_token *)instance->commit_token_storage;

	instance->my_id.no_addrs = INTERFACE_MAX;

	instance->waiting_trans_ack = 1;
}

static void main_token_seqid_get (
	const void *msg,
	unsigned int *seqid,
	unsigned int *token_is)
{
	const struct orf_token *token = msg;

	*seqid = 0;
	*token_is = 0;
	if (token->header.type == MESSAGE_TYPE_ORF_TOKEN) {
		*seqid = token->token_seq;
		*token_is = 1;
	}
}

static unsigned int main_msgs_missing (void)
{
// TODO
	return (0);
}

static int pause_flush (struct totemsrp_instance *instance)
{
	uint64_t now_msec;
	uint64_t timestamp_msec;
	int res = 0;

        now_msec = (qb_util_nano_current_get () / QB_TIME_NS_IN_MSEC);
        timestamp_msec = instance->pause_timestamp / QB_TIME_NS_IN_MSEC;

	if ((now_msec - timestamp_msec) > (instance->totem_config->token_timeout / 2)) {
		log_printf (instance->totemsrp_log_level_notice,
			"Process pause detected for %d ms, flushing membership messages.", (unsigned int)(now_msec - timestamp_msec));
		/*
		 * -1 indicates an error from recvmsg
		 */
		do {
			res = totemrrp_mcast_recv_empty (instance->totemrrp_context);
		} while (res == -1);
	}
	return (res);
}

static int token_event_stats_collector (enum totem_callback_token_type type, const void *void_instance)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)void_instance;
	uint32_t time_now;
	unsigned long long nano_secs = qb_util_nano_current_get ();

	time_now = (nano_secs / QB_TIME_NS_IN_MSEC);

	if (type == TOTEM_CALLBACK_TOKEN_RECEIVED) {
		/* incr latest token the index */
		if (instance->stats.latest_token == (TOTEM_TOKEN_STATS_MAX - 1))
			instance->stats.latest_token = 0;
		else
			instance->stats.latest_token++;

		if (instance->stats.earliest_token == instance->stats.latest_token) {
			/* we have filled up the array, start overwriting */
			if (instance->stats.earliest_token == (TOTEM_TOKEN_STATS_MAX - 1))
				instance->stats.earliest_token = 0;
			else
				instance->stats.earliest_token++;

			instance->stats.token[instance->stats.earliest_token].rx = 0;
			instance->stats.token[instance->stats.earliest_token].tx = 0;
			instance->stats.token[instance->stats.earliest_token].backlog_calc = 0;
		}

		instance->stats.token[instance->stats.latest_token].rx = time_now;
		instance->stats.token[instance->stats.latest_token].tx = 0; /* in case we drop the token */
	} else {
		instance->stats.token[instance->stats.latest_token].tx = time_now;
	}
	return 0;
}

/*
 * Exported interfaces
 */
int totemsrp_initialize (
	qb_loop_t *poll_handle,
	void **srp_context,
	struct totem_config *totem_config,
	totemmrp_stats_t *stats,

	void (*deliver_fn) (
		unsigned int nodeid,
		const void *msg,
		unsigned int msg_len,
		int endian_conversion_required),

	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		const unsigned int *member_list, size_t member_list_entries,
		const unsigned int *left_list, size_t left_list_entries,
		const unsigned int *joined_list, size_t joined_list_entries,
		const struct memb_ring_id *ring_id),
	void (*waiting_trans_ack_cb_fn) (
		int waiting_trans_ack))
{
	struct totemsrp_instance *instance;
	int res;

	instance = malloc (sizeof (struct totemsrp_instance));
	if (instance == NULL) {
		goto error_exit;
	}

	totemsrp_instance_initialize (instance);

	instance->totemsrp_waiting_trans_ack_cb_fn = waiting_trans_ack_cb_fn;
	instance->totemsrp_waiting_trans_ack_cb_fn (1);

	stats->srp = &instance->stats;
	instance->stats.latest_token = 0;
	instance->stats.earliest_token = 0;

	instance->totem_config = totem_config;

	/*
	 * Configure logging
	 */
	instance->totemsrp_log_level_security = totem_config->totem_logging_configuration.log_level_security;
	instance->totemsrp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemsrp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemsrp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemsrp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemsrp_log_level_trace = totem_config->totem_logging_configuration.log_level_trace;
	instance->totemsrp_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemsrp_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	 * Configure totem store and load functions
	 */
	instance->memb_ring_id_create_or_load = totem_config->totem_memb_ring_id_create_or_load;
	instance->memb_ring_id_store = totem_config->totem_memb_ring_id_store;

	/*
	 * Initialize local variables for totemsrp
	 */
	totemip_copy (&instance->mcast_address, &totem_config->interfaces[0].mcast_addr);

	/*
	 * Display totem configuration
	 */
	log_printf (instance->totemsrp_log_level_debug,
		"Token Timeout (%d ms) retransmit timeout (%d ms)",
		totem_config->token_timeout, totem_config->token_retransmit_timeout);
	log_printf (instance->totemsrp_log_level_debug,
		"token hold (%d ms) retransmits before loss (%d retrans)",
		totem_config->token_hold_timeout, totem_config->token_retransmits_before_loss_const);
	log_printf (instance->totemsrp_log_level_debug,
		"join (%d ms) send_join (%d ms) consensus (%d ms) merge (%d ms)",
		totem_config->join_timeout,
		totem_config->send_join_timeout,
		totem_config->consensus_timeout,

		totem_config->merge_timeout);
	log_printf (instance->totemsrp_log_level_debug,
		"downcheck (%d ms) fail to recv const (%d msgs)",
		totem_config->downcheck_timeout, totem_config->fail_to_recv_const);
	log_printf (instance->totemsrp_log_level_debug,
		"seqno unchanged const (%d rotations) Maximum network MTU %d", totem_config->seqno_unchanged_const, totem_config->net_mtu);

	log_printf (instance->totemsrp_log_level_debug,
		"window size per rotation (%d messages) maximum messages per rotation (%d messages)",
		totem_config->window_size, totem_config->max_messages);

	log_printf (instance->totemsrp_log_level_debug,
		"missed count const (%d messages)",
		totem_config->miss_count_const);

	log_printf (instance->totemsrp_log_level_debug,
		"send threads (%d threads)", totem_config->threads);
	log_printf (instance->totemsrp_log_level_debug,
		"RRP token expired timeout (%d ms)",
		totem_config->rrp_token_expired_timeout);
	log_printf (instance->totemsrp_log_level_debug,
		"RRP token problem counter (%d ms)",
		totem_config->rrp_problem_count_timeout);
	log_printf (instance->totemsrp_log_level_debug,
		"RRP threshold (%d problem count)",
		totem_config->rrp_problem_count_threshold);
	log_printf (instance->totemsrp_log_level_debug,
		"RRP multicast threshold (%d problem count)",
		totem_config->rrp_problem_count_mcast_threshold);
	log_printf (instance->totemsrp_log_level_debug,
		"RRP automatic recovery check timeout (%d ms)",
		totem_config->rrp_autorecovery_check_timeout);
	log_printf (instance->totemsrp_log_level_debug,
		"RRP mode set to %s.", instance->totem_config->rrp_mode);

	log_printf (instance->totemsrp_log_level_debug,
		"heartbeat_failures_allowed (%d)", totem_config->heartbeat_failures_allowed);
	log_printf (instance->totemsrp_log_level_debug,
		"max_network_delay (%d ms)", totem_config->max_network_delay);


	cs_queue_init (&instance->retrans_message_queue, RETRANS_MESSAGE_QUEUE_SIZE_MAX,
		sizeof (struct message_item), instance->threaded_mode_enabled);

	sq_init (&instance->regular_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	sq_init (&instance->recovery_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	instance->totemsrp_poll_handle = poll_handle;

	instance->totemsrp_deliver_fn = deliver_fn;

	instance->totemsrp_confchg_fn = confchg_fn;
	instance->use_heartbeat = 1;

	timer_function_pause_timeout (instance);

	if ( totem_config->heartbeat_failures_allowed == 0 ) {
		log_printf (instance->totemsrp_log_level_debug,
			"HeartBeat is Disabled. To enable set heartbeat_failures_allowed > 0");
		instance->use_heartbeat = 0;
	}

	if (instance->use_heartbeat) {
		instance->heartbeat_timeout
			= (totem_config->heartbeat_failures_allowed) * totem_config->token_retransmit_timeout
				+ totem_config->max_network_delay;

		if (instance->heartbeat_timeout >= totem_config->token_timeout) {
			log_printf (instance->totemsrp_log_level_debug,
				"total heartbeat_timeout (%d ms) is not less than token timeout (%d ms)",
				instance->heartbeat_timeout,
				totem_config->token_timeout);
			log_printf (instance->totemsrp_log_level_debug,
				"heartbeat_timeout = heartbeat_failures_allowed * token_retransmit_timeout + max_network_delay");
			log_printf (instance->totemsrp_log_level_debug,
				"heartbeat timeout should be less than the token timeout. Heartbeat is disabled!!");
			instance->use_heartbeat = 0;
		}
		else {
			log_printf (instance->totemsrp_log_level_debug,
				"total heartbeat_timeout (%d ms)", instance->heartbeat_timeout);
		}
	}

	res = totemrrp_initialize (
		poll_handle,
		&instance->totemrrp_context,
		totem_config,
		stats->srp,
		instance,
		main_deliver_fn,
		main_iface_change_fn,
		main_token_seqid_get,
		main_msgs_missing,
		target_set_completed);
	if (res == -1) {
		goto error_exit;
	}

	/*
	 * Must have net_mtu adjusted by totemrrp_initialize first
	 */
	cs_queue_init (&instance->new_message_queue,
		MESSAGE_QUEUE_MAX,
		sizeof (struct message_item), instance->threaded_mode_enabled);

	cs_queue_init (&instance->new_message_queue_trans,
		MESSAGE_QUEUE_MAX,
		sizeof (struct message_item), instance->threaded_mode_enabled);

	totemsrp_callback_token_create (instance,
		&instance->token_recv_event_handle,
		TOTEM_CALLBACK_TOKEN_RECEIVED,
		0,
		token_event_stats_collector,
		instance);
	totemsrp_callback_token_create (instance,
		&instance->token_sent_event_handle,
		TOTEM_CALLBACK_TOKEN_SENT,
		0,
		token_event_stats_collector,
		instance);
	*srp_context = instance;
	return (0);

error_exit:
	return (-1);
}

void totemsrp_finalize (
	void *srp_context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;


	memb_leave_message_send (instance);
	totemrrp_finalize (instance->totemrrp_context);
	cs_queue_free (&instance->new_message_queue);
	cs_queue_free (&instance->new_message_queue_trans);
	cs_queue_free (&instance->retrans_message_queue);
	sq_free (&instance->regular_sort_queue);
	sq_free (&instance->recovery_sort_queue);
	free (instance);
}

/*
 * Return configured interfaces. interfaces is array of totem_ip addresses allocated by caller,
 * with interaces_size number of items. iface_count is final number of interfaces filled by this
 * function.
 *
 * Function returns 0 on success, otherwise if interfaces array is not big enough, -2 is returned,
 * and if interface was not found, -1 is returned.
 */
int totemsrp_ifaces_get (
	void *srp_context,
	unsigned int nodeid,
	struct totem_ip_address *interfaces,
	unsigned int interfaces_size,
	char ***status,
	unsigned int *iface_count)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;
	int res = 0;
	unsigned int found = 0;
	unsigned int i;

	for (i = 0; i < instance->my_memb_entries; i++) {
		if (instance->my_memb_list[i].addr[0].nodeid == nodeid) {
			found = 1;
			break;
		}
	}

	if (found) {
		*iface_count = instance->totem_config->interface_count;

		if (interfaces_size >= *iface_count) {
			memcpy (interfaces, instance->my_memb_list[i].addr,
				sizeof (struct totem_ip_address) * *iface_count);
		} else {
			res = -2;
		}

		goto finish;
	}

	for (i = 0; i < instance->my_left_memb_entries; i++) {
		if (instance->my_left_memb_list[i].addr[0].nodeid == nodeid) {
			found = 1;
			break;
		}
	}

	if (found) {
		*iface_count = instance->totem_config->interface_count;

		if (interfaces_size >= *iface_count) {
			memcpy (interfaces, instance->my_left_memb_list[i].addr,
				sizeof (struct totem_ip_address) * *iface_count);
		} else {
			res = -2;
		}
	} else {
		res = -1;
	}

finish:
	totemrrp_ifaces_get (instance->totemrrp_context, status, NULL);
	return (res);
}

int totemsrp_crypto_set (
	void *srp_context,
	const char *cipher_type,
	const char *hash_type)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;
	int res;

	res = totemrrp_crypto_set(instance->totemrrp_context, cipher_type, hash_type);

	return (res);
}


unsigned int totemsrp_my_nodeid_get (
	void *srp_context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;
	unsigned int res;

	res = instance->totem_config->interfaces[0].boundto.nodeid;

	return (res);
}

int totemsrp_my_family_get (
	void *srp_context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;
	int res;

	res = instance->totem_config->interfaces[0].boundto.family;

	return (res);
}


int totemsrp_ring_reenable (
        void *srp_context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;

	totemrrp_ring_reenable (instance->totemrrp_context,
		instance->totem_config->interface_count);

	return (0);
}


/*
 * Set operations for use by the membership algorithm
 */
static int srp_addr_equal (const struct srp_addr *a, const struct srp_addr *b)
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

static void srp_addr_copy (struct srp_addr *dest, const struct srp_addr *src)
{
	unsigned int i;

	dest->no_addrs = src->no_addrs;

	for (i = 0; i < INTERFACE_MAX; i++) {
		totemip_copy (&dest->addr[i], &src->addr[i]);
	}
}

static void srp_addr_to_nodeid (
	unsigned int *nodeid_out,
	struct srp_addr *srp_addr_in,
	unsigned int entries)
{
	unsigned int i;

	for (i = 0; i < entries; i++) {
		nodeid_out[i] = srp_addr_in[i].addr[0].nodeid;
	}
}

static void srp_addr_copy_endian_convert (struct srp_addr *out, const struct srp_addr *in)
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
	const struct srp_addr *addr)
{
	int found = 0;
	int i;

	if (addr->addr[0].nodeid == LEAVE_DUMMY_NODEID)
	        return;

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
	const struct srp_addr *addr)
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

	if (agreed && instance->failed_to_recv == 1) {
		/*
		 * Both nodes agreed on our failure. We don't care how many proc list items left because we
		 * will create single ring anyway.
		 */

		 return (agreed);
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
	const struct srp_addr *subset, int subset_entries,
	const struct srp_addr *fullset, int fullset_entries)
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
	const struct srp_addr *subset, int subset_entries,
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
			srp_addr_copy (&fullset[*fullset_entries], &subset[i]);
			*fullset_entries = *fullset_entries + 1;
		}
		found = 0;
	}
	return;
}

static void memb_set_and_with_ring_id (
	struct srp_addr *set1,
	struct memb_ring_id *set1_ring_ids,
	int set1_entries,
	struct srp_addr *set2,
	int set2_entries,
	struct memb_ring_id *old_ring_id,
	struct srp_addr *and,
	int *and_entries)
{
	int i;
	int j;
	int found = 0;

	*and_entries = 0;

	for (i = 0; i < set2_entries; i++) {
		for (j = 0; j < set1_entries; j++) {
			if (srp_addr_equal (&set1[j], &set2[i])) {
				if (memcmp (&set1_ring_ids[j], old_ring_id, sizeof (struct memb_ring_id)) == 0) {
					found = 1;
				}
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
		printf ("Address %d with %d rings\n", i, list[i].no_addrs);
		for (j = 0; j < list[i].no_addrs; j++) {
			printf ("\tiface %d %s\n", j, totemip_print (&list[i].addr[j]));
			printf ("\tfamily %d\n", list[i].addr[j].family);
		}
	}
}
#endif
static void my_leave_memb_clear(
        struct totemsrp_instance *instance)
{
        memset(instance->my_leave_memb_list, 0, sizeof(instance->my_leave_memb_list));
        instance->my_leave_memb_entries = 0;
}

static unsigned int my_leave_memb_match(
        struct totemsrp_instance *instance,
        unsigned int nodeid)
{
        int i;
        unsigned int ret = 0;

        for (i = 0; i < instance->my_leave_memb_entries; i++){
                if (instance->my_leave_memb_list[i] ==  nodeid){
                        ret = nodeid;
                        break;
                }
        }
        return ret;
}

static void my_leave_memb_set(
        struct totemsrp_instance *instance,
        unsigned int nodeid)
{
        int i, found = 0;
        for (i = 0; i < instance->my_leave_memb_entries; i++){
                if (instance->my_leave_memb_list[i] ==  nodeid){
                        found = 1;
                        break;
                }
        }
        if (found == 1) {
                return;
        }
        if (instance->my_leave_memb_entries < (PROCESSOR_COUNT_MAX - 1)) {
                instance->my_leave_memb_list[instance->my_leave_memb_entries] = nodeid;
                instance->my_leave_memb_entries++;
        } else {
                log_printf (instance->totemsrp_log_level_warning,
                        "Cannot set LEAVE nodeid=%d", nodeid);
        }
}


static void *totemsrp_buffer_alloc (struct totemsrp_instance *instance)
{
	assert (instance != NULL);
	return totemrrp_buffer_alloc (instance->totemrrp_context);
}

static void totemsrp_buffer_release (struct totemsrp_instance *instance, void *ptr)
{
	assert (instance != NULL);
	totemrrp_buffer_release (instance->totemrrp_context, ptr);
}

static void reset_token_retransmit_timeout (struct totemsrp_instance *instance)
{
	int32_t res;

	qb_loop_timer_del (instance->totemsrp_poll_handle,
		instance->timer_orf_token_retransmit_timeout);
	res = qb_loop_timer_add (instance->totemsrp_poll_handle,
		QB_LOOP_MED,
		instance->totem_config->token_retransmit_timeout*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		timer_function_token_retransmit_timeout,
		&instance->timer_orf_token_retransmit_timeout);
	if (res != 0) {
		log_printf(instance->totemsrp_log_level_error, "reset_token_retransmit_timeout - qb_loop_timer_add error : %d", res);
	}

}

static void start_merge_detect_timeout (struct totemsrp_instance *instance)
{
	int32_t res;

	if (instance->my_merge_detect_timeout_outstanding == 0) {
		res = qb_loop_timer_add (instance->totemsrp_poll_handle,
			QB_LOOP_MED,
			instance->totem_config->merge_timeout*QB_TIME_NS_IN_MSEC,
			(void *)instance,
			timer_function_merge_detect_timeout,
			&instance->timer_merge_detect_timeout);
		if (res != 0) {
			log_printf(instance->totemsrp_log_level_error, "start_merge_detect_timeout - qb_loop_timer_add error : %d", res);
		}

		instance->my_merge_detect_timeout_outstanding = 1;
	}
}

static void cancel_merge_detect_timeout (struct totemsrp_instance *instance)
{
	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->timer_merge_detect_timeout);
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
		memcpy (&instance->my_old_ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id));
		instance->old_ring_state_aru = instance->my_aru;
		instance->old_ring_state_high_seq_received = instance->my_high_seq_received;
		log_printf (instance->totemsrp_log_level_debug,
			"Saving state aru %x high seq received %x",
			instance->my_aru, instance->my_high_seq_received);
	}
}

static void old_ring_state_restore (struct totemsrp_instance *instance)
{
	instance->my_aru = instance->old_ring_state_aru;
	instance->my_high_seq_received = instance->old_ring_state_high_seq_received;
	log_printf (instance->totemsrp_log_level_debug,
		"Restoring instance->my_aru %x my high seq received %x",
		instance->my_aru, instance->my_high_seq_received);
}

static void old_ring_state_reset (struct totemsrp_instance *instance)
{
	log_printf (instance->totemsrp_log_level_debug,
		"Resetting old ring state");
	instance->old_ring_state_saved = 0;
}

static void reset_pause_timeout (struct totemsrp_instance *instance)
{
	int32_t res;

	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->timer_pause_timeout);
	res = qb_loop_timer_add (instance->totemsrp_poll_handle,
		QB_LOOP_MED,
		instance->totem_config->token_timeout * QB_TIME_NS_IN_MSEC / 5,
		(void *)instance,
		timer_function_pause_timeout,
		&instance->timer_pause_timeout);
	if (res != 0) {
		log_printf(instance->totemsrp_log_level_error, "reset_pause_timeout - qb_loop_timer_add error : %d", res);
	}
}

static void reset_token_timeout (struct totemsrp_instance *instance) {
	int32_t res;

	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->timer_orf_token_timeout);
	res = qb_loop_timer_add (instance->totemsrp_poll_handle,
		QB_LOOP_MED,
		instance->totem_config->token_timeout*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		timer_function_orf_token_timeout,
		&instance->timer_orf_token_timeout);
	if (res != 0) {
		log_printf(instance->totemsrp_log_level_error, "reset_token_timeout - qb_loop_timer_add error : %d", res);
	}
}

static void reset_heartbeat_timeout (struct totemsrp_instance *instance) {
	int32_t res;

        qb_loop_timer_del (instance->totemsrp_poll_handle, instance->timer_heartbeat_timeout);
        res = qb_loop_timer_add (instance->totemsrp_poll_handle,
		QB_LOOP_MED,
                instance->heartbeat_timeout*QB_TIME_NS_IN_MSEC,
                (void *)instance,
                timer_function_heartbeat_timeout,
                &instance->timer_heartbeat_timeout);
	if (res != 0) {
		log_printf(instance->totemsrp_log_level_error, "reset_heartbeat_timeout - qb_loop_timer_add error : %d", res);
	}
}


static void cancel_token_timeout (struct totemsrp_instance *instance) {
	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->timer_orf_token_timeout);
}

static void cancel_heartbeat_timeout (struct totemsrp_instance *instance) {
	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->timer_heartbeat_timeout);
}

static void cancel_token_retransmit_timeout (struct totemsrp_instance *instance)
{
	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->timer_orf_token_retransmit_timeout);
}

static void start_token_hold_retransmit_timeout (struct totemsrp_instance *instance)
{
	int32_t res;

	res = qb_loop_timer_add (instance->totemsrp_poll_handle,
		QB_LOOP_MED,
		instance->totem_config->token_hold_timeout*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		timer_function_token_hold_retransmit_timeout,
		&instance->timer_orf_token_hold_retransmit_timeout);
	if (res != 0) {
		log_printf(instance->totemsrp_log_level_error, "start_token_hold_retransmit_timeout - qb_loop_timer_add error : %d", res);
	}
}

static void cancel_token_hold_retransmit_timeout (struct totemsrp_instance *instance)
{
	qb_loop_timer_del (instance->totemsrp_poll_handle,
		instance->timer_orf_token_hold_retransmit_timeout);
}

static void memb_state_consensus_timeout_expired (
		struct totemsrp_instance *instance)
{
        struct srp_addr no_consensus_list[PROCESSOR_COUNT_MAX];
	int no_consensus_list_entries;

	instance->stats.consensus_timeouts++;
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
		memb_state_gather_enter (instance, TOTEMSRP_GSFROM_CONSENSUS_TIMEOUT);
	}
}

static void memb_join_message_send (struct totemsrp_instance *instance);

static void memb_merge_detect_transmit (struct totemsrp_instance *instance);

/*
 * Timers used for various states of the membership algorithm
 */
static void timer_function_pause_timeout (void *data)
{
	struct totemsrp_instance *instance = data;

	instance->pause_timestamp = qb_util_nano_current_get ();
	reset_pause_timeout (instance);
}

static void memb_recovery_state_token_loss (struct totemsrp_instance *instance)
{
	old_ring_state_restore (instance);
	memb_state_gather_enter (instance, TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_RECOVERY_STATE);
	instance->stats.recovery_token_lost++;
}

static void timer_function_orf_token_timeout (void *data)
{
	struct totemsrp_instance *instance = data;

	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			log_printf (instance->totemsrp_log_level_debug,
				"The token was lost in the OPERATIONAL state.");
			log_printf (instance->totemsrp_log_level_notice,
				"A processor failed, forming new configuration.");
			totemrrp_iface_check (instance->totemrrp_context);
			memb_state_gather_enter (instance, TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_OPERATIONAL_STATE);
			instance->stats.operational_token_lost++;
			break;

		case MEMB_STATE_GATHER:
			log_printf (instance->totemsrp_log_level_debug,
				"The consensus timeout expired.");
			memb_state_consensus_timeout_expired (instance);
			memb_state_gather_enter (instance, TOTEMSRP_GSFROM_THE_CONSENSUS_TIMEOUT_EXPIRED);
			instance->stats.gather_token_lost++;
			break;

		case MEMB_STATE_COMMIT:
			log_printf (instance->totemsrp_log_level_debug,
				"The token was lost in the COMMIT state.");
			memb_state_gather_enter (instance, TOTEMSRP_GSFROM_THE_TOKEN_WAS_LOST_IN_THE_COMMIT_STATE);
			instance->stats.commit_token_lost++;
			break;

		case MEMB_STATE_RECOVERY:
			log_printf (instance->totemsrp_log_level_debug,
				"The token was lost in the RECOVERY state.");
			memb_recovery_state_token_loss (instance);
			instance->orf_token_discard = 1;
			break;
	}
}

static void timer_function_heartbeat_timeout (void *data)
{
	struct totemsrp_instance *instance = data;
	log_printf (instance->totemsrp_log_level_debug,
		"HeartBeat Timer expired Invoking token loss mechanism in state %d ", instance->memb_state);
	timer_function_orf_token_timeout(data);
}

static void memb_timer_function_state_gather (void *data)
{
	struct totemsrp_instance *instance = data;
	int32_t res;

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
		qb_loop_timer_del (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);

		res = qb_loop_timer_add (instance->totemsrp_poll_handle,
			QB_LOOP_MED,
			instance->totem_config->join_timeout*QB_TIME_NS_IN_MSEC,
			(void *)instance,
			memb_timer_function_state_gather,
			&instance->memb_timer_state_gather_join_timeout);

		if (res != 0) {
			log_printf(instance->totemsrp_log_level_error, "memb_timer_function_state_gather - qb_loop_timer_add error : %d", res);
		}
		break;
	}
}

static void memb_timer_function_gather_consensus_timeout (void *data)
{
	struct totemsrp_instance *instance = data;
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
		"recovery to regular %x-%x", SEQNO_START_MSG + 1, instance->my_aru);

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
		recovery_message_item = ptr;

		/*
		 * Convert recovery message into regular message
		 */
		mcast = recovery_message_item->mcast;
		if (mcast->header.encapsulated == MESSAGE_ENCAPSULATED) {
			/*
			 * Message is a recovery message encapsulated
			 * in a new ring message
			 */
			regular_message_item.mcast =
				(struct mcast *)(((char *)recovery_message_item->mcast) + sizeof (struct mcast));
			regular_message_item.msg_len =
			recovery_message_item->msg_len - sizeof (struct mcast);
			mcast = regular_message_item.mcast;
		} else {
			/*
			 * TODO this case shouldn't happen
			 */
			continue;
		}

		log_printf (instance->totemsrp_log_level_debug,
			"comparing if ring id is for this processors old ring seqno %d",
			 mcast->seq);

		/*
		 * Only add this message to the regular sort
		 * queue if it was originated with the same ring
		 * id as the previous ring
		 */
		if (memcmp (&instance->my_old_ring_id, &mcast->ring_id,
			sizeof (struct memb_ring_id)) == 0) {

			res = sq_item_inuse (&instance->regular_sort_queue, mcast->seq);
			if (res == 0) {
				sq_item_add (&instance->regular_sort_queue,
					&regular_message_item, mcast->seq);
				if (sq_lt_compare (instance->old_ring_state_high_seq_received, mcast->seq)) {
					instance->old_ring_state_high_seq_received = mcast->seq;
				}
			}
		} else {
			log_printf (instance->totemsrp_log_level_debug,
				"-not adding msg with seq no %x", mcast->seq);
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
	unsigned int aru_save;
	unsigned int joined_list_totemip[PROCESSOR_COUNT_MAX];
	unsigned int trans_memb_list_totemip[PROCESSOR_COUNT_MAX];
	unsigned int new_memb_list_totemip[PROCESSOR_COUNT_MAX];
	unsigned int left_list[PROCESSOR_COUNT_MAX];
	unsigned int i;
	unsigned int res;
	char left_node_msg[1024];
	char joined_node_msg[1024];
	char failed_node_msg[1024];

	instance->originated_orf_token = 0;

	memb_consensus_reset (instance);

	old_ring_state_reset (instance);

	deliver_messages_from_recovery_to_regular (instance);

	log_printf (instance->totemsrp_log_level_trace,
		"Delivering to app %x to %x",
		instance->my_high_delivered + 1, instance->old_ring_state_high_seq_received);

	aru_save = instance->my_aru;
	instance->my_aru = instance->old_ring_state_aru;

	messages_deliver_to_app (instance, 0, instance->old_ring_state_high_seq_received);

	/*
	 * Calculate joined and left list
	 */
	memb_set_subtract (instance->my_left_memb_list,
		&instance->my_left_memb_entries,
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
	 * Inform RRP about transitional change
	 */
	totemrrp_membership_changed (
		instance->totemrrp_context,
		TOTEM_CONFIGURATION_TRANSITIONAL,
		instance->my_trans_memb_list, instance->my_trans_memb_entries,
		instance->my_left_memb_list, instance->my_left_memb_entries,
		NULL, 0,
		&instance->my_ring_id);
	/*
	 * Deliver transitional configuration to application
	 */
	srp_addr_to_nodeid (left_list, instance->my_left_memb_list,
		instance->my_left_memb_entries);
	srp_addr_to_nodeid (trans_memb_list_totemip,
		instance->my_trans_memb_list, instance->my_trans_memb_entries);
	instance->totemsrp_confchg_fn (TOTEM_CONFIGURATION_TRANSITIONAL,
		trans_memb_list_totemip, instance->my_trans_memb_entries,
		left_list, instance->my_left_memb_entries,
		0, 0, &instance->my_ring_id);
	instance->waiting_trans_ack = 1;
	instance->totemsrp_waiting_trans_ack_cb_fn (1);

// TODO we need to filter to ensure we only deliver those
// messages which are part of instance->my_deliver_memb
	messages_deliver_to_app (instance, 1, instance->old_ring_state_high_seq_received);

	instance->my_aru = aru_save;

	/*
	 * Inform RRP about regular membership change
	 */
	totemrrp_membership_changed (
		instance->totemrrp_context,
		TOTEM_CONFIGURATION_REGULAR,
		instance->my_new_memb_list, instance->my_new_memb_entries,
		NULL, 0,
		joined_list, joined_list_entries,
		&instance->my_ring_id);
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

	/* When making my_proc_list smaller, ensure that the
	 * now non-used entries are zero-ed out. There are some suspect
	 * assert's that assume that there is always 2 entries in the list.
	 * These fail when my_proc_list is reduced to 1 entry (and the
	 * valid [0] entry is the same as the 'unused' [1] entry).
	 */
	memset(instance->my_proc_list, 0,
		   sizeof (struct srp_addr) * instance->my_proc_list_entries);

	instance->my_proc_list_entries = instance->my_new_memb_entries;
	memcpy (instance->my_proc_list, instance->my_new_memb_list,
		sizeof (struct srp_addr) * instance->my_memb_entries);

	instance->my_failed_list_entries = 0;
	/*
	 * TODO Not exactly to spec
	 *
	 * At the entry to this function all messages without a gap are
	 * deliered.
	 *
	 * This code throw away messages from the last gap in the sort queue
	 * to my_high_seq_received
	 *
	 * What should really happen is we should deliver all messages up to
	 * a gap, then delier the transitional configuration, then deliver
	 * the messages between the first gap and my_high_seq_received, then
	 * deliver a regular configuration, then deliver the regular
	 * configuration
	 *
	 * Unfortunately totempg doesn't appear to like this operating mode
	 * which needs more inspection
	 */
	i = instance->my_high_seq_received + 1;
	do {
		void *ptr;

		i -= 1;
		res = sq_item_get (&instance->regular_sort_queue, i, &ptr);
		if (i == 0) {
			break;
		}
	} while (res);

	instance->my_high_delivered = i;

	for (i = 0; i <= instance->my_high_delivered; i++) {
		void *ptr;

		res = sq_item_get (&instance->regular_sort_queue, i, &ptr);
		if (res == 0) {
			struct sort_queue_item *regular_message;

			regular_message = ptr;
			free (regular_message->mcast);
		}
	}
	sq_items_release (&instance->regular_sort_queue, instance->my_high_delivered);
	instance->last_released = instance->my_high_delivered;

	if (joined_list_entries) {
		int sptr = 0;
		sptr += snprintf(joined_node_msg, sizeof(joined_node_msg)-sptr, " joined:");
		for (i=0; i< joined_list_entries; i++) {
			sptr += snprintf(joined_node_msg+sptr, sizeof(joined_node_msg)-sptr, " %u", joined_list_totemip[i]);
		}
	}
	else {
		joined_node_msg[0] = '\0';
	}

	if (instance->my_left_memb_entries) {
		int sptr = 0;
		int sptr2 = 0;
		sptr += snprintf(left_node_msg, sizeof(left_node_msg)-sptr, " left:");
		for (i=0; i< instance->my_left_memb_entries; i++) {
			sptr += snprintf(left_node_msg+sptr, sizeof(left_node_msg)-sptr, " %u", left_list[i]);
		}
		for (i=0; i< instance->my_left_memb_entries; i++) {
			if (my_leave_memb_match(instance, left_list[i]) == 0) {
				if (sptr2 == 0) {
					sptr2 += snprintf(failed_node_msg, sizeof(failed_node_msg)-sptr2, " failed:");
				}
				sptr2 += snprintf(failed_node_msg+sptr2, sizeof(left_node_msg)-sptr2, " %u", left_list[i]);
			}		
		}
		if (sptr2 == 0) {
			failed_node_msg[0] = '\0';
		}
	}
	else {
		left_node_msg[0] = '\0';
		failed_node_msg[0] = '\0';
	}

	my_leave_memb_clear(instance);

	log_printf (instance->totemsrp_log_level_debug,
		"entering OPERATIONAL state.");
	log_printf (instance->totemsrp_log_level_notice,
		"A new membership (%s:%lld) was formed. Members%s%s",
		totemip_print (&instance->my_ring_id.rep),
		instance->my_ring_id.seq,
		joined_node_msg,
		left_node_msg);

	if (strlen(failed_node_msg)) {
		log_printf (instance->totemsrp_log_level_notice,
			"Failed to receive the leave message.%s",
			failed_node_msg);
	}

	instance->memb_state = MEMB_STATE_OPERATIONAL;

	instance->stats.operational_entered++;
	instance->stats.continuous_gather = 0;

	instance->my_received_flg = 1;

	reset_pause_timeout (instance);

	/*
	 * Save ring id information from this configuration to determine
	 * which processors are transitioning from old regular configuration
	 * in to new regular configuration on the next configuration change
	 */
	memcpy (&instance->my_old_ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id));

	return;
}

static void memb_state_gather_enter (
	struct totemsrp_instance *instance,
	enum gather_state_from gather_from)
{
	int32_t res;

	instance->orf_token_discard = 1;

	instance->originated_orf_token = 0;

	memb_set_merge (
		&instance->my_id, 1,
		instance->my_proc_list, &instance->my_proc_list_entries);

	memb_join_message_send (instance);

	/*
	 * Restart the join timeout
	 */
	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);

	res = qb_loop_timer_add (instance->totemsrp_poll_handle,
		QB_LOOP_MED,
		instance->totem_config->join_timeout*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		memb_timer_function_state_gather,
		&instance->memb_timer_state_gather_join_timeout);
	if (res != 0) {
		log_printf(instance->totemsrp_log_level_error, "memb_state_gather_enter - qb_loop_timer_add error(1) : %d", res);
	}

	/*
	 * Restart the consensus timeout
	 */
	qb_loop_timer_del (instance->totemsrp_poll_handle,
		instance->memb_timer_state_gather_consensus_timeout);

	res = qb_loop_timer_add (instance->totemsrp_poll_handle,
		QB_LOOP_MED,
		instance->totem_config->consensus_timeout*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		memb_timer_function_gather_consensus_timeout,
		&instance->memb_timer_state_gather_consensus_timeout);
	if (res != 0) {
		log_printf(instance->totemsrp_log_level_error, "memb_state_gather_enter - qb_loop_timer_add error(2) : %d", res);
	}

	/*
	 * Cancel the token loss and token retransmission timeouts
	 */
	cancel_token_retransmit_timeout (instance); // REVIEWED
	cancel_token_timeout (instance); // REVIEWED
	cancel_merge_detect_timeout (instance);

	memb_consensus_reset (instance);

	memb_consensus_set (instance, &instance->my_id);

	log_printf (instance->totemsrp_log_level_debug,
		    "entering GATHER state from %d(%s).",
		    gather_from, gsfrom_to_msg(gather_from));

	instance->memb_state = MEMB_STATE_GATHER;
	instance->stats.gather_entered++;

	if (gather_from == TOTEMSRP_GSFROM_THE_CONSENSUS_TIMEOUT_EXPIRED) {
		/*
		 * State 3 means gather, so we are continuously gathering.
		 */
		instance->stats.continuous_gather++;
	}

	return;
}

static void timer_function_token_retransmit_timeout (void *data);

static void target_set_completed (
	void *context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;

	memb_state_commit_token_send (instance);

}

static void memb_state_commit_enter (
	struct totemsrp_instance *instance)
{
	old_ring_state_save (instance);

	memb_state_commit_token_update (instance);

	memb_state_commit_token_target_set (instance);

	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);

	instance->memb_timer_state_gather_join_timeout = 0;

	qb_loop_timer_del (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_consensus_timeout);

	instance->memb_timer_state_gather_consensus_timeout = 0;

	memb_ring_id_set (instance, &instance->commit_token->ring_id);
	instance->memb_ring_id_store (&instance->my_ring_id, &instance->my_id.addr[0]);

	instance->token_ring_id_seq = instance->my_ring_id.seq;

	log_printf (instance->totemsrp_log_level_debug,
		"entering COMMIT state.");

	instance->memb_state = MEMB_STATE_COMMIT;
	reset_token_retransmit_timeout (instance); // REVIEWED
	reset_token_timeout (instance); // REVIEWED

	instance->stats.commit_entered++;
	instance->stats.continuous_gather = 0;

	/*
	 * reset all flow control variables since we are starting a new ring
	 */
	instance->my_trc = 0;
	instance->my_pbl = 0;
	instance->my_cbl = 0;
	/*
	 * commit token sent after callback that token target has been set
	 */
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
	const struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;
	struct memb_ring_id my_new_memb_ring_id_list[PROCESSOR_COUNT_MAX];

	addr = (const struct srp_addr *)commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + commit_token->addr_entries);

	log_printf (instance->totemsrp_log_level_debug,
		"entering RECOVERY state.");

	instance->orf_token_discard = 0;

	instance->my_high_ring_delivered = 0;

	sq_reinit (&instance->recovery_sort_queue, SEQNO_START_MSG);
	cs_queue_reinit (&instance->retrans_message_queue);

	low_ring_aru = instance->old_ring_state_high_seq_received;

	memb_state_commit_token_send_recovery (instance, commit_token);

	instance->my_token_seq = SEQNO_START_TOKEN - 1;

	/*
	 * Build regular configuration
	 */
	totemrrp_processor_count_set (
		instance->totemrrp_context,
		commit_token->addr_entries);

	/*
	 * Build transitional configuration
	 */
	for (i = 0; i < instance->my_new_memb_entries; i++) {
		memcpy (&my_new_memb_ring_id_list[i],
			&memb_list[i].ring_id,
			sizeof (struct memb_ring_id));
	}
	memb_set_and_with_ring_id (
		instance->my_new_memb_list,
		my_new_memb_ring_id_list,
		instance->my_new_memb_entries,
		instance->my_memb_list,
		instance->my_memb_entries,
		&instance->my_old_ring_id,
		instance->my_trans_memb_list,
		&instance->my_trans_memb_entries);

	for (i = 0; i < instance->my_trans_memb_entries; i++) {
		log_printf (instance->totemsrp_log_level_debug,
			"TRANS [%d] member %s:", i, totemip_print (&instance->my_trans_memb_list[i].addr[0]));
	}
	for (i = 0; i < instance->my_new_memb_entries; i++) {
		log_printf (instance->totemsrp_log_level_debug,
			"position [%d] member %s:", i, totemip_print (&addr[i].addr[0]));
		log_printf (instance->totemsrp_log_level_debug,
			"previous ring seq %llx rep %s",
			memb_list[i].ring_id.seq,
			totemip_print (&memb_list[i].ring_id.rep));

		log_printf (instance->totemsrp_log_level_debug,
			"aru %x high delivered %x received flag %d",
			memb_list[i].aru,
			memb_list[i].high_delivered,
			memb_list[i].received_flg);

	//	assert (totemip_print (&memb_list[i].ring_id.rep) != 0);
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
	assert (range < QUEUE_RTR_ITEMS_SIZE_MAX);

	log_printf (instance->totemsrp_log_level_debug,
		"copying all old ring messages from %x-%x.",
		low_ring_aru + 1, instance->old_ring_state_high_seq_received);

	for (i = 1; i <= range; i++) {
		struct sort_queue_item *sort_queue_item;
		struct message_item message_item;
		void *ptr;
		int res;

		res = sq_item_get (&instance->regular_sort_queue,
			low_ring_aru + i, &ptr);
		if (res != 0) {
			continue;
		}
		sort_queue_item = ptr;
		messages_originated++;
		memset (&message_item, 0, sizeof (struct message_item));
	// TODO	 LEAK
		message_item.mcast = totemsrp_buffer_alloc (instance);
		assert (message_item.mcast);
		message_item.mcast->header.type = MESSAGE_TYPE_MCAST;
		srp_addr_copy (&message_item.mcast->system_from, &instance->my_id);
		message_item.mcast->header.encapsulated = MESSAGE_ENCAPSULATED;
		message_item.mcast->header.nodeid = instance->my_id.addr[0].nodeid;
		assert (message_item.mcast->header.nodeid);
		message_item.mcast->header.endian_detector = ENDIAN_LOCAL;
		memcpy (&message_item.mcast->ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id));
		message_item.msg_len = sort_queue_item->msg_len + sizeof (struct mcast);
		memcpy (((char *)message_item.mcast) + sizeof (struct mcast),
			sort_queue_item->mcast,
			sort_queue_item->msg_len);
		cs_queue_item_add (&instance->retrans_message_queue, &message_item);
	}
	log_printf (instance->totemsrp_log_level_debug,
		"Originated %d messages in RECOVERY.", messages_originated);
	goto originated;

no_originate:
	log_printf (instance->totemsrp_log_level_debug,
		"Did not need to originate any messages in recovery.");

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
	instance->stats.recovery_entered++;
	instance->stats.continuous_gather = 0;

	return;
}

void totemsrp_event_signal (void *srp_context, enum totem_event_type type, int value)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;

	token_hold_cancel_send (instance);

	return;
}

int totemsrp_mcast (
	void *srp_context,
	struct iovec *iovec,
	unsigned int iov_len,
	int guarantee)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;
	int i;
	struct message_item message_item;
	char *addr;
	unsigned int addr_idx;
	struct cs_queue *queue_use;

	if (instance->waiting_trans_ack) {
		queue_use = &instance->new_message_queue_trans;
	} else {
		queue_use = &instance->new_message_queue;
	}

	if (cs_queue_is_full (queue_use)) {
		log_printf (instance->totemsrp_log_level_debug, "queue full");
		return (-1);
	}

	memset (&message_item, 0, sizeof (struct message_item));

	/*
	 * Allocate pending item
	 */
	message_item.mcast = totemsrp_buffer_alloc (instance);
	if (message_item.mcast == 0) {
		goto error_mcast;
	}

	/*
	 * Set mcast header
	 */
	memset(message_item.mcast, 0, sizeof (struct mcast));
	message_item.mcast->header.type = MESSAGE_TYPE_MCAST;
	message_item.mcast->header.endian_detector = ENDIAN_LOCAL;
	message_item.mcast->header.encapsulated = MESSAGE_NOT_ENCAPSULATED;
	message_item.mcast->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (message_item.mcast->header.nodeid);

	message_item.mcast->guarantee = guarantee;
	srp_addr_copy (&message_item.mcast->system_from, &instance->my_id);

	addr = (char *)message_item.mcast;
	addr_idx = sizeof (struct mcast);
	for (i = 0; i < iov_len; i++) {
		memcpy (&addr[addr_idx], iovec[i].iov_base, iovec[i].iov_len);
		addr_idx += iovec[i].iov_len;
	}

	message_item.msg_len = addr_idx;

	log_printf (instance->totemsrp_log_level_trace, "mcasted message added to pending queue");
	instance->stats.mcast_tx++;
	cs_queue_item_add (queue_use, &message_item);

	return (0);

error_mcast:
	return (-1);
}

/*
 * Determine if there is room to queue a new message
 */
int totemsrp_avail (void *srp_context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;
	int avail;
	struct cs_queue *queue_use;

	if (instance->waiting_trans_ack) {
		queue_use = &instance->new_message_queue_trans;
	} else {
		queue_use = &instance->new_message_queue;
	}
	cs_queue_avail (queue_use, &avail);

	return (avail);
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
		log_printf (instance->totemsrp_log_level_debug, "sq not in range");
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

	totemrrp_mcast_noflush_send (
		instance->totemrrp_context,
		sort_queue_item->mcast,
		sort_queue_item->msg_len);

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
	unsigned int i;
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
	assert (range < QUEUE_RTR_ITEMS_SIZE_MAX);

	/*
	 * Release retransmit list items if group aru indicates they are transmitted
	 */
	for (i = 1; i <= range; i++) {
		void *ptr;

		res = sq_item_get (&instance->regular_sort_queue,
			instance->last_released + i, &ptr);
		if (res == 0) {
			regular_message = ptr;
			totemsrp_buffer_release (instance, regular_message->mcast);
		}
		sq_items_release (&instance->regular_sort_queue,
			instance->last_released + i);

		log_release = 1;
	}
	instance->last_released += range;

 	if (log_release) {
		log_printf (instance->totemsrp_log_level_trace,
			"releasing messages up to and including %x", release_to);
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
	struct cs_queue *mcast_queue;
	struct sq *sort_queue;
	struct sort_queue_item sort_queue_item;
	struct mcast *mcast;
	unsigned int fcc_mcast_current;

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		mcast_queue = &instance->retrans_message_queue;
		sort_queue = &instance->recovery_sort_queue;
		reset_token_retransmit_timeout (instance); // REVIEWED
	} else {
		if (instance->waiting_trans_ack) {
			mcast_queue = &instance->new_message_queue_trans;
		} else {
			mcast_queue = &instance->new_message_queue;
		}

		sort_queue = &instance->regular_sort_queue;
	}

	for (fcc_mcast_current = 0; fcc_mcast_current < fcc_mcasts_allowed; fcc_mcast_current++) {
		if (cs_queue_is_empty (mcast_queue)) {
			break;
		}
		message_item = (struct message_item *)cs_queue_item_get (mcast_queue);

		message_item->mcast->seq = ++token->seq;
		message_item->mcast->this_seqno = instance->global_seqno++;

		/*
		 * Build IO vector
		 */
		memset (&sort_queue_item, 0, sizeof (struct sort_queue_item));
		sort_queue_item.mcast = message_item->mcast;
		sort_queue_item.msg_len = message_item->msg_len;

		mcast = sort_queue_item.mcast;

		memcpy (&mcast->ring_id, &instance->my_ring_id, sizeof (struct memb_ring_id));

		/*
		 * Add message to retransmit queue
		 */
		sq_item_add (sort_queue, &sort_queue_item, message_item->mcast->seq);

		totemrrp_mcast_noflush_send (
			instance->totemrrp_context,
			message_item->mcast,
			message_item->msg_len);

		/*
		 * Delete item from pending queue
		 */
		cs_queue_item_remove (mcast_queue);

		/*
		 * If messages mcasted, deliver any new messages to totempg
		 */
		instance->my_high_seq_received = token->seq;
	}

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
			"Retransmit List %d", orf_token->rtr_list_entries);
		for (i = 0; i < orf_token->rtr_list_entries; i++) {
			sprintf (value, "%x ", rtr_list[i].seq);
			strcat (retransmit_msg, value);
		}
		strcat (retransmit_msg, "");
		log_printf (instance->totemsrp_log_level_notice,
			"%s", retransmit_msg);
	}

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
				sizeof (struct rtr_item) * (orf_token->rtr_list_entries - i));

			instance->stats.mcast_retx++;
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

	range = orf_token->seq - instance->my_aru;
	assert (range < QUEUE_RTR_ITEMS_SIZE_MAX);

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
			 * Determine how many times we have missed receiving
			 * this sequence number.  sq_item_miss_count increments
			 * a counter for the sequence number.  The miss count
			 * will be returned and compared.  This allows time for
			 * delayed multicast messages to be received before
			 * declaring the message is missing and requesting a
			 * retransmit.
			 */
			res = sq_item_miss_count (sort_queue, instance->my_aru + i);
			if (res < instance->totem_config->miss_count_const) {
				continue;
			}

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
				memcpy (&rtr_list[orf_token->rtr_list_entries].ring_id,
					&instance->my_ring_id, sizeof (struct memb_ring_id));
				rtr_list[orf_token->rtr_list_entries].seq = instance->my_aru + i;
				orf_token->rtr_list_entries++;
			}
		}
	}
	return (instance->fcc_remcast_current);
}

static void token_retransmit (struct totemsrp_instance *instance)
{
	totemrrp_token_send (instance->totemrrp_context,
		instance->orf_token_retransmit,
		instance->orf_token_retransmit_size);
}

/*
 * Retransmit the regular token if no mcast or token has
 * been received in retransmit token period retransmit
 * the token to the next processor
 */
static void timer_function_token_retransmit_timeout (void *data)
{
	struct totemsrp_instance *instance = data;

	switch (instance->memb_state) {
	case MEMB_STATE_GATHER:
		break;
	case MEMB_STATE_COMMIT:
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		token_retransmit (instance);
		reset_token_retransmit_timeout (instance); // REVIEWED
		break;
	}
}

static void timer_function_token_hold_retransmit_timeout (void *data)
{
	struct totemsrp_instance *instance = data;

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
	struct totemsrp_instance *instance = data;

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
	int res = 0;
	unsigned int orf_token_size;

	orf_token_size = sizeof (struct orf_token) +
		(orf_token->rtr_list_entries * sizeof (struct rtr_item));

	orf_token->header.nodeid = instance->my_id.addr[0].nodeid;
	memcpy (instance->orf_token_retransmit, orf_token, orf_token_size);
	instance->orf_token_retransmit_size = orf_token_size;
	assert (orf_token->header.nodeid);

	if (forward_token == 0) {
		return (0);
	}

	totemrrp_token_send (instance->totemrrp_context,
		orf_token,
		orf_token_size);

	return (res);
}

static int token_hold_cancel_send (struct totemsrp_instance *instance)
{
	struct token_hold_cancel token_hold_cancel;

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
	token_hold_cancel.header.encapsulated = 0;
	token_hold_cancel.header.nodeid = instance->my_id.addr[0].nodeid;
	memcpy (&token_hold_cancel.ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id));
	assert (token_hold_cancel.header.nodeid);

	instance->stats.token_hold_cancel_tx++;

	totemrrp_mcast_flush_send (instance->totemrrp_context, &token_hold_cancel,
		sizeof (struct token_hold_cancel));

	return (0);
}

static int orf_token_send_initial (struct totemsrp_instance *instance)
{
	struct orf_token orf_token;
	int res;

	orf_token.header.type = MESSAGE_TYPE_ORF_TOKEN;
	orf_token.header.endian_detector = ENDIAN_LOCAL;
	orf_token.header.encapsulated = 0;
	orf_token.header.nodeid = instance->my_id.addr[0].nodeid;
	assert (orf_token.header.nodeid);
	orf_token.seq = SEQNO_START_MSG;
	orf_token.token_seq = SEQNO_START_TOKEN;
	orf_token.retrans_flg = 1;
	instance->my_set_retrans_flg = 1;
	instance->stats.orf_token_tx++;

	if (cs_queue_is_empty (&instance->retrans_message_queue) == 1) {
		orf_token.retrans_flg = 0;
		instance->my_set_retrans_flg = 0;
	} else {
		orf_token.retrans_flg = 1;
		instance->my_set_retrans_flg = 1;
	}

	orf_token.aru = 0;
	orf_token.aru = SEQNO_START_MSG - 1;
	orf_token.aru_addr = instance->my_id.addr[0].nodeid;

	memcpy (&orf_token.ring_id, &instance->my_ring_id, sizeof (struct memb_ring_id));
	orf_token.fcc = 0;
	orf_token.backlog = 0;

	orf_token.rtr_list_entries = 0;

	res = token_send (instance, &orf_token, 1);

	return (res);
}

static void memb_state_commit_token_update (
	struct totemsrp_instance *instance)
{
	struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;
	unsigned int high_aru;
	unsigned int i;

	addr = (struct srp_addr *)instance->commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + instance->commit_token->addr_entries);

	memcpy (instance->my_new_memb_list, addr,
		sizeof (struct srp_addr) * instance->commit_token->addr_entries);

	instance->my_new_memb_entries = instance->commit_token->addr_entries;

	memcpy (&memb_list[instance->commit_token->memb_index].ring_id,
		&instance->my_old_ring_id, sizeof (struct memb_ring_id));

	memb_list[instance->commit_token->memb_index].aru = instance->old_ring_state_aru;
	/*
	 *  TODO high delivered is really instance->my_aru, but with safe this
	 * could change?
	 */
	instance->my_received_flg =
		(instance->my_aru == instance->my_high_seq_received);

	memb_list[instance->commit_token->memb_index].received_flg = instance->my_received_flg;

	memb_list[instance->commit_token->memb_index].high_delivered = instance->my_high_delivered;
	/*
	 * find high aru up to current memb_index for all matching ring ids
	 * if any ring id matching memb_index has aru less then high aru set
	 * received flag for that entry to false
	 */
	high_aru = memb_list[instance->commit_token->memb_index].aru;
	for (i = 0; i <= instance->commit_token->memb_index; i++) {
		if (memcmp (&memb_list[instance->commit_token->memb_index].ring_id,
			&memb_list[i].ring_id,
			sizeof (struct memb_ring_id)) == 0) {

			if (sq_lt_compare (high_aru, memb_list[i].aru)) {
				high_aru = memb_list[i].aru;
			}
		}
	}

	for (i = 0; i <= instance->commit_token->memb_index; i++) {
		if (memcmp (&memb_list[instance->commit_token->memb_index].ring_id,
			&memb_list[i].ring_id,
			sizeof (struct memb_ring_id)) == 0) {

			if (sq_lt_compare (memb_list[i].aru, high_aru)) {
				memb_list[i].received_flg = 0;
				if (i == instance->commit_token->memb_index) {
					instance->my_received_flg = 0;
				}
			}
		}
	}

	instance->commit_token->header.nodeid = instance->my_id.addr[0].nodeid;
	instance->commit_token->memb_index += 1;
	assert (instance->commit_token->memb_index <= instance->commit_token->addr_entries);
	assert (instance->commit_token->header.nodeid);
}

static void memb_state_commit_token_target_set (
	struct totemsrp_instance *instance)
{
	struct srp_addr *addr;
	unsigned int i;

	addr = (struct srp_addr *)instance->commit_token->end_of_commit_token;

	for (i = 0; i < instance->totem_config->interface_count; i++) {
		totemrrp_token_target_set (
			instance->totemrrp_context,
			&addr[instance->commit_token->memb_index %
				instance->commit_token->addr_entries].addr[i],
			i);
	}
}

static int memb_state_commit_token_send_recovery (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	unsigned int commit_token_size;

	commit_token->token_seq++;
	commit_token->header.nodeid = instance->my_id.addr[0].nodeid;
	commit_token_size = sizeof (struct memb_commit_token) +
		((sizeof (struct srp_addr) +
			sizeof (struct memb_commit_token_memb_entry)) * commit_token->addr_entries);
	/*
	 * Make a copy for retransmission if necessary
	 */
	memcpy (instance->orf_token_retransmit, commit_token, commit_token_size);
	instance->orf_token_retransmit_size = commit_token_size;

	instance->stats.memb_commit_token_tx++;

	totemrrp_token_send (instance->totemrrp_context,
		commit_token,
		commit_token_size);

	/*
	 * Request retransmission of the commit token in case it is lost
	 */
	reset_token_retransmit_timeout (instance);
	return (0);
}

static int memb_state_commit_token_send (
	struct totemsrp_instance *instance)
{
	unsigned int commit_token_size;

	instance->commit_token->token_seq++;
	instance->commit_token->header.nodeid = instance->my_id.addr[0].nodeid;
	commit_token_size = sizeof (struct memb_commit_token) +
		((sizeof (struct srp_addr) +
			sizeof (struct memb_commit_token_memb_entry)) * instance->commit_token->addr_entries);
	/*
	 * Make a copy for retransmission if necessary
	 */
	memcpy (instance->orf_token_retransmit, instance->commit_token, commit_token_size);
	instance->orf_token_retransmit_size = commit_token_size;

	instance->stats.memb_commit_token_tx++;

	totemrrp_token_send (instance->totemrrp_context,
		instance->commit_token,
		commit_token_size);

	/*
	 * Request retransmission of the commit token in case it is lost
	 */
	reset_token_retransmit_timeout (instance);
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
	const struct srp_addr *srp_a = (const struct srp_addr *)a;
	const struct srp_addr *srp_b = (const struct srp_addr *)b;

	return (totemip_compare (&srp_a->addr[0], &srp_b->addr[0]));
}

static void memb_state_commit_token_create (
	struct totemsrp_instance *instance)
{
	struct srp_addr token_memb[PROCESSOR_COUNT_MAX];
	struct srp_addr *addr;
	struct memb_commit_token_memb_entry *memb_list;
	int token_memb_entries = 0;

	log_printf (instance->totemsrp_log_level_debug,
		"Creating commit token because I am the rep.");

	memb_set_subtract (token_memb, &token_memb_entries,
		instance->my_proc_list, instance->my_proc_list_entries,
		instance->my_failed_list, instance->my_failed_list_entries);

	memset (instance->commit_token, 0, sizeof (struct memb_commit_token));
	instance->commit_token->header.type = MESSAGE_TYPE_MEMB_COMMIT_TOKEN;
	instance->commit_token->header.endian_detector = ENDIAN_LOCAL;
	instance->commit_token->header.encapsulated = 0;
	instance->commit_token->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (instance->commit_token->header.nodeid);

	totemip_copy(&instance->commit_token->ring_id.rep, &instance->my_id.addr[0]);

	instance->commit_token->ring_id.seq = instance->token_ring_id_seq + 4;

	/*
	 * This qsort is necessary to ensure the commit token traverses
	 * the ring in the proper order
	 */
	qsort (token_memb, token_memb_entries, sizeof (struct srp_addr),
		srp_addr_compare);

	instance->commit_token->memb_index = 0;
	instance->commit_token->addr_entries = token_memb_entries;

	addr = (struct srp_addr *)instance->commit_token->end_of_commit_token;
	memb_list = (struct memb_commit_token_memb_entry *)(addr + instance->commit_token->addr_entries);

	memcpy (addr, token_memb,
		token_memb_entries * sizeof (struct srp_addr));
	memset (memb_list, 0,
		sizeof (struct memb_commit_token_memb_entry) * token_memb_entries);
}

static void memb_join_message_send (struct totemsrp_instance *instance)
{
	char memb_join_data[40000];
	struct memb_join *memb_join = (struct memb_join *)memb_join_data;
	char *addr;
	unsigned int addr_idx;

	memb_join->header.type = MESSAGE_TYPE_MEMB_JOIN;
	memb_join->header.endian_detector = ENDIAN_LOCAL;
	memb_join->header.encapsulated = 0;
	memb_join->header.nodeid = instance->my_id.addr[0].nodeid;
	assert (memb_join->header.nodeid);

	memb_join->ring_seq = instance->my_ring_id.seq;
	memb_join->proc_list_entries = instance->my_proc_list_entries;
	memb_join->failed_list_entries = instance->my_failed_list_entries;
	srp_addr_copy (&memb_join->system_from, &instance->my_id);

	/*
	 * This mess adds the joined and failed processor lists into the join
	 * message
	 */
	addr = (char *)memb_join;
	addr_idx = sizeof (struct memb_join);
	memcpy (&addr[addr_idx],
		instance->my_proc_list,
		instance->my_proc_list_entries *
			sizeof (struct srp_addr));
	addr_idx +=
		instance->my_proc_list_entries *
		sizeof (struct srp_addr);
	memcpy (&addr[addr_idx],
		instance->my_failed_list,
		instance->my_failed_list_entries *
		sizeof (struct srp_addr));
	addr_idx +=
		instance->my_failed_list_entries *
		sizeof (struct srp_addr);


	if (instance->totem_config->send_join_timeout) {
		usleep (random() % (instance->totem_config->send_join_timeout * 1000));
	}

	instance->stats.memb_join_tx++;

	totemrrp_mcast_flush_send (
		instance->totemrrp_context,
		memb_join,
		addr_idx);
}

static void memb_leave_message_send (struct totemsrp_instance *instance)
{
	char memb_join_data[40000];
	struct memb_join *memb_join = (struct memb_join *)memb_join_data;
	char *addr;
	unsigned int addr_idx;
	int active_memb_entries;
	struct srp_addr active_memb[PROCESSOR_COUNT_MAX];

	log_printf (instance->totemsrp_log_level_debug,
		"sending join/leave message");

	/*
	 * add us to the failed list, and remove us from
	 * the members list
	 */
	memb_set_merge(
		       &instance->my_id, 1,
		       instance->my_failed_list, &instance->my_failed_list_entries);

	memb_set_subtract (active_memb, &active_memb_entries,
			   instance->my_proc_list, instance->my_proc_list_entries,
			   &instance->my_id, 1);


	memb_join->header.type = MESSAGE_TYPE_MEMB_JOIN;
	memb_join->header.endian_detector = ENDIAN_LOCAL;
	memb_join->header.encapsulated = 0;
	memb_join->header.nodeid = LEAVE_DUMMY_NODEID;

	memb_join->ring_seq = instance->my_ring_id.seq;
	memb_join->proc_list_entries = active_memb_entries;
	memb_join->failed_list_entries = instance->my_failed_list_entries;
	srp_addr_copy (&memb_join->system_from, &instance->my_id);
	memb_join->system_from.addr[0].nodeid = LEAVE_DUMMY_NODEID;

	// TODO: CC Maybe use the actual join send routine.
	/*
	 * This mess adds the joined and failed processor lists into the join
	 * message
	 */
	addr = (char *)memb_join;
	addr_idx = sizeof (struct memb_join);
	memcpy (&addr[addr_idx],
		active_memb,
		active_memb_entries *
			sizeof (struct srp_addr));
	addr_idx +=
		active_memb_entries *
		sizeof (struct srp_addr);
	memcpy (&addr[addr_idx],
		instance->my_failed_list,
		instance->my_failed_list_entries *
		sizeof (struct srp_addr));
	addr_idx +=
		instance->my_failed_list_entries *
		sizeof (struct srp_addr);


	if (instance->totem_config->send_join_timeout) {
		usleep (random() % (instance->totem_config->send_join_timeout * 1000));
	}
	instance->stats.memb_join_tx++;

	totemrrp_mcast_flush_send (
		instance->totemrrp_context,
		memb_join,
		addr_idx);
}

static void memb_merge_detect_transmit (struct totemsrp_instance *instance)
{
	struct memb_merge_detect memb_merge_detect;

	memb_merge_detect.header.type = MESSAGE_TYPE_MEMB_MERGE_DETECT;
	memb_merge_detect.header.endian_detector = ENDIAN_LOCAL;
	memb_merge_detect.header.encapsulated = 0;
	memb_merge_detect.header.nodeid = instance->my_id.addr[0].nodeid;
	srp_addr_copy (&memb_merge_detect.system_from, &instance->my_id);
	memcpy (&memb_merge_detect.ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id));
	assert (memb_merge_detect.header.nodeid);

	instance->stats.memb_merge_detect_tx++;
	totemrrp_mcast_flush_send (instance->totemrrp_context,
		&memb_merge_detect,
		sizeof (struct memb_merge_detect));
}

static void memb_ring_id_set (
	struct totemsrp_instance *instance,
	const struct memb_ring_id *ring_id)
{

	memcpy (&instance->my_ring_id, ring_id, sizeof (struct memb_ring_id));
}

int totemsrp_callback_token_create (
	void *srp_context,
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)srp_context;
	struct token_callback_instance *callback_handle;

	token_hold_cancel_send (instance);

	callback_handle = malloc (sizeof (struct token_callback_instance));
	if (callback_handle == 0) {
		return (-1);
	}
	*handle_out = (void *)callback_handle;
	list_init (&callback_handle->list);
	callback_handle->callback_fn = callback_fn;
	callback_handle->data = (void *) data;
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

	return (0);
}

void totemsrp_callback_token_destroy (void *srp_context, void **handle_out)
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
	struct cs_queue *queue_use = NULL;

	if (instance->memb_state == MEMB_STATE_OPERATIONAL) {
		if (instance->waiting_trans_ack) {
			queue_use = &instance->new_message_queue_trans;
		} else {
			queue_use = &instance->new_message_queue;
		}
	} else
	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		queue_use = &instance->retrans_message_queue;
	}

	if (queue_use != NULL) {
		backlog = cs_queue_used (queue_use);
	}

	instance->stats.token[instance->stats.latest_token].backlog_calc = backlog;
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
	int check = QUEUE_RTR_ITEMS_SIZE_MAX;
	check -= (*transmits_allowed + instance->totem_config->window_size);
	assert (check >= 0);
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
	instance->my_trc = msgs_transmitted;
	instance->my_pbl = instance->my_cbl;
}

/*
 * Message Handlers
 */

unsigned long long int tv_old;
/*
 * message handler called when TOKEN message type received
 */
static int message_handler_orf_token (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
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

#ifdef GIVEINFO
	unsigned long long tv_current;
	unsigned long long tv_diff;

	tv_current = qb_util_nano_current_get ();
	tv_diff = tv_current - tv_old;
	tv_old = tv_current;

	log_printf (instance->totemsrp_log_level_debug,
	"Time since last token %0.4f ms", ((float)tv_diff) / 1000000.0);
#endif

	if (instance->orf_token_discard) {
		return (0);
	}
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
	memcpy (&token->rtr_list[0], (char *)msg + sizeof (struct orf_token),
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
	instance->flushing = 1;
	totemrrp_recv_flush (instance->totemrrp_context);
	instance->flushing = 0;

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
		/*
		 * Do NOT add break, this case should also execute code in gather case.
		 */

	case MEMB_STATE_GATHER:
		/*
		 * DO NOT add break, we use different free mechanism in recovery state
		 */

	case MEMB_STATE_RECOVERY:
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
			return (0); /* discard token */
		}
		last_aru = instance->my_last_aru;
		instance->my_last_aru = token->aru;

		transmits_allowed = fcc_calculate (instance, token);
		mcasted_retransmit = orf_token_rtr (instance, token, &transmits_allowed);

		if (instance->my_token_held == 1 &&
			(token->rtr_list_entries > 0 || mcasted_retransmit > 0)) {
			instance->my_token_held = 0;
			forward_token = 1;
		}

		fcc_rtr_limit (instance, token, &transmits_allowed);
		mcasted_regular = orf_token_mcast (instance, token, transmits_allowed);
/*
if (mcasted_regular) {
printf ("mcasted regular %d\n", mcasted_regular);
printf ("token seq %d\n", token->seq);
}
*/
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

		/*
		 * We really don't follow specification there. In specification, OTHER nodes
		 * detect failure of one node (based on aru_count) and my_id IS NEVER added
		 * to failed list (so node never mark itself as failed)
		 */
		if (instance->my_aru_count > instance->totem_config->fail_to_recv_const &&
			token->aru_addr == instance->my_id.addr[0].nodeid) {

			log_printf (instance->totemsrp_log_level_error,
				"FAILED TO RECEIVE");

			instance->failed_to_recv = 1;

			memb_set_merge (&instance->my_id, 1,
				instance->my_failed_list,
				&instance->my_failed_list_entries);

			memb_state_gather_enter (instance, TOTEMSRP_GSFROM_FAILED_TO_RECEIVE);
		} else {
			instance->my_token_seq = token->token_seq;
			token->token_seq += 1;

			if (instance->memb_state == MEMB_STATE_RECOVERY) {
				/*
				 * instance->my_aru == instance->my_high_seq_received means this processor
				 * has recovered all messages it can recover
				 * (ie: its retrans queue is empty)
				 */
				if (cs_queue_is_empty (&instance->retrans_message_queue) == 0) {

					if (token->retrans_flg == 0) {
						token->retrans_flg = 1;
						instance->my_set_retrans_flg = 1;
					}
				} else
				if (token->retrans_flg == 1 && instance->my_set_retrans_flg) {
					token->retrans_flg = 0;
					instance->my_set_retrans_flg = 0;
				}
				log_printf (instance->totemsrp_log_level_debug,
					"token retrans flag is %d my set retrans flag%d retrans queue empty %d count %d, aru %x",
					token->retrans_flg, instance->my_set_retrans_flg,
					cs_queue_is_empty (&instance->retrans_message_queue),
					instance->my_retrans_flg_count, token->aru);
				if (token->retrans_flg == 0) {
					instance->my_retrans_flg_count += 1;
				} else {
					instance->my_retrans_flg_count = 0;
				}
				if (instance->my_retrans_flg_count == 2) {
					instance->my_install_seq = token->seq;
				}
				log_printf (instance->totemsrp_log_level_debug,
					"install seq %x aru %x high seq received %x",
					instance->my_install_seq, instance->my_aru, instance->my_high_seq_received);
				if (instance->my_retrans_flg_count >= 2 &&
					instance->my_received_flg == 0 &&
					sq_lte_compare (instance->my_install_seq, instance->my_aru)) {
					instance->my_received_flg = 1;
					instance->my_deliver_memb_entries = instance->my_trans_memb_entries;
					memcpy (instance->my_deliver_memb_list, instance->my_trans_memb_list,
						sizeof (struct totem_ip_address) * instance->my_trans_memb_entries);
				}
				if (instance->my_retrans_flg_count >= 3 &&
					sq_lte_compare (instance->my_install_seq, token->aru)) {
					instance->my_rotation_counter += 1;
				} else {
					instance->my_rotation_counter = 0;
				}
				if (instance->my_rotation_counter == 2) {
				log_printf (instance->totemsrp_log_level_debug,
					"retrans flag count %x token aru %x install seq %x aru %x %x",
					instance->my_retrans_flg_count, token->aru, instance->my_install_seq,
					instance->my_aru, token->seq);

					memb_state_operational_enter (instance);
					instance->my_rotation_counter = 0;
					instance->my_retrans_flg_count = 0;
				}
			}

			totemrrp_send_flush (instance->totemrrp_context);
			token_send (instance, token, forward_token);

#ifdef GIVEINFO
			tv_current = qb_util_nano_current_get ();
			tv_diff = tv_current - tv_old;
			tv_old = tv_current;
			log_printf (instance->totemsrp_log_level_debug,
				"I held %0.4f ms",
				((float)tv_diff) / 1000000.0);
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
	struct mcast *mcast_in;
	struct mcast mcast_header;
	unsigned int range = 0;
	int endian_conversion_required;
	unsigned int my_high_delivered_stored = 0;


	range = end_point - instance->my_high_delivered;

	if (range) {
		log_printf (instance->totemsrp_log_level_trace,
			"Delivering %x to %x", instance->my_high_delivered,
			end_point);
	}
	assert (range < QUEUE_RTR_ITEMS_SIZE_MAX);
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

		mcast_in = sort_queue_item_p->mcast;
		assert (mcast_in != (struct mcast *)0xdeadbeef);

		endian_conversion_required = 0;
		if (mcast_in->header.endian_detector != ENDIAN_LOCAL) {
			endian_conversion_required = 1;
			mcast_endian_convert (mcast_in, &mcast_header);
		} else {
			memcpy (&mcast_header, mcast_in, sizeof (struct mcast));
		}

		/*
		 * Skip messages not originated in instance->my_deliver_memb
		 */
		if (skip &&
			memb_set_subset (&mcast_header.system_from,
				1,
				instance->my_deliver_memb_list,
				instance->my_deliver_memb_entries) == 0) {

			instance->my_high_delivered = my_high_delivered_stored + i;

			continue;
		}

		/*
		 * Message found
		 */
		log_printf (instance->totemsrp_log_level_trace,
			"Delivering MCAST message with seq %x to pending delivery queue",
			mcast_header.seq);

		/*
		 * Message is locally originated multicast
		 */
		instance->totemsrp_deliver_fn (
			mcast_header.header.nodeid,
			((char *)sort_queue_item_p->mcast) + sizeof (struct mcast),
			sort_queue_item_p->msg_len - sizeof (struct mcast),
			endian_conversion_required);
	}
}

/*
 * recv message handler called when MCAST message type received
 */
static int message_handler_mcast (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
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

	if (mcast_header.header.encapsulated == MESSAGE_ENCAPSULATED) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}

	assert (msg_len <= FRAME_SIZE_MAX);

#ifdef TEST_DROP_MCAST_PERCENTAGE
	if (random()%100 < TEST_DROP_MCAST_PERCENTAGE) {
		return (0);
	}
#endif

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
			memb_state_gather_enter (instance, TOTEMSRP_GSFROM_FOREIGN_MESSAGE_IN_OPERATIONAL_STATE);
			break;

		case MEMB_STATE_GATHER:
			if (!memb_set_subset (
				&mcast_header.system_from,
				1,
				instance->my_proc_list,
				instance->my_proc_list_entries)) {

				memb_set_merge (&mcast_header.system_from, 1,
					instance->my_proc_list, &instance->my_proc_list_entries);
				memb_state_gather_enter (instance, TOTEMSRP_GSFROM_FOREIGN_MESSAGE_IN_GATHER_STATE);
				return (0);
			}
			break;

		case MEMB_STATE_COMMIT:
			/* discard message */
			instance->stats.rx_msg_dropped++;
			break;

		case MEMB_STATE_RECOVERY:
			/* discard message */
			instance->stats.rx_msg_dropped++;
			break;
		}
		return (0);
	}

	log_printf (instance->totemsrp_log_level_trace,
		"Received ringid(%s:%lld) seq %x",
		totemip_print (&mcast_header.ring_id.rep),
		mcast_header.ring_id.seq,
		mcast_header.seq);

	/*
	 * Add mcast message to rtr queue if not already in rtr queue
	 * otherwise free io vectors
	 */
	if (msg_len > 0 && msg_len <= FRAME_SIZE_MAX &&
		sq_in_range (sort_queue, mcast_header.seq) &&
		sq_item_inuse (sort_queue, mcast_header.seq) == 0) {

		/*
		 * Allocate new multicast memory block
		 */
// TODO LEAK
		sort_queue_item.mcast = totemsrp_buffer_alloc (instance);
		if (sort_queue_item.mcast == NULL) {
			return (-1); /* error here is corrected by the algorithm */
		}
		memcpy (sort_queue_item.mcast, msg, msg_len);
		sort_queue_item.msg_len = msg_len;

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
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed)
{
	struct memb_merge_detect memb_merge_detect;


	if (endian_conversion_needed) {
		memb_merge_detect_endian_convert (msg, &memb_merge_detect);
	} else {
		memcpy (&memb_merge_detect, msg,
			sizeof (struct memb_merge_detect));
	}

	/*
	 * do nothing if this is a merge detect from this configuration
	 */
	if (memcmp (&instance->my_ring_id, &memb_merge_detect.ring_id,
		sizeof (struct memb_ring_id)) == 0) {

		return (0);
	}

	/*
	 * Execute merge operation
	 */
	switch (instance->memb_state) {
	case MEMB_STATE_OPERATIONAL:
		memb_set_merge (&memb_merge_detect.system_from, 1,
			instance->my_proc_list, &instance->my_proc_list_entries);
		memb_state_gather_enter (instance, TOTEMSRP_GSFROM_MERGE_DURING_OPERATIONAL_STATE);
		break;

	case MEMB_STATE_GATHER:
		if (!memb_set_subset (
			&memb_merge_detect.system_from,
			1,
			instance->my_proc_list,
			instance->my_proc_list_entries)) {

			memb_set_merge (&memb_merge_detect.system_from, 1,
				instance->my_proc_list, &instance->my_proc_list_entries);
			memb_state_gather_enter (instance, TOTEMSRP_GSFROM_MERGE_DURING_GATHER_STATE);
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

static void memb_join_process (
	struct totemsrp_instance *instance,
	const struct memb_join *memb_join)
{
	struct srp_addr *proc_list;
	struct srp_addr *failed_list;
	int gather_entered = 0;
	int fail_minus_memb_entries = 0;
	struct srp_addr fail_minus_memb[PROCESSOR_COUNT_MAX];

	proc_list = (struct srp_addr *)memb_join->end_of_memb_join;
	failed_list = proc_list + memb_join->proc_list_entries;

/*
	memb_set_print ("proclist", proc_list, memb_join->proc_list_entries);
	memb_set_print ("faillist", failed_list, memb_join->failed_list_entries);
	memb_set_print ("my_proclist", instance->my_proc_list, instance->my_proc_list_entries);
	memb_set_print ("my_faillist", instance->my_failed_list, instance->my_failed_list_entries);
-*/

	if (memb_join->header.type == MESSAGE_TYPE_MEMB_JOIN) {
		if (instance->flushing) {
			if (memb_join->header.nodeid == LEAVE_DUMMY_NODEID) {
				log_printf (instance->totemsrp_log_level_warning,
			    		"Discarding LEAVE message during flush, nodeid=%u", 
						memb_join->failed_list_entries > 0 ? failed_list[memb_join->failed_list_entries - 1 ].addr[0].nodeid : LEAVE_DUMMY_NODEID);
				if (memb_join->failed_list_entries > 0) {
					my_leave_memb_set(instance, failed_list[memb_join->failed_list_entries - 1 ].addr[0].nodeid);
				}
			} else {
				log_printf (instance->totemsrp_log_level_warning,
			    		"Discarding JOIN message during flush, nodeid=%d", memb_join->header.nodeid);
			}
			return;
		} else {
			if (memb_join->header.nodeid == LEAVE_DUMMY_NODEID) {
				log_printf (instance->totemsrp_log_level_debug,
				    "Received LEAVE message from %u", memb_join->failed_list_entries > 0 ? failed_list[memb_join->failed_list_entries - 1 ].addr[0].nodeid : LEAVE_DUMMY_NODEID);
				if (memb_join->failed_list_entries > 0) {
					my_leave_memb_set(instance, failed_list[memb_join->failed_list_entries - 1 ].addr[0].nodeid);
				}
			}
		}
		
	}

	if (memb_set_equal (proc_list,
		memb_join->proc_list_entries,
		instance->my_proc_list,
		instance->my_proc_list_entries) &&

	memb_set_equal (failed_list,
		memb_join->failed_list_entries,
		instance->my_failed_list,
		instance->my_failed_list_entries)) {

		memb_consensus_set (instance, &memb_join->system_from);

		if (memb_consensus_agreed (instance) && instance->failed_to_recv == 1) {
				instance->failed_to_recv = 0;
				srp_addr_copy (&instance->my_proc_list[0],
					&instance->my_id);
				instance->my_proc_list_entries = 1;
				instance->my_failed_list_entries = 0;

				memb_state_commit_token_create (instance);

				memb_state_commit_enter (instance);
				return;
		}
		if (memb_consensus_agreed (instance) &&
			memb_lowest_in_config (instance)) {

			memb_state_commit_token_create (instance);

			memb_state_commit_enter (instance);
		} else {
			goto out;
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

		goto out;
	} else
	if (memb_set_subset (&memb_join->system_from, 1,
		instance->my_failed_list, instance->my_failed_list_entries)) {

		goto out;
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
			if (memb_set_subset (
				&memb_join->system_from, 1,
				instance->my_memb_list,
				instance->my_memb_entries)) {

				if (memb_set_subset (
					&memb_join->system_from, 1,
					instance->my_failed_list,
					instance->my_failed_list_entries) == 0) {

					memb_set_merge (failed_list,
						memb_join->failed_list_entries,
						instance->my_failed_list, &instance->my_failed_list_entries);
				} else {
					memb_set_subtract (fail_minus_memb,
						&fail_minus_memb_entries,
						failed_list,
						memb_join->failed_list_entries,
						instance->my_memb_list,
						instance->my_memb_entries);

					memb_set_merge (fail_minus_memb,
						fail_minus_memb_entries,
						instance->my_failed_list,
						&instance->my_failed_list_entries);
				}
			}
		}
		memb_state_gather_enter (instance, TOTEMSRP_GSFROM_MERGE_DURING_JOIN);
		gather_entered = 1;
	}

out:
	if (gather_entered == 0 &&
		instance->memb_state == MEMB_STATE_OPERATIONAL) {

		memb_state_gather_enter (instance, TOTEMSRP_GSFROM_JOIN_DURING_OPERATIONAL_STATE);
	}
}

static void memb_join_endian_convert (const struct memb_join *in, struct memb_join *out)
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

static void memb_commit_token_endian_convert (const struct memb_commit_token *in, struct memb_commit_token *out)
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
	totemip_copy_endian_convert(&out->ring_id.rep, &in->ring_id.rep);
	out->ring_id.seq = swab64 (in->ring_id.seq);
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
			totemip_copy_endian_convert (&out_memb_list[i].ring_id.rep,
				     &in_memb_list[i].ring_id.rep);

			out_memb_list[i].ring_id.seq =
				swab64 (in_memb_list[i].ring_id.seq);
			out_memb_list[i].aru = swab32 (in_memb_list[i].aru);
			out_memb_list[i].high_delivered = swab32 (in_memb_list[i].high_delivered);
			out_memb_list[i].received_flg = swab32 (in_memb_list[i].received_flg);
		}
	}
}

static void orf_token_endian_convert (const struct orf_token *in, struct orf_token *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->seq = swab32 (in->seq);
	out->token_seq = swab32 (in->token_seq);
	out->aru = swab32 (in->aru);
	totemip_copy_endian_convert(&out->ring_id.rep, &in->ring_id.rep);
	out->aru_addr = swab32(in->aru_addr);
	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->fcc = swab32 (in->fcc);
	out->backlog = swab32 (in->backlog);
	out->retrans_flg = swab32 (in->retrans_flg);
	out->rtr_list_entries = swab32 (in->rtr_list_entries);
	for (i = 0; i < out->rtr_list_entries; i++) {
		totemip_copy_endian_convert(&out->rtr_list[i].ring_id.rep, &in->rtr_list[i].ring_id.rep);
		out->rtr_list[i].ring_id.seq = swab64 (in->rtr_list[i].ring_id.seq);
		out->rtr_list[i].seq = swab32 (in->rtr_list[i].seq);
	}
}

static void mcast_endian_convert (const struct mcast *in, struct mcast *out)
{
	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->header.encapsulated = in->header.encapsulated;

	out->seq = swab32 (in->seq);
	out->this_seqno = swab32 (in->this_seqno);
	totemip_copy_endian_convert(&out->ring_id.rep, &in->ring_id.rep);
	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->node_id = swab32 (in->node_id);
	out->guarantee = swab32 (in->guarantee);
	srp_addr_copy_endian_convert (&out->system_from, &in->system_from);
}

static void memb_merge_detect_endian_convert (
	const struct memb_merge_detect *in,
	struct memb_merge_detect *out)
{
	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	totemip_copy_endian_convert(&out->ring_id.rep, &in->ring_id.rep);
	out->ring_id.seq = swab64 (in->ring_id.seq);
	srp_addr_copy_endian_convert (&out->system_from, &in->system_from);
}

static int ignore_join_under_operational (
	struct totemsrp_instance *instance,
	const struct memb_join *memb_join)
{
	struct srp_addr *proc_list;
	struct srp_addr *failed_list;
	unsigned long long ring_seq;

	proc_list = (struct srp_addr *)memb_join->end_of_memb_join;
	failed_list = proc_list + memb_join->proc_list_entries;
	ring_seq = memb_join->ring_seq;

	if (memb_set_subset (&instance->my_id, 1,
	    failed_list, memb_join->failed_list_entries)) {
		return (1);
	}

	/*
	 * In operational state, my_proc_list is exactly the same as
	 * my_memb_list.
	 */
	if ((memb_set_subset (&memb_join->system_from, 1,
	    instance->my_memb_list, instance->my_memb_entries)) &&
	    (ring_seq < instance->my_ring_id.seq)) {
		return (1);
	}

	return (0);
}

static int message_handler_memb_join (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed)
{
	const struct memb_join *memb_join;
	struct memb_join *memb_join_convert = alloca (msg_len);

	if (endian_conversion_needed) {
		memb_join = memb_join_convert;
		memb_join_endian_convert (msg, memb_join_convert);

	} else {
		memb_join = msg;
	}
	/*
	 * If the process paused because it wasn't scheduled in a timely
	 * fashion, flush the join messages because they may be queued
	 * entries
	 */
	if (pause_flush (instance)) {
		return (0);
	}

	if (instance->token_ring_id_seq < memb_join->ring_seq) {
		instance->token_ring_id_seq = memb_join->ring_seq;
	}
	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			if (!ignore_join_under_operational (instance, memb_join)) {
				memb_join_process (instance, memb_join);
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
				memb_state_gather_enter (instance, TOTEMSRP_GSFROM_JOIN_DURING_COMMIT_STATE);
			}
			break;

		case MEMB_STATE_RECOVERY:
			if (memb_set_subset (&memb_join->system_from,
				1,
				instance->my_new_memb_list,
				instance->my_new_memb_entries) &&

				memb_join->ring_seq >= instance->my_ring_id.seq) {

				memb_join_process (instance, memb_join);
				memb_recovery_state_token_loss (instance);
				memb_state_gather_enter (instance, TOTEMSRP_GSFROM_JOIN_DURING_RECOVERY);
			}
			break;
	}
	return (0);
}

static int message_handler_memb_commit_token (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed)
{
	struct memb_commit_token *memb_commit_token_convert = alloca (msg_len);
	struct memb_commit_token *memb_commit_token;
	struct srp_addr sub[PROCESSOR_COUNT_MAX];
	int sub_entries;

	struct srp_addr *addr;

	log_printf (instance->totemsrp_log_level_debug,
		"got commit token");

	if (endian_conversion_needed) {
		memb_commit_token_endian_convert (msg, memb_commit_token_convert);
	} else {
		memcpy (memb_commit_token_convert, msg, msg_len);
	}
	memb_commit_token = memb_commit_token_convert;
	addr = (struct srp_addr *)memb_commit_token->end_of_commit_token;

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
				memcpy (instance->commit_token, memb_commit_token, msg_len);
				memb_state_commit_enter (instance);
			}
			break;

		case MEMB_STATE_COMMIT:
			/*
			 * If retransmitted commit tokens are sent on this ring
			 * filter them out and only enter recovery once the
			 * commit token has traversed the array.  This is
			 * determined by :
		 	 * memb_commit_token->memb_index == memb_commit_token->addr_entries) {
			 */
			 if (memb_commit_token->ring_id.seq == instance->my_ring_id.seq &&
				memb_commit_token->memb_index == memb_commit_token->addr_entries) {
				memb_state_recovery_enter (instance, memb_commit_token);
			}
			break;

		case MEMB_STATE_RECOVERY:
			if (totemip_equal (&instance->my_id.addr[0], &instance->my_ring_id.rep)) {

				/* Filter out duplicated tokens */
				if (instance->originated_orf_token) {
					break;
				}

				instance->originated_orf_token = 1;

				log_printf (instance->totemsrp_log_level_debug,
					"Sending initial ORF token");

				// TODO convert instead of initiate
				orf_token_send_initial (instance);
				reset_token_timeout (instance); // REVIEWED
				reset_token_retransmit_timeout (instance); // REVIEWED
			}
			break;
	}
	return (0);
}

static int message_handler_token_hold_cancel (
	struct totemsrp_instance *instance,
	const void *msg,
	size_t msg_len,
	int endian_conversion_needed)
{
	const struct token_hold_cancel *token_hold_cancel = msg;

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
	const void *msg,
	unsigned int msg_len)
{
	struct totemsrp_instance *instance = context;
	const struct message_header *message_header = msg;

	if (msg_len < sizeof (struct message_header)) {
		log_printf (instance->totemsrp_log_level_security,
			    "Received message is too short...  ignoring %u.",
			    (unsigned int)msg_len);
		return;
	}


	switch (message_header->type) {
	case MESSAGE_TYPE_ORF_TOKEN:
		instance->stats.orf_token_rx++;
		break;
	case MESSAGE_TYPE_MCAST:
		instance->stats.mcast_rx++;
		break;
	case MESSAGE_TYPE_MEMB_MERGE_DETECT:
		instance->stats.memb_merge_detect_rx++;
		break;
	case MESSAGE_TYPE_MEMB_JOIN:
		instance->stats.memb_join_rx++;
		break;
	case MESSAGE_TYPE_MEMB_COMMIT_TOKEN:
		instance->stats.memb_commit_token_rx++;
		break;
	case MESSAGE_TYPE_TOKEN_HOLD_CANCEL:
		instance->stats.token_hold_cancel_rx++;
		break;
	default:
		log_printf (instance->totemsrp_log_level_security, "Type of received message is wrong...  ignoring %d.\n", (int)message_header->type);
printf ("wrong message type\n");
		instance->stats.rx_msg_dropped++;
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
	const struct totem_ip_address *iface_addr,
	unsigned int iface_no)
{
	struct totemsrp_instance *instance = context;
	int i;

	totemip_copy (&instance->my_id.addr[iface_no], iface_addr);
	assert (instance->my_id.addr[iface_no].nodeid);

	totemip_copy (&instance->my_memb_list[0].addr[iface_no], iface_addr);

	if (instance->iface_changes++ == 0) {
		instance->memb_ring_id_create_or_load (&instance->my_ring_id,
		    &instance->my_id.addr[0]);
		instance->token_ring_id_seq = instance->my_ring_id.seq;
		log_printf (
			instance->totemsrp_log_level_debug,
			"Created or loaded sequence id %llx.%s for this ring.",
			instance->my_ring_id.seq,
			totemip_print (&instance->my_ring_id.rep));

		if (instance->totemsrp_service_ready_fn) {
			instance->totemsrp_service_ready_fn ();
		}

	}

	for (i = 0; i < instance->totem_config->interfaces[iface_no].member_count; i++) {
		totemsrp_member_add (instance,
			&instance->totem_config->interfaces[iface_no].member_list[i],
			iface_no);
	}

	if (instance->iface_changes >= instance->totem_config->interface_count) {
		memb_state_gather_enter (instance, TOTEMSRP_GSFROM_INTERFACE_CHANGE);
	}
}

void totemsrp_net_mtu_adjust (struct totem_config *totem_config) {
	totem_config->net_mtu -= sizeof (struct mcast);
}

void totemsrp_service_ready_register (
	void *context,
        void (*totem_service_ready) (void))
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;

	instance->totemsrp_service_ready_fn = totem_service_ready;
}

int totemsrp_member_add (
        void *context,
        const struct totem_ip_address *member,
        int ring_no)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;
	int res;

	res = totemrrp_member_add (instance->totemrrp_context, member, ring_no);

	return (res);
}

int totemsrp_member_remove (
        void *context,
        const struct totem_ip_address *member,
        int ring_no)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;
	int res;

	res = totemrrp_member_remove (instance->totemrrp_context, member, ring_no);

	return (res);
}

void totemsrp_threaded_mode_enable (void *context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;

	instance->threaded_mode_enabled = 1;
}

void totemsrp_trans_ack (void *context)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;

	instance->waiting_trans_ack = 0;
	instance->totemsrp_waiting_trans_ack_cb_fn (0);
}
