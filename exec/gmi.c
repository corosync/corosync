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
 * This code implements the ring protocol specified in Yair Amir's PhD thesis:
 *	http://www.cs.jhu.edu/~yairamir/phd.ps) (ch4,5). 
 *
 * Some changes have been made to the design to support things like fragmentation,
 * multiple I/O queues, and other things.
 */

#include <assert.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/types.h>
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

#include "poll.h"
#include "gmi.h"
#include "../include/queue.h"
#include "../include/sq.h"
#include "print.h"

extern struct sockaddr_in this_ip;

#define LOCALHOST_IP				inet_addr("127.0.0.1")
#define QUEUE_PEND_DELV_SIZE_MAX	((MESSAGE_SIZE_MAX / 1472) + 1) * 2
#define QUEUE_RTR_ITEMS_SIZE_MAX	512
#define QUEUE_PEND_TRANS_SIZE_MAX	((MESSAGE_SIZE_MAX / 1472) + 1) * 500
#define MAXIOVS						8
#define RTR_TOKEN_SIZE_MAX			32
#define MISSING_MCAST_WINDOW		64
#define TIMEOUT_STATE_GATHER		100
#define TIMEOUT_TOKEN				300
#define TIMEOUT_STATE_COMMIT		100
#define MAX_MEMBERS					16
#define HOLE_LIST_MAX				MISSING_MCAST_WINDOW
#define PRIORITY_MAX				3

int stats_sent = 0;
int stats_recv = 0;
int stats_delv = 0;
int stats_remcasts = 0;
int stats_orf_token = 0;
int stats_form_token = 0;
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
	MESSAGE_TYPE_MEMB_ATTEMPT_JOIN = 2, /* membership join attempt message */
	MESSAGE_TYPE_MEMB_JOIN = 3, 		/* membership join message */
	MESSAGE_TYPE_MEMB_FORM_TOKEN = 4	/* membership FORM token */
};

/*
 * In-order pending transmit queue
 */
struct queue queues_pend_trans[PRIORITY_MAX];

/*
 * In-order pending delivery queue
 */
struct pend_delv {
	struct in_addr ip;
	int seqid;
	int first_delivery;
	struct queue queue;
};

struct pend_delv queues_pend_delv[MAX_MEMBERS];

/*
 * Sorted delivery/retransmit queue
 */
struct sq queue_rtr_items;

/*
 * Multicast address
 */
struct sockaddr_in sockaddr_in_mcast;

/*
 * Multicast file descriptor
 */
int gmi_fd;

/*
 * Received up to and including
 */
int gmi_arut = 0;

/*
 * Delivered up to and including
 */
int gmi_adut = 0;

int gmi_adut_old = 0;

int gmi_original_arut = 0;

int gmi_highest_seq = 0;

int gmi_highest_seq_old = 0;

int gmi_barrier_seq = 0;

int gmi_last_seqid = 0;

int gmi_fragment = 0;

int gmi_pend_queue_priority = 0;

/*
 * Timers
 */
poll_timer_handle timer_orf_token_timeout = 0;

poll_timer_handle timer_form_token_timeout = 0;

poll_timer_handle timer_memb_state_gather_timeout = 0;

poll_timer_handle timer_memb_state_commit_timeout = 0;

poll_timer_handle timer_single_member = 0;

/*
 * Function called when new message received
 */
int (*gmi_recv) (char *group, struct iovec *iovec, int iov_len);

struct message_header {
	int type;
	int seqid;
};

struct memb_conf_id {
	struct in_addr rep;	
	struct timeval tv;
};

struct mcast {
	struct message_header header;
	char priority;
	struct memb_conf_id memb_conf_id;
	short packet_number;
	short packet_count;
	int packet_seq;
	struct in_addr source;
	struct gmi_groupname groupname;
};

struct rtr_item  {
	struct memb_conf_id conf_id;
	int seqid;
};

struct orf_token {
	struct message_header header;
	int group_arut;
	struct in_addr addr_arut;
	short int fcc;
	short int brake;
	struct in_addr brake_addr;
	struct rtr_item rtr_list[RTR_TOKEN_SIZE_MAX];
	int rtr_list_entries;
};

struct conf_desc {
	struct memb_conf_id conf_id;
	int highest_seq;
	int arut;
#ifdef COMPLIE_OUT
	int hole_list[HOLE_LIST_MAX];
	int hole_list_entries;
#endif
};

struct memb_form_token {
	struct message_header header;
	struct memb_conf_id conf_id;
	struct conf_desc conf_desc_list[MAX_MEMBERS]; /* SHOULD BE MAX_MEMBERS */
	int conf_desc_list_entries;
	struct in_addr member_list[MAX_MEMBERS];
	int member_list_entries;
	struct in_addr rep_list[MAX_MEMBERS];
	int rep_list_entries;
};
	

struct memb_attempt_join {
	struct message_header header;
};

struct memb_join {
	struct message_header header;
	struct in_addr active_rep_list[MAX_MEMBERS];
	int active_rep_list_entries;
	struct in_addr failed_rep_list[MAX_MEMBERS];
	int failed_rep_list_entries;
};

struct gmi_pend_trans_item {
	struct mcast *mcast;

	struct iovec iovec[MAXIOVS];
	int iov_len;
};

struct gmi_pend_delv_item {
	struct iovec iovec[MAXIOVS];
	int iov_len;
};

struct gmi_rtr_item {
	struct iovec iovec[MAXIOVS+2]; /* +2 is for mcast msg + group name  TODO is this right */
	int iov_len;
};

enum memb_state {
	MEMB_STATE_OPERATIONAL,
	MEMB_STATE_GATHER,
	MEMB_STATE_COMMIT,
	MEMB_STATE_FORM,
	MEMB_STATE_EVS
};

static enum memb_state memb_state = MEMB_STATE_GATHER;

static struct sockaddr_in memb_list[MAX_MEMBERS];
static int memb_list_entries = 1;
static int memb_list_entries_confchg = 1;

struct sockaddr_in memb_next;

struct in_addr memb_gather_set[MAX_MEMBERS];
int memb_gather_set_entries = 0;

struct memb_commit_set {
	struct sockaddr_in rep;
	struct in_addr join_rep_list[MAX_MEMBERS];
	int join_rep_list_entries;
	struct in_addr member_list[MAX_MEMBERS];
	int member_list_entries;
};

static struct memb_commit_set memb_commit_set[MAX_MEMBERS];

static int memb_commit_set_entries = 0;

static struct in_addr memb_failed_list[MAX_MEMBERS];

static int memb_failed_list_entries = 0;

static struct sockaddr_in memb_local_sockaddr_in;

static struct memb_conf_id memb_conf_id;

static struct memb_conf_id memb_form_token_conf_id;

static struct memb_join memb_join;

static struct memb_form_token memb_form_token;

char iov_buffer[MESSAGE_SIZE_MAX];

static struct iovec gmi_iov_recv = {
	iov_base:	iov_buffer,
	iov_len:	sizeof (iov_buffer)
};

struct message_handlers {
	int count;
	int (*handler_functions[5]) (struct sockaddr_in *, struct iovec *, int, int);
};

poll_handle *gmi_poll_handle;

void (*gmi_deliver_fn) (
	struct gmi_groupname *groupname,
	struct iovec *iovec,
	int iov_len) = 0;

void (*gmi_confchg_fn) (
	struct sockaddr_in *member_list, int member_list_entries,
	struct sockaddr_in *left_list, int left_list_entries,
	struct sockaddr_in *joined_list, int joined_list_entries) = 0;

/*
 * forward decls
 */
static int message_handler_orf_token (struct sockaddr_in *, struct iovec *, int, int);
static int message_handler_mcast (struct sockaddr_in *, struct iovec *, int, int);
static int message_handler_memb_attempt_join (struct sockaddr_in *, struct iovec *, int, int);
static int message_handler_memb_join (struct sockaddr_in *, struct iovec *, int, int);
static int message_handler_memb_form_token (struct sockaddr_in *, struct iovec *, int, int);
static void memb_conf_id_build (struct memb_conf_id *, struct in_addr);
static int recv_handler (poll_handle handle, int fd, int revents, void *data);
static int local_netif_determine (struct sockaddr_in *bindnet, struct sockaddr_in *bound_to);
static int memb_state_gather_enter (void);
static void pending_queues_deliver (void);
static int orf_token_mcast (struct orf_token *orf_token,
	int fcc_mcasts_allowed, struct sockaddr_in *system_from);
static void queues_pend_delv_memb_new (void);
static void calculate_group_arut (struct orf_token *orf_token);
static int messages_free (int group_arut);

struct message_handlers gmi_message_handlers = {
	5,
	{
		message_handler_orf_token,
		message_handler_mcast,
		message_handler_memb_attempt_join,
		message_handler_memb_join,
		message_handler_memb_form_token
	}
};

/*
 * Exported interfaces
 */
int gmi_init (
	struct sockaddr_in *sockaddr_mcast,
	struct sockaddr_in *sockaddr_bindnet,
	poll_handle *poll_handle,
	struct sockaddr_in *bound_to)
{
	int res;
	struct ip_mreqn mreqn;
	struct sockaddr_in sockaddr_in;
	char flag;
	int i;
	int index;

	memcpy (&sockaddr_in_mcast, sockaddr_mcast, sizeof (struct sockaddr_in));

	for (i = 0; i < PRIORITY_MAX; i++) {
		queue_init (&queues_pend_trans[i], QUEUE_PEND_TRANS_SIZE_MAX,
			sizeof (struct gmi_pend_trans_item));
	}
	sq_init (&queue_rtr_items, QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct gmi_rtr_item), 0);
	index = local_netif_determine (sockaddr_bindnet, bound_to);
	if (index == -1) {
		return (-1);
	}
	memcpy (&memb_list[0], &memb_local_sockaddr_in, sizeof (struct sockaddr_in));
	memb_conf_id_build (&memb_conf_id, memb_local_sockaddr_in.sin_addr);
	memcpy (&memb_form_token_conf_id, &memb_conf_id, sizeof (struct memb_conf_id));

	/*
	 * Set local sock addr for new socket
	 */
	sockaddr_in.sin_family = AF_INET;
	sockaddr_in.sin_addr.s_addr = htonl (INADDR_ANY);
	sockaddr_in.sin_port = sockaddr_in_mcast.sin_port;

	/*
	 * Join group membership on socket
	 */
	mreqn.imr_multiaddr.s_addr = sockaddr_mcast->sin_addr.s_addr;
	mreqn.imr_ifindex = index + 1;

	gmi_fd = socket (AF_INET, SOCK_DGRAM, 0);
	if (gmi_fd == -1) {
		perror ("socket");
		return (-1);
	}

	res = bind (gmi_fd, (struct sockaddr *)&sockaddr_in,
		sizeof (struct sockaddr_in));
	if (res == -1) {
		perror ("bind failed");
		return (-1);
	}
#ifdef BROADCAST_CODE_TAKEN_OUT
this code is the broadcast socket option vs multicast socket option
	setsockopt (gmi_fd, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof (on));
#endif
	res = setsockopt (gmi_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		&mreqn, sizeof (mreqn));
	if (res == -1) {
		perror ("join multicast group failed");
		return (-1);
	}

	flag = 0;
	res = setsockopt (gmi_fd, IPPROTO_IP, IP_MULTICAST_LOOP,
		&flag, sizeof (flag));
	if (res == -1) {
		perror ("turn off loopback");
		return (-1);
	}

	gmi_poll_handle = poll_handle;

	poll_dispatch_add (*gmi_poll_handle, gmi_fd, POLLIN, 0, recv_handler);

	memb_state_gather_enter ();

	memset (&memb_next, 0, sizeof (struct sockaddr_in));

	queues_pend_delv_memb_new ();

	return (0);
}

int gmi_join (
	struct gmi_groupname *groupname,
	void (*deliver_fn) (
		struct gmi_groupname *groupname,
		struct iovec *iovec,
		int iov_len),
	void (*confchg_fn) (
		struct sockaddr_in *member_list, int member_list_entries,
		struct sockaddr_in *left_list, int left_list_entries,
		struct sockaddr_in *joined_list, int joined_list_entries),
	gmi_join_handle *handle_out) {

	gmi_deliver_fn = deliver_fn;
	gmi_confchg_fn = confchg_fn;
	*handle_out = 0;

	return (0);
}

int local_host_seq_count = 0;

int gmi_leave (
	gmi_join_handle handle_join);

static int gmi_pend_trans_item_store (
    struct gmi_groupname *groupname,
	struct iovec *iovec,
	int iov_len,
	int priority,
	 short packet_number, short packet_count)
{
	int i, j;
	struct gmi_pend_trans_item gmi_pend_trans_item;
	/*
	 * Store pending item
	 */
	gmi_pend_trans_item.mcast = malloc (sizeof (struct mcast));
	if (gmi_pend_trans_item.mcast == 0) {
		goto error_mcast;
	}

	/*
	 * Set mcast header
	 */
	gmi_pend_trans_item.mcast->header.type = MESSAGE_TYPE_MCAST;
	gmi_pend_trans_item.mcast->priority = priority;
	gmi_pend_trans_item.mcast->packet_number = packet_number;
	gmi_pend_trans_item.mcast->packet_count = packet_count;
	gmi_pend_trans_item.mcast->packet_seq = local_host_seq_count++;
	gmi_pend_trans_item.mcast->source.s_addr = this_ip.sin_addr.s_addr;

	memcpy (&gmi_pend_trans_item.mcast->groupname, groupname,
		sizeof (struct gmi_groupname));

	for (i = 0; i < iov_len; i++) {
		gmi_pend_trans_item.iovec[i].iov_base = malloc (iovec[i].iov_len);
		if (gmi_pend_trans_item.iovec[i].iov_base == 0) {
			goto error_iovec;
		}
		memcpy (gmi_pend_trans_item.iovec[i].iov_base, iovec[i].iov_base,
			iovec[i].iov_len);
		gmi_pend_trans_item.iovec[i].iov_len = iovec[i].iov_len;
	}
	gmi_pend_trans_item.iov_len = iov_len;

	log_printf (LOG_LEVEL_DEBUG, "mcasted message added to pending queue\n");
	queue_item_add (&queues_pend_trans[priority], &gmi_pend_trans_item);

	return (0);
error_iovec:
	for (j = 0; j < i; j++) {
		free (gmi_pend_trans_item.iovec[j].iov_base);
	}
	return (-1);
error_mcast:
	return (0);
}

/*
 * MTU - multicast message header - IP header - UDP header
 *
 * On lossy switches, making use of the DF UDP flag can lead to loss of
 * forward progress.  So the packets must be fragmented by the algorithm
 * and reassembled at the receiver.
 */
#define FRAGMENT_SIZE (1500 - sizeof (struct mcast) - 20 - 8)

static void timer_function_single_member (void *data);

/*
 * With only a single member, multicast messages as if an orf token was
 * delivered.  This is done as part of the main event loop by specifying
 * a timer with an immediate expiration.  This is a little suboptimal
 * since poll starts afresh.  If more messages are waiting to be
 * self-delivered, queue the timer function again until there are no
 * more waiting messages.
 */
static void single_member_deliver (void)
{
	poll_timer_delete (*gmi_poll_handle, timer_single_member);
	timer_single_member = 0;
	poll_timer_add (*gmi_poll_handle, 0, 0,
		timer_function_single_member, &timer_single_member);
}

static void timer_function_single_member (void *data)
{
	struct orf_token orf_token;
	int more_messages;

	memset (&orf_token, 0, sizeof (struct orf_token));
	orf_token.header.seqid = gmi_arut;
	orf_token.header.type = MESSAGE_TYPE_ORF_TOKEN;
	orf_token.group_arut = gmi_arut;
	orf_token.rtr_list_entries = 0;
	more_messages = orf_token_mcast (&orf_token, 99, &memb_local_sockaddr_in);
	calculate_group_arut (&orf_token);
	messages_free (gmi_arut);

	/*
	 * Queue delivery again if more messages are available
	 */
	if (more_messages) {
		single_member_deliver ();
	}
}

int gmi_mcast (
    struct gmi_groupname *groupname,
    struct iovec *iovec,
    int iov_len,
	int priority)
{
	int res;
	struct iovec copied_iovec;
	struct iovec pending_iovecs[MAXIOVS];
	int pending_iovec_entries = 0;
	int iovec_entry = 0;
	int total_size;
	int packet_size;
	int i;
	int packet_number = 0;
	int packet_count = 0;

	packet_size = FRAGMENT_SIZE;

	log_printf (LOG_LEVEL_DEBUG, "MCASTING MESSAGE\n");

	/*
	 * Determine size of total message
	 */
	total_size = 0;
	for (i = 0; i < iov_len; i++) {
		total_size += iovec[i].iov_len;
		assert (iovec[i].iov_len < MESSAGE_SIZE_MAX);
	}

	packet_count = (total_size / packet_size);

	log_printf (LOG_LEVEL_DEBUG, "Message size is %d\n", total_size);

	/*
	 * Break message up into individual packets and publish them
	 */
	copied_iovec.iov_base = iovec[0].iov_base;
	copied_iovec.iov_len = iovec[0].iov_len;
	packet_size = 0;
	pending_iovec_entries = 0;
	iovec_entry = 0;
	do {
		if (copied_iovec.iov_len + packet_size > FRAGMENT_SIZE) {
			pending_iovecs[pending_iovec_entries].iov_base = copied_iovec.iov_base;
			pending_iovecs[pending_iovec_entries].iov_len = FRAGMENT_SIZE - packet_size;
			copied_iovec.iov_base += FRAGMENT_SIZE - packet_size;
			copied_iovec.iov_len -= FRAGMENT_SIZE - packet_size;
			packet_size += pending_iovecs[pending_iovec_entries].iov_len;
		} else {
			pending_iovecs[pending_iovec_entries].iov_base = copied_iovec.iov_base;
			pending_iovecs[pending_iovec_entries].iov_len = copied_iovec.iov_len;
			packet_size += copied_iovec.iov_len;
			iovec_entry += 1; /* this must be before copied_iovec */
			copied_iovec.iov_base = iovec[iovec_entry].iov_base;
			copied_iovec.iov_len = iovec[iovec_entry].iov_len;
		}
		pending_iovec_entries += 1;
		if (packet_size >= FRAGMENT_SIZE || packet_size == total_size) {
#ifdef DEBUG
for (i = 0; i < pending_iovec_entries; i++) {
	assert (pending_iovecs[i].iov_len < MESSAGE_SIZE_MAX);
	assert (pending_iovecs[i].iov_len >= 0);
	printf ("iovecs[%d] %x %d\n", i, pending_iovecs[i].iov_base, pending_iovecs[i].iov_len);
calced_total += pending_iovecs[i].iov_len;
}
printf ("CALCULATED TOTAL is %d\n", calced_total);
#endif
			total_size -= packet_size;
			assert (total_size >= 0);
			res = gmi_pend_trans_item_store (groupname, pending_iovecs,
				pending_iovec_entries, priority, packet_number, packet_count);
			pending_iovec_entries = 0;
			iovec_entry = 0;
			packet_size = 0;
			packet_number += 1;
		}
	} while (total_size > 0);

	/*
	 * The queued messages are sent in orf_token_mcast, not this function
	 * But if this processor is the only node, it must deliver the messages
	 * for self-delivery requirements because orf_token_mcast is only called
	 * on reception of a token
	 */
	if (memb_list_entries == 1) {
		single_member_deliver ();
	}

	return (0);
}

static int local_netif_determine (struct sockaddr_in *bindnet, struct sockaddr_in *bound_to)
{
	struct sockaddr_in *sockaddr_in;
	int id_fd;
	struct ifconf ifc;
	int numreqs = 0;
	int res;
	int i;

	/*
	 * Generate list of local interfaces in ifc.ifc_req structure
	 */
	id_fd = socket (AF_INET, SOCK_DGRAM, 0);
	ifc.ifc_buf = 0;
	do {
		numreqs += 8;
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
	 * Find interface to bind to
	 */
	for (i = 0; i < ifc.ifc_len / sizeof (struct ifreq); i++) {
	sockaddr_in = (struct sockaddr_in *)&ifc.ifc_ifcu.ifcu_req[i].ifr_ifru.ifru_addr;
		if ((sockaddr_in->sin_addr.s_addr & 0x00ffffff) == (bindnet->sin_addr.s_addr & 0x00ffffff)) {
			memb_local_sockaddr_in.sin_addr.s_addr = sockaddr_in->sin_addr.s_addr;
			memb_local_sockaddr_in.sin_family = AF_INET;
			memb_local_sockaddr_in.sin_port = sockaddr_in_mcast.sin_port;
			memcpy (bound_to, &memb_local_sockaddr_in, sizeof (struct sockaddr_in));
			res = i;
			break; /* for */
		}
	}
	free (ifc.ifc_buf);
	close (id_fd);
	
	return (res);
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
int orf_token_remcast (int seqid) {
	struct msghdr msg_mcast;
	struct gmi_rtr_item *gmi_rtr_item;
	int res;
	struct mcast *mcast;

//printf ("remulticasting %d\n", seqid);
	/*
	 * Get RTR item at seqid, if not available, return
	 */
	res = sq_item_get (&queue_rtr_items, seqid, (void **)&gmi_rtr_item);
	if (res != 0) {
		return -1;
	}
	mcast = gmi_rtr_item->iovec[0].iov_base;

	/*
	 * Build multicast message
	 */
	msg_mcast.msg_name = &sockaddr_in_mcast;
	msg_mcast.msg_namelen = sizeof (struct sockaddr_in);
	msg_mcast.msg_iov = gmi_rtr_item->iovec;
	msg_mcast.msg_iovlen = gmi_rtr_item->iov_len;
	msg_mcast.msg_control = 0;
	msg_mcast.msg_controllen = 0;
	msg_mcast.msg_flags = 0;

	/*
	 * Multicast message
	 */
	res = sendmsg (gmi_fd, &msg_mcast, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (res == -1) {
printf ("error during remulticast %d %d %d\n", seqid, errno, gmi_rtr_item->iov_len);
		return (-1);
	}
	stats_sent += res;
	return (0);
}

int last_group_arut = 0;
int last_released = 0;
int set_arut = -1;

/*
 * Brake output multicasts if the missing window is too large
 */
int gmi_brake;
		
static int messages_free (int group_arut)
{
	struct gmi_rtr_item *gmi_rtr_item_p;
	int i, j;
	int res;
	int lesser;

// TODO printf ("group arut %d last_group-arut  %d gmi_dut %d barrier %d\n", group_arut, last_group_arut, gmi_dut, gmi_barrier_seq);
	/*
	 * Determine braking value (when messages + MISSING_MCAST_WINDOW, stop sending messages)
	 */
	gmi_brake = group_arut;
	if (gmi_brake > last_group_arut) {
		gmi_brake = last_group_arut;
	}
	
	/*
	 * Determine low water mark for messages to be freed
	 */
	lesser = gmi_brake;
	if (lesser > gmi_adut) {
		lesser = gmi_adut;
	}

//printf ("Freeing lesser %d %d %d\n", lesser, group_arut, last_group_arut);
//printf ("lesser %d gropu arut %d last group arut %d\n", lesser, group_arut, last_group_arut);
		
	/*
	 * return early if no messages can be freed
	 */
/*
	if (last_released + 1 == lesser) {
		return (0);
	}
*/

	/*
	 * Release retransmit list items if group arut indicates they are transmitted
	 */
	for (i = last_released; i <= lesser; i++) {
		res = sq_item_get (&queue_rtr_items, i, (void **)&gmi_rtr_item_p);
		if (res == 0) {
			for (j = 0; j < gmi_rtr_item_p->iov_len; j++) {
				free (gmi_rtr_item_p->iovec[j].iov_base);
				gmi_rtr_item_p->iovec[j].iov_base = (void *)0xdeadbeef;
				gmi_rtr_item_p->iovec[j].iov_len = i;
			}
		}
		last_released = i + 1;
	}

	sq_items_release (&queue_rtr_items, lesser);
	log_printf (LOG_LEVEL_DEBUG, "releasing messages up to and including %d\n", lesser);
	return (0);
}

/*
 * Multicasts pending messages onto the ring (requires orf_token possession)
 */
static int orf_token_mcast (
	struct orf_token *orf_token,
	int fcc_mcasts_allowed,
	struct sockaddr_in *system_from)
{
	struct msghdr msg_mcast;
	struct gmi_rtr_item gmi_rtr_item;
	struct gmi_pend_trans_item *gmi_pend_trans_item = 0;
	int res = 0;
	int orf_token_seqid;
	struct mcast *mcast;
	int last_packet = 1;
	struct queue *queue_pend_trans;

	/*
	 * Disallow multicasts unless state is operational
	 */
	if (memb_state != MEMB_STATE_OPERATIONAL) {
		return (0);
	}

	/*
	 * If received a token with a higher sequence number,
	 * set highest seq so retransmits can happen at end of 
	 * message stream
	 */
	if (orf_token->header.seqid > gmi_highest_seq) {
		gmi_highest_seq = orf_token->header.seqid;
	}

	orf_token_seqid = orf_token->header.seqid;

	queue_pend_trans = &queues_pend_trans[gmi_pend_queue_priority];

	for (fcc_mcast_current = 0; fcc_mcast_current < fcc_mcasts_allowed; fcc_mcast_current++) {
		/*
		 * determine which pending queue to take message
		 * from if this is not a message fragment
		 */
		if (gmi_fragment == 0) {
			gmi_pend_queue_priority = 0;
			do {
				queue_pend_trans = &queues_pend_trans[gmi_pend_queue_priority];

				if (queue_is_empty (queue_pend_trans)) {
					gmi_pend_queue_priority++;
				} else {
					break; /* from do - found first queue with data */
				}
			} while (gmi_pend_queue_priority < PRIORITY_MAX);
		}

		if (gmi_pend_queue_priority == PRIORITY_MAX) {
			break; /* all queues are empty, break from for */
		}
//		printf ("selecting pending queue %d\n", gmi_pend_queue_priority);

		gmi_pend_trans_item = (struct gmi_pend_trans_item *)queue_item_get (queue_pend_trans);
		/* preincrement required by algo */
		gmi_pend_trans_item->mcast->header.seqid = ++orf_token->header.seqid;
// UNDO printf ("multicasting seqid %d\n", gmi_pend_trans_item->mcast->header.seqid);
		
		last_packet = (gmi_pend_trans_item->mcast->packet_number ==
			gmi_pend_trans_item->mcast->packet_count);
//printf ("last packet is %d current mcast %d\n", last_packet, fcc_mcast_current);

		/*
		 * Build IO vector
		 */
		gmi_rtr_item.iovec[0].iov_base = gmi_pend_trans_item->mcast;
		gmi_rtr_item.iovec[0].iov_len = sizeof (struct mcast);

		mcast = gmi_rtr_item.iovec[0].iov_base;

		/*
		 * Is this a fragment of a message
		 */
		if (mcast->packet_number == mcast->packet_count) {
			gmi_fragment = 0;
		} else {
			gmi_fragment = 1;
		}

		memcpy (&mcast->memb_conf_id, &memb_form_token_conf_id,
			sizeof (struct memb_conf_id));

		memcpy (&gmi_rtr_item.iovec[1], gmi_pend_trans_item->iovec,
			gmi_pend_trans_item->iov_len * sizeof (struct iovec));

		gmi_rtr_item.iov_len = gmi_pend_trans_item->iov_len + 1;

		assert (gmi_rtr_item.iov_len < 16);

		/*
		 * Add message to retransmit queue
		 */
		sq_item_add (&queue_rtr_items,
			&gmi_rtr_item, gmi_pend_trans_item->mcast->header.seqid);

		/*
		 * Delete item from pending queue
		 */
		queue_item_remove (queue_pend_trans);

		/*
		 * Build multicast message
		 */
		msg_mcast.msg_name = &sockaddr_in_mcast;
		msg_mcast.msg_namelen = sizeof (struct sockaddr_in);
		msg_mcast.msg_iov = gmi_rtr_item.iovec;
		msg_mcast.msg_iovlen = gmi_rtr_item.iov_len;
		msg_mcast.msg_control = 0;
		msg_mcast.msg_controllen = 0;
		msg_mcast.msg_flags = 0;

		/*
		 * Multicast message
		 */
		res = sendmsg (gmi_fd, &msg_mcast, MSG_NOSIGNAL | MSG_DONTWAIT);
		/*
		 * An error here is recovered by the multicast algorithm
	 	 */

// TODO stats_sent isn't right below
		stats_sent += res;
	}
	assert (fcc_mcast_current < 100);
#ifdef OUTA
	if (fcc_mcast_current > fcc_mcasts_allowed) {
		fcc_mcast_current = fcc_mcasts_allowed;
	}
#endif
	/*
	 * If messages mcasted, deliver any new messages to pending queues
	 */
	if (fcc_mcast_current) {
		if (gmi_pend_trans_item->mcast->header.seqid > gmi_highest_seq) {
			gmi_highest_seq = gmi_pend_trans_item->mcast->header.seqid;
		}
		pending_queues_deliver ();
//printf ("orf Token seqid is %d group %d\n", orf_token_seqid, orf_token->group_arut);
#ifdef COMPILE_OUT
		if (orf_token_seqid == orf_token->group_arut) {
//printf ("previous group arut #1 %d\n", orf_token->group_arut);
			orf_token->group_arut = orf_token_seqid + fcc_mcast_current;
			orf_token->addr_arut.s_addr = 0;
}
//printf ("reasing group arut to %d\n", orf_token->group_arut);
#endif
	}
		
	/*
	 * Return 1 if more messages are available for single node clusters
	 */
	return (fcc_mcast_current == fcc_mcasts_allowed);
}

/*
 * Remulticasts messages in orf_token's retransmit list (requires orf_token)
 * Modify's orf_token's rtr to include retransmits required by this process
 */
static void orf_token_rtr (
	struct orf_token *orf_token,
	int *fcc_allowed)
{
	int res;
	int i, j;
	int found;

#ifdef COMPLE_OUT
printf ("Retransmit List %d\n", orf_token->rtr_list_entries);
for (i = 0; i < orf_token->rtr_list_entries; i++) {
	printf ("%d ", orf_token->rtr_list[i].seqid);
}
printf ("\n");
#endif

	/*
	 * Retransmit messages on orf_token's RTR list from RTR queue
	 */
	for (fcc_remcast_current = 0, i = 0;
		fcc_remcast_current <= *fcc_allowed && i < orf_token->rtr_list_entries;) {
#ifdef COMPILE_OUT
printf ("%d.%d.%d vs %d.%d.%d\n",
	orf_token->rtr_list[i].conf_id.rep.s_addr,
	orf_token->rtr_list[i].conf_id.tv.tv_sec,
	orf_token->rtr_list[i].conf_id.tv.tv_usec,
	memb_form_token_conf_id.rep.s_addr,
	memb_form_token_conf_id.tv.tv_sec,
	memb_form_token_conf_id.tv.tv_usec);
#endif
		/*
		 * If this retransmit request isn't from this configuration,
		 * try next rtr entry
		 */
 		if (memcmp (&orf_token->rtr_list[i].conf_id, &memb_form_token_conf_id,
			sizeof (struct memb_conf_id)) != 0) {

			i++;
			continue;
		}
		assert (orf_token->rtr_list[i].seqid > 0);
		res = orf_token_remcast (orf_token->rtr_list[i].seqid);
		if (res == 0) {
			orf_token->rtr_list_entries -= 1;
			assert (orf_token->rtr_list_entries >= 0);
			memmove (&orf_token->rtr_list[i],
				&orf_token->rtr_list[i + 1],
				sizeof (struct rtr_item) * (orf_token->rtr_list_entries));
			fcc_remcast_current++;
			stats_remcasts++;
		} else {
			i++;
//printf ("couldn't remcast %d\n", i);
		}
	}
	*fcc_allowed = *fcc_allowed - fcc_remcast_current - 1;

#ifdef COMPILE_OUT
for (i = 0; i < orf_token->rtr_list_entries; i++) {
	assert (orf_token->rtr_list[i].seqid != -1);
}
#endif

	/*
	 * Add messages to retransmit to RTR list
	 * but only retry if there is room in the retransmit list
	 */
	for (i = gmi_arut + 1;
			orf_token->rtr_list_entries < RTR_TOKEN_SIZE_MAX &&
		//	i <= orf_token->header.seqid; /* TODO this worked previously but not correct for EVS */
			i <= gmi_highest_seq;
			i++) {

		res = sq_item_inuse (&queue_rtr_items, i);
		if (res == 0) {
			found = 0;
			for (j = 0; j < orf_token->rtr_list_entries; j++) {
				if (i == orf_token->rtr_list[j].seqid) {
					found = 1;
				}
			}
			if (found == 0) {
				memcpy (&orf_token->rtr_list[orf_token->rtr_list_entries].conf_id,
					&memb_form_token_conf_id, sizeof (struct memb_conf_id));
				orf_token->rtr_list[orf_token->rtr_list_entries].seqid = i;
				orf_token->rtr_list_entries++;
//printf ("adding to retransmit list %d\n", i);
			}
		}
	}
}

/*
 * Calculate flow control count
 */
static void orf_token_fcc (
	struct orf_token *orf_token)
{
	orf_token->fcc = orf_token->fcc - fcc_mcast_last - fcc_remcast_last
		+ fcc_mcast_current + fcc_remcast_current;

	fcc_mcast_last = fcc_mcast_current;
	fcc_remcast_last = fcc_remcast_current;
}

static void queues_pend_delv_memb_new (void)
{
	struct pend_delv pend_delv_new[MAX_MEMBERS];
	int item_index = 0;
	int i, j;
	int found;

	memset (pend_delv_new, 0, sizeof (struct pend_delv) * MAX_MEMBERS);

	/*
	 * Build new pending list
	 */
	for (i = 0; i < memb_list_entries_confchg; i++) {
		found = 0;
		for (j = 0; j < MAX_MEMBERS; j++) {
			/*
			 * If membership item in queues pending delivery list, copy it
			 */
			if (memb_list[i].sin_addr.s_addr == queues_pend_delv[j].ip.s_addr) {
				memcpy (&pend_delv_new[item_index], &queues_pend_delv[j],
					sizeof (struct pend_delv));
				item_index += 1;
				found = 1;
				break; /* for j = */
			}
		}
		/*
		 * If membership item not found in pending delivery list, make new entry
		 */
		if (found == 0) {
			queue_init (&pend_delv_new[item_index].queue, QUEUE_PEND_DELV_SIZE_MAX,
				sizeof (struct gmi_pend_delv_item));
			pend_delv_new[item_index].seqid = 0;
			pend_delv_new[item_index].ip.s_addr = memb_list[i].sin_addr.s_addr;
			item_index += 1;
		}
	}

	/*
	 * Copy new list into system list
	 */
	memcpy (queues_pend_delv, pend_delv_new,
		sizeof (struct pend_delv) * MAX_MEMBERS);

	for (i = 0; i < memb_list_entries_confchg; i++) {
		/*
		 * If queue not empty, mark it for first delivery
		 * otherwise reset seqno
		 */
		if (queue_is_empty (&queues_pend_delv[i].queue) == 0) {
			queues_pend_delv[i].first_delivery = 1;
		} else {
			queues_pend_delv[i].seqid = 0;
		}
	}
}

static int orf_token_evs (
	struct orf_token *orf_token,
	int starting_group_arut)
{
	int i, j;
	struct sockaddr_in trans_memb_list[MAX_MEMBERS];
	struct sockaddr_in left_list[MAX_MEMBERS];
	struct sockaddr_in joined_list[MAX_MEMBERS];
	int trans_memb_list_entries = 0;
	int left_list_entries = 0;
	int joined_list_entries = 0;
	int found;

//printf ("group arut is %d %d %d %d\n", orf_token->header.seqid, orf_token->group_arut, gmi_arut, gmi_highest_seq);
	/*
	 * We should only execute this function if we are in EVS membership state
	 */
	if (memb_state != MEMB_STATE_EVS) {
		return (0);
	}

	/*
	 * Delete form token timer since the token has been swallowed
	 */
	poll_timer_delete (*gmi_poll_handle, timer_form_token_timeout);
	timer_form_token_timeout = 0;

printf ("EVS STATE group arut %d gmi arut %d highest %d barrier %d starting group arut %d\n", orf_token->group_arut, gmi_arut, gmi_highest_seq, gmi_barrier_seq, starting_group_arut);

	/*
	 * This node has reached highest seq, set local arut to barrier
	 */
	if (gmi_arut == gmi_highest_seq) {
//printf ("setting arut to barrier %d\n", gmi_barrier_seq);
		gmi_arut = gmi_barrier_seq;
	}

	/*
	 * Determine when EVS recovery has completed
	 */
//printf ("group arut is %d %d %d\n", orf_token->group_arut, gmi_arut, gmi_highest_seq);
// TODO
	if (memb_state == MEMB_STATE_EVS && gmi_arut == gmi_barrier_seq && orf_token->group_arut == gmi_barrier_seq) {
		log_printf (LOG_LEVEL_NOTICE, "EVS recovery of messages complete, transitioning to operational.\n");
		/*
		 * EVS recovery complete, reset local variables
		 */
		gmi_arut = 0;

		gmi_adut_old = gmi_adut;
		gmi_adut = 0;

		gmi_highest_seq_old = gmi_highest_seq;
		gmi_highest_seq = 0;
		last_group_arut = 0;
		sq_reinit (&queue_rtr_items, 0);

		memb_failed_list_entries = 0;

		memb_state = MEMB_STATE_OPERATIONAL;
		qsort (memb_form_token.member_list, memb_form_token.member_list_entries,
			sizeof (struct in_addr), in_addr_compare);


		/*
		 * Determine transitional configuration
		 */
		for (i = 0; i < memb_list_entries_confchg; i++) {
			for (found = 0, j = 0; j < memb_form_token.member_list_entries; j++) {
				if (memb_list[i].sin_addr.s_addr == memb_form_token.member_list[j].s_addr) {
					found = 1;
					break;
				}
			}
			if (found == 1) {
				trans_memb_list[trans_memb_list_entries].sin_addr.s_addr = memb_list[i].sin_addr.s_addr;
				trans_memb_list[trans_memb_list_entries].sin_family = AF_INET;
				trans_memb_list[trans_memb_list_entries].sin_port = sockaddr_in_mcast.sin_port;
				trans_memb_list_entries += 1;
			}
		}

		/*
		 * Determine nodes that left the configuration
		 */
		for (i = 0; i < memb_list_entries_confchg; i++) {
			for (found = 0, j = 0; j < memb_form_token.member_list_entries; j++) {
				if (memb_list[i].sin_addr.s_addr == memb_form_token.member_list[j].s_addr) {
					found = 1;
					break; /* for j = 0 */
				}
			}
			/*
			 * Node left membership, add it to list
			 */
			if (found == 0) {
				left_list[left_list_entries].sin_addr.s_addr = memb_list[i].sin_addr.s_addr;
				left_list[left_list_entries].sin_family = AF_INET;
				left_list[left_list_entries].sin_port = sockaddr_in_mcast.sin_port;
				left_list_entries += 1;
			}
		}

		/*
		 * MAIN STEP:
		 * Deliver transitional configuration
		 */
		if (gmi_confchg_fn &&
			(trans_memb_list_entries != memb_list_entries ||
			(memcmp (trans_memb_list, memb_list, sizeof (struct sockaddr_in) * memb_list_entries) != 0))) {
			gmi_confchg_fn (trans_memb_list, trans_memb_list_entries,
				left_list, left_list_entries,
				0, 0);
		}

		
		/*
		 * Determine nodes that joined the configuration
		 */
		for (i = 0; i < memb_form_token.member_list_entries; i++) {
			for (found = 0, j = 0; j < memb_list_entries_confchg; j++) {
				if (memb_form_token.member_list[i].s_addr == memb_list[j].sin_addr.s_addr) {
					found = 1;
					break; /* for j = 0 */
				}
			}

			/*
			 * Node joined membership, add it to list
			 */
			if (found == 0) {
				joined_list[joined_list_entries].sin_addr.s_addr = memb_form_token.member_list[i].s_addr;
				joined_list[joined_list_entries].sin_family = AF_INET;
				joined_list[joined_list_entries].sin_port = sockaddr_in_mcast.sin_port;
				joined_list_entries += 1;
			}
		}

		/*
		 * Install the form token's configuration into the local membership
		 */
		for (i = 0; i < memb_form_token.member_list_entries; i++) {
			memb_list[i].sin_addr.s_addr = memb_form_token.member_list[i].s_addr;
			memb_list[i].sin_family = AF_INET;
			memb_list[i].sin_port = sockaddr_in_mcast.sin_port;
		}

		/*
		 * Install pending delivery queues
		 */
		memb_list_entries = memb_form_token.member_list_entries;
		memb_list_entries_confchg = memb_list_entries;
		queues_pend_delv_memb_new ();

		/*
		 * Install new conf id
		 */
		memcpy (&memb_conf_id, &memb_form_token.conf_id,
			sizeof (struct memb_conf_id));
		memcpy (&memb_form_token_conf_id, &memb_form_token.conf_id,
			sizeof (struct memb_conf_id));

		/*
		 * Deliver regular configuration
		 */
		if (gmi_confchg_fn) {
			gmi_confchg_fn (memb_list, memb_list_entries,
				left_list, 0,
				joined_list, joined_list_entries);
		}
	}

	return (0);
}

int gwin = 90;
int pwin = 45;


static int orf_fcc_allowed (struct orf_token *token)
{
	int allowed;

	if (memb_state != MEMB_STATE_OPERATIONAL) {
		return (0);
	}
	allowed = gwin + pwin - token->fcc;
	if (allowed < 0)  {
		allowed = 0;
	}
	if (allowed > gwin) {
		allowed = gwin;
	}
	if (allowed > pwin) {
		allowed = pwin;
	}
	return (allowed);
}

void timer_function_form_token_timeout (void *data)
{
	log_printf (LOG_LEVEL_WARNING, "Token loss in FORM state\n");
	memb_list_entries = 1;

	/*
	 * Add highest rep to failed list to ensure termination
	 */
	memb_failed_list[memb_failed_list_entries++].s_addr =
		memb_form_token.rep_list[memb_form_token.rep_list_entries].s_addr;

	memb_state_gather_enter ();
}

void orf_timer_function_token_timeout (void *data)
{
	switch (memb_state) {
	case MEMB_STATE_OPERATIONAL:
		log_printf (LOG_LEVEL_WARNING, "Token loss in OPERATIONAL.\n");
		memb_conf_id.rep.s_addr = memb_local_sockaddr_in.sin_addr.s_addr;
		memb_list_entries = 1;

		memb_state_gather_enter ();
		break;

	case MEMB_STATE_GATHER:
	case MEMB_STATE_COMMIT:
		log_printf (LOG_LEVEL_WARNING, "Token loss in GATHER or COMMIT.\n");
		memb_list_entries = 1;
		break;

	case MEMB_STATE_EVS:
		log_printf (LOG_LEVEL_WARNING, "Token loss in EVS state\n");
		memb_list_entries = 1;
		memb_state_gather_enter ();
		break;

	default:
		printf ("token loss in form state doesn't make sense here\n");
		break;
	}
}

/*
 * Send orf_token to next member (requires orf_token)
 */
static int orf_token_send (
	struct orf_token *orf_token)
{
	struct msghdr msg_orf_token;
	struct iovec iovec_orf_token;
	int res;

	poll_timer_delete (*gmi_poll_handle, timer_orf_token_timeout);

	poll_timer_add (*gmi_poll_handle, TIMEOUT_TOKEN, 0,
		orf_timer_function_token_timeout, &timer_orf_token_timeout);

	iovec_orf_token.iov_base = orf_token;
	iovec_orf_token.iov_len = sizeof (struct orf_token);

	msg_orf_token.msg_name = &memb_next;
	msg_orf_token.msg_namelen = sizeof (struct sockaddr_in);
	msg_orf_token.msg_iov = &iovec_orf_token;
	msg_orf_token.msg_iovlen = 1;
	msg_orf_token.msg_control = 0;
	msg_orf_token.msg_controllen = 0;
	msg_orf_token.msg_flags = 0;
	
// THIS IS FOR TESTING ERRORS IN THE EVS STATE
//if ((memb_state == MEMB_STATE_EVS) && ((random () % 3) == 0)) {
//log_printf (LOG_LEVEL_DEBUG, "CAUSING TOKEN LOSS AT EVS STATE\n");
//	return (1);
//}

	res = sendmsg (gmi_fd, &msg_orf_token, MSG_NOSIGNAL);
	assert (res != -1);
	
	/*
	 * res not used here errors are handled by algorithm
	 */
	gmi_last_seqid = orf_token->header.seqid;
	stats_sent += res;

	return (res);
}

int orf_token_send_initial (void)
{
	struct orf_token orf_token;
	int res;

	orf_token.header.seqid = 0;
	orf_token.header.type = MESSAGE_TYPE_ORF_TOKEN;
	orf_token.group_arut = gmi_highest_seq;
	orf_token.addr_arut.s_addr = this_ip.sin_addr.s_addr;
	orf_token.fcc = 0;

	orf_token.rtr_list_entries = 0;
	memset (orf_token.rtr_list, 0, sizeof (struct rtr_item) * RTR_TOKEN_SIZE_MAX);

	res = orf_token_send (&orf_token);

	return (res);
}

/*
 * Membership Management
 */
static int memb_join_send (void)
{
	struct msghdr msghdr_join;
	struct iovec iovec_join;
	int res;

	memb_join.header.seqid = 0;
	memb_join.header.type = MESSAGE_TYPE_MEMB_JOIN;
	/*
	 * copy current gather list to representatives list
	 */
	if ((memb_gather_set_entries == memb_join.active_rep_list_entries) &&
		(memcmp (memb_join.active_rep_list, memb_gather_set,
			sizeof (struct in_addr) * memb_gather_set_entries) == 0) &&
		(memb_failed_list_entries == memb_join.failed_rep_list_entries) &&
		(memcmp (memb_join.failed_rep_list, memb_failed_list,
			sizeof (struct in_addr) * memb_failed_list_entries) == 0)) {

		return (0);
	}
	
	/*
	 * Copy active reps
	 */
	memcpy (memb_join.active_rep_list, memb_gather_set,
		sizeof (struct in_addr) * memb_gather_set_entries);
	memb_join.active_rep_list_entries = memb_gather_set_entries;

	/*
	 * Copy failed reps
	 */
	memcpy (memb_join.failed_rep_list, memb_failed_list,
		sizeof (struct in_addr) * memb_failed_list_entries);
	memb_join.failed_rep_list_entries = memb_failed_list_entries;

	iovec_join.iov_base = &memb_join;
	iovec_join.iov_len = sizeof (struct memb_join);

	msghdr_join.msg_name = &sockaddr_in_mcast;
	msghdr_join.msg_namelen = sizeof (struct sockaddr_in);
	msghdr_join.msg_iov = &iovec_join;
	msghdr_join.msg_iovlen = 1;
	msghdr_join.msg_control = 0;
	msghdr_join.msg_controllen = 0;
	msghdr_join.msg_flags = 0;

	res = sendmsg (gmi_fd, &msghdr_join, MSG_NOSIGNAL | MSG_DONTWAIT);

	return (res);
}

static int memb_state_commit_enter (void);

/*
 * Update gather_set[0].join_reps with list of failed members
 */
void memb_gather_set_update_failed (struct in_addr *list, int list_entries)
{
	int i;
	int j;

	/*
	 * Remove failed members from gather set
	 */
	for (i = 0; i < list_entries; i++) {
		for (j = 0; j < memb_gather_set_entries; j++) {
			if (list[i].s_addr == memb_gather_set[j].s_addr) {
				memb_gather_set_entries -= 1;
				memcpy (&memb_gather_set[j],
					&memb_gather_set[j + 1],
					memb_gather_set_entries * sizeof (struct in_addr));
				break; /* for j = 0 */
			}
		}
	}
}

static void memb_timer_function_state_commit_timeout (void *data)
{
	int i;
	int j;
	int k;
	int found;
	int add_to_failed = 1;
	struct sockaddr_in left_list[MAX_MEMBERS];
	int left_list_entries = 0;

	memb_failed_list_entries = 0;

	/*
	 * No entries responded in commit timeout period
	 */
	if (memb_commit_set_entries == 0) {
		/*
		 * memb_list_entries only set to 0 when token times out, in which case
		 * send a configuration change because no messages can be recovered in EVS
		 */
		if (memb_list_entries == 0) {
			log_printf (LOG_LEVEL_NOTICE, "I am the only member.\n");
			if (gmi_confchg_fn) {
				/*
				 * Determine nodes that left the configuration
				 */
				for (i = 0; i < memb_list_entries_confchg; i++) {
					if (memb_local_sockaddr_in.sin_addr.s_addr != memb_list[i].sin_addr.s_addr) {
						left_list[left_list_entries].sin_addr.s_addr = memb_list[i].sin_addr.s_addr;
						left_list[left_list_entries].sin_family = AF_INET;
						left_list[left_list_entries].sin_port = sockaddr_in_mcast.sin_port;
						left_list_entries += 1;
						
					}
				}

				gmi_confchg_fn (&memb_local_sockaddr_in, 1,
					left_list, left_list_entries,
					0, 0);
			}
		} else {
			log_printf (LOG_LEVEL_NOTICE, "No members sent join, keeping old ring and transitioning to operational.\n");
		}
		memb_state = MEMB_STATE_OPERATIONAL;
		return;
	}
	/*
	 * Find all failed members
	 */
	for (i = 0; i < memb_gather_set_entries; i++) {
		add_to_failed = 1;
		for (j = 0; j < memb_commit_set_entries; j++) {
			/*
			 * If gather entry not in commit rep list, add to failed
			 */
			if (memb_gather_set[i].s_addr == memb_commit_set[j].rep.sin_addr.s_addr) {
				add_to_failed = 0;
				break; /* for found = 0 */
			}
		}
		/*
		 * If gather entry not in commit set, add to failed set
		 */
		for (found = 0, j = 0; j < memb_commit_set_entries; j++) {
			for (k = 0; k < memb_commit_set[j].join_rep_list_entries; k++) {
				if (memb_gather_set[i].s_addr == memb_commit_set[j].join_rep_list[k].s_addr) {
					found = 1;
					break;
				}
			}
			if (found == 0) {
				add_to_failed = 1;
				break;
			}
		}

		/*
		 * If local address, item found
		 */
		if (memb_gather_set[i].s_addr == memb_local_sockaddr_in.sin_addr.s_addr) {
			add_to_failed = 0;
		}

		if (add_to_failed == 1) {
			memb_failed_list[memb_failed_list_entries++].s_addr =
				memb_gather_set[i].s_addr;
		}
	}

	memb_gather_set_update_failed (memb_failed_list, memb_failed_list_entries);

	memb_state_commit_enter ();
}

static int memb_state_commit_enter (void)
{
	int res;

	memb_state = MEMB_STATE_COMMIT;
	memb_commit_set_entries = 0;
	res = memb_join_send();

    poll_timer_delete (*gmi_poll_handle, timer_memb_state_gather_timeout);

	timer_memb_state_gather_timeout = 0;

	poll_timer_add (*gmi_poll_handle, TIMEOUT_STATE_COMMIT, 0,
		memb_timer_function_state_commit_timeout, &timer_memb_state_commit_timeout);

	return (res);
}

static void memb_timer_function_state_gather (void *data)
{
	int i;
	/*
	 * GATHER period expired, sort gather sets and send JOIN
	 */
	memb_state_commit_enter ();
	log_printf (LOG_LEVEL_DEBUG, "GATHER timeout:\n");
	for (i = 0; i < memb_gather_set_entries; i++) {
		log_printf (LOG_LEVEL_DEBUG, "host %d attempted to join %s\n", i, inet_ntoa (memb_gather_set[i]));
	}
}

static void memb_print_commit_set (void)
{
	int i, j;

	log_printf (LOG_LEVEL_DEBUG, "Gather list\n");
	for (i = 0; i < memb_gather_set_entries; i++) {
		log_printf (LOG_LEVEL_DEBUG, "\tmember %d %s\n", i, inet_ntoa (memb_gather_set[i]));
	}
	for (i = 0; i < memb_commit_set_entries; i++) {
		log_printf (LOG_LEVEL_DEBUG, "Join from rep %d %s\n", i, inet_ntoa (memb_commit_set[i].rep.sin_addr));
		for (j = 0; j < memb_commit_set[i].join_rep_list_entries; j++) {
			log_printf (LOG_LEVEL_DEBUG, "\tmember %d %s\n", j, inet_ntoa (memb_commit_set[i].join_rep_list[j]));
		}
	}
}

/*
 * Determine if the commit phase has reached consensus
 */
static int memb_state_consensus_commit (void)
{
	int found;
	int res;
	int i, j;

	/*
	 * Determine consensus
	 */

	/*
	 * If all commit sets don't match gather set, no consensus
	 */
	for (i = 0; i < memb_commit_set_entries; i++) {
		/*
		 * If not same number of entries, no consensus
		 */
		res = memb_gather_set_entries - memb_commit_set[i].join_rep_list_entries;
		if (res != 0) {
			return (0); /* no consensus */
		}

		/*
		 * If entries dont match, no consensus
		 */
		res = memcmp (memb_gather_set, memb_commit_set[i].join_rep_list,
			memb_gather_set_entries * sizeof (struct in_addr));
		
		if (res != 0) {
			return (0); /* no consensus */
		}
	}

	/*
	 * If all reps from gather set represented in commit set, consensus
	 */
	for (i = 0; i < memb_gather_set_entries; i++) {
		found = 0;
		for (j = 0; j < memb_commit_set_entries; j++) {
			if (memb_gather_set[i].s_addr == memb_local_sockaddr_in.sin_addr.s_addr) {
				found = 1;
				break;
			}
			if (memb_gather_set[i].s_addr == memb_commit_set[j].rep.sin_addr.s_addr) {
				found = 1;
				break;
			}
		}

		if (found == 0) {
			return (0); /* no consensus, rep not found from gather set */
		}
	}

	return (1); /* got consensus! */
}

/*
 * Union commit_set_entry into gather set
 */
static void memb_state_commit_union (int commit_set_entry)
{
int found;
int i, j;

	for (i = 0; i < memb_commit_set[commit_set_entry].join_rep_list_entries; i++) {
		for (found = 0, j = 0; j < memb_gather_set_entries; j++) {
			if (memb_commit_set[commit_set_entry].join_rep_list[i].s_addr ==
				memb_gather_set[j].s_addr) {
				found = 1;
				break;
			}
		}

		if (found == 0) {
			memb_gather_set[memb_gather_set_entries++].s_addr =
				memb_commit_set[commit_set_entry].join_rep_list[i].s_addr;
			/*
			 * Sort gather set
			 */
			qsort (memb_gather_set, memb_gather_set_entries,
				sizeof (struct in_addr), in_addr_compare);
		}
	}
}

static void memb_conf_id_build (
	struct memb_conf_id *memb_conf_id,
	struct in_addr memb_local_rep)
{
	gettimeofday (&memb_conf_id->tv, NULL);
	memb_conf_id->rep.s_addr = memb_local_rep.s_addr;
}

static void memb_form_token_update_highest_seq (
	struct memb_form_token *form_token)
{
	struct conf_desc *conf_desc;
	int entry;
	int found = 0;

	for (entry = 0; entry < form_token->conf_desc_list_entries; entry++) {
		if (memcmp (&form_token->conf_desc_list[entry].conf_id,
			&memb_form_token_conf_id, sizeof (struct memb_conf_id)) == 0) {

			found = 1;
			break;
		}
	}
	conf_desc = &form_token->conf_desc_list[entry];
	if (found && gmi_highest_seq < conf_desc->highest_seq) {
		gmi_highest_seq = conf_desc->highest_seq;
	}
}

static void memb_form_token_conf_desc_build (
	struct memb_form_token *form_token)
{
	struct conf_desc *conf_desc;
	int found = 0;
	int entry = 0;

	/*
	 * Determine if local configuration id is already present in form token
	 */
	for (entry = 0; entry < form_token->conf_desc_list_entries; entry++) {
		if (memcmp (&form_token->conf_desc_list[entry].conf_id,
			&memb_form_token_conf_id, sizeof (struct memb_conf_id)) == 0) {

			found = 1;
			break;
		}
	}
	conf_desc = &form_token->conf_desc_list[entry];

	if (found == 0) {
		/*
		 * Item not present, add item
		 */
		conf_desc->highest_seq = gmi_highest_seq;
		conf_desc->arut = gmi_arut;
// TODO holes not currently implemented conf_desc->hole_list_entries = 0;
		memcpy (&conf_desc->conf_id,
			&memb_form_token_conf_id, sizeof (struct memb_conf_id));

		form_token->conf_desc_list_entries += 1;
	} else {
		/*
		 * Item already present, update arut, highest seq
		 */
		if (conf_desc->arut > gmi_arut) {
			conf_desc->arut = gmi_arut;
		}
		if (gmi_highest_seq > conf_desc->highest_seq) {
			conf_desc->highest_seq = gmi_highest_seq;
		}
	}
	
#ifdef COMPILE_OUT
	/*
	 * Build conf_desc->hole_list
	 */
printf ("conf desc build %d %d\n", gmi_arut, gmi_highest_seq);
	conf_desc->hole_list_entries = 0;
	for (i = gmi_arut; i < gmi_highest_seq; i++) {
		assert (conf_desc->hole_list_entries < HOLE_LIST_MAX);
		res = sq_item_get (&queue_rtr_items, i, (void **)&gmi_rtr_item_p);
		if (res == 0) {
			/*
			 * If item present, delete from hole list if it exists
			 */
			for (j = 0; j < conf_desc->hole_list_entries; j++) {
				if (conf_desc->hole_list[j] == i) {
					memmove (&conf_desc->hole_list[j], &conf_desc->hole_list[j + 1],
						sizeof (int) * (conf_desc->hole_list_entries - j - 1));
					conf_desc->hole_list_entries -= 1;
printf ("reducing setting desc entries to %d\n", conf_desc->hole_list_entries);
					break; /* from for (j = ... ) */
				}
			}
		} else {
			/*
			 * If item not present, add to hole list
			 */
			conf_desc->hole_list[conf_desc->hole_list_entries] = i;
			conf_desc->hole_list_entries += 1;
printf ("increasing setting desc entries to %d %d\n", conf_desc->hole_list_entries, i);
		}
	}
printf ("Conf desc build done\n");
#endif
}

static int memb_form_token_send (
	struct memb_form_token *form_token)
{
	struct msghdr msg_form_token;
	struct iovec iovec_form_token;
	int res;

	/*
	 * Build message for sendmsg
	 */
	iovec_form_token.iov_base = form_token;
	iovec_form_token.iov_len = sizeof (struct memb_form_token);

	msg_form_token.msg_name = &memb_next;
	msg_form_token.msg_namelen = sizeof (struct sockaddr_in);
	msg_form_token.msg_iov = &iovec_form_token;
	msg_form_token.msg_iovlen = 1;
	msg_form_token.msg_control = 0;
	msg_form_token.msg_controllen = 0;
	msg_form_token.msg_flags = 0;
	
	res = sendmsg (gmi_fd, &msg_form_token, MSG_NOSIGNAL | MSG_DONTWAIT);

	/*
	 * res not used here, because orf token errors are handled by algorithm
	 */
	stats_sent += res;

	poll_timer_delete (*gmi_poll_handle, timer_orf_token_timeout);
	timer_orf_token_timeout = 0;

	poll_timer_delete (*gmi_poll_handle, timer_form_token_timeout);

	poll_timer_add (*gmi_poll_handle, TIMEOUT_TOKEN, 0,
		timer_function_form_token_timeout, &timer_form_token_timeout);

	return (res);
}

int memb_form_token_send_initial (void)
{
	struct memb_form_token form_token;
	int res;
	int i;

	memset (&form_token, 0x00, sizeof (struct memb_form_token));
	memb_state = MEMB_STATE_FORM;

	/*
	 * Build form token
	 */
	form_token.header.type = MESSAGE_TYPE_MEMB_FORM_TOKEN;
	memcpy (form_token.rep_list,
		memb_gather_set,
		memb_gather_set_entries * sizeof (struct in_addr));
	form_token.rep_list_entries = memb_gather_set_entries;

	/*
	 * Add local member to entry
	 */
	form_token.member_list[0].s_addr =
		memb_local_sockaddr_in.sin_addr.s_addr;
	form_token.member_list_entries = 1;

	memb_conf_id_build (&form_token.conf_id, memb_local_sockaddr_in.sin_addr);

	form_token.conf_desc_list_entries = 0;
	memb_form_token_conf_desc_build (&form_token);
	
	/*
	 * Send FORM to next member, or if no members in this configuration
	 * to next representative
	 */

	if (memb_list_entries <= 1) {
		memb_next.sin_addr.s_addr = memb_gather_set[1].s_addr;
	} else {
		for (i = 0; i < memb_list_entries; i++) {
			if (memb_list[i].sin_addr.s_addr == memb_local_sockaddr_in.sin_addr.s_addr) {
				memb_next.sin_addr.s_addr =
					memb_list[i + 1].sin_addr.s_addr;
				break;
			}
		}
	}

// TODO assertion here about the 1 value
	memb_next.sin_family = AF_INET;
	memb_next.sin_port = sockaddr_in_mcast.sin_port;

	res = memb_form_token_send (&form_token);

	return (res);
}

void print_stats (void)
{
	struct timeval tv_end;
	gettimeofday (&tv_end, NULL);
	
	log_printf (LOG_LEVEL_NOTICE, "Bytes recv %d\n", stats_recv);
	log_printf (LOG_LEVEL_NOTICE, "Bytes sent %d\n", stats_sent);
	log_printf (LOG_LEVEL_NOTICE, "Messages delivered %d\n", stats_delv);
	log_printf (LOG_LEVEL_NOTICE, "Re-Mcasts %d\n", stats_remcasts);
	log_printf (LOG_LEVEL_NOTICE, "Tokens process %d\n", stats_orf_token);
}

/*
 * Authenticates message using nonce, mac, and message body
 */
static int gmi_msg_auth (struct iovec *iovec, int iov_len)
{
	return (0);
}


int last_lowered = 1;

static void calculate_group_arut (struct orf_token *orf_token)
{
//printf ("group arut %d local arut %d gmi_gmi_highest seq %d\n", orf_token->group_arut, gmi_arut, gmi_highest_seq);
//printf ("last %d group arut %d last arut %d arut %d\n", last_lowered, orf_token->group_arut, last_group_arut, gmi_arut);

	/*
	 * increase the group arut if we got back the same group
	 * because everyone has these messages
	 */
	messages_free (orf_token->group_arut);
	if (orf_token->addr_arut.s_addr == this_ip.sin_addr.s_addr) {
		orf_token->group_arut = gmi_arut;
	}

	if (gmi_arut < orf_token->group_arut) {
		orf_token->group_arut = gmi_arut;
		orf_token->addr_arut.s_addr = this_ip.sin_addr.s_addr;
	}
	last_group_arut = orf_token->group_arut;
}

/*
 * Message Handlers
 */

/*
 * message handler called when TOKEN message type received
 */
static int message_handler_orf_token (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received)
{
	struct orf_token *orf_token;
	int transmits_allowed;
	int starting_group_arut;

	orf_token = iovec[0].iov_base;

#ifdef PRINT_STATS
	if (orf_token->header.seqid > 10000) {
		print_stats ();
	}
#endif

	if (memb_state == MEMB_STATE_FORM) {
		log_printf (LOG_LEVEL_NOTICE, "swallowing ORF token %d.\n", stats_orf_token);
		poll_timer_delete (*gmi_poll_handle, timer_orf_token_timeout);
		timer_orf_token_timeout = 0;
		return (0);
	}

//printf ("Got orf token from %s\n", inet_ntoa (system_from->sin_addr));
	starting_group_arut = orf_token->group_arut;
	stats_orf_token++;
	
	transmits_allowed = orf_fcc_allowed (orf_token);

//printf ("retransmit allowed %d\n", transmits_allowed);
	/*
	 * Retransmit failed messages and request retransmissions
	 */

	orf_token_rtr (orf_token, &transmits_allowed);
//printf ("multicasts allowed %d\n", transmits_allowed);

	/*
	 * TODO Ok this is ugly and I dont like it.
	 *
	 * Flow control to limit number of missing multicast messages
	 * on lossy switches, this could cause a large window between
	 * what is delivered locally and what is delivered remotely.
	 * This window could cause the hole list of the form token to
	 * be overrun or cause the form token to be large.
	 */

	if ((gmi_brake + MISSING_MCAST_WINDOW) < orf_token->header.seqid) {
		transmits_allowed = 0;
	}

	/*
	 * Set the group arut and free any messages that can be freed
	 */
	if (memb_state != MEMB_STATE_EVS) {
		calculate_group_arut (orf_token);
	}

	/*
	 * Multicast queued messages
	 */
	orf_token_mcast (orf_token, transmits_allowed, system_from);

	/*
	 * Calculate flow control count
	 */
	orf_token_fcc (orf_token);

	/*
	 * Deliver membership and messages required by EVS
	 */
	orf_token_evs (orf_token, starting_group_arut);

	if (memb_state == MEMB_STATE_EVS) {
		calculate_group_arut (orf_token);
	}

	/*
	 * Transmit orf_token to next member
	 */
	orf_token_send (orf_token);

	return (0);
}

static int memb_state_gather_enter (void) {
	struct msghdr msghdr_attempt_join;
	struct iovec iovec_attempt_join;
	struct memb_attempt_join memb_attempt_join;
	int res = 0;

	log_printf (LOG_LEVEL_NOTICE, "entering GATHER state.\n");
	memb_state = MEMB_STATE_GATHER;

	/*
	 * Join message starts with no entries
	 */
	memb_join.active_rep_list_entries = 0;
	memb_join.failed_rep_list_entries = 0;

	/*
	 * Copy local host info
	 */
	memb_gather_set[0].s_addr = memb_local_sockaddr_in.sin_addr.s_addr;
	memb_gather_set_entries = 1;

	/*
	 * If this node is the representative, send attempt join
	 */
	if (memb_local_sockaddr_in.sin_addr.s_addr == memb_conf_id.rep.s_addr) {
		log_printf (LOG_LEVEL_NOTICE, "SENDING attempt join because this node is ring rep.\n");
		memb_attempt_join.header.seqid = 0;
		memb_attempt_join.header.type = MESSAGE_TYPE_MEMB_ATTEMPT_JOIN;
		
		iovec_attempt_join.iov_base = &memb_attempt_join;
		iovec_attempt_join.iov_len = sizeof (struct memb_attempt_join);

		msghdr_attempt_join.msg_name = &sockaddr_in_mcast;
		msghdr_attempt_join.msg_namelen = sizeof (struct sockaddr_in);
		msghdr_attempt_join.msg_iov = &iovec_attempt_join;
		msghdr_attempt_join.msg_iovlen = 1;
		msghdr_attempt_join.msg_control = 0;
		msghdr_attempt_join.msg_controllen = 0;
		msghdr_attempt_join.msg_flags = 0;

		res = sendmsg (gmi_fd, &msghdr_attempt_join, MSG_NOSIGNAL | MSG_DONTWAIT);
		/*
		 * res not checked here, there is nothing that can be done
		 * instead rely on the algorithm to recover from faults
		 */
	}

	poll_timer_delete (*gmi_poll_handle, timer_memb_state_gather_timeout);

	poll_timer_add (*gmi_poll_handle, TIMEOUT_STATE_GATHER, 0,
		memb_timer_function_state_gather, &timer_memb_state_gather_timeout);

	return (res);
}

struct pend_delv *pend_delv_next_delivery_find (void)
{
	struct pend_delv *pend_delv = 0;
	int i;

	/*
	 * Find first_delivery queue that is not mepty
	 * this sets the first pend_delv
	 */
	for (i = 0; i < memb_list_entries_confchg; i++) {
		if (queues_pend_delv[i].first_delivery && 
			queue_is_empty (&queues_pend_delv[i].queue) == 0) {

			pend_delv = &queues_pend_delv[i];
//			printf ("Selecting first queue %s\n", inet_ntoa (pend_delv->ip));
			break;
		}
	}

	/*
	 * Search remaining pend_delv for first deliveries with
	 * smaller sequence numbers
	 */
	for (++i; i < memb_list_entries_confchg; i++) {
		assert (pend_delv);
		if (queues_pend_delv[i].first_delivery &&
			(queue_is_empty (&queues_pend_delv[i].queue) == 0) &&
			(queues_pend_delv[i].seqid < pend_delv->seqid)) {

			pend_delv = &queues_pend_delv[i];
//			printf ("Selecting first queue %s\n", inet_ntoa (pend_delv->ip));
		}
	}
		
	/*
	 * Found first_delivery queue that wasn't empty, return it
	 */
	if (pend_delv) {
		return (pend_delv);
	}
	/*
	 * No first delivery queues, repeat same
	 * process looking for any queue
	 */
	for (i = 0; i < memb_list_entries_confchg; i++) {
		if (queue_is_empty (&queues_pend_delv[i].queue) == 0) {
			pend_delv = &queues_pend_delv[i];
			break;
		}
	}

	/*
	 * Find lowest sequence number queue
	 */
	for (++i; i < memb_list_entries_confchg; i++) {
		assert (pend_delv);
		if ((queue_is_empty (&queues_pend_delv[i].queue) == 0) &&
			(queues_pend_delv[i].seqid < pend_delv->seqid)) {
			pend_delv = &queues_pend_delv[i];
		}
	}

	return (pend_delv);
}

static int user_deliver ()
{
	struct gmi_pend_delv_item *pend_delv_item;
	int i = 0;
	int res = 0;
	struct iovec iovec_delv[256];
	int iov_len_delv = 0;
	struct mcast *mcast = 0;
	int messages_delivered = 0;
	struct pend_delv *pend_delv;
	int retval = 0;

	/*
	 * Find pend_delv with lowest sequence number.  This pend_delv is
	 * the queue that should be delivered from next
	 */
	pend_delv = pend_delv_next_delivery_find ();
//printf ("Delivering from queue %s\n", inet_ntoa (pend_delv->ip));

	/*
	 * If a message was not assembled on the queue with the lowest
	 * sequence number, return since there is no reason to attempt assembly.
	 */
	memset (iovec_delv, 0, sizeof (iovec_delv));
	queue_item_iterator_init (&pend_delv->queue);
	assert (queue_is_empty (&pend_delv->queue) == 0);
//printf ("Starting a packet assembly\n");
	do {
		pend_delv_item = queue_item_iterator_get (&pend_delv->queue);
		mcast = pend_delv_item->iovec[0].iov_base;

		assert (pend_delv_item);
		assert (pend_delv_item->iovec[0].iov_len < MESSAGE_SIZE_MAX);
		assert (pend_delv_item->iovec[0].iov_len != 0);
		assert (pend_delv_item->iovec[0].iov_base != 0);
		assert (mcast != (struct mcast *)0xdeadbeef);
		assert (pend_delv->ip.s_addr == mcast->source.s_addr);

		messages_delivered += 1;

		/*
		 * Assemble io vector
		 */
		if (pend_delv_item->iovec[0].iov_len == sizeof (struct mcast)) {
			/*
			 * Copy iovec from second iovec if this is self-delivered
			 */
			memcpy (&iovec_delv[iov_len_delv],
				&pend_delv_item->iovec[1],
				sizeof (struct iovec) * pend_delv_item->iov_len - 1);
			iov_len_delv += pend_delv_item->iov_len - 1;
		} else {
			/*
			 * Copy iovec from first iovec if this is an external message
			 */
			iovec_delv[iov_len_delv].iov_base =
				pend_delv_item->iovec[0].iov_base + sizeof (struct mcast);
			iovec_delv[iov_len_delv].iov_len =
				pend_delv_item->iovec[0].iov_len - sizeof (struct mcast);
			assert (iovec_delv[iov_len_delv].iov_len < MESSAGE_SIZE_MAX);
			iov_len_delv += 1;
			if (pend_delv_item->iov_len > 1) {
				memcpy (&iovec_delv[iov_len_delv],
					&pend_delv_item->iovec[1],
					sizeof (struct iovec) * pend_delv_item->iov_len - 1);
				iov_len_delv += pend_delv_item->iov_len - 1;
			}
		}

		assert (iov_len_delv < 256);
		assert (iov_len_delv > 0);

//printf ("Assembling from packet %d of %d of total %d\n",
//	mcast->packet_number, mcast->packet_count, mcast->packet_seq);
		/*
		 * Deliver message if this is the last packet
		 */
		if (mcast->packet_number == mcast->packet_count) {
			log_printf (LOG_LEVEL_DEBUG, "Last packet, delivering iovec %d entries seq %d\n",
				iov_len_delv, i);

			gmi_deliver_fn (
				&mcast->groupname,
				iovec_delv,
				iov_len_delv);

			/*
			 * On the first message delivery:
			 * Free items in the pending queue up to the barrier message
			 * set gmi_adut to rut so that message_free may free any messages.
			 */
			if (pend_delv->first_delivery) {
//				printf ("releasing all messages up to %d\n", gmi_adut);
// TODO actually release the messages from the previous configuration
// TODO without a fix here, those messages are leaked
			}

			/*
			 * Because of the ordering guarantees, we are guaranteed that 
			 * pend_delv->seqid on every invocation of user_deliver shall
			 * increase (or reset to zero).  This allows us to set the
			 * low water mark (gmi_adut) for freeing of messages to atleast
			 * the beginning of this message.
			 */ 
			gmi_adut = pend_delv->seqid;

			/*
			 * Determine if there are more messages on this queue
			 */
			res = queue_item_iterator_next (&pend_delv->queue);
			if (res == 0) {
				/*
				 * More items to deliver set queues seqid head so
				 * correct pending queue can be selected next time
				 */
				pend_delv_item = queue_item_iterator_get (&pend_delv->queue);
				mcast = pend_delv_item->iovec[0].iov_base;
				pend_delv->seqid = mcast->header.seqid;
				for (i = 0; i < messages_delivered; i++) {
					queue_item_remove (&pend_delv->queue);
				}
			} else {
				/*
				 * No more items to deliver
				 */
				pend_delv->seqid = 0;
				queue_reinit (&pend_delv->queue);
			}
			
			retval = 1;
			break; /* From do loop */
		}

		res = queue_item_iterator_next (&pend_delv->queue);
	} while (res == 0);
	return (retval);
}

struct pend_delv *pend_delv_find (struct in_addr source)
{
	struct pend_delv *pend_delv = 0;
	int i;

	for (i = 0; i < memb_list_entries_confchg; i++) {
		if (source.s_addr == queues_pend_delv[i].ip.s_addr) {
			pend_delv = &queues_pend_delv[i];
			break;
		}
	}

	return (pend_delv);
}

static int delivery_outstanding = 0;

static void pending_queues_deliver (void)
{
	struct gmi_rtr_item *gmi_rtr_item_p;
	int i;
	int res;
	struct mcast *mcast;
	struct gmi_pend_delv_item pend_delv_item;
	struct pend_delv *pend_delv;
	int delivered;

//printf ("Delivering messages to pending queues\n");
	/*
	 * Deliver messages in order from rtr queue to pending delivery queue
	 */
	for (i = gmi_arut + 1; i <= gmi_highest_seq; i++) {
		res = sq_item_get (&queue_rtr_items, i, (void **)&gmi_rtr_item_p);
		/*
		 * If hole, stop assembly
		 */
		if (res != 0) {
			break;
		}
		assert (gmi_rtr_item_p->iovec[0].iov_len < MESSAGE_SIZE_MAX);
		mcast = gmi_rtr_item_p->iovec[0].iov_base;
		if (mcast == (struct mcast *)0xdeadbeef) {
			printf ("seqid %d\n", gmi_rtr_item_p->iovec[0].iov_len);
		}
		assert (mcast != (struct mcast *)0xdeadbeef);

		/*
		 * Message found
		 */
		log_printf (LOG_LEVEL_DEBUG,
			"Delivering MCAST message with seqid %d to pending delivery queue\n",
			mcast->header.seqid);

//printf ("Delivering MCAST from packet %d of %d of total %d seqid %d\n", mcast->packet_number, mcast->packet_count, mcast->packet_seq, mcast->header.seqid);
		gmi_arut = i;

		/*
		 * Create pending delivery item
		 */
		pend_delv_item.iov_len = gmi_rtr_item_p->iov_len;
		memcpy (&pend_delv_item.iovec, gmi_rtr_item_p->iovec,
			sizeof (struct iovec) * gmi_rtr_item_p->iov_len);
		assert (gmi_rtr_item_p->iov_len < MAXIOVS);

		assert (mcast->source.s_addr != 0);
		pend_delv = pend_delv_find (mcast->source);
		assert (pend_delv != 0);
		assert (pend_delv->ip.s_addr != 0);

		if (mcast->packet_number == 0) {
			pend_delv->seqid = mcast->header.seqid;
		}

		/*
		 * Add pending delivery item to pending delivery queue
		 */
		queue_item_add (&pend_delv->queue, &pend_delv_item);

		/*
		 * If message is complete, attempt delivery of all messages
		 * that are currently outstanding
		 */
		if (mcast->packet_number == mcast->packet_count) {
//printf ("Starting delivery\n");
			delivery_outstanding += 1;
			do {
				delivered = user_deliver ();
				if (delivered) {
					delivery_outstanding -= 1;
				}
			} while (delivery_outstanding && delivered);
		}
	}
//printf ("Done delivering messages to pending queues\n");
}

/*
 * recv message handler called when MCAST message type received
 */
static int message_handler_mcast (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received)
{
	struct gmi_rtr_item gmi_rtr_item;
	struct mcast *mcast;

	mcast = iovec[0].iov_base;

	/*
	 * Ignore multicasts for other configurations
	 * TODO shouldn't we enter gather here?
	 */
	if (memcmp (&mcast->memb_conf_id,
		&memb_form_token_conf_id, sizeof (struct memb_conf_id)) != 0) {

		return (0);
	}

	/*
	 * Add mcast message to rtr queue if not already in rtr queue
	 * otherwise free io vectors
	 */
	if (bytes_received > 0 && bytes_received < MESSAGE_SIZE_MAX &&
		sq_item_inuse (&queue_rtr_items, mcast->header.seqid) == 0) {

		/*
		 * Allocate new multicast memory block
		 * TODO we need to free this somewhere
		 */
		gmi_rtr_item.iovec[0].iov_base = malloc (bytes_received);
		if (gmi_rtr_item.iovec[0].iov_base == 0) {
			return (-1); /* error here is corrected by the algorithm */
		}
		memcpy (gmi_rtr_item.iovec[0].iov_base, mcast, bytes_received);
		gmi_rtr_item.iovec[0].iov_len = bytes_received;
		assert (gmi_rtr_item.iovec[0].iov_len > 0);
		assert (gmi_rtr_item.iovec[0].iov_len < MESSAGE_SIZE_MAX);
		gmi_rtr_item.iov_len = 1;
		
		if (mcast->header.seqid > gmi_highest_seq) {
			gmi_highest_seq = mcast->header.seqid;
		}

		sq_item_add (&queue_rtr_items, &gmi_rtr_item, mcast->header.seqid);
	}

	pending_queues_deliver ();

	return (0);
}

static int message_handler_memb_attempt_join (
	struct sockaddr_in *system_from,
	struct iovec *iov,
	int iov_len,
	int bytes_received)
{
	int token_lost;
	int found;
	int i;

	log_printf (LOG_LEVEL_NOTICE, "Got attempt join from %s\n", inet_ntoa (system_from->sin_addr));

	for (token_lost = 0, i = 0; i < memb_list_entries; i++) {
		if (memb_list[i].sin_addr.s_addr == system_from->sin_addr.s_addr &&
			memb_conf_id.rep.s_addr != system_from->sin_addr.s_addr) {
			log_printf (LOG_LEVEL_DEBUG, "ATTEMPT JOIN, token lost, taking attempt join msg.\n");
			poll_timer_delete (*gmi_poll_handle, timer_orf_token_timeout);
			timer_orf_token_timeout = 0;
			memb_conf_id.rep.s_addr = memb_local_sockaddr_in.sin_addr.s_addr;
			token_lost = 1;
			break;
		}
	}
	
	/*
	 * Not representative
	 */
	if (token_lost == 0 &&
		memb_conf_id.rep.s_addr != memb_local_sockaddr_in.sin_addr.s_addr) {

		log_printf (LOG_LEVEL_NOTICE, "not the rep for this ring, not handling attempt join.\n");
		return (0);
	}

	switch (memb_state) {
		case MEMB_STATE_OPERATIONAL:
		case MEMB_STATE_COMMIT:
			memb_state_gather_enter ();
			/*
			 * Do NOT place break here, immediately execute gather attempt join
			 */

		case MEMB_STATE_GATHER:
			log_printf (LOG_LEVEL_DEBUG, "ATTEMPT JOIN: state gather\n");
			for (found = 0, i = 0; i < memb_gather_set_entries; i++) {
				if (memb_gather_set[i].s_addr == system_from->sin_addr.s_addr) {
					found = 1;
				}
			}

			if (found == 0) {
				memb_gather_set[memb_gather_set_entries++].s_addr = system_from->sin_addr.s_addr;
				/*
				 * Sort gather set
				 */
				qsort (memb_gather_set, memb_gather_set_entries,
					sizeof (struct in_addr), in_addr_compare);

			}
			break;

		default:
			// TODO what about other states
			log_printf (LOG_LEVEL_ERROR, "memb_attempt_join: EVS or FORM state attempt join occured %d\n", memb_state);
	}

	return (0);
}

static int message_handler_memb_join (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received)
{
	struct memb_join *memb_join;
	int commit_entry;
	int found;
	int consensus;

	/*
	 * Not representative
	 */
	if (memb_conf_id.rep.s_addr != memb_local_sockaddr_in.sin_addr.s_addr) {
		log_printf (LOG_LEVEL_DEBUG, "not the rep for this ring, not handling join.\n");
		return (0);
	}

	switch (memb_state) {
		case MEMB_STATE_OPERATIONAL:
		case MEMB_STATE_GATHER:
			memb_state_commit_enter ();
			/*
			 * do not place break in this case, immediately enter COMMIT state
			 */

		case MEMB_STATE_COMMIT:
			log_printf (LOG_LEVEL_DEBUG, "JOIN in commit\n");
			memb_join = (struct memb_join *)iovec[0].iov_base;
			/*
			 * Find gather set that matches the system message was from	
			 */
			for (found = 0, commit_entry = 0; commit_entry < memb_commit_set_entries; commit_entry++) {
				if (system_from->sin_addr.s_addr == memb_commit_set[commit_entry].rep.sin_addr.s_addr) {
					found = 1;
					break;
				}
			}

			/*
			 * Add system from to commit sets if not currently in commit set
			 */
			if (found == 0) {
				memcpy (&memb_commit_set[commit_entry].rep, system_from, sizeof (struct sockaddr_in));
				memb_commit_set_entries++;
			}

			/*
			 * Set gather join data
			 */
			memcpy (memb_commit_set[commit_entry].join_rep_list, memb_join->active_rep_list,
				sizeof (struct in_addr) * memb_join->active_rep_list_entries);
			memb_commit_set[commit_entry].join_rep_list_entries = memb_join->active_rep_list_entries;

			/*
			 * Union all entries into the gather set (join_rep_list[0])
			 */
			memb_state_commit_union (commit_entry);

			/*
			 * Send JOIN message, but only if gather set has changed
			 */
			memb_join_send ();

			/*
			 * If consensus, transition to FORM
			 */
			memb_print_commit_set ();

			consensus = memb_state_consensus_commit ();
			if (consensus) {
				log_printf (LOG_LEVEL_NOTICE, "CONSENSUS reached!\n");
				if (memb_local_sockaddr_in.sin_addr.s_addr == memb_gather_set[0].s_addr) {
					log_printf (LOG_LEVEL_DEBUG, "This node responsible for sending the FORM token.\n");

					poll_timer_delete (*gmi_poll_handle, timer_memb_state_commit_timeout);
					timer_memb_state_commit_timeout = 0;

					memb_form_token_send_initial ();
				}
			}
			break;
		/*
		 * All other cases are ignored on JOINs
		 */
		case MEMB_STATE_FORM:
			log_printf (LOG_LEVEL_WARNING, "JOIN in form, ignoring since consensus reached in state machine.\n");
			break;

		default:
			// TODO HANDLE THIS CASE
			log_printf (LOG_LEVEL_DEBUG, "memb_join: DEFAULT case %d, shouldn't happen!!\n", memb_state);
			break;
	}

	return (0);
}

static int message_handler_memb_form_token (
	struct sockaddr_in *system_from,
	struct iovec *iovec,
	int iov_len,
	int bytes_received)
{
	int i;
	int local = 0;
	int res = 0;

printf ("Got membership form token\n");
	memcpy (&memb_form_token, iovec->iov_base, sizeof (struct memb_form_token));

	poll_timer_delete (*gmi_poll_handle, timer_form_token_timeout);
	timer_form_token_timeout = 0;


	switch (memb_state) {
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_COMMIT:
		memb_state = MEMB_STATE_FORM;
		poll_timer_delete (*gmi_poll_handle, timer_memb_state_commit_timeout);
		timer_memb_state_commit_timeout = 0;
		/*
		 * Add member to entry
		 */
		memb_form_token.member_list[memb_form_token.member_list_entries].s_addr =
			memb_local_sockaddr_in.sin_addr.s_addr;
		memb_form_token.member_list_entries++;
		/*
		 * Modify the conf_id as necessary
		 */
		memb_form_token_conf_desc_build (&memb_form_token);

		/*
		 * Stop token timeout timer from firing
		 * If we are in FORM state, a previous FORM state member
		 * may have captured the ORF token and swallowed it
		 */
		poll_timer_delete (*gmi_poll_handle, timer_orf_token_timeout);
		timer_orf_token_timeout = 0;
		/*
		 * Find next member
		 */
		for (i = 0; i < memb_list_entries; i++) {
			if (memb_list[i].sin_addr.s_addr == memb_local_sockaddr_in.sin_addr.s_addr) {
				local = 1;
				break;
			}
		}
	
		if (memb_list_entries == 0) { /* 0 or 1 members and we are local */
			local = 1;
		}
	
		if (local && (i + 1 < memb_list_entries)) {
			memb_next.sin_addr.s_addr = memb_list[i + 1].sin_addr.s_addr;
		} else {
			/*
			 * Find next representative
		 	 */
			for (i = 0; i < memb_form_token.rep_list_entries; i++) {
				if (memb_conf_id.rep.s_addr ==
					memb_form_token.rep_list[i].s_addr) {
					break;
				}
			}
			memb_next.sin_addr.s_addr =
				memb_form_token.rep_list[(i + 1) % memb_form_token.rep_list_entries].s_addr;
		}
		memb_next.sin_family = AF_INET;
		memb_next.sin_port = sockaddr_in_mcast.sin_port;
		break;

	case MEMB_STATE_FORM:
		memb_state = MEMB_STATE_EVS;
		memb_form_token_update_highest_seq (&memb_form_token);

		/*
		 * FORM token has rotated once, now install local variables
		 *
		 * Set barrier sequence number
		 * Set original arut
		 */
		gmi_barrier_seq = 0;
printf ("conf_desc_list %d\n", memb_form_token.conf_desc_list_entries);
		for (i = 0; i < memb_form_token.conf_desc_list_entries; i++) {
printf ("highest seq %d %d\n", i, memb_form_token.conf_desc_list[i].highest_seq);
			if (gmi_barrier_seq < memb_form_token.conf_desc_list[i].highest_seq) {
				gmi_barrier_seq = memb_form_token.conf_desc_list[i].highest_seq;
printf ("setting barrier seq to %d\n", gmi_barrier_seq);
			}
		}
		gmi_barrier_seq += 1;
printf ("setting barrier seq to %d\n", gmi_barrier_seq);
		gmi_original_arut = gmi_arut;

		/*
		 * Determine next ORF target
		 */
		for (i = 0; i < memb_form_token.member_list_entries; i++) {
			if (memb_local_sockaddr_in.sin_addr.s_addr == memb_form_token.member_list[i].s_addr) {
				memb_next.sin_addr.s_addr =
					memb_form_token.member_list[(i + 1) % memb_form_token.member_list_entries].s_addr;
				memb_next.sin_family = AF_INET;
				memb_next.sin_port = sockaddr_in_mcast.sin_port;
			}
//ABRA
		}
		break;

	case MEMB_STATE_EVS:
		log_printf (LOG_LEVEL_DEBUG, "Swallowing FORM token in EVS state.\n");
		printf ("FORM CONF ENTRIES %d\n", memb_form_token.conf_desc_list_entries);
		orf_token_send_initial();
		return (0);

	default:
		// TODO
		log_printf (LOG_LEVEL_ERROR, "memb_form_token: default case, shouldn't happen.\n");
		return (0);
	}

	res = memb_form_token_send (&memb_form_token);
	return (res);
}

int recv_handler (poll_handle handle, int fd, int revents, void *data)
{
	struct msghdr msg_recv;
	struct message_header *message_header;
	struct sockaddr_in system_from;
	int res = 0;
	int bytes_received;

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_in);
	msg_recv.msg_iov = &gmi_iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;

	bytes_received = recvmsg (gmi_fd, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (bytes_received == -1) {
		return (0);
	} else {
		stats_recv += bytes_received;
	}

	/*
	 * Authenticate datagram
	 */
	res = gmi_msg_auth (msg_recv.msg_iov, msg_recv.msg_iovlen);
	if (res == -1) {
		return 0;
	}

	if (stats_tv_start.tv_usec == 0) {
		gettimeofday (&stats_tv_start, NULL);
	}

	/*
	 * Handle incoming message
	 */
	message_header = (struct message_header *)msg_recv.msg_iov[0].iov_base;
	gmi_message_handlers.handler_functions[message_header->type] (
		&system_from,
		msg_recv.msg_iov,
		msg_recv.msg_iovlen,
		bytes_received);

	return (0);
}
