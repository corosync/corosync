/*
 * Copyright (c) 2006-2013 Red Hat, Inc.
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

static int ringstatusget_do (char *interface_name)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;
	unsigned int interface_count;
	char **interface_names;
	char **interface_status;
	unsigned int i;
	unsigned int nodeid;
	int rc = 0;

	printf ("Printing ring status.\n");
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
		printf ("Could not get the ring status, the error is: %d\n", result);
	} else {
		for (i = 0; i < interface_count; i++) {
			if ( (interface_name && 
			     	(interface_name[0]=='\0' || 
				strcasecmp (interface_name, interface_names[i]) == 0)) ||
				!interface_name ) {

				printf ("RING ID %d\n", i);
				printf ("\tid\t= %s\n", interface_names[i]);
				printf ("\tstatus\t= %s\n", interface_status[i]);
				if (strstr(interface_status[i], "FAULTY")) {
					rc = 1;
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

static void ringreenable_do (void)
{
	cs_error_t result;
	corosync_cfg_handle_t handle;

	printf ("Re-enabling all failed rings.\n");
	result = corosync_cfg_initialize (&handle, NULL);
	if (result != CS_OK) {
		printf ("Could not initialize corosync configuration API error %d\n", result);
		exit (1);
	}

	result = corosync_cfg_ring_reenable (handle);
	if (result != CS_OK) {
		printf ("Could not re-enable ring error %d\n", result);
	}

	(void)corosync_cfg_finalize (handle);
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

static void showaddrs_do(int nodeid)
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

			if (ss->ss_family == AF_INET6)
				saddr = &sin6->sin6_addr;
			else
				saddr = &sin->sin_addr;

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
	printf ("corosync-cfgtool [-i <interface ip>] [-s] [-r] [-R] [-k nodeid] [-a nodeid] [-h] [-H]\n\n");
	printf ("A tool for displaying and configuring active parameters within corosync.\n");
	printf ("options:\n");
	printf ("\t-i\tFinds only information about the specified interface IP address.\n");
	printf ("\t-s\tDisplays the status of the current rings on this node.\n");
	printf ("\t-r\tReset redundant ring state cluster wide after a fault to\n");
	printf ("\t\tre-enable redundant ring operation.\n");
	printf ("\t-R\tTell all instances of corosync in this cluster to reload corosync.conf.\n");
	printf ("\t-k\tKill a node identified by node id.\n");
	printf ("\t-a\tDisplay the IP address(es) of a node\n");
	printf ("\t-h\tPrint basic usage.\n");
	printf ("\t-H\tShutdown corosync cleanly on this node.\n");
}

int main (int argc, char *argv[]) {
	const char *options = "i:srRk:a:hH";
	int opt;
	unsigned int nodeid;
	char interface_name[128] = "";
	int rc=0;

	if (argc == 1) {
		usage_do ();
	}
	while ( (opt = getopt(argc, argv, options)) != -1 ) {
		switch (opt) {
		case 'i':
			strncpy(interface_name, optarg, sizeof(interface_name));
			interface_name[sizeof(interface_name) - 1] = '\0';
			break;
		case 's':
			rc = ringstatusget_do (interface_name);
			break;
		case 'R':
			rc = reload_config_do ();
			break;
		case 'r':
			ringreenable_do ();
			break;
		case 'k':
			nodeid = atoi (optarg);
			killnode_do(nodeid);
			break;
		case 'H':
			shutdown_do();
			break;
		case 'a':
			nodeid = atoi (optarg);
			showaddrs_do(nodeid);
			break;
		case 'h':
			usage_do();
			break;
		}
	}

	return (rc);
}
