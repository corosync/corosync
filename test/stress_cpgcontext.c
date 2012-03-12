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

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <corosync/corotypes.h>
#include <corosync/cpg.h>
#include <signal.h>

struct my_msg {
	unsigned int msg_size;
	unsigned char sha1[20];
	unsigned char buffer[0];
};

static void cpg_deliver_fn (
        cpg_handle_t handle,
        const struct cpg_name *group_name,
        uint32_t nodeid,
        uint32_t pid,
        void *m,
        size_t msg_len)
{
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

static void sigintr_handler (int num)
{
	exit (1);
}

#define ITERATIONS (1000000)

#define INSTANCES 100

int main (void)
{
	cpg_handle_t handle[INSTANCES];
	cs_error_t res;
	void *context[INSTANCES];
	int i, j;
	void *ctx;

	signal (SIGINT, sigintr_handler);
	for (i = 0; i < INSTANCES; i++) {
		res = cpg_initialize (&handle[i], &callbacks);
		if (res != CS_OK) {
			printf ("FAIL %d\n", res);
			exit (-1);
		}
	}

	for (j = 0; j < ITERATIONS; j++) {
		for (i = 0; i < INSTANCES; i++) {
			context[i] = malloc (20);
			res = cpg_context_set (handle[i], context[i]);
			if (res != CS_OK) {
				printf ("FAIL %d\n", res);
				exit (-1);
			}
		}

		for (i = 0; i < INSTANCES; i++) {
			res = cpg_context_get (handle[i], &ctx);
			if (res != CS_OK) {
				printf ("FAIL %d\n", res);
				exit (-1);
			}
			if (ctx != context[i]) {
				printf ("FAIL\n");
				exit (-1);
			}
			free (ctx);
		}
	}
	
	for (i = 0; i < INSTANCES; i++) {
		cpg_finalize (handle[i]);
	}

	printf ("PASS\n");
	return (0);
}
