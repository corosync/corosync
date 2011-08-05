
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

#include <corosync/corotypes.h>

#define hdb_error_to_cs(_result_) qb_to_cs_error(-_result_)

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define IPC_REQUEST_SIZE        1024*64
#define IPC_RESPONSE_SIZE       1024*64
#define IPC_DISPATCH_SIZE       1024*64
#else
#define IPC_REQUEST_SIZE        8192*128
#define IPC_RESPONSE_SIZE       8192*128
#define IPC_DISPATCH_SIZE       8192*128
#endif /* HAVE_SMALL_MEMORY_FOOTPRINT */

static inline const char * cs_strerror(cs_error_t err)
{
	switch (err) {
	case CS_OK:
		return "success";

	case CS_ERR_LIBRARY:
		return "CS_ERR_LIBRARY";

	case CS_ERR_VERSION:
		return "CS_ERR_VERSION";

	case CS_ERR_INIT:
		return "CS_ERR_INIT";

	case CS_ERR_NO_MEMORY:
		return "CS_ERR_NO_MEMORY";

	case CS_ERR_NAME_TOO_LONG :
		return "CS_ERR_NAME_TOO_LONG ";

	case CS_ERR_TIMEOUT:
		return "CS_ERR_TIMEOUT";

	case CS_ERR_TRY_AGAIN:
		return "CS_ERR_TRY_AGAIN";

	case CS_ERR_INVALID_PARAM:
		return "CS_ERR_INVALID_PARAM";

	case CS_ERR_BAD_HANDLE:
		return "CS_ERR_BAD_HANDLE";

	case CS_ERR_BUSY :
		return "CS_ERR_BUSY ";

	case CS_ERR_ACCESS :
		return "CS_ERR_ACCESS ";

	case CS_ERR_NOT_EXIST :
		return "CS_ERR_NOT_EXIST ";

	case CS_ERR_EXIST :
		return "CS_ERR_EXIST ";

	case CS_ERR_NO_SPACE :
		return "CS_ERR_NO_SPACE ";

	case CS_ERR_INTERRUPT :
		return "CS_ERR_INTERRUPT ";

	case CS_ERR_NAME_NOT_FOUND :
		return "CS_ERR_NAME_NOT_FOUND ";

	case CS_ERR_NO_RESOURCES :
		return "CS_ERR_NO_RESOURCES ";

	case CS_ERR_NOT_SUPPORTED :
		return "CS_ERR_NOT_SUPPORTED ";

	case CS_ERR_BAD_OPERATION :
		return "CS_ERR_BAD_OPERATION ";

	case CS_ERR_FAILED_OPERATION :
		return "CS_ERR_FAILED_OPERATION ";

	case CS_ERR_MESSAGE_ERROR :
		return "CS_ERR_MESSAGE_ERROR ";

	case CS_ERR_QUEUE_FULL :
		return "CS_ERR_QUEUE_FULL ";

	case CS_ERR_QUEUE_NOT_AVAILABLE :
		return "CS_ERR_QUEUE_NOT_AVAILABLE ";

	case CS_ERR_BAD_FLAGS :
		return "CS_ERR_BAD_FLAGS ";

	case CS_ERR_TOO_BIG :
		return "CS_ERR_TOO_BIG ";

	case CS_ERR_NO_SECTIONS :
		return "CS_ERR_NO_SECTIONS ";

	case CS_ERR_CONTEXT_NOT_FOUND :
		return "CS_ERR_CONTEXT_NOT_FOUND ";

	case CS_ERR_TOO_MANY_GROUPS :
		return "CS_ERR_TOO_MANY_GROUPS ";

	case CS_ERR_SECURITY :
		return "CS_ERR_SECURITY ";

	default:
		return "unknown error";
	}
}


#endif /* AIS_UTIL_H_DEFINED */
