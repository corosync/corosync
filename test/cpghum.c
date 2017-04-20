/*
 * Copyright (c) 2015 Red Hat, Inc.
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
#include <zlib.h>
#include <libgen.h>

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
#define ONE_MEG 1048576
#define DATASIZE (ONE_MEG*20)
static char data[DATASIZE];
static int send_counter = 0;
static int do_syslog = 0;
static int quiet = 0;
static volatile int stopped;

// stats
static unsigned int length_errors=0;
static unsigned int crc_errors=0;
static unsigned int sequence_errors=0;
static unsigned int packets_sent=0;
static unsigned int packets_recvd=0;
static unsigned int send_retries=0;
static unsigned int send_fails=0;

static void cpg_bm_confchg_fn (
	cpg_handle_t handle_in,
	const struct cpg_name *group_name,
	const struct cpg_address *member_list, size_t member_list_entries,
	const struct cpg_address *left_list, size_t left_list_entries,
	const struct cpg_address *joined_list, size_t joined_list_entries)
{
}

static unsigned int g_recv_count;
static unsigned int g_recv_length;
static unsigned int g_write_size;
static int g_recv_counter = 0;

static void cpg_bm_deliver_fn (
	cpg_handle_t handle_in,
	const struct cpg_name *group_name,
	uint32_t nodeid,
	uint32_t pid,
	void *msg,
	size_t msg_len)
{
	int *value = msg;
	uLong crc=0;
	uLong recv_crc = value[1] & 0xFFFFFFFF;

	packets_recvd++;
	g_recv_length = msg_len;

	// Basic check, packets should all be the right size
	if (g_write_size && (msg_len != g_write_size)) {
		length_errors++;
		fprintf(stderr, "%s: message sizes don't match. got %zu, expected %u\n", group_name->value, msg_len, g_write_size);
		if (do_syslog) {
			syslog(LOG_ERR, "%s: message sizes don't match. got %zu, expected %u\n", group_name->value, msg_len, g_write_size);
		}
	}

	// Sequence counters are incrementing in step?
	if (*value != g_recv_counter) {
		sequence_errors++;
		fprintf(stderr, "%s: counters don't match. got %d, expected %d\n", group_name->value, *value, g_recv_counter);
		if (do_syslog) {
			syslog(LOG_ERR, "%s: counters don't match. got %d, expected %d\n", group_name->value, *value, g_recv_counter);
		}
		// Catch up or we'll be printing errors for ever
		g_recv_counter = *value +1;
	} else {
		g_recv_counter++;
	}

	// Check crc
	crc = crc32(0, NULL, 0);
	crc = crc32(crc, (Bytef *)&value[2], msg_len-sizeof(int)*2) & 0xFFFFFFFF;
	if (crc != recv_crc) {
		crc_errors++;
		fprintf(stderr, "%s: CRCs don't match. got %lx, expected %lx\n", group_name->value, recv_crc, crc);
		if (do_syslog) {
			syslog(LOG_ERR, "%s: CRCs don't match. got %lx, expected %lx\n", group_name->value, recv_crc, crc);
		}
	}

	g_recv_count++;

}

static cpg_model_v1_data_t model1_data = {
	.cpg_deliver_fn		= cpg_bm_deliver_fn,
	.cpg_confchg_fn		= cpg_bm_confchg_fn,
};

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn		= cpg_bm_deliver_fn,
	.cpg_confchg_fn		= cpg_bm_confchg_fn
};

static struct cpg_name group_name = {
	.value = "cpghum",
	.length = 7
};

static void cpg_test (
	cpg_handle_t handle_in,
	int write_size,
	int delay_time,
	int print_time)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct iovec iov;
	unsigned int res;
	int i;
	unsigned int *dataint = (unsigned int *)data;
	uLong crc;

	alarm_notice = 0;
	iov.iov_base = data;
	iov.iov_len = write_size;

	g_recv_count = 0;
	alarm (print_time);

	gettimeofday (&tv1, NULL);
	do {
		dataint[0] = send_counter++;
		for (i=2; i<(DATASIZE-sizeof(int)*2)/4; i++) {
			dataint[i] = rand();
		}
		crc = crc32(0, NULL, 0);
		dataint[1] = crc32(crc, (Bytef*)&dataint[2], write_size-sizeof(int)*2);
	resend:
		res = cpg_mcast_joined (handle_in, CPG_TYPE_AGREED, &iov, 1);
		if (res == CS_ERR_TRY_AGAIN) {
			usleep(10000);
			send_retries++;
			goto resend;
		}
		if (res != CS_OK) {
			fprintf(stderr, "send failed: %d\n", res);
			send_fails++;
		}
		else {
			packets_sent++;
		}
		usleep(delay_time*1000);
	} while (alarm_notice == 0 && (res == CS_OK || res == CS_ERR_TRY_AGAIN) && stopped == 0);
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	if (!quiet) {
		printf ("%s: %5d message%s received, ", group_name.value, g_recv_count, g_recv_count==1?"":"s");
		printf ("%5d bytes per write\n", write_size);
	}

}

static void sigalrm_handler (int num)
{
	alarm_notice = 1;
}

static void sigint_handler (int num)
{
	stopped = 1;
}

static void* dispatch_thread (void *arg)
{
	cpg_dispatch (handle, CS_DISPATCH_BLOCKING);
	return NULL;
}

static void usage(char *cmd)
{
	fprintf(stderr, "%s [OPTIONS]\n", cmd);
	fprintf(stderr, "\n");
	fprintf(stderr, "%s sends CPG messages to all registered users of the CPG.\n", cmd);
	fprintf(stderr, "The messages have a sequence number and a CRC so that missing or\n");
	fprintf(stderr, "corrupted messages will be detected and reported.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "%s can also be asked to simply listen for (and check) packets\n", cmd);
	fprintf(stderr, "so that there is another node in the cluster connected to the CPG.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "When -l is present, packet size is only checked if specified by -w or -W\n");
	fprintf(stderr, "and it, obviously, must match that of the sender.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Multiple copies, in different CPGs, can also be run on the same or\n");
	fprintf(stderr, "different nodes by using the -n option.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "%s can't handle more than 1 sender in the same CPG as it messes with the\n", cmd);
	fprintf(stderr, "sequence numbers.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "	-w    Write size in Kbytes, default 4\n");
	fprintf(stderr, "	-W    Write size in bytes, default 4096\n");
	fprintf(stderr, "	-n    CPG name to use, default 'cpghum'\n");
	fprintf(stderr, "	-d    Delay between sending packets (mS), default 1000\n");
	fprintf(stderr, "	-r    Number of repetitions, default 100\n");
	fprintf(stderr, "	-p    Delay between printing output(S), default 10s\n");
	fprintf(stderr, "	-l    Listen and check CRCs only, don't send (^C to quit)\n");
	fprintf(stderr, "	-m    cpg_initialise() model. Default 1.\n");
	fprintf(stderr, "	-s    Also send errors to syslog (for daemon log correlation).\n");
	fprintf(stderr, "	-q    Quiet. Don't print messages every 10 seconds (see also -p)\n");
	fprintf(stderr, "\n");
}

int main (int argc, char *argv[]) {
	int i;
	unsigned int res;
	uint32_t maxsize;
	int opt;
	int bs;
	int write_size = 4096;
	int delay_time = 1000;
	int repetitions = 100;
	int print_time = 10;
	int have_size = 0;
	int listen_only = 0;
	int model = 1;

	while ( (opt = getopt(argc, argv, "qlsn:d:r:p:m:w:W:")) != -1 ) {
		switch (opt) {
		case 'w': // Write size in K
			bs = atoi(optarg);
			if (bs > 0) {
				write_size = bs*1024;
				have_size = 1;
			}
			break;
		case 'W': // Write size in bytes
			bs = atoi(optarg);
			if (bs > 0) {
				write_size = bs;
				have_size = 1;
			}
			break;
		case 'n':
			if (strlen(optarg) >= CPG_MAX_NAME_LENGTH) {
				fprintf(stderr, "CPG name too long\n");
				exit(1);
			}

			strcpy(group_name.value, optarg);
			group_name.length = strlen(group_name.value);
			break;
		case 'd':
			delay_time = atoi(optarg);
			break;
		case 'r':
			repetitions = atoi(optarg);
			break;
		case 'p':
			print_time = atoi(optarg);
			break;
		case 'l':
			listen_only = 1;
			break;
		case 's':
			do_syslog = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'm':
			model = atoi(optarg);
			if (model < 0 || model > 1) {
				fprintf(stderr, "%s: Model must be 0-1\n", argv[0]);
				exit(1);
			}
			break;
		case '?':
			usage(basename(argv[0]));
			exit(0);
		}
	}

	qb_log_init("cpghum", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	g_write_size = write_size;

	signal (SIGALRM, sigalrm_handler);
	signal (SIGINT, sigint_handler);
	switch (model) {
	case 0:
		res = cpg_initialize (&handle, &callbacks);
		break;
	case 1:
		res = cpg_model_initialize (&handle, CPG_MODEL_V1, (cpg_model_data_t *)&model1_data, NULL);
		break;
	default:
		res=999; // can't get here but it keeps the compiler happy
		break;
	}

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

	if (listen_only) {
		int secs = 0;
		if (!quiet) {
			printf("-- Listening on CPG %s\n", group_name.value);
			printf("-- Ignore any starting \"counters don't match\" error while we catch up\n");
		}

		/* Only check packet size if specified on the command-line */
		if (!have_size) {
			g_write_size = 0;
		}

		while (!stopped) {
			sleep(1);
			if (++secs > print_time && !quiet) {
				printf ("%s: %5d message%s received. %d bytes\n", group_name.value, g_recv_count, g_recv_count==1?"":"s", g_recv_length);
				secs = 0;
				g_recv_count = 0;
			}
		}
	}
	else {
		cpg_max_atomic_msgsize_get (handle, &maxsize);
		if ( write_size > maxsize) {
			fprintf(stderr, "INFO: packet size (%d) is larger than the maximum atomic size (%d), libcpg will fragment\n",
				write_size, maxsize);
		}
		for (i = 0; i < repetitions && !stopped; i++) {
			cpg_test (handle, write_size, delay_time, print_time);
			signal (SIGALRM, sigalrm_handler);
		}
	}

	res = cpg_finalize (handle);
	if (res != CS_OK) {
		printf ("cpg_finalize failed with result %d\n", res);
		exit (1);
	}

	printf("\n");
	printf("Stats:\n");
	if (!listen_only) {
		printf("   packets sent:    %d\n", packets_sent);
		printf("   send failures:   %d\n", send_fails);
		printf("   send retries:    %d\n", send_retries);
	}
	if (have_size) {
		printf("   length errors:   %d\n", length_errors);
	}
	printf("   packets recvd:   %d\n", packets_recvd);
	printf("   sequence errors: %d\n", sequence_errors);
	printf("   crc errors:	    %d\n", crc_errors);
	printf("\n");
	return (0);
}
