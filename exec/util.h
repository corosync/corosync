/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2004 Open Source Development Lab
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

/*
 * Get the time of day and convert to nanoseconds
 */
extern cs_time_t clust_time_now(void);

enum e_ais_done {
	AIS_DONE_EXIT = 0,
	AIS_DONE_UID_DETERMINE = 1,
	AIS_DONE_GID_DETERMINE = 2,
	AIS_DONE_MEMPOOL_INIT = 3,
	AIS_DONE_FORK = 4,
	AIS_DONE_LIBAIS_SOCKET = 5,
	AIS_DONE_LIBAIS_BIND = 6,
	AIS_DONE_READKEY = 7,
	AIS_DONE_MAINCONFIGREAD = 8,
	AIS_DONE_LOGSETUP = 9,
	AIS_DONE_AMFCONFIGREAD = 10,
	AIS_DONE_DYNAMICLOAD = 11,
	AIS_DONE_OBJDB = 12,
	AIS_DONE_INIT_SERVICES = 13,
	AIS_DONE_OUT_OF_MEMORY = 14,
	AIS_DONE_FATAL_ERR = 15,
	AIS_DONE_DIR_NOT_PRESENT = 16,
	AIS_DONE_AQUIRE_LOCK = 17,
	AIS_DONE_ALREADY_RUNNING = 18,
};

static inline cs_error_t hdb_error_to_cs (int res)		\
{								\
	if (res == 0) {						\
		return (CS_OK);					\
	} else {						\
		if (errno == EBADF) {				\
			return (CS_ERR_BAD_HANDLE);		\
		} else						\
		if (errno == ENOMEM) {				\
			return (CS_ERR_NO_MEMORY);		\
		} else						\
		if (errno == EMFILE) {				\
			return (CS_ERR_NO_RESOURCES);		\
		} else						\
		if (errno == EACCES) {				\
			return (CS_ERR_SECURITY);		\
		}						\
		return (CS_ERR_LIBRARY);			\
	}							\
}

/*
 * Compare two names.  returns non-zero on match.
 */
extern int name_match(cs_name_t *name1, cs_name_t *name2);
#define corosync_exit_error(err) _corosync_exit_error ((err), __FILE__, __LINE__)
extern void _corosync_exit_error (enum e_ais_done err, const char *file,
				  unsigned int line) __attribute__((noreturn));
void _corosync_out_of_memory_error (void) __attribute__((noreturn));
extern char *getcs_name_t (cs_name_t *name);
extern void setcs_name_t (cs_name_t *name, char *str);
extern int cs_name_tisEqual (cs_name_t *str1, char *str2);
#endif /* UTIL_H_DEFINED */
