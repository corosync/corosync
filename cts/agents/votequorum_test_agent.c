/*
 * Copyright (c) 2010 Red Hat Inc
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>
#include <syslog.h>

#include <corosync/corotypes.h>
#include <corosync/votequorum.h>
#include <corosync/quorum.h>
#include "common_test_agent.h"


static void getinfo (int sock)
{
	votequorum_callbacks_t callbacks;
	int ret;
	struct votequorum_info info;
	char response[100];
	votequorum_handle_t g_handle;

	callbacks.votequorum_notify_fn = NULL;
	callbacks.votequorum_expectedvotes_notify_fn = NULL;

	ret = votequorum_initialize(&g_handle, &callbacks);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "votequorum_initialize FAILED: %d\n", ret);
		goto send_response;
	}

	ret = votequorum_getinfo(g_handle, 0, &info);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "votequorum_getinfo FAILED: %d\n", ret);
		goto send_response;
	}

	snprintf (response, 100, "%d:%d:%d:%d:%d",
		info.node_votes,
		info.node_expected_votes,
		info.highest_expected,
		info.total_votes,
		info.quorum);

send_response:
	votequorum_finalize (g_handle);
	send (sock, response, strlen (response), 0);
}


static void setexpected (int sock, char *arg)
{
	votequorum_callbacks_t callbacks;
	int ret;
	char response[100];
	votequorum_handle_t g_handle;

	callbacks.votequorum_notify_fn = NULL;
	callbacks.votequorum_expectedvotes_notify_fn = NULL;

	ret = votequorum_initialize(&g_handle, &callbacks);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "votequorum_initialize FAILED: %d\n", ret);
		goto send_response;
	}

	ret = votequorum_setexpected (g_handle, atoi(arg));
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "set expected votes FAILED: %d\n", ret);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	votequorum_finalize (g_handle);
	send (sock, response, strlen (response) + 1, 0);
}

static void setvotes (int sock, char *arg)
{
	votequorum_callbacks_t callbacks;
	int ret;
	char response[100];
	votequorum_handle_t g_handle;

	callbacks.votequorum_notify_fn = NULL;
	callbacks.votequorum_expectedvotes_notify_fn = NULL;

	ret = votequorum_initialize(&g_handle, &callbacks);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "votequorum_initialize FAILED: %d\n", ret);
		goto send_response;
	}

	ret = votequorum_setvotes (g_handle, 0, atoi(arg));
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "set votes FAILED: %d\n", ret);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	votequorum_finalize (g_handle);
	send (sock, response, strlen (response), 0);
}


static void getquorate (int sock)
{
	int ret;
	int quorate;
	char response[100];
	quorum_handle_t handle;

	ret = quorum_initialize (&handle, NULL);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "quorum_initialize FAILED: %d\n", ret);
		goto send_response;
	}

	ret = quorum_getquorate (handle, &quorate);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "getquorate FAILED: %d\n", ret);
		goto send_response;
	}

	snprintf (response, 100, "%d", quorate);

send_response:
	quorum_finalize (handle);
	send (sock, response, strlen (response), 0);
}

static void do_command (int sock, char* func, char*args[], int num_args)
{
	char response[100];

	if (parse_debug)
		syslog (LOG_DEBUG,"RPC:%s() called.", func);

	if (strcmp ("votequorum_getinfo", func) == 0) {
		getinfo (sock);
	} else if (strcmp ("votequorum_setvotes", func) == 0) {
		setvotes (sock, args[0]);
	} else if (strcmp ("votequorum_setexpected", func) == 0) {
		setexpected (sock, args[0]);
	} else if (strcmp ("quorum_getquorate", func) == 0) {
		getquorate (sock);
	} else {
		syslog (LOG_ERR,"%s RPC:%s not supported!", __func__, func);
		snprintf (response, 100, "%s", NOT_SUPPORTED_STR);
		send (sock, response, strlen (response), 0);
	}
}


int main (int argc, char *argv[])
{
	int ret;

	openlog (NULL, LOG_CONS|LOG_PID, LOG_DAEMON);
	syslog (LOG_ERR, "votequorum_test_agent STARTING");

	parse_debug = 1;
	ret = test_agent_run (9037, do_command);
	syslog (LOG_ERR, "votequorum_test_agent EXITING");

	return ret;
}


