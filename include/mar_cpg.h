/*
 * Copyright (c) 2006-2008 Red Hat, Inc.
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
 *
 * All rights reserved.
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
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
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
#ifndef MAR_CPG_H_DEFINED
#define MAR_CPG_H_DEFINED

#include "cpg.h"
#include "swab.h"

typedef struct {
	uint32_t length __attribute__((aligned(8)));
	char value[CPG_MAX_NAME_LENGTH] __attribute__((aligned(8)));
} mar_cpg_name_t;

static inline void swab_mar_cpg_name_t (mar_cpg_name_t *to_swab)
{
	swab_mar_uint32_t (&to_swab->length);
}

static inline void marshall_from_mar_cpg_name_t (
	struct cpg_name *dest,
	mar_cpg_name_t *src)
{
	dest->length = src->length;
	memcpy (&dest->value, &src->value, CPG_MAX_NAME_LENGTH);
}

static inline void marshall_to_mar_cpg_name_t (
	mar_cpg_name_t *dest,
	struct cpg_name *src)
{
	dest->length = src->length;
	memcpy (&dest->value, &src->value, CPG_MAX_NAME_LENGTH);
}
		
typedef struct {
        mar_uint32_t nodeid __attribute__((aligned(8)));
        mar_uint32_t pid __attribute__((aligned(8)));
        mar_uint32_t reason __attribute__((aligned(8)));
} mar_cpg_address_t;

static inline void marshall_from_mar_cpg_address_t (
	struct cpg_address *dest,
	mar_cpg_address_t *src)
{
	dest->nodeid = src->nodeid;
	dest->pid = src->pid;
	dest->reason = src->reason;
}

static inline void marshall_to_mar_cpg_address_t (
	mar_cpg_address_t *dest,
	struct cpg_address *src)
{
	dest->nodeid = src->nodeid;
	dest->pid = src->pid;
	dest->reason = src->reason;
}

#endif /* MAR_CPG_H_DEFINED */
