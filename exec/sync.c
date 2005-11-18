/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/saAis.h"
#include "main.h"
#include "sync.h"
#include "totempg.h"
#include "totemsrp.h"
#include "print.h"

#define LOG_SERVICE LOG_SERVICE_SYNC

struct barrier_data {
	struct in_addr addr;
	int completed;
};

static struct memb_ring_id *sync_ring_id;

static totemsrp_handle sync_totemsrp_handle;

static struct sync_callbacks *sync_callbacks;

static int sync_callback_count;

static int sync_processing = 0;

static void (*sync_synchronization_completed) (void);

static int sync_recovery_index = 0;

static void *sync_callback_token_handle = 0;

static struct barrier_data barrier_data_confchg[PROCESSOR_COUNT_MAX];

static int barrier_data_confchg_entries;

static struct barrier_data barrier_data_process[PROCESSOR_COUNT_MAX];

static int sync_barrier_send (struct memb_ring_id *ring_id);

static int sync_start_process (enum totem_callback_token_type type, void *data);

static void sync_service_init (struct memb_ring_id *ring_id);

static int sync_service_process (enum totem_callback_token_type type, void *data);

struct req_exec_sync_barrier_start {
	struct req_header header;
	struct memb_ring_id ring_id;
};

/*
 * Send a barrier data structure
 */
static int sync_barrier_send (struct memb_ring_id *ring_id)
{
	struct req_exec_sync_barrier_start req_exec_sync_barrier_start;
	struct iovec iovec;
	int res;

	req_exec_sync_barrier_start.header.size = sizeof (struct req_exec_sync_barrier_start);
	req_exec_sync_barrier_start.header.id = MESSAGE_REQ_EXEC_SYNC_BARRIER;

	memcpy (&req_exec_sync_barrier_start.ring_id, ring_id,
		sizeof (struct memb_ring_id));

	iovec.iov_base = (char *)&req_exec_sync_barrier_start;
	iovec.iov_len = sizeof (req_exec_sync_barrier_start);

	res = totempg_mcast (&iovec, 1, TOTEMPG_AGREED);

	return (res);
}

void sync_start_init (struct memb_ring_id *ring_id)
{
	totemsrp_callback_token_create (sync_totemsrp_handle,
		&sync_callback_token_handle,
		TOTEM_CALLBACK_TOKEN_SENT,
		0, /* don't delete after callback */
		sync_start_process,
		(void *)ring_id);
}

static void sync_service_init (struct memb_ring_id *ring_id)
{
	sync_callbacks[sync_recovery_index].sync_init ();
	totemsrp_callback_token_destroy (sync_totemsrp_handle,
		&sync_callback_token_handle);

	/*
	 * Create the token callback for the processing
	 */
	totemsrp_callback_token_create (sync_totemsrp_handle,
		&sync_callback_token_handle,
		TOTEM_CALLBACK_TOKEN_SENT,
		0, /* don't delete after callback */
		sync_service_process,
		(void *)ring_id);
}

static int sync_start_process (enum totem_callback_token_type type, void *data)
{
	int res;
	struct memb_ring_id *ring_id = (struct memb_ring_id *)data;

	res = sync_barrier_send (ring_id);
	if (res == 0) {
		/*
		 * Delete the token callback for the barrier
		 */
		totemsrp_callback_token_destroy (sync_totemsrp_handle,
			&sync_callback_token_handle);
	}
	return (0);
}

static int sync_service_process (enum totem_callback_token_type type, void *data)
{
	int res;
	struct memb_ring_id *ring_id = (struct memb_ring_id *)data;

	
	/*
	 * If process returns 0, then its time to activate
	 * and start the next service's synchronization
	 */
	res = sync_callbacks[sync_recovery_index].sync_process ();
	if (res != 0) {
		return (0);
	}
	/*
	 * This sync is complete so activate and start next service sync
	 */
	sync_callbacks[sync_recovery_index].sync_activate ();
	totemsrp_callback_token_destroy (sync_totemsrp_handle,
		&sync_callback_token_handle);
	sync_recovery_index += 1;

	if (sync_recovery_index > sync_callback_count) {
		sync_processing = 0;
	} else {
		sync_start_init (ring_id);
	}
	return (0);
}

void sync_register (
	totemsrp_handle handle,
	struct sync_callbacks *callbacks,
	int callback_count,
	void (*synchronization_completed) (void))
{
	sync_totemsrp_handle = handle;
	sync_callbacks = callbacks;
	sync_callback_count = callback_count;
	sync_synchronization_completed = synchronization_completed;
}

void sync_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct in_addr *member_list, int member_list_entries,
	struct in_addr *left_list, int left_list_entries,
	struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;

	if (configuration_type != TOTEM_CONFIGURATION_REGULAR) {
		return;
	}

	sync_processing = 1;

	totemsrp_callback_token_destroy (sync_totemsrp_handle, &sync_callback_token_handle);

	sync_ring_id = ring_id;

	sync_recovery_index = 0;
	memset (&barrier_data_confchg, 0, sizeof (barrier_data_confchg));
	for (i = 0; i < member_list_entries; i++) {
		barrier_data_confchg[i].addr.s_addr = member_list[i].s_addr;
		barrier_data_confchg[i].completed = 0;
	}
	memcpy (barrier_data_process, barrier_data_confchg,
		sizeof (barrier_data_confchg));
	barrier_data_confchg_entries = member_list_entries;
	sync_start_init (ring_id);
}

static struct memb_ring_id deliver_ring_id;

int sync_deliver_fn (void *msg, struct in_addr source_addr,
	int endian_conversion_needed)
{
	struct req_exec_sync_barrier_start *req_exec_sync_barrier_start =
		(struct req_exec_sync_barrier_start *)msg;

	int i;

	int barrier_completed = 1;

	memcpy (&deliver_ring_id, &req_exec_sync_barrier_start->ring_id,
		sizeof (struct memb_ring_id));

	/*
	 * Is this barrier from this configuration, if not, ignore it
	 */
	if (memcmp (&req_exec_sync_barrier_start->ring_id, sync_ring_id,
		sizeof (struct memb_ring_id)) != 0) {
		
		return (0);
	}

	/*
	 * Set completion for source_addr's address
	 */
	for (i = 0; i < barrier_data_confchg_entries; i++) {
		if (source_addr.s_addr == barrier_data_process[i].addr.s_addr) {
			barrier_data_process[i].completed = 1;
			break;
		}
	}

	/*
	 * Test if barrier is complete
	 */
	for (i = 0; i < barrier_data_confchg_entries; i++) {
		if (barrier_data_process[i].completed == 0) {
			barrier_completed = 0;
		}
	}
	if (barrier_completed) {
		log_printf (LOG_LEVEL_NOTICE,
			"Synchronization barrier completed\n");
	}
	/*
	 * Start synchronization if the barrier has completed
	 */
	if (barrier_completed) {
		memcpy (barrier_data_process, barrier_data_confchg,
			sizeof (barrier_data_confchg));
		if (sync_recovery_index < sync_callback_count) {
			sync_service_init (&deliver_ring_id);
		} else {
			sync_processing = 0;
		}
	}
	return (0);
}

int sync_in_process (void)
{
	return (sync_processing);
}

