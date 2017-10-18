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

#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qdevice-heuristics-io.h"

#define QDEVICE_HEURISTICS_IO_BUFFER_SIZE	256

ssize_t
qdevice_heuristics_io_blocking_write(int fd, const void *buf, size_t count)
{
	ssize_t bytes_written;
	ssize_t tmp_bytes_written;

	bytes_written = 0;

	do {
		tmp_bytes_written = write(fd, (const char *)buf + bytes_written,
		    (count - bytes_written > SSIZE_MAX) ? SSIZE_MAX : count - bytes_written);
		if (tmp_bytes_written == -1) {
			if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK) {
				return (-1);
			}
		} else {
			bytes_written += tmp_bytes_written;
		}
	} while ((size_t)bytes_written != count);

	return (bytes_written);
}

/*
 *  1 Full line readed (at least one \n found)
 *  0 Partial read (no error)
 * -1 End of connection
 * -2 Buffer too long
 * -3 Unhandled error
 */
int
qdevice_heuristics_io_read(int fd, struct dynar *dest)
{
	char buf[QDEVICE_HEURISTICS_IO_BUFFER_SIZE];
	ssize_t readed;
	int res;
	size_t zi;

	res = 0;
	readed = read(fd, buf, sizeof(buf));
	if (readed > 0) {
		if (dynar_cat(dest, buf, readed) == -1) {
			res = -2;
			goto exit_err;
		}

		for (zi = 0; zi < (size_t)readed; zi++) {
			if (buf[zi] == '\n') {
				res = 1;
			}
		}
	}

	if (readed == 0) {
		res = -1;
	}

	if (readed < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		res = -3;
	}

exit_err:
	return (res);
}

/*
 * 1 All data succesfully sent
 *  0 Partial send (no error)
 * -1 send returned 0,
 * -2 Unhandled error
 */
int
qdevice_heuristics_io_write(int fd, const struct dynar *msg, size_t *already_sent_bytes)
{
	ssize_t sent;
	size_t to_send;
	int res;

	res = 0;

	to_send = dynar_size(msg) - *already_sent_bytes;
	if (to_send > QDEVICE_HEURISTICS_IO_BUFFER_SIZE) {
		to_send = QDEVICE_HEURISTICS_IO_BUFFER_SIZE;
	}

	sent = write(fd, dynar_data(msg) + *already_sent_bytes,
	    to_send);

	if (sent > 0) {
		*already_sent_bytes += sent;

		if (*already_sent_bytes == dynar_size(msg)) {
			return (1);
		}
	}

	if (sent == 0) {
		res = -1;
	}

	if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		res = -2;
	}

	return (res);
}
