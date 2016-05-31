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
#include "dynar-getopt-lex.h"

int
main(void)
{
	struct dynar input_str;
	struct dynar_getopt_lex lex;

	dynar_init(&input_str, 128);

	assert(dynar_str_catf(&input_str, "option=value") != -1);
	dynar_getopt_lex_init(&lex, &input_str);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option") == 0);
	assert(strcmp(dynar_data(&lex.value), "value") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "") == 0);
	assert(strcmp(dynar_data(&lex.value), "") == 0);
	dynar_getopt_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "option1=value1,option2=value2") != -1);
	dynar_getopt_lex_init(&lex, &input_str);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option1") == 0);
	assert(strcmp(dynar_data(&lex.value), "value1") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option2") == 0);
	assert(strcmp(dynar_data(&lex.value), "value2") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "") == 0);
	assert(strcmp(dynar_data(&lex.value), "") == 0);
	dynar_getopt_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "option1=value1,option2=value2,option3=value3") != -1);
	dynar_getopt_lex_init(&lex, &input_str);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option1") == 0);
	assert(strcmp(dynar_data(&lex.value), "value1") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option2") == 0);
	assert(strcmp(dynar_data(&lex.value), "value2") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option3") == 0);
	assert(strcmp(dynar_data(&lex.value), "value3") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "") == 0);
	assert(strcmp(dynar_data(&lex.value), "") == 0);
	dynar_getopt_lex_destroy(&lex);

	assert(dynar_str_cpy(&input_str, "") == 0);
	assert(dynar_str_catf(&input_str, "option1,option2=value2") != -1);
	dynar_getopt_lex_init(&lex, &input_str);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option1") == 0);
	assert(strcmp(dynar_data(&lex.value), "") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "option2") == 0);
	assert(strcmp(dynar_data(&lex.value), "value2") == 0);
	assert(dynar_getopt_lex_token_next(&lex) == 0);
	assert(strcmp(dynar_data(&lex.option), "") == 0);
	assert(strcmp(dynar_data(&lex.value), "") == 0);
	dynar_getopt_lex_destroy(&lex);

	dynar_destroy(&input_str);

	return (0);
}
