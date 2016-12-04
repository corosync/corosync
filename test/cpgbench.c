/*
 * Copyright (c) 2006, 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <qb/qblog.h>
#include <qb/qbutil.h>

#include <corosync/corotypes.h>
#include <corosync/cpg.h>

static cpg_handle_t handle;

static pthread_t thread;

#ifndef timersub
#define timersub(a, b, result)						\
	do {								\
		(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
		(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
		if ((result)->tv_usec < 0) {				\
			--(result)->tv_sec;				\
			(result)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif /* timersub */

static int alarm_notice;

static void cpg_bm_confchg_fn (
	cpg_handle_t handle_in,
	const struct cpg_name *group_name,
	const struct cpg_address *member_list, size_t member_list_entries,
	const struct cpg_address *left_list, size_t left_list_entries,
	const struct cpg_address *joined_list, size_t joined_list_entries)
{
}

static unsigned int write_count;

static void cpg_bm_deliver_fn (
        cpg_handle_t handle_in,
        const struct cpg_name *group_name,
        uint32_t nodeid,
        uint32_t pid,
        void *msg,
        size_t msg_len)
{
	write_count++;
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn 	= cpg_bm_deliver_fn,
	.cpg_confchg_fn		= cpg_bm_confchg_fn
};

#define ONE_MEG 1048576
static char data[ONE_MEG];

static void cpg_benchmark (
	cpg_handle_t handle_in,
	int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct iovec iov;
	unsigned int res;

	alarm_notice = 0;
	iov.iov_base = data;
	iov.iov_len = write_size;

	write_count = 0;
	alarm (10);

	gettimeofday (&tv1, NULL);
	do {
		res = cpg_mcast_joined (handle_in, CPG_TYPE_AGREED, &iov, 1);
	} while (alarm_notice == 0 && (res == CS_OK || res == CS_ERR_TRY_AGAIN));
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d messages received ", write_count);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ",
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)write_count) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n",
		((float)write_count) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}

static void sigalrm_handler (int num)
{
	alarm_notice = 1;
}

static struct cpg_name group_name = {
	.value = "cpg_bm",
	.length = 6
};

static void* dispatch_thread (void *arg)
{
	cpg_dispatch (handle, CS_DISPATCH_BLOCKING);
	return NULL;
}

int main (void) {
	unsigned int size;
	int i;
	unsigned int res;

	qb_log_init("cpgbench", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	size = 64;
	signal (SIGALRM, sigalrm_handler);
	res = cpg_initialize (&handle, &callbacks);
	if (res != CS_OK) {
		printf ("cpg_initialize failed with result %d\n", res);
		exit (1);
	}
	pthread_create (&thread, NULL, dispatch_thread, NULL);

	res = cpg_join (handle, &group_name);
	if (res != CS_OK) {
		printf ("cpg_join failed with result %d\n", res);
		exit (1);
	}

	for (i = 0; i < 10; i++) { /* number of repetitions - up to 50k */
		cpg_benchmark (handle, size);
		signal (SIGALRM, sigalrm_handler);
		size *= 5;
		if (size >= (ONE_MEG - 100)) {
			break;
		}
	}

	res = cpg_finalize (handle);
	if (res != CS_OK) {
		printf ("cpg_finalize failed with result %d\n", res);
		exit (1);
	}
	return (0);
}
