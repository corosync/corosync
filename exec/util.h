/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2004 Open Source Development Lab
 * Copyright (c) 2006-2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com), Mark Haverkamp (markh@osdl.org)
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
#ifndef UTIL_H_DEFINED
#define UTIL_H_DEFINED

#include <sys/time.h>
#include <corosync/corotypes.h>

/**
 * Get the time of day and convert to nanoseconds
 */
extern cs_time_t clust_time_now(void);

enum e_corosync_done {
	COROSYNC_DONE_EXIT = 0,
	COROSYNC_DONE_FORK = 4,
	COROSYNC_DONE_LOGCONFIGREAD = 7,
	COROSYNC_DONE_MAINCONFIGREAD = 8,
	COROSYNC_DONE_LOGSETUP = 9,
	COROSYNC_DONE_ICMAP = 12,
	COROSYNC_DONE_INIT_SERVICES = 13,
	COROSYNC_DONE_FATAL_ERR = 15,
	COROSYNC_DONE_DIR_NOT_PRESENT = 16,
	COROSYNC_DONE_AQUIRE_LOCK = 17,
	COROSYNC_DONE_ALREADY_RUNNING = 18,
	COROSYNC_DONE_STD_TO_NULL_REDIR = 19,
	COROSYNC_DONE_SERVICE_ENGINE_INIT = 20,
	COROSYNC_DONE_STORE_RINGID = 21,
	COROSYNC_DONE_PLOAD = 99
};


/**
 * Compare two names.  returns non-zero on match.
 */
extern int name_match(cs_name_t *name1, cs_name_t *name2);
#define corosync_exit_error(err) _corosync_exit_error ((err), __FILE__, __LINE__)
extern void _corosync_exit_error (enum e_corosync_done err, const char *file,
				  unsigned int line) __attribute__((noreturn));
void _corosync_out_of_memory_error (void) __attribute__((noreturn));
extern char *getcs_name_t (cs_name_t *name);
extern void setcs_name_t (cs_name_t *name, char *str);
extern int cs_name_tisEqual (cs_name_t *str1, char *str2);
/**
 * Get the short name of a service from the service_id.
 */
const char * short_service_name_get(uint32_t service_id,
				    char *buf, size_t buf_size);

/*
 * Return run directory (ether COROSYNC_RUN_DIR env or LOCALSTATEDIR/lib/corosync)
 */
const char *get_run_dir(void);

#endif /* UTIL_H_DEFINED */
