/*
 * Copyright (c) 2015-2019 Red Hat, Inc.
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

#include "qdevice-log.h"
#include "qdevice-config.h"
#include "utils.h"

static int qdevice_log_global_force_debug;

void
qdevice_log_configure(struct qdevice_instance *instance)
{
	int debug;
	char *str;
	int i;

	debug = QDEVICE_LOG_DEFAULT_DEBUG;
	if (cmap_get_string(instance->cmap_handle, "logging.debug", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			if (strcasecmp(str, "trace") == 0) {
				debug = 1;
			} else {
				log(LOG_WARNING, "logging.debug value is not valid");
			}
		} else {
			debug = i;
		}
		free(str);
	}

	if (cmap_get_string(instance->cmap_handle,
	    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".debug", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			if (strcasecmp(str, "trace") == 0) {
				debug = 1;
			} else {
				log(LOG_WARNING,
				    "logging.logger_subsys." QDEVICE_LOG_SUBSYS ".debug value is not valid.");
			}
		} else {
			debug = i;
		}
		free(str);
	}

	if (qdevice_log_global_force_debug) {
		debug = 1;
	}

	log_set_debug(debug);
}

int
qdevice_log_init(struct qdevice_instance *instance, int foreground, int force_debug, int bump_log_priority)
{
	int res;
	int log_target;

	qdevice_log_global_force_debug = force_debug;

	log_target = LOG_TARGET_SYSLOG;
	if (foreground) {
		log_target |= LOG_TARGET_STDERR;
	}

	res = log_init(QDEVICE_PROGRAM_NAME, log_target, QDEVICE_LOG_DEFAULT_SYSLOG_FACILITY);
	if (res == -1) {
		return (res);
	}

	log_set_priority_bump(bump_log_priority);

	qdevice_log_configure(instance);

	return (0);
}

void
qdevice_log_close(struct qdevice_instance *instance)
{

	log_close();
}
