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

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "dynar.h"
#include "dynar-str.h"

int
main(void)
{
	struct dynar str;

	dynar_init(&str, 3);
	assert(dynar_cat(&str, "a", 1) == 0);
	assert(dynar_size(&str) == 1);
	assert(dynar_cat(&str, "b", 1) == 0);
	assert(dynar_size(&str) == 2);
	assert(dynar_cat(&str, "c", 1) == 0);
	assert(dynar_size(&str) == 3);
	assert(dynar_cat(&str, "d", 1) != 0);
	assert(memcmp(dynar_data(&str), "abc", 3) == 0);

	dynar_clean(&str);
	assert(dynar_size(&str) == 0);
	dynar_set_max_size(&str, 4);
	assert(dynar_cat(&str, "a", 1) == 0);
	assert(dynar_size(&str) == 1);
	assert(memcmp(dynar_data(&str), "a", 1) == 0);
	assert(dynar_cat(&str, "b", 1) == 0);
	assert(dynar_size(&str) == 2);
	assert(dynar_cat(&str, "c", 1) == 0);
	assert(dynar_size(&str) == 3);
	assert(dynar_cat(&str, "d", 1) == 0);
	assert(dynar_size(&str) == 4);
	assert(memcmp(dynar_data(&str), "abcd", 4) == 0);

	assert(dynar_str_cpy(&str, "e") == 0);
	assert(dynar_size(&str) == 1);
	assert(memcmp(dynar_data(&str), "e", 1) == 0);

	assert(dynar_str_cpy(&str, "fgh") == 0);
	assert(dynar_size(&str) == 3);
	assert(memcmp(dynar_data(&str), "fgh", 3) == 0);

	assert(dynar_str_cpy(&str, "fghi") == 0);
	assert(dynar_size(&str) == 4);
	assert(memcmp(dynar_data(&str), "fghi", 4) == 0);

	assert(dynar_str_cpy(&str, "fghij") != 0);
	assert(dynar_size(&str) == 4);
	assert(dynar_str_cat(&str, "a") != 0);

	assert(dynar_str_cpy(&str, "") == 0);
	assert(dynar_size(&str) == 0);

	assert(dynar_str_cat(&str, "a") == 0);
	assert(dynar_size(&str) == 1);
	assert(memcmp(dynar_data(&str), "a", 1) == 0);

	assert(dynar_str_cat(&str, "b") == 0);
	assert(dynar_size(&str) == 2);
	assert(memcmp(dynar_data(&str), "ab", 2) == 0);

	assert(dynar_str_cat(&str, "cd") == 0);
	assert(dynar_size(&str) == 4);
	assert(memcmp(dynar_data(&str), "abcb", 1) == 0);

	assert(dynar_str_cpy(&str, "") == 0);
	assert(dynar_str_catf(&str, "%s", "a") == 1);
	assert(memcmp(dynar_data(&str), "a", 1) == 0);
	assert(dynar_str_catf(&str, "%s", "ab") == 2);
	assert(memcmp(dynar_data(&str), "aab", 3) == 0);
	assert(dynar_str_cpy(&str, "") == 0);
	assert(dynar_str_catf(&str, "%s", "abc") == 3);
	assert(dynar_str_cpy(&str, "") == 0);
	assert(dynar_str_catf(&str, "%s", "abcd") == -1);
	assert(dynar_str_cpy(&str, "a") == 0);
	assert(dynar_str_catf(&str, "%s", "") == 0);
	assert(memcmp(dynar_data(&str), "a", 1) == 0);

	dynar_destroy(&str);
	dynar_init(&str, 5);
	assert(dynar_str_catf(&str, "%s", "abcd") == 4);
	dynar_destroy(&str);
	dynar_init(&str, 5);
	assert(dynar_str_catf(&str, "%s", "") == 0);
	assert(dynar_str_catf(&str, "%s", "abc") == 3);
	assert(dynar_str_catf(&str, "%s", "d") == 1);
	assert(memcmp(dynar_data(&str), "abcd", 4) == 0);
	dynar_destroy(&str);

	dynar_init(&str, 10);
	assert(dynar_str_cat(&str, "abcd") == 0);
	assert(memcmp(dynar_data(&str), "abcd", 4) == 0);
	assert(dynar_str_prepend(&str, "e") == 0);
	assert(dynar_size(&str) == 5);
	assert(memcmp(dynar_data(&str), "eabcd", 5) == 0);
	assert(dynar_str_prepend(&str, "fgh") == 0);
	assert(dynar_size(&str) == 8);
	assert(memcmp(dynar_data(&str), "fgheabcd", 8) == 0);
	assert(dynar_str_prepend(&str, "ijk") != 0);
	assert(dynar_size(&str) == 8);
	assert(dynar_str_prepend(&str, "ij") == 0);
	assert(dynar_size(&str) == 10);
	assert(memcmp(dynar_data(&str), "ijfgheabcd", 10) == 0);
	dynar_destroy(&str);

	dynar_init(&str, 10);
	assert(dynar_str_cat(&str, "abcd") == 0);
	assert(memcmp(dynar_data(&str), "abcd", 4) == 0);
	assert(dynar_str_prepend(&str, "ef") == 0);
	assert(dynar_size(&str) == 6);
	assert(memcmp(dynar_data(&str), "efabcd", 6) == 0);
	assert(dynar_str_cat(&str, "ij") == 0);
	assert(dynar_size(&str) == 8);
	assert(memcmp(dynar_data(&str), "efabcdij", 8) == 0);
	assert(dynar_str_prepend(&str, "k") == 0);
	assert(dynar_size(&str) == 9);
	assert(memcmp(dynar_data(&str), "kefabcdij", 9) == 0);
	assert(dynar_str_cat(&str, "l") == 0);
	assert(dynar_size(&str) == 10);
	assert(memcmp(dynar_data(&str), "kefabcdijl", 10) == 0);
	dynar_destroy(&str);

	return (0);
}
