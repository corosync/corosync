/*
 * Copyright (c) 2003-2005 MontaVista Software, Inc.
 * Copyright (c) 2005 OSDL.
 * Copyright (c) 2006-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 * Author: Mark Haverkamp (markh@osdl.org)
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
 * FRAGMENTATION AND PACKING ALGORITHM:
 *
 * Assemble the entire message into one buffer
 * if full fragment
 *	 store fragment into lengths list
 *	for each full fragment
 *		multicast fragment
 *		set length and fragment fields of pg mesage
 *	store remaining multicast into head of fragmentation data and set lens field
 *
 * If a message exceeds the maximum packet size allowed by the totem
 * single ring protocol, the protocol could lose forward progress.
 * Statically calculating the allowed data amount doesn't work because
 * the amount of data allowed depends on the number of fragments in
 * each message.  In this implementation, the maximum fragment size
 * is dynamically calculated for each fragment added to the message.

 * It is possible for a message to be two bytes short of the maximum
 * packet size.  This occurs when a message or collection of
 * messages + the mcast header + the lens are two bytes short of the
 * end of the packet.  Since another len field consumes two bytes, the
 * len field would consume the rest of the packet without room for data.
 *
 * One optimization would be to forgo the final len field and determine
 * it from the size of the udp datagram.  Then this condition would no
 * longer occur.
 */

/*
 * ASSEMBLY AND UNPACKING ALGORITHM:
 *
 * copy incoming packet into assembly data buffer indexed by current
 * location of end of fragment
 *
 * if not fragmented
 *	deliver all messages in assembly data buffer
 * else
 * if msg_count > 1 and fragmented
 *	deliver all messages except last message in assembly data buffer
 *	copy last fragmented section to start of assembly data buffer
 * else
 * if msg_count = 1 and fragmented
 *	do nothing
 *
 */

#include <config.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <netinet/in.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>

#include <corosync/swab.h>
#include <corosync/hdb.h>
#include <corosync/list.h>
#include <corosync/totem/coropoll.h>
#include <corosync/totem/totempg.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/engine/logsys.h>

#include "totemmrp.h"
#include "totemsrp.h"

#define min(a,b) ((a) < (b)) ? a : b

struct totempg_mcast_header {
	short version;
	short type;
};

#if !(defined(__i386__) || defined(__x86_64__))
/*
 * Need align on architectures different than i386 or x86_64
 */
#define TOTEMPG_NEED_ALIGN 1
#endif

/*
 * totempg_mcast structure
 *
 * header:				Identify the mcast.
 * fragmented:			Set if this message continues into next message
 * continuation:		Set if this message is a continuation from last message
 * msg_count			Indicates how many packed messages are contained
 * 						in the mcast.
 * Also, the size of each packed message and the messages themselves are
 * appended to the end of this structure when sent.
 */
struct totempg_mcast {
	struct totempg_mcast_header header;
	unsigned char fragmented;
	unsigned char continuation;
	unsigned short msg_count;
	/*
	 * short msg_len[msg_count];
	 */
	/*
	 * data for messages
	 */
};

/*
 * Maximum packet size for totem pg messages
 */
#define TOTEMPG_PACKET_SIZE (totempg_totem_config->net_mtu - \
	sizeof (struct totempg_mcast))


/*
 * Function and data used to log messages
 */
static int totempg_log_level_security;
static int totempg_log_level_error;
static int totempg_log_level_warning;
static int totempg_log_level_notice;
static int totempg_log_level_debug;
static int totempg_subsys_id;
static void (*totempg_log_printf) (
	unsigned int rec_ident,
	const char *function,
	const char *file,
	int line,
	const char *format, ...) __attribute__((format(printf, 5, 6)));

struct totem_config *totempg_totem_config;

static totempg_stats_t totempg_stats;

enum throw_away_mode {
	THROW_AWAY_INACTIVE,
	THROW_AWAY_ACTIVE
};

struct assembly {
	unsigned int nodeid;
	unsigned char data[MESSAGE_SIZE_MAX];
	int index;
	unsigned char last_frag_num;
	enum throw_away_mode throw_away_mode;
	struct list_head list;
};

static void assembly_deref (struct assembly *assembly);

static int callback_token_received_fn (enum totem_callback_token_type type,
	const void *data);

DECLARE_LIST_INIT(assembly_list_inuse);

DECLARE_LIST_INIT(assembly_list_inuse_trans);

DECLARE_LIST_INIT(assembly_list_free_trans);

DECLARE_LIST_INIT(assembly_list_free);

/*
 * Structure for storing totem_pg contexts so they can be switched
 */
struct totempg_context {
	/*
	 * Staging buffer for packed messages.  Messages are staged in this buffer
	 * before sending.  Multiple messages may fit which cuts down on the
	 * number of mcasts sent.  If a message doesn't completely fit, then
	 * the mcast header has a fragment bit set that says that there are more
	 * data to follow.  fragment_size is an index into the buffer.  It indicates
	 * the size of message data and where to place new message data.
	 * fragment_contuation indicates whether the first packed message in
	 * the buffer is a continuation of a previously packed fragment.
	 */
	unsigned char *fragmentation_data;

	int fragment_size;

	int fragment_continuation;

	unsigned char next_fragment;

	/*
	 * Local variables used for packing small messages
	 */
	unsigned short mcast_packed_msg_lens[FRAME_SIZE_MAX];

	int mcast_packed_msg_count;

	int totempg_reserved;
};

#define TOTEMPG_NO_CONTEXTS	2

static struct totempg_context totempg_contexts_array[TOTEMPG_NO_CONTEXTS];
static struct totempg_context *totempg_active_context;

struct totempg_group_instance {
	void (*deliver_fn) (
		unsigned int nodeid,
		const void *msg,
		unsigned int msg_len,
		int endian_conversion_required);

	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		const unsigned int *member_list, size_t member_list_entries,
		const unsigned int *left_list, size_t left_list_entries,
		const unsigned int *joined_list, size_t joined_list_entries,
		const struct memb_ring_id *ring_id);

	struct totempg_group *groups;

	int groups_cnt;
};

DECLARE_HDB_DATABASE (totempg_groups_instance_database,NULL);

static unsigned int totempg_max_handle = 0;
static int totempg_waiting_transack = 0;
static unsigned int totempg_size_limit = 0;

static pthread_mutex_t totempg_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t callback_token_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t mcast_msg_mutex = PTHREAD_MUTEX_INITIALIZER;

#define log_printf(level, format, args...)				\
do {									\
        totempg_log_printf (						\
		LOGSYS_ENCODE_RECID(level,				\
				    totempg_subsys_id,			\
				    LOGSYS_RECID_LOG),			\
		__FUNCTION__, __FILE__, __LINE__,			\
		format, ##args);					\
} while (0);

static int msg_count_send_ok (int msg_count);

static int byte_count_send_ok (int byte_count);

static void totempg_set_context(int context_no)
{
	totempg_active_context = &totempg_contexts_array[context_no];
}

static void totempg_waiting_trans_ack_cb (int waiting_trans_ack)
{
	log_printf(LOG_DEBUG, "waiting_trans_ack changed to %u", waiting_trans_ack);
	totempg_waiting_transack = waiting_trans_ack;
	if (!waiting_trans_ack) {
		totempg_set_context(0);
	} else {
		totempg_set_context(1);
	}
}


static struct assembly *assembly_ref (unsigned int nodeid)
{
	struct assembly *assembly;
	struct list_head *list;
	struct list_head *active_assembly_list_inuse;
	struct list_head *active_assembly_list_free;

	if (totempg_waiting_transack) {
		active_assembly_list_inuse = &assembly_list_inuse_trans;
		active_assembly_list_free = &assembly_list_free_trans;
	} else {
		active_assembly_list_inuse = &assembly_list_inuse;
		active_assembly_list_free = &assembly_list_free;
	}

	/*
	 * Search inuse list for node id and return assembly buffer if found
	 */
	for (list = active_assembly_list_inuse->next;
		list != active_assembly_list_inuse;
		list = list->next) {

		assembly = list_entry (list, struct assembly, list);

		if (nodeid == assembly->nodeid) {
			return (assembly);
		}
	}

	/*
	 * Nothing found in inuse list get one from free list if available
	 */
	if (list_empty (active_assembly_list_free) == 0) {
		assembly = list_entry (active_assembly_list_free->next, struct assembly, list);
		list_del (&assembly->list);
		list_add (&assembly->list, active_assembly_list_inuse);
		assembly->nodeid = nodeid;
		assembly->index = 0;
		assembly->last_frag_num = 0;
		assembly->throw_away_mode = THROW_AWAY_INACTIVE;
		return (assembly);
	}

	/*
	 * Nothing available in inuse or free list, so allocate a new one
	 */
	assembly = malloc (sizeof (struct assembly));
	/*
	 * TODO handle memory allocation failure here
	 */
	assert (assembly);
	assembly->nodeid = nodeid;
	assembly->data[0] = 0;
	assembly->index = 0;
	assembly->last_frag_num = 0;
	assembly->throw_away_mode = THROW_AWAY_INACTIVE;
	list_init (&assembly->list);
	list_add (&assembly->list, active_assembly_list_inuse);

	return (assembly);
}

static void assembly_deref (struct assembly *assembly)
{
	struct list_head *active_assembly_list_free;

	if (totempg_waiting_transack) {
		active_assembly_list_free = &assembly_list_free_trans;
	} else {
		active_assembly_list_free = &assembly_list_free;
	}

	list_del (&assembly->list);
	list_add (&assembly->list, active_assembly_list_free);
}

static void assembly_deref_from_normal_and_trans (int nodeid)
{
	int j;
	struct list_head *list, *list_next;
	struct list_head *active_assembly_list_inuse;
	struct list_head *active_assembly_list_free;
	struct assembly *assembly;

	for (j = 0; j < 2; j++) {
		if (j == 0) {
			active_assembly_list_inuse = &assembly_list_inuse;
			active_assembly_list_free = &assembly_list_free;
		} else {
			active_assembly_list_inuse = &assembly_list_inuse_trans;
			active_assembly_list_free = &assembly_list_free_trans;
		}

		for (list = active_assembly_list_inuse->next;
			list != active_assembly_list_inuse;
			list = list_next) {

			list_next = list->next;
			assembly = list_entry (list, struct assembly, list);

			if (nodeid == assembly->nodeid) {
				list_del (&assembly->list);
				list_add (&assembly->list, active_assembly_list_free);
			}
		}
	}
}

static inline void app_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;
	struct totempg_group_instance *instance;
	unsigned int res;

	/*
	 * For every leaving processor, add to free list
	 * This also has the side effect of clearing out the dataset
	 * In the leaving processor's assembly buffer.
	 */
	for (i = 0; i < left_list_entries; i++) {
		assembly_deref_from_normal_and_trans (left_list[i]);
	}
	for (i = 0; i <= totempg_max_handle; i++) {
		res = hdb_handle_get (&totempg_groups_instance_database,
			hdb_nocheck_convert (i), (void *)&instance);

		if (res == 0) {
			if (instance->confchg_fn) {
				instance->confchg_fn (
					configuration_type,
					member_list,
					member_list_entries,
					left_list,
					left_list_entries,
					joined_list,
					joined_list_entries,
					ring_id);
			}

			hdb_handle_put (&totempg_groups_instance_database,
				hdb_nocheck_convert (i));
		}
	}
}

static inline void group_endian_convert (
	void *msg,
	int msg_len)
{
	unsigned short *group_len;
	int i;
	char *aligned_msg;

#ifdef TOTEMPG_NEED_ALIGN
	/*
	 * Align data structure for not i386 or x86_64
	 */
	if ((size_t)msg % 4 != 0) {
		aligned_msg = alloca(msg_len);
		memcpy(aligned_msg, msg, msg_len);
	} else {
		aligned_msg = msg;
	}
#else
	aligned_msg = msg;
#endif

	group_len = (unsigned short *)aligned_msg;
	group_len[0] = swab16(group_len[0]);
	for (i = 1; i < group_len[0] + 1; i++) {
		group_len[i] = swab16(group_len[i]);
	}

	if (aligned_msg != msg) {
		memcpy(msg, aligned_msg, msg_len);
	}
}

static inline int group_matches (
	struct iovec *iovec,
	unsigned int iov_len,
	struct totempg_group *groups_b,
	unsigned int group_b_cnt,
	unsigned int *adjust_iovec)
{
	unsigned short *group_len;
	char *group_name;
	int i;
	int j;
#ifdef TOTEMPG_NEED_ALIGN
        struct iovec iovec_aligned = { NULL, 0 };
#endif

	assert (iov_len == 1);

#ifdef TOTEMPG_NEED_ALIGN
	/*
	 * Align data structure for not i386 or x86_64
	 */
	if ((size_t)iovec->iov_base % 4 != 0) {
		iovec_aligned.iov_base = alloca(iovec->iov_len);
		memcpy(iovec_aligned.iov_base, iovec->iov_base, iovec->iov_len);
		iovec_aligned.iov_len = iovec->iov_len;
		iovec = &iovec_aligned;
	}
#endif

	group_len = (unsigned short *)iovec->iov_base;
	group_name = ((char *)iovec->iov_base) +
		sizeof (unsigned short) * (group_len[0] + 1);


	/*
	 * Calculate amount to adjust the iovec by before delivering to app
	 */
	*adjust_iovec = sizeof (unsigned short) * (group_len[0] + 1);
	for (i = 1; i < group_len[0] + 1; i++) {
		*adjust_iovec += group_len[i];
	}

	/*
	 * Determine if this message should be delivered to this instance
	 */
	for (i = 1; i < group_len[0] + 1; i++) {
		for (j = 0; j < group_b_cnt; j++) {
			if ((group_len[i] == groups_b[j].group_len) &&
				(memcmp (groups_b[j].group, group_name, group_len[i]) == 0)) {
				return (1);
			}
		}
		group_name += group_len[i];
	}
	return (0);
}


static inline void app_deliver_fn (
	unsigned int nodeid,
	void *msg,
	unsigned int msg_len,
	int endian_conversion_required)
{
	int i;
	struct totempg_group_instance *instance;
	struct iovec stripped_iovec;
	unsigned int adjust_iovec;
	unsigned int res;
	struct iovec *iovec;

        struct iovec aligned_iovec = { NULL, 0 };

	if (endian_conversion_required) {
		group_endian_convert (msg, msg_len);
	}

	/*
	 * TODO: segmentation/assembly need to be redesigned to provide aligned access
	 * in all cases to avoid memory copies on non386 archs. Probably broke backwars
	 * compatibility
	 */

#ifdef TOTEMPG_NEED_ALIGN
	/*
	 * Align data structure for not i386 or x86_64
	 */
	aligned_iovec.iov_base = alloca(msg_len);
	aligned_iovec.iov_len = msg_len;
	memcpy(aligned_iovec.iov_base, msg, msg_len);
#else
	aligned_iovec.iov_base = msg;
	aligned_iovec.iov_len = msg_len;
#endif

	iovec = &aligned_iovec;

	for (i = 0; i <= totempg_max_handle; i++) {
		res = hdb_handle_get (&totempg_groups_instance_database,
			hdb_nocheck_convert (i), (void *)&instance);

		if (res == 0) {
			if (group_matches (iovec, 1, instance->groups, instance->groups_cnt, &adjust_iovec)) {
				stripped_iovec.iov_len = iovec->iov_len - adjust_iovec;
				stripped_iovec.iov_base = (char *)iovec->iov_base + adjust_iovec;

#ifdef TOTEMPG_NEED_ALIGN
				/*
				 * Align data structure for not i386 or x86_64
				 */
				if ((char *)iovec->iov_base + adjust_iovec % 4 != 0) {
					/*
					 * Deal with misalignment
					 */
					stripped_iovec.iov_base =
						alloca (stripped_iovec.iov_len);
					memcpy (stripped_iovec.iov_base,
						 (char *)iovec->iov_base + adjust_iovec,
						stripped_iovec.iov_len);
				}
#endif
				instance->deliver_fn (
					nodeid,
					stripped_iovec.iov_base,
					stripped_iovec.iov_len,
					endian_conversion_required);
			}

			hdb_handle_put (&totempg_groups_instance_database, hdb_nocheck_convert(i));
		}
	}
}

static void totempg_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
// TODO optimize this
	app_confchg_fn (configuration_type,
		member_list, member_list_entries,
		left_list, left_list_entries,
		joined_list, joined_list_entries,
		ring_id);
}

static void totempg_deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required)
{
	struct totempg_mcast *mcast;
	unsigned short *msg_lens;
	int i;
	struct assembly *assembly;
	char header[FRAME_SIZE_MAX];
	int msg_count;
	int continuation;
	int start;
	const char *data;
	int datasize;
	struct iovec iov_delv;

	assembly = assembly_ref (nodeid);
	assert (assembly);

	/*
	 * Assemble the header into one block of data and
	 * assemble the packet contents into one block of data to simplify delivery
	 */

	mcast = (struct totempg_mcast *)msg;
	if (endian_conversion_required) {
		mcast->msg_count = swab16 (mcast->msg_count);
	}

	msg_count = mcast->msg_count;
	datasize = sizeof (struct totempg_mcast) +
		msg_count * sizeof (unsigned short);

	memcpy (header, msg, datasize);
	data = msg;

	msg_lens = (unsigned short *) (header + sizeof (struct totempg_mcast));
	if (endian_conversion_required) {
		for (i = 0; i < mcast->msg_count; i++) {
			msg_lens[i] = swab16 (msg_lens[i]);
		}
	}

	memcpy (&assembly->data[assembly->index], &data[datasize],
		msg_len - datasize);

	/*
	 * If the last message in the buffer is a fragment, then we
	 * can't deliver it.  We'll first deliver the full messages
	 * then adjust the assembly buffer so we can add the rest of the
	 * fragment when it arrives.
	 */
	msg_count = mcast->fragmented ? mcast->msg_count - 1 : mcast->msg_count;
	continuation = mcast->continuation;
	iov_delv.iov_base = (void *)&assembly->data[0];
	iov_delv.iov_len = assembly->index + msg_lens[0];

	/*
	 * Make sure that if this message is a continuation, that it
	 * matches the sequence number of the previous fragment.
	 * Also, if the first packed message is a continuation
	 * of a previous message, but the assembly buffer
	 * is empty, then we need to discard it since we can't
	 * assemble a complete message. Likewise, if this message isn't a
	 * continuation and the assembly buffer is empty, we have to discard
	 * the continued message.
	 */
	start = 0;

	if (assembly->throw_away_mode == THROW_AWAY_ACTIVE) {
		 /* Throw away the first msg block */
		if (mcast->fragmented == 0 || mcast->fragmented == 1) {
			assembly->throw_away_mode = THROW_AWAY_INACTIVE;

			assembly->index += msg_lens[0];
			iov_delv.iov_base = (void *)&assembly->data[assembly->index];
			iov_delv.iov_len = msg_lens[1];
			start = 1;
		}
	} else
	if (assembly->throw_away_mode == THROW_AWAY_INACTIVE) {
		if (continuation == assembly->last_frag_num) {
			assembly->last_frag_num = mcast->fragmented;
			for  (i = start; i < msg_count; i++) {
				app_deliver_fn(nodeid, iov_delv.iov_base, iov_delv.iov_len,
					endian_conversion_required);
				assembly->index += msg_lens[i];
				iov_delv.iov_base = (void *)&assembly->data[assembly->index];
				if (i < (msg_count - 1)) {
					iov_delv.iov_len = msg_lens[i + 1];
				}
			}
		} else {
			log_printf (LOG_DEBUG, "fragmented continuation %u is not equal to assembly last_frag_num %u",
					continuation, assembly->last_frag_num);
			assembly->throw_away_mode = THROW_AWAY_ACTIVE;
		}
	}

	if (mcast->fragmented == 0) {
		/*
		 * End of messages, dereference assembly struct
		 */
		assembly->last_frag_num = 0;
		assembly->index = 0;
		assembly_deref (assembly);
	} else {
		/*
		 * Message is fragmented, keep around assembly list
		 */
		if (mcast->msg_count > 1) {
			memmove (&assembly->data[0],
				&assembly->data[assembly->index],
				msg_lens[msg_count]);

			assembly->index = 0;
		}
		assembly->index += msg_lens[msg_count];
	}
}

/*
 * Totem Process Group Abstraction
 * depends on poll abstraction, POSIX, IPV4
 */

void *callback_token_received_handle;

int callback_token_received_fn (enum totem_callback_token_type type,
				const void *data)
{
	struct totempg_mcast mcast;
	struct iovec iovecs[3];
	int res;
	struct totempg_context *con = totempg_active_context;

	pthread_mutex_lock (&mcast_msg_mutex);
	if (con->mcast_packed_msg_count == 0) {
		pthread_mutex_unlock (&mcast_msg_mutex);
		return (0);
	}
	if (totemmrp_avail() == 0) {
		pthread_mutex_unlock (&mcast_msg_mutex);
		return (0);
	}
	mcast.header.version = 0;
	mcast.fragmented = 0;

	/*
	 * Was the first message in this buffer a continuation of a
	 * fragmented message?
	 */
	mcast.continuation = con->fragment_continuation;
	con->fragment_continuation = 0;

	mcast.msg_count = con->mcast_packed_msg_count;

	iovecs[0].iov_base = (void *)&mcast;
	iovecs[0].iov_len = sizeof (struct totempg_mcast);
	iovecs[1].iov_base = (void *)con->mcast_packed_msg_lens;
	iovecs[1].iov_len = con->mcast_packed_msg_count * sizeof (unsigned short);
	iovecs[2].iov_base = (void *)&con->fragmentation_data[0];
	iovecs[2].iov_len = con->fragment_size;
	res = totemmrp_mcast (iovecs, 3, 0);

	con->mcast_packed_msg_count = 0;
	con->fragment_size = 0;

	pthread_mutex_unlock (&mcast_msg_mutex);
	return (0);
}

/*
 * Initialize the totem process group abstraction
 */
int totempg_initialize (
	hdb_handle_t poll_handle,
	struct totem_config *totem_config)
{
	int res;
	int i;
	struct totempg_context *con;

	totempg_totem_config = totem_config;
	totempg_log_level_security = totem_config->totem_logging_configuration.log_level_security;
	totempg_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	totempg_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	totempg_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	totempg_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	totempg_log_printf = totem_config->totem_logging_configuration.log_printf;
	totempg_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;

	for (i = 0; i < TOTEMPG_NO_CONTEXTS; i++) {
		con = &totempg_contexts_array[i];
		memset(con, 0, sizeof(*con));

		con->fragmentation_data = malloc (TOTEMPG_PACKET_SIZE);
		if (con->fragmentation_data == 0) {
			return (-1);
		}
		con->totempg_reserved = 1;
		con->next_fragment = 1;
	}

	totemsrp_net_mtu_adjust (totem_config);

	res = totemmrp_initialize (
		poll_handle,
		totem_config,
		&totempg_stats,
		totempg_deliver_fn,
		totempg_confchg_fn,
		totempg_waiting_trans_ack_cb);

	totemmrp_callback_token_create (
		&callback_token_received_handle,
		TOTEM_CALLBACK_TOKEN_RECEIVED,
		0,
		callback_token_received_fn,
		0);

	totempg_size_limit = (totemmrp_avail() - 1) *
		(totempg_totem_config->net_mtu -
		sizeof (struct totempg_mcast) - 16);

	return (res);
}

void totempg_finalize (void)
{
	pthread_mutex_lock (&totempg_mutex);
	totemmrp_finalize ();
	pthread_mutex_unlock (&totempg_mutex);
}

/*
 * Multicast a message
 */
static int mcast_msg (
	struct iovec *iovec_in,
	unsigned int iov_len,
	int guarantee)
{
	int res = 0;
	struct totempg_mcast mcast;
	struct iovec iovecs[3];
	struct iovec iovec[64];
	int i;
	int dest, src;
	int max_packet_size = 0;
	int copy_len = 0;
	int copy_base = 0;
	int total_size = 0;
	struct totempg_context *con = totempg_active_context;

	pthread_mutex_lock (&mcast_msg_mutex);
	totemmrp_event_signal (TOTEM_EVENT_NEW_MSG, 1);

	/*
	 * Remove zero length iovectors from the list
	 */
	assert (iov_len < 64);
	for (dest = 0, src = 0; src < iov_len; src++) {
		if (iovec_in[src].iov_len) {
			memcpy (&iovec[dest++], &iovec_in[src],
				sizeof (struct iovec));
		}
	}
	iov_len = dest;

	max_packet_size = TOTEMPG_PACKET_SIZE -
		(sizeof (unsigned short) * (con->mcast_packed_msg_count + 1));

	con->mcast_packed_msg_lens[con->mcast_packed_msg_count] = 0;

	/*
	 * Check if we would overwrite new message queue
	 */
	for (i = 0; i < iov_len; i++) {
		total_size += iovec[i].iov_len;
	}

	if (byte_count_send_ok (total_size + sizeof(unsigned short) *
		(con->mcast_packed_msg_count)) == 0) {

		pthread_mutex_unlock (&mcast_msg_mutex);
		return(-1);
	}

	mcast.header.version = 0;
	for (i = 0; i < iov_len; ) {
		mcast.fragmented = 0;
		mcast.continuation = con->fragment_continuation;
		copy_len = iovec[i].iov_len - copy_base;

		/*
		 * If it all fits with room left over, copy it in.
		 * We need to leave at least sizeof(short) + 1 bytes in the
		 * fragment_buffer on exit so that max_packet_size + fragment_size
		 * doesn't exceed the size of the fragment_buffer on the next call.
		 */
		if ((copy_len + con->fragment_size) <
			(max_packet_size - sizeof (unsigned short))) {

			memcpy (&con->fragmentation_data[con->fragment_size],
				(char *)iovec[i].iov_base + copy_base, copy_len);
			con->fragment_size += copy_len;
			con->mcast_packed_msg_lens[con->mcast_packed_msg_count] += copy_len;
			con->next_fragment = 1;
			copy_len = 0;
			copy_base = 0;
			i++;
			continue;

		/*
		 * If it just fits or is too big, then send out what fits.
		 */
		} else {
			unsigned char *data_ptr;

			copy_len = min(copy_len, max_packet_size - con->fragment_size);
			if( copy_len == max_packet_size )
				data_ptr = (unsigned char *)iovec[i].iov_base + copy_base;
			else {
				data_ptr = con->fragmentation_data;
				memcpy (&con->fragmentation_data[con->fragment_size],
				(unsigned char *)iovec[i].iov_base + copy_base, copy_len);
			}

			memcpy (&con->fragmentation_data[con->fragment_size],
				(unsigned char *)iovec[i].iov_base + copy_base, copy_len);
			con->mcast_packed_msg_lens[con->mcast_packed_msg_count] += copy_len;

			/*
			 * if we're not on the last iovec or the iovec is too large to
			 * fit, then indicate a fragment. This also means that the next
			 * message will have the continuation of this one.
			 */
			if ((i < (iov_len - 1)) ||
					((copy_base + copy_len) < iovec[i].iov_len)) {
				if (!con->next_fragment) {
					con->next_fragment++;
				}
				con->fragment_continuation = con->next_fragment;
				mcast.fragmented = con->next_fragment++;
				assert(con->fragment_continuation != 0);
				assert(mcast.fragmented != 0);
			} else {
				con->fragment_continuation = 0;
			}

			/*
			 * assemble the message and send it
			 */
			mcast.msg_count = ++con->mcast_packed_msg_count;
			iovecs[0].iov_base = (void *)&mcast;
			iovecs[0].iov_len = sizeof(struct totempg_mcast);
			iovecs[1].iov_base = (void *)con->mcast_packed_msg_lens;
			iovecs[1].iov_len = con->mcast_packed_msg_count *
				sizeof(unsigned short);
			iovecs[2].iov_base = (void *)data_ptr;
			iovecs[2].iov_len = max_packet_size;
			assert (totemmrp_avail() > 0);
			res = totemmrp_mcast (iovecs, 3, guarantee);
			if (res == -1) {
				goto error_exit;
			}

			/*
			 * Recalculate counts and indexes for the next.
			 */
			con->mcast_packed_msg_lens[0] = 0;
			con->mcast_packed_msg_count = 0;
			con->fragment_size = 0;
			max_packet_size = TOTEMPG_PACKET_SIZE - (sizeof(unsigned short));

			/*
			 * If the iovec all fit, go to the next iovec
			 */
			if ((copy_base + copy_len) == iovec[i].iov_len) {
				copy_len = 0;
				copy_base = 0;
				i++;

			/*
			 * Continue with the rest of the current iovec.
			 */
			} else {
				copy_base += copy_len;
			}
		}
	}

	/*
	 * Bump only if we added message data.  This may be zero if
	 * the last buffer just fit into the fragmentation_data buffer
	 * and we were at the last iovec.
	 */
	if (con->mcast_packed_msg_lens[con->mcast_packed_msg_count]) {
			con->mcast_packed_msg_count++;
	}

error_exit:
	pthread_mutex_unlock (&mcast_msg_mutex);
	return (res);
}

/*
 * Determine if a message of msg_size could be queued
 */
static int msg_count_send_ok (
	int msg_count)
{
	int avail = 0;
	struct totempg_context *con = totempg_active_context;

	avail = totemmrp_avail ();
	totempg_stats.msg_queue_avail = avail;

	return ((avail - con->totempg_reserved) > msg_count);
}

static int byte_count_send_ok (
	int byte_count)
{
	unsigned int msg_count = 0;
	int avail = 0;

	avail = totemmrp_avail ();

	msg_count = (byte_count / (totempg_totem_config->net_mtu - sizeof (struct totempg_mcast) - 16)) + 1;

	return (avail >= msg_count);
}

static int send_reserve (
	int msg_size)
{
	unsigned int msg_count = 0;
	struct totempg_context *con = totempg_active_context;

	msg_count = (msg_size / (totempg_totem_config->net_mtu - sizeof (struct totempg_mcast) - 16)) + 1;
	con->totempg_reserved += msg_count;
	totempg_stats.msg_reserved = con->totempg_reserved;

	return (msg_count);
}

static void send_release (
	int msg_count)
{
	struct totempg_context *con = totempg_active_context;

	con->totempg_reserved -= msg_count;
	totempg_stats.msg_reserved = con->totempg_reserved;
}

int totempg_callback_token_create (
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, const void *),
	const void *data)
{
	unsigned int res;
	pthread_mutex_lock (&callback_token_mutex);
	res = totemmrp_callback_token_create (handle_out, type, delete,
		callback_fn, data);
	pthread_mutex_unlock (&callback_token_mutex);
	return (res);
}

void totempg_callback_token_destroy (
	void *handle_out)
{
	pthread_mutex_lock (&callback_token_mutex);
	totemmrp_callback_token_destroy (handle_out);
	pthread_mutex_unlock (&callback_token_mutex);
}

/*
 *	vi: set autoindent tabstop=4 shiftwidth=4 :
 */

int totempg_groups_initialize (
	hdb_handle_t *handle,

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
		const struct memb_ring_id *ring_id))
{
	struct totempg_group_instance *instance;
	unsigned int res;

	pthread_mutex_lock (&totempg_mutex);
	res = hdb_handle_create (&totempg_groups_instance_database,
		sizeof (struct totempg_group_instance), handle);
	if (res != 0) {
		goto error_exit;
	}

	if (*handle > totempg_max_handle) {
		totempg_max_handle = *handle;
	}

	res = hdb_handle_get (&totempg_groups_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}

	instance->deliver_fn = deliver_fn;
	instance->confchg_fn = confchg_fn;
	instance->groups = 0;
	instance->groups_cnt = 0;


	hdb_handle_put (&totempg_groups_instance_database, *handle);

	pthread_mutex_unlock (&totempg_mutex);
	return (0);
error_destroy:
	hdb_handle_destroy (&totempg_groups_instance_database, *handle);

error_exit:
	pthread_mutex_unlock (&totempg_mutex);
	return (-1);
}

int totempg_groups_join (
	hdb_handle_t handle,
	const struct totempg_group *groups,
	size_t group_cnt)
{
	struct totempg_group_instance *instance;
	struct totempg_group *new_groups;
	unsigned int res;

	pthread_mutex_lock (&totempg_mutex);
	res = hdb_handle_get (&totempg_groups_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	new_groups = realloc (instance->groups,
		sizeof (struct totempg_group) *
		(instance->groups_cnt + group_cnt));
	if (new_groups == 0) {
		res = ENOMEM;
		goto error_exit;
	}
	memcpy (&new_groups[instance->groups_cnt],
		groups, group_cnt * sizeof (struct totempg_group));
	instance->groups = new_groups;
	instance->groups_cnt += group_cnt;

	hdb_handle_put (&totempg_groups_instance_database, handle);

error_exit:
	pthread_mutex_unlock (&totempg_mutex);
	return (res);
}

int totempg_groups_leave (
	hdb_handle_t handle,
	const struct totempg_group *groups,
	size_t group_cnt)
{
	struct totempg_group_instance *instance;
	unsigned int res;

	pthread_mutex_lock (&totempg_mutex);
	res = hdb_handle_get (&totempg_groups_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	hdb_handle_put (&totempg_groups_instance_database, handle);

error_exit:
	pthread_mutex_unlock (&totempg_mutex);
	return (res);
}

#define MAX_IOVECS_FROM_APP 32
#define MAX_GROUPS_PER_MSG 32

int totempg_groups_mcast_joined (
	hdb_handle_t handle,
	const struct iovec *iovec,
	unsigned int iov_len,
	int guarantee)
{
	struct totempg_group_instance *instance;
	unsigned short group_len[MAX_GROUPS_PER_MSG + 1];
	struct iovec iovec_mcast[MAX_GROUPS_PER_MSG + 1 + MAX_IOVECS_FROM_APP];
	int i;
	unsigned int res;

	pthread_mutex_lock (&totempg_mutex);
	res = hdb_handle_get (&totempg_groups_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	/*
	 * Build group_len structure and the iovec_mcast structure
	 */
	group_len[0] = instance->groups_cnt;
	for (i = 0; i < instance->groups_cnt; i++) {
		group_len[i + 1] = instance->groups[i].group_len;
		iovec_mcast[i + 1].iov_len = instance->groups[i].group_len;
		iovec_mcast[i + 1].iov_base = (void *) instance->groups[i].group;
	}
	iovec_mcast[0].iov_len = (instance->groups_cnt + 1) * sizeof (unsigned short);
	iovec_mcast[0].iov_base = group_len;
	for (i = 0; i < iov_len; i++) {
		iovec_mcast[i + instance->groups_cnt + 1].iov_len = iovec[i].iov_len;
		iovec_mcast[i + instance->groups_cnt + 1].iov_base = iovec[i].iov_base;
	}

	res = mcast_msg (iovec_mcast, iov_len + instance->groups_cnt + 1, guarantee);
	hdb_handle_put (&totempg_groups_instance_database, handle);

error_exit:
	pthread_mutex_unlock (&totempg_mutex);
	return (res);
}

int totempg_groups_joined_reserve (
	hdb_handle_t handle,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	struct totempg_group_instance *instance;
	unsigned int size = 0;
	unsigned int i;
	unsigned int res;
	unsigned int reserved = 0;

	pthread_mutex_lock (&totempg_mutex);
	pthread_mutex_lock (&mcast_msg_mutex);
	res = hdb_handle_get (&totempg_groups_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	for (i = 0; i < instance->groups_cnt; i++) {
		size += instance->groups[i].group_len;
	}
	for (i = 0; i < iov_len; i++) {
		size += iovec[i].iov_len;
	}
	if (size >= totempg_size_limit) {
		reserved = -1;
		goto error_put;
	}

	reserved = send_reserve (size);
	if (msg_count_send_ok (reserved) == 0) {
		send_release (reserved);
		reserved = 0;
	}

error_put:
	hdb_handle_put (&totempg_groups_instance_database, handle);

error_exit:
	pthread_mutex_unlock (&mcast_msg_mutex);
	pthread_mutex_unlock (&totempg_mutex);
	return (reserved);
}


int totempg_groups_joined_release (int msg_count)
{
	pthread_mutex_lock (&totempg_mutex);
	pthread_mutex_lock (&mcast_msg_mutex);
	send_release (msg_count);
	pthread_mutex_unlock (&mcast_msg_mutex);
	pthread_mutex_unlock (&totempg_mutex);
	return 0;
}

int totempg_groups_mcast_groups (
	hdb_handle_t handle,
	int guarantee,
	const struct totempg_group *groups,
	size_t groups_cnt,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	struct totempg_group_instance *instance;
	unsigned short group_len[MAX_GROUPS_PER_MSG + 1];
	struct iovec iovec_mcast[MAX_GROUPS_PER_MSG + 1 + MAX_IOVECS_FROM_APP];
	int i;
	unsigned int res;

	pthread_mutex_lock (&totempg_mutex);
	res = hdb_handle_get (&totempg_groups_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	/*
	 * Build group_len structure and the iovec_mcast structure
	 */
	group_len[0] = groups_cnt;
	for (i = 0; i < groups_cnt; i++) {
		group_len[i + 1] = groups[i].group_len;
		iovec_mcast[i + 1].iov_len = groups[i].group_len;
		iovec_mcast[i + 1].iov_base = (void *) groups[i].group;
	}
	iovec_mcast[0].iov_len = (groups_cnt + 1) * sizeof (unsigned short);
	iovec_mcast[0].iov_base = group_len;
	for (i = 0; i < iov_len; i++) {
		iovec_mcast[i + groups_cnt + 1].iov_len = iovec[i].iov_len;
		iovec_mcast[i + groups_cnt + 1].iov_base = iovec[i].iov_base;
	}

	res = mcast_msg (iovec_mcast, iov_len + groups_cnt + 1, guarantee);

	hdb_handle_put (&totempg_groups_instance_database, handle);

error_exit:
	pthread_mutex_unlock (&totempg_mutex);
	return (res);
}

/*
 * Returns -1 if error, 0 if can't send, 1 if can send the message
 */
int totempg_groups_send_ok_groups (
	hdb_handle_t handle,
	const struct totempg_group *groups,
	size_t groups_cnt,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	struct totempg_group_instance *instance;
	unsigned int size = 0;
	unsigned int i;
	unsigned int res;

	pthread_mutex_lock (&totempg_mutex);
	res = hdb_handle_get (&totempg_groups_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	for (i = 0; i < groups_cnt; i++) {
		size += groups[i].group_len;
	}
	for (i = 0; i < iov_len; i++) {
		size += iovec[i].iov_len;
	}

	res = msg_count_send_ok (size);

	hdb_handle_put (&totempg_groups_instance_database, handle);
error_exit:
	pthread_mutex_unlock (&totempg_mutex);
	return (res);
}

int totempg_ifaces_get (
	unsigned int nodeid,
	struct totem_ip_address *interfaces,
	char ***status,
	unsigned int *iface_count)
{
	int res;

	res = totemmrp_ifaces_get (
		nodeid,
		interfaces,
		status,
		iface_count);

	return (res);
}

void totempg_event_signal (enum totem_event_type type, int value)
{
	totemmrp_event_signal (type, value);
}

void* totempg_get_stats (void)
{
	return &totempg_stats;
}

int totempg_crypto_set (
	unsigned int type)
{
	int res;

	res = totemmrp_crypto_set (
		type);

	return (res);
}

int totempg_ring_reenable (void)
{
	int res;

	res = totemmrp_ring_reenable ();

	return (res);
}

const char *totempg_ifaces_print (unsigned int nodeid)
{
	static char iface_string[256 * INTERFACE_MAX];
	char one_iface[64];
	struct totem_ip_address interfaces[INTERFACE_MAX];
	char **status;
	unsigned int iface_count;
	unsigned int i;
	int res;

	iface_string[0] = '\0';

	res = totempg_ifaces_get (nodeid, interfaces, &status, &iface_count);
	if (res == -1) {
		return ("no interface found for nodeid");
	}

	for (i = 0; i < iface_count; i++) {
		sprintf (one_iface, "r(%d) ip(%s) ",
			i, totemip_print (&interfaces[i]));
		strcat (iface_string, one_iface);
	}
	return (iface_string);
}

unsigned int totempg_my_nodeid_get (void)
{
	return (totemmrp_my_nodeid_get());
}

int totempg_my_family_get (void)
{
	return (totemmrp_my_family_get());
}
extern void totempg_service_ready_register (
	void (*totem_service_ready) (void))
{
	totemmrp_service_ready_register (totem_service_ready);
}

extern int totempg_member_add (
	const struct totem_ip_address *member,
	int ring_no);

extern int totempg_member_remove (
	const struct totem_ip_address *member,
	int ring_no);

void totempg_trans_ack (void)
{
	totemmrp_trans_ack ();
}
