/*
 * Copyright (c) 2008 Allied Telesis Labs.
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld (asalkeld@redhat.com)
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
#include <corosync/corotypes.h>

cs_error_t qb_to_cs_error (int result)
{
	int32_t res;
	cs_error_t err = CS_ERR_LIBRARY;

	if (result >= 0) {
		return CS_OK;
	}
	res = -result;

	switch (res) {
	case EBADF:
		err = CS_ERR_BAD_HANDLE;
		break;
	case ENOMEM:
		err = CS_ERR_NO_MEMORY;
		break;
	case ENOMSG:
	case ENOBUFS:
	case ETIMEDOUT:
	case EAGAIN:
		err = CS_ERR_TRY_AGAIN;
		break;
#ifdef EBADE
	case EBADE:
		err = CS_ERR_FAILED_OPERATION;
		break;
#endif
#ifdef ETIME
	case ETIME:
		err = CS_ERR_TIMEOUT;
		break;
#endif
	case EINVAL:
		err = CS_ERR_INVALID_PARAM;
		break;
	case EBUSY:
		err = CS_ERR_BUSY;
		break;
	case EACCES:
		err = CS_ERR_ACCESS;
		break;
	case EOVERFLOW:
		err = CS_ERR_NAME_TOO_LONG;
		break;
	case EEXIST:
		err = CS_ERR_EXIST;
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
		break;
	case ENOSYS:
	case ENOTSUP:
		err = CS_ERR_NOT_SUPPORTED;
		break;
#ifdef EBADMSG
	case EBADMSG:
		err = CS_ERR_MESSAGE_ERROR;
		break;
#endif
	case EMSGSIZE:
	case E2BIG:
		err = CS_ERR_TOO_BIG;
		break;
	case ECONNREFUSED:
	case ENOTCONN:
	default:
		err = CS_ERR_LIBRARY;
		break;
	}

	return err;
}

cs_error_t hdb_error_to_cs (int res)
{
	if (res == 0) {
		return (CS_OK);
	} else {
		if (res == -EBADF) {
			return (CS_ERR_BAD_HANDLE);
		} else if (res == -ENOMEM) {
			return (CS_ERR_NO_MEMORY);
		} else 	if (res == -EMFILE) {
			return (CS_ERR_NO_RESOURCES);
		} else	if (res == -EACCES) {
			return (CS_ERR_ACCESS);
		}
		return (CS_ERR_LIBRARY);
	}
}

const char * cs_strerror(cs_error_t err)
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
