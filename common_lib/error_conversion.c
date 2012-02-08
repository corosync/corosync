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
	case ETIMEDOUT:
	case EAGAIN:
		err = CS_ERR_TRY_AGAIN;
		break;
	case EBADE:
		err = CS_ERR_FAILED_OPERATION;
		break;
	case ETIME:
		err = CS_ERR_TIMEOUT;
		break;
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
	case ECONNREFUSED:
	case ENOTCONN:
	default:
		err = CS_ERR_LIBRARY;
		break;
	}

	return err;
}


