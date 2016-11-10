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

#include "dynar.h"
#include "dynar-str.h"
#include "qdevice-heuristics-exec-result.h"
#include "qdevice-heuristics-cmd.h"
#include "qdevice-heuristics-cmd-str.h"
#include "qdevice-heuristics-io.h"
#include "qdevice-log.h"

static int
qdevice_heuristics_cmd_process_exec_result(struct qdevice_heuristics_instance *instance,
    struct dynar *data)
{
	uint32_t seq_number;
	char *str;
	enum qdevice_heuristics_exec_result exec_result;

	str = dynar_data(data);

	if (sscanf(str, QDEVICE_HEURISTICS_CMD_STR_EXEC_RESULT_ADD_SPACE "%"PRIu32" %u", &seq_number,
	    &exec_result) != 2) {
		qdevice_log(LOG_CRIT, "Can't parse exec result command (sscanf)");

		return (-1);
	}

	qdevice_log(LOG_DEBUG,
	    "Received heuristics exec result command with seq_no \"%"PRIu32"\" and result \"%s\"", seq_number,
	    qdevice_heuristics_exec_result_to_str(exec_result));

	if (!instance->waiting_for_result) {
		qdevice_log(LOG_DEBUG, "Received exec result is not expected. Ignoring.");

		return (0);
	}

	if (seq_number != instance->expected_reply_seq_number) {
		qdevice_log(LOG_DEBUG, "Received heuristics exec result seq number %"PRIu32
		    " is not expected one (expected %"PRIu32"). Ignoring.", seq_number,
		    instance->expected_reply_seq_number);

		return (0);
	}

	instance->waiting_for_result = 0;

	if (qdevice_heuristics_result_notifier_notify(&instance->exec_result_notifier_list,
	    (void *)instance, seq_number, exec_result) != 0) {
		qdevice_log(LOG_DEBUG, "qdevice_heuristics_result_notifier_notify returned non-zero result");
		return (-1);
	}

	return (0);
}

/*
 * 1 - Line processed
 * 0 - No line to process - everything processed
 * -1 - Error
 */
static int
qdevice_heuristics_cmd_process_one_line(struct qdevice_heuristics_instance *instance,
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

	if (strncmp(str, QDEVICE_HEURISTICS_CMD_STR_EXEC_RESULT_ADD_SPACE,
	    strlen(QDEVICE_HEURISTICS_CMD_STR_EXEC_RESULT_ADD_SPACE)) == 0) {
		if (qdevice_heuristics_cmd_process_exec_result(instance, data) != 0) {
			return (-1);
		}
	} else {
		qdevice_log(LOG_CRIT,
		    "Heuristics worker sent unknown command \"%s\"", str);

		    return (-1);
	}

	/*
	 * Find place where is begining of new "valid" line
	 */
	for (zi = nl_pos + 1; zi < str_len && (str[zi] == '\0' || str[zi] == '\n' || str[zi] == '\r'); zi++) ;

	memmove(str, str + zi, str_len - zi);
	if (dynar_set_size(data, str_len - zi) == -1) {
		qdevice_log(LOG_CRIT,
		    "qdevice_heuristics_cmd_process_one_line: Can't set dynar size");
		return (-1);
	}

	return (1);
}

/*
 * 0 - No error
 * -1 - Error
 */
static int
qdevice_heuristics_cmd_process(struct qdevice_heuristics_instance *instance)
{
	int res;

	while ((res =
	    qdevice_heuristics_cmd_process_one_line(instance, &instance->cmd_in_buffer)) == 1) ;

	return (res);
}

/*
 * 0 - No error
 * 1 - Error
 */
int
qdevice_heuristics_cmd_read_from_pipe(struct qdevice_heuristics_instance *instance)
{
	int res;
	int ret;

	res = qdevice_heuristics_io_read(instance->pipe_cmd_recv, &instance->cmd_in_buffer);

	ret = 0;

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		qdevice_log(LOG_ERR, "Lost connection with heuristics worker");
		ret = -1;
		break;
	case -2:
		qdevice_log(LOG_ERR, "Heuristics worker sent too long cmd.");
		ret = -1;
		break;
	case -3:
		qdevice_log(LOG_ERR, "Unhandled error when reading from heuristics worker cmd fd");
		ret = -1;
		break;
	case 1:
		/*
		 * At least one cmd line received
		 */
		ret = qdevice_heuristics_cmd_process(instance);
		break;
	}

	return (ret);
}

/*
 * 0 - No error
 * 1 - Error
 */
int
qdevice_heuristics_cmd_write(struct qdevice_heuristics_instance *instance)
{
	struct send_buffer_list_entry *send_buffer;
	int res;

	send_buffer = send_buffer_list_get_active(&instance->cmd_out_buffer_list);
	if (send_buffer == NULL) {
		qdevice_log(LOG_CRIT, "send_buffer_list_get_active in qdevice_heuristics_cmd_write returned NULL");

		return (-1);
	}

	res = qdevice_heuristics_io_write(instance->pipe_cmd_send, &send_buffer->buffer,
	    &send_buffer->msg_already_sent_bytes);

	if (res == 1) {
		send_buffer_list_delete(&instance->cmd_out_buffer_list, send_buffer);
	}

	if (res == -1) {
		qdevice_log(LOG_CRIT, "qdevice_heuristics_io_write returned -1 (write returned 0)");

		return (-1);
	}

	if (res == -2) {
		qdevice_log(LOG_CRIT, "Unhandled error in during sending message to heuristics "
		    "worker (qdevice_heuristics_io_write returned -2)");

		return (-1);
	}

	return (0);
}

static int
qdevice_heuristics_cmd_remove_newlines(struct dynar *str)
{
	size_t len;
	size_t zi;
	char *buf;

	len = dynar_size(str);
	buf = dynar_data(str);

	for (zi = 0; zi < len ; zi++) {
		if (buf[zi] == '\n' || buf[zi] == '\r') {
			buf[zi] = ' ';
		}
	}

	return (0);
}

int
qdevice_heuristics_cmd_write_exec_list(struct qdevice_heuristics_instance *instance,
    const struct qdevice_heuristics_exec_list *new_exec_list)
{
	struct send_buffer_list_entry *send_buffer;
	struct qdevice_heuristics_exec_list_entry *entry;

	send_buffer = send_buffer_list_get_new(&instance->cmd_out_buffer_list);
	if (send_buffer == NULL) {
		qdevice_log(LOG_ERR, "Can't alloc send list for cmd change exec list");

		return (-1);
	}

	if (dynar_str_cpy(&send_buffer->buffer, QDEVICE_HEURISTICS_CMD_STR_EXEC_LIST_CLEAR) == -1 ||
	    dynar_str_cat(&send_buffer->buffer, "\n") == -1) {
		qdevice_log(LOG_ERR, "Can't alloc list clear message");

		send_buffer_list_discard_new(&instance->cmd_out_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&instance->cmd_out_buffer_list, send_buffer);

	if (new_exec_list == NULL) {
		return (0);
	}

	/*
	 * new_exec_list is not NULL, send it
	 */
	TAILQ_FOREACH(entry, new_exec_list, entries) {
		send_buffer = send_buffer_list_get_new(&instance->cmd_out_buffer_list);
		if (send_buffer == NULL) {
			qdevice_log(LOG_ERR, "Can't alloc send list for cmd change exec list");

			return (-1);
		}

		if (dynar_str_cpy(&send_buffer->buffer,
		    QDEVICE_HEURISTICS_CMD_STR_EXEC_LIST_ADD_SPACE) == -1 ||
		    dynar_str_cat(&send_buffer->buffer, entry->name) == -1 ||
		    dynar_str_cat(&send_buffer->buffer, " ") == -1 ||
		    dynar_str_cat(&send_buffer->buffer, entry->command) == -1 ||
		    qdevice_heuristics_cmd_remove_newlines(&send_buffer->buffer) == -1 ||
		    dynar_str_cat(&send_buffer->buffer, "\n") == -1) {
			qdevice_log(LOG_ERR, "Can't alloc list add message");

			send_buffer_list_discard_new(&instance->cmd_out_buffer_list, send_buffer);

			return (-1);
		}

		send_buffer_list_put(&instance->cmd_out_buffer_list, send_buffer);
	}

	return (0);
}

int
qdevice_heuristics_cmd_write_exec(struct qdevice_heuristics_instance *instance,
    uint32_t timeout, uint32_t seq_number)
{
	struct send_buffer_list_entry *send_buffer;

	send_buffer = send_buffer_list_get_new(&instance->cmd_out_buffer_list);
	if (send_buffer == NULL) {
		qdevice_log(LOG_ERR, "Can't alloc send list for cmd change exec list");

		return (-1);
	}

	if (dynar_str_cpy(&send_buffer->buffer, QDEVICE_HEURISTICS_CMD_STR_EXEC_ADD_SPACE) == -1 ||
	    dynar_str_catf(&send_buffer->buffer, "%"PRIu32" %"PRIu32"\n", timeout, seq_number) == -1) {
		qdevice_log(LOG_ERR, "Can't alloc exec message");

		send_buffer_list_discard_new(&instance->cmd_out_buffer_list, send_buffer);

		return (-1);
	}

	send_buffer_list_put(&instance->cmd_out_buffer_list, send_buffer);

	return (0);
}
