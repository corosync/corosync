/*
 * Copyright (c) 2003-2005 MontaVista Software, Inc.
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
 * single ring protocol, the protocol could loose forward progress.
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
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <netinet/in.h>

#include "swab.h"

struct totempg_mcast_header {
	short version;
	short type;
};

struct totempg_mcast {
	struct totempg_mcast_header header;
	short fragmented; /* This message continues into next message */
	short msg_count;
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
#define TOTEMPG_PACKET_SIZE (TOTEMSRP_PACKET_SIZE_MAX - sizeof (struct totempg_mcast))

/*
 * Local variables used for packing small messages
 */
static unsigned short mcast_packed_msg_lens[TOTEMSRP_PACKET_SIZE_MAX];

static int mcast_packed_msg_count = 0;

static void (*app_deliver_fn) (
		struct in_addr source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required) = 0;

static void (*app_confchg_fn) (
		enum totempg_configuration_type configuration_type,
		struct in_addr *member_list, void *member_list_private, 
			int member_list_entries,
		struct in_addr *left_list, void *left_list_private,
			int left_list_entries,
		struct in_addr *joined_list, void *joined_list_private,
			int joined_list_entries) = 0;

struct assembly {
	struct in_addr addr;
	unsigned char data[MESSAGE_SIZE_MAX];
	int index;
};

struct assembly *assembly_list[16]; // MAX PROCESSORS TODO
int assembly_list_entries = 0;

static unsigned char fragmentation_data[MESSAGE_SIZE_MAX];

int fragment_size = 0;

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
	enum totempg_configuration_type configuration_type,
	struct in_addr *member_list, void *member_list_private, 
		int member_list_entries,
	struct in_addr *left_list, void *left_list_private,
		int left_list_entries,
	struct in_addr *joined_list, void *joined_list_private,
		int joined_list_entries)
{
	int i;
	int j;
	int found;
		
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
			assembly_list[assembly_list_entries]->addr.s_addr = member_list[i].s_addr;
			assembly_list[i]->index = 0;
			assembly_list_entries += 1;
		}
	}

	app_confchg_fn (configuration_type,
		member_list, member_list_private, member_list_entries,
		left_list, left_list_private, left_list_entries,
		joined_list, joined_list_private, joined_list_entries);
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
	char header[1500];
	int h_index;
	int a_i = 0;
	int msg_count;

	assembly = find_assembly (source_addr);
	assert (assembly);

	/*
	 * Assemble the header into one block of data
	 * Assemble the packet contents into one block of data to simplify delivery
	 */
	if (iov_len == 1) {
		/* message originated from external processor - 1 iovec for full msg */
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
		data = iovec[0].iov_base;

		msg_lens = (unsigned short *) (header + sizeof (struct totempg_mcast));

		memcpy (&assembly->data[assembly->index], &data[datasize],
			iovec[0].iov_len - datasize);
	} else {
		/* message originated from local processor  - <1 iovec for full msg */
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
			memcpy (&assembly->data[a_i], iovec[i].iov_base, iovec[i].iov_len);
			a_i += msg_lens[i - 2];
		}
		iov_len -= 2;
	}

	if (endian_conversion_required) {
		mcast->fragmented = swab16 (mcast->fragmented);
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
	 * Deliver all full messages in packed message
	 */

	/*
	 * this message's last packed message is not a fragment
	 */
	if (mcast->fragmented == 0) {
		iov_delv.iov_base = &assembly->data[0];
		iov_delv.iov_len = assembly->index + msg_lens[0];
		for  (i = 0; i < mcast->msg_count; i++) {
			assembly->index += msg_lens[i];
//printf ("app deliver\n");
			app_deliver_fn (source_addr, &iov_delv, 1,
				endian_conversion_required);
			iov_delv.iov_base = &assembly->data[assembly->index];
			iov_delv.iov_len = msg_lens[i + 1];
		}
		assembly->index = 0;
	} else

	/*
	 * This message's last packed message is a fragment
	 */
	if (mcast->fragmented == 1) {
		iov_delv.iov_base = &assembly->data[0];
		iov_delv.iov_len = assembly->index + msg_lens[0];
		for  (i = 0; i < mcast->msg_count - 1; i++) {
			assembly->index += msg_lens[i];
//printf ("app deliver\n");
			app_deliver_fn (source_addr, &iov_delv, 1,
				endian_conversion_required);
			iov_delv.iov_base = &assembly->data[assembly->index];
			iov_delv.iov_len = msg_lens[i + 1];
		}
		if (mcast->msg_count > 1) {
			memmove (&assembly->data[0],
				&assembly->data[assembly->index],
				msg_lens[mcast->msg_count - 1]);

			assembly->index = 0;
		}
		assembly->index += msg_lens[mcast->msg_count - 1];
	}
}

/*
 * Totem Process Group Abstraction
 * depends on poll abstraction, POSIX, IPV4
 */
/*
 * Initialize the logger
 */
void totempg_log_printf_init (
	void (*log_printf) (int , char *, ...),
	int log_level_security,
	int log_level_error,
	int log_level_warning,
	int log_level_notice,
	int log_level_debug)
{
	totemsrp_log_printf_init (
		log_printf,
		log_level_security,
		log_level_error,
		log_level_warning,
		log_level_notice,
		log_level_debug);
}

void *callback_token_received_handle;

int callback_token_received_fn (enum totemsrp_callback_token_type type,
	void *data)
{
	struct totempg_mcast mcast;
	struct iovec iovecs[3];
	int res;

	if (mcast_packed_msg_count == 0) {
		return (0);
	}
	mcast.fragmented = 0;
	mcast.msg_count = mcast_packed_msg_count;

	iovecs[0].iov_base = &mcast;
	iovecs[0].iov_len = sizeof (struct totempg_mcast);
	iovecs[1].iov_base = mcast_packed_msg_lens;
	iovecs[1].iov_len = mcast_packed_msg_count * sizeof (unsigned short);
	iovecs[2].iov_base = &fragmentation_data[0];
	iovecs[2].iov_len = fragment_size;
	res = totemsrp_mcast (iovecs, 3, 0);

	mcast_packed_msg_count = 0;
	fragment_size = 0;

	return (0);
}

/*
 * Initialize the totem process group abstraction
 */
int totempg_initialize (
	struct sockaddr_in *sockaddr_mcast,
	struct totempg_interface *interfaces,
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
		enum totempg_configuration_type configuration_type,
		struct in_addr *member_list, void *member_list_private, 
			int member_list_entries,
		struct in_addr *left_list, void *left_list_private,
			int left_list_entries,
		struct in_addr *joined_list, void *joined_list_private,
			int joined_list_entries))
{
	int res;

	app_deliver_fn = deliver_fn;
	app_confchg_fn = confchg_fn;

	res = totemsrp_initialize (sockaddr_mcast, (struct totemsrp_interface *)interfaces,
		interface_count,
		poll_handle,
		private_key, private_key_len,
		member_private, member_private_len,
		totempg_deliver_fn, totempg_confchg_fn);
 
	totemsrp_callback_token_create (&callback_token_received_handle, 
		TOTEMSRP_CALLBACK_TOKEN_RECEIVED,
		0,
		callback_token_received_fn,
		0);

	return (res);
}

/*
 * Multicast a message
 */
int totempg_mcast (
	struct iovec *iovec,
	int iov_len,
	int guarantee,
	int priority)
{
	int res = 0;
	struct totempg_mcast mcast;
	struct iovec iovecs[3];
	int i;
	int j;
	int copy_len;
	int fragment_index = 0;
	int fragment_size_assem = 0;
	int max_packet_size = 0;
	int remaining_size = 0;
	int goober;
	int f_i;

	mcast.msg_count = 0;

	copy_len = 0;
	for (i = 0; i < iov_len; i++) {
		memcpy (&fragmentation_data[fragment_size],
			iovec[i].iov_base, iovec[i].iov_len);
		fragment_size += iovec[i].iov_len;
		copy_len += iovec[i].iov_len;
	}

	max_packet_size = TOTEMPG_PACKET_SIZE -
		(sizeof (unsigned short) * (mcast_packed_msg_count + 1));

	if (fragment_size >= max_packet_size) {
		/*
		 * Determine size of packed data so far
		 */
		for (j = 0; j < mcast_packed_msg_count; j++) {
			fragment_size_assem += mcast_packed_msg_lens[j];
		}

		/*
		 * If there was previously packed data, remainder of packet
		 * should be consumed 
		 */
		if (max_packet_size - fragment_size_assem) {
			mcast_packed_msg_lens[mcast_packed_msg_count] = max_packet_size - fragment_size_assem;
			mcast_packed_msg_count++;
		} else {
			max_packet_size = TOTEMPG_PACKET_SIZE -
				(sizeof (unsigned short) * mcast_packed_msg_count);
		}
		mcast.fragmented = 1;

		/*
		 * Multicast any full fragments
		 */
		fragment_index = max_packet_size;

		f_i = 0;
		while (fragment_index <= fragment_size) {
			mcast.msg_count = mcast_packed_msg_count;

			if (fragment_index == fragment_size) {
				mcast.fragmented = 0;
			}

			iovecs[0].iov_base = &mcast;
			iovecs[0].iov_len = sizeof (struct totempg_mcast);
			iovecs[1].iov_base = mcast_packed_msg_lens;
			iovecs[1].iov_len = mcast_packed_msg_count * sizeof (unsigned short);
			iovecs[2].iov_base = &fragmentation_data[f_i];
			iovecs[2].iov_len = max_packet_size;

			f_i += max_packet_size;
/*
 * Ensure maximum message size is being queued
 */
for (goober = 0, j = 0; j < 3; j++) {
	goober += iovecs[j].iov_len;
}
//assert (goober == 1408);

for (i = 0; i < mcast_packed_msg_count; i++) {
}
			res = totemsrp_mcast (iovecs, 3, guarantee);

			remaining_size = fragment_size - f_i;
			fragment_index += max_packet_size;
			mcast_packed_msg_count = 1;
			max_packet_size = TOTEMPG_PACKET_SIZE -
				(sizeof (unsigned short) * 1);
			mcast_packed_msg_lens[0] = max_packet_size;
		}
		/*
		 * Copy remaining fragmented data
		 */
		assert (remaining_size >= 0);
		if (remaining_size > 0) {
			memmove (&fragmentation_data[0],
				&fragmentation_data[fragment_size - remaining_size],
				remaining_size);

			mcast_packed_msg_lens[0] = remaining_size;
			mcast_packed_msg_count = 1;
			fragment_size = remaining_size;
		} else {
			mcast_packed_msg_count = 0;
			fragment_size = 0;
		}
	} else {
		mcast_packed_msg_lens[mcast_packed_msg_count] = copy_len;
		mcast_packed_msg_count++;
	}

	return (res);
}


/*
 * Determine if a message of msg_size could be queued
 */
int totempg_send_ok (
	int priority,
	int msg_size)
{
	int avail = 0;
	avail = totemsrp_avail ();

	return (avail > 200);
}
