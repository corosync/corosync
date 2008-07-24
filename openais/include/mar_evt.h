/*
 * Copyright (c) 2004-2005 Mark Haverkamp
 * Copyright (c) 2004-2005 Open Source Development Lab
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
 * - Neither the name of the Open Source Developement Lab nor the names of its
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
#ifndef MAR_EVT_H_DEFINED
#define MAR_EVT_H_DEFINED

#include <corosync/mar_gen.h>

typedef mar_uint64_t mar_evtchannelhandle_t;

typedef mar_uint64_t mar_evtsubscriptionid_t;

typedef mar_uint8_t mar_evteventpriority_t;

typedef mar_uint64_t mar_evteventid_t;

typedef mar_uint8_t mar_evtchannelopenflags_t;

typedef struct {
	mar_size_t allocated_size __attribute__((aligned(8)));
	mar_size_t pattern_size __attribute__((aligned(8)));
	mar_uint8_t *pattern __attribute__((aligned(8)));
} mar_evt_event_pattern_t;


typedef struct {
	mar_size_t allocated_number __attribute__((aligned(8)));
	mar_size_t patterns_number __attribute__((aligned(8)));
	mar_evt_event_pattern_t *patterns __attribute__((aligned(8)));
} mar_evt_event_pattern_array_t;

typedef enum {
	MAR_EVT_PREFIX_FILTER = 1,
	MAR_EVT_SUFFIX_FILTER = 2,
	MAR_EVT_EXACT_FILTER = 3,
	MAR_EVT_PASS_ALL_FILTER = 4
} mar_evt_event_filter_type_t;

typedef struct {
	mar_evt_event_filter_type_t filter_type __attribute__((aligned(8)));
	mar_evt_event_pattern_t filter __attribute__((aligned(8)));
} mar_evt_event_filter_t;

typedef struct {
	mar_size_t filters_number __attribute__((aligned(8)));
	mar_evt_event_filter_t *filters __attribute__((aligned(8)));
} mar_evt_event_filter_array_t;

#endif /* MAR_EVT_H_DEFINED */
