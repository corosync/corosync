/*
 * Copyright (c) 2009-2012 Red Hat, Inc.
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
#include <arpa/inet.h>

#include <corosync/corotypes.h>
#include <corosync/swab.h>
#include <corosync/totem/totempg.h>
#include <corosync/totem/totem.h>
#include <corosync/logsys.h>
#include <qb/qbipc_common.h>
#include "schedwrk.h"
#include "quorum.h"
#include "sync.h"
#include "main.h"

LOGSYS_DECLARE_SUBSYS ("SYNC");

#define MESSAGE_REQ_SYNC_BARRIER 0
#define MESSAGE_REQ_SYNC_SERVICE_BUILD 1
#define MESSAGE_REQ_SYNC_MEMB_DETERMINE 2

enum sync_process_state {
	INIT,
	PROCESS,
	ACTIVATE
};

enum sync_state {
	SYNC_SERVICELIST_BUILD,
	SYNC_PROCESS,
	SYNC_BARRIER
};

struct service_entry {
	int service_id;
	void (*sync_init) (
		const unsigned int *trans_list,
		size_t trans_list_entries,
		const unsigned int *member_list,
		size_t member_list_entries,
		const struct memb_ring_id *ring_id);
	void (*sync_abort) (void);
	int (*sync_process) (void);
	void (*sync_activate) (void);
	enum sync_process_state state;
	char name[128];
};

struct processor_entry {
	int nodeid;
	int received;
};

struct req_exec_memb_determine_message {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
};

struct req_exec_service_build_message {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	int service_list_entries __attribute__((aligned(8)));
	int service_list[128] __attribute__((aligned(8)));
};

struct req_exec_barrier_message {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
};

static enum sync_state my_state = SYNC_BARRIER;

static struct memb_ring_id my_ring_id;

static struct memb_ring_id my_memb_determine_ring_id;

static int my_memb_determine = 0;

static unsigned int my_memb_determine_list[PROCESSOR_COUNT_MAX];

static unsigned int my_memb_determine_list_entries = 0;

static int my_processing_idx = 0;

static hdb_handle_t my_schedwrk_handle;

static struct processor_entry my_processor_list[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_trans_list[PROCESSOR_COUNT_MAX];

static size_t my_member_list_entries = 0;

static size_t my_trans_list_entries = 0;

static int my_processor_list_entries = 0;

static struct service_entry my_service_list[SERVICES_COUNT_MAX];

static int my_service_list_entries = 0;

static void (*sync_synchronization_completed) (void);

static void sync_deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required);

static int schedwrk_processor (const void *context);

static void sync_process_enter (void);

static struct totempg_group sync_group = {
    .group      = "sync",
    .group_len  = 4
};

static void *sync_group_handle;

int (*my_sync_callbacks_retrieve) (
		int service_id,
                struct sync_callbacks *callbacks);

int sync_init (
        int (*sync_callbacks_retrieve) (
                int service_id,
                struct sync_callbacks *callbacks),
        void (*synchronization_completed) (void))
{
	unsigned int res;

	res = totempg_groups_initialize (
		&sync_group_handle,
		sync_deliver_fn,
		NULL);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"Couldn't initialize groups interface.");
		return (-1);
	}

	res = totempg_groups_join (
		sync_group_handle,
		&sync_group,
		1);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Couldn't join group.");
		return (-1);
	}

	sync_synchronization_completed = synchronization_completed;
	my_sync_callbacks_retrieve = sync_callbacks_retrieve;

	return (0);
}

static void sync_barrier_handler (unsigned int nodeid, const void *msg)
{
	const struct req_exec_barrier_message *req_exec_barrier_message = msg;
	int i;
	int barrier_reached = 1;

	if (memcmp (&my_ring_id, &req_exec_barrier_message->ring_id,
		sizeof (struct memb_ring_id)) != 0) {

		log_printf (LOGSYS_LEVEL_DEBUG, "barrier for old ring - discarding");
		return;
	}
	for (i = 0; i < my_processor_list_entries; i++) {
		if (my_processor_list[i].nodeid == nodeid) {
			my_processor_list[i].received = 1;
		}
	}
	for (i = 0; i < my_processor_list_entries; i++) {
		if (my_processor_list[i].received == 0) {
			barrier_reached = 0;
		}
	}
	if (barrier_reached) {
		log_printf (LOGSYS_LEVEL_DEBUG, "Committing synchronization for %s",
			my_service_list[my_processing_idx].name);
		my_service_list[my_processing_idx].state = ACTIVATE;

		if (my_sync_callbacks_retrieve(my_service_list[my_processing_idx].service_id, NULL) != -1) {
			my_service_list[my_processing_idx].sync_activate ();
		}

		my_processing_idx += 1;
		if (my_service_list_entries == my_processing_idx) {
			my_memb_determine_list_entries = 0;
			sync_synchronization_completed ();
		} else {
			sync_process_enter ();
		}
	}
}

static void dummy_sync_init (
	const unsigned int *trans_list,
	size_t trans_list_entries,
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
}

static void dummy_sync_abort (void)
{
}

static int dummy_sync_process (void)
{
	return (0);
}

static void dummy_sync_activate (void)
{
}

static int service_entry_compare (const void *a, const void *b)
{
	const struct service_entry *service_entry_a = a;
	const struct service_entry *service_entry_b = b;

	return (service_entry_a->service_id > service_entry_b->service_id);
}

static void sync_memb_determine (unsigned int nodeid, const void *msg)
{
	const struct req_exec_memb_determine_message *req_exec_memb_determine_message = msg;
	int found = 0;
	int i;

	if (memcmp (&req_exec_memb_determine_message->ring_id,
		&my_memb_determine_ring_id, sizeof (struct memb_ring_id)) != 0) {

		log_printf (LOGSYS_LEVEL_DEBUG, "memb determine for old ring - discarding");
		return;
	}

	my_memb_determine = 1;
	for (i = 0; i < my_memb_determine_list_entries; i++) {
		if (my_memb_determine_list[i] == nodeid) {
			found = 1;
		}
	}
	if (found == 0) {
		my_memb_determine_list[my_memb_determine_list_entries] = nodeid;
		my_memb_determine_list_entries += 1;
	}
}

static void sync_service_build_handler (unsigned int nodeid, const void *msg)
{
	const struct req_exec_service_build_message *req_exec_service_build_message = msg;
	int i, j;
	int barrier_reached = 1;
	int found;
	int qsort_trigger = 0;

	if (memcmp (&my_ring_id, &req_exec_service_build_message->ring_id,
		sizeof (struct memb_ring_id)) != 0) {
		log_printf (LOGSYS_LEVEL_DEBUG, "service build for old ring - discarding");
		return;
	}
	for (i = 0; i < req_exec_service_build_message->service_list_entries; i++) {

		found = 0;
		for (j = 0; j < my_service_list_entries; j++) {
			if (req_exec_service_build_message->service_list[i] ==
				my_service_list[j].service_id) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			my_service_list[my_service_list_entries].state =
				INIT;
			my_service_list[my_service_list_entries].service_id =
				req_exec_service_build_message->service_list[i];
			sprintf (my_service_list[my_service_list_entries].name,
				"Unknown External Service (id = %d)\n",
				req_exec_service_build_message->service_list[i]);
			my_service_list[my_service_list_entries].sync_init =
				dummy_sync_init;
			my_service_list[my_service_list_entries].sync_abort =
				dummy_sync_abort;
			my_service_list[my_service_list_entries].sync_process =
				dummy_sync_process;
			my_service_list[my_service_list_entries].sync_activate =
				dummy_sync_activate;
			my_service_list_entries += 1;

			qsort_trigger = 1;
		}
	}
	if (qsort_trigger) {
		qsort (my_service_list, my_service_list_entries,
			sizeof (struct service_entry), service_entry_compare);
	}
	for (i = 0; i < my_processor_list_entries; i++) {
		if (my_processor_list[i].nodeid == nodeid) {
			my_processor_list[i].received = 1;
		}
	}
	for (i = 0; i < my_processor_list_entries; i++) {
		if (my_processor_list[i].received == 0) {
			barrier_reached = 0;
		}
	}
	if (barrier_reached) {
		sync_process_enter ();
	}
}

static void sync_deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required)
{
	struct qb_ipc_request_header *header = (struct qb_ipc_request_header *)msg;

	switch (header->id) {
		case MESSAGE_REQ_SYNC_BARRIER:
			sync_barrier_handler (nodeid, msg);
			break;
		case MESSAGE_REQ_SYNC_SERVICE_BUILD:
			sync_service_build_handler (nodeid, msg);
			break;
		case MESSAGE_REQ_SYNC_MEMB_DETERMINE:
			sync_memb_determine (nodeid, msg);
			break;
	}
}

static void memb_determine_message_transmit (void)
{
	struct iovec iovec;
	struct req_exec_memb_determine_message req_exec_memb_determine_message;

	req_exec_memb_determine_message.header.size = sizeof (struct req_exec_memb_determine_message);
	req_exec_memb_determine_message.header.id = MESSAGE_REQ_SYNC_MEMB_DETERMINE;

	memcpy (&req_exec_memb_determine_message.ring_id,
		&my_memb_determine_ring_id,
		sizeof (struct memb_ring_id));

	iovec.iov_base = (char *)&req_exec_memb_determine_message;
	iovec.iov_len = sizeof (req_exec_memb_determine_message);

	(void)totempg_groups_mcast_joined (sync_group_handle,
		&iovec, 1, TOTEMPG_AGREED);
}

static void barrier_message_transmit (void)
{
	struct iovec iovec;
	struct req_exec_barrier_message req_exec_barrier_message;

	req_exec_barrier_message.header.size = sizeof (struct req_exec_barrier_message);
	req_exec_barrier_message.header.id = MESSAGE_REQ_SYNC_BARRIER;

	memcpy (&req_exec_barrier_message.ring_id, &my_ring_id,
		sizeof (struct memb_ring_id));

	iovec.iov_base = (char *)&req_exec_barrier_message;
	iovec.iov_len = sizeof (req_exec_barrier_message);

	(void)totempg_groups_mcast_joined (sync_group_handle,
		&iovec, 1, TOTEMPG_AGREED);
}

static void service_build_message_transmit (struct req_exec_service_build_message *service_build_message)
{
	struct iovec iovec;

	service_build_message->header.size = sizeof (struct req_exec_service_build_message);
	service_build_message->header.id = MESSAGE_REQ_SYNC_SERVICE_BUILD;

	memcpy (&service_build_message->ring_id, &my_ring_id,
		sizeof (struct memb_ring_id));

	iovec.iov_base = (void *)service_build_message;
	iovec.iov_len = sizeof (struct req_exec_service_build_message);

	(void)totempg_groups_mcast_joined (sync_group_handle,
		&iovec, 1, TOTEMPG_AGREED);
}

static void sync_barrier_enter (void)
{
	my_state = SYNC_BARRIER;
	barrier_message_transmit ();
}

static void sync_process_enter (void)
{
	int i;

	my_state = SYNC_PROCESS;

	/*
	 * No sync services
	 */
	if (my_service_list_entries == 0) {
		my_state = SYNC_SERVICELIST_BUILD;
		my_memb_determine_list_entries = 0;
		sync_synchronization_completed ();
		return;
	}
	for (i = 0; i < my_processor_list_entries; i++) {
		my_processor_list[i].received = 0;
	}
	schedwrk_create (&my_schedwrk_handle,
		schedwrk_processor,
		NULL);
}

static void sync_servicelist_build_enter (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	struct req_exec_service_build_message service_build;
	int i;
	int res;
	struct sync_callbacks sync_callbacks;

	my_state = SYNC_SERVICELIST_BUILD;
	for (i = 0; i < member_list_entries; i++) {
		my_processor_list[i].nodeid = member_list[i];
		my_processor_list[i].received = 0;
	}
	my_processor_list_entries = member_list_entries;

	memcpy (my_member_list, member_list,
		member_list_entries * sizeof (unsigned int));
	my_member_list_entries = member_list_entries;

	my_processing_idx = 0;

	memset(my_service_list, 0, sizeof (struct service_entry) * SERVICES_COUNT_MAX);
	my_service_list_entries = 0;

	for (i = 0; i < SERVICES_COUNT_MAX; i++) {
		res = my_sync_callbacks_retrieve (i, &sync_callbacks);
		if (res == -1) {
			continue;
		}
		if (sync_callbacks.sync_init == NULL) {
			continue;
		}
		my_service_list[my_service_list_entries].state = INIT;
		my_service_list[my_service_list_entries].service_id = i;
		strcpy (my_service_list[my_service_list_entries].name,
			sync_callbacks.name);
		my_service_list[my_service_list_entries].sync_init = sync_callbacks.sync_init;
		my_service_list[my_service_list_entries].sync_process = sync_callbacks.sync_process;
		my_service_list[my_service_list_entries].sync_abort = sync_callbacks.sync_abort;
		my_service_list[my_service_list_entries].sync_activate = sync_callbacks.sync_activate;
		my_service_list_entries += 1;
	}

	for (i = 0; i < my_service_list_entries; i++) {
		service_build.service_list[i] =
			my_service_list[i].service_id;
	}
	service_build.service_list_entries = my_service_list_entries;

	service_build_message_transmit (&service_build);
}

static int schedwrk_processor (const void *context)
{
	int res = 0;

	if (my_service_list[my_processing_idx].state == INIT) {
		unsigned int old_trans_list[PROCESSOR_COUNT_MAX];
		size_t old_trans_list_entries = 0;
		int o, m;
		my_service_list[my_processing_idx].state = PROCESS;

		memcpy (old_trans_list, my_trans_list, my_trans_list_entries *
			sizeof (unsigned int));
		old_trans_list_entries = my_trans_list_entries;

		my_trans_list_entries = 0;
		for (o = 0; o < old_trans_list_entries; o++) {
			for (m = 0; m < my_member_list_entries; m++) {
				if (old_trans_list[o] == my_member_list[m]) {
					my_trans_list[my_trans_list_entries] = my_member_list[m];
					my_trans_list_entries++;
					break;
				}
			}
		}

		if (my_sync_callbacks_retrieve(my_service_list[my_processing_idx].service_id, NULL) != -1) {
			my_service_list[my_processing_idx].sync_init (my_trans_list,
				my_trans_list_entries, my_member_list,
				my_member_list_entries,
				&my_ring_id);
		}
	}
	if (my_service_list[my_processing_idx].state == PROCESS) {
		my_service_list[my_processing_idx].state = PROCESS;
		if (my_sync_callbacks_retrieve(my_service_list[my_processing_idx].service_id, NULL) != -1) {
			res = my_service_list[my_processing_idx].sync_process ();
		} else {
			res = 0;
		}
		if (res == 0) {
			sync_barrier_enter();
		} else {
			return (-1);
		}
	}
	return (0);
}

void sync_start (
        const unsigned int *member_list,
        size_t member_list_entries,
        const struct memb_ring_id *ring_id)
{
	ENTER();
	memcpy (&my_ring_id, ring_id, sizeof (struct memb_ring_id));

	if (my_memb_determine) {
		my_memb_determine = 0;
		sync_servicelist_build_enter (my_memb_determine_list,
			my_memb_determine_list_entries, ring_id);
	} else {
		sync_servicelist_build_enter (member_list, member_list_entries,
			ring_id);
	}
}

void sync_save_transitional (
        const unsigned int *member_list,
        size_t member_list_entries,
        const struct memb_ring_id *ring_id)
{
	ENTER();
	memcpy (my_trans_list, member_list, member_list_entries *
		sizeof (unsigned int));
	my_trans_list_entries = member_list_entries;
}

void sync_abort (void)
{
	ENTER();
	if (my_state == SYNC_PROCESS) {
		schedwrk_destroy (my_schedwrk_handle);
		if (my_sync_callbacks_retrieve(my_service_list[my_processing_idx].service_id, NULL) != -1) {
			my_service_list[my_processing_idx].sync_abort ();
		}
	}

	/* this will cause any "old" barrier messages from causing
	 * problems.
	 */
	memset (&my_ring_id, 0,	sizeof (struct memb_ring_id));
}

void sync_memb_list_determine (const struct memb_ring_id *ring_id)
{
	ENTER();
	memcpy (&my_memb_determine_ring_id, ring_id,
		sizeof (struct memb_ring_id));

	memb_determine_message_transmit ();
}

void sync_memb_list_abort (void)
{
	ENTER();
	my_memb_determine_list_entries = 0;
	memset (&my_memb_determine_ring_id, 0, sizeof (struct memb_ring_id));
}
