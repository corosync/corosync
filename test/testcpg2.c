/*
 * Copyright (c) 2007, 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Alan Conway <aconway@redhat.com>
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

#include <assert.h>
#include <stdio.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <corosync/corotypes.h>
#include <corosync/cpg.h>

static void deliver(
	cpg_handle_t handle,
	const struct cpg_name *group_name,
	uint32_t nodeid,
	uint32_t pid,
	void *msg,
	size_t msg_len)
{
    printf("self delivered nodeid: %x\n", nodeid);
}

static void confch(
	cpg_handle_t handle,
	const struct cpg_name *group_name,
	const struct cpg_address *member_list, size_t member_list_entries,
	const struct cpg_address *left_list, size_t left_list_entries,
	const struct cpg_address *joined_list, size_t joined_list_entries)
{
	printf("confchg nodeid %x\n", member_list[0].nodeid);
}

int main(int argc, char** argv) {
	cpg_handle_t handle=0;
	cpg_callbacks_t cb={&deliver,&confch};
	unsigned int nodeid=0;
	struct cpg_name group={3,"foo"};
	struct iovec msg={(void *)"hello", 5}; /* discard const */

	struct pollfd pfd;
	int fd;

	printf ("All of the nodeids should match on a single node configuration\n for the test to pass.");
	assert(CS_OK==cpg_initialize(&handle, &cb));
	assert(CS_OK==cpg_local_get(handle,&nodeid));
	printf("local_get: %x\n", nodeid);
	assert(CS_OK==cpg_join(handle, &group));
	assert(CS_OK==cpg_mcast_joined(handle,CPG_TYPE_AGREED,&msg,1));
	cpg_fd_get (handle, &fd);
	pfd.fd = fd;
	pfd.events = POLLIN;

	assert(poll (&pfd, 1, 1000) == 1);
	cpg_dispatch(handle, CS_DISPATCH_ALL);
	return (0);
}
