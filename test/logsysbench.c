/*
 * Copyright (c) 2008, 2009 Red Hat, Inc.
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
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <corosync/engine/logsys.h>

LOGSYS_DECLARE_SYSTEM ("logtest_rec",
	LOGSYS_MODE_OUTPUT_STDERR | LOGSYS_MODE_THREADED,
	0,                /* debug */
	NULL,
	LOGSYS_LEVEL_INFO,   /* logfile_priority */
	LOG_DAEMON,       /* syslog facility */
	LOGSYS_LEVEL_INFO,   /* syslog level */
	NULL,             /* use default format */
	1000000);         /* flight recorder size */

#define LOGREC_ID_CHECKPOINT_CREATE 2
#define LOGREC_ARGS_CHECKPOINT_CREATE 2
#define ITERATIONS 1000000

static struct timeval tv1, tv2, tv_elapsed;

#ifndef timersub
#define timersub(a, b, result)					\
do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {				\
		--(result)->tv_sec;				\
		(result)->tv_usec += 1000000;			\
	}							\
} while (0)
#endif

static void bm_start (void)
{
        gettimeofday (&tv1, NULL);
}
static void bm_finish (const char *operation)
{
        gettimeofday (&tv2, NULL);
        timersub (&tv2, &tv1, &tv_elapsed);

	if (strlen (operation) > 22) {
        	printf ("%s\t\t", operation);
	} else {
        	printf ("%s\t\t\t", operation);
	}
        printf ("%9.3f operations/sec\n",
                ((float)ITERATIONS) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
}

static char buffer[256];
int main (void)
{
	int i;
	char buf[1024];


	printf ("heating up cache with logrec functionality\n");
	for (i = 0; i < ITERATIONS; i++) {
	log_rec (LOGREC_ID_CHECKPOINT_CREATE,
		"recordA", 8, "recordB", 8, LOGSYS_REC_END);
	}
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
	log_rec (LOGREC_ID_CHECKPOINT_CREATE,
		buffer, 7, LOGSYS_REC_END);
	}
	bm_finish ("log_rec 1 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
	log_rec (LOGREC_ID_CHECKPOINT_CREATE,
		"recordA", 8, LOGSYS_REC_END);
	}
	bm_finish ("log_rec 2 arguments:");
	bm_start();
	for (i = 0; i < 10; i++) {
	log_rec (LOGREC_ID_CHECKPOINT_CREATE,
		"recordA", 8, "recordB", 8, LOGSYS_REC_END);
	}
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
	log_rec (LOGREC_ID_CHECKPOINT_CREATE,
		"recordA", 8, "recordB", 8, "recordC", 8, LOGSYS_REC_END);
	}
	bm_finish ("log_rec 3 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
	log_rec (LOGREC_ID_CHECKPOINT_CREATE,
		"recordA", 8, "recordB", 8, "recordC", 8, "recordD", 8, LOGSYS_REC_END);
	}
	bm_finish ("log_rec 4 arguments:");

	/*
	 * sprintf testing
	 */
	printf ("heating up cache with sprintf functionality\n");
	for (i = 0; i < ITERATIONS; i++) {
		snprintf (buf, sizeof(buf), "Some logging information %s", "recordA");
	}
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		snprintf (buf, sizeof(buf), "Some logging information %s", "recordA");
	}
	bm_finish ("sprintf 1 argument:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		sprintf (buf, "Some logging information %s %s", "recordA", "recordB");
	}
	bm_finish ("sprintf 2 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		sprintf (buf, "Some logging information %s %s %s", "recordA", "recordB", "recordC");
	}
	bm_finish ("sprintf 3 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		sprintf (buf, "Some logging information %s %s %s %s", "recordA", "recordB", "recordC", "recordD");
	}
	bm_finish ("sprintf 4 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		sprintf (buf, "Some logging information %s %s %s %d", "recordA", "recordB", "recordC", i);
	}
	bm_finish ("sprintf 4 arguments (1 int):");

	logsys_log_rec_store ("fdata");
/* TODO
	currently fails under some circumstances

	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
	log_printf (LOGSYS_LEVEL_NOTICE, "test %d", i);
	}
	bm_finish("log_printf");
*/

	return (0);
}
