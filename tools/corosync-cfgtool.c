/*
 * Copyright (c) 2006-2020 Red Hat, Inc.
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
#include <limits.h>
#include <getopt.h>

#include <corosync/corotypes.h>
#include <corosync/totem/totem.h>
#include <corosync/cfg.h>
#include <corosync/cmap.h>
#include "util.h"

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
	ACTION_NODESTATUS_GET,
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
nodestatusget_do (enum user_action action, int brief)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	cmap_handle_t cmap_handle;
	char iter_key[CMAP_KEYNAME_MAXLEN];
	cmap_iter_handle_t iter;
	unsigned int local_nodeid;
	unsigned int local_nodeid_index=0;
	unsigned int other_nodeid_index=0;
	unsigned int nodeid;
	int nodeid_match_guard;
	cmap_value_types_t type;
	size_t value_len;
	char *str;
	char *transport_str = NULL;
	uint32_t nodeid_list[KNET_MAX_HOST];
	const char *link_transport[KNET_MAX_LINK];
	int s = 0;
	int rc = EXIT_SUCCESS;
	int transport_number = TOTEM_TRANSPORT_KNET;
	int i,j;
	struct corosync_cfg_node_status_v1 node_status;

	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %d\n", result);
		exit (EXIT_FAILURE);
	}

	result = cmap_initialize (&cmap_handle);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync cmap API error %d\n", result);
		exit (EXIT_FAILURE);
	}

	result = cmap_get_string(cmap_handle, "totem.transport", &str);
	if (result == CS_OK) {
		if (strcmp (str, "udpu") == 0) {
			transport_number = TOTEM_TRANSPORT_UDPU;
		}
		if (strcmp (str, "udp") == 0) {
			transport_number = TOTEM_TRANSPORT_UDP;
		}
		transport_str = str;
	}
	if (!transport_str) {
		transport_str = strdup("knet"); /* It's the default */
	}

	result = corosync_cfg_local_get(handle, &local_nodeid);
	if (result != CS_OK) {
		fprintf (stderr, "Could not get the local node id, the error is: %d\n", result);
		free(transport_str);
		cmap_finalize(cmap_handle);
		corosync_cfg_finalize(handle);
		return EXIT_FAILURE;
	}

	/* Get a list of nodes. We do it this way rather than using votequorum as cfgtool
	 * needs to be independent of quorum type
	 */
	result = cmap_iter_init(cmap_handle, "nodelist.node.", &iter);
	if (result != CS_OK) {
		fprintf (stderr, "Could not get nodelist from cmap. error %d\n", result);
		free(transport_str);
		cmap_finalize(cmap_handle);
		corosync_cfg_finalize(handle);
		exit (EXIT_FAILURE);
	}

	while ((cmap_iter_next(cmap_handle, iter, iter_key, &value_len, &type)) == CS_OK) {
		nodeid_match_guard = 0;
		if (sscanf(iter_key, "nodelist.node.%*u.nodeid%n", &nodeid_match_guard) != 0) {
			continue;
		}
		/* check for exact match */
		if (nodeid_match_guard != strlen(iter_key)) {
			continue;
		}
		if (cmap_get_uint32(cmap_handle, iter_key, &nodeid) == CS_OK) {
			nodeid_list[s++] = nodeid;
		}
	}

	if (s == 0) {
		fprintf(stderr, "No nodes found in nodelist\n");
		exit (EXIT_FAILURE);
	}

	/* It's nice to have these in nodeid order */
	qsort(nodeid_list, s, sizeof(uint32_t), node_compare);

	/*
	 * Find local and other nodeid index in nodeid_list
	 */
	for (i = 0; i < s; i++) {
		if (nodeid_list[i] == local_nodeid) {
			local_nodeid_index = i;
		} else {
			/* Bit of an odd one this. but local node only uses one link (of course, to itself)
			   so if we want to know which links are active across the cluster we need to look
			   at another node (any other) node's link list */
			other_nodeid_index = i;
		}
	}

	/* Get the transport of each link - but set reasonable defaults */
	if (transport_number == TOTEM_TRANSPORT_KNET) {
		for (i = 0; i<KNET_MAX_LINK; i++) {
			link_transport[i] = "udp";
		}
	} else {
		for (i = 0; i<KNET_MAX_LINK; i++) {
			link_transport[i] = ""; /* No point in displaying "udp" again */
		}
	}
	result = cmap_iter_init(cmap_handle, "totem.interface.", &iter);
	if (result == CS_OK) { /* it's fine for this to fail, we just use the defaults */
		while ((cmap_iter_next(cmap_handle, iter, iter_key, &value_len, &type)) == CS_OK) {
			unsigned int link_number;
			char *knet_transport;
			char knet_transport_str[CMAP_KEYNAME_MAXLEN];

			/* transport is (sensibly) indexed by link number */
			if (sscanf(iter_key, "totem.interface.%u.knet_transport", &link_number) != 1) {
				continue;
			}
			snprintf(knet_transport_str, sizeof(knet_transport_str),
				 "totem.interface.%u.knet_transport", link_number);
			if (cmap_get_string(cmap_handle, knet_transport_str, &knet_transport) == CS_OK) {
				link_transport[link_number] = knet_transport;
			}
		}

		cmap_iter_finalize(cmap_handle, iter);
	}

	cmap_finalize(cmap_handle);

	printf ("Local node ID " CS_PRI_NODE_ID ", transport %s\n", local_nodeid, transport_str);

        /* If node status requested then do print node-based info */
	if (action == ACTION_NODESTATUS_GET) {
		for (i=0; i<s; i++) {
			result = corosync_cfg_node_status_get(handle, nodeid_list[i], CFG_NODE_STATUS_V1, &node_status);
			if (result == CS_OK) {
				/* Only display node info if it is reachable (and not us) */
				if (node_status.reachable && node_status.nodeid != local_nodeid) {
					printf("nodeid: " CS_PRI_NODE_ID "", node_status.nodeid);
					printf(" reachable");
					if (node_status.remote) {
						printf(" remote");
					}
					if (node_status.external) {
						printf(" external");
					}
#ifdef HAVE_KNET_ONWIRE_VER
					if (transport_number == TOTEM_TRANSPORT_KNET) {
						printf("   onwire (min/max/cur): %d, %d, %d",
						       node_status.onwire_min,
						       node_status.onwire_max,
						       node_status.onwire_ver);
					}
#endif
					printf("\n");
					for (j=0; j<CFG_MAX_LINKS; j++) {
						if (node_status.link_status[j].enabled) {
							printf("   LINK: %d %s", j, link_transport[j]);
							printf(" (%s%s%s)",
							       node_status.link_status[j].src_ipaddr,
							       transport_number==TOTEM_TRANSPORT_KNET?"->":"",
							       node_status.link_status[j].dst_ipaddr);
							if (node_status.link_status[j].enabled) {
								printf(" enabled");
							}
							if (node_status.link_status[j].connected) {
								printf(" connected");
							}
							if (node_status.link_status[j].dynconnected) {
								printf(" dynconnected");
							}
							printf(" mtu: %d\n", node_status.link_status[j].mtu);
						}
					}
					printf("\n");
				}
			}
		}
	}
	/* Print in link order */
	else {
		struct corosync_cfg_node_status_v1 node_info[s];
		memset(node_info, 0, sizeof(node_info));

		for (i=0; i<s; i++) {
			result = corosync_cfg_node_status_get(handle, nodeid_list[i], CFG_NODE_STATUS_V1, &node_info[i]);
			if (result != CS_OK) {
				fprintf (stderr, "Could not get the node status for nodeid %d, the error is: %d\n", nodeid_list[i], result);
			}
		}

		for (i=0; i<CFG_MAX_LINKS; i++) {
			if (node_info[other_nodeid_index].link_status[i].enabled) {
				printf("LINK ID %d %s\n", i, link_transport[i]);
				printf("\taddr\t= %s\n", node_info[other_nodeid_index].link_status[i].src_ipaddr);
				if (brief) {
					printf("\tstatus\t= ");
					for (j=0; j<s; j++) {
						char status = (node_info[j].link_status[i].enabled |
							       (node_info[j].link_status[i].connected << 1)) + '0';
						if (j == local_nodeid_index) {
							status = 'n';
						}
						printf("%c", status);
					}
					printf("\n");
				} else {
					printf("\tstatus:\n");
					for (j=0; j<s; j++) {
						printf("\t\tnodeid: " CS_PRI_NODE_ID_PADDED ":\t", node_info[j].nodeid);
						if (j == local_nodeid_index) {
							printf("localhost");
						} else {
							if (node_info[j].link_status[i].connected) {
								printf("connected");
							} else {
								printf("disconnected");
							}
						}
						printf("\n");
					}
				}
			}
		}
	}
	free(transport_str);
	corosync_cfg_finalize(handle);
	return rc;
}


static int check_for_reload_errors(void)
{
	cmap_handle_t cmap_handle;
	cs_error_t result;
	char *str;
	int res;

	result = cmap_initialize (&cmap_handle);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync cmap API error %d\n", result);
		exit (EXIT_FAILURE);
	}

	result = cmap_get_string(cmap_handle, "config.reload_error_message", &str);
	if (result == CS_OK) {
		printf("ERROR from reload: %s - see syslog for more information\n", str);
		free(str);
		res = 1;
	}
	else {
		res = 0;
	}
	cmap_finalize(cmap_handle);
	return res;
}

static int reload_config_do (void)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	int rc;

	rc = EXIT_SUCCESS;

	printf ("Reloading corosync.conf...\n");
	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %s\n", cs_strerror(result));
		exit (EXIT_FAILURE);
	}

	result = corosync_cfg_reload_config (handle);
	if (result != CS_OK) {
		fprintf (stderr, "Could not reload configuration. Error %s\n", cs_strerror(result));
		rc = (int)result;
	}
	else {
		printf ("Done\n");
	}

	(void)corosync_cfg_finalize (handle);

	if ((rc = check_for_reload_errors())) {
		fprintf(stderr, "Errors in appying config, corosync.conf might not match the running system\n");
	}

	return (rc);
}

static int reopen_log_files_do (void)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	int rc;

	rc = EXIT_SUCCESS;

	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %s\n", cs_strerror(result));
		exit (EXIT_FAILURE);
	}

	result = corosync_cfg_reopen_log_files (handle);
	if (result != CS_OK) {
		fprintf (stderr, "Could not reopen corosync logging files. Error %s\n", cs_strerror(result));
		rc = (int)result;
	}

	(void)corosync_cfg_finalize (handle);

	return (rc);
}

static void shutdown_do(int force)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	corosync_cfg_callbacks_t callbacks;
	int flag;

	callbacks.corosync_cfg_shutdown_callback = NULL;
	if (force) {
		flag = COROSYNC_CFG_SHUTDOWN_FLAG_REGARDLESS;
	} else {
		flag = COROSYNC_CFG_SHUTDOWN_FLAG_REQUEST;
	}

	result = corosync_cfg_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %d\n", result);
		exit (EXIT_FAILURE);
	}

	printf ("Shutting down corosync\n");
	cs_repeat(result, 30, corosync_cfg_try_shutdown (handle, flag));
	if (result != CS_OK) {
		fprintf (stderr, "Could not shutdown (error = %d)\n", result);
	}

	(void)corosync_cfg_finalize (handle);
}

static int showaddrs_do(unsigned int nodeid)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	int numaddrs;
	int i;
	int rc = EXIT_SUCCESS;
	corosync_cfg_node_address_t addrs[INTERFACE_MAX];


	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %d\n", result);
		exit (EXIT_FAILURE);
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
	} else {
		fprintf (stderr, "Could not get node address for nodeid %d\n", nodeid);
		rc = EXIT_FAILURE;
	}


	(void)corosync_cfg_finalize (handle);
	return rc;
}


static void killnode_do(unsigned int nodeid)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;

	printf ("Killing node " CS_PRI_NODE_ID "\n", nodeid);
	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize corosync configuration API error %d\n", result);
		exit (EXIT_FAILURE);
	}
	result = corosync_cfg_kill_node (handle, nodeid, "Killed by corosync-cfgtool");
	if (result != CS_OK) {
		fprintf (stderr, "Could not kill node (error = %s)\n", cs_strerror(result));
		exit(EXIT_FAILURE);
	}
	(void)corosync_cfg_finalize (handle);
}


static void usage_do (void)
{
	printf ("corosync-cfgtool [[-i <interface ip>] [-b] -s] [-R] [-L] [-k nodeid] [-a nodeid] [-h] [-H]\n\n");
	printf ("A tool for displaying and configuring active parameters within corosync.\n");
	printf ("options:\n");
	printf ("\t-i\tFinds only information about the specified interface IP address or link id when used with -s..\n");
	printf ("\t-s\tDisplays the status of the current links on this node.\n");
	printf ("\t-n\tDisplays the status of the connected nodes and their links.\n");
	printf ("\t-b\tDisplays the brief status of the current links on this node when used with -s.\n");
	printf ("\t-R\tTell all instances of corosync in this cluster to reload corosync.conf.\n");
	printf ("\t-L\tTell corosync to reopen all logging files.\n");
	printf ("\t-k\tKill a node identified by node id.\n");
	printf ("\t-a\tDisplay the IP address(es) of a node\n");
	printf ("\t-h\tPrint basic usage.\n");
	printf ("\t-H\tShutdown corosync cleanly on this node.\n");
	printf ("\t\t--force will shut down corosync regardless of daemon vetos\n");
}

int main (int argc, char *argv[]) {
	int opt;
	unsigned int nodeid = 0;
	char interface_name[128] = "";
	int rc = EXIT_SUCCESS;
	enum user_action action = ACTION_NOOP;
	int brief = 0;
	long long int l;
	int option_index = 0;
	int force_shutdown  = 0;
	const char *options = "i:snbrRLk:a:hH";
	struct option long_options[] = {
		{"if",       required_argument, 0, 'i'},
		{"status",   no_argument,       0, 's'},
		{"nodes",    no_argument,       0, 'n'},
		{"brief",    no_argument,       0, 'b'},
		{"reload",   no_argument,       0, 'R'},
		{"reopen",   no_argument,       0, 'L'},
		{"kill",     required_argument, 0, 'k'},
		{"address",  required_argument, 0, 'a'},
		{"shutdown", no_argument,       0, 'H'},
		{"force",    no_argument,       0, 0},
		{0,          0,                 0, 0}
	};

		while ( (opt = getopt_long(argc, argv, options, long_options, &option_index)) != -1 ) {
		switch (opt) {
			case 0: // options with no short equivalent - just --force ATM
			if (strcmp(long_options[option_index].name, "force") == 0) {
				force_shutdown = 1;
			}
			break;
		case 'i':
			strncpy(interface_name, optarg, sizeof(interface_name));
			interface_name[sizeof(interface_name) - 1] = '\0';
			break;
		case 's':
			action = ACTION_LINKSTATUS_GET;
			break;
		case 'n':
			action = ACTION_NODESTATUS_GET;
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
			if (util_strtonum(optarg, 1, UINT_MAX, &l) == -1) {
				fprintf(stderr, "The nodeid was not valid, try a positive number\n");
				exit(EXIT_FAILURE);
			}
			nodeid = l;
			action = ACTION_KILL_NODE;
			break;
		case 'H':
			action = ACTION_SHUTDOW;
			break;
		case 'a':
			if (util_strtonum(optarg, 1, UINT_MAX, &l) == -1) {
				fprintf(stderr, "The nodeid was not valid, try a positive number\n");
				exit(EXIT_FAILURE);
			}
			nodeid = l;
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
		rc = nodestatusget_do(action, brief);
		break;
	case ACTION_NODESTATUS_GET:
		rc = nodestatusget_do(action, brief);
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
		shutdown_do(force_shutdown);
		break;
	case ACTION_SHOWADDR:
		rc = showaddrs_do(nodeid);
		break;
	case ACTION_NOOP:
	default:
		usage_do();
		break;
	}

	return (rc);
}
