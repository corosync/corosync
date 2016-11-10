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

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "dynar.h"

void
dynar_init(struct dynar *array, size_t maximum_size)
{

	memset(array, 0, sizeof(*array));
	array->maximum_size = maximum_size;
}

void
dynar_set_max_size(struct dynar *array, size_t maximum_size)
{

	array->maximum_size = maximum_size;
}

int
dynar_set_size(struct dynar *array, size_t size)
{

	if (size > dynar_max_size(array)) {
		dynar_set_max_size(array, size);
	}

	if (size > dynar_size(array)) {
		if (dynar_prealloc(array, size - dynar_size(array)) == -1) {
			return (-1);
		}
	}

	array->size = size;

	return (0);
}

void
dynar_destroy(struct dynar *array)
{

	free(array->data);
	dynar_init(array, array->maximum_size);
}

void
dynar_clean(struct dynar *array)
{

	array->size = 0;
}

size_t
dynar_size(const struct dynar *array)
{

	return (array->size);
}

size_t
dynar_max_size(const struct dynar *array)
{

	return (array->maximum_size);
}

char *
dynar_data(const struct dynar *array)
{

	return (array->data);
}

static int
dynar_realloc(struct dynar *array, size_t new_array_size)
{
	char *new_data;

	if (new_array_size > array->maximum_size) {
		return (-1);
	}

	new_data = realloc(array->data, new_array_size);

	if (new_data == NULL) {
		return (-1);
	}

	array->allocated = new_array_size;
	array->data = new_data;

	return (0);
}

int
dynar_prealloc(struct dynar *array, size_t size)
{
	size_t new_size;

	if (array->size + size > array->maximum_size) {
		return (-1);
	}

	if (array->size + size > array->allocated) {
		new_size = (array->allocated + size) * 2;
		if (new_size > array->maximum_size) {
			new_size = array->maximum_size;
		}

		if (dynar_realloc(array, new_size) == -1) {
			return (-1);
		}
	}

	return (0);
}

int
dynar_cat(struct dynar *array, const void *src, size_t size)
{

	if (dynar_prealloc(array, size) != 0) {
		return (-1);
	}

	memmove(array->data + array->size, src, size);
	array->size += size;

	return (0);
}

int
dynar_prepend(struct dynar *array, const void *src, size_t size)
{

	if (dynar_prealloc(array, size) != 0) {
		return (-1);
	}

	memmove(array->data + size, array->data, array->size);
	memmove(array->data, src, size);
	array->size += size;

	return (0);
}
