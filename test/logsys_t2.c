/*
 * Copyright (c) 2007 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Lon Hohberger (lhh@redhat.com)
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

#include <config.h>

#include <stdio.h>
#include <stdint.h>

#include <corosync/engine/logsys.h>

LOGSYS_DECLARE_SYSTEM ("logtest_t2",
	LOGSYS_MODE_OUTPUT_STDERR | LOGSYS_MODE_THREADED,
	0,
	NULL,
	LOGSYS_LEVEL_INFO,
	LOG_DAEMON,
	LOGSYS_LEVEL_INFO,
	NULL,
	1000000);

int
main(int argc, char **argv)
{
	/*
	 * fork could occur here and the file to output to could be set
	 */
	logsys_config_mode_set (NULL, LOGSYS_MODE_OUTPUT_STDERR | LOGSYS_MODE_THREADED);

	log_printf(LOGSYS_LEVEL_NOTICE, "Hello, world!\n");
	log_printf(LOGSYS_LEVEL_DEBUG, "If you see this, the logger's busted\n");

	logsys_config_logfile_priority_set (NULL, LOGSYS_LEVEL_ALERT);

	log_printf(LOGSYS_LEVEL_DEBUG, "If you see this, the logger's busted\n");
	log_printf(LOGSYS_LEVEL_CRIT, "If you see this, the logger's busted\n");
	log_printf(LOGSYS_LEVEL_ALERT, "Alert 1\n");

	logsys_config_logfile_priority_set (NULL, LOGSYS_LEVEL_NOTICE);

	log_printf(LOGSYS_LEVEL_CRIT, "Crit 1\n");
	log_printf(LOGSYS_LEVEL_INFO, "If you see this, the logger's busted\n");

	logsys_config_logfile_priority_set (NULL, LOGSYS_LEVEL_DEBUG);

	log_printf(LOGSYS_LEVEL_DEBUG, "Debug 1\n");

	logsys_config_mode_set (NULL, LOGSYS_MODE_OUTPUT_STDERR);

	log_printf(LOGSYS_LEVEL_DEBUG, "Debug 2\n");

	return 0;
}
