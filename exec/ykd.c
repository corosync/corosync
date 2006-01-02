/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
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
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>

#include "handlers.h"
#include "print.h"
#include "swab.h"

#define LOG_SERVICE LOG_SERVICE_YKD

enum ykd_header_values {
	YKD_HEADER_SENDSTATE = 0,
	YKD_HEADER_ATTEMPT = 1
};

enum ykd_mode {
	YKD_MODE_SENDSTATE = 0,
	YKD_MODE_ATTEMPT = 1
};

struct ykd_header {
	int id;
};

struct ykd_session {
	struct totem_ip_address member_list[PROCESSOR_COUNT_MAX];
	int member_list_entries;
	int session_id;
};

struct ykd_state {
	struct ykd_session last_primary;

	struct ykd_session last_formed[PROCESSOR_COUNT_MAX];

	int last_formed_entries;

	struct ykd_session ambiguous_sessions[PROCESSOR_COUNT_MAX];

	int ambiguous_sessions_entries;

	int session_id;
};

struct state_received {
	struct totem_ip_address addr;
	int received;
	struct ykd_state ykd_state;
};

struct ykd_state ykd_state;

static totempg_groups_handle ykd_group_handle;

static struct state_received state_received_confchg[PROCESSOR_COUNT_MAX];

static int state_received_confchg_entries;

static struct state_received state_received_process[PROCESSOR_COUNT_MAX];

static int state_received_process_entries;

static enum ykd_mode ykd_mode;

static struct totem_ip_address view_list[PROCESSOR_COUNT_MAX];

static int view_list_entries;

static int session_id_max;

static struct ykd_session *last_primary_max;

static struct ykd_session ambiguous_sessions_max[PROCESSOR_COUNT_MAX];

static int ambiguous_sessions_max_entries;

static int primary_designated = 0;

static void (*ykd_primary_callback_fn) (
	struct totem_ip_address *view_list,
	int view_list_entries,
	int primary_designated) = NULL;

void ykd_state_init (void)
{
	ykd_state.session_id = 0;
	ykd_state.last_formed_entries = 0;
	ykd_state.ambiguous_sessions_entries = 0;
	ykd_state.last_primary.session_id = 0;
	ykd_state.last_primary.member_list_entries = 0;
}

void ykd_state_send (void)
{
	struct iovec iovec[2];
	struct ykd_header header;

	header.id = YKD_HEADER_SENDSTATE;
	
	iovec[0].iov_base = &header;
	iovec[0].iov_len = sizeof (struct ykd_header);
	iovec[1].iov_base = &ykd_state;
	iovec[1].iov_len = sizeof (struct ykd_state);

	totempg_groups_mcast_joined (ykd_group_handle, iovec, 2, TOTEMPG_AGREED);
}

void ykd_attempt_send (void)
{
	struct iovec iovec;
	struct ykd_header header;

	header.id = YKD_HEADER_SENDSTATE;
	
	iovec.iov_base = &header;
	iovec.iov_len = sizeof (struct ykd_header);

	totempg_groups_mcast_joined (ykd_group_handle, &iovec, 1, TOTEMPG_AGREED);
}

void compute (void)
{
	int i;
	int j;

	session_id_max = 0;
	last_primary_max = &state_received_process[0].ykd_state.last_primary;
	ambiguous_sessions_max_entries = 0;

	for (i = 0; i < state_received_process_entries; i++) {
		/*
		 * Calculate maximum session id
		 */
		if (state_received_process[i].ykd_state.session_id > session_id_max) {
			session_id_max = state_received_process[i].ykd_state.session_id;
		}

		/*
		 * Calculate maximum primary id
		 */
		if (state_received_process[i].ykd_state.last_primary.session_id > last_primary_max->session_id) {
			last_primary_max = &state_received_process[i].ykd_state.last_primary;
		}

		/*
		 * generate the maximum ambiguous sessions list
		 */
		for (j = 0; j < state_received_process[i].ykd_state.ambiguous_sessions_entries; j++) {
			if (state_received_process[i].ykd_state.ambiguous_sessions[j].session_id > last_primary_max->session_id) {
				memcpy (&ambiguous_sessions_max[ambiguous_sessions_max_entries],
					&state_received_process[i].ykd_state.ambiguous_sessions[j],
					sizeof (struct ykd_session));
				ambiguous_sessions_max_entries += 1;
			}
		}
	}
}

int subquorum (
	struct totem_ip_address *member_list,
	int member_list_entries,
	struct ykd_session *session)
{
	int intersections = 0;
	int i;
	int j;

	for (i = 0; i < member_list_entries; i++) {
		for (j = 0; j < session->member_list_entries; j++) {
			if (totemip_equal (&member_list[i], &session->member_list[j])) {
				intersections += 1;
			}
		}
	}

	/*
	 * even split
	 */
	if (intersections == (session->member_list_entries - intersections)) {
		return (1);
	} else

	/*
	 * majority split
	 */
	if (intersections > (session->member_list_entries - intersections)) {
		return (1);
	}
	return (0);
}

int decide (void)
{
	int i;

	/*
	 * Determine if there is a subquorum
	 */
	if (subquorum (view_list, view_list_entries, last_primary_max) == 0) {
		return (0);
	}

	for (i = 0; i < ambiguous_sessions_max_entries; i++) {
		if (subquorum (view_list, view_list_entries, &ambiguous_sessions_max[i]) == 0) {
			return (0);
		}
		
	}
	return (1);
}

static void ykd_deliver_fn (
	struct totem_ip_address *source_addr,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required)
{
	int all_received = 1;
	int state_position = 0;
	int i;
	char *msg_state = iovec->iov_base + sizeof (struct ykd_header);
	
#ifdef COMPILE_OUT
	memcpy (&ykd_ring_id, &req_exec_sync_barrier_start->ring_id,
	sizeof (struct memb_ring_id));

/*
	* Is this barrier from this configuration, if not, ignore it
	*/
	if (memcmp (&req_exec_sync_barrier_start->ring_id, ykd_ring_id,
		sizeof (struct memb_ring_id)) != 0) {
		*/

	return (0);
#endif
	/*
	* Set completion for source_addr's address
	*/
	for (state_position = 0; state_position < state_received_confchg_entries; state_position++) {
		if (totemip_equal(source_addr, &state_received_process[state_position].addr)) {
			/*
			 * State position contains the address of the state to modify
			 * This may be used later by the other algorithms
			 */
			state_received_process[state_position].received = 1;
			break;
		}
	}

	/*
	 * Test if all nodes have submitted their state data
	 */
	for (i = 0; i < state_received_confchg_entries; i++) {
		if (state_received_process[i].received == 0) {
			all_received = 0;
		}
	}

	switch (ykd_mode) {
		case YKD_MODE_SENDSTATE:
			/*
			 * Copy state information for the sending processor
			 */
			memcpy (&state_received_process[state_position].ykd_state,
				msg_state, sizeof (struct ykd_state));

			/*
			 * Try to form a component
			 */
			if (all_received) {
				for (i = 0; i < state_received_confchg_entries; i++) {
					state_received_process[i].received = 0;
				}
				ykd_mode = YKD_MODE_ATTEMPT;

// TODO resolve optimizes for failure conditions during ykd calculation
// resolve();
				compute();

				if (decide ()) {
					ykd_state.session_id = session_id_max + 1;
					memcpy (ykd_state.ambiguous_sessions[ykd_state.ambiguous_sessions_entries].member_list,
						view_list, sizeof (struct totem_ip_address) * view_list_entries);
						ykd_state.ambiguous_sessions[ykd_state.ambiguous_sessions_entries].member_list_entries = view_list_entries;
						ykd_state.ambiguous_sessions_entries += 1;
					ykd_attempt_send();
				}
			}
			break;

		case YKD_MODE_ATTEMPT:
			if (all_received) {
				log_printf (LOG_LEVEL_NOTICE,
					"This processor is within the primary component.\n");
				primary_designated = 1;

				ykd_primary_callback_fn (
					view_list,
					view_list_entries,
					primary_designated);

				memcpy (ykd_state.last_primary.member_list, view_list, sizeof (view_list));
				ykd_state.last_primary.member_list_entries = view_list_entries;
				ykd_state.last_primary.session_id = ykd_state.session_id;
				ykd_state.ambiguous_sessions_entries = 0;
			}
			break;
	}
}

int first_run = 1;
static void ykd_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;

	if (configuration_type != TOTEM_CONFIGURATION_REGULAR) {
		return;
	}
	if (first_run) {
		totemip_copy (&ykd_state.last_primary.member_list[0], this_ip);
		ykd_state.last_primary.member_list_entries = 1;
		ykd_state.last_primary.session_id = 0;
		first_run = 0;
	}
	memcpy (view_list, member_list,
		member_list_entries * sizeof (struct totem_ip_address));
	view_list_entries = member_list_entries;

	ykd_mode = YKD_MODE_SENDSTATE;

	primary_designated = 0;

	ykd_primary_callback_fn (
		view_list,
		view_list_entries,
		primary_designated);

	memset (&state_received_confchg, 0, sizeof (state_received_confchg));
	for (i = 0; i < member_list_entries; i++) {
		totemip_copy(&state_received_confchg[i].addr, &member_list[i]);
		state_received_confchg[i].received = 0;
	}
	memcpy (state_received_process, state_received_confchg,
		sizeof (state_received_confchg));

	state_received_confchg_entries = member_list_entries;
	state_received_process_entries = member_list_entries;

	ykd_state_send ();
}

struct totempg_group ykd_group = {
	.group		= "ykd",
	.group_len	= 3
};

int ykd_init (
	void (*primary_callback_fn) (
		struct totem_ip_address *view_list,
		int view_list_entries,
		int primary_designated))
{
	ykd_primary_callback_fn = primary_callback_fn;

	totempg_groups_initialize (
		&ykd_group_handle,
		ykd_deliver_fn,
		ykd_confchg_fn);

	totempg_groups_join (
		ykd_group_handle,
		&ykd_group,
		1);

	ykd_state_init ();

	return (0);
}

/*
 * Returns 1 if this processor is in the primary 
 */
int ykd_primary (void) {
	return (primary_designated);
}

