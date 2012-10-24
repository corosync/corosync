/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2007, 2009 Red Hat, Inc.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <corosync/corotypes.h>
#include <corosync/swab.h>
#include <corosync/totem/totempg.h>
#include <corosync/totem/totem.h>
#include <corosync/lcr/lcr_ifact.h>
#include <corosync/engine/logsys.h>
#include <corosync/coroipc_types.h>
#include "quorum.h"
#include "sync.h"

LOGSYS_DECLARE_SUBSYS ("SYNC");

#define MESSAGE_REQ_SYNC_BARRIER 0

struct barrier_data {
	unsigned int nodeid;
	int completed;
};

static const struct memb_ring_id *sync_ring_id;

static int (*sync_callbacks_retrieve) (int sync_id, struct sync_callbacks *callack);

static void (*sync_started) (
	const struct memb_ring_id *ring_id);

static void (*sync_aborted) (void);

static struct sync_callbacks sync_callbacks;

static int sync_processing = 0;

static void (*sync_next_start) (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int sync_recovery_index = 0;

static int my_processing_idx = 0;

static void *sync_callback_token_handle = 0;

static struct barrier_data barrier_data_confchg[PROCESSOR_COUNT_MAX];

static size_t barrier_data_confchg_entries;

static struct barrier_data barrier_data_process[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_trans_list[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list_entries;

static unsigned int my_trans_list_entries;

static int sync_barrier_send (const struct memb_ring_id *ring_id);

static int sync_start_process (enum totem_callback_token_type type,
			       const void *data);

static void sync_service_init (struct memb_ring_id *ring_id);

static int sync_service_process (enum totem_callback_token_type type,
				 const void *data);

static void sync_deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required);

static void sync_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list,
	size_t member_list_entries,
	const unsigned int *left_list,
	size_t left_list_entries,
	const unsigned int *joined_list,
	size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

static void sync_primary_callback_fn (
        const unsigned int *view_list,
        size_t view_list_entries,
        int primary_designated,
        const struct memb_ring_id *ring_id);


static struct totempg_group sync_group = {
    .group      = "sync",
    .group_len  = 4
};

static hdb_handle_t sync_group_handle;

struct req_exec_sync_barrier_start {
	coroipc_request_header_t header;
	struct memb_ring_id ring_id;
};

/*
 * Send a barrier data structure
 */
static int sync_barrier_send (const struct memb_ring_id *ring_id)
{
	struct req_exec_sync_barrier_start req_exec_sync_barrier_start;
	struct iovec iovec;
	int res;

	req_exec_sync_barrier_start.header.size = sizeof (struct req_exec_sync_barrier_start);
	req_exec_sync_barrier_start.header.id = MESSAGE_REQ_SYNC_BARRIER;

	memcpy (&req_exec_sync_barrier_start.ring_id, ring_id,
		sizeof (struct memb_ring_id));

	iovec.iov_base = (char *)&req_exec_sync_barrier_start;
	iovec.iov_len = sizeof (req_exec_sync_barrier_start);

	res = totempg_groups_mcast_joined (sync_group_handle, &iovec, 1, TOTEMPG_AGREED);

	return (res);
}

static void sync_start_init (const struct memb_ring_id *ring_id)
{
	totempg_callback_token_create (
		&sync_callback_token_handle,
		TOTEM_CALLBACK_TOKEN_SENT,
		0, /* don't delete after callback */
		sync_start_process,
		ring_id);
}

static void sync_service_init (struct memb_ring_id *ring_id)
{
	if (sync_callbacks.api_version == 1) {
		if (sync_callbacks_retrieve(my_processing_idx, NULL) != -1) {
			sync_callbacks.sync_init_api.sync_init_v1 (my_member_list,
				my_member_list_entries, ring_id);
		}
	} else {
		if (sync_callbacks_retrieve(my_processing_idx, NULL) != -1) {
			sync_callbacks.sync_init_api.sync_init_v2 (my_trans_list,
				my_trans_list_entries,
				my_member_list, my_member_list_entries, ring_id);
		}
	}
	totempg_callback_token_destroy (&sync_callback_token_handle);

	/*
	 * Create the token callback for the processing
	 */
	totempg_callback_token_create (
		&sync_callback_token_handle,
		TOTEM_CALLBACK_TOKEN_SENT,
		0, /* don't delete after callback */
		sync_service_process,
		ring_id);
}

static int sync_start_process (enum totem_callback_token_type type,
			       const void *data)
{
	int res;
	const struct memb_ring_id *ring_id = data;

	res = sync_barrier_send (ring_id);
	if (res == 0) {
		/*
		 * Delete the token callback for the barrier
		 */
		totempg_callback_token_destroy (&sync_callback_token_handle);
	}
	return (0);
}

static void sync_callbacks_load (void)
{
	int res;

	for (;;) {
		res = sync_callbacks_retrieve (sync_recovery_index,
			&sync_callbacks);

		/*
		 * No more service handlers have sync callbacks at this time
	`	 */
		if (res == -1) {
			sync_processing = 0;
			break;
		}
		my_processing_idx = sync_recovery_index;
		sync_recovery_index += 1;
		if (sync_callbacks.sync_init_api.sync_init_v1) {
			break;
		}
	}
}

static int sync_service_process (enum totem_callback_token_type type,
				 const void *data)
{
	int res;
	const struct memb_ring_id *ring_id = data;


	/*
	 * If process operation not from this ring id, then ignore it and stop
	 * processing
	 */
	if (memcmp (ring_id, sync_ring_id, sizeof (struct memb_ring_id)) != 0) {
		return (0);
	}

	/*
	 * If process returns 0, then its time to activate
	 * and start the next service's synchronization
	 */
	if (sync_callbacks_retrieve(my_processing_idx, NULL) != -1) {
		res = sync_callbacks.sync_process ();
	} else {
		res = 0;
	}
	if (res != 0) {
		return (0);
	}
	totempg_callback_token_destroy (&sync_callback_token_handle);

	sync_start_init (ring_id);

	return (0);
}

int sync_register (
	int (*callbacks_retrieve) (
		int sync_id,
		struct sync_callbacks *callbacks),

	void (*started) (
		const struct memb_ring_id *ring_id),

	void (*aborted) (void),

	void (*next_start) (
		const unsigned int *member_list,
		size_t member_list_entries,
		const struct memb_ring_id *ring_id))
{
	unsigned int res;

	res = totempg_groups_initialize (
		&sync_group_handle,
		sync_deliver_fn,
		sync_confchg_fn);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"Couldn't initialize groups interface.\n");
		return (-1);
	}

	res = totempg_groups_join (
		sync_group_handle,
		&sync_group,
		1);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Couldn't join group.\n");
		return (-1);
	}

	sync_callbacks_retrieve = callbacks_retrieve;
	sync_next_start = next_start;
	sync_started = started;
	sync_aborted = aborted;
	return (0);
}


static void sync_primary_callback_fn (
	const unsigned int *view_list,
	size_t view_list_entries,
	int primary_designated,
	const struct memb_ring_id *ring_id)
{
	int i;

	if (primary_designated) {
		log_printf (LOGSYS_LEVEL_DEBUG, "This node is within the primary component and will provide service.\n");
	} else {
		log_printf (LOGSYS_LEVEL_DEBUG, "This node is within the non-primary component and will NOT provide any services.\n");
		return;
	}

	/*
	 * Execute configuration change for synchronization service
	 */
	sync_processing = 1;

	totempg_callback_token_destroy (&sync_callback_token_handle);

	sync_recovery_index = 0;
	my_processing_idx = 0;
	memset (&barrier_data_confchg, 0, sizeof (barrier_data_confchg));
	for (i = 0; i < view_list_entries; i++) {
		barrier_data_confchg[i].nodeid = view_list[i];
		barrier_data_confchg[i].completed = 0;
	}
	memcpy (barrier_data_process, barrier_data_confchg,
		sizeof (barrier_data_confchg));
	barrier_data_confchg_entries = view_list_entries;
	sync_start_init (sync_ring_id);
}

static struct memb_ring_id deliver_ring_id;

static void sync_endian_convert (struct req_exec_sync_barrier_start
				 *req_exec_sync_barrier_start)
{
	totemip_copy_endian_convert(&req_exec_sync_barrier_start->ring_id.rep,
		&req_exec_sync_barrier_start->ring_id.rep);
	req_exec_sync_barrier_start->ring_id.seq = swab64 (req_exec_sync_barrier_start->ring_id.seq);

}

static void sync_deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required)
{
	struct req_exec_sync_barrier_start *req_exec_sync_barrier_start =
		(struct req_exec_sync_barrier_start *)msg;
	unsigned int barrier_completed;
	int i;

	log_printf (LOGSYS_LEVEL_DEBUG, "confchg entries %lu\n",
		    (unsigned long int) barrier_data_confchg_entries);
	if (endian_conversion_required) {
		sync_endian_convert (req_exec_sync_barrier_start);
	}

	if (sync_ring_id == NULL) {
		log_printf (LOGSYS_LEVEL_DEBUG,
		    "Initial sync was not yet proceed. Ignoring sync msg\n");
		return ;
	}

	barrier_completed = 1;

	memcpy (&deliver_ring_id, &req_exec_sync_barrier_start->ring_id,
		sizeof (struct memb_ring_id));

	/*
	 * Is this barrier from this configuration, if not, ignore it
	 */
	if (memcmp (&req_exec_sync_barrier_start->ring_id, sync_ring_id,
		sizeof (struct memb_ring_id)) != 0) {
		return;
	}

	/*
	 * Set completion for source_addr's address
	 */
	for (i = 0; i < barrier_data_confchg_entries; i++) {
		if (nodeid == barrier_data_process[i].nodeid) {
			barrier_data_process[i].completed = 1;
			log_printf (LOGSYS_LEVEL_DEBUG,
				"Barrier Start Received From %d\n",
				barrier_data_process[i].nodeid);
			break;
		}
	}

	/*
	 * Test if barrier is complete
	 */
	for (i = 0; i < barrier_data_confchg_entries; i++) {
		log_printf (LOGSYS_LEVEL_DEBUG,
			"Barrier completion status for nodeid %d = %d. \n",
			barrier_data_process[i].nodeid,
			barrier_data_process[i].completed);
		if (barrier_data_process[i].completed == 0) {
			barrier_completed = 0;
		}
	}
	if (barrier_completed) {
		log_printf (LOGSYS_LEVEL_DEBUG,
			"Synchronization barrier completed\n");
	}
	/*
	 * This sync is complete so activate and start next service sync
	 */
	if (barrier_completed && sync_callbacks.sync_activate) {
		if (sync_callbacks_retrieve(my_processing_idx, NULL) != -1) {
			sync_callbacks.sync_activate ();
		}

		log_printf (LOGSYS_LEVEL_DEBUG,
			"Committing synchronization for (%s)\n",
			sync_callbacks.name);
	}

	/*
	 * Start synchronization if the barrier has completed
	 */
	if (barrier_completed) {
		memcpy (barrier_data_process, barrier_data_confchg,
			sizeof (barrier_data_confchg));

		sync_callbacks_load();

		/*
		 * if sync service found, execute it
		 */
		if (sync_processing && sync_callbacks.sync_init_api.sync_init_v1) {
			log_printf (LOGSYS_LEVEL_DEBUG,
				"Synchronization actions starting for (%s)\n",
				sync_callbacks.name);
			sync_service_init (&deliver_ring_id);
		}
		if (sync_processing == 0) {
			sync_next_start (my_member_list, my_member_list_entries, sync_ring_id);
		}
	}
	return;
}

static void sync_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list,
	size_t member_list_entries,
	const unsigned int *left_list,
	size_t left_list_entries,
	const unsigned int *joined_list,
	size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	sync_ring_id = ring_id;

	if (configuration_type != TOTEM_CONFIGURATION_REGULAR) {
		memcpy (my_trans_list, member_list, member_list_entries *
			sizeof (unsigned int));
		my_trans_list_entries = member_list_entries;
		return;
	}
	memcpy (my_member_list, member_list, member_list_entries * sizeof (unsigned int));
	my_member_list_entries = member_list_entries;

	sync_aborted ();
	if (sync_processing && sync_callbacks.sync_abort != NULL) {
		if (sync_callbacks_retrieve(my_processing_idx, NULL) != -1) {
			sync_callbacks.sync_abort ();
		}
		sync_callbacks.sync_activate = NULL;
	}

	sync_started (
		ring_id);

	sync_primary_callback_fn (
		member_list,
		member_list_entries,
		1,
		ring_id);
}
