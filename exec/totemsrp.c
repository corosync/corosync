int my_token_held = 0;
int my_do_delivery = 0;
unsigned long long token_ring_id_seq = 0;
int log_digest = 0;
int last_released = 0;
int set_aru = -1;
int totemsrp_brake;
		
/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
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

#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
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
#include "totemsrp.h"
#include "../include/queue.h"
#include "../include/sq.h"
#include "../include/list.h"
#include "hdb.h"
#include "swab.h"

#include "crypto.h"
#define AUTHENTICATION 1 /* use authentication */
#define ENCRYPTION 1	 /* use encryption */

#define LOCALHOST_IP					inet_addr("127.0.0.1")
#define QUEUE_RTR_ITEMS_SIZE_MAX		2000 /* allow 512 retransmit items */
#define NEW_MESSAGE_QUEUE_SIZE_MAX		2000 /* allow 500 messages to be queued */
#define RETRANS_MESSAGE_QUEUE_SIZE_MAX	2000 /* allow 500 messages to be queued */
#define RECEIVED_MESSAGE_QUEUE_SIZE_MAX	2000 /* allow 500 messages to be queued */
#define MAXIOVS							5
#define RETRANSMIT_ENTRIES_MAX			30
#define MISSING_MCAST_WINDOW			128
#define TIMEOUT_STATE_GATHER_JOIN		100
#define TIMEOUT_STATE_GATHER_CONSENSUS	200
#define TIMEOUT_TOKEN					1000
#define TIMEOUT_TOKEN_RETRANSMIT		200
#define PACKET_SIZE_MAX					2000
#define FAIL_TO_RECV_CONST				250
#define SEQNO_UNCHANGED_CONST			20

/*
 * we compare incoming messages to determine if their endian is
 * different - if so convert them
 *
 * do not change
 */
#define ENDIAN_LOCAL					 0xff22



/*
 * Authentication of messages
 */
hmac_state totemsrp_hmac_state;
prng_state totemsrp_prng_state;

unsigned char totemsrp_private_key[1024];
unsigned int totemsrp_private_key_len;

int stats_sent = 0;
int stats_recv = 0;
int stats_delv = 0;
int stats_remcasts = 0;
int stats_orf_token = 0;
struct timeval stats_tv_start = { 0, 0 };

/*
 * Flow control mcasts and remcasts on last and current orf_token
 */
int fcc_remcast_last = 0;
int fcc_mcast_last = 0;
int fcc_mcast_current = 0;
int fcc_remcast_current = 0;

enum message_type {
	MESSAGE_TYPE_ORF_TOKEN = 0,			/* Ordering, Reliability, Flow (ORF) control Token */
	MESSAGE_TYPE_MCAST = 1,				/* ring ordered multicast message */
	MESSAGE_TYPE_MEMB_JOIN = 2, 		/* membership join message */
	MESSAGE_TYPE_MEMB_COMMIT_TOKEN = 3,	/* membership commit token */
};

/* 
 * New membership algorithm local variables
 */
struct consensus_list_item {
	struct in_addr addr;
	int set;
};

static struct consensus_list_item consensus_list[PROCESSOR_COUNT_MAX];

static int consensus_list_entries;

static struct in_addr my_proc_list[PROCESSOR_COUNT_MAX];

static struct in_addr my_failed_list[PROCESSOR_COUNT_MAX];

static struct in_addr my_new_memb_list[PROCESSOR_COUNT_MAX];

static struct in_addr my_trans_memb_list[PROCESSOR_COUNT_MAX];

static struct in_addr my_memb_list[PROCESSOR_COUNT_MAX];

static struct in_addr my_deliver_memb_list[PROCESSOR_COUNT_MAX];

static int my_proc_list_entries = 0;

static int my_failed_list_entries = 0;

static int my_new_memb_entries = 0;

static int my_trans_memb_entries = 0;

static int my_memb_entries = 0;

static int my_deliver_memb_entries = 0;

static struct memb_ring_id my_ring_id;

static int my_aru_count = 0;

static int my_last_aru = 0;

static int my_seq_unchanged = 0;

static int my_received_flg = 1;

static int my_high_seq_received;

static int my_install_seq = 0;

static int my_rotation_counter = 0;

static int my_set_retrans_flg = 0;

static int my_retrans_flg_count = 0;

static unsigned int my_high_ring_delivered = 0;

static unsigned int my_high_seq_delivered = 0;

static unsigned int my_old_high_seq_delivered = 0;

struct token_callback_instance {
	struct list_head list;
	int (*callback_fn) (enum totemsrp_callback_token_type type, void *);
	enum totemsrp_callback_token_type callback_type;
	int delete;
	void *data;
};

/*
 * Queues used to order, deliver, and recover messages
 */
struct queue new_message_queue;
struct queue retrans_message_queue;
struct sq regular_sort_queue;
struct sq recovery_sort_queue;

/*
 * Multicast address
 */
struct sockaddr_in sockaddr_in_mcast;

struct totemsrp_socket {
	int mcast;
	int token;
};

/*
 * File descriptors in use by TOTEMSRP
 */
struct totemsrp_socket totemsrp_sockets[2];

/*
 * Received up to and including
 */
int my_aru = 0;

int my_aru_save = 0;

int my_high_seq_received_save = 0;

DECLARE_LIST_INIT (token_callback_received_listhead);
DECLARE_LIST_INIT (token_callback_sent_listhead);

char orf_token_retransmit[15000]; // sizeof (struct orf_token) + sizeof (struct rtr_item) * RETRANSMIT_ENTRIES_MAX];

int orf_token_retransmit_size;

int my_token_seq = -1;

/*
 * Timers
 */
poll_timer_handle timer_orf_token_timeout = 0;

poll_timer_handle timer_orf_token_retransmit_timeout = 0;

poll_timer_handle memb_timer_state_gather_join_timeout = 0;

poll_timer_handle memb_timer_state_gather_consensus_timeout = 0;

poll_timer_handle memb_timer_state_commit_timeout = 0;

/*
 * Function called when new message received
 */
int (*totemsrp_recv) (char *group, struct iovec *iovec, int iov_len);

/*
 * Function and data used to log messages
 */
static void (*totemsrp_log_printf) (int level, char *format, ...);
int totemsrp_log_level_security;
int totemsrp_log_level_error;
int totemsrp_log_level_warning;
int totemsrp_log_level_notice;
int totemsrp_log_level_debug;

#define HMAC_HASH_SIZE 20
struct security_header {
	unsigned char hash_digest[HMAC_HASH_SIZE]; /* The hash *MUST* be first in the data structure */
	unsigned char salt[16]; /* random number */
} __attribute__((packed));

struct message_header {
	struct security_header security_header;
	char type;
	char encapsulated;
//	unsigned short filler;
	unsigned short endian_detector;
} __attribute__((packed));

struct mcast {
	struct message_header header;
	int seq;
	struct memb_ring_id ring_id;
	struct in_addr source;
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
#define FRAGMENT_SIZE (PACKET_SIZE_MAX - sizeof (struct mcast) - 20 - 8)

struct rtr_item  {
	struct memb_ring_id ring_id;
	int seq;
}__attribute__((packed));

struct orf_token {
	struct message_header header;
	int seq;
	int token_seq;
	int aru;
	struct in_addr aru_addr;
	struct memb_ring_id ring_id; 
	short int fcc;
	int retrans_flg;
	int rtr_list_entries;
	struct rtr_item rtr_list[0];
}__attribute__((packed));

struct memb_join {
	struct message_header header;
	struct in_addr proc_list[PROCESSOR_COUNT_MAX];
	int proc_list_entries;
	struct in_addr failed_list[PROCESSOR_COUNT_MAX];
	int failed_list_entries;
	unsigned long long ring_seq;
} __attribute__((packed));

struct memb_commit_token_memb_entry {
	struct memb_ring_id ring_id;
	int aru;
	int high_delivered;
	int received_flg;
}__attribute__((packed));

struct memb_commit_token {
	struct message_header header;
	int token_seq;
	struct memb_ring_id ring_id;
	unsigned int retrans_flg;
	int memb_index;
	int addr_entries;
	struct in_addr addr[PROCESSOR_COUNT_MAX];
	struct memb_commit_token_memb_entry memb_list[PROCESSOR_COUNT_MAX];
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

enum memb_state {
	MEMB_STATE_OPERATIONAL = 1,
	MEMB_STATE_GATHER = 2,
	MEMB_STATE_COMMIT = 3,
	MEMB_STATE_RECOVERY = 4
};

static enum memb_state memb_state = MEMB_STATE_OPERATIONAL;

static struct sockaddr_in my_id;

struct sockaddr_in next_memb;

static struct sockaddr_in memb_local_sockaddr_in;

static char iov_buffer[15000]; //PACKET_SIZE_MAX];

static struct iovec totemsrp_iov_recv = {
	.iov_base	= iov_buffer,
	.iov_len	= sizeof (iov_buffer)
};

static char iov_encrypted_buffer[15000]; //char orf_token_retransmit[15000]; // sizeof (struct orf_token) + sizeof (struct rtr_item) * RETRANSMIT_ENTRIES_MAX];

static struct iovec iov_encrypted = {
	.iov_base	= iov_encrypted_buffer,
	.iov_len	= sizeof (iov_encrypted_buffer)
};

struct message_handlers {
	int count;
	int (*handler_functions[4]) (struct sockaddr_in *, struct iovec *, int, int, int);
};

poll_handle *totemsrp_poll_handle;

void (*totemsrp_deliver_fn) (
	struct in_addr source_addr,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required) = 0;

void (*totemsrp_confchg_fn) (
	enum totemsrp_configuration_type configuration_type,
	struct in_addr *member_list, void *member_list_private,
		int member_list_entries,
	struct in_addr *left_list, void *left_list_private,
		int left_list_entries,
	struct in_addr *joined_list, void *joined_list_private,
		int joined_list_entries,
	struct memb_ring_id *ring_id) = 0;

/*
 * forward decls
 */
static int message_handler_orf_token (struct sockaddr_in *, struct iovec *, int, int, int);
static int message_handler_mcast (struct sockaddr_in *, struct iovec *, int, int, int);

static int message_handler_memb_join (struct sockaddr_in *, struct iovec *, int, int, int);

static int message_handler_memb_commit_token (struct sockaddr_in *, struct iovec *, int, int, int);

static void memb_ring_id_create_or_load (struct memb_ring_id *);
static int recv_handler (poll_handle handle, int fd, int revents, void *data, unsigned int *prio);
static int netif_determine (struct sockaddr_in *bindnet, struct sockaddr_in *bound_to);
static int totemsrp_build_sockets (struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	struct totemsrp_socket *sockets,
	struct sockaddr_in *bound_to);
static void memb_state_gather_enter (void);
static void messages_deliver_to_app (int skip, int *start_point, int end_point);
static int orf_token_mcast (struct orf_token *oken,
	int fcc_mcasts_allowed, struct sockaddr_in *system_from);
static int messages_free (int token_aru);

static void encrypt_and_sign (struct iovec *iovec, int iov_len);
static int authenticate_and_decrypt (struct iovec *iov);
static int recv_handler (poll_handle handle, int fd, int revents, void *data, unsigned int *prio);
static void memb_ring_id_store (struct memb_commit_token *commit_token);
static void memb_state_commit_token_update (struct memb_commit_token *memb_commit_token);
static int memb_state_commit_token_send (struct memb_commit_token *memb_commit_token);
static void memb_state_commit_token_create (struct memb_commit_token *commit_token);
static void orf_token_endian_convert (struct orf_token *in, struct orf_token *out);
static void memb_commit_token_endian_convert (struct memb_commit_token *in, struct memb_commit_token *out);
static void memb_join_endian_convert (struct memb_join *in, struct memb_join *out);
static void mcast_endian_convert (struct mcast *in, struct mcast *out);

struct message_handlers totemsrp_message_handlers = {
	4,
	{
		message_handler_orf_token,
		message_handler_mcast,
		message_handler_memb_join,
		message_handler_memb_commit_token
	}
};

void totemsrp_log_printf_init (
	void (*log_printf) (int , char *, ...),
	int log_level_security,
	int log_level_error,
	int log_level_warning,
	int log_level_notice,
	int log_level_debug)
{
	totemsrp_log_level_security = log_level_security;
	totemsrp_log_level_error = log_level_error;
	totemsrp_log_level_warning = log_level_warning;
	totemsrp_log_level_notice = log_level_notice;
	totemsrp_log_level_debug = log_level_debug;
	totemsrp_log_printf = log_printf;
}

#ifdef CODE_COVERAGE_COMPILE_OUT
void print_digest (char *where, unsigned char *digest)
{
	int i;

	printf ("DIGEST %s:\n", where);
	for (i = 0; i < 16; i++) {
		printf ("%x ", digest[i]);
	}
	printf ("\n");
}

void print_msg (unsigned char *msg, int size)
{
	int i;
	printf ("MSG CONTENTS START\n");
	for (i = 0; i < size; i++) {
		printf ("%x ", msg[i]);
		if ((i % 16) == 15) {
			printf ("\n");
		}
	}
	printf ("MSG CONTENTS DONE\n");
}
#endif

/*
 * Exported interfaces
 */
int totemsrp_initialize (
	struct sockaddr_in *sockaddr_mcast,
	struct totemsrp_interface *interfaces,
	int interface_count,
	poll_handle *poll_handle,
	unsigned char *private_key,
	int private_key_len,
	void *member_private,
	int member_private_len,
	void (*deliver_fn) (
		struct in_addr source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required),
	void (*confchg_fn) (
		enum totemsrp_configuration_type configuration_type,
		struct in_addr *member_list, void *member_list_private,
			int member_list_entries,
		struct in_addr *left_list, void *left_list_private,
			int left_list_entries,
		struct in_addr *joined_list, void *joined_list_private,
			int joined_list_entries,
		struct memb_ring_id *ring_id))
{

	int res;
	int interface_no;

	/*
	 * Initialize random number generator for later use to generate salt
	 */
	memcpy (totemsrp_private_key, private_key, private_key_len);

	totemsrp_private_key_len = private_key_len;

	rng_make_prng (128, PRNG_SOBER, &totemsrp_prng_state, NULL);

	/*
	 * Initialize local variables for totemsrp
	 */
	memcpy (&sockaddr_in_mcast, sockaddr_mcast, sizeof (struct sockaddr_in));
	memset (&next_memb, 0, sizeof (struct sockaddr_in));
	memset (iov_buffer, 0, PACKET_SIZE_MAX);

	queue_init (&new_message_queue, NEW_MESSAGE_QUEUE_SIZE_MAX,
		sizeof (struct message_item));

	queue_init (&retrans_message_queue, RETRANS_MESSAGE_QUEUE_SIZE_MAX,
		sizeof (struct message_item));

	sq_init (&regular_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	sq_init (&recovery_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	/*
	 * Build sockets for every interface
	 */
	for (interface_no = 0; interface_no < interface_count; interface_no++) {
		/*
		 * Create and bind the multicast and unicast sockets
		 */
		res = totemsrp_build_sockets (sockaddr_mcast,
			&interfaces[interface_no].bindnet,
			&totemsrp_sockets[interface_no],
			&interfaces[interface_no].boundto);

		if (res == -1) {
			return (res);
		}
		totemsrp_poll_handle = poll_handle;

		poll_dispatch_add (*totemsrp_poll_handle, totemsrp_sockets[interface_no].mcast,
			POLLIN, 0, recv_handler, UINT_MAX);

		poll_dispatch_add (*totemsrp_poll_handle, totemsrp_sockets[interface_no].token,
			POLLIN, 0, recv_handler, UINT_MAX);
	}

	memcpy (&my_id, &interfaces->boundto, sizeof (struct sockaddr_in));

	/*
	 * This stuff depends on totemsrp_build_sockets
	 */
	my_memb_list[0].s_addr = interfaces->boundto.sin_addr.s_addr;

	memb_ring_id_create_or_load (&my_ring_id);
	totemsrp_log_printf (totemsrp_log_level_notice, "Created or loaded sequence id %lld.%s for this ring.\n",
		my_ring_id.seq, inet_ntoa (my_ring_id.rep));

	memb_state_gather_enter ();

	totemsrp_deliver_fn = deliver_fn;

	totemsrp_confchg_fn = confchg_fn;

	return (0);
}

/*
 * Set operations for use by the membership algorithm
 */
static void memb_consensus_reset (void)
{
	consensus_list_entries = 0;
}

void
memb_set_subtract (struct in_addr *out_list, int *out_list_entries,
	struct in_addr *one_list, int one_list_entries,
	struct in_addr *two_list, int two_list_entries)
{
	int found = 0;
	int i;
	int j;

	*out_list_entries = 0;

	for (i = 0; i < one_list_entries; i++) {
		for (j = 0; j < two_list_entries; j++) {
			if (one_list[i].s_addr == two_list[j].s_addr) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			out_list[*out_list_entries].s_addr = one_list[i].s_addr;
			*out_list_entries = *out_list_entries + 1;
		}
		found = 0;
	}
}

/*
 * Set consensus for a specific processor
 */
static void memb_consensus_set (struct in_addr *addr)
{
	int found = 0;
	int i;

	for (i = 0; i < consensus_list_entries; i++) {
		if (addr->s_addr == consensus_list[i].addr.s_addr) {
			found = 1;
			break; /* found entry */
		}
	}
	consensus_list[i].addr.s_addr = addr->s_addr;
	consensus_list[i].set = 1;
	if (found == 0) {
		consensus_list_entries++;
	}
	return;
}

/*
 * Is consensus set for a specific processor
 */
static int memb_consensus_isset (struct in_addr *addr)
{
	int i;

	for (i = 0; i < consensus_list_entries; i++) {
		if (addr->s_addr == consensus_list[i].addr.s_addr) {
			return (consensus_list[i].set);
		}
	}
	return (0);
}

/*
 * Is consensus agreed upon based upon consensus database
 */
static int memb_consensus_agreed (void)
{
	struct in_addr token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;
	int agreed = 1;
	int i;

	memb_set_subtract (token_memb, &token_memb_entries,
		my_proc_list, my_proc_list_entries,
		my_failed_list, my_failed_list_entries);

	for (i = 0; i < token_memb_entries; i++) {
		if (memb_consensus_isset (&token_memb[i]) == 0) {
			agreed = 0;
			break;
		}
	}
	return (agreed);
}

void memb_consensus_notset (struct in_addr *no_consensus_list,
		int *no_consensus_list_entries,
		struct in_addr *comparison_list,
		int comparison_list_entries)
{
	int i;

	*no_consensus_list_entries = 0;

	for (i = 0; i < my_proc_list_entries; i++) {
		if (memb_consensus_isset (&my_proc_list[i]) == 0) {
			no_consensus_list[*no_consensus_list_entries].s_addr = my_proc_list[i].s_addr;
			*no_consensus_list_entries = *no_consensus_list_entries + 1;
		}
	}
}

/*
 * Is set1 equal to set2 Entries can be in different orders
 */
int memb_set_equal (struct in_addr *set1, int set1_entries,
	struct in_addr *set2, int set2_entries)
{
	int i;
	int j;

	int found = 0;

	if (set1_entries != set2_entries) {
		return (0);
	}
	for (i = 0; i < set2_entries; i++) {
		for (j = 0; j < set1_entries; j++) {
			if (set1[j].s_addr == set2[i].s_addr) {
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
int memb_set_subset (struct in_addr *subset, int subset_entries,
	struct in_addr *fullset, int fullset_entries)
{
	int i;
	int j;
	int found = 0;

	if (subset_entries > fullset_entries) {
		return (0);
	}
	for (i = 0; i < subset_entries; i++) {
		for (j = 0; j < fullset_entries; j++) {
			if (subset[i].s_addr == fullset[j].s_addr) {
				found = 1;
			}
		}
		if (found == 0) {
			return (0);
		}
		found = 1;
	}
	return (1);
}

/*
 * merge subset into fullset taking care not to add duplicates
 */
void memb_set_merge (struct in_addr *subset, int subset_entries,
	struct in_addr *fullset, int *fullset_entries)
{
	int found = 0;
	int i;
	int j;

	for (i = 0; i < subset_entries; i++) {
		for (j = 0; j < *fullset_entries; j++) {
			if (fullset[j].s_addr == subset[i].s_addr) {
				found = 1;
				break;
			}	
		}
		if (found == 0) {
			fullset[j].s_addr = subset[i].s_addr;
			*fullset_entries = *fullset_entries + 1;
		}
		found = 0;
	}
	return;
}

void memb_set_and (struct in_addr *set1, int set1_entries,
	struct in_addr *set2, int set2_entries,
	struct in_addr *and, int *and_entries)
{
	int i;
	int j;
	int found = 0;

	*and_entries = 0;

	for (i = 0; i < set2_entries; i++) {
		for (j = 0; j < set1_entries; j++) {
			if (set1[j].s_addr == set2[i].s_addr) {
				found = 1;
				break;
			}
		}
		if (found) {
			and[*and_entries].s_addr = set1[j].s_addr;
			*and_entries = *and_entries + 1;
		}
		found = 0;
	}
	return;
}

#ifdef CODE_COVERAGE_COMPILE_OUT
void memb_set_print (char *string,
	struct in_addr *list, int list_entries)
{
	int i;
	printf ("List '%s' contains %d entries:\n", string, list_entries);

	for (i = 0; i < list_entries; i++) {
		printf ("addr %s\n", inet_ntoa (list[i]));
	}
}
#endif

static void timer_function_orf_token_timeout (void *data);
static void timer_function_token_retransmit_timeout (void *data);

void reset_token_retransmit_timeout (void) {
			poll_timer_delete (*totemsrp_poll_handle,
				timer_orf_token_retransmit_timeout);
			poll_timer_add (*totemsrp_poll_handle, TIMEOUT_TOKEN_RETRANSMIT, 0,
				timer_function_token_retransmit_timeout,
				&timer_orf_token_retransmit_timeout);

}

void reset_token_timeout (void) {
			poll_timer_delete (*totemsrp_poll_handle, timer_orf_token_timeout);
			poll_timer_add (*totemsrp_poll_handle, TIMEOUT_TOKEN, (void *)9999,
				timer_function_orf_token_timeout, &timer_orf_token_timeout);
}

void cancel_token_timeout (void) {
		poll_timer_delete (*totemsrp_poll_handle, timer_orf_token_timeout);
}

void cancel_token_retransmit_timeout (void) {
		poll_timer_delete (*totemsrp_poll_handle, timer_orf_token_retransmit_timeout);
}

static void memb_state_consensus_timeout_expired (void)
{
	struct in_addr no_consensus_list[PROCESSOR_COUNT_MAX];
	int no_consensus_list_entries;

	if (memb_consensus_agreed ()) {
		memb_consensus_reset ();

		memb_consensus_set (&my_id.sin_addr);

		reset_token_timeout (); // REVIEWED
	} else {
		memb_consensus_notset (no_consensus_list,
			&no_consensus_list_entries,
			my_proc_list, my_proc_list_entries);

		memb_set_merge (no_consensus_list, no_consensus_list_entries,
			my_failed_list, &my_failed_list_entries);

		memb_state_gather_enter ();
	}
}

static int memb_join_message_send (void);

/*
 * Timers used for various states of the membership algorithm
 */
static void timer_function_orf_token_timeout (void *data)
{
	totemsrp_log_printf (totemsrp_log_level_notice,
		"The token was lost in state %d from timer %x\n", memb_state, data);
	switch (memb_state) {
		case MEMB_STATE_OPERATIONAL:
			memb_state_gather_enter ();
			break;

		case MEMB_STATE_GATHER:
			memb_state_consensus_timeout_expired ();
			memb_state_gather_enter ();
			break;

		case MEMB_STATE_COMMIT:
			memb_state_gather_enter ();
			break;
		
		case MEMB_STATE_RECOVERY:
printf ("setting my_aru %d to %d\n", my_aru, my_aru_save);
			my_aru = my_aru_save;
			my_high_seq_received = my_high_seq_received_save;
			sq_reinit (&recovery_sort_queue, 0);
			queue_reinit (&retrans_message_queue);
			// TODO calculate current old ring aru
			memb_state_gather_enter();
			break;
	}
}

static void memb_timer_function_state_gather (void *data)
{
	switch (memb_state) {
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		assert (0); /* this should never happen */
		break;
	case MEMB_STATE_GATHER:
	case MEMB_STATE_COMMIT:
		memb_join_message_send ();

		/*
		 * Restart the join timeout
		`*/
		poll_timer_delete (*totemsrp_poll_handle, memb_timer_state_gather_join_timeout);
	
		poll_timer_add (*totemsrp_poll_handle, TIMEOUT_STATE_GATHER_JOIN, 0,
			memb_timer_function_state_gather, &memb_timer_state_gather_join_timeout);
		break;
	}
}

static void memb_timer_function_gather_consensus_timeout (void *data)
{
	memb_state_consensus_timeout_expired ();
}

void deliver_messages_from_recovery_to_regular (void)
{
	int i;
	struct sort_queue_item *recovery_message_item;
	struct sort_queue_item regular_message_item;
	int res;
	void *ptr;
	struct mcast *mcast;

printf ("recovery to regular %d-%d\n", 1, my_aru);
	/*
	 * Move messages from recovery to regular sort queue
	 */
// todo should i be initialized to 0 or 1 ?
	for (i = 1; i <= my_aru; i++) {
		res = sq_item_get (&recovery_sort_queue, i, &ptr);
		if (res != 0) {
printf ("item not present in recovery sort queue\n");
			continue;
		}
		recovery_message_item = (struct sort_queue_item *)ptr;

		/*
		 * Convert recovery message into regular message
		 */
		if (recovery_message_item->iov_len > 1) {
			mcast = recovery_message_item->iovec[1].iov_base;
			memcpy (&regular_message_item.iovec[0],
				&recovery_message_item->iovec[1],
				sizeof (struct iovec) * recovery_message_item->iov_len);
		} else {
			regular_message_item.iovec[0].iov_base =
				recovery_message_item->iovec[0].iov_base + sizeof (struct mcast);
			regular_message_item.iovec[0].iov_len =
				recovery_message_item->iovec[0].iov_len - sizeof (struct mcast);
			mcast = regular_message_item.iovec[0].iov_base;
		}
		regular_message_item.iov_len = recovery_message_item->iov_len;

		res = sq_item_inuse (&regular_sort_queue, mcast->seq);
		if (res == 0) {
			sq_item_add (&regular_sort_queue,
				&regular_message_item, mcast->seq);
		}
	}
}

/*
 * Change states in the state machine of the membership algorithm
 */
static void memb_state_operational_enter (void)
{
	struct in_addr joined_list[PROCESSOR_COUNT_MAX];
	int joined_list_entries = 0;
	struct in_addr left_list[PROCESSOR_COUNT_MAX];
	int left_list_entries = 0;

	deliver_messages_from_recovery_to_regular ();

	printf ("Delivering to app %d to %d\n",
		my_old_high_seq_delivered, my_high_ring_delivered);
	messages_deliver_to_app (0, &my_old_high_seq_delivered, my_high_ring_delivered);

	/*
	 * Calculate joined and left list
	 */
	memb_set_subtract (left_list, &left_list_entries,
		my_memb_list, my_memb_entries,
		my_trans_memb_list, my_trans_memb_entries);

	memb_set_subtract (joined_list, &joined_list_entries,
		my_new_memb_list, my_new_memb_entries,
		my_trans_memb_list, my_trans_memb_entries);

	/*
	 * Deliver transitional configuration to application
	 */
	totemsrp_confchg_fn (TOTEMSRP_CONFIGURATION_TRANSITIONAL,
		my_trans_memb_list, 0, my_trans_memb_entries,
		left_list, 0, left_list_entries,
		0, 0, 0, &my_ring_id);
		
// TODO we need to filter to ensure we only deliver those
// messages which are part of my_deliver_memb
	messages_deliver_to_app (1, &my_old_high_seq_delivered, my_high_ring_delivered);

	/*
	 * Deliver regular configuration to application
	 */
	totemsrp_confchg_fn (TOTEMSRP_CONFIGURATION_REGULAR,
		my_new_memb_list, 0, my_new_memb_entries,
		0, 0, 0,
		joined_list, 0, joined_list_entries, &my_ring_id);

	/*
	 * Install new membership
	 */
	my_memb_entries = my_new_memb_entries;
	memcpy (my_memb_list, my_new_memb_list,
		sizeof (struct in_addr) * my_memb_entries);
	last_released = my_aru;
	my_set_retrans_flg = 0;
	sq_reinit (&regular_sort_queue, my_aru);
	sq_reinit (&recovery_sort_queue, 0);
	my_high_seq_delivered = my_aru;
	my_aru_save = my_aru;
	my_high_seq_received_save = my_aru;
	my_last_aru = 0;

	my_proc_list_entries = my_new_memb_entries;
	memcpy (my_proc_list, my_new_memb_list,
		sizeof (struct in_addr) * my_memb_entries);

	my_failed_list_entries = 0;
// TODO the recovery messages are leaked

	my_old_high_seq_delivered = 0;

	totemsrp_log_printf (totemsrp_log_level_notice, "entering OPERATIONAL state.\n");
	memb_state = MEMB_STATE_OPERATIONAL;
	return;
}

static void memb_state_gather_enter (void)
{
// TODO this isn't part of spec but i think its needed
	memb_set_merge (&my_id.sin_addr, 1,
		my_proc_list, &my_proc_list_entries);

	memb_join_message_send ();

	/*
	 * Restart the join timeout
	 */
	poll_timer_delete (*totemsrp_poll_handle, memb_timer_state_gather_join_timeout);

	poll_timer_add (*totemsrp_poll_handle, TIMEOUT_STATE_GATHER_JOIN, 0,
		memb_timer_function_state_gather, &memb_timer_state_gather_join_timeout);

	/*
	 * Restart the consensus timeout
	 */
	poll_timer_delete (*totemsrp_poll_handle,
		memb_timer_state_gather_consensus_timeout);

	poll_timer_add (*totemsrp_poll_handle, TIMEOUT_STATE_GATHER_CONSENSUS, 0,
		memb_timer_function_gather_consensus_timeout,
		&memb_timer_state_gather_consensus_timeout);

	/*
	 * Cancel the token loss and token retransmission timeouts
	 */
	cancel_token_retransmit_timeout (); // REVIEWED
	cancel_token_timeout (); // REVIEWED

	memb_consensus_reset ();

	memb_consensus_set (&my_id.sin_addr);

	totemsrp_log_printf (totemsrp_log_level_notice, "entering GATHER state.\n");

	memb_state = MEMB_STATE_GATHER;

	return;
}

void timer_function_token_retransmit_timeout (void *data);

static void memb_state_commit_enter (struct memb_commit_token *commit_token)
{

	memb_state_commit_token_update (commit_token);

	memb_state_commit_token_send (commit_token);

	memb_ring_id_store (commit_token);

	poll_timer_delete (*totemsrp_poll_handle, memb_timer_state_gather_join_timeout);

	memb_timer_state_gather_join_timeout = 0;

	poll_timer_delete (*totemsrp_poll_handle, memb_timer_state_gather_consensus_timeout);

	memb_timer_state_gather_consensus_timeout = 0;

	reset_token_timeout (); // REVIEWED
	reset_token_retransmit_timeout (); // REVIEWED

	totemsrp_log_printf (totemsrp_log_level_notice, "entering COMMIT state.\n");

	memb_state = MEMB_STATE_COMMIT;

	return;
}

void memb_state_recovery_enter (struct memb_commit_token *commit_token)
{
	int i;
	unsigned int low_ring_aru = 0xFFFFFFFF;
	int local_received_flg = 1;

	my_high_ring_delivered = 0;
	int copy_min;
	int copy_max;

	memb_state_commit_token_send (commit_token);

my_token_seq = -1;
	/*
	 * Build regular configuration
	 */
	my_new_memb_entries = commit_token->addr_entries;

	memcpy (my_new_memb_list, commit_token->addr,
		sizeof (struct in_addr) * my_new_memb_entries);

	/*
	 * Build transitional configuration
	 */
	memb_set_and (my_new_memb_list, my_new_memb_entries,
		my_memb_list, my_memb_entries,
		my_trans_memb_list, &my_trans_memb_entries);

	for (i = 0; i < my_new_memb_entries; i++) {
		printf ("position [%d] member %s:\n", i, inet_ntoa (commit_token->addr[i]));
		printf ("previous ring seq %lld rep %s\n",
			commit_token->memb_list[i].ring_id.seq,
			inet_ntoa (commit_token->memb_list[i].ring_id.rep));
//assert (commit_token->memb_list[i].ring_id.rep.s_addr);
		printf ("aru %d high delivered %d received flag %d\n",
			commit_token->memb_list[i].aru,
			commit_token->memb_list[i].high_delivered,
			commit_token->memb_list[i].received_flg);
assert (commit_token->memb_list[i].ring_id.rep.s_addr);
	}
	/*
	 * Determine if any received flag is false
	 */
	for (i = 0; i < commit_token->addr_entries; i++) {
		if (memb_set_subset (&my_new_memb_list[i], 1,
			my_trans_memb_list, my_trans_memb_entries) &&

			commit_token->memb_list[i].received_flg == 0) {
			my_deliver_memb_entries = my_trans_memb_entries;
			memcpy (my_deliver_memb_list, my_trans_memb_list,
				sizeof (struct in_addr) * my_trans_memb_entries);
			local_received_flg = 0;
			break;
		}
	}
	if (local_received_flg == 0) {
		/*
		 * Calculate low ring_aru, my_high_ring_delivered for the transitional membership
		 */
		for (i = 0; i < commit_token->addr_entries; i++) {
			if (memb_set_subset (&my_new_memb_list[i], 1,
				my_deliver_memb_list, my_deliver_memb_entries)) {
	
				if (low_ring_aru > commit_token->memb_list[i].aru) {
					low_ring_aru = commit_token->memb_list[i].aru;
				}
				if (my_high_ring_delivered < commit_token->memb_list[i].high_delivered) {
					my_high_ring_delivered = commit_token->memb_list[i].high_delivered;
				}
			}
		}
		/*
		 * Copy all old ring messages to retrans_message_queue
		 */
{ int j = 0;
		// TODO this shouldn't be needed
		copy_min = low_ring_aru;
		if ((last_released - 1) > copy_min) {
			copy_min = (last_released - 1);
		}
		
		copy_max = my_high_ring_delivered;
		if (copy_max > my_high_seq_received) {
			copy_max = my_high_seq_received;
		}
		totemsrp_log_printf (totemsrp_log_level_notice,
			"copying all old messages from %d to %d, range %d-%d.\n",
			low_ring_aru, my_high_ring_delivered, copy_min, copy_max);
		for (i = copy_min + 1; i <= copy_max; i++) {

			struct sort_queue_item *sort_queue_item;
			struct message_item message_item;
			void *ptr;
			int res;

			res = sq_item_get (&regular_sort_queue, i, &ptr);
			if (res != 0) {
				continue;
			}
j++;
			sort_queue_item = ptr;
			memset (&message_item, 0, sizeof (struct message_item));
			message_item.mcast = malloc (sizeof (struct mcast));
			assert (message_item.mcast);
			memcpy (message_item.mcast, sort_queue_item->iovec[0].iov_base,
				sizeof (struct mcast));
			message_item.iov_len = sort_queue_item->iov_len;
			message_item.iov_len = sort_queue_item->iov_len;
			memcpy (&message_item.iovec, &sort_queue_item->iovec, sizeof (struct iovec) *
				sort_queue_item->iov_len);
			queue_item_add (&retrans_message_queue, &message_item);
		}
		totemsrp_log_printf (totemsrp_log_level_notice,
			"Originated %d messages in RECOVERY.\n", j);
}
	}

	my_aru_save = my_aru;
	my_high_seq_received_save = my_high_seq_received;

	my_aru = 0;
	my_aru_count = 0;
	my_seq_unchanged = 0;
	my_high_seq_received = 0;
	my_install_seq = 0;
	if (my_old_high_seq_delivered == 0) {
		my_old_high_seq_delivered = my_high_seq_delivered;
	}

	totemsrp_log_printf (totemsrp_log_level_notice, "entering RECOVERY state.\n");
	reset_token_timeout (); // REVIEWED
	reset_token_retransmit_timeout (); // REVIEWED


	memb_state = MEMB_STATE_RECOVERY;
	return;
}

static void encrypt_and_sign (struct iovec *iovec, int iov_len)
{
	char *addr = iov_encrypted.iov_base + sizeof (struct security_header);
	int i;
	char keys[48];
	struct security_header *header = iov_encrypted.iov_base;
	prng_state keygen_prng_state;
	prng_state stream_prng_state;
	char *hmac_key = &keys[32];
	char *cipher_key = &keys[16];
	char *initial_vector = &keys[0];
	unsigned long len;

	iov_encrypted.iov_len = 0;

	memset (keys, 0, sizeof (keys));
	memset (header->salt, 0, sizeof (header->salt));

#if (defined(ENCRYPTION) || defined(AUTHENITCATION))
	/*
	 * Generate MAC, CIPHER, IV keys from private key
	 */
	sober128_read (header->salt, sizeof (header->salt), &totemsrp_prng_state);
	sober128_start (&keygen_prng_state);
	sober128_add_entropy (totemsrp_private_key, totemsrp_private_key_len, &keygen_prng_state);	
	sober128_add_entropy (header->salt, sizeof (header->salt), &keygen_prng_state);

	sober128_read (keys, sizeof (keys), &keygen_prng_state);
#endif

#ifdef ENCRYPTION
	/*
	 * Setup stream cipher
	 */
	sober128_start (&stream_prng_state);
	sober128_add_entropy (cipher_key, 16, &stream_prng_state);	
	sober128_add_entropy (initial_vector, 16, &stream_prng_state);	
#endif

#ifdef CODE_COVERAGE_COMPILE_OUT
if (log_digest) {
printf ("new encryption\n");
print_digest ("salt", header->salt);
print_digest ("initial_vector", initial_vector);
print_digest ("cipher_key", cipher_key);
print_digest ("hmac_key", hmac_key);
}
#endif

	/*
	 * Copy header of message, then remainder of message, then encrypt it
	 */
	memcpy (addr, iovec[0].iov_base + sizeof (struct security_header),
		iovec[0].iov_len - sizeof (struct security_header));
	addr += iovec[0].iov_len - sizeof (struct security_header);
	iov_encrypted.iov_len += iovec[0].iov_len;

	for (i = 1; i < iov_len; i++) {
		memcpy (addr, iovec[i].iov_base, iovec[i].iov_len);
		addr += iovec[i].iov_len;
		iov_encrypted.iov_len += iovec[i].iov_len;
	}

	/*
 	 * Encrypt message by XORing stream cipher data
	 */
#ifdef ENCRYPTION
	sober128_read (iov_encrypted.iov_base + sizeof (struct security_header),
		iov_encrypted.iov_len - sizeof (struct security_header),
		&stream_prng_state);
#endif

#ifdef AUTHENTICATION
	memset (&totemsrp_hmac_state, 0, sizeof (hmac_state));

	/*
	 * Sign the contents of the message with the hmac key and store signature in message
	 */
	hmac_init (&totemsrp_hmac_state, DIGEST_SHA1, hmac_key, 16);

	hmac_process (&totemsrp_hmac_state, 
		iov_encrypted.iov_base + HMAC_HASH_SIZE,
		iov_encrypted.iov_len - HMAC_HASH_SIZE);

	len = hash_descriptor[DIGEST_SHA1]->hashsize;

	hmac_done (&totemsrp_hmac_state, header->hash_digest, &len);
#endif

#ifdef COMPILE_OUT
print_digest ("initial_vector", initial_vector);
print_digest ("cipher_key", cipher_key);
print_digest ("hmac_key", hmac_key);
print_digest ("salt", header->salt);
print_digest ("sent digest", header->hash_digest);
#endif
}

/*
 * Only designed to work with a message with one iov
 */
static int authenticate_and_decrypt (struct iovec *iov)
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

	iov_encrypted.iov_len = 0;

#ifdef COMPILE_OUT
	printf ("Decryption message\n");
	print_msg (header, iov[0].iov_len);
#endif
#if (defined(ENCRYPTION) || defined(AUTHENITCATION))
	/*
	 * Generate MAC, CIPHER, IV keys from private key
	 */
	memset (keys, 0, sizeof (keys));
	sober128_start (&keygen_prng_state);
	sober128_add_entropy (totemsrp_private_key, totemsrp_private_key_len, &keygen_prng_state);	
	sober128_add_entropy (header->salt, sizeof (header->salt), &keygen_prng_state);

	sober128_read (keys, sizeof (keys), &keygen_prng_state);
#endif

#ifdef ENCRYPTION
	/*
	 * Setup stream cipher
	 */
	sober128_start (&stream_prng_state);
	sober128_add_entropy (cipher_key, 16, &stream_prng_state);	
	sober128_add_entropy (initial_vector, 16, &stream_prng_state);	
#endif

#ifdef CODE_COVERAGE_COMPILE_OUT
if (log_digest) {
printf ("New decryption\n");
print_digest ("salt", header->salt);
print_digest ("initial_vector", initial_vector);
print_digest ("cipher_key", cipher_key);
print_digest ("hmac_key", hmac_key);
}
#endif

#ifdef AUTHENTICATION
	/*
	 * Authenticate contents of message
	 */
	hmac_init (&totemsrp_hmac_state, DIGEST_SHA1, hmac_key, 16);

	hmac_process (&totemsrp_hmac_state, 
		iov->iov_base + HMAC_HASH_SIZE,
		iov->iov_len - HMAC_HASH_SIZE);

	len = hash_descriptor[DIGEST_SHA1]->hashsize;
	assert (HMAC_HASH_SIZE >= len);
	hmac_done (&totemsrp_hmac_state, digest_comparison, &len);

#ifdef PRINTDIGESTS
print_digest ("received digest", header->hash_digest);
print_digest ("calculated digest", digest_comparison);
#endif
	if (memcmp (digest_comparison, header->hash_digest, len) != 0) {
#ifdef CODE_COVERAGE_COMPILE_OUT
print_digest ("initial_vector", initial_vector);
print_digest ("cipher_key", cipher_key);
print_digest ("hmac_key", hmac_key);
print_digest ("salt", header->salt);
print_digest ("sent digest", header->hash_digest);
print_digest ("calculated digest", digest_comparison);
printf ("received message size %d\n", iov->iov_len);
#endif
		totemsrp_log_printf (totemsrp_log_level_security, "Received message has invalid digest... ignoring.\n");
		res = -1;
		return (-1);
	}
#endif /* AUTHENTICATION */
	
	/*
	 * Decrypt the contents of the message with the cipher key
	 */
#ifdef ENCRYPTION
	sober128_read (iov->iov_base + sizeof (struct security_header),
		iov->iov_len - sizeof (struct security_header),
		&stream_prng_state);
#endif

	return (res);
	return (0);
}

int totemsrp_mcast (
	struct iovec *iovec,
	int iov_len,
	int guarantee)
{
	int i;
	int j;
	struct message_item message_item;

	
	if (queue_is_full (&new_message_queue)) {
		return (-1);
	}
	for (j = 0, i = 0; i < iov_len; i++) {
		j+= iovec[i].iov_len;
	}
//	assert (j == FRAGMENT_SIZE || j == (FRAGMENT_SIZE - 2)); /* ensure we use the maximum badnwidth available for now */

//	printf ("j is %d fragment size is %d\n", j, FRAGMENT_SIZE);
//	assert (j <= FRAGMENT_SIZE);
		

	totemsrp_log_printf (totemsrp_log_level_debug, "Multicasting message.\n");
	memset (&message_item, 0, sizeof (struct message_item));

	/*
	 * Allocate pending item
	 */
	message_item.mcast = malloc (sizeof (struct mcast));
	if (message_item.mcast == 0) {
		goto error_mcast;
	}

	/*
	 * Set mcast header
	 */
	message_item.mcast->header.type = MESSAGE_TYPE_MCAST;
	message_item.mcast->header.endian_detector = ENDIAN_LOCAL;
	message_item.mcast->header.encapsulated = 0;
	message_item.mcast->guarantee = guarantee;
	message_item.mcast->source.s_addr = my_id.sin_addr.s_addr;

	for (i = 0; i < iov_len; i++) {
		message_item.iovec[i].iov_base = malloc (iovec[i].iov_len);

		if (message_item.iovec[i].iov_base == 0) {
			goto error_iovec;
		}

		memcpy (message_item.iovec[i].iov_base, iovec[i].iov_base,
			iovec[i].iov_len);

		message_item.iovec[i].iov_len = iovec[i].iov_len;
	}

	message_item.iov_len = iov_len;

	totemsrp_log_printf (totemsrp_log_level_debug, "mcasted message added to pending queue\n");
	queue_item_add (&new_message_queue, &message_item);

	return (0);
error_iovec:
	for (j = 0; j < i; j++) {
		free (message_item.iovec[j].iov_base);
	}
	return (-1);
error_mcast:
	return (0);
}

/*
 * Determine if there is room to queue a new message
 */
int totemsrp_avail (void)
{
	int avail;

	queue_avail (&new_message_queue, &avail);

	return (avail);
}

static int netif_determine (struct sockaddr_in *bindnet,
	struct sockaddr_in *bound_to)
{
	struct sockaddr_in *sockaddr_in;
	int id_fd;
	struct ifconf ifc;
	int numreqs = 0;
	int res;
	int i;
	in_addr_t mask_addr;

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
			break; /* for */
		}
	}
	free (ifc.ifc_buf);
	close (id_fd);
	
	return (res);
}

static int totemsrp_build_sockets (struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	struct totemsrp_socket *sockets,
	struct sockaddr_in *bound_to)
{
	struct ip_mreq mreq;
	struct sockaddr_in sockaddr_in;
	char flag;
	int res;
	
	memset (&mreq, 0, sizeof (struct ip_mreq));

	/*
	 * Determine the ip address bound to and the interface name
	 */
	res = netif_determine (sockaddr_bindnet,
		bound_to);

	if (res == -1) {
		return (-1);
	}

	/* TODO this should be somewhere else */
	memb_local_sockaddr_in.sin_addr.s_addr = bound_to->sin_addr.s_addr;
	memb_local_sockaddr_in.sin_family = AF_INET;
	memb_local_sockaddr_in.sin_port = sockaddr_mcast->sin_port;

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

		totemsrp_log_printf (totemsrp_log_level_warning, "Could not bind to device for multicast, group messaging may not work properly. (%s)\n", strerror (errno));
	}

	/*
	 * Bind to multicast socket used for multicast send/receives
	 */
	sockaddr_in.sin_family = AF_INET;
	sockaddr_in.sin_addr.s_addr = sockaddr_mcast->sin_addr.s_addr;
	sockaddr_in.sin_port = sockaddr_mcast->sin_port;
	res = bind (sockets->mcast, (struct sockaddr *)&sockaddr_in,
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
	sockaddr_in.sin_addr.s_addr = bound_to->sin_addr.s_addr;
	res = bind (sockets->token, (struct sockaddr *)&sockaddr_in,
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
 * Misc Management
 */
int in_addr_compare (const void *a, const void *b) {
	struct in_addr *in_addr_a = (struct in_addr *)a;
	struct in_addr *in_addr_b = (struct in_addr *)b;

	return (in_addr_a->s_addr > in_addr_b->s_addr);
}

/*
 * ORF Token Management
 */
/* 
 * Recast message to mcast group if it is available
 */
int orf_token_remcast (int seq) {
	struct msghdr msg_mcast;
	struct sort_queue_item *sort_queue_item;
	int res;
	struct mcast *mcast;
	void *ptr;

	struct sq *sort_queue;

	if (memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &recovery_sort_queue;
	} else {
		sort_queue = &regular_sort_queue;
	}

	/*
	 * Get RTR item at seq, if not available, return
	 */
	res = sq_item_get (sort_queue, seq, &ptr);
	if (res != 0) {
		return -1;
	}

	sort_queue_item = ptr;

	mcast = (struct mcast *)sort_queue_item->iovec[0].iov_base;

	encrypt_and_sign (sort_queue_item->iovec, sort_queue_item->iov_len);

	/*
	 * Build multicast message
	 */
	msg_mcast.msg_name = (caddr_t)&sockaddr_in_mcast;
	msg_mcast.msg_namelen = sizeof (struct sockaddr_in);
	msg_mcast.msg_iov = &iov_encrypted;
	msg_mcast.msg_iovlen = 1;
	msg_mcast.msg_control = 0;
	msg_mcast.msg_controllen = 0;
	msg_mcast.msg_flags = 0;

	/*
	 * Multicast message
	 */
	res = sendmsg (totemsrp_sockets[0].mcast, &msg_mcast, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (res == -1) {
		return (-1);
	}
	stats_sent += res;
	return (0);
}


/*
 * Free all freeable messages from ring
 */
static int messages_free (int token_aru)
{
	struct sort_queue_item *regular_message;
	int i, j;
	int res;
	int log_release = 0;
	int release_to;

	release_to = token_aru;
	if (release_to > my_last_aru) {
		release_to = my_last_aru;
	}

	/*
	 * Release retransmit list items if group aru indicates they are transmitted
	 */
	for (i = last_released; i <= release_to; i++) {
		void *ptr;
		res = sq_item_get (&regular_sort_queue, i, &ptr);
		if (res == 0) {
			regular_message = ptr;
			for (j = 0; j < regular_message->iov_len; j++) {
				free (regular_message->iovec[j].iov_base);
			}
		}
		sq_items_release (&regular_sort_queue, i);
		last_released = i + 1;
		log_release = 1;
	}

log_release=1;
 	if (log_release) {
//TODprintf ("%d\n", lesser);
//		totemsrp_log_printf (totemsrp_log_level_notice,
//			"releasing messages up to and including %d\n", lesser);
	}
	return (0);
}

void update_aru (void)
{
	int i;
	int res;
	struct sq *sort_queue;

	if (memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &recovery_sort_queue;
	} else {
		sort_queue = &regular_sort_queue;
	}

	for (i = my_aru + 1; i <= my_high_seq_received; i++) {
		void *ptr;

		res = sq_item_get (sort_queue, i, &ptr);
		/*
		 * If hole, stop assembly
		 */
		if (res != 0) {
			break;
		}
		my_aru = i;
	}
//printf ("setting received flag to false %d %d\n", my_aru, my_high_seq_received);
	my_received_flg = 0;
	if (my_aru == my_high_seq_received) {
//TODOprintf ("setting received flag to TRUE %d %d\n", my_aru, my_high_seq_received);
		my_received_flg = 1;
	}
}

/*
 * Multicasts pending messages onto the ring (requires orf_token possession)
 */
static int orf_token_mcast (
	struct orf_token *token,
	int fcc_mcasts_allowed,
	struct sockaddr_in *system_from)
{
	struct msghdr msg_mcast;
	struct sort_queue_item sort_queue_item;
	struct message_item *message_item = 0;
	int res = 0;
	struct mcast *mcast;
	struct queue *mcast_queue;
	struct sq *sort_queue;

	if (memb_state == MEMB_STATE_RECOVERY) {
		mcast_queue = &retrans_message_queue;
		sort_queue = &recovery_sort_queue;
		reset_token_retransmit_timeout (); // REVIEWED
	} else {
		mcast_queue = &new_message_queue;
		sort_queue = &regular_sort_queue;
	}

	for (fcc_mcast_current = 0; fcc_mcast_current < fcc_mcasts_allowed; fcc_mcast_current++) {
		if (queue_is_empty (mcast_queue)) {
			break;
		}
		message_item = (struct message_item *)queue_item_get (mcast_queue);
		/* preincrement required by algo */
		message_item->mcast->seq = ++token->seq;

		/*
		 * Build IO vector
		 */
		memset (&sort_queue_item, 0, sizeof (struct sort_queue_item));
		sort_queue_item.iovec[0].iov_base = message_item->mcast;
		sort_queue_item.iovec[0].iov_len = sizeof (struct mcast);

		mcast = sort_queue_item.iovec[0].iov_base;

		memcpy (&sort_queue_item.iovec[1], message_item->iovec,
			message_item->iov_len * sizeof (struct iovec));

		sort_queue_item.iov_len = message_item->iov_len + 1;

		assert (sort_queue_item.iov_len < 16);

		/*
		 * Add message to retransmit queue
		 */
		sq_item_add (sort_queue,
			&sort_queue_item, message_item->mcast->seq);

		/*
		 * Delete item from pending queue
		 */
		queue_item_remove (mcast_queue);

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign (sort_queue_item.iovec, sort_queue_item.iov_len);

		/*
		 * Build multicast message
		 */
		msg_mcast.msg_name = &sockaddr_in_mcast;
		msg_mcast.msg_namelen = sizeof (struct sockaddr_in);
		msg_mcast.msg_iov = &iov_encrypted;
		msg_mcast.msg_iovlen = 1;
		msg_mcast.msg_control = 0;
		msg_mcast.msg_controllen = 0;
		msg_mcast.msg_flags = 0;

		/*
		 * Multicast message
		 * An error here is recovered by the multicast algorithm
		 */
		res = sendmsg (totemsrp_sockets[0].mcast, &msg_mcast, MSG_NOSIGNAL | MSG_DONTWAIT);
//printf ("multicasting %d bytes\n", res);
//f (res != iov_encrypted.iov_len) {
//printf ("res %d errno is %d\n", res, errno);
//}
//		assert (res == iov_encrypted.iov_len);
		iov_encrypted.iov_len = PACKET_SIZE_MAX;

		if (res > 0) {
			stats_sent += res;
		}
	}
	assert (fcc_mcast_current < 100);

	/*
	 * If messages mcasted, deliver any new messages to totemg
	 */
	if (fcc_mcast_current) {
		my_do_delivery = 1;
	}
	my_high_seq_received = token->seq;
		
	update_aru ();
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
	struct orf_token *orf_token,
	int *fcc_allowed)
{
	int res;
	int i, j;
	int found;
	int total_entries;
	struct sq *sort_queue;
	struct rtr_item *rtr_list;

	if (memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &recovery_sort_queue;
	} else {
		sort_queue = &regular_sort_queue;
	}

	rtr_list = &orf_token->rtr_list[0];
if (orf_token->rtr_list_entries) {
printf ("Retransmit List %d\n", orf_token->rtr_list_entries);
for (i = 0; i < orf_token->rtr_list_entries; i++) {
	printf ("%d ", rtr_list[i].seq);
}
printf ("\n");
}

	total_entries = orf_token->rtr_list_entries;

	/*
	 * Retransmit messages on orf_token's RTR list from RTR queue
	 */
	for (fcc_remcast_current = 0, i = 0;
		fcc_remcast_current <= *fcc_allowed && i < orf_token->rtr_list_entries;) {

		/*
		 * If this retransmit request isn't from this configuration,
		 * try next rtr entry
		 */
 		if (memcmp (&rtr_list[i].ring_id, &my_ring_id,
			sizeof (struct memb_ring_id)) != 0) {

			i += 1;
			continue;
		}

		assert (rtr_list[i].seq > 0);
		res = orf_token_remcast (rtr_list[i].seq);
		if (res == 0) {
			/*
			 * Multicasted message, so no need to copy to new retransmit list
			 */
			orf_token->rtr_list_entries -= 1;
			assert (orf_token->rtr_list_entries >= 0);
			memmove (&rtr_list[i], &rtr_list[i + 1],
				sizeof (struct rtr_item) * (orf_token->rtr_list_entries));

			fcc_remcast_current++;
			stats_remcasts++;
		} else {
			i += 1;
		}
	}
	*fcc_allowed = *fcc_allowed - fcc_remcast_current - 1;

#ifdef COMPILE_OUT
for (i = 0; i < orf_token->rtr_list_entries; i++) {
	assert (rtr_list_old[index_old].seq != -1);
}
#endif

	/*
	 * Add messages to retransmit to RTR list
	 * but only retry if there is room in the retransmit list
	 */
	for (i = my_aru + 1;
		orf_token->rtr_list_entries < RETRANSMIT_ENTRIES_MAX &&
		i <= my_high_seq_received;
		i++) {

		/*
		 * Find if a message is missing from this processor
		 */
		res = sq_item_inuse (sort_queue, i);
		if (res == 0) {
			/*
			 * Determine if missing message is already in retransmit list
			 */
			found = 0;
			for (j = 0; j < orf_token->rtr_list_entries; j++) {
				if (i == rtr_list[j].seq) {
					found = 1;
				}
			}
			if (found == 0) {
				/*
				 * Missing message not found in current retransmit list so add it
				 */
				memcpy (&rtr_list[orf_token->rtr_list_entries].ring_id,
					&my_ring_id, sizeof (struct memb_ring_id));
				rtr_list[orf_token->rtr_list_entries].seq = i;
				orf_token->rtr_list_entries++;
			}
		}
	}
	return (fcc_remcast_current);
}

void token_retransmit (void) {
	struct iovec iovec;
	struct msghdr msg_orf_token;
	int res;

	iovec.iov_base = orf_token_retransmit;
	iovec.iov_len = orf_token_retransmit_size;

	msg_orf_token.msg_name = &next_memb;
	msg_orf_token.msg_namelen = sizeof (struct sockaddr_in);
	msg_orf_token.msg_iov = &iovec;
	msg_orf_token.msg_iovlen = 1;
	msg_orf_token.msg_control = 0;
	msg_orf_token.msg_controllen = 0;
	msg_orf_token.msg_flags = 0;
	
	res = sendmsg (totemsrp_sockets[0].token, &msg_orf_token, MSG_NOSIGNAL);
	assert (res != -1);
	assert (res == orf_token_retransmit_size);
}

/*
 * Retransmit the regular token if no mcast or token has
 * been received in retransmit token period retransmit
 * the token to the next processor
 */
void timer_function_token_retransmit_timeout (void *data)
{
struct timeval timeval;

	gettimeofday (&timeval, 0);
	switch (memb_state) {
	case MEMB_STATE_GATHER:
		break;
	case MEMB_STATE_COMMIT:
		break;
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		token_retransmit ();
		reset_token_retransmit_timeout (); // REVIEWED
		break;
	}
}


/*
 * Send orf_token to next member (requires orf_token)
 */
static int token_send (
	struct orf_token *orf_token,
	int forward_token)
{
	struct msghdr msg_orf_token;
	struct iovec iovec;
	int res;

	iovec.iov_base = (char *)orf_token;
	iovec.iov_len = sizeof (struct orf_token) +
		(orf_token->rtr_list_entries * sizeof (struct rtr_item));

#ifdef COMPILE_OUT
{ int i;
if (orf_token->rtr_list_entries) {
printf ("Retransmit List Sending %d\n", orf_token->rtr_list_entries);
for (i = 0; i < orf_token->rtr_list_entries; i++) {
	printf ("%d ", rtr_list[i].seq);
assert (rtr_list[i].seq != 0);
}
printf ("\n");
}
}
#endif

	encrypt_and_sign (&iovec, 1);

	/*
	 * Keep an encrypted copy in case the token retransmit timer expires
	 */
	memcpy (orf_token_retransmit, iov_encrypted.iov_base, iov_encrypted.iov_len);
	orf_token_retransmit_size = iov_encrypted.iov_len;

	/*
	 * IF the user doesn't want the token forwarded, then dont send
	 * it but keep an encrypted copy for the retransmit timeout
	 */ 
	if (forward_token == 0) {
		return (0);
	}
	
	/*
	 * Send the message
	 */
	msg_orf_token.msg_name = &next_memb;
	msg_orf_token.msg_namelen = sizeof (struct sockaddr_in);
	msg_orf_token.msg_iov = &iov_encrypted;
	msg_orf_token.msg_iovlen = 1;
	msg_orf_token.msg_control = 0;
	msg_orf_token.msg_controllen = 0;
	msg_orf_token.msg_flags = 0;

	res = sendmsg (totemsrp_sockets[0].token, &msg_orf_token, MSG_NOSIGNAL);
	if (res == -1) {
		printf ("Couldn't send token to addr %s %s %d\n",
			inet_ntoa (next_memb.sin_addr), 
			strerror (errno), totemsrp_sockets[0].token);
	}
	assert (res != -1);
	assert (res == iov_encrypted.iov_len);
	
	/*
	 * res not used here errors are handled by algorithm
	 */
	if (res > 0) {
		stats_sent += res;
	}

	return (res);
}

int orf_token_send_initial (void)
{
	struct orf_token orf_token;
	int res;

	orf_token.header.type = MESSAGE_TYPE_ORF_TOKEN;
	orf_token.header.endian_detector = ENDIAN_LOCAL;
	orf_token.header.encapsulated = 0;
	orf_token.seq = 0;
	orf_token.token_seq = 0;
	orf_token.retrans_flg = 1;
	my_set_retrans_flg = 1;
/*
	if (queue_is_empty (&retrans_message_queue) == 1) {
		orf_token.retrans_flg = 0;
	} else {
		orf_token.retrans_flg = 1;
		my_set_retrans_flg = 1;
	}
*/
		
	orf_token.aru = 0;
	orf_token.aru_addr.s_addr = my_id.sin_addr.s_addr;
	memcpy (&orf_token.ring_id, &my_ring_id, sizeof (struct memb_ring_id));
	orf_token.fcc = 0;

	orf_token.rtr_list_entries = 0;

	res = token_send (&orf_token, 1);

	return (res);
}

static void memb_state_commit_token_update (struct memb_commit_token *memb_commit_token)
{
	int memb_index_this;

	memb_index_this = (memb_commit_token->memb_index + 1) % memb_commit_token->addr_entries;
	memcpy (&memb_commit_token->memb_list[memb_index_this].ring_id, &my_ring_id,
		sizeof (struct memb_ring_id));
assert (my_ring_id.rep.s_addr != 0);
	memb_commit_token->memb_list[memb_index_this].aru = my_aru;
	memb_commit_token->memb_list[memb_index_this].high_delivered = my_aru; /* no safe, for now this is my_aru */
	memb_commit_token->memb_list[memb_index_this].received_flg = my_received_flg;
}

static int memb_state_commit_token_send (struct memb_commit_token *memb_commit_token)
{
	struct msghdr msghdr;
	struct iovec iovec;
	int res;
	int memb_index_this;
	int memb_index_next;

	memb_commit_token->token_seq++;
	memb_index_this = (memb_commit_token->memb_index + 1) % memb_commit_token->addr_entries;
	memb_index_next = (memb_index_this + 1) % memb_commit_token->addr_entries;
	memb_commit_token->memb_index = memb_index_this;

	iovec.iov_base = memb_commit_token;
	iovec.iov_len = sizeof (struct memb_commit_token);

	encrypt_and_sign (&iovec, 1);

	next_memb.sin_addr.s_addr = memb_commit_token->addr[memb_index_next].s_addr;
	next_memb.sin_family = AF_INET;
	next_memb.sin_port = sockaddr_in_mcast.sin_port;

	msghdr.msg_name = &next_memb;
	msghdr.msg_namelen = sizeof (struct sockaddr_in);
	msghdr.msg_iov = &iov_encrypted;
	msghdr.msg_iovlen = 1;
	msghdr.msg_control = 0;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	res = sendmsg (totemsrp_sockets[0].token, &msghdr, MSG_NOSIGNAL | MSG_DONTWAIT);
	assert (res != -1);
	return (res);
}

int memb_lowest_in_config (void)
{
	struct in_addr token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;
	struct in_addr lowest_addr;
	int i;

	lowest_addr.s_addr = 0xFFFFFFFF;

	memb_set_subtract (token_memb, &token_memb_entries,
		my_proc_list, my_proc_list_entries,
		my_failed_list, my_failed_list_entries);

	/*
	 * find representative by searching for smallest identifier
	 */
	for (i = 0; i < token_memb_entries; i++) {
		if (lowest_addr.s_addr > token_memb[i].s_addr) {
			lowest_addr.s_addr = token_memb[i].s_addr;
		}
	}
	return (my_id.sin_addr.s_addr == lowest_addr.s_addr);
}

static void memb_state_commit_token_create (struct memb_commit_token *commit_token)
{
	struct in_addr token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;

	totemsrp_log_printf (totemsrp_log_level_notice,
		"Creating commit token because I am the rep.\n");

	memb_set_subtract (token_memb, &token_memb_entries,
		my_proc_list, my_proc_list_entries,
		my_failed_list, my_failed_list_entries);

	memset (commit_token, 0, sizeof (struct memb_commit_token));
	commit_token->header.type = MESSAGE_TYPE_MEMB_COMMIT_TOKEN;
	commit_token->header.endian_detector = ENDIAN_LOCAL;
	commit_token->header.encapsulated = 0;

	commit_token->ring_id.rep.s_addr = my_id.sin_addr.s_addr;

	commit_token->ring_id.seq = token_ring_id_seq + 4;
	qsort (token_memb, token_memb_entries, 
		sizeof (struct in_addr), in_addr_compare);
	memcpy (commit_token->addr, token_memb,
		token_memb_entries * sizeof (struct in_addr));
	memset (commit_token->memb_list, 0,
		sizeof (struct memb_commit_token_memb_entry) * PROCESSOR_COUNT_MAX);
	commit_token->memb_index = token_memb_entries - 1;
	commit_token->addr_entries = token_memb_entries;
}

int memb_join_message_send (void)
{
	struct msghdr msghdr;
	struct iovec iovec;
	struct memb_join memb_join;
	int res;

	memb_join.header.type = MESSAGE_TYPE_MEMB_JOIN;
	memb_join.header.endian_detector = ENDIAN_LOCAL;
	memb_join.header.encapsulated = 0;

	memb_join.ring_seq = my_ring_id.seq;

	memcpy (memb_join.proc_list, my_proc_list,
		my_proc_list_entries * sizeof (struct in_addr));
	memb_join.proc_list_entries = my_proc_list_entries;

	memcpy (memb_join.failed_list, my_failed_list,
		my_failed_list_entries * sizeof (struct in_addr));
	memb_join.failed_list_entries = my_failed_list_entries;
		
	iovec.iov_base = &memb_join;
	iovec.iov_len = sizeof (struct memb_join);

	encrypt_and_sign (&iovec, 1);

	msghdr.msg_name = &sockaddr_in_mcast;
	msghdr.msg_namelen = sizeof (struct sockaddr_in);
	msghdr.msg_iov = &iov_encrypted;
	msghdr.msg_iovlen = 1;
	msghdr.msg_control = 0;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	res = sendmsg (totemsrp_sockets[0].mcast, &msghdr, MSG_NOSIGNAL | MSG_DONTWAIT);

	return (res);
}

static void memb_ring_id_create_or_load (
	struct memb_ring_id *memb_ring_id)
{
	int fd;
	int res;
	char filename[256];

	sprintf (filename, "/tmp/ringid_%s",
		inet_ntoa (my_id.sin_addr));
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
			printf ("couldn't create file %d %s\n", fd, strerror(errno));
		}
		res = write (fd, &memb_ring_id->seq, sizeof (unsigned long long));
		assert (res == sizeof (unsigned long long));
		close (fd);
	} else {
		printf ("Couldn't open %s %s\n", filename, strerror (errno));
	}
	
	memb_ring_id->rep.s_addr = my_id.sin_addr.s_addr;
	assert (memb_ring_id->rep.s_addr);
	token_ring_id_seq = memb_ring_id->seq;
}

static void memb_ring_id_store (
	struct memb_commit_token *commit_token)
{
	char filename[256];
	int fd;
	int res;

	sprintf (filename, "/tmp/ringid_%s",
		inet_ntoa (my_id.sin_addr));

	fd = open (filename, O_WRONLY, 0777);
	if (fd == -1) {
		fd = open (filename, O_CREAT|O_RDWR, 0777);
	}
	if (fd == -1) {
		totemsrp_log_printf (totemsrp_log_level_notice,
			"Couldn't store new ring id %llx to stable storage (%s)\n",
				commit_token->ring_id.seq, strerror (errno));
		assert (0);
		return;
	}
	totemsrp_log_printf (totemsrp_log_level_notice,
		"Storing new sequence id for ring %d\n", commit_token->ring_id.seq);
	assert (fd > 0);
	res = write (fd, &commit_token->ring_id.seq, sizeof (unsigned long long));
	assert (res == sizeof (unsigned long long));
	close (fd);
	memcpy (&my_ring_id, &commit_token->ring_id, sizeof (struct memb_ring_id));
	token_ring_id_seq = my_ring_id.seq;
}

void print_stats (void)
{
	struct timeval tv_end;
	gettimeofday (&tv_end, NULL);
	
	totemsrp_log_printf (totemsrp_log_level_notice, "Bytes recv %d\n", stats_recv);
	totemsrp_log_printf (totemsrp_log_level_notice, "Bytes sent %d\n", stats_sent);
	totemsrp_log_printf (totemsrp_log_level_notice, "Messages delivered %d\n", stats_delv);
	totemsrp_log_printf (totemsrp_log_level_notice, "Re-Mcasts %d\n", stats_remcasts);
	totemsrp_log_printf (totemsrp_log_level_notice, "Tokens process %d\n", stats_orf_token);
}

int totemsrp_callback_token_create (void **handle_out,
	enum totemsrp_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totemsrp_callback_token_type type, void *),
	void *data)
{
	struct token_callback_instance *handle;
	handle = (struct token_callback_instance *)malloc (sizeof (struct token_callback_instance));
	if (handle == 0) {
		return (-1);
	}
	*handle_out = (void *)handle;
	list_init (&handle->list);
	handle->callback_fn = callback_fn;
	handle->data = data;
	handle->callback_type = type;
	handle->delete = delete;
	switch (type) {
	case TOTEMSRP_CALLBACK_TOKEN_RECEIVED:
		list_add (&handle->list, &token_callback_received_listhead);
		break;
	case TOTEMSRP_CALLBACK_TOKEN_SENT:
		list_add (&handle->list, &token_callback_sent_listhead);
		break;
	}
	
	return (0);
}

void totemsrp_callback_token_destroy (void **handle_out)
{
	struct token_callback_instance *h;

	if (*handle_out) {
 		h = (struct token_callback_instance *)*handle_out;
		list_del (&h->list);
		free (h);
		*handle_out = 0;
	}
}

void totemsrp_callback_token_type (void *handle)
{
	struct token_callback_instance *token_callback_instance = (struct token_callback_instance *)handle;

	list_del (&token_callback_instance->list);
	free (token_callback_instance);
}

void token_callbacks_execute (enum totemsrp_callback_token_type type)
{
	struct list_head *list;
	struct list_head *list_next;
	struct list_head *callback_listhead = 0;
	struct token_callback_instance *token_callback_instance;
	int res;

	switch (type) {
	case TOTEMSRP_CALLBACK_TOKEN_RECEIVED:
		callback_listhead = &token_callback_received_listhead;
		break;
	case TOTEMSRP_CALLBACK_TOKEN_SENT:
		callback_listhead = &token_callback_sent_listhead;
		break;
	default:
		assert (0);
	}
	
	for (list = callback_listhead->next; list != callback_listhead;
		list = list_next) {

		token_callback_instance = list_entry (list, struct token_callback_instance, list);
		list_next = list->next;
		if (token_callback_instance->delete == 1) {
			list_del (list);
		}

		res = token_callback_instance->callback_fn (
			token_callback_instance->callback_type,
			token_callback_instance->data);

		/*
		 * This callback failed to execute, try it again on the next token
		 */
		if (res == -1 && token_callback_instance->delete == 1) {
			list_add (list, callback_listhead);
		} else
		if (token_callback_instance->delete) {
			free (token_callback_instance);
		}
	}
}

/*
 * Message Handlers
 */

int my_last_seq = 0;

	struct timeval tv_old;
/*
 * message handler called when TOKEN message type received
 */
static int message_handler_orf_token (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received,
	int endian_conversion_needed)
{
	char token_storage[1500];
	char token_convert[1500];
	struct orf_token *token;
	int prio = UINT_MAX;
	struct pollfd ufd;
	int nfds;
	struct orf_token *token_ref = (struct orf_token *)iovec->iov_base;
	int transmits_allowed;
	int forward_token;
	int mcasted;
	int last_aru;
	int low_water;

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

	my_token_held = 1;
	my_do_delivery = 0;

#ifdef RANDOM_DROP
if (random () % 100 < 10) {
	return (0);
}
#endif
	/*
	 * Hold onto token when there is no activity on ring and
	 * this processor is the ring rep
	 */
	forward_token = 1;
	if (my_ring_id.rep.s_addr == my_id.sin_addr.s_addr) {
			if (my_seq_unchanged > SEQNO_UNCHANGED_CONST) {
				forward_token = 0;
			}
	}

	if (token_ref->seq == my_last_seq) {
		my_seq_unchanged++;
	} else {
		my_seq_unchanged = 0;
	}
	my_last_seq = token_ref->seq;

	assert (bytes_received >= sizeof (struct orf_token));
//	assert (bytes_received == sizeof (struct orf_token) +
//		(sizeof (struct rtr_item) * token_ref->rtr_list_entries);
	/*
	 * Make copy of token and retransmit list in case we have
	 * to flush incoming messages from the kernel queue
	 */
	token = (struct orf_token *)token_storage;
	memcpy (token, iovec->iov_base, sizeof (struct orf_token));
	memcpy (&token->rtr_list[0], iovec->iov_base + sizeof (struct orf_token),
		sizeof (struct rtr_item) * RETRANSMIT_ENTRIES_MAX);

	if (endian_conversion_needed) {
//		printf ("Must convert endian of token message\n");
		orf_token_endian_convert (token, (struct orf_token *)token_convert);
		token = (struct orf_token *)token_convert;
	}
	/*
	 * flush incoming queue from kernel
	 */
	do {
		ufd.fd = totemsrp_sockets[0].mcast;
		ufd.events = POLLIN;
		nfds = poll (&ufd, 1, 0);
		if (nfds == 1 && ufd.revents & POLLIN) {
			totemsrp_iov_recv.iov_len = PACKET_SIZE_MAX;
			recv_handler (0, totemsrp_sockets[0].mcast, ufd.revents, 0,
				&prio);
		}
	} while (nfds == 1);


	token_callbacks_execute (TOTEMSRP_CALLBACK_TOKEN_RECEIVED);

	switch (memb_state) {
	case MEMB_STATE_COMMIT:
		 /* Discard token */
		break;

	case MEMB_STATE_OPERATIONAL:
		messages_free (token->aru);
	case MEMB_STATE_GATHER:
		/*
		 * DO NOT add break, we use different free mechanism in recovery state
		 */

	case MEMB_STATE_RECOVERY:
		last_aru = my_last_aru;
		my_last_aru = token->aru;

		/*
		 * Discard tokens from another configuration
		 */
		if (memcmp (&token->ring_id, &my_ring_id,
			sizeof (struct memb_ring_id)) != 0) {

			my_token_held = 0;
			return (0); /* discard token */
		}

		/*
		 * Discard retransmitted tokens
		 */
		if (my_token_seq >= token->token_seq) {
			my_token_held = 0;
			reset_token_retransmit_timeout ();
			reset_token_timeout ();
			return (0); /* discard token */
		}		
		transmits_allowed = 30;
		mcasted = orf_token_rtr (token, &transmits_allowed);
		if (mcasted) {
			forward_token = 1;
			my_seq_unchanged = 0;
		}

        if ((last_aru + MISSING_MCAST_WINDOW) < token->seq) {
                transmits_allowed = 0;
        }
		mcasted = orf_token_mcast (token, transmits_allowed, system_from);
		if (mcasted) {
			forward_token = 1;
			my_seq_unchanged = 0;
		}
		if (my_aru < token->aru ||
			my_id.sin_addr.s_addr == token->aru_addr.s_addr || 
			token->aru_addr.s_addr == 0) {
			
			token->aru = my_aru;
			if (token->aru == token->seq) {
				token->aru_addr.s_addr = 0;
			} else {
				token->aru_addr.s_addr = my_id.sin_addr.s_addr;
			}
		}
		if (token->aru == my_last_aru && token->aru_addr.s_addr != 0) {
			my_aru_count += 1;
		} else {
			my_aru_count = 0;
		}

		if (my_aru_count > FAIL_TO_RECV_CONST &&
			token->aru_addr.s_addr == my_id.sin_addr.s_addr) {
			
			memb_set_merge (&token->aru_addr, 1,
				my_failed_list, &my_failed_list_entries);
			memb_state_gather_enter ();
		} else {
			my_token_seq = token->token_seq;
			token->token_seq += 1;

			if (memb_state == MEMB_STATE_RECOVERY) {
				/*
				 * my_aru == my_high_seq_received means this processor
				 * has recovered all messages it can recover
				 * (ie: its retrans queue is empty)
				 */
				low_water = my_aru;
				if (low_water > my_last_aru) {
					low_water = my_last_aru;
				}
				if (queue_is_empty (&retrans_message_queue) == 0 ||
					low_water != my_high_seq_received) {

					if (token->retrans_flg == 0) {
						token->retrans_flg = 1;
						my_set_retrans_flg = 1;
					}
				} else
				if (token->retrans_flg == 1 && my_set_retrans_flg) {
					token->retrans_flg = 0;
				}
printf ("token retrans flag is %d my set retrans flag%d retrans queue empty %d count %d, low_water %d aru %d\n", 
	token->retrans_flg, my_set_retrans_flg,
	queue_is_empty (&retrans_message_queue), my_retrans_flg_count,
	low_water, token->aru);
				if (token->retrans_flg == 0) { 
					my_retrans_flg_count += 1;
				} else {
					my_retrans_flg_count = 0;
				}
				if (my_retrans_flg_count == 2) {
					my_install_seq = token->seq;
				}
printf ("install seq %d aru %d high seq received %d\n", my_install_seq, my_aru,
my_high_seq_received);
				if (my_retrans_flg_count >= 2 && my_aru >= my_install_seq && my_received_flg == 0) {
					my_received_flg = 1;
					my_deliver_memb_entries = my_trans_memb_entries;
					memcpy (my_deliver_memb_list, my_trans_memb_list,
						sizeof (struct in_addr) * my_trans_memb_entries);
				}
				if (my_retrans_flg_count >= 3 && token->aru >= my_install_seq) {
					my_rotation_counter += 1;
				} else {
					my_rotation_counter = 0;
				}
				if (my_rotation_counter == 2) {
				printf ("retrans flag count %d token aru %d install seq %d aru %d %d\n",
					my_retrans_flg_count, token->aru, my_install_seq,
					my_aru, token->seq);

					memb_state_operational_enter ();
					my_rotation_counter = 0;
					my_retrans_flg_count = 0;
				}
			}
	
			token_send (token, 1 /* forward_token */);

#ifdef GIVEINFO
gettimeofday (&tv_current, NULL);
timersub (&tv_current, &tv_old, &tv_diff);
memcpy (&tv_old, &tv_current, sizeof (struct timeval));
if ((((float)tv_diff.tv_usec) / 100.0) > 5.0) {
printf ("I held %0.4f ms\n", ((float)tv_diff.tv_usec) / 100.0);
}
#endif
			if (my_do_delivery) {
				if (memb_state != MEMB_STATE_RECOVERY) {
					messages_deliver_to_app (0, &my_high_seq_delivered, my_high_seq_received);
				}
			}

			/*
			 * Deliver messages after token has been transmitted
			 * to improve performance
			 */
			reset_token_timeout (); // REVIEWED
			if (forward_token == 0) {
				reset_token_retransmit_timeout (); // REVIEWED
			}

			token_callbacks_execute (TOTEMSRP_CALLBACK_TOKEN_SENT);
		}
		break;
	}
			my_token_held = 0;
	return (0);
}

static void messages_deliver_to_app (int skip, int *start_point, int end_point)
{
    struct sort_queue_item *sort_queue_item_p;
    int i;
    int res;
    struct mcast *mcast;

	totemsrp_log_printf (totemsrp_log_level_debug,
		"Delivering %d to %d\n", *start_point + 1, my_high_seq_received);
	/*
	 * Deliver messages in order from rtr queue to pending delivery queue
	 */
	for (i = *start_point + 1; i <= end_point; i++) {
		void *ptr;

		res = sq_item_get (&regular_sort_queue, i, &ptr);
		if (res != 0 && skip) {
			*start_point = i;
			continue;
		}
		/*
		 * If hole, stop assembly
		 */
		if (res != 0) {
			break;
		}
		sort_queue_item_p = ptr;

		mcast = sort_queue_item_p->iovec[0].iov_base;
		assert (mcast != (struct mcast *)0xdeadbeef);

		/*
		 * Message found
		 */
		totemsrp_log_printf (totemsrp_log_level_debug,
			"Delivering MCAST message with seq %d to pending delivery queue\n",
			mcast->seq);

		*start_point = i;

		/*
		 * Message is locally originated multicasat
		 */
	 	if (sort_queue_item_p->iov_len > 1 &&
			sort_queue_item_p->iovec[0].iov_len == sizeof (struct mcast)) {
			totemsrp_deliver_fn (
				mcast->source,
				&sort_queue_item_p->iovec[1],
				sort_queue_item_p->iov_len - 1,
				mcast->header.endian_detector != ENDIAN_LOCAL);
		} else {
			sort_queue_item_p->iovec[0].iov_len -= sizeof (struct mcast);
			sort_queue_item_p->iovec[0].iov_base += sizeof (struct mcast);

			totemsrp_deliver_fn (
				mcast->source,
				sort_queue_item_p->iovec,
				sort_queue_item_p->iov_len,
				mcast->header.endian_detector != ENDIAN_LOCAL);

			sort_queue_item_p->iovec[0].iov_len += sizeof (struct mcast);
			sort_queue_item_p->iovec[0].iov_base -= sizeof (struct mcast);
		}
		stats_delv += 1;
	}
}

/*
 * recv message handler called when MCAST message type received
 */
static int message_handler_mcast (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received,
	int endian_conversion_needed)
{
	struct sort_queue_item sort_queue_item;
	struct sq *sort_queue;
	struct mcast mcast_header;

	if (memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &recovery_sort_queue;
	} else {
		sort_queue = &regular_sort_queue;
	}
	if (endian_conversion_needed) {
		mcast_endian_convert (iovec[0].iov_base, &mcast_header);
	} else {
		memcpy (&mcast_header, iovec[0].iov_base, sizeof (struct mcast));
	}

	assert (bytes_received < PACKET_SIZE_MAX);
#ifdef RANDOM_DROP
if (random()%100 < 20) {
	return (0);
}
#endif
	cancel_token_retransmit_timeout (); // REVIEWED

	/*
	 * If the message is foriegn execute the switch below
	 */
// TODO this detection of foreign messages isn't correct
// it doesn't work in the recovery state for the new processors
// my_memb_list is the wrong list to use I think we should use my_new_memb_list
	if (!memb_set_subset (&system_from->sin_addr,
		1,
		my_new_memb_list,
		my_new_memb_entries)) {

printf ("got foreign message\n");

		switch (memb_state) {
		case MEMB_STATE_OPERATIONAL:
			memb_set_merge (&system_from->sin_addr, 1,
				my_proc_list, &my_proc_list_entries);
			memb_state_gather_enter ();
			break;

		case MEMB_STATE_GATHER:
			if (!memb_set_subset (&system_from->sin_addr,
				1,
				my_proc_list,
				my_proc_list_entries)) {

				memb_set_merge (&system_from->sin_addr, 1,
					my_proc_list, &my_proc_list_entries);
				memb_state_gather_enter ();
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
		return (0); /* discard all foreign messages */
	}
	/*
	 * Add mcast message to rtr queue if not already in rtr queue
	 * otherwise free io vectors
	 */
	if (bytes_received > 0 && bytes_received < PACKET_SIZE_MAX &&
		sq_item_inuse (sort_queue, mcast_header.seq) == 0) {
//printf ("adding message %d\n", mcast->seq);

		/*
		 * Allocate new multicast memory block
		 */
		sort_queue_item.iovec[0].iov_base = malloc (bytes_received);
		if (sort_queue_item.iovec[0].iov_base == 0) {
			return (-1); /* error here is corrected by the algorithm */
		}
		memcpy (sort_queue_item.iovec[0].iov_base, iovec[0].iov_base,
			bytes_received);
		sort_queue_item.iovec[0].iov_len = bytes_received;
		assert (sort_queue_item.iovec[0].iov_len > 0);
		assert (sort_queue_item.iovec[0].iov_len < PACKET_SIZE_MAX);
		sort_queue_item.iov_len = 1;
		
		if (mcast_header.seq > my_high_seq_received) {
			my_high_seq_received = mcast_header.seq;
		}

		sq_item_add (sort_queue, &sort_queue_item, mcast_header.seq);
	}

	update_aru ();
	if (my_token_held) {
		my_do_delivery = 1;
	} else {
		if (memb_state != MEMB_STATE_RECOVERY) {
			messages_deliver_to_app (0, &my_high_seq_delivered, my_high_seq_received);
		}
	}

/* TODO remove from retrans message queue for old ring in recovery state */
	return (0);
}

int memb_join_process (struct memb_join *memb_join, struct sockaddr_in *system_from)
{
	struct memb_commit_token my_commit_token;

	if (memb_set_equal (memb_join->proc_list,
		memb_join->proc_list_entries,
		my_proc_list,
		my_proc_list_entries) &&

	memb_set_equal (memb_join->failed_list,
		memb_join->failed_list_entries,
		my_failed_list,
		my_failed_list_entries)) {

		memb_consensus_set (&system_from->sin_addr);
	
		if (memb_consensus_agreed () &&
			memb_lowest_in_config ()) {

			memb_state_commit_token_create (&my_commit_token);
	
			memb_state_commit_enter (&my_commit_token);
		} else {
			return (0); // TODO added to match spec
		}
	} else
	if (memb_set_subset (memb_join->proc_list,
		memb_join->proc_list_entries,
		my_proc_list,
		my_proc_list_entries) &&

		memb_set_subset (memb_join->failed_list,
		memb_join->failed_list_entries,
		my_failed_list, // TODO changed proc to failed to match spec
		my_failed_list_entries)) {

		return (0);
	} else
	if (memb_set_subset (&system_from->sin_addr, 1, // TODO changed proc to failed to match spec
		my_failed_list, my_failed_list_entries)) {

		return (0);
	} else {
		memb_set_merge (memb_join->proc_list,
			memb_join->proc_list_entries,
			my_proc_list, &my_proc_list_entries);

		if (memb_set_subset (&my_id.sin_addr, 1,
			memb_join->failed_list, memb_join->failed_list_entries)) {

			memb_set_merge (&system_from->sin_addr, 1,
				my_failed_list, &my_failed_list_entries);
		} else {
			memb_set_merge (memb_join->failed_list,
				memb_join->failed_list_entries,
				my_failed_list, &my_failed_list_entries);
		}
		memb_state_gather_enter ();
		return (1); /* gather entered */
	}
	return (0); /* gather not entered */
}

static void memb_join_endian_convert (struct memb_join *in, struct memb_join *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->proc_list_entries = swab32 (in->proc_list_entries);
	out->failed_list_entries = swab32 (in->failed_list_entries);
	out->ring_seq = swab64 (in->ring_seq);
	for (i = 0; i < out->proc_list_entries; i++) {
		out->proc_list[i].s_addr = in->proc_list[i].s_addr;
	}
	for (i = 0; i < out->failed_list_entries; i++) {
		out->failed_list[i].s_addr = in->failed_list[i].s_addr;
	}
}

static void memb_commit_token_endian_convert (struct memb_commit_token *in, struct memb_commit_token *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->token_seq = swab32 (in->token_seq);
	out->ring_id.rep.s_addr = in->ring_id.rep.s_addr;
	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->retrans_flg = swab32 (in->retrans_flg);
	out->memb_index = swab32 (in->memb_index);
	out->addr_entries = swab32 (in->addr_entries);
	for (i = 0; i < out->addr_entries; i++) {
		out->addr[i].s_addr = in->addr[i].s_addr;
		out->memb_list[i].ring_id.rep.s_addr =
			in->memb_list[i].ring_id.rep.s_addr;
		out->memb_list[i].ring_id.seq =
			swab64 (in->memb_list[i].ring_id.seq);
		out->memb_list[i].aru = swab32 (in->memb_list[i].aru);
		out->memb_list[i].high_delivered = swab32 (in->memb_list[i].high_delivered);
		out->memb_list[i].received_flg = swab32 (in->memb_list[i].received_flg);
	}
}

static void orf_token_endian_convert (struct orf_token *in, struct orf_token *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->seq = swab32 (in->seq);
	out->token_seq = swab32 (in->token_seq);
	out->aru = swab32 (in->aru);
	out->ring_id.rep.s_addr = in->ring_id.rep.s_addr;
	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->fcc = swab32 (in->fcc);
	out->retrans_flg = swab32 (in->retrans_flg);
	out->rtr_list_entries = swab32 (in->rtr_list_entries);
	for (i = 0; i < out->rtr_list_entries; i++) {
		out->rtr_list[i].ring_id.rep.s_addr = in->rtr_list[i].ring_id.rep.s_addr;
		out->rtr_list[i].ring_id.seq = swab64 (in->rtr_list[i].ring_id.seq);
		out->rtr_list[i].seq = swab32 (in->rtr_list[i].seq);
	}
}

static void mcast_endian_convert (struct mcast *in, struct mcast *out)
{
	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->seq = swab32 (in->seq);
	out->ring_id.rep.s_addr = in->ring_id.rep.s_addr;
	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->source = in->source;
	out->guarantee = in->guarantee;
}

static int message_handler_memb_join (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received,
	int endian_conversion_needed)
{
	struct memb_join *memb_join;
	struct memb_join memb_join_convert;

	int gather_entered;

	if (endian_conversion_needed) {
		memb_join = &memb_join_convert;
		memb_join_endian_convert (iovec->iov_base, &memb_join_convert);
	} else {
		memb_join = (struct memb_join *)iovec->iov_base;
	}

	if (token_ring_id_seq < memb_join->ring_seq) {
		token_ring_id_seq = memb_join->ring_seq;
	}
	switch (memb_state) {
		case MEMB_STATE_OPERATIONAL:
			gather_entered = memb_join_process (memb_join, system_from);
			if (gather_entered == 0) {
				memb_state_gather_enter ();
			}
			break;

		case MEMB_STATE_GATHER:
			memb_join_process (memb_join, system_from);
			break;
	
		case MEMB_STATE_COMMIT:
			if (memb_set_subset (&system_from->sin_addr,
				1,
				my_new_memb_list,
				my_new_memb_entries) &&

				memb_join->ring_seq >= my_ring_id.seq) {

				memb_join_process (memb_join, system_from);
				memb_state_gather_enter ();
			}
			break;

		case MEMB_STATE_RECOVERY:
			if (memb_set_subset (&system_from->sin_addr,
				1,
				my_new_memb_list,
				my_new_memb_entries) &&

				memb_join->ring_seq >= my_ring_id.seq) {

				memb_join_process (memb_join, system_from);
				memb_state_gather_enter ();
				my_aru = my_aru_save;
				my_high_seq_received = my_high_seq_received_save;
				sq_reinit (&recovery_sort_queue, 0);
				queue_reinit (&retrans_message_queue);

				// TODO calculate current old ring aru
			}
			break;
	}
	return (0);
}

static int message_handler_memb_commit_token (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received,
	int endian_conversion_needed)
{
	struct memb_commit_token memb_commit_token_convert;
	struct memb_commit_token *memb_commit_token;
	struct in_addr sub[PROCESSOR_COUNT_MAX];
	int sub_entries;

	
	if (endian_conversion_needed) {
		memb_commit_token = &memb_commit_token_convert;
		memb_commit_token_endian_convert (iovec->iov_base, memb_commit_token);
	} else {
		memb_commit_token = (struct memb_commit_token *)iovec->iov_base;
	}
	
/* TODO do we need to check for a duplicate token?
	if (memb_commit_token->token_seq > 0 &&
		my_token_seq >= memb_commit_token->token_seq) {

		printf ("already received commit token %d %d\n",
			memb_commit_token->token_seq, my_token_seq);
		return (0);
	}
*/
#ifdef RANDOM_DROP
if (random()%100 < 10) {
	return (0);
}
#endif
	switch (memb_state) {
		case MEMB_STATE_OPERATIONAL:
			/* discard token */
			break;

		case MEMB_STATE_GATHER:
			memb_set_subtract (sub, &sub_entries,
				my_proc_list, my_proc_list_entries,
				my_failed_list, my_failed_list_entries);
			
			if (memb_set_equal (memb_commit_token->addr,
				memb_commit_token->addr_entries,
				sub,
				sub_entries) &&

				memb_commit_token->ring_id.seq > my_ring_id.seq) {

				memb_state_commit_enter (memb_commit_token);
			}
			break;

		case MEMB_STATE_COMMIT:
			if (memcmp (&memb_commit_token->ring_id, &my_ring_id,
				sizeof (struct memb_ring_id)) == 0) {
//			 if (memb_commit_token->ring_id.seq == my_ring_id.seq) {
				memb_state_recovery_enter (memb_commit_token);
			}
			break;

		case MEMB_STATE_RECOVERY:
			totemsrp_log_printf (totemsrp_log_level_notice,
				"Sending initial ORF token\n");

			if (my_id.sin_addr.s_addr == my_ring_id.rep.s_addr) {
				// TODO convert instead of initiate
				orf_token_send_initial ();
				reset_token_timeout (); // REVIEWED
				reset_token_retransmit_timeout (); // REVIEWED
			}
			break;
	}
	return (0);
}

static int recv_handler (poll_handle handle, int fd, int revents,
	void *data, unsigned int *prio)
{
	struct msghdr msg_recv;
	struct message_header *message_header;
	struct sockaddr_in system_from;
	int res = 0;
	int bytes_received;

	*prio = UINT_MAX;

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_in);
	msg_recv.msg_iov = &totemsrp_iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;

	bytes_received = recvmsg (fd, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (bytes_received == -1) {
		return (0);
	} else {
		stats_recv += bytes_received;
	}
	if (bytes_received < sizeof (struct message_header)) {
		totemsrp_log_printf (totemsrp_log_level_security, "Received message is too short...  ignoring %d %d.\n", bytes_received);
		return (0);
	}

	message_header = (struct message_header *)msg_recv.msg_iov->iov_base;

	/*
	 * Authenticate and if authenticated, decrypt datagram
	 */
	totemsrp_iov_recv.iov_len = bytes_received;
	res = authenticate_and_decrypt (&totemsrp_iov_recv);
	log_digest = 0;
	if (res == -1) {
printf ("message header type %d %d\n", message_header->type, bytes_received);
		totemsrp_iov_recv.iov_len = PACKET_SIZE_MAX;
//exit (1);
		return 0;
	}

	if (stats_tv_start.tv_usec == 0) {
		gettimeofday (&stats_tv_start, NULL);
	}

	 /*
	 * Handle incoming message
	 */
	message_header = (struct message_header *)msg_recv.msg_iov[0].iov_base;
	totemsrp_message_handlers.handler_functions[(int)message_header->type] (
		&system_from,
		msg_recv.msg_iov,
		msg_recv.msg_iovlen,
		bytes_received,
		message_header->endian_detector != ENDIAN_LOCAL);

	totemsrp_iov_recv.iov_len = PACKET_SIZE_MAX;
	return (0);
}
