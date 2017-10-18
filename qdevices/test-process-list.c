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

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <poll.h>
#include <signal.h>

#include "process-list.h"

static int no_executed;
static int no_finished;

static void
signal_handlers_register(void)
{
	struct sigaction act;

	act.sa_handler = SIG_DFL;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGCHLD, &act, NULL);

	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGPIPE, &act, NULL);
}

static void
plist_notify(enum process_list_notify_reason reason, const struct process_list_entry *entry,
    void *user_data)
{

	assert(user_data == (void *)0x42);

	switch (reason) {
	case PROCESS_LIST_NOTIFY_REASON_EXECUTED:
		no_executed++;
		break;
	case PROCESS_LIST_NOTIFY_REASON_FINISHED:
		no_finished++;
		break;
	}
}

int
main(void)
{
	struct process_list plist;
	struct process_list_entry *plist_entry;
	int i;
	int timeout;
	int no_repeats;

	signal_handlers_register();

	process_list_init(&plist, 10, 1, plist_notify, (void *)0x42);
	plist_entry = process_list_add(&plist, "test name", "command");
	assert(plist_entry != NULL);
	assert(strcmp(plist_entry->name, "test name") == 0);
	assert(plist_entry->state == PROCESS_LIST_ENTRY_STATE_INITIALIZED);
	assert(plist_entry->exec_argc == 1);
	assert(plist_entry->exec_argv[0] != NULL && strcmp(plist_entry->exec_argv[0], "command") == 0);
	assert(plist_entry->exec_argv[1] == NULL);

	plist_entry = process_list_add(&plist, "test name", "/bin/ping -c \"host wit\\\"h  space\"   notaspace");
	assert(plist_entry != NULL);
	assert(strcmp(plist_entry->name, "test name") == 0);
	assert(plist_entry->state == PROCESS_LIST_ENTRY_STATE_INITIALIZED);
	assert(plist_entry->exec_argc == 4);
	assert(plist_entry->exec_argv[0] != NULL && strcmp(plist_entry->exec_argv[0], "/bin/ping") == 0);
	assert(plist_entry->exec_argv[1] != NULL && strcmp(plist_entry->exec_argv[1], "-c") == 0);
	assert(plist_entry->exec_argv[2] != NULL && strcmp(plist_entry->exec_argv[2], "host wit\"h  space") == 0);
	assert(plist_entry->exec_argv[3] != NULL && strcmp(plist_entry->exec_argv[3], "notaspace") == 0);
	assert(plist_entry->exec_argv[4] == NULL);

	process_list_free(&plist);

	/*
	 * Test no process
	 */
	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 0);
	assert(process_list_get_no_running(&plist) == 0);

	/*
	 * Wait to exit
	 */
	no_repeats = 10;
	timeout = 1000 / no_repeats;
	for (i = 0; i < no_repeats; i++) {
		assert(process_list_waitpid(&plist) == 0);
		if (process_list_get_no_running(&plist) > 0) {
			poll(NULL, 0, timeout);
		}
	}

	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_no_running(&plist) == 0);
	assert(no_finished == 0);
	assert(process_list_get_summary_result(&plist) == 0);
	assert(process_list_get_summary_result_short(&plist) == 0);

	process_list_free(&plist);

	/*
	 * Test two processes. /bin/true and /bin/false. Accumulated result should be fail
	 */
	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "false", "/bin/false");
	assert(plist_entry != NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 2);
	assert(process_list_get_no_running(&plist) == 2);

	/*
	 * Wait to exit
	 */
	no_repeats = 10;
	timeout = 1000 / no_repeats;
	for (i = 0; i < no_repeats; i++) {
		assert(process_list_waitpid(&plist) == 0);
		if (process_list_get_no_running(&plist) > 0) {
			poll(NULL, 0, timeout);
		}
	}

	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_no_running(&plist) == 0);
	assert(no_finished == 2);
	assert(process_list_get_summary_result(&plist) == 1);
	assert(process_list_get_summary_result_short(&plist) == 1);

	process_list_free(&plist);

	/*
	 * Test two processes. /bin/true and one non-existing. Accumulated result should be fail
	 */
	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "false", "/nonexistingdir/nonexistingfile");
	assert(plist_entry != NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 2);
	assert(process_list_get_no_running(&plist) == 2);

	/*
	 * Wait to exit
	 */
	no_repeats = 10;
	timeout = 1000 / no_repeats;
	for (i = 0; i < no_repeats; i++) {
		assert(process_list_waitpid(&plist) == 0);
		if (process_list_get_no_running(&plist) > 0) {
			poll(NULL, 0, timeout);
		}
	}

	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_no_running(&plist) == 0);
	assert(no_finished == 2);
	assert(process_list_get_summary_result(&plist) == 1);
	assert(process_list_get_summary_result_short(&plist) == 1);

	process_list_free(&plist);

	/*
	 * Test three processes /bin/true. Accumulated result should be success.
	 */
	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true2", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true3", "/bin/true");
	assert(plist_entry != NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 3);
	assert(process_list_get_no_running(&plist) == 3);

	/*
	 * Wait to exit
	 */
	no_repeats = 10;
	timeout = 1000 / no_repeats;
	for (i = 0; i < no_repeats; i++) {
		assert(process_list_waitpid(&plist) == 0);
		if (process_list_get_no_running(&plist) > 0) {
			poll(NULL, 0, timeout);
		}
	}

	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_no_running(&plist) == 0);
	assert(no_finished == 3);
	assert(process_list_get_summary_result(&plist) == 0);
	assert(process_list_get_summary_result_short(&plist) == 0);

	process_list_free(&plist);

	/*
	 * Test two processes. /bin/true and cat. Waiting for maximum of 2 sec
	 */
	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "cat", "/bin/cat /dev/zero");
	assert(plist_entry != NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 2);
	assert(process_list_get_no_running(&plist) == 2);

	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);

	assert(process_list_get_no_running(&plist) == 1);
	assert(no_finished == 1);
	assert(process_list_get_summary_result(&plist) == -1);
	assert(process_list_get_summary_result_short(&plist) == -1);

	process_list_move_active_entries_to_kill_list(&plist);
	assert(process_list_process_kill_list(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_process_kill_list(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);

	assert(process_list_get_kill_list_items(&plist) == 0);

	assert(process_list_process_kill_list(&plist) == 0);

	process_list_free(&plist);

	/*
	 * Test two bash proceses. One ignores INT and second ignores INT and TERM. Waiting for maximum of 2 sec
	 */
	plist_entry = process_list_add(&plist, "ignoresig1", "bash -c \"trap 'echo trap' SIGINT;while true;do sleep 1;done\"");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "ignoresig2", "bash -c \"trap 'echo trap' SIGINT SIGTERM;while true;do sleep 1;done\"");
	assert(plist_entry != NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 2);
	assert(process_list_get_no_running(&plist) == 2);

	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);

	assert(process_list_get_no_running(&plist) == 2);
	assert(no_finished == 0);
	assert(process_list_get_summary_result(&plist) == -1);
	assert(process_list_get_summary_result_short(&plist) == -1);

	process_list_move_active_entries_to_kill_list(&plist);
	assert(process_list_process_kill_list(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_kill_list_items(&plist) == 1);

	assert(process_list_process_kill_list(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_kill_list_items(&plist) == 0);

	process_list_free(&plist);

	/*
	 * Test 3 processes. Test if entries are properly deallocated
	 */
	process_list_init(&plist, 3, 1, plist_notify, (void *)0x42);
	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true2", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true3", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true4", "/bin/true");
	assert(plist_entry == NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 3);
	assert(process_list_get_no_running(&plist) == 3);

	/*
	 * Wait to exit
	 */
	no_repeats = 10;
	timeout = 1000 / no_repeats;
	for (i = 0; i < no_repeats; i++) {
		assert(process_list_waitpid(&plist) == 0);
		if (process_list_get_no_running(&plist) > 0) {
			poll(NULL, 0, timeout);
		}
	}

	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_no_running(&plist) == 0);
	assert(no_finished == 3);
	assert(process_list_get_summary_result(&plist) == 0);
	assert(process_list_get_summary_result_short(&plist) == 0);

	process_list_move_active_entries_to_kill_list(&plist);

	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "ignoresig1", "bash -c \"trap 'echo trap' SIGINT;while true;do sleep 1;done\"");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "ignoresig2", "bash -c \"trap 'echo trap' SIGINT SIGTERM;while true;do sleep 1;done\"");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true4", "/bin/true");
	assert(plist_entry == NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 3);
	assert(process_list_get_no_running(&plist) == 3);

	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);

	assert(process_list_get_no_running(&plist) == 2);
	assert(no_finished == 1);
	assert(process_list_get_summary_result(&plist) == -1);
	assert(process_list_get_summary_result_short(&plist) == -1);

	plist_entry = process_list_add(&plist, "true4", "/bin/true");
	assert(plist_entry == NULL);

	process_list_move_active_entries_to_kill_list(&plist);

	plist_entry = process_list_add(&plist, "true4", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true5", "/bin/true");
	assert(plist_entry == NULL);

	assert(process_list_process_kill_list(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_kill_list_items(&plist) == 1);

	assert(process_list_process_kill_list(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_kill_list_items(&plist) == 0);

	process_list_move_active_entries_to_kill_list(&plist);
	assert(process_list_get_summary_result(&plist) == 0);
	assert(process_list_get_summary_result_short(&plist) == 0);

	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true2", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true3", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true4", "/bin/true");
	assert(plist_entry == NULL);

	process_list_free(&plist);

	/*
	 * Test 3 processes and difference between summary and short-circuit summary
	 */
	process_list_init(&plist, 3, 1, plist_notify, (void *)0x42);
	plist_entry = process_list_add(&plist, "true", "/bin/true");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "false", "/bin/false");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "loop", "bash -c \"while true;do sleep 1;done\"");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "true4", "/bin/true");
	assert(plist_entry == NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 3);
	assert(process_list_get_no_running(&plist) == 3);

	/*
	 * Wait to exit
	 */
	no_repeats = 10;
	timeout = 1000 / no_repeats;
	for (i = 0; i < no_repeats; i++) {
		assert(process_list_waitpid(&plist) == 0);
		if (process_list_get_no_running(&plist) > 0) {
			poll(NULL, 0, timeout);
		}
	}

	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_no_running(&plist) == 1);
	assert(no_finished == 2);
	assert(process_list_get_summary_result(&plist) == -1);
	assert(process_list_get_summary_result_short(&plist) == 1);

	process_list_move_active_entries_to_kill_list(&plist);
	assert(process_list_process_kill_list(&plist) == 0);
	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);
	assert(process_list_get_kill_list_items(&plist) == 0);

	process_list_free(&plist);

	/*
	 * Test process_list_killall by running two bash proceses.
	 * One ignores INT and second ignores INT and TERM. Waiting for maximum of 2 sec
	 */
	plist_entry = process_list_add(&plist, "ignoresig1", "bash -c \"trap 'echo trap' SIGINT;while true;do sleep 1;done\"");
	assert(plist_entry != NULL);

	plist_entry = process_list_add(&plist, "ignoresig2", "bash -c \"trap 'echo trap' SIGINT SIGTERM;while true;do sleep 1;done\"");
	assert(plist_entry != NULL);

	no_executed = 0;
	no_finished = 0;
	assert(process_list_exec_initialized(&plist) == 0);
	assert(no_executed == 2);
	assert(process_list_get_no_running(&plist) == 2);

	poll(NULL, 0, 500);
	assert(process_list_waitpid(&plist) == 0);

	assert(process_list_get_no_running(&plist) == 2);
	assert(no_finished == 0);
	assert(process_list_get_summary_result(&plist) == -1);
	assert(process_list_get_summary_result_short(&plist) == -1);

	assert(process_list_killall(&plist, 2000) == 0);
	assert(process_list_get_kill_list_items(&plist) == 0);

	process_list_free(&plist);

	/*
	 * Empty killall exits with sucess result
	 */
	assert(process_list_killall(&plist, 2000) == 0);

	process_list_free(&plist);

	return (0);
}
