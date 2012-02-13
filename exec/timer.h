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

#ifndef TIMER_H_DEFINED
#define TIMER_H_DEFINED

#include <stdint.h>
#include <time.h>
#include <qb/qbloop.h>

#ifndef TIMER_HANDLE_T
typedef qb_loop_timer_handle corosync_timer_handle_t;
#define TIMER_HANDLE_T 1
#endif

extern int corosync_timer_add_duration (
	unsigned long long nanosec_duration,
	void *data,
	void (*timer_fn) (void *data),
	corosync_timer_handle_t *handle);

extern int corosync_timer_add_absolute (
	unsigned long long nanoseconds_from_epoch,
	void *data,
	void (*timer_fn) (void *data),
	corosync_timer_handle_t *handle);

extern void corosync_timer_delete (corosync_timer_handle_t handle);

extern unsigned long long corosync_timer_expire_time_get (corosync_timer_handle_t handle);

extern unsigned long long cs_timer_time_get (void);

#endif /* TIMER_H_DEFINED */
