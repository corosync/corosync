/*
 * Copyright (c) 2008-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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

#ifndef QUORUM_H_DEFINED
#define QUORUM_H_DEFINED

struct memb_ring_id;

typedef void (*quorum_callback_fn_t) (int quorate, void *context);

typedef void (*quorum_set_quorate_fn_t) (const unsigned int *view_list,
                                         size_t view_list_entries,
                                         int quorate, struct memb_ring_id *);

struct quorum_callin_functions
{
	int (*quorate) (void);
	int (*register_callback) (quorum_callback_fn_t, void*);
	int (*unregister_callback) (quorum_callback_fn_t, void*);
};

extern int corosync_quorum_is_quorate (void);

extern int corosync_quorum_register_callback (quorum_callback_fn_t fn, void *context);

extern int corosync_quorum_unregister_callback (quorum_callback_fn_t fn, void *context);

extern int corosync_quorum_initialize (struct quorum_callin_functions *fns);


extern int quorum_none(void);


#endif /* QUORUM_H_DEFINED */
