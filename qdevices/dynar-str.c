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

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "dynar-str.h"

int
dynar_str_cpy(struct dynar *dest, const char *str)
{

	if (strlen(str) > dynar_max_size(dest)) {
		return (-1);
	}

	dynar_clean(dest);

	return (dynar_str_cat(dest, str));
}

int
dynar_str_cat(struct dynar *dest, const char *str)
{

	return (dynar_cat(dest, str, strlen(str)));
}

int
dynar_str_prepend(struct dynar *dest, const char *str)
{

	return (dynar_prepend(dest, str, strlen(str)));
}

int
dynar_str_vcatf(struct dynar *dest, const char *format, va_list ap)
{
	int to_write;
	int written;
	va_list ap_copy;
	size_t allocated;
	char buf;
	char *p;

	/*
	 * Find out how much bytes is needed
	 */
	va_copy(ap_copy, ap);
	to_write = vsnprintf(&buf, sizeof(buf), format, ap_copy);
	va_end(ap_copy);

	if (to_write < 0) {
		return (-1);
	}

	if ((size_t)to_write < sizeof(buf)) {
		/*
		 * Writing 1 byte string (snprintf writes also '\0') means string is empty
		 */

		return (0);
	}

	allocated = to_write + 1;
	if (dynar_prealloc(dest, allocated) != 0) {
		return (-1);
	}

	p = dynar_data(dest) + dynar_size(dest);

	va_copy(ap_copy, ap);
	written = vsnprintf(p, allocated, format, ap_copy);
	va_end(ap_copy);

	if (written < 0) {
		return (-1);
	}

	if ((size_t)written >= allocated) {
		return (-1);
	}

	dest->size += written;

	return (written);
}

int
dynar_str_catf(struct dynar *dest, const char *format, ...)
{
	va_list ap;
	int res;

	va_start(ap, format);
	res = dynar_str_vcatf(dest, format, ap);
	va_end(ap);

	return (res);
}

int
dynar_str_quote_cat(struct dynar *dest, const char *str)
{
	size_t zi;

	if (dynar_str_cat(dest, "\"") != 0) {
		return (-1);
	}

	for (zi = 0; zi < strlen(str); zi++) {
		if (str[zi] == '"' || str[zi] == '\\') {
			if (dynar_str_cat(dest, "\\") != 0) {
				return (-1);
			}
		}

		if (dynar_cat(dest, &str[zi], sizeof(str[zi])) != 0) {
			return (-1);
		}
	}

	if (dynar_str_cat(dest, "\"") != 0) {
		return (-1);
	}

	return (0);
}

int
dynar_str_quote_cpy(struct dynar *dest, const char *str)
{

	dynar_clean(dest);

	return (dynar_str_quote_cat(dest, str));
}
