/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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

#ifndef SALCK_H_DEFINED
#define SALCK_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

typedef SaUint64T SaLckHandleT;

typedef SaUint64T SaLckLockIdT;

typedef SaUint64T SaLckResourceHandleT;

#define SA_LCK_LOCK_NO_QUEUE 0x1
#define SA_LCK_LOCK_ORPHAN 0x2

typedef SaUint32T SaLckLockFlagsT;

#define SA_LCK_RESOURCE_CREATE 0x1

typedef SaUint32T SaLckResourceOpenFlagsT;

typedef enum {
	SA_LCK_LOCK_GRANTED = 1,
	SA_LCK_LOCK_DEADLOCK = 2,
	SA_LCK_LOCK_NOT_QUEUED = 3,
	SA_LCK_LOCK_ORPHANED = 4,
	SA_LCK_LOCK_NO_MORE = 5,
	SA_LCK_LOCK_DUPLICATE_EX = 6
} SaLckLockStatusT;

typedef enum {
	SA_LCK_PR_LOCK_MODE = 1,
	SA_LCK_EX_LOCK_MODE = 2
} SaLckLockModeT;

#define SA_LCK_OPT_ORPHAN_LOCKS 0x1
#define SA_LCK_OPT_DEADLOCK_DETECTION 0x2

typedef SaUint32T SaLckOptionsT;

typedef SaUint64T SaLckWaiterSignalT;

typedef void (*SaLckResourceOpenCallbackT) (
	SaInvocationT invocation,
	SaLckResourceHandleT lockResourceHandle,
	SaAisErrorT error);

typedef void (*SaLckLockGrantcallbackT) (
	SaInvocationT invocation,
	SaLckLockStatusT lockStatus,
	SaAisErrorT error);

typedef void (*SaLckLockWaiterCallbackT) (
	SaLckWaiterSignalT waiterSignal,
	SaLckLockIdT lockId,
	SaLckLockModeT modeHeld,
	SaLckLockModeT modeRequested);

typedef void (*SaLckResourceUnlockCallbackT) (
	SaInvocationT invocation,
	SaAisErrorT error);

typedef struct {
	SaLckResourceOpenCallbackT saLckResourceOpenCallback;
	SaLckLockGrantcallbackT saLckLockGrantCallback;
	SaLckLockWaiterCallbackT saLckLockWaiterCallback;
	SaLckResourceUnlockCallbackT saLckResourceUnlockCallback;
} SaLckCallbacksT;

SaAisErrorT
saLckInitialize (
	SaLckHandleT *lckHandle,
	const SaLckCallbacksT *lckCallbacks,
	SaVersionT *version);

SaAisErrorT
saLckSelectionObjectGet (
	SaLckHandleT lckHandle,
	SaSelectionObjectT *selectionObject);

SaAisErrorT
saLckOptionCheck (
	SaLckHandleT lckHandle,
	SaLckOptionsT *lckOptions);

SaAisErrorT
saLckDispatch (
	SaLckHandleT lckHandle,
	SaDispatchFlagsT dispatchFlags);

SaAisErrorT
saLckFinalize (
	SaLckHandleT lckHandle);

SaAisErrorT
saLckResourceOpen (
	SaLckHandleT lckHandle,
	const SaNameT *lockResourceName,
	SaLckResourceOpenFlagsT resourceFlags,
	SaTimeT timeout,
	SaLckResourceHandleT *lockResourceHandle);

SaAisErrorT
saLckResourceOpenAsync (
	SaLckHandleT lckHandle,
	SaInvocationT invocation,
	const SaNameT *lockResourceName,
	SaLckResourceOpenFlagsT resourceFlags);

SaAisErrorT
saLckResourceClose (
	SaLckResourceHandleT lockResourceHandle);

SaAisErrorT
saLckResourceLock (
	SaLckResourceHandleT lockResourceHandle,
	SaLckLockIdT *lockId,
	SaLckLockModeT lockMode,
	SaLckLockFlagsT lockFlags,
	SaLckWaiterSignalT waiterSignal,	
	SaTimeT timeout,
	SaLckLockStatusT *lockStatus);

SaAisErrorT
SaLckResourceLockAsync (
	SaLckResourceHandleT lockResourceHandle,
	SaInvocationT invocation,
	SaLckLockIdT *lockId,
	SaLckLockModeT lockMode,
	SaLckLockFlagsT lockFlags,
	SaLckWaiterSignalT waiterSignal);

SaAisErrorT
saLckResourceUnlock (
	SaLckLockIdT lockId,
	SaTimeT timeout);

SaAisErrorT
saLckResourceUnlockAsync (
	SaInvocationT invocation,	
	SaLckLockIdT lockId);

SaAisErrorT
saLckLockPurge (
	SaLckResourceHandleT lockResourceHandle);

#endif /* SALCK_H_DEFINED */
