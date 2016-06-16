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

#include <sys/types.h>
#include <arpa/inet.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "pr-poll-array.h"

void
pr_poll_array_init(struct pr_poll_array *poll_array, size_t user_data_size)
{

	memset(poll_array, 0, sizeof(*poll_array));
	poll_array->user_data_size = user_data_size;
}

void
pr_poll_array_destroy(struct pr_poll_array *poll_array)
{

	free(poll_array->array);
	free(poll_array->user_data_array);
	pr_poll_array_init(poll_array, poll_array->user_data_size);
}

void
pr_poll_array_clean(struct pr_poll_array *poll_array)
{

	poll_array->items = 0;
}

static int
pr_poll_array_realloc(struct pr_poll_array *poll_array,
    ssize_t new_array_size)
{
	PRPollDesc *new_array;
	char *new_user_data_array;

	new_array = realloc(poll_array->array,
	    sizeof(PRPollDesc) * new_array_size);

	if (new_array == NULL) {
		return (-1);
	}

	poll_array->allocated = new_array_size;
	poll_array->array = new_array;

	if (poll_array->user_data_size > 0) {
		new_user_data_array = realloc(poll_array->user_data_array,
		    poll_array->user_data_size * new_array_size);

		if (new_user_data_array == NULL) {
			return (-1);
		}

		poll_array->user_data_array = new_user_data_array;
	}

	return (0);
}

ssize_t
pr_poll_array_size(struct pr_poll_array *poll_array)
{

	return (poll_array->items);
}

ssize_t
pr_poll_array_add(struct pr_poll_array *poll_array, PRPollDesc **pfds, void **user_data)
{

	if (pr_poll_array_size(poll_array) >= poll_array->allocated) {
		if (pr_poll_array_realloc(poll_array, (poll_array->allocated * 2) + 1)) {
			return (-1);
		}
	}

	*pfds = &poll_array->array[pr_poll_array_size(poll_array)];
	memset(*pfds, 0, sizeof(**pfds));

	*user_data = poll_array->user_data_array + (poll_array->items * poll_array->user_data_size);
	memset(*user_data, 0, poll_array->user_data_size);

	poll_array->items++;

	return (poll_array->items - 1);
}

void
pr_poll_array_gc(struct pr_poll_array *poll_array)
{

	if (poll_array->allocated > (pr_poll_array_size(poll_array) * 3) + 1) {
		pr_poll_array_realloc(poll_array, (pr_poll_array_size(poll_array) * 2) + 1);
	}
}

PRPollDesc *
pr_poll_array_get(const struct pr_poll_array *poll_array, ssize_t pos)
{

	if (pos >= poll_array->items) {
		return (NULL);
	}

	return (&poll_array->array[pos]);
}

void *
pr_poll_array_get_user_data(const struct pr_poll_array *poll_array, ssize_t pos)
{

	if (pos >= poll_array->items) {
		return (NULL);
	}

	return (poll_array->user_data_array + (pos * poll_array->user_data_size));
}
