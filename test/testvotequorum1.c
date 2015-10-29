/*
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
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

#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <corosync/corotypes.h>
#include <corosync/votequorum.h>

static votequorum_handle_t g_handle;

static const char *node_state(int state)
{
	switch (state) {
	case VOTEQUORUM_NODESTATE_MEMBER:
		return "Member";
		break;
	case VOTEQUORUM_NODESTATE_DEAD:
		return "Dead";
		break;
	case VOTEQUORUM_NODESTATE_LEAVING:
		return "Leaving";
		break;
	default:
		return "UNKNOWN";
		break;
	}
}


static void votequorum_expectedvotes_notification_fn(
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t expected_votes
	)
{
	printf("votequorum expectedvotes notification called \n");
	printf("  expected_votes = %d\n", expected_votes);
}


static void votequorum_quorum_notification_fn(
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t quorate,
	uint32_t node_list_entries,
	votequorum_node_t node_list[]
	)
{
	int i;

	printf("votequorum quorum notification called \n");
	printf("  number of nodes = %d\n", node_list_entries);
	printf("  quorate         = %d\n", quorate);

	for (i = 0; i< node_list_entries; i++) {
		printf("      %d: %s\n", node_list[i].nodeid, node_state(node_list[i].state));
	}

}

static void votequorum_nodelist_notification_fn(
	votequorum_handle_t handle,
	uint64_t context,
	votequorum_ring_id_t ring_id,
	uint32_t node_list_entries,
	uint32_t node_list[]
	)
{
	int i;

	printf("votequorum nodelist notification called \n");
	printf("  number of nodes = %d\n", node_list_entries);
	printf("  current ringid  = (%u.%"PRIu64")\n", ring_id.nodeid, ring_id.seq);
	printf("  nodes: ");

	for (i = 0; i< node_list_entries; i++) {
		printf("%d ", node_list[i]);
	}
	printf("\n\n");
}


int main(int argc, char *argv[])
{
	struct votequorum_info info;
	votequorum_callbacks_t callbacks;
	int err;

	if (argc > 1 && strcmp(argv[1], "-h")==0) {
		fprintf(stderr, "usage: %s [new-expected] [new-votes]\n", argv[0]);
		return 0;
	}

	callbacks.votequorum_quorum_notify_fn = votequorum_quorum_notification_fn;
	callbacks.votequorum_nodelist_notify_fn = votequorum_nodelist_notification_fn;
	callbacks.votequorum_expectedvotes_notify_fn = votequorum_expectedvotes_notification_fn;

	if ( (err=votequorum_initialize(&g_handle, &callbacks)) != CS_OK)
		fprintf(stderr, "votequorum_initialize FAILED: %d\n", err);

	if ( (err = votequorum_trackstart(g_handle, g_handle, CS_TRACK_CHANGES)) != CS_OK)
		fprintf(stderr, "votequorum_trackstart FAILED: %d\n", err);

	if ( (err=votequorum_getinfo(g_handle, 0, &info)) != CS_OK)
		fprintf(stderr, "votequorum_getinfo FAILED: %d\n", err);
	else {
		printf("node votes       %d\n", info.node_votes);
		printf("expected votes   %d\n", info.node_expected_votes);
		printf("highest expected %d\n", info.highest_expected);
		printf("total votes      %d\n", info.total_votes);
		printf("quorum           %d\n", info.quorum);
		printf("flags            ");
		if (info.flags & VOTEQUORUM_INFO_TWONODE) printf("2Node ");
		if (info.flags & VOTEQUORUM_INFO_QUORATE) printf("Quorate ");
		if (info.flags & VOTEQUORUM_INFO_WAIT_FOR_ALL) printf("WaitForAll ");
		if (info.flags & VOTEQUORUM_INFO_LAST_MAN_STANDING) printf("LastManStanding ");
		if (info.flags & VOTEQUORUM_INFO_AUTO_TIE_BREAKER) printf("AutoTieBreaker ");
		if (info.flags & VOTEQUORUM_INFO_ALLOW_DOWNSCALE) printf("AllowDownscale ");

		printf("\n");
	}

	if (argc >= 2 && atoi(argv[1])) {
		if ( (err=votequorum_setexpected(g_handle, atoi(argv[1]))) != CS_OK)
			fprintf(stderr, "set expected votes FAILED: %d\n", err);
	}
	if (argc >= 3 && atoi(argv[2])) {
		if ( (err=votequorum_setvotes(g_handle, 0, atoi(argv[2]))) != CS_OK)
			fprintf(stderr, "set votes FAILED: %d\n", err);
	}

	if (argc >= 2) {
		if ( (err=votequorum_getinfo(g_handle, 0, &info)) != CS_OK)
			fprintf(stderr, "votequorum_getinfo2 FAILED: %d\n", err);
		else {
			printf("-------------------\n");
			printf("node votes       %d\n", info.node_votes);
			printf("expected votes   %d\n", info.node_expected_votes);
			printf("highest expected %d\n", info.highest_expected);
			printf("total votes      %d\n", info.total_votes);
			printf("votequorum           %d\n", info.quorum);
			printf("flags            ");
			if (info.flags & VOTEQUORUM_INFO_TWONODE) printf("2Node ");
			if (info.flags & VOTEQUORUM_INFO_QUORATE) printf("Quorate ");
			if (info.flags & VOTEQUORUM_INFO_WAIT_FOR_ALL) printf("WaitForAll ");
			if (info.flags & VOTEQUORUM_INFO_LAST_MAN_STANDING) printf("LastManStanding ");
			if (info.flags & VOTEQUORUM_INFO_AUTO_TIE_BREAKER) printf("AutoTieBreaker ");
			if (info.flags & VOTEQUORUM_INFO_ALLOW_DOWNSCALE) printf("AllowDownscale ");
			printf("\n");
		}
	}

	printf("Waiting for votequorum events, press ^C to finish\n");
	printf("-------------------\n");

	while (1) {
		if (votequorum_dispatch(g_handle, CS_DISPATCH_ALL) != CS_OK) {
			fprintf(stderr, "votequorum_dispatch error\n");
			return -1;
		}
	}

	return 0;
}
