/*
 * Copyright (c) 2021 Red Hat Inc
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

#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>

#include <corosync/corotypes.h>
#include <corosync/cfg.h>

static void shutdown_callback(corosync_cfg_handle_t handle, corosync_cfg_shutdown_flags_t flags)
{
	/* Prevent shutdown */
	printf("In shutdown callback  - denying corosync shutdown\n");
	corosync_cfg_replyto_shutdown(handle, COROSYNC_CFG_SHUTDOWN_FLAG_NO);
}

static void *dispatch_thread(void *arg)
{
	corosync_cfg_handle_t handle = *(corosync_cfg_handle_t *)arg;

	int res = CS_OK;
	while (res == CS_OK) {
		res = corosync_cfg_dispatch(handle, CS_DISPATCH_ONE);
	}
	fprintf(stderr, "ERROR: Corosync shut down\n");
	return (void*)0;
}


int main (int argc, char *argv[]) {
	corosync_cfg_handle_t cfg_handle;
	corosync_cfg_handle_t cfg_handle1;
	unsigned int local_nodeid;
	int i;
	int res;
	struct corosync_cfg_node_status_v1 ns;
	pthread_t thread;
	corosync_cfg_callbacks_t callbacks = {
		.corosync_cfg_shutdown_callback = shutdown_callback
	};

	res = corosync_cfg_initialize(&cfg_handle, &callbacks);
	if (res != CS_OK) {
		fprintf(stderr, "corosync_Cfg_initialize(0) failed: %d\n", res);
		return 1;
	}

	/* Start a new handle & thread to prevent the shutdown we request later on */
	res = corosync_cfg_initialize(&cfg_handle1, &callbacks);
	if (res != CS_OK) {
		fprintf(stderr, "corosync_cfg_initialize(1) failed: %d\n", res);
		return 1;
	}
	res = corosync_cfg_trackstart(cfg_handle1, 0);
	if (res != CS_OK) {
		fprintf(stderr, "corosync_cfg_initialize(1) failed: %d\n", res);
		return 1;
	}
	res = pthread_create(&thread, NULL, dispatch_thread, (void *)&cfg_handle1);
	if (res != 0) {
		perror("pthread_create failed");
		return 1;
	}

	/* Exercise a few functions */
	res = corosync_cfg_local_get(cfg_handle, &local_nodeid);
	if (res != CS_OK) {
		fprintf(stderr, "corosync_cfg_local_get failed: %d\n", res);
		return 1;
	}

	printf("Local nodeid is %d\n", local_nodeid);

	/*
	 * Test node_status_get.
	 * node status for the local node looks odd (cos it's the loopback connection), so
	 * we try for a node ID one less or more than us just to get output that looks
	 * sensible to the user.
	 */
	res = corosync_cfg_node_status_get(cfg_handle, local_nodeid-1, CFG_NODE_STATUS_V1, &ns);
	if (res != CS_OK) {
		res = corosync_cfg_node_status_get(cfg_handle, local_nodeid+1, CFG_NODE_STATUS_V1, &ns);
	}
	if (res != CS_OK) {
		fprintf(stderr, "corosync_cfg_node_status_get failed: %d\n", res);
		return 1;
	}
	printf("Node Status for nodeid %d\n", ns.nodeid);
	printf("   reachable: %d\n", ns.reachable);
	printf("   remote: %d\n", ns.remote);
	printf("   onwire_min: %d\n", ns.onwire_min);
	printf("   onwire_max: %d\n", ns.onwire_max);
	printf("   onwire_ver: %d\n", ns.onwire_ver);
	for (i = 0; i<CFG_MAX_LINKS; i++) {
		if (ns.link_status[i].enabled) {
				printf("   Link %d\n", i);
				printf("      connected: %d\n", ns.link_status[i].connected);
				printf("      mtu: %d\n", ns.link_status[i].mtu);
				printf("      src: %s\n", ns.link_status[i].src_ipaddr);
				printf("      dst: %s\n", ns.link_status[i].dst_ipaddr);
		}
	}

	/* This shutdown request should be denied by the thread */
	res = corosync_cfg_try_shutdown(cfg_handle, COROSYNC_CFG_SHUTDOWN_FLAG_REQUEST);

	if (res == CS_OK) {
		fprintf(stderr, "ERROR: corosync_cfg_try_shutdown suceeded. should have been prevented\n");
	}
	if (res != CS_ERR_BUSY && res != CS_OK) {
		fprintf(stderr, "corosync_cfg_try_shutdown failed: %d\n", res);
		return 1;
	}

	/*
	 * Test bug presented in 3.1.1 and 3.1.2 that makes trackstop blocks forever
	 */
	res = corosync_cfg_trackstop(cfg_handle1);
	if (res != CS_OK) {
		fprintf(stderr, "corosync_cfg_trackstop failed: %d\n", res);
		return 1;
	}

	return 0;
}
