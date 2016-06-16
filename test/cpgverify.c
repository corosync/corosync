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
#include <corosync/corotypes.h>
#include <corosync/cpg.h>

#include <nss.h>
#include <pk11pub.h>

struct my_msg {
	unsigned int msg_size;
	unsigned char sha1[20];
	unsigned char buffer[0];
};

static int deliveries = 0;
PK11Context* sha1_context;

static void cpg_deliver_fn (
        cpg_handle_t handle,
        const struct cpg_name *group_name,
        uint32_t nodeid,
        uint32_t pid,
        void *m,
        size_t msg_len)
{
	const struct my_msg *msg2 = m;
	unsigned char sha1_compare[20];
	unsigned int i;
	unsigned int sha1_len;

	printf ("msg '%s'\n", msg2->buffer);
	PK11_DigestBegin(sha1_context);
	PK11_DigestOp(sha1_context, msg2->buffer, msg2->msg_size);
	PK11_DigestFinal(sha1_context, sha1_compare, &sha1_len, sizeof(sha1_compare));
printf ("SIZE %d HASH: ", msg2->msg_size);
for (i = 0; i < 20; i++) {
printf ("%x", sha1_compare[i]);
}
printf ("\n");
	if (memcmp (sha1_compare, msg2->sha1, 20) != 0) {
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
	unsigned int sha1_len;

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

	if (NSS_NoDB_Init(".") != SECSuccess) {
		printf ("Couldn't initialize nss\n");
		exit (0);
	}

	if ((sha1_context = PK11_CreateDigestContext(SEC_OID_SHA1)) == NULL) {
		printf ("Couldn't initialize nss\n");
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

		PK11_DigestBegin(sha1_context);
		PK11_DigestOp(sha1_context, buffer, msg.msg_size);
		PK11_DigestFinal(sha1_context, msg.sha1, &sha1_len, sizeof(msg.sha1));
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

	PK11_DestroyContext(sha1_context, PR_TRUE);

	cpg_finalize (handle);

	return (0);
}
