/*
 * Copyright (c) 2006-2009 Red Hat, Inc.
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

#ifndef COROIPCS_H_DEFINED
#define COROIPCS_H_DEFINED

#include <stdlib.h>
#include <corosync/engine/objdb.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec;

typedef int (*coroipcs_init_fn_lvalue) (void *conn);
typedef int (*coroipcs_exit_fn_lvalue) (void *conn);
typedef void (*coroipcs_handler_fn_lvalue) (void *conn, const void *msg);

struct coroipcs_init_state {
	const char *socket_name;
	int sched_policy;
	const struct sched_param *sched_param;
	void *(*malloc) (size_t size);
	void (*free) (void *ptr);
        void (*log_printf) (
                const char *format,
                ...) __attribute__((format(printf, 1, 2)));
	int (*service_available)(unsigned int service);
	int (*private_data_size_get)(unsigned int service);
	int (*security_valid)(int uid, int gid);
	void (*serialize_lock)(void);
	void (*serialize_unlock)(void);
	int (*sending_allowed)(unsigned int service, unsigned int id,
		const void *msg, void *sending_allowed_private_data);
	void (*sending_allowed_release)(void *sending_allowed_private_data);
	void (*poll_accept_add)(int fd);
	void (*poll_dispatch_add)(int fd, void *context);
	void (*poll_dispatch_modify)(int fd, int events);
	void (*poll_dispatch_destroy)(int fd, void *context);
	void (*fatal_error)(const char *error_msg) __attribute__ ((noreturn));
	coroipcs_init_fn_lvalue (*init_fn_get)(unsigned int service);
	coroipcs_exit_fn_lvalue (*exit_fn_get)(unsigned int service);
	coroipcs_handler_fn_lvalue (*handler_fn_get)(unsigned int service, unsigned int id);
};

struct coroipcs_init_state_v2 {
	const char *socket_name;
	int sched_policy;
	const struct sched_param *sched_param;
	void *(*malloc) (size_t size);
	void (*free) (void *ptr);
        void (*old_log_printf) (
                const char *format,
                ...) __attribute__((format(printf, 1, 2)));
	int (*service_available)(unsigned int service);
	int (*private_data_size_get)(unsigned int service);
	int (*security_valid)(int uid, int gid);
	void (*serialize_lock)(void);
	void (*serialize_unlock)(void);
	int (*sending_allowed)(unsigned int service, unsigned int id,
		const void *msg, void *sending_allowed_private_data);
	void (*sending_allowed_release)(void *sending_allowed_private_data);
	void (*poll_accept_add)(int fd);
	void (*poll_dispatch_add)(int fd, void *context);
	void (*poll_dispatch_modify)(int fd, int events);
	void (*poll_dispatch_destroy)(int fd, void *context);
	void (*fatal_error)(const char *error_msg) __attribute__ ((noreturn));
	coroipcs_init_fn_lvalue (*init_fn_get)(unsigned int service);
	coroipcs_exit_fn_lvalue (*exit_fn_get)(unsigned int service);
	coroipcs_handler_fn_lvalue (*handler_fn_get)(unsigned int service, unsigned int id);
	/* v2 functions */
	hdb_handle_t (*stats_create_connection) (const char* name,
		pid_t pid, int fd);
	void (*stats_destroy_connection) (hdb_handle_t handle);
	void (*stats_update_value) (hdb_handle_t handle,
		const char *name, const void *value, size_t value_len);
	void (*stats_increment_value) (hdb_handle_t handle, const char* name);
	void (*log_printf) (
		unsigned int rec_ident,
		const char *function_name,
		const char *file_name,
		int file_line,
		const char *format,
		...) __attribute__((format(printf, 5, 6)));
	int log_subsys_id;
	void (*stats_decrement_value) (hdb_handle_t handle, const char* name);
};

extern void coroipcs_ipc_init (
	struct coroipcs_init_state *init_state);

extern void coroipcs_ipc_init_v2 (
        struct coroipcs_init_state_v2 *init_state_v2);

extern void *coroipcs_private_data_get (void *conn);

extern int coroipcs_response_send (
	void *conn,
	const void *msg,
	size_t mlen);

extern int coroipcs_response_iov_send (
	void *conn,
	const struct iovec *iov,
	unsigned int iov_len);

extern int coroipcs_dispatch_send (
	void *conn,
	const void *msg,
	size_t mlen);

extern int coroipcs_dispatch_iov_send (
	void *conn,
	const struct iovec *iov,
	unsigned int iov_len);

extern void coroipcs_refcount_inc (void *conn);

extern void coroipcs_refcount_dec (void *conn);

extern void coroipcs_ipc_exit (void);

extern int coroipcs_ipc_service_exit (unsigned int service);

extern int coroipcs_handler_accept (
	int fd,
	int revent,
	void *context);

extern int coroipcs_handler_dispatch (
	int fd,
	int revent,
	void *context);

#ifdef __cplusplus
}
#endif

#endif /* COROIPCS_H_DEFINED */
