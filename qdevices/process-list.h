/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#ifndef _PROCESS_LIST_H_
#define _PROCESS_LIST_H_

#include <sys/queue.h>

#include "dynar.h"

#ifdef __cplusplus
extern "C" {
#endif

enum process_list_entry_state {
	PROCESS_LIST_ENTRY_STATE_INITIALIZED,
	PROCESS_LIST_ENTRY_STATE_RUNNING,
	PROCESS_LIST_ENTRY_STATE_FINISHED,
	PROCESS_LIST_ENTRY_STATE_SIGTERM_SENT,
	PROCESS_LIST_ENTRY_STATE_SIGKILL_SENT,
};

enum process_list_notify_reason {
	PROCESS_LIST_NOTIFY_REASON_EXECUTED,
	PROCESS_LIST_NOTIFY_REASON_FINISHED,
};

struct process_list_entry {
	char *name;
	enum process_list_entry_state state;
	char **exec_argv;
	size_t exec_argc;
	pid_t pid;
	int exit_status;

	TAILQ_ENTRY(process_list_entry) entries;
};

typedef void (*process_list_notify_fn_t) (enum process_list_notify_reason reason,
    const struct process_list_entry *entry, void *user_data);

struct process_list {
	int use_execvp;
	size_t max_list_entries;
	size_t allocated_list_entries;
	process_list_notify_fn_t notify_fn;
	void *notify_fn_user_data;

	TAILQ_HEAD(, process_list_entry) active_list;
	TAILQ_HEAD(, process_list_entry) to_kill_list;
};


extern void				 process_list_init(struct process_list *plist,
    size_t max_list_entries, int use_execvp, process_list_notify_fn_t notify_fn,
    void *notify_fn_user_data);

extern struct process_list_entry	*process_list_add(struct process_list *plist,
    const char *name, const char *command);

extern void				 process_list_free(struct process_list *plist);

extern int				 process_list_exec_initialized(struct process_list *plist);

extern int				 process_list_waitpid(struct process_list *plist);

extern size_t				 process_list_get_no_running(struct process_list *plist);

extern int				 process_list_get_summary_result(struct process_list *plist);

extern int				 process_list_get_summary_result_short(
    struct process_list *plist);

extern void				 process_list_move_active_entries_to_kill_list(
    struct process_list *plist);

extern int				 process_list_process_kill_list(struct process_list *plist);

extern size_t				 process_list_get_kill_list_items(struct process_list *plist);

extern int				 process_list_killall(struct process_list *plist,
    uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* _PROCESS_LIST_H_ */
