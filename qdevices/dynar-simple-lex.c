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

#include "dynar-simple-lex.h"

/*
 * Simple_lex is going to be used in protocol and it's not good idea to depend on locale
 */
static int
dynar_simple_lex_is_space(char ch)
{
	return (ch == ' ' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\v');
}

void
dynar_simple_lex_init(struct dynar_simple_lex *lex, struct dynar *input,
    enum dynar_simple_lex_type lex_type)
{

	memset(lex, 0, sizeof(*lex));
	lex->input = input;
	lex->lex_type = lex_type;
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
	char ch, ch2;
	int add_char;
	int state;

	dynar_clean(&lex->token);

	size = dynar_size(lex->input);
	str = dynar_data(lex->input);

	state = 1;
	pos = lex->pos;

	while (state != 0) {
		if (pos < size) {
			ch = str[pos];
		} else {
			ch = '\0';
		}

		add_char = 0;

		switch (state) {
		case 1:
			/*
			 * Skip spaces. Newline is special and means end of processing
			 */
			if (pos >= size || ch == '\n' || ch == '\r') {
				state = 0;
			} else if (dynar_simple_lex_is_space(ch)) {
				pos++;
			} else {
				state = 2;
			}
			break;
		case 2:
			/*
			 * Read word
			 */
			if (pos >= size) {
				state = 0;
			} else if ((lex->lex_type == DYNAR_SIMPLE_LEX_TYPE_BACKSLASH ||
			    lex->lex_type == DYNAR_SIMPLE_LEX_TYPE_QUOTE) && ch == '\\') {
				pos++;
				state = 3;
			} else if (lex->lex_type == DYNAR_SIMPLE_LEX_TYPE_QUOTE &&
			    ch == '"') {
				pos++;
				state = 4;
			} else if (dynar_simple_lex_is_space(ch)) {
				state = 0;
			} else {
				pos++;
				add_char = 1;
			}
			break;
		case 3:
			/*
			 * Process backslash
			 */
			if (pos >= size || ch == '\n' || ch == '\r') {
				/*
				 * End of string. Do not include backslash (it's just ignored)
				 */
				state = 0;
			} else {
				add_char = 1;
				state = 2;
				pos++;
			}
			break;
		case 4:
			/*
			 * Quote word
			 */
			if (pos >= size) {
				state = 0;
			} else if (ch == '\\') {
				state = 5;
				pos++;
			} else if (ch == '"') {
				state = 2;
				pos++;
			} else if (ch == '\n' || ch == '\r') {
				state = 0;
			} else {
				pos++;
				add_char = 1;
			}
			break;
		case 5:
			/*
			 * Quote word backslash
			 */
			if (pos >= size || ch == '\n' || ch == '\r') {
				/*
				 * End of string. Do not include backslash (it's just ignored)
				 */
				state = 0;
			} else if (ch == '\\' || ch == '"') {
				add_char = 1;
				state = 4;
				pos++;
			} else {
				ch2 = '\\';
				if (dynar_cat(&lex->token, &ch2, sizeof(ch2)) != 0) {
					return (NULL);
				}

				add_char = 1;
				state = 4;
				pos++;
			}
			break;
		}

		if (add_char) {
			if (dynar_cat(&lex->token, &ch, sizeof(ch)) != 0) {
				return (NULL);
			}
		}
	}

	ch = '\0';
	if (dynar_cat(&lex->token, &ch, sizeof(ch)) != 0) {
		return (NULL);
	}

	lex->pos = pos;

	return (&lex->token);
}
