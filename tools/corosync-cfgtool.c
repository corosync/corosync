/*
 * Copyright (c) 2006-2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake <sdake@redhat.com>
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

#include <corosync/corotypes.h>
#include <corosync/totem/totem.h>
#include <corosync/cfg.h>

#define cs_repeat(result, max, code)				\
	do {							\
		int counter = 0;				\
		do {						\
			result = code;				\
			if (result == CS_ERR_TRY_AGAIN) {	\
				sleep(1);			\
				counter++;			\
			} else {				\
				break;				\
			}					\
		} while (counter < max);			\
	} while (0)

enum user_action {
	ACTION_NOOP=0,
	ACTION_LINKSTATUS_GET,
	ACTION_RELOAD_CONFIG,
	ACTION_SHUTDOW,
	ACTION_SHOWADDR,
	ACTION_KILL_NODE,
};

static int
linkstatusget_do (char *interface_name, int brief)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	unsigned int interface_count;
	char **interface_names;
	char **interface_status;
	unsigned int i;
	unsigned int nodeid;
	int rc = 0;
	int len, s = 0, t;

	printf ("Printing link status.\n");
	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		printf ("Could not initialize corosync configuration API error %d\n", result);
		exit (1);
	}

	result = corosync_cfg_local_get(handle, &nodeid);
	if (result != CS_OK) {
		printf ("Could not get the local node id, the error is: %d\n", result);
	}
	else {
		printf ("Local node ID %u\n", nodeid);
	}

	result = corosync_cfg_ring_status_get (handle,
				&interface_names,
				&interface_status,
				&interface_count);
	if (result != CS_OK) {
		printf ("Could not get the link status, the error is: %d\n", result);
	} else {
		for (i = 0; i < interface_count; i++) {
			s = 0;
			if ( (interface_name &&
			      interface_names[i][0] != '\0' &&
			      (interface_name[0]=='\0' ||
				strcasecmp (interface_name, interface_names[i]) == 0)) ||
				!interface_name ) {

				printf ("LINK ID %c\n", interface_names[i][0]);
				printf ("\taddr\t= %s\n", interface_names[i]+1);
				if((!brief) && (strcmp(interface_status[i], "OK") != 0) &&
					(!strstr(interface_status[i], "FAULTY"))) {
					len = strlen(interface_status[i]);
					printf ("\tstatus:\n");
					while(s < len) {
						t = interface_status[i][s] - '0';
						printf("\t\tnode %d:\t", s++);
						printf("link enabled:%d\t", t&1? 1 : 0);
						printf("link connected:%d\n", t&2? 1: 0);
					}
				} else {
					printf ("\tstatus\t= %s\n", interface_status[i]);
					if (strstr(interface_status[i], "FAULTY")) {
						rc = 1;
					}
				}
			}
		}

		for (i = 0; i < interface_count; i++) {
			free(interface_status[i]);
			free(interface_names[i]);
		}
		free(interface_status);
		free(interface_names);
	}

	(void)corosync_cfg_finalize (handle);
	return rc;
}

static int reload_config_do (void)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	int rc;

	rc = 0;

	printf ("Reloading corosync.conf...\n");
	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		printf ("Could not initialize corosync configuration API error %s\n", cs_strerror(result));
		exit (1);
	}

	result = corosync_cfg_reload_config (handle);
	if (result != CS_OK) {
		printf ("Could not reload configuration. Error %s\n", cs_strerror(result));
		rc = (int)result;
	}
	else {
		printf ("Done\n");
	}

	(void)corosync_cfg_finalize (handle);

	return (rc);
}

static void shutdown_do(void)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	corosync_cfg_callbacks_t callbacks;

	callbacks.corosync_cfg_shutdown_callback = NULL;

	result = corosync_cfg_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		printf ("Could not initialize corosync configuration API error %d\n", result);
		exit (1);
	}

	printf ("Shutting down corosync\n");
	cs_repeat(result, 30, corosync_cfg_try_shutdown (handle, COROSYNC_CFG_SHUTDOWN_FLAG_REQUEST));
	if (result != CS_OK) {
		printf ("Could not shutdown (error = %d)\n", result);
	}

	(void)corosync_cfg_finalize (handle);
}

static void showaddrs_do(unsigned int nodeid)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	corosync_cfg_callbacks_t callbacks;
	int numaddrs;
	int i;
	corosync_cfg_node_address_t addrs[INTERFACE_MAX];


	result = corosync_cfg_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		printf ("Could not initialize corosync configuration API error %d\n", result);
		exit (1);
	}

	if (corosync_cfg_get_node_addrs(handle, nodeid, INTERFACE_MAX, &numaddrs, addrs) == CS_OK) {
		for (i=0; i<numaddrs; i++) {
			char buf[INET6_ADDRSTRLEN];
			struct sockaddr_storage *ss = (struct sockaddr_storage *)addrs[i].address;
			struct sockaddr_in *sin = (struct sockaddr_in *)addrs[i].address;
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addrs[i].address;
			void *saddr;

			if (!ss->ss_family) {
				continue;
			}
			if (ss->ss_family == AF_INET6) {
				saddr = &sin6->sin6_addr;
			} else {
				saddr = &sin->sin_addr;
			}

			inet_ntop(ss->ss_family, saddr, buf, sizeof(buf));
			if (i != 0) {
				printf(" ");
			}
			printf("%s", buf);
		}
		printf("\n");
	}


	(void)corosync_cfg_finalize (handle);
}


static void killnode_do(unsigned int nodeid)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;

	printf ("Killing node %d\n", nodeid);
	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		printf ("Could not initialize corosync configuration API error %d\n", result);
		exit (1);
	}
	result = corosync_cfg_kill_node (handle, nodeid, "Killed by corosync-cfgtool");
	if (result != CS_OK) {
		printf ("Could not kill node (error = %d)\n", result);
	}
	(void)corosync_cfg_finalize (handle);
}


static void usage_do (void)
{
	printf ("corosync-cfgtool [[-i <interface ip>] [-b] -s] [-R] [-k nodeid] [-a nodeid] [-h] [-H]\n\n");
	printf ("A tool for displaying and configuring active parameters within corosync.\n");
	printf ("options:\n");
	printf ("\t-i\tFinds only information about the specified interface IP address when used with -s..\n");
	printf ("\t-s\tDisplays the status of the current links on this node(UDP/UDPU), while extended status for KNET.\n");
	printf ("\t-b\tDisplays the brief status of the current links on this node when used with -s.(KNET only)\n");
	printf ("\t-R\tTell all instances of corosync in this cluster to reload corosync.conf.\n");
	printf ("\t-k\tKill a node identified by node id.\n");
	printf ("\t-a\tDisplay the IP address(es) of a node\n");
	printf ("\t-h\tPrint basic usage.\n");
	printf ("\t-H\tShutdown corosync cleanly on this node.\n");
}

int main (int argc, char *argv[]) {
	const char *options = "i:sbrRk:a:hH";
	int opt;
	unsigned int nodeid = 0;
	char interface_name[128] = "";
	int rc = 0;
	enum user_action action = ACTION_NOOP;
	int brief = 0;

	while ( (opt = getopt(argc, argv, options)) != -1 ) {
		switch (opt) {
		case 'i':
			strncpy(interface_name, optarg, sizeof(interface_name));
			interface_name[sizeof(interface_name) - 1] = '\0';
			break;
		case 's':
			action = ACTION_LINKSTATUS_GET;
			break;
		case 'b':
			brief = 1;
			break;
		case 'R':
			action = ACTION_RELOAD_CONFIG;
			break;
		case 'k':
			nodeid = atoi (optarg);
			action = ACTION_KILL_NODE;
			break;
		case 'H':
			action = ACTION_SHUTDOW;
			break;
		case 'a':
			nodeid = atoi (optarg);
			action = ACTION_SHOWADDR;
			break;
		case '?':
			return (EXIT_FAILURE);
			break;
		case 'h':
		default:
			break;
		}
	}
	switch(action) {
	case ACTION_LINKSTATUS_GET:
		rc = linkstatusget_do(interface_name, brief);
		break;
	case ACTION_RELOAD_CONFIG:
		rc = reload_config_do();
		break;
	case ACTION_KILL_NODE:
		killnode_do(nodeid);
		break;
	case ACTION_SHUTDOW:
		shutdown_do();
		break;
	case ACTION_SHOWADDR:
		showaddrs_do(nodeid);
		break;
	case ACTION_NOOP:
	default:
		usage_do();
		break;
	}

	return (rc);
}
