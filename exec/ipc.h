/*
 * Copyright (c) 2006-2007 Red Hat, Inc.
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

#ifndef IPC_H_DEFINED
#define IPC_H_DEFINED

#ifndef TIMER_HANDLE
typedef void * timer_handle;
#define TIMER_HANDLE
#endif

extern void message_source_set (mar_message_source_t *source, void *conn);

extern int message_source_is_local (mar_message_source_t *source);

extern void *openais_conn_partner_get (void *conn);

extern void *openais_conn_private_data_get (void *conn);

extern int openais_conn_send_response (void *conn, void *msg, int mlen);

extern void openais_ipc_init (
        void (*serialize_lock_fn) (void),
        void (*serialize_unlock_fn) (void),
	unsigned int gid_valid);

extern int openais_ipc_timer_add (
	void *conn,
	void (*timer_fn) (void *data),
	void *data,
	unsigned int msec_in_future,
	timer_handle *handle);

extern void openais_ipc_timer_del (
	void *conn,
	timer_handle timer_handle);

extern void openais_ipc_timer_del_data (
	void *conn,
	timer_handle timer_handle);

extern void openais_ipc_flow_control_create (
	void *conn,
	unsigned int service,
	char *id,
	int id_len,
	void (*flow_control_state_set_fn) (void *context, enum openais_flow_control_state flow_control_state_set),
	void *context);
	
extern void openais_ipc_flow_control_destroy (
	void *conn,
	unsigned int service,
	unsigned char *id,
	int id_len);

extern void openais_ipc_flow_control_local_increment (
	void *conn);

extern void openais_ipc_flow_control_local_decrement (
	void *conn);

#endif /* IPC_H_DEFINED */
