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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>

#include <corosync/corotypes.h>
#include <corosync/swab.h>
#include <corosync/totem/totempg.h>
#include <corosync/totem/totem.h>
#include <corosync/logsys.h>

#include "quorum.h"
#include "main.h"
#include "vsf.h"

LOGSYS_DECLARE_SUBSYS ("QUORUM");

static struct quorum_callin_functions *corosync_quorum_fns = NULL;

int corosync_quorum_is_quorate (void)
{
	if (corosync_quorum_fns) {
		return corosync_quorum_fns->quorate();
	}
	else {
		return 1;
	}
}

int corosync_quorum_register_callback (quorum_callback_fn_t fn, void *context)
{
	if (corosync_quorum_fns) {
		return corosync_quorum_fns->register_callback(fn, context);
	}
	else {
		return 0;
	}
}

int corosync_quorum_unregister_callback (quorum_callback_fn_t fn, void *context)
{
	if (corosync_quorum_fns) {
		return corosync_quorum_fns->unregister_callback(fn, context);
	}
	else {
		return 0;
	}
}

int corosync_quorum_initialize (struct quorum_callin_functions *fns)
{
	if (corosync_quorum_fns)
		return -1;

	corosync_quorum_fns = fns;
	return 0;
}

int quorum_none(void)
{
	if (corosync_quorum_fns)
		return 0;
	else
		return 1;
}
