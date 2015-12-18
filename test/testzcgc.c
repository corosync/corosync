/*
 * Copyright (c) 2006-2009 Red Hat Inc
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield <ccaulfie@redhat.com>
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
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <corosync/corotypes.h>
#include <corosync/cpg.h>

static int quit = 0;
static int show_ip = 0;

static void print_cpgname (const struct cpg_name *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

static void DeliverCallback (
	cpg_handle_t handle,
	const struct cpg_name *groupName,
	uint32_t nodeid,
	uint32_t pid,
	void *msg,
	size_t msg_len)
{
	if (show_ip) {
		struct in_addr saddr;
		saddr.s_addr = nodeid;
		printf("DeliverCallback: message (len=%lu)from node/pid %s/%d: '%s'\n",
		       (unsigned long int) msg_len,
		       inet_ntoa(saddr), pid, (const char *)msg);
	}
	else {
		printf("DeliverCallback: message (len=%lu)from node/pid %d/%d: '%s'\n",
		       (unsigned long int) msg_len, nodeid, pid,
		       (const char *)msg);
	}
}

static void ConfchgCallback (
	cpg_handle_t handle,
	const struct cpg_name *groupName,
	const struct cpg_address *member_list, size_t member_list_entries,
	const struct cpg_address *left_list, size_t left_list_entries,
	const struct cpg_address *joined_list, size_t joined_list_entries)
{
	int i;
	struct in_addr saddr;

	printf("\nConfchgCallback: group '");
	print_cpgname(groupName);
	printf("'\n");
	for (i=0; i<joined_list_entries; i++) {
		if (show_ip) {
			saddr.s_addr = joined_list[i].nodeid;
			printf("joined node/pid: %s/%d reason: %d\n",
			       inet_ntoa (saddr), joined_list[i].pid,
			       joined_list[i].reason);
		}
		else {
			printf("joined node/pid: %d/%d reason: %d\n",
			       joined_list[i].nodeid, joined_list[i].pid,
			       joined_list[i].reason);
		}
	}

	for (i=0; i<left_list_entries; i++) {
		if (show_ip) {
			saddr.s_addr = left_list[i].nodeid;
			printf("left node/pid: %s/%d reason: %d\n",
			       inet_ntoa (saddr), left_list[i].pid,
			       left_list[i].reason);
		}
		else {
			printf("left node/pid: %d/%d reason: %d\n",
			       left_list[i].nodeid, left_list[i].pid,
			       left_list[i].reason);
		}
	}

	printf("nodes in group now %lu\n",
	       (unsigned long int) member_list_entries);
	for (i=0; i<member_list_entries; i++) {
		if (show_ip) {
			saddr.s_addr = member_list[i].nodeid;
			printf("node/pid: %s/%d\n",
			       inet_ntoa (saddr), member_list[i].pid);
		}
		else {
			printf("node/pid: %d/%d\n",
			       member_list[i].nodeid, member_list[i].pid);
		}
	}

	/* Is it us??
	   NOTE: in reality we should also check the nodeid */
	if (left_list_entries && left_list[0].pid == getpid()) {
		printf("We have left the building\n");
		quit = 1;
	}
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn =            DeliverCallback,
	.cpg_confchg_fn =            ConfchgCallback,
};

static struct cpg_name group_name;

int main (int argc, char *argv[]) {
	cpg_handle_t handle;
	int result;
	void *buffer;
	unsigned int i;

	strcpy(group_name.value, "GROUP");
	group_name.length = 6;

	result = cpg_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		printf ("Could not initialize Cluster Process Group API instance error %d\n", result);
		exit (1);
	}
	for (i = 0; i < 100; i++) {
		cpg_zcb_alloc (handle, 1024*1024, &buffer);
	}
	return (0);
}
