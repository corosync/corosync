/*
 * Copyright (c) 2004 MontaVista Software, Inc.
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
#include <pthread.h>
#include "../include/evs.h"

char *delivery_string;

#define CALLBACKS 200000
int callback_count = 0;
void evs_deliver_fn (struct in_addr source_addr, const void *msg, size_t msg_len)
{
#ifdef PRINT_OUTPUT
	char *buf;
	buf += 100000;
	printf ("Delivery callback\n");
	printf ("callback %d '%s' msg '%s'\n", callback_count, delivery_string, buf);
#endif
	callback_count += 1;
	if (callback_count % 50 == 0) {
		printf ("Callback %d\n", callback_count);
	}
}

void evs_confchg_fn (
	const struct in_addr *member_list, size_t member_list_entries,
	const struct in_addr *left_list, size_t left_list_entries,
	const struct in_addr *joined_list, size_t joined_list_entries)
{
	int i;

	printf ("CONFIGURATION CHANGE\n");
	printf ("--------------------\n");
	printf ("New configuration\n");
	for (i = 0; i < member_list_entries; i++) {
		printf ("%s\n", inet_ntoa (member_list[i]));
	}
	printf ("Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
		printf ("%s\n", inet_ntoa (left_list[i]));
	}
	printf ("Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
		printf ("%s\n", inet_ntoa (joined_list[i]));
	}
}

evs_callbacks_t callbacks = {
	evs_deliver_fn,
	evs_confchg_fn
};

struct evs_group groups[3] = {
	{ "key1" },
	{ "key2" },
	{ "key3" }
};

char buffer[1000];
struct iovec iov = {
	.iov_base = buffer,
	.iov_len = sizeof (buffer)
};

void *th_dispatch (void *arg)
{
	evs_error_t result;
	evs_handle_t handle = *(evs_handle_t *)arg;

	printf ("THREAD DISPATCH starting.\n");
	result = evs_dispatch (handle, EVS_DISPATCH_BLOCKING);
	printf ("THREAD DISPATCH return result is %d\n", result);
	return (0);
}

static struct sched_param sched_param = {
    sched_priority: 99
};

int main (void)
{
	evs_handle_t handle;
	evs_error_t result;
	int i = 0;
	pthread_t dispatch_thread;
	pthread_attr_t dispatch_thread_attribute;

	result = evs_initialize (&handle, &callbacks);
	if (result != EVS_OK) {
		printf ("Couldn't initialize EVS service %d\n", result);
		exit (0);
	}

        pthread_attr_init (&dispatch_thread_attribute);
        pthread_attr_setschedpolicy (&dispatch_thread_attribute, SCHED_FIFO);
        pthread_attr_setschedparam (&dispatch_thread_attribute, &sched_param);

        pthread_create (&dispatch_thread, NULL, th_dispatch, &handle);

	printf ("Init result %d\n", result);
	result = evs_join (handle, groups, 3);
	printf ("Join result %d\n", result);
	result = evs_leave (handle, &groups[0], 1);
	printf ("Leave result %d\n", result);
	delivery_string = "evs_mcast_joined";

	/*
	 * Demonstrate evs_mcast_joined
	 */
	for (i = 0; i < CALLBACKS/2; i++) {
		sprintf (buffer, "evs_mcast_joined: This is message %d", i);
try_again_one:
		result = evs_mcast_joined (handle, EVS_TYPE_AGREED, &iov, 1);
		if (result == EVS_ERR_TRY_AGAIN) {
			goto try_again_one;
		} else
		if (result != EVS_OK) {
			printf ("Got error result, exiting %d\n", result);
			exit (1);
		}
	}

	/*
	 * Demonstrate evs_mcast_joined
	 */
	delivery_string = "evs_mcast_groups";
	for (i = 0; i < CALLBACKS/2; i++) {
		sprintf (buffer, "evs_mcast_groups: This is message %d", i);
try_again_two:
		result = evs_mcast_groups (handle, EVS_TYPE_AGREED,
			 &groups[1], 1, &iov, 1);
		if (result == EVS_ERR_TRY_AGAIN) {
			goto try_again_two;
		}
	}

	/*
	 * Wait until all callbacks have been executed by dispatch thread
	 */
	for (;;) {
		if (callback_count == CALLBACKS) {
		printf ("Test completed successfully\n");
			exit (0);
		}
	}
	return (0);
}
