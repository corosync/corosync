/*
 * Copyright (c) 2010-2012 Red Hat
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
#ifndef FSM_H_DEFINED
#define FSM_H_DEFINED

#include <sys/time.h>
#include <corosync/corotypes.h>
#include "util.h"

struct cs_fsm;
struct cs_fsm_entry;
typedef void (*cs_fsm_event_action_fn)(struct cs_fsm* fsm, int32_t event, void * data);
typedef const char * (*cs_fsm_state_to_str_fn)(struct cs_fsm* fsm, int32_t state);
typedef const char * (*cs_fsm_event_to_str_fn)(struct cs_fsm* fsm, int32_t event);

typedef void (*cs_fsm_cb)(struct cs_fsm *fsm, int cb_event, int32_t curr_state,
    int32_t next_state, int32_t fsm_event, void *data);

#define CS_FSM_NEXT_STATE_SIZE 32

#define CS_FSM_STATE_NONE		-1

#define CS_FSM_CB_EVENT_PROCESS_NF	0
#define CS_FSM_CB_EVENT_STATE_SET	1
#define CS_FSM_CB_EVENT_STATE_SET_NF	2

struct cs_fsm_entry {
	int32_t curr_state;
	int32_t event;
	cs_fsm_event_action_fn handler_fn;
	int32_t next_states[CS_FSM_NEXT_STATE_SIZE];
};

struct cs_fsm {
	const char *name;
	int32_t curr_state;
	int32_t curr_entry;
	size_t entries;
	struct cs_fsm_entry *table;
	cs_fsm_state_to_str_fn state_to_str;
	cs_fsm_event_to_str_fn event_to_str;
};

/*
 * the table entry is defined by the state + event (curr_entry).
 * so cs_fsm_process() sets the entry and cs_fsm_state_set()
 * sets the new state.
 */
static inline void cs_fsm_process (struct cs_fsm *fsm, int32_t new_event, void * data, cs_fsm_cb cb)
{
	int32_t i;

	for (i = 0; i < fsm->entries; i++) {
		if (fsm->table[i].event == new_event &&
		    fsm->table[i].curr_state == fsm->curr_state) {

			assert (fsm->table[i].handler_fn != NULL);
			/* set current entry */
			fsm->curr_entry = i;
			fsm->table[i].handler_fn (fsm, new_event, data);
			return;
		}
	}

	if (cb != NULL) {
		cb(fsm, CS_FSM_CB_EVENT_PROCESS_NF, fsm->curr_state, CS_FSM_STATE_NONE, new_event, data);
	}
}

static inline void cs_fsm_state_set (struct cs_fsm* fsm, int32_t next_state, void* data, cs_fsm_cb cb)
{
	int i;
	struct cs_fsm_entry *entry = &fsm->table[fsm->curr_entry];

	if (fsm->curr_state == next_state) {
		return;
	}
	/*
	 * confirm that "next_state" is in the current entry's next list
	 */
	for (i = 0; i < CS_FSM_NEXT_STATE_SIZE; i++) {
		if (entry->next_states[i] < 0) {
			break;
		}
		if (entry->next_states[i] == next_state) {
			if (cb != NULL) {
				cb(fsm, CS_FSM_CB_EVENT_STATE_SET, fsm->curr_state, next_state, entry->event, data);
			}
			fsm->curr_state = next_state;
			return;
		}
	}
	if (cb != NULL) {
		cb(fsm, CS_FSM_CB_EVENT_STATE_SET_NF, fsm->curr_state, next_state, entry->event, data);
	}
}

#endif /* FSM_H_DEFINED */


