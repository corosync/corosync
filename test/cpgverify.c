/*
 * Copyright (c) 2009 Red Hat, Inc.
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <corosync/corotypes.h>
#include <corosync/cpg.h>

#include <zlib.h>

struct my_msg {
	unsigned int msg_size;
	unsigned char crc32[4];
	unsigned char buffer[0];
};

static int deliveries = 0;

static void cpg_deliver_fn (
        cpg_handle_t handle,
        const struct cpg_name *group_name,
        uint32_t nodeid,
        uint32_t pid,
        void *m,
        size_t msg_len)
{
	const struct my_msg *msg2 = m;
	uLong chsum;
	uint32_t nchsum;

	printf ("msg '%s'\n", msg2->buffer);

	chsum = crc32(0L, Z_NULL, 0);
	chsum = crc32(chsum, msg2->buffer, msg2->msg_size) & 0xFFFFFFFF;

	printf ("SIZE %d HASH: 0x%08"PRIx32"\n", msg2->msg_size, (uint32_t)chsum);

	nchsum = htonl((uint32_t)chsum);

	if (memcmp(&nchsum, msg2->crc32, sizeof(nchsum)) != 0) {
		printf ("incorrect hash\n");
		exit (1);
	}
	deliveries++;
}

static void cpg_confchg_fn (
        cpg_handle_t handle,
        const struct cpg_name *group_name,
        const struct cpg_address *member_list, size_t member_list_entries,
        const struct cpg_address *left_list, size_t left_list_entries,
        const struct cpg_address *joined_list, size_t joined_list_entries)
{
}

static cpg_callbacks_t callbacks = {
	cpg_deliver_fn,
	cpg_confchg_fn
};

static struct cpg_name group_name = {
        .value = "cpg_bm",
        .length = 6
};


static unsigned char buffer[200000];
int main (int argc, char *argv[])
{
	cpg_handle_t handle;
	cs_error_t result;
	int i = 0;
	int j;
	struct my_msg msg;
	struct iovec iov[2];
	const char *options = "i:";
	int iter = 1000;
	int opt;
	int run_forever = 1;
	uLong chsum;
	uint32_t nchsum;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'i':
			run_forever = 0;
			iter = atoi(optarg);
			break;
		}
	}

	result = cpg_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		printf ("Couldn't initialize CPG service %d\n", result);
		exit (0);
	}

        result = cpg_join (handle, &group_name);
        if (result != CS_OK) {
                printf ("cpg_join failed with result %d\n", result);
                exit (1);
        }

	iov[0].iov_base = (void *)&msg;
	iov[0].iov_len = sizeof (struct my_msg);
	iov[1].iov_base = (void *)buffer;

	/*
	 * Demonstrate cpg_mcast_joined
	 */
	i = 0;
	do {
		msg.msg_size = 100 + rand() % 100000;
		iov[1].iov_len = msg.msg_size;
		for (j = 0; j < msg.msg_size; j++) {
			buffer[j] = j;
		}
		sprintf ((char *)buffer,
			"cpg_mcast_joined: This is message %12d", i);

		chsum = crc32(0L, Z_NULL, 0);
		chsum = crc32(chsum, buffer, msg.msg_size) & 0xFFFFFFFF;
		nchsum = htonl((uint32_t)chsum);
		memcpy(msg.crc32, &nchsum, sizeof(nchsum)) ;
try_again_one:
		result = cpg_mcast_joined (handle, CPG_TYPE_AGREED,
			iov, 2);
		if (result == CS_ERR_TRY_AGAIN) {
			goto try_again_one;
		}
		result = cpg_dispatch (handle, CS_DISPATCH_ALL);
		if (result != CS_OK && result != CS_ERR_TRY_AGAIN) {
			printf("cpg_dispatch failed with result %d\n", result);
			exit(1);
		}
		i++;
	} while (run_forever || i < iter);

	cpg_finalize (handle);

	return (0);
}
