/*
 * Copyright (c) 2008-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Steven Dake (sdake@redhat.com)
 *          Fabio M. Di Nitto (fdinitto@redhat.com)
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

#include <qb/qblist.h>
#include <qb/qbutil.h>
#include <qb/qbipc_common.h>

#include <corosync/swab.h>
#include <corosync/corodefs.h>
#include <corosync/coroapi.h>
#include <corosync/icmap.h>
#include <corosync/logsys.h>

#include "service.h"
#include "util.h"

LOGSYS_DECLARE_SUBSYS ("PLOAD");

/*
 * Service Interfaces required by service_message_handler struct
 */
static struct corosync_api_v1 *api;

static char *pload_exec_init_fn (struct corosync_api_v1 *corosync_api);

/*
 * on wire / network bits
 */
enum pload_exec_message_req_types {
	MESSAGE_REQ_EXEC_PLOAD_START = 0,
	MESSAGE_REQ_EXEC_PLOAD_MCAST = 1
};

struct req_exec_pload_start {
	struct qb_ipc_request_header header;
	uint32_t msg_count;
	uint32_t msg_size;
};

struct req_exec_pload_mcast {
	struct qb_ipc_request_header header;
};

static void message_handler_req_exec_pload_start (const void *msg,
						  unsigned int nodeid);
static void req_exec_pload_start_endian_convert (void *msg);

static void message_handler_req_exec_pload_mcast (const void *msg,
						  unsigned int nodeid);
static void req_exec_pload_mcast_endian_convert (void *msg);

static struct corosync_exec_handler pload_exec_engine[] =
{
	{
		.exec_handler_fn 	= message_handler_req_exec_pload_start,
		.exec_endian_convert_fn	= req_exec_pload_start_endian_convert
	},
	{
		.exec_handler_fn 	= message_handler_req_exec_pload_mcast,
		.exec_endian_convert_fn	= req_exec_pload_mcast_endian_convert
	}
};

/*
 * internal bits and pieces
 */

/*
 * really unused buffer but we need to give something to iovec
 */
static char *buffer = NULL;

/*
 * wanted/size come from config
 * sent/delivered track the runtime status
 */
static uint32_t msgs_wanted = 0;
static uint32_t msg_size = 0;
static uint32_t msgs_sent = 0;
static uint32_t msgs_delivered = 0;

/*
 * bit flip to track if we are running or not and avoid multiple instances
 */
static uint8_t pload_started = 0;

/*
 * handle for scheduler
 */
static hdb_handle_t start_mcasting_handle;

/*
 * timing/profiling
 */
static unsigned long long int tv1;
static unsigned long long int tv2;
static unsigned long long int tv_elapsed;

/*
 * Service engine hooks
 */
struct corosync_service_engine pload_service_engine = {
	.name			= "corosync profile loading service",
	.id			= PLOAD_SERVICE,
	.priority		= 1,
	.flow_control		= CS_LIB_FLOW_CONTROL_REQUIRED,
	.exec_engine		= pload_exec_engine,
	.exec_engine_count	= sizeof (pload_exec_engine) / sizeof (struct corosync_exec_handler),
	.exec_init_fn		= pload_exec_init_fn
};

struct corosync_service_engine *pload_get_service_engine_ver0 (void)
{
	return (&pload_service_engine);
}

/*
 * internal use only functions
 */

/*
 * not all architectures / OSes define timersub in sys/time.h or time.h
 */

#ifndef timersub
#warning Using internal timersub definition. Check your include header files
#define timersub(a, b, result)					\
do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {				\
		--(result)->tv_sec;				\
		(result)->tv_usec += 1000000;			\
	}							\
} while (0)
#endif /* timersub */

/*
 * tell all cluster nodes to start mcasting
 */
static void pload_send_start (uint32_t count, uint32_t size)
{
	struct req_exec_pload_start req_exec_pload_start;
	struct iovec iov;

	req_exec_pload_start.header.id = SERVICE_ID_MAKE (PLOAD_SERVICE, MESSAGE_REQ_EXEC_PLOAD_START);
	req_exec_pload_start.msg_count = count;
	req_exec_pload_start.msg_size = size;
	iov.iov_base = (void *)&req_exec_pload_start;
	iov.iov_len = sizeof (struct req_exec_pload_start);

	api->totem_mcast (&iov, 1, TOTEM_AGREED);
}

/*
 * send N empty data messages of size X
 */
static int pload_send_message (const void *arg)
{
	struct req_exec_pload_mcast req_exec_pload_mcast;
	struct iovec iov[2];
	unsigned int res;
	unsigned int iov_len = 1;

	req_exec_pload_mcast.header.id = SERVICE_ID_MAKE (PLOAD_SERVICE, MESSAGE_REQ_EXEC_PLOAD_MCAST);
	req_exec_pload_mcast.header.size = sizeof (struct req_exec_pload_mcast) + msg_size;

	iov[0].iov_base = (void *)&req_exec_pload_mcast;
	iov[0].iov_len = sizeof (struct req_exec_pload_mcast);
	if (msg_size > sizeof (req_exec_pload_mcast)) {
		iov[1].iov_base = &buffer;
		iov[1].iov_len = msg_size - sizeof (req_exec_pload_mcast);
		iov_len = 2;
	}

	do {
		res = api->totem_mcast (iov, iov_len, TOTEM_AGREED);
		if (res == -1) {
			break;
		} else {
			msgs_sent++;
		}
	} while (msgs_sent < msgs_wanted);

	if (msgs_sent == msgs_wanted) {
		return (0);
	} else {
		return (-1);
	}
}

/*
 * hook into icmap to read config at runtime
 * we do NOT start by default, ever!
 */
static void pload_read_config(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	uint32_t pload_count = 1500000;
	uint32_t pload_size = 300;
	char *pload_start = NULL;

	icmap_get_uint32("pload.count", &pload_count);
	icmap_get_uint32("pload.size", &pload_size);

	if (pload_size > MESSAGE_SIZE_MAX) {
		pload_size = MESSAGE_SIZE_MAX;
		log_printf(LOGSYS_LEVEL_WARNING, "pload size limited to %u", pload_size);
	}

	if ((!pload_started) &&
	    (icmap_get_string("pload.start", &pload_start) == CS_OK)) {
		if (!strcmp(pload_start,
			    "i_totally_understand_pload_will_crash_my_cluster_and_kill_corosync_on_exit")) {
			buffer = malloc(pload_size);
			if (buffer) {
				log_printf(LOGSYS_LEVEL_WARNING, "Starting pload!");
				pload_send_start(pload_count,  pload_size);
			} else {
				log_printf(LOGSYS_LEVEL_WARNING,
					  "Unable to allocate pload buffer!");
			}
		}
		free(pload_start);
	}
}

/*
 * exec functions
 */
static char *pload_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	icmap_track_t pload_track = NULL;

	api = corosync_api;

	/*
	 * track changes to pload config and start only on demand
	 */
	if (icmap_track_add("pload.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		pload_read_config,
		NULL,
		&pload_track) != CS_OK) {
		return (char *)"Unable to setup pload config tracking!\n";
	}

	return NULL;
}

/*
 * network messages/onwire handlers
 */

static void req_exec_pload_start_endian_convert (void *msg)
{
	struct req_exec_pload_start *req_exec_pload_start = msg;

	req_exec_pload_start->msg_count = swab32(req_exec_pload_start->msg_count);
	req_exec_pload_start->msg_size = swab32(req_exec_pload_start->msg_size);
}

static void message_handler_req_exec_pload_start (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_pload_start *req_exec_pload_start = msg;

	/*
	 * don't start multiple instances
	 */
	if (pload_started) {
		return;
	}

	pload_started = 1;

	msgs_wanted = req_exec_pload_start->msg_count;
	msg_size = req_exec_pload_start->msg_size;

	api->schedwrk_create (
		&start_mcasting_handle,
		pload_send_message,
		&start_mcasting_handle);
}

static void req_exec_pload_mcast_endian_convert (void *msg)
{
}

static void message_handler_req_exec_pload_mcast (
	const void *msg,
	unsigned int nodeid)
{
	char log_buffer[1024];

	if (msgs_delivered == 0) {
		tv1 = qb_util_nano_current_get ();
	}
	msgs_delivered += 1;
	if (msgs_delivered == msgs_wanted) {
		tv2 = qb_util_nano_current_get ();
		tv_elapsed = tv2 - tv1;
		sprintf (log_buffer, "%5d Writes %d bytes per write %7.3f seconds runtime, %9.3f TP/S, %9.3f MB/S.",
			msgs_delivered,
			msg_size,
			(tv_elapsed / 1000000000.0),
			((float)msgs_delivered) /  (tv_elapsed / 1000000000.0),
			(((float)msgs_delivered) * ((float)msg_size) /
				(tv_elapsed / 1000000000.0)) / (1024.0 * 1024.0));
		log_printf (LOGSYS_LEVEL_NOTICE, "%s", log_buffer);
		log_printf (LOGSYS_LEVEL_WARNING, "Stopping corosync the hard way");
		if (buffer) {
			free(buffer);
			buffer = NULL;
		}
		exit(COROSYNC_DONE_PLOAD);
	}
}
