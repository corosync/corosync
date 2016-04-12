/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#ifndef _QNETD_CLIENT_LIST_H_
#define _QNETD_CLIENT_LIST_H_

#include <sys/types.h>
#include <inttypes.h>

#include "qnetd-client.h"

#ifdef __cplusplus
extern "C" {
#endif

TAILQ_HEAD(qnetd_client_list, qnetd_client);

extern void			 qnetd_client_list_init(struct qnetd_client_list *client_list);

extern struct qnetd_client	*qnetd_client_list_add(struct qnetd_client_list *client_list,
    PRFileDesc *sock, PRNetAddr *addr, char *addr_str, size_t max_receive_size,
    size_t max_send_buffers, size_t max_send_size, struct timer_list *main_timer_list);

extern void			 qnetd_client_list_free(struct qnetd_client_list *client_list);

extern void			 qnetd_client_list_del(struct qnetd_client_list *client_list,
    struct qnetd_client *client);

extern size_t			 qnetd_client_list_no_clients(
    struct qnetd_client_list *client_list);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_CLIENT_LIST_H_ */
