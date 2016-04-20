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

#ifndef _DYNAR_SIMPLE_LEX_H_
#define _DYNAR_SIMPLE_LEX_H_

#include "dynar.h"

#ifdef __cplusplus
extern "C" {
#endif

enum dynar_simple_lex_type {
	DYNAR_SIMPLE_LEX_TYPE_PLAIN,
	DYNAR_SIMPLE_LEX_TYPE_BACKSLASH,
	DYNAR_SIMPLE_LEX_TYPE_QUOTE,
};

struct dynar_simple_lex {
	struct dynar token;
	struct dynar *input;
	enum dynar_simple_lex_type lex_type;
	size_t pos;
};

extern void	 	 dynar_simple_lex_init(struct dynar_simple_lex *lex, struct dynar *input,
    enum dynar_simple_lex_type lex_type);

extern void	 	 dynar_simple_lex_destroy(struct dynar_simple_lex *lex);

extern struct dynar	*dynar_simple_lex_token_next(struct dynar_simple_lex *lex);

#ifdef __cplusplus
}
#endif

#endif /* _DYNAR_SIMPLE_LEX_H_ */
