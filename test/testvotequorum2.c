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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <corosync/corotypes.h>
#include <corosync/votequorum.h>

static votequorum_handle_t handle;


static void print_info(int ok_to_fail)
{
	struct votequorum_qdisk_info qinfo;
	int err;

	if ( (err=votequorum_qdisk_getinfo(handle, &qinfo)) != CS_OK)
		fprintf(stderr, "votequorum_qdisk_getinfo error %d: %s\n", err, ok_to_fail?"OK":"FAILED");
	else {
		printf("qdisk votes  %d\n", qinfo.votes);
		printf("state        %d\n", qinfo.state);
		printf("name         %s\n", qinfo.name);
		printf("\n");
	}
}

int main(int argc, char *argv[])
{
	int pollcount=0, polltime=1;
	int err;

	if ( (err=votequorum_initialize(&handle, NULL)) != CS_OK) {
		fprintf(stderr, "votequorum_initialize FAILED: %d\n", err);
		return -1;
	}

	print_info(1);

	if (argc >= 2 && atoi(argv[1])) {
		pollcount = atoi(argv[1]);
	}
	if (argc >= 3 && atoi(argv[2])) {
		polltime = atoi(argv[2]);
	}

	if (argc >= 2) {
		if ( (err=votequorum_qdisk_register(handle, "QDISK", 4)) != CS_OK)
			fprintf(stderr, "qdisk_register FAILED: %d\n", err);

		while (pollcount--) {
			print_info(0);
			if ((err=votequorum_qdisk_poll(handle, 1)) != CS_OK)
				fprintf(stderr, "qdisk poll FAILED: %d\n", err);
			print_info(0);
			sleep(polltime);
		}
		if ((err= votequorum_qdisk_unregister(handle)) != CS_OK)
			fprintf(stderr, "qdisk unregister FAILED: %d\n", err);
	}
	print_info(1);

	return 0;
}
