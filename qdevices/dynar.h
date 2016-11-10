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

#ifndef _DYNAR_H_
#define _DYNAR_H_

#include <sys/types.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dynamic array structure
 */
struct dynar {
	char *data;
	size_t size;
	size_t allocated;
	size_t maximum_size;
};

extern void	 dynar_init(struct dynar *array, size_t maximum_size);

extern void	 dynar_destroy(struct dynar *array);

extern void	 dynar_clean(struct dynar *array);

extern size_t	 dynar_size(const struct dynar *array);

extern size_t	 dynar_max_size(const struct dynar *array);

extern void	 dynar_set_max_size(struct dynar *array, size_t maximum_size);

extern char	*dynar_data(const struct dynar *array);

extern int	 dynar_cat(struct dynar *array, const void *src, size_t size);

extern int	 dynar_prealloc(struct dynar *array, size_t size);

extern int	 dynar_prepend(struct dynar *array, const void *src, size_t size);

extern int	 dynar_set_size(struct dynar *array, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _DYNAR_H_ */
