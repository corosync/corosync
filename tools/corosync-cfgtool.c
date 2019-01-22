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
#include <corosync/cmap.h>

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
	ACTION_REOPEN_LOG_FILES,
	ACTION_SHUTDOW,
	ACTION_SHOWADDR,
	ACTION_KILL_NODE,
};

static int node_compare(const void *aptr, const void *bptr)
{
	uint32_t a,b;

	a = *(uint32_t *)aptr;
	b = *(uint32_t *)bptr;

	return a > b;
}

static int
linkstatusget_do (char *interface_name, int brief)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	cmap_handle_t cmap_handle;
	unsigned int interface_count;
	char **interface_names;
	char **interface_status;
	uint32_t nodeid_list[KNET_MAX_HOST];
	char iter_key[CMAP_KEYNAME_MAXLEN];
	unsigned int i;
	cmap_iter_handle_t iter;
	unsigned int nodeid;
	unsigned int node_pos;
	cmap_value_types_t type;
	size_t value_len;
	int rc = 0;
	int len, s = 0, t;

	printf ("Printing link status.\n");
	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		printf ("Could not initialize corosync configuration API error %d\n", result);
		exit (1);
	}

	result = cmap_initialize (&cmap_handle);
	if (result != CS_OK) {
		printf ("Could not initialize corosync cmap API error %d\n", result);
		exit (1);
	}
	/* Get a list of nodes. We do it this way rather than using votequorum as cfgtool
	 * needs to be independent of quorum type
	 */
	result = cmap_iter_init(cmap_handle, "nodelist.node.", &iter);
	if (result != CS_OK) {
		printf ("Could not get nodelist from cmap. error %d\n", result);
		exit (1);
	}

	while ((cmap_iter_next(cmap_handle, iter, iter_key, &value_len, &type)) == CS_OK) {
		result = sscanf(iter_key, "nodelist.node.%u.nodeid", &node_pos);
		if (result != 1) {
			continue;
		}
		if (cmap_get_uint32(cmap_handle, iter_key, &nodeid) == CS_OK) {
			nodeid_list[s++] = nodeid;
		}
	}

	/* totemknet returns nodes in nodeid order - even though it doesn't tell us
	   what the nodeid is. So sort our node list and we can then look up
	   knet node pos to get an actual nodeid.
	   Yep, I really should have totally rewritten the cfg interface for this.
	*/
	qsort(nodeid_list, s, sizeof(uint32_t), node_compare);

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

				/*
				 * Interface_name is "<linkid> <IP address>"
				 * separate them out
				 */
				char *space = strchr(interface_names[i], ' ');
				if (!space) {
					continue;
				}
				*space = '\0';

				printf ("LINK ID %s\n", interface_names[i]);
				printf ("\taddr\t= %s\n", space+1);
				if((!brief) && (strcmp(interface_status[i], "OK") != 0) &&
					(!strstr(interface_status[i], "FAULTY"))) {
					len = strlen(interface_status[i]);
					printf ("\tstatus:\n");
					while (s < len) {
						nodeid = nodeid_list[s];
						t = interface_status[i][s] - '0';
						s++;
						printf("\t\tnodeid %2d:\t", nodeid);
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
	(void)cmap_finalize (cmap_handle);
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

static int reopen_log_files_do (void)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	int rc;

	rc = 0;

	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %s\n", cs_strerror(result));
		exit (1);
	}

	result = corosync_cfg_reopen_log_files (handle);
	if (result != CS_OK) {
		fprintf (stderr, "Could not reopen corosync logging files. Error %s\n", cs_strerror(result));
		rc = (int)result;
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
	printf ("corosync-cfgtool [[-i <interface ip>] [-b] -s] [-R] [-L] [-k nodeid] [-a nodeid] [-h] [-H]\n\n");
	printf ("A tool for displaying and configuring active parameters within corosync.\n");
	printf ("options:\n");
	printf ("\t-i\tFinds only information about the specified interface IP address when used with -s..\n");
	printf ("\t-s\tDisplays the status of the current links on this node(UDP/UDPU), while extended status for KNET.\n");
	printf ("\t-b\tDisplays the brief status of the current links on this node when used with -s.(KNET only)\n");
	printf ("\t-R\tTell all instances of corosync in this cluster to reload corosync.conf.\n");
	printf ("\t-L\tTell corosync to reopen all logging files.\n");
	printf ("\t-k\tKill a node identified by node id.\n");
	printf ("\t-a\tDisplay the IP address(es) of a node\n");
	printf ("\t-h\tPrint basic usage.\n");
	printf ("\t-H\tShutdown corosync cleanly on this node.\n");
}

int main (int argc, char *argv[]) {
	const char *options = "i:sbrRLk:a:hH";
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
		case 'L':
			action = ACTION_REOPEN_LOG_FILES;
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
	case ACTION_REOPEN_LOG_FILES:
		rc = reopen_log_files_do();
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
