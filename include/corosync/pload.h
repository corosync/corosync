/*
 * Copyright (c) 2008 Red Hat, Inc.
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
#ifndef COROSYNC_PLOAD_H_DEFINED
#define COROSYNC_PLOAD_H_DEFINED

#include <sys/types.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup corosync Other API services provided by corosync
 */
/**
 * @addtogroup pload_corosync
 *
 * @{
 */

typedef uint64_t pload_handle_t;

typedef enum {
	PLOAD_OK = 1,
	PLOAD_ERR_LIBRARY = 2,
	PLOAD_ERR_TIMEOUT = 5,
	PLOAD_ERR_TRY_AGAIN = 6,
	PLOAD_ERR_INVALID_PARAM = 7,
	PLOAD_ERR_NO_MEMORY = 8,
	PLOAD_ERR_BAD_HANDLE = 9,
	PLOAD_ERR_ACCESS = 11,
	PLOAD_ERR_NOT_EXIST = 12,
	PLOAD_ERR_EXIST = 14,
	PLOAD_ERR_NOT_SUPPORTED = 20,
	PLOAD_ERR_SECURITY = 29,
	PLOAD_ERR_TOO_MANY_GROUPS=30
} pload_error_t;

typedef struct {
	int callback;
} pload_callbacks_t;

/** @} */

/*
 * Create a new pload connection
 */
pload_error_t pload_initialize (
	pload_handle_t *handle,
	pload_callbacks_t *callbacks);

/*
 * Close the pload handle
 */
pload_error_t pload_finalize (
	pload_handle_t handle);

/*
 * Get a file descriptor on which to poll.  pload_handle_t is NOT a
 * file descriptor and may not be used directly.
 */
pload_error_t pload_fd_get (
	pload_handle_t handle,
	int *fd);

unsigned int pload_start (
        pload_handle_t handle,
        unsigned int code,
        unsigned int msg_count,
        unsigned int msg_size);


#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_PLOAD_H_DEFINED */
