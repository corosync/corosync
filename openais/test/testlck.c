/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "saAis.h"
#include "saLck.h"

SaNameT resource_name_async;
SaLckResourceHandleT resource_handle_async;

void testLckResourceOpenCallback (
	SaInvocationT invocation,
	SaLckResourceHandleT lockResourceHandle,
	SaAisErrorT error)
{
	printf ("testLckResourceOpenCallback invocation %llu error %d\n",
		(unsigned long long)invocation, error);
	resource_handle_async = lockResourceHandle;
}

void testLckLockGrantCallback (
	SaInvocationT invocation,
	SaLckLockStatusT lockStatus,
	SaAisErrorT error)
{
	printf ("testLckLockGrantCallback invocation %llu status %d error %d\n",
		(unsigned long long)invocation, lockStatus, error);
}

SaLckLockIdT pr_lock_id;
SaLckLockIdT pr_lock_async_id;

void testLckLockWaiterCallback (
        SaLckWaiterSignalT waiterSignal,
        SaLckLockIdT lockId,
        SaLckLockModeT modeHeld,
        SaLckLockModeT modeRequested)
{
	int result;
	printf ("waiter callback mode held %d mode requested %d lock id %llu\n",
		modeHeld,
		modeRequested,
		(unsigned long long)lockId);
	printf ("pr lock id %llu\n", (unsigned long long)pr_lock_async_id);
	result = saLckResourceUnlockAsync (
		25,
		lockId);
	printf ("saLckResourceUnlockAsync result %d (should be 1)\n", result);
}

void testLckResourceUnlockCallback (
        SaInvocationT invocation,
        SaAisErrorT error)
{
	printf ("testLckResourceUnlockCallback async invocation %llu error %d\n",
		(unsigned long long)invocation, error);
}

SaLckCallbacksT callbacks = {
	.saLckResourceOpenCallback	= testLckResourceOpenCallback,
	.saLckLockGrantCallback		= testLckLockGrantCallback,
	.saLckLockWaiterCallback	= testLckLockWaiterCallback,
	.saLckResourceUnlockCallback	= testLckResourceUnlockCallback
};

SaVersionT version = { 'B', 1, 1 };

void setSaNameT (SaNameT *name, char *str) {
	strncpy ((char *)name->value, str, SA_MAX_NAME_LENGTH);
	if (strlen ((char *)name->value) > SA_MAX_NAME_LENGTH) {
		name->length = SA_MAX_NAME_LENGTH;
	} else {
		name->length = strlen (str);
	}
}

void sigintr_handler (int signum) {
	exit (0);
}

struct th_data {
	SaLckHandleT handle;
};

void *th_dispatch (void *arg)
{
	struct th_data *th_data = (struct th_data *)arg;

	saLckDispatch (th_data->handle, SA_DISPATCH_BLOCKING);
	return (0);
}

int main (void) {
	SaLckHandleT handle;
	SaLckResourceHandleT resource_handle;
	int result;
	SaLckLockIdT ex_lock_id;
	SaLckLockStatusT status;
	SaNameT resource_name;
	pthread_t dispatch_thread;
	fd_set read_fds;
	struct th_data th_data;

	signal (SIGINT, sigintr_handler);

	result = saLckInitialize (&handle, &callbacks, &version);
	if (result != SA_AIS_OK) {
		printf ("Could not initialize Lock Service API instance error %d\n", result);
		exit (1);
	}
	printf ("saLckInitialize result is %d (should be 1)\n", result);

	th_data.handle = handle;
	pthread_create (&dispatch_thread, NULL, th_dispatch, &th_data);

	setSaNameT (&resource_name_async, "test_resource_async");

	setSaNameT (&resource_name, "not_existingaabb");
	result = saLckResourceOpen (
		handle,
		&resource_name,
//		SA_LCK_RESOURCE_CREATE,
		0,
		SA_TIME_ONE_SECOND,
		&resource_handle);
	printf ("saLckResourceOpen %d (should be 12)\n", result);

	result = saLckResourceClose (resource_handle);
	printf ("saLckResourceClose %d (should be 9)\n", result);

	setSaNameT (&resource_name, "test_resource");
	result = saLckResourceOpen (
		handle,
		&resource_name,
		SA_LCK_RESOURCE_CREATE,
		SA_TIME_ONE_SECOND,
		&resource_handle);
	printf ("saLckResourceOpen %d (should be 1)\n", result);

	result = saLckResourceOpenAsync (
		handle,
		(SaInvocationT)0x56,
		&resource_name_async,
		SA_LCK_RESOURCE_CREATE);
	printf ("saLckResourceOpenAsync %d (should be 1)\n", result);
		
	result = saLckResourceLock (
		resource_handle,
		&pr_lock_id,
		SA_LCK_PR_LOCK_MODE,
		SA_LCK_LOCK_ORPHAN,
		55,
		SA_TIME_END,
		&status);
	printf ("saLckResourceLock PR %d (should be 1)\n", result);
	printf ("status %d\n", status);

	result = saLckResourceUnlock (
		pr_lock_id,
		SA_TIME_END);
	printf ("saLckResourceUnlock result %d (should be 1)\n", result);

	result = saLckResourceLock (
			resource_handle,
			&pr_lock_id,
			SA_LCK_PR_LOCK_MODE,
			SA_LCK_LOCK_ORPHAN,
			55,
			SA_TIME_END,
			&status);
	printf ("saLckResourceLock PR %d (should be 1)\n", result);
	printf ("status %d\n", status);

	result = saLckResourceLock (
		resource_handle,
		&ex_lock_id,
		SA_LCK_EX_LOCK_MODE,
		0,
		55,
		SA_TIME_END,
		&status);
	printf ("saLckResourceLock EX %d (should be 1)\n", result);
	printf ("status %d\n", status);
		
	result = saLckResourceLockAsync (
		resource_handle_async,
		0x56,
		&pr_lock_async_id,
		SA_LCK_PR_LOCK_MODE,
		0,
		55);
	printf ("saLckResourceLockAsync PR %d (should be 1)\n", result);
	printf ("status %d\n", status);
	printf ("press the enter key to exit\n");
	FD_ZERO (&read_fds);
	do {
		FD_SET (STDIN_FILENO, &read_fds);
		result = select (STDIN_FILENO + 1, &read_fds, 0, 0, 0);
		if (FD_ISSET (STDIN_FILENO, &read_fds)) {
			break;
		}
	} while (result);
	
	printf ("Starting saLckResourceUnlock\n");
	result = saLckResourceUnlock (
		ex_lock_id,
		SA_TIME_END);
	printf ("saLckResourceUnlock result %d (should be 1)\n",
		result);
	
	result = saLckResourceClose (resource_handle);
	printf ("saLckResourceClose result %d (should be 1)\n", result);

	result = saLckFinalize (handle);
	printf ("saLckFinalize %d (should be 1)\n", result);
	return (0);
}
