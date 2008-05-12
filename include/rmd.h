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
#ifndef OPENAIS_RMD_H_DEFINED
#define OPENAIS_RMD_H_DEFINED

#include <netinet/in.h>

typedef unsigned int rmd_handle_t;

typedef enum {
	RMD_DISPATCH_ONE,
	RMD_DISPATCH_ALL,
	RMD_DISPATCH_BLOCKING
} rmd_dispatch_t;

typedef enum {
	RMD_OK = 1,
	RMD_ERR_LIBRARY = 2,
	RMD_ERR_TIMEOUT = 5,
	RMD_ERR_TRY_AGAIN = 6,
	RMD_ERR_INVALID_PARAM = 7,
	RMD_ERR_NO_MEMORY = 8,
	RMD_ERR_BAD_HANDLE = 9,
	RMD_ERR_ACCESS = 11,
	RMD_ERR_NOT_EXIST = 12,
	RMD_ERR_EXIST = 14,
	RMD_ERR_TOOBIG = 31
} rmd_error_t;

/*
 * This callback occurs when a synchronized read is executed
 */
typedef void (*rmd_read_synchronized_t) (
	rmd_invocation_t invocation,
	void *key, void key_len,
	void *data_conents, int data_len, int data_start);

/*
 * This callback occurs when a commit is completed after
 * an rmd_commit_async call
 */
typedef void (*rmd_commit_async_t) (
	rmd_invocation_t invocation,
	rmd_error_t error);

typedef struct {
		rmd_read_synchronized_t rmd_read_synchronized;
		rmd_commit_async_t rmd_commit_async;
} rmd_callbacks_t;

/*
 * Create a new replicated memory database connection
 */
rmd_error_t rmd_initialize (
	rmd_handle_t *handle,
	rmd_callbacks_t *callbacks);

/*
 * Close a replicated memory database connection
 */
rmd_error_t rmd_finalize (
	rmd_handle_t *handle);

/*
 * Get a file descriptor on which to poll.  rmd_handle_t is NOT a
 * file descriptor and may not be used directly.
 */
rmd_error_t rmd_fd_get (
	rmd_handle_t *handle,
	int *fd);

/*
 * Dispatch synchronized reads and async commitments
 */
rmd_error_t rmd_dispatch (
	rmd_handle_t *handle,
	rmd_dispatch_t dispatch_types);

/*
 * Abort all updates in the currently outstanding transaction
 */
rmd_error_t rmd_abort (
	rmd_handle_t *handle);

/*
 * Commit all updates for the transaction to the cluster.  Commit
 * blocks until committed data is replicated to all processors.
 */
rmd_error_t rmd_commit (
	rmd_handle_t *handle);

/*
 * Commit all updates for the transaction to the cluster.  Call returns
 * immediately and response of commitment complete (or error) comes in
 * callback 
 */
rmd_error_t rmd_commit_async (
	rmd_handle_t *handle,
	rmd_handle_t *invocation);

/*
 * Read the value for a key without synchronizing within the
 * transaction.  This offers optimal performance.  If rmd_commit has not
 * been called for write operations, rmd_read will only return committed 
 * data.
 */
rmd_error_t rmd_read (
	rmd_handle_t *handle,
	void *key_name, int key_len,
	void *data_contents, int data_contents_size, int data_start,
	int *data_len);

/*
 * Write a key/value pair to the cluster, but only when the trasaction
 * is commited with rmd_commit.  If key/value * doesn't exist create it.
 */
rmd_error_t rmd_write (
	rmd_handle_t *handle,
	void *key_name, int key_len,
	void *data_contents, int data_len, int data_start);
	
/*
 * Read the value for a key and synchronize the read within the
 * transaction.  The read data is deliver by callback.  Callbacks
 * are uniquely identified by invocation which is set by the API.
 */
rmd_error_t rmd_read_synchronized (
	rmd_handle_t *handle,
	rmd_invocation_t *invocation,
	void *key_name, int key_len,
	void *data_contents, int data_contents_size, int data_start,
	int *data_len);

/*
 * Delete a key and value pair.  Operation occurs immediately.
 */
rmd_error_t rmd_delete (
	rmd_handle_t *handle,
	void *key_name, int key_len);

#endif /* OPENAIS_RMD_H_DEFINED */
