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

#ifndef _PR_POLL_ARRAY_H_
#define _PR_POLL_ARRAY_H_

#include <sys/types.h>
#include <inttypes.h>

#include <nspr.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pr_poll_array {
	PRPollDesc* array;
	char* user_data_array;
	size_t user_data_size;
	ssize_t allocated;
	ssize_t items;
};

extern void pr_poll_array_init(struct pr_poll_array* poll_array, size_t user_data_size);

extern void pr_poll_array_destroy(struct pr_poll_array* poll_array);

extern void pr_poll_array_clean(struct pr_poll_array* poll_array);

extern ssize_t pr_poll_array_size(struct pr_poll_array* poll_array);

extern ssize_t pr_poll_array_add(struct pr_poll_array* poll_array, PRPollDesc** pfds, void** user_data);

extern PRPollDesc* pr_poll_array_get(const struct pr_poll_array* poll_array, ssize_t pos);

extern void* pr_poll_array_get_user_data(const struct pr_poll_array* poll_array, ssize_t pos);

extern void pr_poll_array_gc(struct pr_poll_array* poll_array);

#ifdef __cplusplus
}
#endif

#endif /* _PR_POLL_ARRAY_H_ */
