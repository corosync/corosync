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

#include <sys/types.h>

#include <pk11func.h>
#include "qnetd-instance.h"
#include "qnetd-client.h"
#include "qnetd-algorithm.h"
#include "qnetd-log-debug.h"
#include "qnetd-dpd-timer.h"
#include "qnetd-poll-array-user-data.h"
#include "qnetd-client-algo-timer.h"

int
qnetd_instance_init(struct qnetd_instance *instance,
    enum tlv_tls_supported tls_supported, int tls_client_cert_required, size_t max_clients,
    const struct qnetd_advanced_settings *advanced_settings)
{

	memset(instance, 0, sizeof(*instance));

	instance->advanced_settings = advanced_settings;

	pr_poll_array_init(&instance->poll_array, sizeof(struct qnetd_poll_array_user_data));
	qnetd_client_list_init(&instance->clients);
	qnetd_cluster_list_init(&instance->clusters);

	instance->tls_supported = tls_supported;
	instance->tls_client_cert_required = tls_client_cert_required;

	instance->max_clients = max_clients;

	timer_list_init(&instance->main_timer_list);

	if (qnetd_dpd_timer_init(instance) != 0) {
		return (0);
	}

	return (0);
}

int
qnetd_instance_destroy(struct qnetd_instance *instance)
{
	struct qnetd_client *client;
	struct qnetd_client *client_next;

	qnetd_dpd_timer_destroy(instance);

	client = TAILQ_FIRST(&instance->clients);
	while (client != NULL) {
		client_next = TAILQ_NEXT(client, entries);

		qnetd_instance_client_disconnect(instance, client, 1);

		client = client_next;
	}

	pr_poll_array_destroy(&instance->poll_array);
	qnetd_cluster_list_free(&instance->clusters);
	qnetd_client_list_free(&instance->clients);
	timer_list_free(&instance->main_timer_list);

	return (0);
}

void
qnetd_instance_client_disconnect(struct qnetd_instance *instance, struct qnetd_client *client,
    int server_going_down)
{

	qnetd_log_debug_client_disconnect(client, server_going_down);

	if (client->init_received) {
		qnetd_algorithm_client_disconnect(client, server_going_down);
	}

	PR_Close(client->socket);
	if (client->cluster != NULL) {
		qnetd_cluster_list_del_client(&instance->clusters, client->cluster, client);
	}
	qnetd_client_algo_timer_abort(client);
	qnetd_client_list_del(&instance->clients, client);
}

int
qnetd_instance_init_certs(struct qnetd_instance *instance)
{

	instance->server.cert = PK11_FindCertFromNickname(
	    instance->advanced_settings->cert_nickname, NULL);
	if (instance->server.cert == NULL) {
		return (-1);
	}

	instance->server.private_key = PK11_FindKeyByAnyCert(instance->server.cert, NULL);
	if (instance->server.private_key == NULL) {
		return (-1);
	}

	return (0);
}
