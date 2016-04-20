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
#include "dynar-simple-lex.h"

int
main(void)
{
	struct dynar input_str;
	struct dynar_simple_lex lex;
	struct dynar *output_str_ptr;
	struct dynar output_str;
	const char *cstr;

	dynar_init(&input_str, 128);

	assert(dynar_str_catf(&input_str, "token1 token2") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_PLAIN);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "token1") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "token2") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "	 token1			   token2		") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_PLAIN);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "token1") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "token2") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "      token1		 	token2	 	\ntoken3") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_PLAIN);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "token1") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "token2") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "\\ab\\cd e\\fg\\ h i\\") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_PLAIN);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "\\ab\\cd") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "e\\fg\\") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "h") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "i\\") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, " a b\rc") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_PLAIN);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "a") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "b") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "\\ab\\\\cd e\\fg\\ h i\\") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_BACKSLASH);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "ab\\cd") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "efg h") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "i") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "ab\\\\cd e\\fg\\ h ij\\\na") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_BACKSLASH);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "ab\\cd") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "efg h") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "ij") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, " a b\\\rc") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_BACKSLASH);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "a") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "b") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "abc def \"ghi\" jkl \"m n	o\"") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_QUOTE);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "abc") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "def") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "ghi") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "jkl") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "m n	o") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "a\\bc \"d\\e \\\"f\\\\  \\\"  \"g hij") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_QUOTE);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "abc") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "d\\e \"f\\  \"  g") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "hij") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "abc \"d e  \r\n") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_QUOTE);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "abc") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "d e  ") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "abc \"d e  \\\"\\\r\n") != -1);
	dynar_simple_lex_init(&lex, &input_str, DYNAR_SIMPLE_LEX_TYPE_QUOTE);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "abc") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "d e  \"") == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	dynar_init(&output_str, 128);
	assert(dynar_str_quote_cpy(&output_str, "abcd") == 0);
	assert(dynar_size(&output_str) == 6);
	assert(memcmp(dynar_data(&output_str), "\"abcd\"", dynar_size(&output_str)) == 0);
	assert(dynar_str_cat(&output_str, " ") == 0);
	assert(dynar_str_quote_cat(&output_str, "abcd") == 0);
	assert(dynar_size(&output_str) == 13);
	assert(memcmp(dynar_data(&output_str), "\"abcd\" \"abcd\"", dynar_size(&output_str)) == 0);

	assert(dynar_str_quote_cpy(&output_str, "ab\\cd") == 0);
	assert(dynar_size(&output_str) == 8);
	assert(memcmp(dynar_data(&output_str), "\"ab\\\\cd\"", dynar_size(&output_str)) == 0);

	assert(dynar_str_quote_cpy(&output_str, "ab\\\\cd") == 0);
	assert(dynar_size(&output_str) == 10);
	assert(memcmp(dynar_data(&output_str), "\"ab\\\\\\\\cd\"", dynar_size(&output_str)) == 0);

	assert(dynar_str_quote_cpy(&output_str, "ab cd \\\"e") == 0);
	assert(dynar_size(&output_str) == 13);
	assert(memcmp(dynar_data(&output_str), "\"ab cd \\\\\\\"e\"", dynar_size(&output_str)) == 0);

	cstr = "ab cd \\ ef\\g h\"i";
	assert(dynar_str_quote_cpy(&output_str, cstr) == 0);
	dynar_simple_lex_init(&lex, &output_str, DYNAR_SIMPLE_LEX_TYPE_QUOTE);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strlen(cstr) == dynar_size(output_str_ptr) - 1);
	assert(memcmp(cstr, dynar_data(output_str_ptr), strlen(cstr)) == 0);
	assert((output_str_ptr = dynar_simple_lex_token_next(&lex)) != NULL);
	assert(strcmp(dynar_data(output_str_ptr), "") == 0);
	dynar_simple_lex_destroy(&lex);

	dynar_destroy(&input_str);
	dynar_destroy(&output_str);

	return (0);
}
