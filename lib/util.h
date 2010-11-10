
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

#include <qb/qbdefs.h>
#include <errno.h>

#define hdb_error_to_cs errno_to_cs
static inline cs_error_t errno_to_cs (int result)
{
	int32_t res;
	cs_error_t err = CS_ERR_LIBRARY;

	if (result >= 0) {
		return CS_OK;
	}
	res = -result;

	switch (res) {
	case 0:
		err = CS_OK;
		break;
	case EBADF:
		err = CS_ERR_BAD_HANDLE;
		break;
	case ENOMEM:
		err = CS_ERR_NO_MEMORY;
		break;
	case EAGAIN:
		err = CS_ERR_TRY_AGAIN;
		break;
	case EBADE:
		err = CS_ERR_FAILED_OPERATION;
		//err = CS_ERR_BAD_OPERATION;
		//err = CS_ERR_LIBRARY;
		break;
	case ETIME:
		err = CS_ERR_TIMEOUT;
		break;
	case EINVAL:
		err = CS_ERR_INVALID_PARAM;
		//err = CS_ERR_BAD_FLAGS;
		break;
	case EBUSY:
		err = CS_ERR_BUSY;
		break;
	case EACCES:
		err = CS_ERR_ACCESS;
		break;
	case ECONNREFUSED:
		err = CS_ERR_SECURITY;
		break;
	case EOVERFLOW:
		err = CS_ERR_NAME_TOO_LONG;
		break;
	case EEXIST:
		err = CS_ERR_EXIST;
		break;
	case ENOBUFS:
		err = CS_ERR_QUEUE_FULL;
		break;
	case ENOSPC:
		err = CS_ERR_NO_SPACE;
		break;
	case EINTR:
		err = CS_ERR_INTERRUPT;
		break;
	case ENOENT:
	case ENODEV:
		err = CS_ERR_NOT_EXIST;
		//err = CS_ERR_NAME_NOT_FOUND;
		//err = CS_ERR_NO_RESOURCES;
		break;
	case ENOSYS:
	case ENOTSUP:
		err = CS_ERR_NOT_SUPPORTED;
		break;
	case EBADMSG:
		err = CS_ERR_MESSAGE_ERROR;
		break;
	case EMSGSIZE:
	case E2BIG:
		err = CS_ERR_TOO_BIG;
		break;
	default:
		err = CS_ERR_LIBRARY;
		break;
	}

	return err;
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
