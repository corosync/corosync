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

#ifndef _QNETD_INSTANCE_H_
#define _QNETD_INSTANCE_H_

#include <sys/types.h>

#include <certt.h>
#include <keyhi.h>
#include <sys/queue.h>

#include "qnetd-client-list.h"
#include "qnetd-cluster-list.h"
#include "pr-poll-array.h"
#include "qnet-config.h"
#include "timer-list.h"
#include "unix-socket-ipc.h"
#include "qnetd-advanced-settings.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qnetd_instance {
	struct {
		PRFileDesc *socket;
		CERTCertificate *cert;
		SECKEYPrivateKey *private_key;
	} server;
	size_t max_clients;
	struct qnetd_client_list clients;
	struct qnetd_cluster_list clusters;
	struct pr_poll_array poll_array;
	enum tlv_tls_supported tls_supported;
	int tls_client_cert_required;
	const char *host_addr;
	uint16_t host_port;
	struct timer_list main_timer_list;
	struct timer_list_entry *dpd_timer;		/* Dead peer detection timer */
	struct unix_socket_ipc local_ipc;
	PRFileDesc *ipc_socket_poll_fd;
	const struct qnetd_advanced_settings *advanced_settings;
};

extern int		qnetd_instance_init(struct qnetd_instance *instance,
    enum tlv_tls_supported tls_supported, int tls_client_cert_required, size_t max_clients,
    const struct qnetd_advanced_settings *advanced_settings);

extern int		qnetd_instance_destroy(struct qnetd_instance *instance);

extern void		qnetd_instance_client_disconnect(struct qnetd_instance *instance,
    struct qnetd_client *client, int server_going_down);

extern int		qnetd_instance_init_certs(struct qnetd_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_INSTANCE_H_ */
