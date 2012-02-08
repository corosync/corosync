/*
 * Copyright (c) 2008 Allied Telesis Labs.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld (ahsalkeld@gmail.com)
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

#ifndef COROTYPES_H_DEFINED
#define COROTYPES_H_DEFINED

#ifndef COROSYNC_SOLARIS
#include <stdint.h>
#else
#include <sys/types.h>
#endif
#include <errno.h>
#include <time.h>
#include <sys/time.h>

typedef int64_t cs_time_t;

#define CS_FALSE 0
#define CS_TRUE !CS_FALSE
#define CS_MAX_NAME_LENGTH 256
#define CS_TIME_END    ((cs_time_t)0x7FFFFFFFFFFFFFFFULL)
#define CS_MAX(x, y) (((x) > (y)) ? (x) : (y))

typedef struct {
   uint16_t length;
   uint8_t value[CS_MAX_NAME_LENGTH];
} cs_name_t;

typedef struct {
   char releaseCode;
   unsigned char majorVersion;
   unsigned char minorVersion;
} cs_version_t;

typedef enum {
	CS_DISPATCH_ONE = 1,
	CS_DISPATCH_ALL = 2,
	CS_DISPATCH_BLOCKING = 3,
	CS_DISPATCH_ONE_NONBLOCKING = 4
} cs_dispatch_flags_t;

#define CS_TRACK_CURRENT 0x01
#define CS_TRACK_CHANGES 0x02
#define CS_TRACK_CHANGES_ONLY 0x04

typedef enum {
   CS_OK = 1,
   CS_ERR_LIBRARY = 2,
   CS_ERR_VERSION = 3,
   CS_ERR_INIT = 4,
   CS_ERR_TIMEOUT = 5,
   CS_ERR_TRY_AGAIN = 6,
   CS_ERR_INVALID_PARAM = 7,
   CS_ERR_NO_MEMORY = 8,
   CS_ERR_BAD_HANDLE = 9,
   CS_ERR_BUSY = 10,
   CS_ERR_ACCESS = 11,
   CS_ERR_NOT_EXIST = 12,
   CS_ERR_NAME_TOO_LONG = 13,
   CS_ERR_EXIST = 14,
   CS_ERR_NO_SPACE = 15,
   CS_ERR_INTERRUPT = 16,
   CS_ERR_NAME_NOT_FOUND = 17,
   CS_ERR_NO_RESOURCES = 18,
   CS_ERR_NOT_SUPPORTED = 19,
   CS_ERR_BAD_OPERATION = 20,
   CS_ERR_FAILED_OPERATION = 21,
   CS_ERR_MESSAGE_ERROR = 22,
   CS_ERR_QUEUE_FULL = 23,
   CS_ERR_QUEUE_NOT_AVAILABLE = 24,
   CS_ERR_BAD_FLAGS = 25,
   CS_ERR_TOO_BIG = 26,
   CS_ERR_NO_SECTIONS = 27,
   CS_ERR_CONTEXT_NOT_FOUND = 28,
   CS_ERR_TOO_MANY_GROUPS = 30,
   CS_ERR_SECURITY = 100
} cs_error_t;

#define CS_IPC_TIMEOUT_MS 1000

#define CS_TIME_MS_IN_SEC   1000ULL
#define CS_TIME_US_IN_SEC   1000000ULL
#define CS_TIME_NS_IN_SEC   1000000000ULL
#define CS_TIME_US_IN_MSEC  1000ULL
#define CS_TIME_NS_IN_MSEC  1000000ULL
#define CS_TIME_NS_IN_USEC  1000ULL
static inline uint64_t cs_timestamp_get(void)
{
	uint64_t result;

#if defined _POSIX_MONOTONIC_CLOCK && _POSIX_MONOTONIC_CLOCK >= 0
	struct timespec ts;

	clock_gettime (CLOCK_MONOTONIC, &ts);
	result = (ts.tv_sec * CS_TIME_NS_IN_SEC) + (uint64_t)ts.tv_nsec;
#else
	struct timeval time_from_epoch;

	gettimeofday (&time_from_epoch, 0);
	result = ((time_from_epoch.tv_sec * CS_TIME_NS_IN_SEC) +
		(time_from_epoch.tv_usec * CS_TIME_NS_IN_USEC));
#endif

	return result;
}

static inline cs_error_t qb_to_cs_error (int result)
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

/*
 * static inline so multiple libraries can link into same binary
 */
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
#endif /* COROTYPES_H_DEFINED */

