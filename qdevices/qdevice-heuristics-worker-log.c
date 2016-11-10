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

#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>

#include "dynar.h"
#include "dynar-str.h"
#include "qdevice-heuristics-io.h"
#include "qdevice-heuristics-worker.h"
#include "qdevice-heuristics-worker-log.h"

static int
qdevice_heuristics_worker_log_remove_newlines(struct dynar *str)
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

void
qdevice_heuristics_worker_log_printf(struct qdevice_heuristics_worker_instance *instance,
    int priority, const char *format, ...)
{
	va_list ap;
	va_list ap_copy;

	va_start(ap, format);

	if (dynar_str_cpy(&instance->log_out_buffer, "") != -1 &&
	    dynar_str_catf(&instance->log_out_buffer, "%u ", priority) != -1 &&
	    dynar_str_vcatf(&instance->log_out_buffer, format, ap) != -1 &&
	    qdevice_heuristics_worker_log_remove_newlines(&instance->log_out_buffer) != -1 &&
	    dynar_str_cat(&instance->log_out_buffer, "\n") != -1) {
		/*
		 * It was possible to log everything
		 */
		(void)qdevice_heuristics_io_blocking_write(QDEVICE_HEURISTICS_WORKER_LOG_OUT_FD,
		    dynar_data(&instance->log_out_buffer), dynar_size(&instance->log_out_buffer));
	} else {
		/*
		 * As a fallback try to log to syslog
		 */
		va_copy(ap_copy, ap);
		openlog("qdevice_heuristics_worker", LOG_PID, LOG_DAEMON);
		syslog(LOG_ERR, "Log entry sent to syslog instead of parent process");
		vsyslog(priority, format, ap_copy);
		closelog();
		va_end(ap_copy);
	}

	va_end(ap);
}
