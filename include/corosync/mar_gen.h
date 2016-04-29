/*
 * Copyright (c) 2006-2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

#ifndef MAR_GEN_H_DEFINED
#define MAR_GEN_H_DEFINED

#include <stdint.h>
#include <string.h>

#include <corosync/corotypes.h>
#include <corosync/swab.h>

#define MAR_ALIGN_UP(addr,size) (((addr)+((size)-1))&(~((size)-1)))

typedef int8_t mar_int8_t;
typedef int16_t mar_int16_t;
typedef int32_t mar_int32_t;
typedef int64_t mar_int64_t;

typedef uint8_t mar_uint8_t;
typedef uint16_t mar_uint16_t;
typedef uint32_t mar_uint32_t;
typedef uint64_t mar_uint64_t;

/**
 * @brief swab_mar_int8_t
 * @param to_swab
 */
static inline void swab_mar_int8_t (mar_int8_t *to_swab)
{
	return;
}

/**
 * @brief swab_mar_int16_t
 * @param to_swab
 */
static inline void swab_mar_int16_t (mar_int16_t *to_swab)
{
	*to_swab = swab16 (*to_swab);
}

/**
 * @brief swab_mar_int32_t
 * @param to_swab
 */
static inline void swab_mar_int32_t (mar_int32_t *to_swab)
{
	*to_swab = swab32 (*to_swab);
}

/**
 * @brief swab_mar_int64_t
 * @param to_swab
 */
static inline void swab_mar_int64_t (mar_int64_t *to_swab)
{
	*to_swab = swab64 (*to_swab);
}

/**
 * @brief swab_mar_uint8_t
 * @param to_swab
 */
static inline void swab_mar_uint8_t (mar_uint8_t *to_swab)
{
	return;
}

/**
 * @brief swab_mar_uint16_t
 * @param to_swab
 */
static inline void swab_mar_uint16_t (mar_uint16_t *to_swab)
{
	*to_swab = swab16 (*to_swab);
}

/**
 * @brief swab_mar_uint32_t
 * @param to_swab
 */
static inline void swab_mar_uint32_t (mar_uint32_t *to_swab)
{
	*to_swab = swab32 (*to_swab);
}

/**
 * @brief swab_mar_uint64_t
 * @param to_swab
 */
static inline void swab_mar_uint64_t (mar_uint64_t *to_swab)
{
	*to_swab = swab64 (*to_swab);
}

/**
 * @brief swabbin
 * @param data
 * @param len
 */
static inline void swabbin(char *data, size_t len)
{
	int i;
	char tmp;

	for (i = 0; i < len / 2; i++) {
		tmp = data[i];
		data[i] = data[len - i - 1];
		data[len - i - 1] = tmp;
	}
}

/**
 * @brief swabflt
 * @param flt
 */
static inline void swabflt(float *flt)
{
	swabbin((char *)flt, sizeof(*flt));
}

/**
 * @brief swabdbl
 * @param dbl
 */
static inline void swabdbl(double *dbl)
{
	swabbin((char *)dbl, sizeof(*dbl));
}

/**
 * @brief mar_name_t struct
 */
typedef struct {
	mar_uint16_t length __attribute__((aligned(8)));
	mar_uint8_t value[CS_MAX_NAME_LENGTH] __attribute__((aligned(8)));
} mar_name_t;

/**
 * @brief get_mar_name_t
 * @param name
 * @return
 */
static inline const char *get_mar_name_t (const mar_name_t *name) {
        return ((const char *)name->value);
}

/**
 * @brief mar_name_match
 * @param name1
 * @param name2
 * @return
 */
static inline int mar_name_match(const mar_name_t *name1, const mar_name_t *name2)
{
        if (name1->length == name2->length) {
                return ((strncmp ((const char *)name1->value,
				  (const char *)name2->value,
                        name1->length)) == 0);
        }
        return 0;
}

/**
 * @brief swab_mar_name_t
 * @param to_swab
 */
static inline void swab_mar_name_t (mar_name_t *to_swab)
{
	swab_mar_uint16_t (&to_swab->length);
}

/**
 * @brief marshall_from_mar_name_t
 * @param dest
 * @param src
 */
static inline void marshall_from_mar_name_t (
	cs_name_t *dest,
	const mar_name_t *src)
{
	dest->length = src->length;
	memcpy (dest->value, src->value, CS_MAX_NAME_LENGTH);
}

/**
 * @brief marshall_to_mar_name_t
 * @param dest
 * @param src
 */
static inline void marshall_to_mar_name_t (
	mar_name_t *dest,
	const cs_name_t *src)
{
	dest->length = src->length;
	memcpy (dest->value, src->value, CS_MAX_NAME_LENGTH);
}

/**
 * @brief mar_bool_t enum
 */
typedef enum {
	MAR_FALSE = 0,
	MAR_TRUE = 1
} mar_bool_t;

/**
 * @brief mar_time_t
 */
typedef mar_uint64_t mar_time_t;

/**
 * @brief swab_mar_time_t
 * @param to_swab
 */
static inline void swab_mar_time_t (mar_time_t *to_swab)
{
	swab_mar_uint64_t (to_swab);
}

#define MAR_TIME_END ((int64_t)0x7fffffffffffffffull)
#define MAR_TIME_BEGIN            0x0ULL
#define MAR_TIME_UNKNOWN          0x8000000000000000ULL

#define MAR_TIME_ONE_MICROSECOND 1000ULL
#define MAR_TIME_ONE_MILLISECOND 1000000ULL
#define MAR_TIME_ONE_SECOND      1000000000ULL
#define MAR_TIME_ONE_MINUTE      60000000000ULL
#define MAR_TIME_ONE_HOUR        3600000000000ULL
#define MAR_TIME_ONE_DAY         86400000000000ULL
#define MAR_TIME_MAX             CS_TIME_END

#define MAR_TRACK_CURRENT 0x01
#define MAR_TRACK_CHANGES 0x02
#define MAR_TRACK_CHANGES_ONLY 0x04

/**
 * @brief mar_invocation_t
 */
typedef mar_uint64_t mar_invocation_t;

/**
 * @brief swab_mar_invocation_t
 * @param to_swab
 */
static inline void swab_mar_invocation_t (mar_invocation_t *to_swab)
{
	swab_mar_uint64_t (to_swab);
}

/**
 * @brief mar_size_t
 */
typedef mar_uint64_t mar_size_t;

/**
 * @brief swab_mar_size_t
 * @param to_swab
 */
static inline void swab_mar_size_t (mar_size_t *to_swab)
{
	swab_mar_uint64_t (to_swab);
}

/**
 * @brief swab_coroipc_request_header_t
 * @param to_swab
 */
static inline void swab_coroipc_request_header_t (struct qb_ipc_request_header *to_swab)
{
	swab_mar_int32_t (&to_swab->size);
	swab_mar_int32_t (&to_swab->id);
}

#endif /* MAR_GEN_H_DEFINED */
