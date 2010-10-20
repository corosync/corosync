
/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
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

#ifndef AIS_UTIL_H_DEFINED
#define AIS_UTIL_H_DEFINED

#include <errno.h>

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

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define IPC_REQUEST_SIZE        1024*64
#define IPC_RESPONSE_SIZE       1024*64
#define IPC_DISPATCH_SIZE       1024*64
#else
#define IPC_REQUEST_SIZE        8192*128
#define IPC_RESPONSE_SIZE       8192*128
#define IPC_DISPATCH_SIZE       8192*128
#endif /* HAVE_SMALL_MEMORY_FOOTPRINT */

#endif /* AIS_UTIL_H_DEFINED */
