/*
 * Copyright (c) 2015 Red Hat, Inc.
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

#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>

#include "qnet-config.h"
#include "qnetd-log.h"

static int qnetd_log_config_target = 0;
static int qnetd_log_config_debug = 0;

void
qnetd_log_init(int target)
{

	qnetd_log_config_target = target;

	if (qnetd_log_config_target & QNETD_LOG_TARGET_SYSLOG) {
		openlog(QNETD_PROGRAM_NAME, LOG_PID, LOG_DAEMON);
	}
}

void
qnetd_log_printf(int priority, const char *format, ...)
{
	va_list ap;

	if (priority != LOG_DEBUG || (qnetd_log_config_debug)) {
		if (qnetd_log_config_target & QNETD_LOG_TARGET_STDERR) {
			va_start(ap, format);
			vfprintf(stderr, format, ap);
			fprintf(stderr, "\n");
			va_end(ap);
		}

		if (qnetd_log_config_target & QNETD_LOG_TARGET_SYSLOG) {
			va_start(ap, format);
			vsyslog(priority, format, ap);
			va_end(ap);
		}
	}
}

void
qnetd_log_close(void)
{

	if (qnetd_log_config_target & QNETD_LOG_TARGET_SYSLOG) {
		closelog();
	}
}

void
qnetd_log_set_debug(int enabled)
{

	qnetd_log_config_debug = enabled;
}
