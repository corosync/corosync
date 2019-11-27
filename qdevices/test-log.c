/*
 * Copyright (c) 2019 Red Hat, Inc.
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

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include "log.h"

#define MAX_LINE_LEN		512

static int openlog_called;
static int openlog_facility;
static char vsyslog_buf[MAX_LINE_LEN];
static int vsyslog_priority;
static int vsyslog_called;
static int closelog_called;

extern void __vsyslog_chk(int priority, int flag, const char *format, va_list ap)
    __attribute__((__format__(__printf__, 3, 0)));

void
openlog(const char *ident, int option, int facility)
{

	openlog_called = 1;
	openlog_facility = facility;
}

void
vsyslog(int priority, const char *format, va_list ap)
{
	va_list ap_copy;
	int res;

	vsyslog_called = 1;
	vsyslog_priority = priority;

	va_copy(ap_copy, ap);
	res = vsnprintf(vsyslog_buf, MAX_LINE_LEN, format, ap_copy);
	assert(res < MAX_LINE_LEN && res != -1);
	va_end(ap_copy);
}

void
__vsyslog_chk(int priority, int flag, const char *format, va_list ap)
{

	vsyslog(priority, format, ap);
}

void
closelog(void)
{

	closelog_called = 1;
}

static void
log_check(int priority, const char *msg, int should_be_called, int expected_priority)
{

	vsyslog_called = 0;
	vsyslog_priority = -1;
	vsyslog_buf[0] = '\0';

	log(priority, "%s", msg);

	if (should_be_called) {
		assert(vsyslog_called);
		assert(vsyslog_priority == expected_priority);
		assert(strcmp(vsyslog_buf, msg) == 0);
	} else {
		assert(!vsyslog_called);
	}
}

int
main(void)
{

	openlog_called = 0;
	assert(log_init("test", LOG_TARGET_SYSLOG, LOG_DAEMON) == 0);
	assert(openlog_called);
	assert(openlog_facility == LOG_DAEMON);

	log_check(LOG_INFO, "test log info", 1, LOG_INFO);
	log_check(LOG_ERR, "test log err", 1, LOG_ERR);
	log_check(LOG_DEBUG, "test log debug", 0, LOG_DEBUG);

	log_set_debug(1);
	log_check(LOG_DEBUG, "test log debug", 1, LOG_DEBUG);

	log_set_debug(0);
	log_check(LOG_DEBUG, "test log debug", 0, LOG_DEBUG);

	log_set_debug(1);
	log_set_priority_bump(1);
	log_check(LOG_ERR, "test log err", 1, LOG_ERR);
	log_check(LOG_DEBUG, "test log debug", 1, LOG_INFO);

	log_set_priority_bump(0);
	log_check(LOG_ERR, "test log err", 1, LOG_ERR);
	log_check(LOG_DEBUG, "test log debug", 1, LOG_DEBUG);

	closelog_called = 0;
	openlog_called = 0;
	log_set_target(LOG_TARGET_SYSLOG, LOG_USER);
	assert(openlog_called);
	assert(closelog_called);
	assert(openlog_facility == LOG_USER);

	closelog_called = 0;
	log_close();
	assert(closelog_called);

	return (0);
}
