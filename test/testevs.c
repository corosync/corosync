/*
 * Copyright (c) 2004 MontaVista Software, Inc.
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <corosync/corotypes.h>
#include <corosync/evs.h>

static const char *delivery_string;

static int deliveries = 0;
static void evs_deliver_fn (
	hdb_handle_t handle,
	unsigned int nodeid,
	const void *msg,
	size_t msg_len)
{
	const char *buf = msg;

	printf ("API '%s' msg '%s'\n", delivery_string, buf);
	deliveries++;
}

static void evs_confchg_fn (
	hdb_handle_t handle,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct evs_ring_id *ring_id)
{
	int i;

	printf ("CONFIGURATION CHANGE\n");
	printf ("--------------------\n");
	printf ("New configuration\n");
	for (i = 0; i < member_list_entries; i++) {
                printf ("%x\n", member_list[i]);
	}
	printf ("Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
                printf ("%x\n", left_list[i]);
	}
	printf ("Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
                printf ("%x\n", joined_list[i]);
	}
}

static evs_callbacks_t callbacks = {
	evs_deliver_fn,
	evs_confchg_fn
};

static struct evs_group groups[3] = {
	{ "key1" },
	{ "key2" },
	{ "key3" }
};

static char buffer[2000];
struct iovec iov = {
	.iov_base = buffer,
	.iov_len = sizeof (buffer)
};

int main (void)
{
	evs_handle_t handle;
	cs_error_t result;
	int i = 0;
	int fd;
	unsigned int member_list[32];
	unsigned int local_nodeid;
	size_t member_list_entries = 32;

	result = evs_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		printf ("Couldn't initialize EVS service %d\n", result);
		exit (0);
	}

	result = evs_membership_get (handle, &local_nodeid,
		member_list, &member_list_entries);
	printf ("Current membership from evs_membership_get entries %lu\n",
		(unsigned long int) member_list_entries);
	for (i = 0; i < member_list_entries; i++) {
		printf ("member [%d] is %x\n", i, member_list[i]);
	}
	printf ("local processor from evs_membership_get %x\n", local_nodeid);

	printf ("Init result %d\n", result);
	result = evs_join (handle, groups, 3);
	printf ("Join result %d\n", result);
	delivery_string = "evs_mcast_joined";

	/*
	 * Demonstrate evs_mcast_joined
	 */
	for (i = 0; i < 500; i++) {
		sprintf (buffer, "evs_mcast_joined: This is message %d", i);
#ifdef COMPILE_OUT
		sprintf (buffer,
		"%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d",
			i, i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,
			i, i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i,  i);
#endif
try_again_one:
		result = evs_mcast_joined (handle, EVS_TYPE_AGREED,
			&iov, 1);
		if (result == CS_ERR_TRY_AGAIN) {
			goto try_again_one;
		}
		result = evs_dispatch (handle, CS_DISPATCH_ALL);
	}

	do {
		result = evs_dispatch (handle, CS_DISPATCH_ALL);
	} while (deliveries < 20);
	/*
	 * Demonstrate evs_mcast_joined
	 */
	delivery_string = "evs_mcast_groups";
	for (i = 0; i < 500; i++) {
		sprintf (buffer, "evs_mcast_groups: This is message %d", i);
try_again_two:
		result = evs_mcast_groups (handle, EVS_TYPE_AGREED,
			 &groups[1], 1, &iov, 1);
		if (result == CS_ERR_TRY_AGAIN) {
			goto try_again_two;
		}

		result = evs_dispatch (handle, CS_DISPATCH_ALL);
	}
	/*
	 * Flush any pending callbacks
	 */
	do {
		result = evs_dispatch (handle, CS_DISPATCH_ALL);
	} while (deliveries < 500);

	evs_fd_get (handle, &fd);

	evs_finalize (handle);

	return (0);
}
