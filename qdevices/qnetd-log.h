/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#ifndef _QNETD_LOG_H_
#define _QNETD_LOG_H_

#include <syslog.h>
#include <stdarg.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QNETD_LOG_TARGET_STDERR		1
#define QNETD_LOG_TARGET_SYSLOG		2

#define qnetd_log(...)	qnetd_log_printf(__VA_ARGS__)
#define qnetd_log_nss(priority, str) qnetd_log_printf(priority, "%s (%d): %s", \
    str, PR_GetError(), PR_ErrorToString(PR_GetError(), PR_LANGUAGE_I_DEFAULT));

#define qnetd_log_err(priority, str) qnetd_log_printf(priority, "%s (%d): %s", \
    str, errno, strerror(errno))

extern void		qnetd_log_init(int target);

extern void		qnetd_log_printf(int priority, const char *format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

extern void		qnetd_log_vprintf(int priority, const char *format, va_list ap)
    __attribute__((__format__(__printf__, 2, 0)));

extern void		qnetd_log_close(void);

extern void		qnetd_log_set_debug(int enabled);

extern void		qnetd_log_set_priority_bump(int enabled);

extern void		qnetd_log_msg_decode_error(int ret);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_LOG_H_ */
