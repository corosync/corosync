/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2004 Open Source Development Lab
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com), Mark Haverkamp (markh@osdl.org)
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>

#include "../include/saAis.h"
#include "../include/list.h"
#include "aispoll.h"
#include "util.h"
#define LOG_SERVICE LOG_SERVICE_MAIN
#include "print.h"

/*
 * Compare two names.  returns non-zero on match.
 */
int name_match(SaNameT *name1, SaNameT *name2) 
{
	if (name1->length == name2->length) {
		return ((strncmp ((char *)name1->value, (char *)name2->value,
			name1->length)) == 0);
	} 
	return 0;
}

/*
 * Get the time of day and convert to nanoseconds
 */
SaTimeT clust_time_now(void)
{
	struct timeval tv;
	SaTimeT time_now;

	if (gettimeofday(&tv, 0)) {
		return 0ULL;
	}

	time_now = (SaTimeT)(tv.tv_sec) * 1000000000ULL;
	time_now += (SaTimeT)(tv.tv_usec) * 1000ULL;

	return time_now;
}


void openais_exit_error (enum e_ais_done err)
{
	log_printf (LOG_LEVEL_ERROR, "AIS Executive exiting (%d).\n", err);
	exit (err);
}

char *getSaNameT (SaNameT *name)
{
	static char ret_name[300];

	memset (ret_name, 0, sizeof (ret_name));
	if (name->length > 299) {
		memcpy (ret_name, name->value, 299);
	} else {

		memcpy (ret_name, name->value, name->length);
	}
	return (ret_name);
}
