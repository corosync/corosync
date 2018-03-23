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

#include "msgio.h"
#include "msg.h"

#define MSGIO_LOCAL_BUF_SIZE			(1 << 10)

/*
 * -1 = send returned 0,
 * -2 = unhandled error.
 *  0 = success but whole buffer is still not sent
 *  1 = all data was sent
 */
int
msgio_write(PRFileDesc *sock, const struct dynar *msg, size_t *already_sent_bytes)
{
	PRInt32 sent;
	PRInt32 to_send_i32;
	size_t to_send;

	to_send = dynar_size(msg) - *already_sent_bytes;
	if (to_send > MSGIO_LOCAL_BUF_SIZE) {
		to_send_i32 = MSGIO_LOCAL_BUF_SIZE;
	} else {
		to_send_i32 = (PRInt32)to_send;
	}

	sent = PR_Send(sock, dynar_data(msg) + *already_sent_bytes, to_send_i32, 0,
	    PR_INTERVAL_NO_TIMEOUT);

	if (sent > 0) {
		*already_sent_bytes += (size_t)sent;

		if (*already_sent_bytes == dynar_size(msg)) {
			/*
			 * All data sent
			 */
			return (1);
		}
	}

	if (sent == 0) {
		return (-1);
	}

	if (sent < 0 && PR_GetError() != PR_WOULD_BLOCK_ERROR) {
		return (-2);
	}

	return (0);
}

/*
 *  1 Full message received
 *  0 Partial read (no error)
 * -1 End of connection
 * -2 Unhandled error
 * -3 Fatal error. Unable to store message header
 * -4 Unable to store message
 * -5 Invalid msg type
 * -6 Msg too long
 */
int
msgio_read(PRFileDesc *sock, struct dynar *msg, size_t *already_received_bytes, int *skipping_msg)
{
	char local_read_buffer[MSGIO_LOCAL_BUF_SIZE];
	PRInt32 readed;
	size_t to_read;
	PRInt32 to_read_i32;
	int ret;

	ret = 0;

	if (*already_received_bytes < msg_get_header_length()) {
		/*
		 * Complete reading of header
		 */
		to_read = msg_get_header_length() - *already_received_bytes;
	} else {
		/*
		 * Read rest of message (or at least as much as possible)
		 */
		to_read = (msg_get_header_length() + msg_get_len(msg)) - *already_received_bytes;
	}

	if (to_read > MSGIO_LOCAL_BUF_SIZE) {
		to_read_i32 = MSGIO_LOCAL_BUF_SIZE;
	} else {
		to_read_i32 = (PRInt32)to_read;
	}

	readed = PR_Recv(sock, local_read_buffer, to_read_i32, 0, PR_INTERVAL_NO_TIMEOUT);
	if (readed > 0) {
		*already_received_bytes += (size_t)readed;

		if (!*skipping_msg) {
			if (dynar_cat(msg, local_read_buffer, readed) == -1) {
				*skipping_msg = 1;
				ret = -4;
			}
		}

		if (*skipping_msg && *already_received_bytes < msg_get_header_length()) {
			/*
			 * Fatal error. We were unable to store even message header
			 */
			return (-3);
		}

		if (!*skipping_msg && *already_received_bytes == msg_get_header_length()) {
			/*
			 * Full header received. Check type, maximum size, ...
			 */
			if (!msg_is_valid_msg_type(msg)) {
				*skipping_msg = 1;
				ret = -5;
			} else if ((msg_get_header_length() + msg_get_len(msg)) >
			    dynar_max_size(msg)) {
				*skipping_msg = 1;
				ret = -6;
			}
		}

		if (*already_received_bytes >= msg_get_header_length() &&
		    *already_received_bytes == (msg_get_header_length() + msg_get_len(msg))) {
			/*
			 * Full message skipped or received
			 */
			ret = 1;
		}

	}

	if (readed == 0) {
		return (-1);
	}

	if (readed < 0 && PR_GetError() != PR_WOULD_BLOCK_ERROR) {
		return (-2);
	}

	return (ret);
}
