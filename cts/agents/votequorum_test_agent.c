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
#include <poll.h>

#include <corosync/totem/coropoll.h>
#include <corosync/corotypes.h>
#include <corosync/votequorum.h>
#include <corosync/quorum.h>
#include "common_test_agent.h"

static quorum_handle_t q_handle = 0;
static votequorum_handle_t vq_handle = 0;

static void votequorum_notification_fn(
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t quorate,
	uint32_t node_list_entries,
	votequorum_node_t node_list[])
{
	syslog (LOG_INFO, "VQ notification quorate: %d", quorate);
}

static void quorum_notification_fn(
	quorum_handle_t handle,
	uint32_t quorate,
	uint64_t ring_id,
	uint32_t view_list_entries,
	uint32_t *view_list)
{
	syslog (LOG_INFO, "NQ notification quorate: %d", quorate);
}


static int vq_dispatch_wrapper_fn (hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	cs_error_t error = votequorum_dispatch (vq_handle, CS_DISPATCH_ALL);
	if (error == CS_ERR_LIBRARY) {
		syslog (LOG_ERR, "%s() got LIB error disconnecting from corosync.", __func__);
		poll_dispatch_delete (ta_poll_handle_get(), fd);
		close (fd);
	}
	return 0;
}

static int q_dispatch_wrapper_fn (hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	cs_error_t error = quorum_dispatch (q_handle, CS_DISPATCH_ALL);
	if (error == CS_ERR_LIBRARY) {
		syslog (LOG_ERR, "%s() got LIB error disconnecting from corosync.", __func__);
		poll_dispatch_delete (ta_poll_handle_get(), fd);
		close (fd);
	}
	return 0;
}

static int q_lib_init(void)
{
	votequorum_callbacks_t vq_callbacks;
	quorum_callbacks_t q_callbacks;
	int ret = 0;
	int fd;

	if (vq_handle == 0) {
		syslog (LOG_INFO, "votequorum_initialize");
		vq_callbacks.votequorum_notify_fn = votequorum_notification_fn;
		vq_callbacks.votequorum_expectedvotes_notify_fn = NULL;
		ret = CS_ERR_NOT_EXIST;
		while (ret == CS_ERR_NOT_EXIST) {
			ret = votequorum_initialize (&vq_handle, &vq_callbacks);
			sleep (1);
		}
		if (ret != CS_OK) {
			syslog (LOG_ERR, "votequorum_initialize FAILED: %d\n", ret);
			vq_handle = 0;
		}
		else {
			ret = votequorum_trackstart (vq_handle, vq_handle, CS_TRACK_CHANGES);
			if (ret != CS_OK) {
				syslog (LOG_ERR, "votequorum_trackstart FAILED: %d\n", ret);
			}

			votequorum_fd_get (vq_handle, &fd);
			poll_dispatch_add (ta_poll_handle_get(), fd,
				POLLIN|POLLNVAL, NULL, vq_dispatch_wrapper_fn);
		}
	}
	if (q_handle == 0) {
		syslog (LOG_INFO, "quorum_initialize");
		q_callbacks.quorum_notify_fn = quorum_notification_fn;
		ret = quorum_initialize (&q_handle, &q_callbacks);
		if (ret != CS_OK) {
			syslog (LOG_ERR, "quorum_initialize FAILED: %d\n", ret);
			q_handle = 0;
		}
		else {
			ret = quorum_trackstart (q_handle, CS_TRACK_CHANGES);
			if (ret != CS_OK) {
				syslog (LOG_ERR, "quorum_trackstart FAILED: %d\n", ret);
			}
			quorum_fd_get (q_handle, &fd);
			poll_dispatch_add (ta_poll_handle_get(), fd,
				POLLIN|POLLNVAL, NULL, q_dispatch_wrapper_fn);
		}
	}
	return ret;
}

static void lib_init (int sock)
{
	int ret;
	char response[100];

	snprintf (response, 100, "%s", OK_STR);
	ret = q_lib_init ();

	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "q_lib_init FAILED: %d\n", ret);
	}

	send (sock, response, strlen (response), 0);
}

static void getinfo (int sock)
{
	int ret;
	struct votequorum_info info;
	char response[100];

	q_lib_init ();

	ret = votequorum_getinfo(vq_handle, 0, &info);
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
	send (sock, response, strlen (response), 0);
}


static void setexpected (int sock, char *arg)
{
	int ret;
	char response[100];

	q_lib_init ();

	ret = votequorum_setexpected (vq_handle, atoi(arg));
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "set expected votes FAILED: %d\n", ret);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	votequorum_finalize (vq_handle);
	send (sock, response, strlen (response) + 1, 0);
}

static void setvotes (int sock, char *arg)
{
	int ret;
	char response[100];

	q_lib_init ();

	ret = votequorum_setvotes (vq_handle, 0, atoi(arg));
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "set votes FAILED: %d\n", ret);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	votequorum_finalize (vq_handle);
	send (sock, response, strlen (response), 0);
}


static void getquorate (int sock)
{
	int ret;
	int quorate;
	char response[100];

	q_lib_init ();

	ret = quorum_getquorate (q_handle, &quorate);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "getquorate FAILED: %d\n", ret);
		goto send_response;
	}

	snprintf (response, 100, "%d", quorate);

send_response:
	send (sock, response, strlen (response), 0);
}

static void context_test (int sock)
{
	char response[100];
	char *cmp;

	snprintf (response, 100, "%s", OK_STR);

	votequorum_context_set (vq_handle, response);
	votequorum_context_get (vq_handle, (void**)&cmp);
	if (response != cmp) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "votequorum context not the same");
	}

	quorum_context_set (q_handle, response);
	quorum_context_get (q_handle, (const void**)&cmp);
	if (response != cmp) {
		snprintf (response, 100, "%s", FAIL_STR);
		syslog (LOG_ERR, "quorum context not the same");
	}
	send (sock, response, strlen (response) + 1, 0);
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
	} else if (strcmp ("init", func) == 0) {
		lib_init (sock);
	} else if (strcmp ("context_test", func) == 0) {
		context_test (sock);
	} else if (strcmp ("are_you_ok_dude", func) == 0) {
		snprintf (response, 100, "%s", OK_STR);
		send (sock, response, strlen (response) + 1, 0);
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


