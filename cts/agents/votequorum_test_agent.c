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
#include <poll.h>

#include <qb/qbloop.h>
#include <corosync/corotypes.h>
#include <corosync/votequorum.h>
#include <corosync/quorum.h>
#include "common_test_agent.h"
#include "../../lib/util.h"

static quorum_handle_t q_handle = 0;
static votequorum_handle_t vq_handle = 0;

static void votequorum_notification_fn(
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t quorate,
	uint32_t node_list_entries,
	votequorum_node_t node_list[])
{
	qb_log (LOG_INFO, "VQ notification quorate: %d", quorate);
}

static void quorum_notification_fn(
	quorum_handle_t handle,
	uint32_t quorate,
	uint64_t ring_id,
	uint32_t view_list_entries,
	uint32_t *view_list)
{
	qb_log (LOG_INFO, "NQ notification quorate: %d", quorate);
}


static int vq_dispatch_wrapper_fn (
	int fd,
	int revents,
	void *data)
{
	cs_error_t error = votequorum_dispatch (vq_handle, CS_DISPATCH_ALL);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "got %s error, disconnecting.",
			cs_strerror(error));
		votequorum_finalize(vq_handle);
		vq_handle = 0;
		return -1;
	}
	return 0;
}

static int q_dispatch_wrapper_fn (
	int fd,
	int revents,
	void *data)
{
	cs_error_t error = quorum_dispatch (q_handle, CS_DISPATCH_ALL);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "got %s error, disconnecting.",
			cs_strerror(error));
		quorum_finalize(q_handle);
		q_handle = 0;
		return -1;
	}
	return 0;
}

static int q_lib_init(void)
{
	votequorum_callbacks_t vq_callbacks;
	quorum_callbacks_t q_callbacks;
	int ret = 0;
	int retry = 3;
	int fd;

	if (vq_handle == 0) {
		qb_log (LOG_INFO, "votequorum_initialize");
		vq_callbacks.votequorum_quorum_notify_fn = votequorum_notification_fn;
		vq_callbacks.votequorum_expectedvotes_notify_fn = NULL;
		ret = CS_ERR_NOT_EXIST;
		while (ret == CS_ERR_NOT_EXIST && retry > 0) {
			ret = votequorum_initialize (&vq_handle, &vq_callbacks);
			if (ret == CS_ERR_NOT_EXIST) {
				sleep (1);
				retry--;
			}
		}
		if (ret != CS_OK) {
			qb_log (LOG_ERR, "votequorum_initialize FAILED: %d", ret);
			vq_handle = 0;
		}
		else {
			ret = votequorum_trackstart (vq_handle, vq_handle, CS_TRACK_CHANGES);
			if (ret != CS_OK) {
				qb_log (LOG_ERR, "votequorum_trackstart FAILED: %d", ret);
			}

			votequorum_fd_get (vq_handle, &fd);
			qb_loop_poll_add (ta_poll_handle_get(), QB_LOOP_MED, fd,
				POLLIN|POLLNVAL, NULL, vq_dispatch_wrapper_fn);
		}
	}
	if (q_handle == 0) {
		uint32_t q_type;
		qb_log (LOG_INFO, "quorum_initialize");
		q_callbacks.quorum_notify_fn = quorum_notification_fn;
		ret = quorum_initialize (&q_handle, &q_callbacks, &q_type);
		if (ret != CS_OK) {
			qb_log (LOG_ERR, "quorum_initialize FAILED: %d", ret);
			q_handle = 0;
		}
		else {
			ret = quorum_trackstart (q_handle, CS_TRACK_CHANGES);
			if (ret != CS_OK) {
				qb_log (LOG_ERR, "quorum_trackstart FAILED: %d", ret);
			}
			quorum_fd_get (q_handle, &fd);
			qb_loop_poll_add (ta_poll_handle_get(), QB_LOOP_MED, fd,
				POLLIN|POLLNVAL, NULL, q_dispatch_wrapper_fn);
		}
	}
	return ret;
}

static void getinfo (int sock)
{
	int ret;
	struct votequorum_info info;
	char response[100];
	ssize_t rc;
	size_t send_len;

	q_lib_init ();

	ret = votequorum_getinfo(vq_handle, 0, &info);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "votequorum_getinfo FAILED: %d", ret);
		goto send_response;
	}

	snprintf (response, 100, "%d:%d:%d:%d:%d",
		info.node_votes,
		info.node_expected_votes,
		info.highest_expected,
		info.total_votes,
		info.quorum);

send_response:
	send_len = strlen (response);
	rc = send (sock, response, send_len, 0);
	assert(rc == send_len);
}


static void setexpected (int sock, char *arg)
{
	int ret;
	char response[100];
	ssize_t rc;
	size_t send_len;

	q_lib_init ();

	ret = votequorum_setexpected (vq_handle, atoi(arg));
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "set expected votes FAILED: %d", ret);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	send_len = strlen (response);
	rc = send (sock, response, send_len, 0);
	assert(rc == send_len);
}

static void setvotes (int sock, char *arg)
{
	int ret;
	char response[100];
	ssize_t rc;
	size_t send_len;

	q_lib_init ();

	ret = votequorum_setvotes (vq_handle, 0, atoi(arg));
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "set votes FAILED: %d", ret);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	send_len = strlen (response);
	rc = send (sock, response, send_len, 0);
	assert(rc == send_len);
}


static void getquorate (int sock)
{
	int ret;
	int quorate;
	char response[100];
	ssize_t rc;
	size_t send_len;

	q_lib_init ();

	ret = quorum_getquorate (q_handle, &quorate);
	if (ret != CS_OK) {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "getquorate FAILED: %d", ret);
		goto send_response;
	}

	snprintf (response, 100, "%d", quorate);

send_response:
	send_len = strlen (response);
	rc = send (sock, response, send_len, 0);
	assert(rc == send_len);
}

static void context_test (int sock)
{
	char response[100];
	char *cmp;
	cs_error_t rc1;
	cs_error_t rc2;
	ssize_t send_rc;
	size_t send_len;

	snprintf (response, 100, "%s", OK_STR);

	rc1 = votequorum_context_set (vq_handle, response);
	rc2 = votequorum_context_get (vq_handle, (void**)&cmp);
	if (response != cmp) {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "votequorum context not the same %d %d",
			rc1, rc2);
	}

	rc1 = quorum_context_set (q_handle, response);
	rc2 = quorum_context_get (q_handle, (const void**)&cmp);
	if (response != cmp) {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "quorum context not the same %d %d",
			rc1, rc2);
	}
	send_len = strlen (response);
	send_rc = send (sock, response, send_len, 0);
	assert(send_rc == send_len);
}

static void do_command (int sock, char* func, char*args[], int num_args)
{
	char response[100];
	ssize_t rc;
	size_t send_len;

	q_lib_init ();

	qb_log (LOG_INFO,"RPC:%s() called.", func);

	if (strcmp ("votequorum_getinfo", func) == 0) {
		getinfo (sock);
	} else if (strcmp ("votequorum_setvotes", func) == 0) {
		setvotes (sock, args[0]);
	} else if (strcmp ("votequorum_setexpected", func) == 0) {
		setexpected (sock, args[0]);
	} else if (strcmp ("quorum_getquorate", func) == 0) {
		getquorate (sock);
	} else if (strcmp ("context_test", func) == 0) {
		context_test (sock);
	} else if (strcmp ("are_you_ok_dude", func) == 0 ||
	           strcmp ("init", func) == 0) {
		snprintf (response, 100, "%s", OK_STR);
		goto send_response;
	} else {
		qb_log (LOG_ERR,"%s RPC:%s not supported!", __func__, func);
		snprintf (response, 100, "%s", NOT_SUPPORTED_STR);
		goto send_response;
	}

	return ;
send_response:
	send_len = strlen (response);
	rc = send (sock, response, send_len, 0);
	assert(rc == send_len);
}

static void my_pre_exit(void)
{
	qb_log (LOG_INFO, "PRE EXIT");
	if (vq_handle) {
		votequorum_finalize(vq_handle);
		vq_handle = 0;
	}
	if (q_handle) {
		quorum_finalize(q_handle);
		q_handle = 0;
	}
}

int
main(int argc, char *argv[])
{
	return test_agent_run ("quorum_test_agent", 9037, do_command, my_pre_exit);
}
