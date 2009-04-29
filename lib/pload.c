/*
 * Copyright (c) 2008-2009 Red Hat, Inc.
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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/coroipcc.h>
#include <corosync/coroipc_types.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>

#include <corosync/pload.h>
#include <corosync/ipc_pload.h>

#include "util.h"

struct pload_inst {
	hdb_handle_t handle;
	unsigned int finalize;
};

DECLARE_HDB_DATABASE(pload_handle_t_db,NULL);

/**
 * @defgroup pload_corosync The extended virtual synchrony passthrough API
 * @ingroup corosync
 *
 * @{
 */
/**
 * test
 * @param handle The handle of pload initialize
 * @param callbacks The callbacks for pload_initialize
 * @returns PLOAD_OK
 */
unsigned int pload_initialize (
	pload_handle_t *handle,
	pload_callbacks_t *callbacks)
{
	cs_error_t error;
	struct pload_inst *pload_inst;

	error = hdb_error_to_cs(hdb_handle_create (&pload_handle_t_db, sizeof (struct pload_inst), handle));
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_cs(hdb_handle_get (&pload_handle_t_db, *handle, (void *)&pload_inst));
	if (error != CS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		PLOAD_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&pload_inst->handle);
	if (error != CS_OK) {
		goto error_put_destroy;
	}

	(void)hdb_handle_put (&pload_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	(void)hdb_handle_put (&pload_handle_t_db, *handle);
error_destroy:
	(void)hdb_handle_destroy (&pload_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

unsigned int pload_finalize (
	pload_handle_t handle)
{
	struct pload_inst *pload_inst;
	cs_error_t error;

	error = hdb_error_to_cs (hdb_handle_get (&pload_handle_t_db, handle, (void *)&pload_inst));
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (pload_inst->finalize) {
		(void)hdb_handle_put (&pload_handle_t_db, handle);
		return (PLOAD_ERR_BAD_HANDLE);
	}

	pload_inst->finalize = 1;

	coroipcc_service_disconnect(pload_inst->handle);

	(void)hdb_handle_destroy (&pload_handle_t_db, handle);

	(void)hdb_handle_put (&pload_handle_t_db, handle);

	return (PLOAD_OK);
}

unsigned int pload_fd_get (
	pload_handle_t handle,
	int *fd)
{
	cs_error_t error;
	struct pload_inst *pload_inst;

	error = hdb_error_to_cs(hdb_handle_get (&pload_handle_t_db, handle, (void *)&pload_inst));
	if (error != CS_OK) {
		return (error);
	}

	coroipcc_fd_get (pload_inst->handle, fd);

	(void)hdb_handle_put (&pload_handle_t_db, handle);

	return (CS_OK);
}

unsigned int pload_start (
	pload_handle_t handle,
	unsigned int code,
	unsigned int msg_count,
	unsigned int msg_size)
{
	unsigned int error;
	struct pload_inst *pload_inst;
	struct iovec iov;
	struct req_lib_pload_start req_lib_pload_start;
	struct res_lib_pload_start res_lib_pload_start;

	error = hdb_error_to_cs(hdb_handle_get (&pload_handle_t_db, handle, (void *)&pload_inst));
	if (error != CS_OK) {
		return (error);
	}

	req_lib_pload_start.header.size = sizeof (struct req_lib_pload_start);
	req_lib_pload_start.header.id = MESSAGE_REQ_PLOAD_START;
	req_lib_pload_start.msg_code = code;
	req_lib_pload_start.msg_count = msg_count;
	req_lib_pload_start.msg_size = msg_size;

	iov.iov_base = (char *)&req_lib_pload_start;
	iov.iov_len = sizeof (struct req_lib_pload_start);

	error = coroipcc_msg_send_reply_receive(pload_inst->handle,
		&iov,
		1,
		&res_lib_pload_start,
		sizeof (struct res_lib_pload_start));

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_pload_start.header.error;

error_exit:
	(void)hdb_handle_put (&pload_handle_t_db, handle);

	return (error);
}

/** @} */
