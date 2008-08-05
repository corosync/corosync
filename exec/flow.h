/*
 * Copyright (c) 2006 Red Hat, Inc.
 *
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

#ifndef FLOW_H_DEFINED
#define FLOW_H_DEFINED

#define COROSYNC_FLOW_CONTROL_STATE
enum corosync_flow_control_state {
	COROSYNC_FLOW_CONTROL_STATE_DISABLED,
	COROSYNC_FLOW_CONTROL_STATE_ENABLED
};

unsigned int openais_flow_control_initialize (void);

unsigned int openais_flow_control_ipc_init (
	unsigned int *flow_control_identifier,
	unsigned int service);

unsigned int openais_flow_control_ipc_exit (
	unsigned int flow_control_identifier);

unsigned int openais_flow_control_create (
	unsigned int flow_control_handle,
	unsigned int service,
	void *id,
	unsigned int id_len,
	void (*flow_control_state_set_fn) (void *context, enum corosync_flow_control_state flow_control_state),
	void *context);

unsigned int openais_flow_control_destroy (
	unsigned int flow_control_identifier,
	unsigned int service,
	unsigned char *id,
	unsigned int id_len);

unsigned int openais_flow_control_disable (
	unsigned int flow_control_identifier);

unsigned int openais_flow_control_enable (
	unsigned int flow_control_identifier);

#endif /* FLOW_H_DEFINED */
