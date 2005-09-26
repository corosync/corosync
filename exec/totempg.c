/*
 * Copyright (c) 2003-2005 MontaVista Software, Inc.
 * Copyright (c) 2005 OSDL.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
 *         Mark Haverkamp (markh@osdl.org)
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

#include "totempg.h"
#include "totemsrp.h"
#include "totemmrp.h"
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <netinet/in.h>

#include "swab.h"

#define min(a,b) ((a) < (b)) ? a : b

struct totempg_mcast_header {
	short version;
	short type;
};

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
 * Local variables used for packing small messages
 */
static unsigned short mcast_packed_msg_lens[FRAME_SIZE_MAX];

static int mcast_packed_msg_count = 0;

struct totem_config *totempg_totem_config;

static void (*app_deliver_fn) (
		struct in_addr source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required) = 0;

static void (*app_confchg_fn) (
		enum totem_configuration_type configuration_type,
		struct in_addr *member_list, int member_list_entries,
		struct in_addr *left_list, int left_list_entries,
		struct in_addr *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id) = 0;

struct assembly {
	struct in_addr addr;
	unsigned char data[MESSAGE_SIZE_MAX];
	int index;
	unsigned char last_frag_num;
};

struct assembly *assembly_list[PROCESSOR_COUNT_MAX];
int assembly_list_entries = 0;

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
static unsigned char *fragmentation_data;
int fragment_size = 0;
int fragment_continuation = 0;

static struct iovec iov_delv;

static struct assembly *find_assembly (struct in_addr addr)
{
	int i;

	for (i = 0; i < assembly_list_entries; i++) {
		if (addr.s_addr == assembly_list[i]->addr.s_addr) {
			return (assembly_list[i]);
		}
	}
	return (0);
}

static void totempg_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct in_addr *member_list, int member_list_entries,
	struct in_addr *left_list, int left_list_entries,
	struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;
	int j;
	int found;
		
	/*
	 * Clean out the assembly area for nodes that have left the
	 * membership.  If they return, we don't want any stale message
	 * data that may be there.
	 */
	for (i = 0; i < left_list_entries; i++) {
		for (j = 0; j < assembly_list_entries; j++) {
			if (left_list[i].s_addr == assembly_list[j]->addr.s_addr) {
				assembly_list[j]->index = 0;
			}
		}
	}

	/*
	 * Create a message assembly area for any new members.
	 */
	for (i = 0; i < member_list_entries; i++) {
		found = 0;
		for (j = 0; j < assembly_list_entries; j++) {
			if (member_list[i].s_addr == assembly_list[j]->addr.s_addr) {
				found = 1; 
				break;
			}
		}
		if (found == 0) {
			assembly_list[assembly_list_entries] =
				malloc (sizeof (struct assembly));
			assert (assembly_list[assembly_list_entries]); // TODO
			assembly_list[assembly_list_entries]->addr.s_addr = 
				member_list[i].s_addr;
			assembly_list[assembly_list_entries]->index = 0;
			assembly_list_entries += 1;
		}
	}

	app_confchg_fn (configuration_type,
		member_list, member_list_entries,
		left_list, left_list_entries,
		joined_list, joined_list_entries,
		ring_id);
}

static void totempg_deliver_fn (
	struct in_addr source_addr,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required)
{
	struct totempg_mcast *mcast;
	unsigned short *msg_lens;
	int i;
	struct assembly *assembly;
	char header[FRAME_SIZE_MAX];
	int h_index;
	int a_i = 0;
	int msg_count;
	int continuation;
	int start;

	assembly = find_assembly (source_addr);
	assert (assembly);

	/*
	 * Assemble the header into one block of data and
	 * assemble the packet contents into one block of data to simplify delivery
	 */
	if (iov_len == 1) {
		/* 
		 * This message originated from external processor 
		 * because there is only one iovec for the full msg.
		 */
		char *data;
		int datasize;

		mcast = (struct totempg_mcast *)iovec[0].iov_base;

		msg_count = mcast->msg_count;
		if (endian_conversion_required) {
			msg_count = swab16 (mcast->msg_count);
		}
		datasize = sizeof (struct totempg_mcast) +
			msg_count * sizeof (unsigned short);
		
		memcpy (header, iovec[0].iov_base, datasize);
		assert(iovec);
		data = iovec[0].iov_base;

		msg_lens = (unsigned short *) (header + sizeof (struct totempg_mcast));

		memcpy (&assembly->data[assembly->index], &data[datasize],
			iovec[0].iov_len - datasize);
	} else {
		/* 
		 * The message originated from local processor  
		 * becasue there is greater than one iovec for then full msg.
		 */
		h_index = 0;
		for (i = 0; i < 2; i++) {
			memcpy (&header[h_index], iovec[i].iov_base, iovec[i].iov_len);
			h_index += iovec[i].iov_len;
		}

		mcast = (struct totempg_mcast *)header;
// TODO make sure we are using a copy of mcast not the actual data itself

		msg_lens = (unsigned short *) (header + sizeof (struct totempg_mcast));

		for (i = 2; i < iov_len; i++) {
			a_i = assembly->index;
			assert (iovec[i].iov_len + a_i <= MESSAGE_SIZE_MAX);
			memcpy (&assembly->data[a_i], iovec[i].iov_base, iovec[i].iov_len);
			a_i += msg_lens[i - 2];
		}
		iov_len -= 2;
	}

	if (endian_conversion_required) {
		mcast->msg_count = swab16 (mcast->msg_count);
		for (i = 0; i < mcast->msg_count; i++) {
			msg_lens[i] = swab16 (msg_lens[i]);
		}
	}

/*
printf ("Message fragmented %d count %d\n", mcast->fragmented, mcast->msg_count);
	for (i = 0; i < mcast->msg_count; i++) {
		printf ("len[%d] = %d\n", i, msg_lens[i]);
	}
*/

	/*
	 * If the last message in the buffer is a fragment, then we
	 * can't deliver it.  We'll first deliver the full messages
	 * then adjust the assembly buffer so we can add the rest of the 
	 * fragment when it arrives.
	 */
	msg_count = mcast->fragmented ? mcast->msg_count - 1 : mcast->msg_count;
	continuation = mcast->continuation;
	iov_delv.iov_base = &assembly->data[0];
	iov_delv.iov_len = assembly->index + msg_lens[0];
//	printf ("%d %d %d\n", msg_count, continuation, assembly->last_frag_num);

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
	if (continuation) {

		if (continuation != assembly->last_frag_num) {
			printf("Message continuation doesn't match previous frag e: %u - a: %u\n",
					assembly->last_frag_num, continuation);
			continuation = 0;
		}

		if ((assembly->index == 0) ||
							(!continuation && assembly->index)) {
			printf("Throwing away broken message: continuation %u, index %u\n",
					continuation, assembly->index);
			continuation = 0;
		}
		
		/* 
		 * we decided to throw away the first continued message
		 * in this buffer, if continuation was set to zero.
		 */
		if (!continuation) {
			assembly->index += msg_lens[0];
			iov_delv.iov_base = &assembly->data[assembly->index];
			iov_delv.iov_len = msg_lens[1];
			start = 1;
		}
			
	}

	for  (i = start; i < msg_count; i++) {
		app_deliver_fn(source_addr, &iov_delv, 1,
			endian_conversion_required);
		assembly->index += msg_lens[i];
		iov_delv.iov_base = &assembly->data[assembly->index];
		if (i < (msg_count - 1)) {
			iov_delv.iov_len = msg_lens[i + 1];
		}
	}

	if (mcast->fragmented) {
		assembly->last_frag_num = mcast->fragmented;
		if (mcast->msg_count > 1) {
			memmove (&assembly->data[0],
				&assembly->data[assembly->index],
				msg_lens[msg_count]);

			assembly->index = 0;
		}
		assembly->index += msg_lens[msg_count];
	} else {
		assembly->last_frag_num = 0;
		assembly->index = 0;
	}
}

/*
 * Totem Process Group Abstraction
 * depends on poll abstraction, POSIX, IPV4
 */

void *callback_token_received_handle;

int callback_token_received_fn (enum totem_callback_token_type type,
	void *data)
{
	struct totempg_mcast mcast;
	struct iovec iovecs[3];
	int res;

	if (mcast_packed_msg_count == 0) {
		return (0);
	}
	mcast.fragmented = 0;

	/*
	 * Was the first message in this buffer a continuation of a
	 * fragmented message?
	 */
	mcast.continuation = fragment_continuation;
	fragment_continuation = 0;

	mcast.msg_count = mcast_packed_msg_count;

	iovecs[0].iov_base = &mcast;
	iovecs[0].iov_len = sizeof (struct totempg_mcast);
	iovecs[1].iov_base = mcast_packed_msg_lens;
	iovecs[1].iov_len = mcast_packed_msg_count * sizeof (unsigned short);
	iovecs[2].iov_base = &fragmentation_data[0];
	iovecs[2].iov_len = fragment_size;
	res = totemmrp_mcast (iovecs, 3, 0);

	mcast_packed_msg_count = 0;
	fragment_size = 0;

	return (0);
}

/*
 * Initialize the totem process group abstraction
 */
int totempg_initialize (
	poll_handle poll_handle,
	totemsrp_handle *totemsrp_handle,
	struct totem_config *totem_config,

	void (*deliver_fn) (
		struct in_addr source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required),

	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		struct in_addr *member_list, int member_list_entries,
		struct in_addr *left_list, int left_list_entries,
		struct in_addr *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id))
{
	int res;

	app_deliver_fn = deliver_fn;
	app_confchg_fn = confchg_fn;

	totempg_totem_config = totem_config;

	fragmentation_data = malloc (TOTEMPG_PACKET_SIZE);
	if (fragmentation_data == 0) {
		return (-1);
	}

	res = totemmrp_initialize (
		poll_handle,
		totemsrp_handle,
		totem_config,
		totempg_deliver_fn, totempg_confchg_fn);

	totemmrp_callback_token_create (
		&callback_token_received_handle, 
		TOTEM_CALLBACK_TOKEN_RECEIVED,
		0,
		callback_token_received_fn,
		0);

	totemsrp_net_mtu_adjust (totem_config);

	return (res);
}

void totempg_finalize (void)
{
	totemmrp_finalize ();
}

static unsigned char next_fragment = 1;

/*
 * Multicast a message
 */
int totempg_mcast (
	struct iovec *iovec,
	int iov_len,
	int guarantee)
{
	int res = 0;
	struct totempg_mcast mcast;
	struct iovec iovecs[3];
	int i;
	int max_packet_size = 0;
	int copy_len = 0; 
	int copy_base = 0;
	int total_size = 0;

	totemmrp_new_msg_signal ();

	max_packet_size = TOTEMPG_PACKET_SIZE -
		(sizeof (unsigned short) * (mcast_packed_msg_count + 1));

	mcast_packed_msg_lens[mcast_packed_msg_count] = 0;

	/*
	 * Check if we would overwrite new message queue
	 */
	for (i = 0; i < iov_len; i++) {
		total_size += iovec[i].iov_len;
	}

	if( totempg_send_ok (total_size + sizeof(unsigned short) *
		(mcast_packed_msg_count+1)) == 0) {

		return(-1);
	}

	for (i = 0; i < iov_len; ) {
		mcast.fragmented = 0;
		mcast.continuation = fragment_continuation;
		copy_len = iovec[i].iov_len - copy_base;

		/*
		 * If it all fits with room left over, copy it in.
		 * We need to leave at least sizeof(short) + 1 bytes in the
		 * fragment_buffer on exit so that max_packet_size + fragment_size
		 * doesn't exceed the size of the fragment_buffer on the next call.
		 */
		if ((copy_len + fragment_size) < 
			(max_packet_size - sizeof (unsigned short))) {

			memcpy (&fragmentation_data[fragment_size],
				iovec[i].iov_base + copy_base, copy_len);
			fragment_size += copy_len;
			mcast_packed_msg_lens[mcast_packed_msg_count] += copy_len;
			copy_len = 0;
			copy_base = 0;
			i++;
			continue;

		/*
		 * If it just fits or is too big, then send out what fits.
		 */
		} else {
			copy_len = min(copy_len, max_packet_size - fragment_size);
			memcpy (&fragmentation_data[fragment_size],
				iovec[i].iov_base + copy_base, copy_len);
			mcast_packed_msg_lens[mcast_packed_msg_count] += copy_len;

			/*
			 * if we're not on the last iovec or the iovec is too large to
			 * fit, then indicate a fragment. This also means that the next
			 * message will have the continuation of this one.
			 */
			if ((i < (iov_len - 1)) || 
					((copy_base + copy_len) < iovec[i].iov_len)) {
				if (!next_fragment) {
					next_fragment++;
				}
				fragment_continuation = next_fragment;
				mcast.fragmented = next_fragment++;
				assert(fragment_continuation != 0);
				assert(mcast.fragmented != 0);
			} else {
				fragment_continuation = 0;
			}

			/*
			 * assemble the message and send it
			 */
			mcast.msg_count = ++mcast_packed_msg_count;
			iovecs[0].iov_base = &mcast;
			iovecs[0].iov_len = sizeof(struct totempg_mcast);
			iovecs[1].iov_base = mcast_packed_msg_lens;
			iovecs[1].iov_len = mcast_packed_msg_count * 
												sizeof(unsigned short);
			iovecs[2].iov_base = fragmentation_data;
			iovecs[2].iov_len = max_packet_size;
			res = totemmrp_mcast (iovecs, 3, guarantee);

			/*
			 * Recalculate counts and indexes for the next.
			 */
			mcast_packed_msg_lens[0] = 0;
			mcast_packed_msg_count = 0;
			fragment_size = 0;
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
	if (mcast_packed_msg_lens[mcast_packed_msg_count]) {
			mcast_packed_msg_count++;
	}

	return (res);
}

/*
 * Determine if a message of msg_size could be queued
 */
int totempg_send_ok (
	int msg_size)
{
	int avail = 0;
	int total;

	avail = totemmrp_avail ();
	
	/*
	 * msg size less then totempg_totem_config->net_mtu - 25 will take up
	 * a full message, so add +1
	 * totempg_totem_config->net_mtu - 25 is for the totempg_mcast header
	 */


	total = (msg_size / (totempg_totem_config->net_mtu - 25)) + 1; 

	return (avail >= total);
}
/*
 *	vi: set autoindent tabstop=4 shiftwidth=4 :
 */
