/*
 * Copyright (c) 2009-2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse <jfriesse@redhat.com>
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
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

#include <corosync/corotypes.h>
#include <corosync/totem/totem.h>
#include <corosync/cfg.h>
#include <corosync/cpg.h>

static corosync_cfg_handle_t cfg_handle;
static cpg_handle_t cpg_handle;

typedef enum {
	OPER_NAMES_ONLY = 1,
	OPER_FULL_OUTPUT = 2,
} operation_t;

static void fprint_addrs(FILE *f, int nodeid)
{
	int numaddrs;
	int i;
	corosync_cfg_node_address_t addrs[INTERFACE_MAX];

	if (corosync_cfg_get_node_addrs(cfg_handle, nodeid, INTERFACE_MAX, &numaddrs, addrs) == CS_OK) {
		for (i=0; i<numaddrs; i++) {
			char buf[INET6_ADDRSTRLEN];
			struct sockaddr_storage *ss = (struct sockaddr_storage *)addrs[i].address;
			struct sockaddr_in *sin = (struct sockaddr_in *)addrs[i].address;
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addrs[i].address;
			void *saddr;

			if (ss->ss_family == AF_INET6)
				saddr = &sin6->sin6_addr;
			else
				saddr = &sin->sin_addr;

			inet_ntop(ss->ss_family, saddr, buf, sizeof(buf));
			if (i != 0) {
				fprintf(f, " ");
			}
			fprintf(f, "%s", buf);
		}
	}
}

static void fprint_group (FILE *f, int escape, const struct cpg_name *group) {
	int i;
	char c;

	for (i = 0; i < group->length; i++) {
		c = group->value[i];

		if (!escape || (c >= ' ' && c < 0x7f && c != '\\')) {
			fputc (c, f);
		} else {
			if (c == '\\')
				fprintf (f, "\\\\");
			else
				fprintf (f, "\\x%02X", c);
		}
	}
}

static int display_groups (char delimiter, int escape)
{
	cs_error_t res;
	cpg_iteration_handle_t iter_handle;
	struct cpg_iteration_description_t description;

	res = cpg_iteration_initialize (cpg_handle, CPG_ITERATION_NAME_ONLY, NULL, &iter_handle);
	if (res != CS_OK) {
		fprintf (stderr, "Cannot initialize cpg iterator error %d\n", res);

		return 0;
	}

	while ((res = cpg_iteration_next (iter_handle, &description)) == CS_OK) {
		fprint_group (stdout, escape, &description.group);
		fputc ((delimiter ? delimiter : '\n'), stdout);
	}

	if (delimiter)
		putc ('\n', stdout);

	cpg_iteration_finalize (iter_handle);

	return 1;
}

static inline int group_name_compare (
	const struct cpg_name *g1,
	const struct cpg_name *g2)
{
	return (g1->length == g2->length?
		memcmp (g1->value, g2->value, g1->length):
		g1->length - g2->length);
}

static int display_groups_with_members (char delimiter, int escape) {
	cs_error_t res;
	cpg_iteration_handle_t iter_handle;
	struct cpg_iteration_description_t description;
	struct cpg_name old_group;

	res = cpg_iteration_initialize (cpg_handle, CPG_ITERATION_ALL, NULL, &iter_handle);
	if (res != CS_OK) {
		fprintf (stderr, "Cannot initialize cpg iterator error %d\n", res);

		return 0;
	}

	memset (&old_group, 0, sizeof (struct cpg_name));

	if (delimiter) {
		fprintf (stdout, "GRP_NAME%cPID%cNODEID\n", delimiter, delimiter);
	} else {
		fprintf (stdout, "Group Name\t%10s\t%10s\n", "PID", "Node ID");
	}

	while ((res = cpg_iteration_next (iter_handle, &description)) == CS_OK) {
		if (!delimiter && group_name_compare (&old_group, &description.group) != 0) {
			fprint_group (stdout, escape, &description.group);
			fprintf (stdout, "\n");

			memcpy (&old_group, &description.group, sizeof (struct cpg_name));
		}

		if (!delimiter) {
			fprintf (stdout, "\t\t%10u\t%10u (", description.pid, description.nodeid);
			fprint_addrs (stdout, description.nodeid);
			fprintf (stdout, ")\n");
		} else {
			fprint_group (stdout, escape, &description.group);
			fprintf (stdout, "%c%u%c%u\n", delimiter, description.pid, delimiter, description.nodeid);
		}
	}

	if (res != CS_ERR_NO_SECTIONS) {
		fprintf (stderr, "cpg iteration error %d\n", res);

		return 0;
	}

	cpg_iteration_finalize (iter_handle);

	return 1;
}

static void usage_do (const char *prog_name)
{
	printf ("%s [-d delimiter] [-e] [-n] [-h]\n\n", prog_name);
	printf ("A tool for displaying cpg groups and members.\n");
	printf ("options:\n");
	printf ("\t-d\tDelimiter between fields.\n");
	printf ("\t-e\tDon't escape unprintable characters in group name.\n");
	printf ("\t-n\tDisplay only all existing group names.\n");
	printf ("\t-h\tDisplay this help.\n");
}


int main (int argc, char *argv[]) {
	const char *options = "hd:ne";
	int opt;
	const char *prog_name = basename(argv[0]);
	char delimiter = 0;
	int escape = 1;
	operation_t operation = OPER_FULL_OUTPUT;
	int result;

	while ( (opt = getopt(argc, argv, options)) != -1 ) {
		switch (opt) {
		case 'd':
			if (strlen (optarg) > 0) {
				delimiter = optarg[0];
			}
		break;

		case 'n':
			operation = OPER_NAMES_ONLY;
		break;

		case 'e':
			escape = 0;
		break;

		case 'h':
			usage_do (prog_name);
			return (EXIT_SUCCESS);
		break;

		case '?':
		case ':':
			return (EXIT_FAILURE);
		break;
		}
	}

	result = cpg_initialize (&cpg_handle, NULL);

	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync cpg API error %d\n", result);
		return (EXIT_FAILURE);
	}

	result = corosync_cfg_initialize (&cfg_handle, NULL);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %d\n", result);
		return (EXIT_FAILURE);
	}

	switch (operation) {
	case OPER_NAMES_ONLY:
		result = display_groups (delimiter, escape);
		break;

	case OPER_FULL_OUTPUT:
		result = display_groups_with_members (delimiter, escape);
		break;
	}

	cpg_finalize (cpg_handle);
	corosync_cfg_finalize (cfg_handle);

	return (result ? EXIT_SUCCESS : EXIT_FAILURE);
}
