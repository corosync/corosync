/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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

#include <sys/time.h>
#include "../include/list.h"

#ifndef TLIST_H_DEFINED
#define TLIST_H_DEFINED

typedef void * timer_handle;

struct timerlist {
	struct list_head timer_head;
	struct list_head *timer_iter;
};

void timerlist_init (struct timerlist *timerlist);

void timerlist_free (struct timerlist *timerlist);

timer_handle timerlist_add_future (struct timerlist *timerlist,
	void (*timer_fn) (void *data),
	void *data,
	unsigned int msec_in_future,
    timer_handle *handle);

void timerlist_del (struct timerlist *timerlist, timer_handle timer_handle);

void timerlist_expire (struct timerlist *timerlist);

unsigned int timerlist_timeout_msec (struct timerlist *timerlist);

#endif /* TLIST_H_DEFINED */
