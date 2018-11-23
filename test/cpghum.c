/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
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
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <syslog.h>
#include <stdarg.h>
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
#include <getopt.h>

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
#define MAX_NODEID 65536
#define ONE_MEG 1048576
#define DATASIZE (ONE_MEG*20)
static char data[DATASIZE];
static int send_counter = 0;
static int do_syslog = 0;
static int quiet = 0;
static int report_rtt = 0;
static int abort_on_error = 0;
static int machine_readable = 0;
static char delimiter = ',';
static int to_stderr = 0;
static unsigned int g_our_nodeid;
static volatile int stopped;
static unsigned int flood_start = 64;
static unsigned int flood_multiplier = 5;
static unsigned long flood_max = (ONE_MEG - 100);

// stats
static unsigned int length_errors=0;
static unsigned int crc_errors=0;
static unsigned int sequence_errors=0;
static unsigned int packets_sent=0;
static unsigned int packets_recvd=0;
static unsigned int packets_recvd1=0; /* For flood intermediates */
static unsigned int send_retries=0;
static unsigned int send_fails=0;
static unsigned long avg_rtt=0;
static unsigned long max_rtt=0;
static unsigned long min_rtt=LONG_MAX;
static unsigned long interim_avg_rtt=0;
static unsigned long interim_max_rtt=0;
static unsigned long interim_min_rtt=LONG_MAX;

struct cpghum_header {
	unsigned int counter;
	unsigned int crc;
	unsigned int size;
	struct timeval timestamp;
};

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
static int g_recv_start[MAX_NODEID+1];
static int g_recv_counter[MAX_NODEID+1];
static int g_recv_size[MAX_NODEID+1];
static int g_log_mask = 0xFFFF;
typedef enum
{
	CPGH_LOG_INFO  = 1,
	CPGH_LOG_PERF  = 2,
	CPGH_LOG_RTT   = 4,
	CPGH_LOG_STATS = 8,
	CPGH_LOG_ERR   = 16
} log_type_t;

static void cpgh_print_message(int syslog_level, const char *facility_name, const char *format, va_list ap)
    __attribute__((format(printf, 3, 0)));

static void cpgh_log_printf(log_type_t type, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void cpgh_print_message(int syslog_level, const char *facility_name, const char *format, va_list ap)
{
	char msg[1024];
	int start = 0;

	if (machine_readable) {
		snprintf(msg, sizeof(msg), "%s%c", facility_name, delimiter);
		start = strlen(msg);
	}

	assert(vsnprintf(msg+start, sizeof(msg)-start, format, ap) < sizeof(msg)-start);

	if (to_stderr || (syslog_level <= LOG_ERR)) {
		fprintf(stderr, "%s", msg);
	}
	else {
		printf("%s", msg);
	}
	if (do_syslog) {
		syslog(syslog_level, "%s", msg);
	}
}

static void cpgh_log_printf(log_type_t type, const char *format, ...)
{
	va_list ap;

	if (!(type & g_log_mask)) {
		return;
	}

	va_start(ap, format);

	switch (type) {
	case CPGH_LOG_INFO:
		cpgh_print_message(LOG_INFO, "[Info]", format, ap);
		break;
	case CPGH_LOG_PERF:
		cpgh_print_message(LOG_INFO, "[Perf]", format, ap);
		break;
	case CPGH_LOG_RTT:
		cpgh_print_message(LOG_INFO, "[RTT]", format, ap);
		break;
	case CPGH_LOG_STATS:
		cpgh_print_message(LOG_INFO, "[Stats]", format, ap);
		break;
	case CPGH_LOG_ERR:
		cpgh_print_message(LOG_ERR, "[Err]", format, ap);
		break;
	default:
		break;
	}

	va_end(ap);
}

static unsigned long update_rtt(struct timeval *header_timestamp, int packet_count,
				unsigned long *rtt_min, unsigned long *rtt_avg, unsigned long *rtt_max)
{
	struct timeval tv1;
	struct timeval rtt;
	unsigned long rtt_usecs;

	gettimeofday (&tv1, NULL);
	timersub(&tv1, header_timestamp, &rtt);

	rtt_usecs = rtt.tv_usec + rtt.tv_sec*1000000;
	if (rtt_usecs > *rtt_max) {
		*rtt_max = rtt_usecs;
	}
	if (rtt_usecs < *rtt_min) {
		*rtt_min = rtt_usecs;
	}

	/* Don't start the average with 0 */
	if (*rtt_avg == 0) {
		*rtt_avg = rtt_usecs;
	}
	else {
		*rtt_avg = ((*rtt_avg * packet_count) + rtt_usecs) / (packet_count+1);
	}

	return rtt_usecs;
}


static void cpg_bm_deliver_fn (
	cpg_handle_t handle_in,
	const struct cpg_name *group_name,
	uint32_t nodeid,
	uint32_t pid,
	void *msg,
	size_t msg_len)
{
	uLong crc=0;
	struct cpghum_header *header = (struct cpghum_header *)msg;
	uLong recv_crc = header->crc & 0xFFFFFFFF;
	unsigned int *dataint = (unsigned int *)((char*)msg + sizeof(struct cpghum_header));
	unsigned int datalen;

	if (nodeid > MAX_NODEID) {
		cpgh_log_printf(CPGH_LOG_ERR, "Got message from invalid nodeid %d (too high for us). Quitting\n", nodeid);
		exit(1);
	}

	packets_recvd++;
	packets_recvd1++;
	g_recv_length = msg_len;
	datalen = header->size - sizeof(struct cpghum_header);

	// Report RTT first in case abort_on_error is set
	if (nodeid == g_our_nodeid) {
		unsigned long rtt_usecs;

		// For flood
		update_rtt(&header->timestamp, packets_recvd1, &interim_min_rtt, &interim_avg_rtt, &interim_max_rtt);

		rtt_usecs = update_rtt(&header->timestamp, g_recv_counter[nodeid], &min_rtt, &avg_rtt, &max_rtt);

		if (report_rtt) {
			if (machine_readable) {
				cpgh_log_printf(CPGH_LOG_RTT, "%ld%c%ld%c%ld%c%ld\n", rtt_usecs, delimiter, min_rtt, delimiter, avg_rtt, delimiter, max_rtt);
			}
			else {
				cpgh_log_printf(CPGH_LOG_RTT, "%s: RTT %ld uS (min/avg/max): %ld/%ld/%ld\n", group_name->value, rtt_usecs, min_rtt, avg_rtt, max_rtt);
			}
		}
	}

	// Basic check, packets should all be the right size
	if (msg_len != header->size) {
		length_errors++;
		cpgh_log_printf(CPGH_LOG_ERR, "%s: message sizes don't match. got %zu, expected %u from node %d\n", group_name->value, msg_len, header->size, nodeid);

		if (abort_on_error) {
			exit(2);
		}
	}
	g_recv_size[nodeid] = msg_len;

	// Sequence counters are incrementing in step?
	if (header->counter != g_recv_counter[nodeid]) {

		/* Don't report the first mismatch or a newly restarted sender, we're just catching up */
		if (g_recv_counter[nodeid] && header->counter) {
			sequence_errors++;
			cpgh_log_printf(CPGH_LOG_ERR, "%s: counters don't match. got %d, expected %d from node %d\n", group_name->value, header->counter, g_recv_counter[nodeid], nodeid);

			if (abort_on_error) {
				exit(2);
			}
		}
		else {
			g_recv_start[nodeid] = header->counter;
		}

		/* Catch up or we'll be printing errors for ever */
		g_recv_counter[nodeid] = header->counter+1;
	}
	else {
		g_recv_counter[nodeid]++;
	}

	/* Check crc */
	crc = crc32(0, NULL, 0);
	crc = crc32(crc, (Bytef *)dataint, datalen) & 0xFFFFFFFF;
	if (crc != recv_crc) {
		crc_errors++;
		cpgh_log_printf(CPGH_LOG_ERR, "%s: CRCs don't match. got %lx, expected %lx from nodeid %d\n", group_name->value, recv_crc, crc, nodeid);

		if (abort_on_error) {
			exit(2);
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

static void set_packet(int write_size, int counter)
{
	struct cpghum_header *header = (struct cpghum_header *)data;
	int i;
	unsigned int *dataint = (unsigned int *)(data + sizeof(struct cpghum_header));
	unsigned int datalen = write_size - sizeof(struct cpghum_header);
	struct timeval tv1;
	uLong crc;

	header->counter = counter;
	for (i=0; i<(datalen/4); i++) {
		dataint[i] = rand();
	}
	crc = crc32(0, NULL, 0);
	header->crc = crc32(crc, (Bytef*)&dataint[0], datalen);
	header->size = write_size;

	gettimeofday (&tv1, NULL);
	memcpy(&header->timestamp, &tv1, sizeof(struct timeval));
}

/* Basically this is cpgbench.c */
static void cpg_flood (
	cpg_handle_t handle_in,
	int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct iovec iov;
	unsigned int res = CS_OK;

	alarm_notice = 0;
	iov.iov_base = data;
	iov.iov_len = write_size;

	alarm (10);
	packets_recvd1 = 0;
	interim_avg_rtt = 0;
	interim_max_rtt = 0;
	interim_min_rtt = LONG_MAX;

	gettimeofday (&tv1, NULL);
	do {
		if (res == CS_OK) {
			set_packet(write_size, send_counter);
		}

		res = cpg_mcast_joined (handle_in, CPG_TYPE_AGREED, &iov, 1);
		if (res == CS_OK) {
			/* Only increment the packet counter if it was sucessfully sent */
			packets_sent++;
			send_counter++;
		}
		else {
			if (res == CS_ERR_TRY_AGAIN) {
				send_retries++;
			}
			else {
				send_fails++;
			}
		}
	} while (!stopped && alarm_notice == 0 && (res == CS_OK || res == CS_ERR_TRY_AGAIN));
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	if (!quiet) {
		if (machine_readable) {
			cpgh_log_printf (CPGH_LOG_PERF, "%d%c%d%c%f%c%f%c%f%c%ld%c%ld%c%ld\n", packets_recvd1, delimiter, write_size, delimiter,
					 (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)), delimiter,
					 ((float)packets_recvd1) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)), delimiter,
					 ((float)packets_recvd1) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0), delimiter,
					 interim_min_rtt, delimiter, interim_avg_rtt, delimiter, interim_max_rtt);
		}
		else {
			cpgh_log_printf (CPGH_LOG_PERF, "%5d messages received ", packets_recvd1);
			cpgh_log_printf (CPGH_LOG_PERF, "%5d bytes per write ", write_size);
			cpgh_log_printf (CPGH_LOG_PERF, "%7.3f Seconds runtime ",
					 (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
			cpgh_log_printf (CPGH_LOG_PERF, "%9.3f TP/s ",
					 ((float)packets_recvd1) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
			cpgh_log_printf (CPGH_LOG_PERF, "%7.3f MB/s ",
					 ((float)packets_recvd1) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
			cpgh_log_printf (CPGH_LOG_PERF, "RTT for this size (min/avg/max) %ld/%ld/%ld\n",
					 interim_min_rtt, interim_avg_rtt, interim_max_rtt);
		}
	}
}

static void cpg_test (
	cpg_handle_t handle_in,
	int write_size,
	int delay_time,
	int print_time)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct iovec iov;
	unsigned int res;

	alarm_notice = 0;
	iov.iov_base = data;
	iov.iov_len = write_size;

	g_recv_count = 0;
	alarm (print_time);

	do {
		send_counter++;
	resend:
		set_packet(write_size, send_counter);

		res = cpg_mcast_joined (handle_in, CPG_TYPE_AGREED, &iov, 1);
		if (res == CS_ERR_TRY_AGAIN) {
			usleep(10000);
			send_retries++;
			goto resend;
		}
		if (res != CS_OK) {
			cpgh_log_printf(CPGH_LOG_ERR, "send failed: %d\n", res);
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
		if (machine_readable) {
			cpgh_log_printf(CPGH_LOG_RTT,  "%d%c%ld%c%ld%c%ld\n", 0, delimiter, min_rtt, delimiter, avg_rtt, delimiter, max_rtt);
		}
		else {
			cpgh_log_printf(CPGH_LOG_PERF, "%s: %5d message%s received, ", group_name.value, g_recv_count, g_recv_count==1?"":"s");
			cpgh_log_printf(CPGH_LOG_PERF, "%5d bytes per write. ", write_size);
			cpgh_log_printf(CPGH_LOG_RTT, "RTT min/avg/max: %ld/%ld/%ld\n", min_rtt, avg_rtt, max_rtt);
		}
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
	fprintf(stderr, "Multiple copies, in different CPGs, can also be run on the same or\n");
	fprintf(stderr, "different nodes by using the -n option.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "%s can handle more than 1 sender in the same CPG provided they are on\n", cmd);
	fprintf(stderr, "different nodes.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, " -w<num>,  --size-bytes   Write size in Kbytes, default 4\n");
	fprintf(stderr, " -W<num>,  --size-kb      Write size in bytes, default 4096\n");
	fprintf(stderr, " -n<name>, --name         CPG name to use, default 'cpghum'\n");
	fprintf(stderr, " -M                       Write machine-readable results\n");
	fprintf(stderr, " -D<char>                 Delimiter for machine-readable results (default ',')\n");
	fprintf(stderr, " -E                       Send normal output to stderr instead of stdout\n");
	fprintf(stderr, " -d<num>, --delay         Delay between sending packets (mS), default 1000\n");
	fprintf(stderr, " -r<num>                  Number of repetitions, default 100\n");
	fprintf(stderr, " -p<num>                  Delay between printing output (seconds), default 10s\n");
	fprintf(stderr, " -l, --listen             Listen and check CRCs only, don't send (^C to quit)\n");
	fprintf(stderr, " -t, --rtt                Report Round Trip Times for each packet.\n");
	fprintf(stderr, " -m<num>                  cpg_initialise() model. Default 1.\n");
	fprintf(stderr, " -s                       Also send errors to syslog.\n");
	fprintf(stderr, " -f, --flood              Flood test CPG (cpgbench). see --flood-* long options\n");
	fprintf(stderr, " -a                       Abort on crc/length/sequence error\n");
	fprintf(stderr, " -q, --quiet              Quiet. Don't print messages every 10s (see also -p)\n");
	fprintf(stderr, " -qq                      Very quiet. Don't print stats at the end\n");
	fprintf(stderr, "     --flood-start=bytes  Start value for --flood\n");
	fprintf(stderr, "     --flood-mult=value   Packet size multiplier value for --flood\n");
	fprintf(stderr, "     --flood-max=bytes    Maximum packet size for --flood\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  values for --flood* and -W can have K or M suffixes to indicate\n");
	fprintf(stderr, "  Kilobytes or Megabytes\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "%s exit code is 0 if no error happened, 1 on generic error and 2 on\n", cmd);
	fprintf(stderr, "send/crc/length/sequence error");
	fprintf(stderr, "\n");
}

/* Parse a size, optionally ending in 'K', 'M' */
static long parse_bytes(const char *valstring)
{
	unsigned int value;
	int multiplier = 1;
	char suffix = '\0';
	int have_suffix = 0;

	/* Suffix is optional */
	if (sscanf(valstring, "%u%c", &value, &suffix) == 0) {
		return 0;
	}

	if (toupper(suffix) == 'M') {
		multiplier = 1024*1024;
		have_suffix = 1;
	}
	if (toupper(suffix) == 'K') {
		multiplier = 1024;
		have_suffix = 1;
	}

	if (!have_suffix && suffix != '\0') {
		fprintf(stderr, "Invalid suffix '%c', only K or M supported\n", suffix);
		return 0;
	}
	return value * multiplier;
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
	int flood = 0;
	int model = 1;
	int option_index = 0;
	struct option long_options[] = {
		{"flood-start", required_argument, 0,  0  },
		{"flood-mult",  required_argument, 0,  0  },
		{"flood-max",   required_argument, 0,  0  },
		{"size-kb",     required_argument, 0, 'w' },
		{"size-bytes",  required_argument, 0, 'W' },
		{"name",        required_argument, 0, 'n' },
		{"rtt",         no_argument,       0, 't' },
		{"flood",       no_argument,       0, 'f' },
		{"quiet",       no_argument,       0, 'q' },
		{"listen",      no_argument,       0, 'l' },
		{"help",        no_argument,       0, '?' },
		{0,             0,                 0,  0  }
	};

	while ( (opt = getopt_long(argc, argv, "qlstafMEn:d:r:p:m:w:W:D:",
				   long_options, &option_index)) != -1 ) {
		switch (opt) {
			case 0: // Long-only options
			if (strcmp(long_options[option_index].name, "flood-start") == 0) {
				flood_start = parse_bytes(optarg);
				if (flood_start == 0) {
					fprintf(stderr, "flood-start value invalid\n");
					exit(1);
				}
			}
			if (strcmp(long_options[option_index].name, "flood-mult") == 0) {
				flood_multiplier = parse_bytes(optarg);
				if (flood_multiplier == 0) {
					fprintf(stderr, "flood-mult value invalid\n");
					exit(1);
				}
			}
			if (strcmp(long_options[option_index].name, "flood-max") == 0) {
				flood_max = parse_bytes(optarg);
				if (flood_max == 0) {
					fprintf(stderr, "flood-max value invalid\n");
					exit(1);
				}
			}
			break;
		case 'w': // Write size in K
			bs = atoi(optarg);
			if (bs > 0) {
				write_size = bs*1024;
				have_size = 1;
			}
			break;
		case 'W': // Write size in bytes (or with a suffix)
			bs = parse_bytes(optarg);
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
		case 't':
			report_rtt = 1;
			break;
		case 'E':
			to_stderr = 1;
			break;
		case 'M':
			machine_readable = 1;
			break;
		case 'f':
			flood = 1;
			break;
		case 'a':
			abort_on_error = 1;
			break;
		case 'd':
			delay_time = atoi(optarg);
			break;
		case 'D':
			delimiter = optarg[0];
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
			quiet++;
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
			exit(1);
		}
	}

	if (!have_size && flood) {
		write_size = flood_start;
	}

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
		cpgh_log_printf(CPGH_LOG_ERR, "cpg_initialize failed with result %d\n", res);
		exit (1);
	}
	res = cpg_local_get(handle, &g_our_nodeid);
	if (res != CS_OK) {
		cpgh_log_printf(CPGH_LOG_ERR, "cpg_local_get failed with result %d\n", res);
		exit (1);
	}

	pthread_create (&thread, NULL, dispatch_thread, NULL);

	res = cpg_join (handle, &group_name);
	if (res != CS_OK) {
		cpgh_log_printf(CPGH_LOG_ERR, "cpg_join failed with result %d\n", res);
		exit (1);
	}

	if (listen_only) {
		int secs = 0;

		while (!stopped) {
			sleep(1);
			if (++secs > print_time && !quiet) {
				int nodes_printed = 0;

				if (!machine_readable) {
					for (i=1; i<MAX_NODEID; i++) {
						if (g_recv_counter[i]) {
							cpgh_log_printf(CPGH_LOG_INFO, "%s: %5d message%s of %d bytes received from node %d\n",
									group_name.value, g_recv_counter[i] - g_recv_start[i],
									g_recv_counter[i]==1?"":"s",
									g_recv_size[i], i);
							nodes_printed++;
						}
					}
				}

				/* Separate list of nodes if more than one */
				if (nodes_printed > 1) {
					cpgh_log_printf(CPGH_LOG_INFO, "\n");
				}
				secs = 0;
			}
		}
	}
	else {
		cpg_max_atomic_msgsize_get (handle, &maxsize);
		if (write_size > maxsize) {
			fprintf(stderr, "INFO: packet size (%d) is larger than the maximum atomic size (%d), libcpg will fragment\n",
				write_size, maxsize);
		}

		/* The main job starts here */
		if (flood) {
			for (i = 0; i < 10; i++) { /* number of repetitions - up to 50k */
				cpg_flood (handle, write_size);
				signal (SIGALRM, sigalrm_handler);
				write_size *= flood_multiplier;
				if (write_size > flood_max) {
					break;
				}
			}
		}
		else {
			send_counter = -1; /* So we start from zero to allow listeners to sync */
			for (i = 0; i < repetitions && !stopped; i++) {
				cpg_test (handle, write_size, delay_time, print_time);
				signal (SIGALRM, sigalrm_handler);
			}
		}
	}

	res = cpg_finalize (handle);
	if (res != CS_OK) {
		cpgh_log_printf(CPGH_LOG_ERR, "cpg_finalize failed with result %d\n", res);
		exit (1);
	}

	if (quiet < 2) {
		/* Don't print LONG_MAX for min_rtt if we don't have a value */
		if (min_rtt == LONG_MAX) {
			min_rtt = 0L;
		}

		if (machine_readable) {
			cpgh_log_printf(CPGH_LOG_STATS, "%d%c%d%c%d%c%d%c%d%c%d%c%d%c%ld%c%ld%c%ld\n",
					packets_sent, delimiter,
					send_fails, delimiter,
					send_retries, delimiter,
					length_errors, delimiter,
					packets_recvd, delimiter,
					sequence_errors, delimiter,
					crc_errors, delimiter,
					min_rtt, delimiter,
					avg_rtt, delimiter,
					max_rtt);
		}
		else {
			cpgh_log_printf(CPGH_LOG_STATS, "\n");
			cpgh_log_printf(CPGH_LOG_STATS, "Stats:\n");
			if (!listen_only) {
				cpgh_log_printf(CPGH_LOG_STATS, "   packets sent:    %d\n", packets_sent);
				cpgh_log_printf(CPGH_LOG_STATS, "   send failures:   %d\n", send_fails);
				cpgh_log_printf(CPGH_LOG_STATS, "   send retries:    %d\n", send_retries);
			}
			cpgh_log_printf(CPGH_LOG_STATS, "   length errors:   %d\n", length_errors);
			cpgh_log_printf(CPGH_LOG_STATS, "   packets recvd:   %d\n", packets_recvd);
			cpgh_log_printf(CPGH_LOG_STATS, "   sequence errors: %d\n", sequence_errors);
			cpgh_log_printf(CPGH_LOG_STATS, "   crc errors:	    %d\n", crc_errors);
			if (!listen_only) {
				cpgh_log_printf(CPGH_LOG_STATS, "   min RTT:         %ld\n", min_rtt);
				cpgh_log_printf(CPGH_LOG_STATS, "   max RTT:         %ld\n", max_rtt);
				cpgh_log_printf(CPGH_LOG_STATS, "   avg RTT:         %ld\n", avg_rtt);
			}
			cpgh_log_printf(CPGH_LOG_STATS, "\n");
		}
	}

	res = 0;

	if (send_fails > 0 || (have_size && length_errors > 0) || sequence_errors > 0 || crc_errors > 0) {
		res = 2;
	}

	return (res);
}
