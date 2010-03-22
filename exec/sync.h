/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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

#ifndef SYNC_H_DEFINED
#define SYNC_H_DEFINED

#include <netinet/in.h>
#include <corosync/totem/totempg.h>
#include "totemsrp.h"

union sync_init_api {
	void (*sync_init_v1) (
		const unsigned int *member_list,
		size_t member_list_entries,
		const struct memb_ring_id *ring_id);

	void (*sync_init_v2) (
		const unsigned int *trans_list,
		size_t trans_list_entries,
		const unsigned int *member_list,
		size_t member_list_entries,
		const struct memb_ring_id *ring_id);
};

struct sync_callbacks {
	int api_version;
	union sync_init_api sync_init_api;
	int (*sync_process) (void);
	void (*sync_activate) (void);
	void (*sync_abort) (void);
	const char *name;
};

int sync_register (
	int (*sync_callbacks_retrieve) (
		int sync_id,
		struct sync_callbacks *callbacks),

	void (*sync_started) (
		const struct memb_ring_id *ring_id),

	void (*sync_aborted) (void),

	void (*next_start) (
		const unsigned int *member_list,
		size_t member_list_entries,
		const struct memb_ring_id *ring_id));


#endif /* SYNC_H_DEFINED */
