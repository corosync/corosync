/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>

#include "dynar.h"
#include "dynar-str.h"
#include "dynar-simple-lex.h"
#include "process-list.h"

static void		process_list_free_argv(size_t no_params, char **argv);

static void		process_list_entry_free(struct process_list_entry *entry);

static char 		**process_list_parse_command(const char *command, size_t *no_params);

static int		process_list_entry_exec(const struct process_list *plist,
    struct process_list_entry *entry);

void
process_list_init(struct process_list *plist, size_t max_list_entries, int use_execvp,
    process_list_notify_fn_t notify_fn, void *notify_fn_user_data)
{

	memset(plist, 0, sizeof(*plist));

	plist->max_list_entries = max_list_entries;
	plist->allocated_list_entries = 0;
	plist->use_execvp = use_execvp;
	plist->notify_fn = notify_fn;
	plist->notify_fn_user_data = notify_fn_user_data;

	TAILQ_INIT(&plist->active_list);
	TAILQ_INIT(&plist->to_kill_list);
}

static void
process_list_free_argv(size_t no_params, char **argv)
{
	size_t zi;

	for (zi = 0; zi < no_params; zi++) {
		free(argv[zi]);
	}
	free(argv);
}

static void
process_list_entry_free(struct process_list_entry *entry)
{

	process_list_free_argv(entry->exec_argc, entry->exec_argv);
	free(entry->name);
	free(entry);
}

static char **
process_list_parse_command(const char *command, size_t *no_params)
{
	struct dynar command_dstr;
	struct dynar_simple_lex lex;
	struct dynar *token;
	int finished;
	char **res_argv;
	size_t zi;

	res_argv = NULL;

	dynar_init(&command_dstr, strlen(command) + 1);
	if (dynar_str_cpy(&command_dstr, command) != 0) {
		return (NULL);
	}

	dynar_simple_lex_init(&lex, &command_dstr, DYNAR_SIMPLE_LEX_TYPE_QUOTE);
	*no_params = 0;
	finished = 0;

	while (!finished) {
		token = dynar_simple_lex_token_next(&lex);
		if (token == NULL) {
			goto exit_res;
		}

		if (strcmp(dynar_data(token), "") == 0) {
			finished = 1;
		} else {
			(*no_params)++;
		}
	}

	if (*no_params < 1) {
		goto exit_res;
	}

	dynar_simple_lex_destroy(&lex);

	res_argv = malloc(sizeof(char *) * (*no_params + 1));
	if (res_argv == NULL) {
		goto exit_res;
	}
	memset(res_argv, 0, sizeof(char *) * (*no_params + 1));

	dynar_simple_lex_init(&lex, &command_dstr, DYNAR_SIMPLE_LEX_TYPE_QUOTE);

	finished = 0;
	zi = 0;
	while (!finished) {
		token = dynar_simple_lex_token_next(&lex);
		if (token == NULL) {
			process_list_free_argv(*no_params, res_argv);
			res_argv = NULL;
			goto exit_res;
		}

		if (strcmp(dynar_data(token), "") == 0) {
			finished = 1;
		} else {
			res_argv[zi] = strdup(dynar_data(token));
			if (res_argv[zi] == NULL) {
				process_list_free_argv(*no_params, res_argv);
				res_argv = NULL;
			}
			zi++;
		}
	}

	if (zi != *no_params) {
		/*
		 * If this happens it means something is seriously broken (memory corrupted)
		 */
		process_list_free_argv(*no_params, res_argv);
		res_argv = NULL;
		goto exit_res;
	}

exit_res:
	dynar_simple_lex_destroy(&lex);
	dynar_destroy(&command_dstr);
	return (res_argv);
}

struct process_list_entry *
process_list_add(struct process_list *plist, const char *name, const char *command)
{
	struct process_list_entry *entry;

	if (plist->allocated_list_entries + 1 > plist->max_list_entries) {
		return (NULL);
	}

	/*
	 * Alloc new entry
	 */
	entry = malloc(sizeof(*entry));
	if (entry == NULL) {
		return (NULL);
	}

	memset(entry, 0, sizeof(*entry));
	entry->name = strdup(name);
	if (entry->name == NULL) {
		process_list_entry_free(entry);

		return (NULL);
	}

	entry->state = PROCESS_LIST_ENTRY_STATE_INITIALIZED;
	entry->exec_argv = process_list_parse_command(command, &entry->exec_argc);
	if (entry->exec_argv == NULL) {
		process_list_entry_free(entry);

		return (NULL);
	}

	plist->allocated_list_entries++;
	TAILQ_INSERT_TAIL(&plist->active_list, entry, entries);

	return (entry);
}

void
process_list_free(struct process_list *plist)
{
	struct process_list_entry *entry;
	struct process_list_entry *entry_next;

	entry = TAILQ_FIRST(&plist->active_list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		process_list_entry_free(entry);

		entry = entry_next;
	}

	entry = TAILQ_FIRST(&plist->to_kill_list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		process_list_entry_free(entry);

		entry = entry_next;
	}

	plist->allocated_list_entries = 0;

	TAILQ_INIT(&plist->active_list);
	TAILQ_INIT(&plist->to_kill_list);
}

static void
process_list_entry_exec_helper_set_stdfd(void)
{
	int devnull;

	devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		err(1, "Can't open /dev/null");
	}

	if (dup2(devnull, 0) < 0 || dup2(devnull, 1) < 0 || dup2(devnull, 2) < 0) {
		close(devnull);
		err(1, "Can't dup2 stdin/out/err to /dev/null");
	}

	close(devnull);
}

static int
process_list_entry_exec(const struct process_list *plist, struct process_list_entry *entry)
{
	pid_t pid;

	if (entry->state != PROCESS_LIST_ENTRY_STATE_INITIALIZED) {
		return (-1);
	}

	pid = fork();
	if (pid == -1) {
		return (-1);
	} else if (pid == 0) {
		process_list_entry_exec_helper_set_stdfd();

		if (!plist->use_execvp) {
			execv(entry->exec_argv[0], entry->exec_argv);
		} else {
			execvp(entry->exec_argv[0], entry->exec_argv);
		}

		/*
		 * Exec returned -> exec failed
		 */
		err(1, "Can't execute command %s (%s)", entry->name, entry->exec_argv[0]);
	} else {
		entry->pid = pid;
		entry->state = PROCESS_LIST_ENTRY_STATE_RUNNING;

		if (plist->notify_fn != NULL) {
			plist->notify_fn(PROCESS_LIST_NOTIFY_REASON_EXECUTED, entry,
			    plist->notify_fn_user_data);
		}
	}

	return (0);
}

int
process_list_exec_initialized(struct process_list *plist)
{
	struct process_list_entry *entry;

	TAILQ_FOREACH(entry, &plist->active_list, entries) {
		if (entry->state == PROCESS_LIST_ENTRY_STATE_INITIALIZED) {
			if (process_list_entry_exec(plist, entry) != 0) {
				return (-1);
			}
		}
	}

	return (0);
}

static int
process_list_entry_waitpid(const struct process_list *plist, struct process_list_entry *entry)
{
	pid_t wpid_res;
	int status;

	if (entry->state == PROCESS_LIST_ENTRY_STATE_INITIALIZED ||
	    entry->state == PROCESS_LIST_ENTRY_STATE_FINISHED) {
		return (0);
	}

	wpid_res = waitpid(entry->pid, &status, WNOHANG);
	if (wpid_res == -1) {
		return (-1);
	}

	if (wpid_res == 0) {
		/*
		 * No change
		 */
		return (0);
	}

	entry->exit_status = status;

	if (entry->state == PROCESS_LIST_ENTRY_STATE_RUNNING) {
		if (plist->notify_fn != NULL) {
			plist->notify_fn(PROCESS_LIST_NOTIFY_REASON_FINISHED, entry,
			    plist->notify_fn_user_data);
		}
	}

	entry->state = PROCESS_LIST_ENTRY_STATE_FINISHED;

	return (0);
}

int
process_list_waitpid(struct process_list *plist)
{
	struct process_list_entry *entry;
	struct process_list_entry *entry_next;

	TAILQ_FOREACH(entry, &plist->active_list, entries) {
		if (process_list_entry_waitpid(plist, entry) != 0) {
			return (-1);
		}
	}

	entry = TAILQ_FIRST(&plist->to_kill_list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		if (process_list_entry_waitpid(plist, entry) != 0) {
			return (-1);
		}

		if (entry->state == PROCESS_LIST_ENTRY_STATE_FINISHED) {
			/*
			 * Process finished -> remove it from list
			 */
			TAILQ_REMOVE(&plist->to_kill_list, entry, entries);
			process_list_entry_free(entry);
			plist->allocated_list_entries--;
		}

		entry = entry_next;
	}

	return (0);
}

size_t
process_list_get_no_running(struct process_list *plist)
{
	struct process_list_entry *entry;
	size_t res;

	res = 0;

	TAILQ_FOREACH(entry, &plist->active_list, entries) {
		if (entry->state == PROCESS_LIST_ENTRY_STATE_RUNNING) {
			res++;
		}
	}

	return (res);
}

/*
 * -1 = Not all processes finished
 *  0 = All processes finished sucesfully
 *  1 - All processes finished but some of them not sucesfully
 */
int
process_list_get_summary_result(struct process_list *plist)
{
	struct process_list_entry *entry;
	int res;

	res = 0;

	TAILQ_FOREACH(entry, &plist->active_list, entries) {
		if (entry->state != PROCESS_LIST_ENTRY_STATE_FINISHED) {
			return (-1);
		}

		if (!WIFEXITED(entry->exit_status) || WEXITSTATUS(entry->exit_status) != 0) {
			res = 1;
		}
	}

	return (res);
}

/*
 *  0 = All processes finished sucesfully
 *  1 = Some process finished and failed
 * -1 = Not all processed finished and none of finished failed
 */
int
process_list_get_summary_result_short(struct process_list *plist)
{
	struct process_list_entry *entry;
	int res;

	res = 0;

	TAILQ_FOREACH(entry, &plist->active_list, entries) {
		if (entry->state == PROCESS_LIST_ENTRY_STATE_FINISHED) {
			if (!WIFEXITED(entry->exit_status) || WEXITSTATUS(entry->exit_status) != 0) {
				return (1);
			}
		} else {
			res = -1;
		}
	}

	return (res);
}

static void
process_list_move_entry_to_kill_list(struct process_list *plist, struct process_list_entry *entry)
{

	TAILQ_REMOVE(&plist->active_list, entry, entries);
	TAILQ_INSERT_TAIL(&plist->to_kill_list, entry, entries);
}

void
process_list_move_active_entries_to_kill_list(struct process_list *plist)
{
	struct process_list_entry *entry;
	struct process_list_entry *entry_next;

	entry = TAILQ_FIRST(&plist->active_list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		if (entry->state == PROCESS_LIST_ENTRY_STATE_INITIALIZED ||
		    entry->state == PROCESS_LIST_ENTRY_STATE_FINISHED) {
			TAILQ_REMOVE(&plist->active_list, entry, entries);
			process_list_entry_free(entry);
			plist->allocated_list_entries--;
		} else {
			process_list_move_entry_to_kill_list(plist, entry);
		}

		entry = entry_next;
	}
}

static int
process_list_process_kill_list_entry(struct process_list *plist, struct process_list_entry *entry)
{
	int sig_to_send;
	enum process_list_entry_state new_state;
	int res;

	sig_to_send = 0;
	new_state = PROCESS_LIST_ENTRY_STATE_INITIALIZED;

	switch (entry->state) {
	case PROCESS_LIST_ENTRY_STATE_INITIALIZED:
		/*
		 * This shouldn't happen. If it does, process_list_move_active_entries_to_kill_list
		 * doesn't work as expected or there is some kind of memory corruption.
		 */
		assert(entry->state != PROCESS_LIST_ENTRY_STATE_INITIALIZED);
		break;
	case PROCESS_LIST_ENTRY_STATE_FINISHED:
		/*
		 * This shouldn't happen. If it does, process_list_waitpid
		 * doesn't work as expected or there is some kind of memory corruption.
		 */
		assert(entry->state != PROCESS_LIST_ENTRY_STATE_FINISHED);
		break;
	case PROCESS_LIST_ENTRY_STATE_RUNNING:
		sig_to_send = SIGTERM;
		new_state = PROCESS_LIST_ENTRY_STATE_SIGTERM_SENT;
		break;
	case PROCESS_LIST_ENTRY_STATE_SIGTERM_SENT:
		sig_to_send = SIGKILL;
		new_state = PROCESS_LIST_ENTRY_STATE_SIGKILL_SENT;
		break;
	case PROCESS_LIST_ENTRY_STATE_SIGKILL_SENT:
		sig_to_send = SIGKILL;
		new_state = PROCESS_LIST_ENTRY_STATE_SIGKILL_SENT;
		break;
	}

	res = 0;

	if (kill(entry->pid, sig_to_send) == -1) {
		if (errno == EPERM || errno == EINVAL) {
			res = -1;
		}
	}

	entry->state = new_state;

	return (res);
}

int
process_list_process_kill_list(struct process_list *plist)
{
	struct process_list_entry *entry;

	if (process_list_waitpid(plist) != 0) {
		return (-1);
	}

	TAILQ_FOREACH(entry, &plist->to_kill_list, entries) {
		if (process_list_process_kill_list_entry(plist, entry) != 0) {
			return (-1);
		}
	}

	return (0);
}

size_t
process_list_get_kill_list_items(struct process_list *plist)
{
	struct process_list_entry *entry;
	size_t res;

	res = 0;

	TAILQ_FOREACH(entry, &plist->to_kill_list, entries) {
		res++;
	}

	return (res);
}

int
process_list_killall(struct process_list *plist, uint32_t timeout)
{
	uint32_t action_timeout;
	int i;

	process_list_move_active_entries_to_kill_list(plist);

	action_timeout = timeout / 10;
	if (action_timeout < 1) {
		action_timeout = 1;
	}

	for (i = 0; i < 10; i++) {
		/*
		 * Make sure all process got signal (quick phase)
		 */
		if (process_list_process_kill_list(plist) != 0) {
			return (-1);
		}
	}

	for (i = 0; i < 10 && process_list_get_kill_list_items(plist) > 0; i++) {
		if (process_list_process_kill_list(plist) != 0) {
			return (-1);
		}

		poll(NULL, 0, action_timeout);
	}

	if (process_list_get_kill_list_items(plist) > 0) {
		return (-1);
	}

	return (0);
}
