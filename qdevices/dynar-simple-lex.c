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
#include <ctype.h>

#include "dynar-simple-lex.h"

void
dynar_simple_lex_init(struct dynar_simple_lex *lex, struct dynar *input)
{

	memset(lex, 0, sizeof(*lex));
	lex->input = input;
	dynar_init(&lex->token, dynar_max_size(input));
}

void
dynar_simple_lex_destroy(struct dynar_simple_lex *lex)
{

	dynar_destroy(&lex->token);
	memset(lex, 0, sizeof(*lex));
}

struct dynar *
dynar_simple_lex_token_next(struct dynar_simple_lex *lex)
{
	size_t pos;
	size_t size;
	char *str;
	char ch;

	dynar_clean(&lex->token);

	size = dynar_size(lex->input);
	str = dynar_data(lex->input);

	for (pos = lex->pos; pos < size && isspace(str[pos]) && str[pos] != '\n'; pos++) ;

	for (; pos < size && !isspace(str[pos]); pos++) {
		if (dynar_cat(&lex->token, &str[pos], sizeof(*str)) != 0) {
			return (NULL);
		}
	}

	ch = '\0';
	dynar_cat(&lex->token, &ch, sizeof(ch));

	lex->pos = pos;

	return (&lex->token);
}
