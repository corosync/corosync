/*
 * Copyright (c) 2002-2003 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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

#ifndef AIS_TYPES_H_DEFINED
#define AIS_TYPES_H_DEFINED

typedef enum {
	SA_FALSE = 0,
	SA_TRUE = 1
} SaBoolT;

#include <stdint.h>

typedef int8_t SaInt8T;
typedef int16_t SaInt16T;
typedef int32_t SaInt32T;
typedef int64_t SaInt64T;

typedef uint8_t SaUint8T;
typedef uint16_t SaUint16T;
typedef uint32_t SaUint32T;
typedef uint64_t SaUint64T;

typedef float SaFloatT;
typedef double SaDoubleT;
typedef char * SaStringT;
typedef SaInt64T SaTimeT;

#define SA_TIME_END ((SaTimeT)0x7fffffffffffffffull)
#define SA_TIME_BEGIN            0x0ULL
#define SA_TIME_UNKNOWN          0x8000000000000000ULL

#define SA_TIME_ONE_MICROSECOND 1000ULL
#define SA_TIME_ONE_MILLISECOND 1000000ULL
#define SA_TIME_ONE_SECOND      1000000000ULL
#define SA_TIME_ONE_MINUTE      60000000000ULL
#define SA_TIME_ONE_HOUR        3600000000000ULL
#define SA_TIME_ONE_DAY         86400000000000ULL
#define SA_TIME_MAX             SA_TIME_END

#define SA_MAX_NAME_LENGTH 256
typedef struct {
	SaUint16T length;
	SaUint8T value[SA_MAX_NAME_LENGTH];
} SaNameT;

typedef struct {
	char releaseCode;
	unsigned char majorVersion;
	unsigned char minorVersion;
} SaVersionT;

typedef SaUint64T SaNtfIdentifierT;

#define SA_TRACK_CURRENT 0x01
#define SA_TRACK_CHANGES 0x02
#define SA_TRACK_CHANGES_ONLY 0x04

typedef enum {
	SA_DISPATCH_ONE = 1,
	SA_DISPATCH_ALL = 2,
	SA_DISPATCH_BLOCKING = 3
} SaDispatchFlagsT;

typedef enum {
	SA_AIS_OK = 1,
	SA_AIS_ERR_LIBRARY = 2,
	SA_AIS_ERR_VERSION = 3,
	SA_AIS_ERR_INIT = 4,
	SA_AIS_ERR_TIMEOUT = 5,
	SA_AIS_ERR_TRY_AGAIN = 6,
	SA_AIS_ERR_INVALID_PARAM = 7,
	SA_AIS_ERR_NO_MEMORY = 8,
	SA_AIS_ERR_BAD_HANDLE = 9,
	SA_AIS_ERR_BUSY = 10,
	SA_AIS_ERR_ACCESS = 11,
	SA_AIS_ERR_NOT_EXIST = 12,
	SA_AIS_ERR_NAME_TOO_LONG = 13,
	SA_AIS_ERR_EXIST = 14,
	SA_AIS_ERR_NO_SPACE = 15,
	SA_AIS_ERR_INTERRUPT = 16,
	SA_AIS_ERR_NAME_NOT_FOUND = 17,
	SA_AIS_ERR_NO_RESOURCES = 18,
	SA_AIS_ERR_NOT_SUPPORTED = 19,
	SA_AIS_ERR_BAD_OPERATION = 20,
	SA_AIS_ERR_FAILED_OPERATION = 21,
	SA_AIS_ERR_MESSAGE_ERROR = 22,
	SA_AIS_ERR_QUEUE_FULL = 23,
	SA_AIS_ERR_QUEUE_NOT_AVAILABLE = 24,
	SA_AIS_ERR_BAD_FLAGS = 25,
	SA_AIS_ERR_TOO_BIG = 26,
	SA_AIS_ERR_NO_SECTIONS = 27
} SaAisErrorT;

typedef SaUint64T SaSelectionObjectT;

typedef SaUint64T SaInvocationT;

typedef SaUint64T SaSizeT;

#define SA_HANDLE_INVALID 0x0ull

#endif /* AIS_TYPES_H_DEFINED */
