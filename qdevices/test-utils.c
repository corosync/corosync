/*
 * Copyright (c) 2015-2019 Red Hat, Inc.
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
#include <errno.h>

#include "utils.h"

int
main(void)
{
	long long int ll;
	long long int lli;
	char buf[32];

	assert(utils_strtonum("0", 0, 100, &ll) == 0);
	assert(ll == 0);

	assert(utils_strtonum("100", 0, 100, &ll) == 0);
	assert(ll == 100);

	assert(utils_strtonum("101", 0, 100, &ll) != 0);
	assert(utils_strtonum("0", 1, 100, &ll) != 0);

	errno = ERANGE;
	assert(utils_strtonum("10", 0, 100, &ll) == 0);
	assert(ll == 10);

	assert(utils_strtonum("-1", -1, 0, &ll) == 0);
	assert(ll == -1);

	assert(utils_strtonum("-10", -20, -10, &ll) == 0);
	assert(ll == -10);

	assert(utils_strtonum("0", 1, 0, &ll) == -1);

	for (lli = -100; lli <= 100; lli++) {
		assert(snprintf(buf, sizeof(buf), "%lld", lli) > 0);

		assert(utils_strtonum(buf, -100, 100, &ll) == 0);
		assert(ll == lli);
	}

	assert(utils_strtonum("test", -1000, 1000, &ll) == -1);
	assert(utils_strtonum("12a", -1000, 1000, &ll) == -1);

	assert(utils_parse_bool_str("on") == 1);
	assert(utils_parse_bool_str("yes") == 1);
	assert(utils_parse_bool_str("1") == 1);
	assert(utils_parse_bool_str("ON") == 1);
	assert(utils_parse_bool_str("YeS") == 1);

	assert(utils_parse_bool_str("off") == 0);
	assert(utils_parse_bool_str("no") == 0);
	assert(utils_parse_bool_str("0") == 0);
	assert(utils_parse_bool_str("oFf") == 0);

	assert(utils_parse_bool_str("of") == -1);
	assert(utils_parse_bool_str("noo") == -1);
	assert(utils_parse_bool_str("01") == -1);

	return (0);
}
