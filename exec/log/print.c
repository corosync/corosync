/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/un.h>

#include "print.h"

int log_fd = 0;

struct sockaddr_un syslog_sockaddr = {
	sun_family: AF_UNIX,
	sun_path: "/dev/log"
};

/*
 * logging printf
 */
void
internal_log_printf (int level, char *string, ...)
{
	va_list ap;
	char newstring[1024];
	struct msghdr msg_log;
	struct iovec iov_log;
	int res;

	va_start(ap, string);
	
	sprintf (newstring, "L(%x): %s", level, string);
	vfprintf(stderr, newstring, ap);

	va_end(ap);

	if (log_fd == 0) {
		log_fd = socket (AF_UNIX, SOCK_DGRAM, 0);
	}

	iov_log.iov_base = newstring;
	iov_log.iov_len = strlen (newstring) + 1;

	msg_log.msg_iov = &iov_log;
	msg_log.msg_iovlen = 1;
	msg_log.msg_name = &syslog_sockaddr;
	msg_log.msg_namelen = sizeof (syslog_sockaddr);
	msg_log.msg_control = 0;
	msg_log.msg_controllen = 0;
	msg_log.msg_flags = 0;

	res = sendmsg (log_fd, &msg_log, MSG_NOSIGNAL | MSG_DONTWAIT);
printf ("res is %d\n", res);
}

int main (void) {
	log_printf (LOG_LEVEL_ERROR, "This is error string 1=%d\n", 1);
}
