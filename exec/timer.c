/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2010 Red Hat, Inc.
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

#include <config.h>

#include "timer.h"
#include "main.h"
#include <qb/qbdefs.h>
#include <qb/qbutil.h>

int corosync_timer_add_absolute (
		unsigned long long nanosec_from_epoch,
		void *data,
		void (*timer_fn) (void *data),
		corosync_timer_handle_t *handle)
{
	uint64_t expire_time = nanosec_from_epoch - qb_util_nano_current_get();
	return qb_loop_timer_add(cs_poll_handle_get(),
				QB_LOOP_MED,
				 expire_time,
				 data,
				 timer_fn,
				 handle);
}

int corosync_timer_add_duration (
	unsigned long long nanosec_duration,
	void *data,
	void (*timer_fn) (void *data),
	corosync_timer_handle_t *handle)
{
	return qb_loop_timer_add(cs_poll_handle_get(),
				QB_LOOP_MED,
				 nanosec_duration,
				 data,
				 timer_fn,
				 handle);
}

void corosync_timer_delete (
	corosync_timer_handle_t th)
{
	qb_loop_timer_del(cs_poll_handle_get(), th);
}

unsigned long long corosync_timer_expire_time_get (
	corosync_timer_handle_t th)
{
	uint64_t expire;

	if (th == 0) {
		return (0);
	}

	expire = qb_loop_timer_expire_time_get(cs_poll_handle_get(), th);

	return (expire);
}

unsigned long long cs_timer_time_get (void)
{
	return qb_util_nano_from_epoch_get();
}

