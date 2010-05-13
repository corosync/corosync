/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

LOGSYS_DECLARE_SYSTEM ("logtest_rec",
	LOGSYS_MODE_OUTPUT_STDERR | LOGSYS_MODE_THREADED,
	0,
	NULL,
	LOG_INFO,
	LOG_DAEMON,
	LOG_INFO,
	NULL,
	100000);

#define LOGREC_ID_CHECKPOINT_CREATE 2
#define LOGREC_ARGS_CHECKPOINT_CREATE 2

int main(int argc, char **argv)
{
	int i;

	for (i = 0; i < 10000; i++) {
		log_printf (LOGSYS_LEVEL_NOTICE,
			"This is a test of %s(%d)\n", "stringparse", i);

		log_rec (LOGSYS_ENCODE_RECID(LOGSYS_LEVEL_NOTICE,
					     logsys_subsys_id,
					     LOGREC_ID_CHECKPOINT_CREATE),
			"record1", 8, "record22", 9, "record333", 10, "record444", 11, LOGSYS_REC_END);
	}
	logsys_atexit ();
	logsys_log_rec_store ("fdata");

	return 0;
}
