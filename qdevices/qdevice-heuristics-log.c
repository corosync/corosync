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

#include "qdevice-heuristics-io.h"
#include "qdevice-heuristics-log.h"
#include "qdevice-log.h"

/*
 * 1 - Line logged
 * 0 - No line to log - everything processed
 * -1 - Error
 */
static int
qdevice_heuristics_log_process_one_line(struct dynar *data)
{
	char *str;
	char *log_str_start;
	size_t str_len;
	size_t nl_pos;
	size_t zi;
	int status;
	unsigned int log_priority;

	str = dynar_data(data);
	str_len = dynar_size(data);
	log_str_start = str;

	status = 0;
	log_priority = 0;
	/*
	 * Find start of log message and end of line
	 */
	for (zi = 0; zi < str_len && status != -1; zi++) {
		switch (status) {
		case 0:
			if (str[zi] >= '0' && str[zi] <= '9') {
				log_priority = log_priority * 10 + (str[zi] - '0');
			} else if (str[zi] == ' ') {
				status = 1;
			} else {
				qdevice_log(LOG_ERR, "Parsing of heuristics log line failed. "
				    "Unexpected char '%c'", str[zi]);
				return (-1);
			}
			break;
		case 1:
			if (str[zi] != ' ') {
				status = 2;
				log_str_start = str + zi;
			}
			break;
		case 2:
			if (str[zi] == '\n' || str[zi] == '\r') {
				str[zi] = '\0';
				nl_pos = zi;
				status = -1;
			}
			break;
		}
	}

	if (status != -1) {
		return (0);
	}

	/*
	 * Do actual logging
	 */
	qb_log_from_external_source(__func__, __FILE__, "worker: %s", log_priority, __LINE__, 0, log_str_start);

	/*
	 * Find place where is begining of new "valid" line
	 */
	for (zi = nl_pos + 1; zi < str_len && (str[zi] == '\0' || str[zi] == '\n' || str[zi] == '\r'); zi++) ;

	memmove(str, str + zi, str_len - zi);
	if (dynar_set_size(data, str_len - zi) == -1) {
		qdevice_log(LOG_ERR, "qdevice_heuristics_log_process_one_line: Can't set dynar size");
		return (-1);
	}

	return (1);

}


/*
 * 0 - No error
 * -1 - Error
 */
static int
qdevice_heuristics_log_process(struct qdevice_heuristics_instance *instance)
{
	int res;

	while ((res = qdevice_heuristics_log_process_one_line(&instance->log_in_buffer)) == 1) ;

	return (res);
}

/*
 * 0 - No error
 * 1 - Error
 */
int
qdevice_heuristics_log_read_from_pipe(struct qdevice_heuristics_instance *instance)
{
	int res;
	int ret;

	res = qdevice_heuristics_io_read(instance->pipe_log_recv, &instance->log_in_buffer);

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
		qdevice_log(LOG_ERR, "Heuristics worker sent too long log. Ignoring line");
		dynar_clean(&instance->log_in_buffer);
		break;
	case -3:
		qdevice_log(LOG_ERR, "Unhandled error when reading from heuristics worker log fd");
		ret = -1;
		break;
	case 1:
		/*
		 * At least one log line received
		 */
		ret = qdevice_heuristics_log_process(instance);
		break;
	}

	return (ret);
}
