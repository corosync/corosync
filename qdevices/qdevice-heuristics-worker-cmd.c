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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dynar.h"
#include "dynar-str.h"
#include "qdevice-heuristics-io.h"
#include "qdevice-heuristics-worker.h"
#include "qdevice-heuristics-worker-cmd.h"
#include "qdevice-heuristics-cmd-str.h"
#include "qdevice-heuristics-worker-log.h"

static int
qdevice_heuristics_worker_cmd_process_exec_list_add(struct qdevice_heuristics_worker_instance *instance,
    struct dynar *data)
{
	size_t zi;
	char *exec_name;
	char *exec_command;
	char *str;

	str = dynar_data(data);

	/*
	 * Skip to first space
	 */
	for (zi = 0; str[zi] != ' ' && str[zi] != '\0'; zi++) ;

	if (str[zi] == '\0') {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_exec_list_add: Can't find first space");
		return (-1);
	}

	/*
	 * Skip to the end of spaces
	 */
	for (; str[zi] == ' '; zi++) ;

	if (str[zi] == '\0') {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_exec_list_add: Can't find start of exec name");
		return (-1);
	}

	/*
	 * Found exec name
	 */
	exec_name = str + zi;

	/*
	 * Skip to the next spaces
	 */
	for (; str[zi] != ' ' && str[zi] != '\0'; zi++) ;

	if (str[zi] == '\0') {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_exec_list_add: Can't find second space");
		return (-1);
	}

	/*
	 * Put trailing \0 into exec_name
	 */
	str[zi] = '\0';
	zi++;

	/*
	 * Skip to the end of next spaces
	 */
	for (; str[zi] == ' '; zi++) ;

	if (str[zi] == '\0') {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_exec_list_add: Can't find start of exec command");
		return (-1);
	}

	/*
	 * Found exec_command
	 */
	exec_command = str + zi;

	qdevice_heuristics_worker_log_printf(instance, LOG_DEBUG,
	    "qdevice_heuristics_worker_cmd_process_one_line: Received exec-list-add command "
	    "with name \"%s\" and command \"%s\"", exec_name, exec_command);

	if (qdevice_heuristics_exec_list_add(&instance->exec_list, exec_name, exec_command) == NULL) {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_exec_list_add: Can't alloc exec list entry");
		return (-1);
	}

	return (0);
}

static int
qdevice_heuristics_worker_cmd_process_exec(struct qdevice_heuristics_worker_instance *instance,
    struct dynar *data)
{
	uint32_t timeout;
	uint32_t seq_number;
	char *str;
	struct qdevice_heuristics_exec_list_entry *exec_list_entry;
	struct process_list_entry *plist_entry;

	str = dynar_data(data);

	if (sscanf(str, QDEVICE_HEURISTICS_CMD_STR_EXEC_ADD_SPACE "%"PRIu32" %"PRIu32, &timeout,
	    &seq_number) != 2) {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_exec: Can't parse command (sscanf)");
		return (-1);
	}

	qdevice_heuristics_worker_log_printf(instance, LOG_DEBUG,
	    "qdevice_heuristics_worker_cmd_process_exec: Received exec command "
	    "with seq_no \"%"PRIu32"\" and timeout \"%"PRIu32"\"", seq_number, timeout);

	if (instance->exec_timeout_timer != NULL) {
		process_list_move_active_entries_to_kill_list(&instance->main_process_list);

		timer_list_delete(&instance->main_timer_list, instance->exec_timeout_timer);
		instance->exec_timeout_timer = NULL;
	}

	instance->last_exec_seq_number = seq_number;

	if (qdevice_heuristics_exec_list_is_empty(&instance->exec_list)) {
		if (qdevice_heuristics_worker_cmd_write_exec_result(instance,
		    instance->last_exec_seq_number,
		    QDEVICE_HEURISTICS_EXEC_RESULT_DISABLED) != 0) {
			return (-1);
		}
	} else {
		/*
		 * Initialize process list (from exec list)
		 */
		TAILQ_FOREACH(exec_list_entry, &instance->exec_list, entries) {
			plist_entry = process_list_add(&instance->main_process_list,
			    exec_list_entry->name, exec_list_entry->command);

			if (plist_entry == NULL) {
				qdevice_heuristics_worker_log_printf(instance, LOG_ERR,
				    "qdevice_heuristics_worker_cmd_process_exec: Can't allocate "
				    "process list entry");

				process_list_move_active_entries_to_kill_list(
				    &instance->main_process_list);

				if (qdevice_heuristics_worker_cmd_write_exec_result(instance,
				    instance->last_exec_seq_number,
				    QDEVICE_HEURISTICS_EXEC_RESULT_FAIL) != 0) {
					return (-1);
				}

				return (0);
			}
		}

		if (process_list_exec_initialized(&instance->main_process_list) != 0) {
			qdevice_heuristics_worker_log_printf(instance, LOG_ERR,
			    "qdevice_heuristics_worker_cmd_process_exec: Can't execute "
			    "process list");

			process_list_move_active_entries_to_kill_list(&instance->main_process_list);

			if (qdevice_heuristics_worker_cmd_write_exec_result(instance,
			    instance->last_exec_seq_number,
			    QDEVICE_HEURISTICS_EXEC_RESULT_FAIL) != 0) {
				return (-1);
			}

			return (0);
		}

		instance->exec_timeout_timer = timer_list_add(&instance->main_timer_list,
		    timeout, qdevice_heuristics_worker_exec_timeout_timer_callback,
		    (void *)instance, NULL);
		if (instance->exec_timeout_timer == NULL) {
			qdevice_heuristics_worker_log_printf(instance, LOG_ERR,
			    "qdevice_heuristics_worker_cmd_process_exec: Can't add exec timeout "
			    "timer to timer list");

			process_list_move_active_entries_to_kill_list(&instance->main_process_list);

			if (qdevice_heuristics_worker_cmd_write_exec_result(instance,
			    instance->last_exec_seq_number,
			    QDEVICE_HEURISTICS_EXEC_RESULT_FAIL) != 0) {
				return (-1);
			}

			return (0);
		}
	}

	return (0);
}

/*
 * 1 - Line processed
 * 0 - No line to process - everything processed
 * -1 - Error
 */
static int
qdevice_heuristics_worker_cmd_process_one_line(struct qdevice_heuristics_worker_instance *instance,
    struct dynar *data)
{
	char *str;
	size_t str_len;
	size_t nl_pos;
	size_t zi;

	str = dynar_data(data);
	str_len = dynar_size(data);

	/*
	 * Find valid line
	 */
	for (zi = 0; zi < str_len && str[zi] != '\r' && str[zi] != '\n'; zi++) ;

	if (zi >= str_len) {
		/*
		 * Command is not yet fully readed
		 */
		return (0);
	}

	nl_pos = zi;

	str[nl_pos] = '\0';

	if (strcmp(str, QDEVICE_HEURISTICS_CMD_STR_EXEC_LIST_CLEAR) == 0) {
		qdevice_heuristics_worker_log_printf(instance, LOG_DEBUG,
		    "qdevice_heuristics_worker_cmd_process_one_line: Received exec-list-clear command");

		qdevice_heuristics_exec_list_free(&instance->exec_list);
	} else if (strncmp(str, QDEVICE_HEURISTICS_CMD_STR_EXEC_LIST_ADD_SPACE,
	    strlen(QDEVICE_HEURISTICS_CMD_STR_EXEC_LIST_ADD)) == 0) {
		if (qdevice_heuristics_worker_cmd_process_exec_list_add(instance, data) != 0) {
			return (-1);
		}
	} else if (strncmp(str, QDEVICE_HEURISTICS_CMD_STR_EXEC_ADD_SPACE,
	    strlen(QDEVICE_HEURISTICS_CMD_STR_EXEC_ADD_SPACE)) == 0) {
		if (qdevice_heuristics_worker_cmd_process_exec(instance, data) != 0) {
			return (-1);
		}
	} else {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_one_line: Unknown command \"%s\" "
		    "received from main qdevice process", str);

		    return (-1);
	}

	/*
	 * Find place where is begining of new "valid" line
	 */
	for (zi = nl_pos + 1; zi < str_len && (str[zi] == '\0' || str[zi] == '\n' || str[zi] == '\r'); zi++) ;

	memmove(str, str + zi, str_len - zi);
	if (dynar_set_size(data, str_len - zi) == -1) {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "qdevice_heuristics_worker_cmd_process_one_line: Can't set dynar size");
		return (-1);
	}

	return (1);
}

/*
 * 0 - No error
 * -1 - Error
 */
static int
qdevice_heuristics_worker_cmd_process(struct qdevice_heuristics_worker_instance *instance)
{
	int res;

	while ((res =
	    qdevice_heuristics_worker_cmd_process_one_line(instance, &instance->cmd_in_buffer)) == 1) ;

	return (res);
}

/*
 * 0 - No error
 * 1 - Error
 */
int
qdevice_heuristics_worker_cmd_read_from_pipe(struct qdevice_heuristics_worker_instance *instance)
{
	int res;
	int ret;

	res = qdevice_heuristics_io_read(QDEVICE_HEURISTICS_WORKER_CMD_IN_FD, &instance->cmd_in_buffer);

	ret = 0;

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		qdevice_heuristics_worker_log_printf(instance, LOG_ERR,
		    "Lost connection with main qdevice process");
		ret = -1;
		break;
	case -2:
		qdevice_heuristics_worker_log_printf(instance, LOG_ERR,
		    "Heuristics sent too long command line");
		ret = -1;
		break;
	case -3:
		qdevice_heuristics_worker_log_printf(instance, LOG_ERR,
		    "Unhandled error when reading from heuristics command in fd");
		ret = -1;
		break;
	case 1:
		/*
		 * At least one log line received
		 */
		ret = qdevice_heuristics_worker_cmd_process(instance);
		break;
	}

	return (ret);
}

int
qdevice_heuristics_worker_cmd_write_exec_result(struct qdevice_heuristics_worker_instance *instance,
    uint32_t seq_number, enum qdevice_heuristics_exec_result exec_result)
{
	if (dynar_str_cpy(&instance->cmd_out_buffer,
	    QDEVICE_HEURISTICS_CMD_STR_EXEC_RESULT_ADD_SPACE) != -1 &&
	    dynar_str_catf(&instance->cmd_out_buffer, "%"PRIu32" %u\n", seq_number,
	    (int)exec_result) != -1) {
		(void)qdevice_heuristics_io_blocking_write(QDEVICE_HEURISTICS_WORKER_CMD_OUT_FD,
		    dynar_data(&instance->cmd_out_buffer), dynar_size(&instance->cmd_out_buffer));
	} else {
		qdevice_heuristics_worker_log_printf(instance, LOG_CRIT,
		    "Can't alloc memory for exec result");

		return (-1);
	}

	return (0);
}
